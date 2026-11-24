// Vidéos promo RAW 2 (ABRASION) : scène 3D et montage, rendus image par image.
// scene.html?ad=lancement|le-son&w=1080&h=1920 ; window.renderFrame(t) dessine l'instant t (s).
// Tout est déterministe : une même image donne toujours le même rendu.
import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { Pass, FullScreenQuad } from 'three/addons/postprocessing/Pass.js';
import { CopyShader } from 'three/addons/shaders/CopyShader.js';
import { makeSynth } from './synth3d.js';
import { makeBonhomme, POSE } from './bonhomme.js';

const Q = new URLSearchParams(location.search);
const AD = Q.get('ad') || 'lancement';
const W = +(Q.get('w') || 1080), H = +(Q.get('h') || 1920);
const FPS = +(Q.get('fps') || 30);
const ASSETS = Q.get('assets') || '../build/promo-assets/';
const PORTRAIT = W / H < 0.8;
const U = Math.min(W, H) / 1080;
document.documentElement.style.setProperty('--u', U + 'px');

// ---------------------------------------------------------------- outils
const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const lerp = (a, b, t) => a + (b - a) * t;
const prog = (t, a, b) => clamp((t - a) / (b - a));
const eIO = t => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
const eOut = t => 1 - Math.pow(1 - t, 3);
const eBack = t => { const c1 = 1.70158, c3 = c1 + 1; return 1 + c3 * Math.pow(t - 1, 3) + c1 * Math.pow(t - 1, 2); };
const v3 = (x, y, z) => new THREE.Vector3(x, y, z);
const vl = (a, b, t) => a.clone().lerp(b, t);
function rng(seed) {
    return () => {
        seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
        let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}
const HOT = new THREE.Color(3.2, 0.95, 0.06);  // orange #FF4D00 « HDR » : seul ce qui dépasse 1 brille (bloom)
const ACC = new THREE.Color(0xff4d00);
const $ = id => document.getElementById(id);

// ---------------------------------------------------------------- données
const T = await (await fetch('./textes.json')).json();
const scoreTxt = await (await fetch(`./${AD}.txt`)).text();
const wurli = await (await fetch(ASSETS + 'wurli.json')).json();
const THEMES = await (await fetch('../resources/data/themes.json')).json();
const TEXTES = T;
const HIST = AD === 'histoire' ? await (await fetch('./histoire.json')).json() : null;
for (const f of ['PixB', 'Pix', 'Mono', 'MonoM', 'MonoB']) await document.fonts.load(`20px ${f}`);
await document.fonts.ready;

// coups de 808 (piste « factory 5 ») tirés de la partition : la caméra et les lumières tapent dessus
function scoreHits(txt) {
    let bpm = 120, cur = '', hits = [];
    for (const raw of txt.split('\n')) {
        const l = raw.split('#')[0].trim();
        if (!l) continue;
        const a = l.split(/\s+/);
        if (a[0] === 'bpm') bpm = +a[1];
        else if (a[0] === 'track') cur = a.slice(2).join(' ');
        else if (a[0] === 'n' && cur === 'factory 5') hits.push({ t: +a[1] * 60 / bpm, v: +(a[4] || 0.8) });
    }
    return { bpm, hits };
}
// musique externe (pick_segment.py) : le montage, écrit à 120 BPM, suit son tempo
const META = await fetch(ASSETS + AD + '-meta.json').then(r => (r.ok ? r.json() : null)).catch(() => null);
const K = META && META.bpm && META.warp !== false ? META.bpm / 120 : 1;   // temps du montage = temps réel × K
let REALT = 0;
const SCORE = META && META.external ? { bpm: 120, hits: [] } : scoreHits(scoreTxt);
const AUD0 = await (await fetch(ASSETS + AD + '-audio.json')).json();
if (!SCORE.hits.length && META && META.bpm) {
    // musique externe : l'extrait commence sur une mesure, on tape sur chaque temps avec la force de
    // l'attaque mesurée là (faible dans les silences et les breaks), plus les gros coups de grave
    const beat = 60 / META.bpm, f = AUD0.flux, lo = AUD0.low, k = AUD0.kick;
    for (let b = 0; b * beat < AUD0.n / AUD0.fps; ++b) {
        const tb = b * beat + (META.phase || 0);
        if (tb < 0) continue;
        const i = Math.round(tb * AUD0.fps);
        let s = 0;
        for (let j = Math.max(0, i - 2); j <= Math.min(f.length - 1, i + 2); ++j) s = Math.max(s, f[j], k[j]);
        const v = Math.min(1, 0.3 + 0.9 * s) * Math.min(1, (lo[i] || 0) * 1.4);
        if (v > 0.28) SCORE.hits.push({ t: tb, v: b % 4 === 0 ? Math.min(1, v * 1.15) : v * 0.85 });
    }
} else if (!SCORE.hits.length) {  // sans tempo connu : pics de l'attaque basse
    const k = AUD0.kick;
    for (let i = 1; i < k.length - 1; ++i) if (k[i] > 0.45 && k[i] >= k[i - 1] && k[i] > k[i + 1]) SCORE.hits.push({ t: i / AUD0.fps, v: Math.min(1, k[i] + 0.2) });
}
const hitEnv = (t, k = 16) => {
    let s = 0;
    for (const h of SCORE.hits) if (t >= h.t) s = Math.max(s, h.v * Math.exp(-(t - h.t) * k));
    return s;
};

// analyse de la musique image par image (audio_features.py) : graves, médiums, aigus, attaques, spectre
const AUD = AUD0;
function aud(t) {
    const x = clamp(t * AUD.fps, 0, AUD.n - 1.001), i = Math.floor(x), f = x - i;
    const g = k => lerp(AUD[k][i], AUD[k][i + 1], f);
    return { rms: g('rms'), low: g('low'), mid: g('mid'), high: g('high'), flux: g('flux'), bands: AUD.bands[Math.round(x)] };
}
let A = aud(0);   // valeurs de l'image en cours
let KICK = 0, KICK9 = 0, KICK16 = 0;   // enveloppes des coups de grosse caisse (temps réel)

// bruit simplex 3D (Ashima / Ian McEwan, licence MIT) pour les shaders
const SNOISE = `
vec3 mod289(vec3 x){return x-floor(x*(1./289.))*289.;} vec4 mod289(vec4 x){return x-floor(x*(1./289.))*289.;}
vec4 permute(vec4 x){return mod289(((x*34.)+1.)*x);} vec4 taylorInvSqrt(vec4 r){return 1.79284291400159-.85373472095314*r;}
float snoise(vec3 v){const vec2 C=vec2(1./6.,1./3.);const vec4 D=vec4(0.,.5,1.,2.);
 vec3 i=floor(v+dot(v,C.yyy));vec3 x0=v-i+dot(i,C.xxx);vec3 g=step(x0.yzx,x0.xyz);vec3 l=1.-g;
 vec3 i1=min(g.xyz,l.zxy);vec3 i2=max(g.xyz,l.zxy);vec3 x1=x0-i1+C.xxx;vec3 x2=x0-i2+C.yyy;vec3 x3=x0-D.yyy;
 i=mod289(i);vec4 p=permute(permute(permute(i.z+vec4(0.,i1.z,i2.z,1.))+i.y+vec4(0.,i1.y,i2.y,1.))+i.x+vec4(0.,i1.x,i2.x,1.));
 float n_=.142857142857;vec3 ns=n_*D.wyz-D.xzx;vec4 j=p-49.*floor(p*ns.z*ns.z);vec4 x_=floor(j*ns.z);vec4 y_=floor(j-7.*x_);
 vec4 x=x_*ns.x+ns.yyyy;vec4 y=y_*ns.x+ns.yyyy;vec4 h=1.-abs(x)-abs(y);vec4 b0=vec4(x.xy,y.xy);vec4 b1=vec4(x.zw,y.zw);
 vec4 s0=floor(b0)*2.+1.;vec4 s1=floor(b1)*2.+1.;vec4 sh=-step(h,vec4(0.));vec4 a0=b0.xzyw+s0.xzyw*sh.xxyy;vec4 a1=b1.xzyw+s1.xzyw*sh.zzww;
 vec3 p0=vec3(a0.xy,h.x);vec3 p1=vec3(a0.zw,h.y);vec3 p2=vec3(a1.xy,h.z);vec3 p3=vec3(a1.zw,h.w);
 vec4 norm=taylorInvSqrt(vec4(dot(p0,p0),dot(p1,p1),dot(p2,p2),dot(p3,p3)));p0*=norm.x;p1*=norm.y;p2*=norm.z;p3*=norm.w;
 vec4 m=max(.6-vec4(dot(x0,x0),dot(x1,x1),dot(x2,x2),dot(x3,x3)),0.);m=m*m;
 return 42.*dot(m*m,vec4(dot(p0,x0),dot(p1,x1),dot(p2,x2),dot(p3,x3)));}
`;
const HASH = `float hash(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vnoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);}`;

// ---------------------------------------------------------------- rendu
const canvas = $('gl');
canvas.width = W; canvas.height = H;
const renderer = new THREE.WebGLRenderer({ canvas, antialias: false, preserveDrawingBuffer: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(1);
renderer.setSize(W, H, false);
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.NoToneMapping;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x070707);
scene.fog = new THREE.Fog(0x070707, 45, 150);
const pmrem = new THREE.PMREMGenerator(renderer);
scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
scene.environmentIntensity = 0.55;

const camera = new THREE.PerspectiveCamera(PORTRAIT ? 50 : 38, W / H, 0.1, 500);
const rt = new THREE.WebGLRenderTarget(W, H, { type: THREE.HalfFloatType, samples: 4 });
const composer = new EffectComposer(renderer, rt);
composer.setPixelRatio(1);
composer.setSize(W, H);
composer.addPass(new RenderPass(scene, camera));

// feedback (comme le Feedback TOP de TouchDesigner) : l'image précédente, zoomée, tournée, déplacée
// par du bruit et assombrie, sous l'image courante ; les traînées tirent vers l'orange
class FeedbackPass extends Pass {
    constructor() {
        super();
        this.prev = new THREE.WebGLRenderTarget(W, H, { type: THREE.HalfFloatType });
        this.mat = new THREE.ShaderMaterial({
            uniforms: { tCur: { value: null }, tPrev: { value: this.prev.texture }, uDecay: { value: 0 }, uZoom: { value: 0 },
                        uRot: { value: 0 }, uDisp: { value: 0 }, uTime: { value: 0 } },
            vertexShader: CopyShader.vertexShader,
            fragmentShader: HASH + `uniform sampler2D tCur, tPrev; uniform float uDecay, uZoom, uRot, uDisp, uTime; varying vec2 vUv;
              void main(){
                vec2 c = vUv - .5; float cs = cos(uRot), sn = sin(uRot);
                c = mat2(cs, -sn, sn, cs) * c * (1. - uZoom);
                vec2 n = vec2(vnoise(vUv * 6. + uTime * .5), vnoise(vUv * 6. + 17. - uTime * .5)) - .5;
                vec4 p = texture2D(tPrev, c + .5 + n * uDisp) * uDecay * vec4(1., .86, .72, 1.);
                gl_FragColor = max(texture2D(tCur, vUv), p);
              }`,
        });
        this.quad = new FullScreenQuad(this.mat);
        this.copy = new FullScreenQuad(new THREE.ShaderMaterial(CopyShader));
    }
    render(renderer, writeBuffer, readBuffer) {
        this.mat.uniforms.tCur.value = readBuffer.texture;
        renderer.setRenderTarget(this.renderToScreen ? null : writeBuffer);
        this.quad.render(renderer);
        this.copy.material.uniforms.tDiffuse.value = writeBuffer.texture;
        renderer.setRenderTarget(this.prev);
        this.copy.render(renderer);
    }
}
const feedback = new FeedbackPass();
composer.addPass(feedback);
const bloom = new UnrealBloomPass(new THREE.Vector2(W, H), 0.85, 0.55, 1.0);
composer.addPass(bloom);
// glitch : tranches décalées, déplacement par bruit, aberration chromatique, légère courbure,
// et trame pixel (dither de Bayer noir / orange / blanc) sur les coupes
const fxPass = new (class extends Pass {
    constructor() {
        super();
        this.mat = new THREE.ShaderMaterial({
            uniforms: { tDiffuse: { value: null }, uCA: { value: 0 }, uGlitch: { value: 0 }, uSeed: { value: 0 }, uDisp: { value: 0 },
                        uTime: { value: 0 }, uDither: { value: 0 }, uWarp: { value: 0 }, uPix: { value: 2 } },
            vertexShader: CopyShader.vertexShader,
            fragmentShader: HASH + `uniform sampler2D tDiffuse; uniform float uCA, uGlitch, uSeed, uDisp, uTime, uDither, uWarp, uPix; varying vec2 vUv;
              float bayer(vec2 p){ p = mod(floor(p), 4.); int i = int(p.x + p.y * 4.);
                float m[16] = float[16](0.,8.,2.,10.,12.,4.,14.,6.,3.,11.,1.,9.,15.,7.,13.,5.); return m[i] / 16.; }
              void main(){
                vec2 uv = vUv; vec2 d = uv - .5; uv = .5 + d * (1. + uWarp * dot(d, d));
                float band = floor(uv.y * 54.); float h = hash(vec2(band, uSeed));
                if (h > 1. - uGlitch * .55) uv.x += (hash(vec2(band, uSeed + 3.)) - .5) * uGlitch * .22;
                uv += (vec2(vnoise(uv * 4. + uTime), vnoise(uv * 4. + 9. - uTime)) - .5) * uDisp;
                vec2 off = (uv - .5) * uCA;
                vec3 col = vec3(texture2D(tDiffuse, uv + off).r, texture2D(tDiffuse, uv).g, texture2D(tDiffuse, uv - off).b);
                if (uDither > 0.) {
                  vec2 cell = floor(gl_FragCoord.xy / uPix);
                  float l = dot(min(col, vec3(1.5)), vec3(.299, .587, .114)); float b = bayer(cell);
                  vec3 dc = l > .62 + b * .5 ? vec3(1., .96, .9) : (l > .08 + b * .3 ? vec3(1., .3, 0.) : vec3(.02));
                  col = mix(col, dc, uDither);
                }
                gl_FragColor = vec4(col, 1.);
              }`,
        });
        this.quad = new FullScreenQuad(this.mat);
    }
    render(renderer, writeBuffer, readBuffer) {
        this.mat.uniforms.tDiffuse.value = readBuffer.texture;
        renderer.setRenderTarget(this.renderToScreen ? null : writeBuffer);
        this.quad.render(renderer);
    }
})();
composer.addPass(fxPass);
composer.addPass(new OutputPass());
// réglages des effets pour l'image en cours (chaque plan les modifie)
let FX;
function fxDefaults(t, frame) {
    FX = { decay: 0, zoom: 0, rot: 0, disp: 0, ca: 0.0016 + 0.012 * KICK + 0.004 * A.high, glitch: Math.max(0, A.flux - 0.82) * 2.2,
           fdisp: 0.003 * KICK, dither: 0, warp: 0.05, bloom: 0.62 + 0.55 * A.low, seed: frame };
}
// coupe franche : deux images en trame pixel + glitch, et le feedback repart de zéro
function cutFx(t, cuts) {
    for (const c of cuts) {
        const d = t - c;
        if (d >= 0 && d < 0.075) { FX.dither = 1; FX.glitch = Math.max(FX.glitch, 0.9); FX.ca = 0.03; }
        if (d >= 0 && d < 1 / FPS) FX.decay = 0;
    }
}
function applyFx(t) {
    const f = feedback.mat.uniforms, x = fxPass.mat.uniforms;
    f.uDecay.value = FX.decay; f.uZoom.value = FX.zoom; f.uRot.value = FX.rot; f.uDisp.value = FX.disp; f.uTime.value = t;
    x.uCA.value = FX.ca; x.uGlitch.value = clamp(FX.glitch); x.uSeed.value = FX.seed % 97; x.uDisp.value = FX.fdisp;
    x.uTime.value = t; x.uDither.value = FX.dither; x.uWarp.value = FX.warp; x.uPix.value = Math.max(2, Math.round(3 * U));
    bloom.strength = FX.bloom;
}

// distance de caméra pour qu'un objet de largeur w (et hauteur h) tienne dans l'image
function fitDist(w, h = 0) {
    const tv = Math.tan(THREE.MathUtils.degToRad(camera.fov / 2));
    return Math.max(w / 2 / (tv * camera.aspect), h / 2 / tv);
}
function setCam(pos, look, roll = 0) {
    camera.position.copy(pos);
    camera.up.set(Math.sin(roll), Math.cos(roll), 0);
    camera.lookAt(look);
}

// lumières : clé blanche, contre-jour orange, appoint froid
const amb = new THREE.AmbientLight(0xffffff, 0.25);
scene.add(amb);
const key = new THREE.DirectionalLight(0xffffff, 1.6);
key.position.set(-8, 14, 12);
scene.add(key);
const rim = new THREE.PointLight(0xff4d00, 260, 60, 1.6);
rim.position.set(6, 4, -8);
scene.add(rim);
const fill = new THREE.DirectionalLight(0x9fb4ff, 0.35);
fill.position.set(10, -4, 6);
scene.add(fill);

// ---------------------------------------------------------------- sol : grille de lignes vivante (bruit + ondes sur la 808)
// calme près du centre (r < uCalm) pour ne jamais traverser les objets posés au-dessus
const floorMat = new THREE.ShaderMaterial({
    uniforms: { uScan: { value: -999 }, uScanW: { value: 1.6 }, uT: { value: 0 }, uAmp: { value: 1.3 }, uKick: { value: 0 }, uCalm: { value: 12 } },
    vertexShader: SNOISE + `uniform float uT, uAmp, uKick, uCalm; varying float vH; varying vec3 vW;
      void main(){
        vec3 p = position; vec2 q = p.xz; float r = length(q);
        float h = snoise(vec3(q * .045, uT * .14)) * uAmp + sin(q.x * .21 + uT * 1.3) * sin(q.y * .17 - uT * .9) * uAmp * .3;
        h += sin(r * .33 - uT * 7.5) * uKick * 1.6 * exp(-r * .025) * step(.01, uAmp + 1.);
        h *= smoothstep(uCalm * .4, uCalm * 1.6, r);
        p.y += h; vH = h;
        vec4 w = modelMatrix * vec4(p, 1.); vW = w.xyz;
        gl_Position = projectionMatrix * viewMatrix * w;
      }`,
    fragmentShader: `uniform float uScan, uScanW; varying float vH; varying vec3 vW;
      void main(){
        float fade = exp(-length(vW.xz) * .021);
        float s = exp(-pow((vW.z - uScan) / uScanW, 2.));
        vec3 c = vec3(.26) * fade + vec3(3.2, .95, .06) * (smoothstep(1.1, 2.6, vH) * .8 + s * 1.3) * fade;
        gl_FragColor = vec4(c, 1.);
      }`,
});
const floor = (() => {
    const N = 110, E = 170, S = 110, pos = [];
    for (let k = 0; k <= N; ++k) {
        const c = (k / N - 0.5) * E;
        for (let s = 0; s < S; ++s) {
            const a = (s / S - 0.5) * E, b = ((s + 1) / S - 0.5) * E;
            pos.push(a, 0, c, b, 0, c, c, 0, a, c, 0, b);
        }
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    const m = new THREE.LineSegments(g, floorMat);
    m.frustumCulled = false;
    return m;
})();
floor.position.y = -6.5;
scene.add(floor);

// ---------------------------------------------------------------- textures
const loader = new THREE.TextureLoader();
async function tex(name) {
    const t = await loader.loadAsync(ASSETS + name);
    t.colorSpace = THREE.SRGBColorSpace;
    t.anisotropy = renderer.capabilities.getMaxAnisotropy();
    return t;
}

// ---------------------------------------------------------------- l'appareil RAW 2
const bodyMat = new THREE.MeshStandardMaterial({ color: 0xb9b9b3, metalness: 0.55, roughness: 0.5, envMapIntensity: 0.4 });
const darkMat = new THREE.MeshStandardMaterial({ color: 0x1a1a1a, metalness: 0.6, roughness: 0.35 });
const bodyGeo = new RoundedBoxGeometry(13.5, 10.3, 0.95, 5, 0.34);
const screwGeo = new THREE.CylinderGeometry(0.15, 0.15, 0.08, 20);
// le synthé RAW 2 en 3D (synth3d.js), thème ALU ; les écrans changent de mode selon le plan
const SYN = makeSynth(THEMES.alu, { hot: HOT, seed: 11 });
const device = SYN.group;
device.userData = { strip: SYN.strip };
scene.add(device);
let devMode = 'scope', devPlay = 0, devCut = 0.5, devPower = 1, devAt = null, devKeys = null, devKnobs = null, devRipples = null, devLogo = 0;
const uiPt = (px, py) => SYN.uiPt(px, py);

// ---------------------------------------------------------------- nuage : chaque pixel de l'interface devient une particule
// uAsm 0 = nuage dispersé dans un champ de bruit, 1 = particules rangées sur l'écran de l'appareil
const CLOUD = await (async () => {
    const CW = 320, CH = 240, n = CW * CH;
    const bm = await createImageBitmap(await (await fetch(ASSETS + (AD === 'le-son' ? 'inst.png' : 'son.png'))).blob());
    const cv = new OffscreenCanvas(CW, CH), cx = cv.getContext('2d');
    cx.drawImage(bm, 0, 0, CW, CH);
    const px = cx.getImageData(0, 0, CW, CH).data;
    const tgt = new Float32Array(n * 3), col = new Float32Array(n * 3), sd = new Float32Array(n * 4), spk = new Float32Array(n);
    const r = rng(4242);
    for (let j = 0; j < CH; ++j) for (let i = 0; i < CW; ++i) {
        const k = j * CW + i;
        tgt.set([((i + 0.5) / CW - 0.5) * 12.8, (0.5 - (j + 0.5) / CH) * 9.6, 0.5], k * 3);
        const lin = c => Math.pow(c / 255, 2.2);
        col.set([lin(px[k * 4]), lin(px[k * 4 + 1]), lin(px[k * 4 + 2])], k * 3);
        spk[k] = r() < 0.05 ? 1 : 0;  // étincelles orange tant que le nuage est dispersé
        // direction au hasard dans une boule (rayon en racine cubique : densité uniforme)
        let x, y, z;
        do { x = r() * 2 - 1; y = r() * 2 - 1; z = r() * 2 - 1; } while (x * x + y * y + z * z > 1);
        sd.set([x, y * 0.7, z, r()], k * 4);
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(tgt, 3));
    g.setAttribute('aCol', new THREE.BufferAttribute(col, 3));
    g.setAttribute('aSeed', new THREE.BufferAttribute(sd, 4));
    g.setAttribute('aSpark', new THREE.BufferAttribute(spk, 1));
    const mat = new THREE.ShaderMaterial({
        uniforms: { uT: { value: 0 }, uAsm: { value: 0 }, uR: { value: 24 }, uNoise: { value: 2 }, uLow: { value: 0 }, uSize: { value: 0.055 },
                    uSpin: { value: 0 }, uBright: { value: 1 }, uScale: { value: 1000 }, uDens: { value: 0.22 }, uPxMax: { value: 4 } },
        vertexShader: SNOISE + `uniform float uT, uAsm, uR, uNoise, uLow, uSize, uSpin, uScale, uDens, uPxMax; attribute vec3 aCol; attribute vec4 aSeed; attribute float aSpark;
          varying vec3 vCol; varying float vA;
          void main(){
            float a = smoothstep(0., 1., clamp(uAsm * 1.6 - aSeed.w * .6, 0., 1.));
            vec3 sc = aSeed.xyz * uR; float an = uSpin * (.55 + aSeed.w * .9);
            sc.xz = mat2(cos(an), -sin(an), sin(an), cos(an)) * sc.xz;
            vec3 p = mix(sc, position, a);
            float amp = uNoise * (1. - a * .92) + uLow * .5 * (1. - a * .7);
            float nz = snoise(p * .09 + vec3(0., 0., uT * .3));   // un seul bruit, tourné en champ de directions
            float ph = nz * 4.2 + aSeed.w * 6.283;
            p += vec3(sin(ph) * cos(nz * 3.1), cos(ph * .8 + uT * .7), sin(ph * 1.3 + 1.7)) * amp;
            vec4 mv = modelViewMatrix * vec4(p, 1.);
            gl_Position = projectionMatrix * mv;
            // dispersé : poussière fine et clairsemée (uDens des points, étincelles toujours là) ;
            // rangé : chaque point couvre son pixel d'écran
            bool spark = aSpark > .5;
            float keep = (spark || fract(aSeed.w * 7.13) < mix(uDens, 1., a)) ? 1. : 0.;
            float size = mix(.018, uSize, a) * (1. + uLow * .5) * uScale / -mv.z;
            gl_PointSize = keep * clamp(size, 1., mix(2.2, uPxMax, a));
            vCol = spark ? mix(vec3(3.2, .95, .06), aCol, a) : aCol; vA = spark ? 1. : mix(.32, 1., a);
          }`,
        fragmentShader: `uniform float uBright; varying vec3 vCol; varying float vA;
          void main(){ vec2 d = gl_PointCoord - .5; if (dot(d, d) > .25) discard; gl_FragColor = vec4(vCol * uBright * vA, 1.); }`,
    });
    const pts = new THREE.Points(g, mat);
    pts.frustumCulled = false;
    const grp = new THREE.Group();
    grp.add(pts);
    scene.add(grp);
    function set(o) { for (const k in o) mat.uniforms[k].value = o[k]; }
    function follow(obj) { grp.position.copy(obj.position); grp.rotation.copy(obj.rotation); grp.scale.copy(obj.scale); }
    return { g: grp, set, follow, mat };
})();

// ---------------------------------------------------------------- wavetable 3D (48 frames)
const WT = (() => {
    const NF = 48, NP = 220, WD = 12, AMP = 2.3, DZ = 0.3, TH = 0.032;
    const g = new THREE.Group();
    const strips = [];
    for (let k = 0; k < NF; ++k) {
        const geo = new THREE.BufferGeometry();
        const pos = new Float32Array(NP * 2 * 3);
        const idx = [];
        for (let i = 0; i < NP - 1; ++i) { const a = i * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
        geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
        geo.setIndex(idx);
        const f = k / (NF - 1);
        const c = new THREE.Color(0x2e2e2e).lerp(new THREE.Color(0xf4f4f0), Math.pow(f, 1.6));
        const m = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color: c, side: THREE.DoubleSide, transparent: true }));
        m.frustumCulled = false;
        g.add(m);
        strips.push({ m, pos, base: c.clone() });
    }
    function wave(f, x, ph) {
        const xw = x + 0.16 * f * Math.sin(2 * Math.PI * x + ph);  // distorsion de phase (warp)
        let y = 0;
        for (let h = 1; h <= 10; ++h) {
            const a = (h === 1 ? 1 : clamp(f * 11 - (h - 1.5))) / h;
            y += a * Math.sin(2 * Math.PI * h * xw + (h > 1 ? ph * 0.25 * h : 0));
        }
        return y / 1.55;
    }
    // reveal : part de frames dessinées ; hot : frame jouée (orange) ; ph : mouvement ; squash : écrasement
    function update(reveal, hot, ph, squash = 1, ampMul = 1) {
        for (let k = 0; k < NF; ++k) {
            const s = strips[k];
            const vis = clamp(reveal * NF - k);
            s.m.visible = vis > 0;
            if (!s.m.visible) continue;
            const f = k / (NF - 1);
            const isHot = Math.abs(k - hot) < 0.5;
            const th = isHot ? TH * 2.6 : TH;
            const amp = AMP * eOut(vis) * squash * ampMul;
            const z = (k - NF / 2) * DZ * (0.25 + 0.75 * squash);
            for (let i = 0; i < NP; ++i) {
                const x = i / (NP - 1);
                const y = wave(f, x, ph) * amp;
                const X = (x - 0.5) * WD;
                s.pos.set([X, y - th, z, X, y + th, z], i * 6);
            }
            s.m.geometry.attributes.position.needsUpdate = true;
            s.m.material.color.copy(isHot ? HOT : s.base);
            s.m.material.opacity = eOut(vis);
        }
    }
    // hauteur de la frame k au point x (0..1) : pour poser le bonhomme sur la vague
    const height = (k, x, ph, ampMul = 1) => wave(k / (NF - 1), x, ph) * AMP * ampMul;
    return { g, update, NF, height, DZ, WD };
})();
scene.add(WT.g);

// ---------------------------------------------------------------- VIE : jeu de la vie B3/S23 en cubes
const LIFE = (() => {
    const N = 40, GENS = 260;
    const r = rng(20251123);
    let cur = new Uint8Array(N * N);
    for (let y = 8; y < N - 8; ++y) for (let x = 8; x < N - 8; ++x) cur[y * N + x] = r() < 0.32 ? 1 : 0;
    for (const [x, y] of [[3, 3], [4, 4], [2, 5], [3, 5], [4, 5]]) cur[y * N + x] = 1;  // planeur
    const gens = [cur];
    for (let g = 1; g < GENS; ++g) {
        const nxt = new Uint8Array(N * N);
        for (let y = 0; y < N; ++y) for (let x = 0; x < N; ++x) {
            let n = 0;
            for (let dy = -1; dy <= 1; ++dy) for (let dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const xx = (x + dx + N) % N, yy = (y + dy + N) % N;
                if (cur[yy * N + xx]) ++n;
            }
            const a = cur[y * N + x];
            nxt[y * N + x] = a ? (n === 2 || n === 3 ? Math.min(a + 1, 250) : 0) : (n === 3 ? 1 : 0);
        }
        gens.push(nxt);
        cur = nxt;
    }
    const geo = new THREE.BoxGeometry(0.84, 1, 0.84);
    geo.translate(0, 0.5, 0);
    const body = new THREE.InstancedMesh(geo, new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.42, metalness: 0.15 }), N * N);
    const capGeo = new THREE.PlaneGeometry(0.84, 0.84);
    capGeo.rotateX(-Math.PI / 2);
    const caps = new THREE.InstancedMesh(capGeo, new THREE.MeshBasicMaterial({ color: 0xffffff }), N * N);
    const g = new THREE.Group();
    g.add(body, caps);
    const M = new THREE.Matrix4(), Qn = new THREE.Quaternion(), S = new THREE.Vector3(), P = new THREE.Vector3();
    const ages = [new THREE.Color(0xff4d00), new THREE.Color(0xc23a00), new THREE.Color(0x8a2a00), new THREE.Color(0x5a1c04), new THREE.Color(0x3a1406), new THREE.Color(0x241008)];
    const dead = new THREE.Color(0x1d1d1d), black = new THREE.Color(0x000000);
    const capHot = HOT.clone(), capWarm = new THREE.Color(1.3, 0.38, 0.02);
    function update(gt, hmul = 1) {
        const gi = clamp(Math.floor(gt), 0, GENS - 1), fr = gt - Math.floor(gt);
        const A = gens[gi];
        for (let i = 0; i < N * N; ++i) {
            const x = i % N, y = (i / N) | 0, a = A[i];
            let h, c, cap;
            if (a) {
                const grow = a === 1 ? eOut(clamp(fr * 2.5)) : 1;
                h = (0.35 + 2.4 * Math.max(0, 1 - (a - 1) / 6)) * grow * hmul + 0.06;
                c = ages[Math.min(a - 1, 5)];
                cap = a === 1 ? capHot : a === 2 ? capWarm : c;
            } else { h = 0.06; c = dead; cap = black; }
            P.set(x - N / 2 + 0.5, 0, y - N / 2 + 0.5);
            S.set(1, h, 1);
            M.compose(P, Qn, S);
            body.setMatrixAt(i, M);
            body.setColorAt(i, c);
            P.y = h + 0.002;
            S.set(1, 1, 1);
            M.compose(P, Qn, S);
            caps.setMatrixAt(i, M);
            caps.setColorAt(i, cap);
        }
        body.instanceMatrix.needsUpdate = caps.instanceMatrix.needsUpdate = true;
        body.instanceColor.needsUpdate = caps.instanceColor.needsUpdate = true;
        let alive = 0;
        for (let i = 0; i < N * N; ++i) if (A[i]) ++alive;
        return { gen: gi, alive };
    }
    return { g, update, N };
})();
scene.add(LIFE.g);

// ---------------------------------------------------------------- échantillon réel : enveloppe du Wurlitzer en barres
const BARS = (() => {
    const env = wurli.env, wave = wurli.wave, n = env.length, DZ = 0.32;
    const geo = new THREE.BoxGeometry(0.13, 1, 0.13);
    const mesh = new THREE.InstancedMesh(geo, new THREE.MeshBasicMaterial({ color: 0xffffff }), n * 2);
    const g = new THREE.Group();
    g.add(mesh);
    const M = new THREE.Matrix4(), Qn = new THREE.Quaternion(), S = new THREE.Vector3(), P = new THREE.Vector3();
    const white = new THREE.Color(0xe8e8e4), grey = new THREE.Color(0x3a3a3a);
    function update(reveal, play) {
        for (let i = 0; i < n; ++i) {
            const vis = clamp(reveal * n - i);
            // vraie forme d'onde (une dizaine de cycles) qui décroît comme l'échantillon
            const v = wave[Math.floor(i * wave.length / n)] * Math.pow(env[i], 1.5) * eOut(vis);
            const h = Math.max(0.03, Math.abs(v) * 8.5);
            const c = i / n < play ? (Math.abs(i / n - play) < 0.012 ? HOT : white) : grey;
            for (let s = 0; s < 2; ++s) {
                if (s) P.set(0, 0, (i - n / 2) * DZ), S.set(1.6, 0.05, 1.6);  // repère de la ligne de zéro
                else P.set(0, v >= 0 ? h / 2 : -h / 2, (i - n / 2) * DZ), S.set(1, h, 1);
                M.compose(P, Qn, S);
                mesh.setMatrixAt(i * 2 + s, M);
                mesh.setColorAt(i * 2 + s, c);
            }
        }
        mesh.instanceMatrix.needsUpdate = true;
        mesh.instanceColor.needsUpdate = true;
    }
    return { g, update, len: n * DZ };
})();
scene.add(BARS.g);

// ---------------------------------------------------------------- les 6 thèmes, sans jamais se toucher
// 9:16 : une pile verticale (pas de 7,6 > hauteur 6,4) qu'on parcourt ; 1:1 : un carrousel de rayon 26
// (corde 10,8 > largeur 8,4). Chaque appareil entre par le côté sur sa propre ligne.
const FAN = (() => {
    const g = new THREE.Group();
    const items = [];
    for (let i = 0; i < 6; ++i) {
        const d = makeSynth(THEMES[['alu', 'nothing', 'titane', 'crt', 'ultraviolet', 'onirique'][i]], { hot: HOT, light: true, seed: 3 + i }).group;
        d.scale.setScalar(0.62);
        g.add(d);
        items.push(d);
    }
    function update(t, t0, s) {
        items.forEach((d, i) => {
            const k = i - 2.5, e = eOut(prog(t, t0 + i * 0.07, t0 + 0.45 + i * 0.07)), side = i % 2 ? 1 : -1;
            if (PORTRAIT) {
                d.position.set(side * 1.1 + (1 - e) * side * 30, -k * 7.6, 0);
                d.rotation.set(0.1, side * -0.22 + Math.sin(s * 4 + i) * 0.06, 0);
            } else {
                const R = 26, a = k * 0.42 + (s - 0.5) * 0.5;
                d.position.set(Math.sin(a) * R, (1 - e) * 26 * side, Math.cos(a) * R - R);
                d.rotation.set(0, a, 0);
            }
        });
    }
    return { g, update };
})();
scene.add(FAN.g);

// ---------------------------------------------------------------- interface (HTML par-dessus la 3D)
const el = {
    term: $('term'), title: $('title'), sub: $('sub'), callout: $('callout'), list: $('list'),
    end: $('end'), end2: $('end2'), flash: $('flash'), hudBL: $('hudBL'), hudTL: $('hudTL'), hudTR: $('hudTR'),
    grain: $('grain'), rec: document.querySelector('#hudTR .rec'),
};
el.end.querySelector('.tagline').textContent = T.accroche;
el.end2.querySelector('.cta').textContent = T.appel;
el.end2.querySelector('.url').textContent = T.site;
el.end2.querySelector('.formats').textContent = T.formats;
el.end2.querySelector('.offre').textContent = T.offre || '';
el.end2.querySelector('.offre').classList.toggle('hide', !T.offre);
el.end.querySelector('.word small').textContent = 'ABRASION';

const GLYPHS = '█▓▒░#%&@$/\\<>=+*';
// texte qui « s'abrase » : lettres remplacées par des blocs au début, puis le vrai texte
function scramble(text, p, seed) {
    if (p >= 1) return text;
    const r = rng(seed);
    let out = '';
    for (let i = 0; i < text.length; ++i) {
        const ch = text[i];
        if (ch === ' ' || ch === '\n') { out += ch; r(); continue; }
        out += r() < p * 1.15 ? ch : GLYPHS[Math.floor(r() * GLYPHS.length)];
    }
    return out;
}
const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
function showTitle(text, t, t0, t1, { size = 64, y = 0.2, accentLast = false, frame = 0 } = {}) {
    const on = t >= t0 && t < t1;
    el.title.style.display = on ? 'block' : 'none';
    if (!on) return;
    if (!PORTRAIT && y < 0.15) el.hudTL.style.display = 'none';
    const p = prog(t, t0, t0 + 0.28);
    let s = scramble(text, p, 977 + frame);
    let html = esc(s).replace(/\n/g, '<br>');
    if (accentLast && p >= 1) html = html.replace(/(\S+)$/, '<i>$1</i>');
    el.title.innerHTML = html;
    el.title.style.fontSize = size * U + 'px';
    el.title.style.top = y * H + 'px';
    const jit = p < 1 ? (rng(frame * 7 + 1)() - 0.5) * 16 * U : 0;
    const out = t > t1 - 0.12 ? prog(t, t1 - 0.12, t1) : 0;
    el.title.style.transform = `translateX(${jit}px) scaleY(${1 - out})`;
    el.title.style.textShadow = p < 1 ? `${-6 * U}px 0 #FF4D00, ${6 * U}px 0 rgba(255,255,255,.6)` : 'none';
}
function showSub(text, t, t0, t1, { size = 24, y = 0.3 } = {}) {
    const on = t >= t0 && t < t1 && text;
    el.sub.style.display = on ? 'block' : 'none';
    if (!on) return;
    const n = Math.floor((t - t0) * 70);
    el.sub.textContent = text.slice(0, n);
    el.sub.style.fontSize = size * U + 'px';
    el.sub.style.top = y * H + 'px';
}
function showTerm(lines, t, t0, t1, cps = 42) {
    const on = t >= t0 && t < t1;
    el.term.style.display = on ? 'block' : 'none';
    if (!on) return;
    let left = Math.floor((t - t0) * cps), html = '';
    for (const ln of lines) {
        if (left <= 0) break;
        const part = ln.slice(0, left);
        left -= ln.length + 6;  // petite pause en fin de ligne
        html += esc(part).replace(/(OK|\.{3,})$/, m => (m === 'OK' ? '<span class="ok">OK</span>' : m)) + '\n';
    }
    const blink = Math.floor(t * 4) % 2 === 0;
    el.term.innerHTML = html.replace(/\n$/, '') + (blink ? '<span class="cur"></span>' : '');
}
function showCallout(idx, data, t, t0, t1, y) {
    const on = t >= t0 && t < t1;
    el.callout.style.display = on ? 'block' : 'none';
    if (!on) return;
    shadeBottom(true);
    if (!PORTRAIT) $('hudBot').style.display = 'none';
    const [tag, h, p] = data;
    const q = prog(t, t0, t0 + 0.35);
    el.callout.style.top = y * H + 'px';
    el.callout.querySelector('.tag').textContent = '[' + String(idx).padStart(2, '0') + ']';
    el.callout.querySelector('.tag2').textContent = tag;
    el.callout.querySelector('h2').textContent = scramble(h, prog(t, t0 + 0.05, t0 + 0.3), 31 * idx + Math.floor(t * FPS));
    const pt = p.slice(0, Math.floor(Math.max(0, t - t0 - 0.2) * 80));
    el.callout.querySelector('p').textContent = pt;
    el.callout.querySelector('.rule').style.transform = `scaleX(${eOut(q)})`;
    const out = prog(t, t1 - 0.1, t1);
    el.callout.style.opacity = String(1 - out);
    el.callout.style.transform = `translateY(${(1 - eOut(q)) * 30 * U}px)`;
}
function showEnd(t, t0, yTop, yBot) {
    const on = t >= t0;
    el.end.style.display = el.end2.style.display = on ? 'block' : 'none';
    if (!on) return;
    const a = prog(t, t0, t0 + 0.4), b = prog(t, t0 + 0.45, t0 + 0.85);
    el.end.style.top = yTop * H + 'px';
    el.end2.style.top = yBot * H + 'px';
    el.end.style.opacity = String(a > 0 ? 1 : 0);
    el.end.style.transform = `translateY(${(1 - eOut(a)) * -40 * U}px)`;
    const word = el.end.querySelector('.word b');
    word.innerHTML = esc(scramble('RAW2', prog(t, t0, t0 + 0.3), 5 + Math.floor(t * FPS))).replace(/2$/, '<i>2</i>');
    el.end2.style.opacity = String(eOut(b));
    el.end2.style.transform = `translateY(${(1 - eOut(b)) * 40 * U}px)`;
    // l'appel à l'action « tape » sur les temps
    const beat = (t - t0) * 2, pulse = Math.exp(-(beat % 1) * 6);
    el.end2.querySelector('.cta').style.transform = `scale(${1 + 0.025 * pulse * (t > t0 + 0.9 ? 1 : 0)})`;
}
function flashAt(t, cuts) {
    let o = 0, col = '#fff';
    for (const c of cuts) if (t >= c.t && t < c.t + (c.d || 0.25)) { o = (c.o || 1) * (1 - (t - c.t) / (c.d || 0.25)); col = c.c || '#fff'; }
    el.flash.style.opacity = String(o);
    el.flash.style.background = col;
}
const gctx = el.grain.getContext('2d');
el.grain.width = Math.round(W / 2); el.grain.height = Math.round(H / 2);
const gimg = gctx.createImageData(el.grain.width, el.grain.height);
function grain(frame) {
    const r = rng(frame * 13 + 7), d = gimg.data;
    for (let i = 0; i < d.length; i += 4) { const v = (r() * 255) | 0; d[i] = d[i + 1] = d[i + 2] = v; d[i + 3] = 255; }
    gctx.putImageData(gimg, 0, 0);
}
function hud(t, extra = '') {
    const s = Math.floor(t), f = Math.floor((t - s) * FPS);
    const bpm = META && META.bpm ? Math.round(META.bpm) : SCORE.bpm;
    el.hudBL.textContent = `T+00:${String(Math.floor(REALT)).padStart(2, '0')}:${String(Math.floor((REALT % 1) * FPS)).padStart(2, '0')} · ${bpm} BPM${extra}`;
    el.rec.style.opacity = Math.floor(t * 4) % 2 === 0 ? '1' : '0.25';
}
const spec = $('spec'), sctx = spec.getContext('2d');
spec.width = Math.round(24 * U); spec.height = Math.round(320 * U);
// analyseur vertical dans la marge gauche (graves en bas), entre les coins : rien d'autre n'y passe
function spectrum() {
    const b = A.bands, n = b.length, hh = spec.height / n, w = spec.width;
    sctx.clearRect(0, 0, w, spec.height);
    for (let i = 0; i < n; ++i) {
        const v = b[i], bw = Math.max(1, Math.round(v * w));
        sctx.fillStyle = v > 0.82 ? '#FF4D00' : 'rgba(244,244,240,0.65)';
        sctx.fillRect(0, Math.round(spec.height - (i + 1) * hh), bw, Math.max(1, Math.round(hh - 3 * U)));
    }
}
function shadeBottom(on) { $('shade').style.opacity = on ? '1' : '0'; }
function hideAll() {
    shadeBottom(false);
    for (const k of ['term', 'title', 'sub', 'callout', 'list', 'end', 'end2']) el[k].style.display = 'none';
    el.hudTL.style.display = 'block';
    $('hudBot').style.display = 'block';
}

// ---------------------------------------------------------------- scène : visibilité et petites aides
// le bonhomme d'ABRASION (pub HISTOIRE)
const BON = makeBonhomme();
scene.add(BON.root);
BON.root.visible = false;
function only(...gs) {
    for (const g of [device, WT.g, LIFE.g, BARS.g, FAN.g, CLOUD.g]) g.visible = gs.includes(g);
}
function shake(t, amt = 0.35) {
    const e = KICK16 * amt;
    return v3(Math.sin(t * 91.7) * e, Math.cos(t * 73.3) * e, 0);
}
function pulseStrip(t, obj = device) {
    const e = KICK9;
    obj.userData.strip.material.color.copy(HOT).multiplyScalar(0.5 + 1.2 * e);
    rim.intensity = 180 + 520 * e;
}
function resetDevice() {
    device.position.set(0, 0, 0);
    device.rotation.set(0, 0, 0);
    device.scale.setScalar(1);
    devMode = 'scope'; devPower = 1; devAt = null; devKeys = null; devKnobs = null; devRipples = null; devLogo = 0;
}
const DEV_D = fitDist(13.5 * 1.12, 10.3 * 1.12);
// sol : position, relief et zone calme (les objets ne le touchent jamais)
function ground(y, amp = 1.3, calm = 12) {
    floor.position.y = y;
    floorMat.uniforms.uAmp.value = amp * (0.75 + 0.5 * A.mid);
    floorMat.uniforms.uCalm.value = calm;
}
// nuage autour de l'appareil, en orbite lente (cartons de fin)
function cloudOrbit(t, bright = 0.55) {
    CLOUD.follow(device);
    CLOUD.set({ uAsm: 0, uR: 30, uNoise: 2.2, uSpin: t * 0.18, uBright: bright });
}

// carton de fin commun : l'appareil tourne lentement entre le logo et l'appel à l'action
function endShot(t, t0, mode, r0, r1) {
    only(device, CLOUD.g);
    ground(-6.5, 1.1, 14);
    const u = prog(t, t0, t0 + 4);
    devMode = mode;
    device.rotation.set(-0.08, lerp(r0, r1, eIO(u)), 0);
    device.scale.setScalar(PORTRAIT ? 0.78 : 0.5);
    device.position.set(0, PORTRAIT ? 0.5 : 1.15, 0);
    cloudOrbit(t, 0.5);
    // le nuage s'est dispersé à la coupe et revient en orbite
    CLOUD.set({ uAsm: 1 - eOut(prog(t, t0, t0 + 0.8)), uNoise: lerp(5, 2.2, prog(t, t0, t0 + 1)) });
    setCam(v3(0, 0.6, DEV_D * lerp(1.0, 0.94, u)), v3(0, 0.2, 0));
    pulseStrip(t);
    floorMat.uniforms.uScan.value = lerp(-20, 30, prog(t, t0, t0 + 2));
    FX.decay = 0.3; FX.zoom = 0.002;
    el.hudTL.style.display = 'none';
    $('hudBot').style.display = PORTRAIT ? 'block' : 'none';
    showEnd(t, t0 + 0.05, PORTRAIT ? 0.12 : 0.06, PORTRAIT ? 0.665 : 0.6);
}

// ---------------------------------------------------------------- pub 1 : LANCEMENT (20 s)
function adLancement(t, frame) {
    hideAll();
    resetDevice();
    floorMat.uniforms.uScan.value = -999;
    const L = T.lancement;
    const pts = L.points;
    const cuts = [{ t: 4, d: 0.3, o: 0.8 }, { t: 6, d: 0.12, o: 0.45, c: '#FF4D00' }, { t: 8, d: 0.12, o: 0.45, c: '#FF4D00' }, { t: 10, d: 0.12, o: 0.45, c: '#FF4D00' },
                  { t: 12, d: 0.12, o: 0.45, c: '#FF4D00' }, { t: 14, d: 0.12, o: 0.45, c: '#FF4D00' }, { t: 16, d: 0.4 }];
    flashAt(t, cuts);
    cutFx(t, [4, 6, 8, 10, 12, 14, 16]);
    const calloutY = PORTRAIT ? 0.66 : 0.6;
    let extra = '';
    if (t < 4) {
        // intro : la wavetable se dessine ; les pixels de l'interface tournent en nuage puis se rangent en écran
        only(CLOUD.g);
        if (t < 3.5) WT.g.visible = true;   // la wavetable s'écrase et disparaît avant que l'écran de particules se forme
        const u = t / 4;
        const squash = t > 2.95 ? 1 - eIO(prog(t, 2.95, 3.5)) * 0.97 : 1;
        WT.update(eOut(prog(t, 0.1, 3.2)), Math.floor(prog(t, 0.1, 3.2) * (WT.NF - 1)), t * 1.3, squash, 0.8 + 0.5 * A.mid);
        CLOUD.follow(device);
        CLOUD.set({ uAsm: eIO(prog(t, 3.05, 3.95)), uR: 30, uNoise: 3.2, uSpin: t * 0.35, uBright: lerp(0.35, 1, prog(t, 2.6, 3.9)) });
        const d = PORTRAIT ? 26 : 22;
        const a = lerp(-0.55, 0.18, eIO(u));
        setCam(v3(Math.sin(a) * d, lerp(7, 2.5, eIO(u)), Math.cos(a) * d), v3(0, PORTRAIT ? -0.6 : -0.3, 0), lerp(0.04, 0, u));
        ground(-5.2, 1.2, 11);
        floorMat.uniforms.uScan.value = lerp(-30, 30, prog(t, 0.2, 3.8));
        FX.decay = lerp(0.86, 0.55, prog(t, 3.0, 3.6)); FX.zoom = 0.006; FX.rot = 0.003; FX.disp = 0.004;
        FX.dither = Math.max(FX.dither, prog(t, 3.75, 3.98));   // l'image se trame juste avant le drop
        showTerm(L.intro, t, 0.15, 4);
        showTitle(L.presente, t, 2.1, 3.8, { size: 34, y: PORTRAIT ? 0.74 : 0.72, frame });
        extra = ' · FRAME ' + String(Math.floor(prog(t, 0.1, 3.2) * 255) + 1).padStart(3, '0') + '/256';
    } else if (t < 6) {
        // drop : l'appareil apparaît là où les pixels se sont rangés, puis le nuage explose
        only(device, CLOUD.g);
        ground(-8.5, 1.4, 13);   // sol plus bas : même au rebond, l'appareil ne le touche pas
        const p = prog(t, 4, 4.3);
        const u = prog(t, 4, 6);
        device.position.y = lerp(1.2, 0, eBack(p));
        device.rotation.set(lerp(-0.2, -0.1, eOut(p)), lerp(0.18, -0.3, eIO(u)), 0);
        if (!PORTRAIT) { device.scale.setScalar(0.74); device.position.y -= 1.5; }
        CLOUD.follow(device);
        CLOUD.set({ uAsm: 1 - eOut(prog(t, 4, 4.9)), uR: 34, uNoise: lerp(1, 6, prog(t, 4, 4.6)), uSpin: (t - 4) * 0.6, uBright: lerp(1, 0.45, prog(t, 4.2, 6)) });
        const d = DEV_D * lerp(1.0, 1.16, eOut(u));
        setCam(v3(lerp(-2, 2, u), lerp(-0.6, 0.4, u) - (PORTRAIT ? 1.2 : 0), d).add(shake(t, 0.6)), v3(0, PORTRAIT ? -1.6 : -0.3, 0));
        pulseStrip(t);
        FX.decay = t < 4.9 ? 0.82 : 0.5; FX.zoom = t < 4.9 ? 0.012 : 0.004; FX.disp = 0.003;
        showTitle('RAW 2', t, 4, 6, { size: PORTRAIT ? 150 : 120, y: PORTRAIT ? 0.13 : 0.08, accentLast: true, frame });
        showSub(T.accroche, t, 4.35, 6, { size: PORTRAIT ? 21 : 22, y: PORTRAIT ? 0.235 : 0.215 });
        floorMat.uniforms.uScan.value = lerp(-10, 40, prog(t, 4, 5));
    } else if (t < 8) {
        // [01] oscillateurs : la wavetable respire avec la musique, traînées de feedback
        only(WT.g);
        const u = prog(t, 6, 8);
        WT.update(1, Math.floor(u * 40) % WT.NF, t * (1.8 + A.mid), 1, 0.75 + 0.6 * A.mid + 0.25 * KICK);
        ground(-5.2, 1.3, 11);
        const a = lerp(0.65, 0.2, eIO(u));
        const d = PORTRAIT ? lerp(17, 14, u) : lerp(15, 12, u);
        setCam(v3(Math.sin(a) * d, lerp(2.5, 3.6, u), Math.cos(a) * d).add(shake(t, 0.25)), v3(0, PORTRAIT ? 1.2 : 0, -1), 0.05);
        FX.decay = 0.78; FX.zoom = 0.003; FX.disp = 0.002;
        showCallout(1, pts[0], t, 6.05, 8, calloutY);
        extra = ' · OSC A/B';
    } else if (t < 10) {
        // [02] VIE : la grille vit à la double croche, les cubes poussent avec les graves
        only(LIFE.g);
        const u = prog(t, 8, 10);
        const st = LIFE.update(12 + (t - 8) * 8, 0.7 + 0.6 * A.low);
        const a = lerp(-0.5, 0.25, u);
        const d = PORTRAIT ? 34 : 30;
        ground(-0.01, 0, 30);   // sol plat sous la grille
        setCam(v3(Math.sin(a) * d, lerp(24, 18, u), Math.cos(a) * d).add(shake(t, 0.3)), v3(0, PORTRAIT ? -6 : 0, PORTRAIT ? 4 : 0));
        FX.decay = 0.7; FX.zoom = 0.002;
        showCallout(2, pts[1], t, 8.05, 10, calloutY);
        extra = ` · GEN ${String(st.gen).padStart(4, '0')} · ${st.alive}`;
    } else if (t < 12) {
        // [03] instruments : gros plan net sur l'écran de l'OSC A qui joue le Wurlitzer
        only(device);
        const u = prog(t, 10, 12);
        devMode = 'wurli'; devPlay = u;
        const target = uiPt(205, 270);
        const d = PORTRAIT ? lerp(9.5, 7.2, eIO(u)) : lerp(8.5, 6.5, eIO(u));
        setCam(target.clone().add(v3(lerp(3.2, 1.0, u), lerp(-1.5, -0.6, u), d)).add(shake(t, 0.15)), target.clone().add(v3(0, PORTRAIT ? -2.2 : -0.8, 0)), -0.03);
        pulseStrip(t);
        showCallout(3, pts[2], t, 10.05, 12, calloutY);
        extra = ' · ZONE C#4 · 36 ZONES';
    } else if (t < 14) {
        // [04] synthèse : travelling sur la page SYNTH (filtre puis warps)
        only(device);
        const u = prog(t, 12, 14);
        devMode = 'filter'; devCut = 0.25 + 0.55 * eIO(u) + 0.08 * KICK;
        // des potards de l'OSC B jusqu'à son écran, qui trace la courbe du filtre
        const look = vl(uiPt(1000, 500), uiPt(1075, 290), eIO(u));
        const d = PORTRAIT ? lerp(8.5, 10, u) : lerp(7.5, 9, u);
        setCam(look.clone().add(v3(lerp(-3.2, 1.2, u), -2.6, d)).add(shake(t, 0.15)), look.clone().add(v3(0, PORTRAIT ? -2.2 : -0.6, 0)), 0.04);
        pulseStrip(t);
        showCallout(4, pts[3], t, 12.05, 14, calloutY);
        extra = ' · FILTRE · WARP';
    } else if (t < 16) {
        // [05] thèmes : pile verticale (9:16) ou carrousel (1:1), sans contact
        only(FAN.g);
        const u = prog(t, 14, 16);
        FAN.update(t, 14, eIO(u));
        ground(-12, 1.3, 16);
        if (PORTRAIT) setCam(v3(lerp(-4, 4, u), lerp(12, -9, eIO(u)) + 3, 26).add(shake(t, 0.25)), v3(0, lerp(12, -9, eIO(u)) - 4, 0), 0.03);
        else setCam(v3(lerp(-3, 3, u), 2, 26).add(shake(t, 0.25)), v3(0, 0, -6));
        FX.decay = 0.18; FX.zoom = 0;   // pas de copies fantômes des appareils
        showCallout(5, pts[4], t, 14.05, 16, calloutY);
        extra = ' · THÈME ' + ['ALU', 'NOTHING', 'TITANE', 'CRT', 'ULTRAVIOLET', 'ONIRIQUE'][Math.floor(u * 6) % 6];
    } else {
        endShot(t, 16, 'scope', -0.42, 0.18);
    }
    hud(t, extra);
}

// ---------------------------------------------------------------- pub 2 : LE SON (16 s)
function adLeSon(t, frame) {
    hideAll();
    resetDevice();
    floorMat.uniforms.uScan.value = -999;
    const S = T['le-son'];
    flashAt(t, [{ t: 2, d: 0.35 }, { t: 8, d: 0.16, o: 0.55, c: '#FF4D00' }, { t: 12, d: 0.4 }]);
    cutFx(t, [2, 8, 12]);
    let extra = '';
    if (t < 2) {
        // intro : la vraie forme d'onde du Wurlitzer se dresse note après note, le nuage tourne au loin
        only(BARS.g, CLOUD.g);
        const notes = [0, 0.25, 0.5, 0.875, 1.25, 1.5];
        let rev = 0;
        notes.forEach((n, i) => { if (t >= n) rev = Math.max(rev, lerp(i / notes.length, (i + 1) / notes.length, eOut(prog(t, n, n + 0.22)))); });
        BARS.g.position.set(0, 0, 0);
        BARS.g.rotation.set(0, 0, 0);
        BARS.update(rev, rev);
        ground(-5.8, 1.2, 9);
        CLOUD.g.position.set(0, 2, -10); CLOUD.g.rotation.set(0, 0, 0); CLOUD.g.scale.setScalar(1);
        CLOUD.set({ uAsm: 0, uR: 34, uNoise: 3, uSpin: t * 0.4, uBright: lerp(0.3, 0.7, prog(t, 1.2, 2)) });
        const u = t / 2;
        const a = lerp(1.2, 1.02, eIO(u)), d = PORTRAIT ? lerp(33, 27, u) : lerp(30, 25, u);
        setCam(v3(Math.sin(a) * d, lerp(5, 3, u), Math.cos(a) * d), v3(0, PORTRAIT ? -2.5 : 0, -3), -0.06);
        FX.decay = 0.86; FX.zoom = 0.005; FX.rot = -0.002; FX.disp = 0.004;
        FX.dither = Math.max(FX.dither, prog(t, 1.82, 1.98));
        showTitle(S.intro, t, 0.3, 2, { size: PORTRAIT ? 92 : 80, y: PORTRAIT ? 0.16 : 0.12, frame });
        extra = ' · ' + wurli.name.toUpperCase();
    } else if (t < 8) {
        // 23 instruments : l'appareil se pose, le nuage explose, la liste défile sur les temps
        only(device, CLOUD.g);
        devMode = 'wurli'; devPlay = ((t - 2) / 2.87) % 1;
        floorMat.uniforms.uScan.value = lerp(-25, 35, ((t - 2) / 2) % 1);
        const p = prog(t, 2, 2.3);
        const u = prog(t, 2, 8);
        device.position.set(0, lerp(PORTRAIT ? 2.2 : 3, PORTRAIT ? 1.2 : 2.0, eBack(p)), 0);
        device.rotation.set(lerp(-0.2, -0.1, eOut(p)), lerp(-0.35, 0.3, eIO(u)), 0);
        device.scale.setScalar(PORTRAIT ? 0.82 : 0.5);
        CLOUD.follow(device);
        CLOUD.set({ uAsm: 1 - eOut(prog(t, 2, 2.9)), uR: 34, uNoise: lerp(1, 5, prog(t, 2, 2.7)), uSpin: (t - 2) * 0.3, uBright: lerp(1, 0.4, prog(t, 2.2, 4)) });
        ground(-7.5, 1.3, 12);
        setCam(v3(0, PORTRAIT ? 0.5 : 0.8, DEV_D * (PORTRAIT ? 0.95 : 1.05)).add(shake(t, 0.5)), v3(0, PORTRAIT ? -1.2 : 0.4, 0));
        pulseStrip(t);
        FX.decay = t < 2.9 ? 0.88 : 0.45; FX.zoom = t < 2.9 ? 0.012 : 0.003;
        showTitle(PORTRAIT ? S.titre.replace(' RÉELS', '\nRÉELS') : S.titre, t, 2, 8, { size: PORTRAIT ? 84 : 54, y: PORTRAIT ? 0.1 : 0.085, frame });
        const k = Math.floor((t - 2.25) * 2);
        const win = PORTRAIT ? 12 : 5;
        const first = Math.max(0, Math.min(k - win + 1, S.instruments.length - win));
        let html = '';
        for (let i = first; i < Math.min(S.instruments.length, first + win); ++i) {
            const [n, cat, z] = S.instruments[i];
            const line = `${String(i + 1).padStart(2, '0')}  ${n.padEnd(20)} ${cat.padEnd(8)} ${String(z).padStart(3)} ZONES`;
            const cls = i === k ? 'on' : i < k ? 'done' : '';
            html += (cls ? `<span class="${cls}">` : '') + esc(line) + (cls ? '</span>' : '') + '\n';
        }
        el.list.innerHTML = html;
        el.list.style.display = t >= 2.25 ? 'block' : 'none';
        shadeBottom(true);
        el.list.style.top = (PORTRAIT ? 0.6 : 0.635) * H + 'px';
        el.list.style.fontSize = (PORTRAIT ? 25 : 22) * U + 'px';
        extra = ` · INST ${String(Math.min(23, Math.max(1, k + 1))).padStart(2, '0')}/23`;
    } else if (t < 12) {
        // fusion : la wavetable flotte bien au-dessus de la grille vivante (aucun contact)
        only(WT.g, LIFE.g);
        const u = prog(t, 8, 12);
        LIFE.g.position.set(0, -3, 0);
        const st = LIFE.update(30 + (t - 8) * 8, 0.7 + 0.5 * A.low);
        WT.g.position.set(0, 8.5, 0);   // creux de la wavetable >= 8,5 - 2,3 x 1,45 = 5,2 ; cubes <= -3 + 2,4 x 1,2 = -0,1
        WT.update(1, Math.floor(u * 60) % WT.NF, t * (1.6 + A.mid), 1, 0.8 + 0.55 * A.mid);
        ground(-3.01, 0, 40);
        const a = lerp(0.6, -0.3, eIO(u));
        const d = PORTRAIT ? 38 : 32;
        setCam(v3(Math.sin(a) * d, lerp(14, 10, u), Math.cos(a) * d).add(shake(t, 0.3)), v3(0, PORTRAIT ? 1.5 : 3, 0));
        FX.decay = 0.7; FX.zoom = 0.003; FX.disp = 0.002;
        showTitle(S.fusion, t, 8, 12, { size: PORTRAIT ? 96 : 80, y: PORTRAIT ? 0.13 : 0.1, frame });
        showSub(S.fusionDetail, t, 8.4, 12, { size: PORTRAIT ? 21 : 22, y: 0.2 });
        extra = ` · GEN ${String(st.gen).padStart(4, '0')}`;
    } else {
        endShot(t, 12, 'wurli', 0.4, -0.15);
    }
    hud(t, extra);
}


// ---------------------------------------------------------------- pub 3 : HISTOIRE (le bonhomme présente RAW 2, ≈ 29 s)
// RAW 2 posé à plat et agrandi 4 fois : le bonhomme marche sur l'écran de l'OSC A, allume la machine
// avec le logo, saute sur le clavier et frappe une touche pile sur le drop de « shape ».
const BOARD_S = 4;
const surf = { panel: 1.8 + 0.52 * BOARD_S, chassis: 1.8 + 0.45 * BOARD_S, key: 1.8 + 0.73 * BOARD_S, led: 1.8 + 0.566 * BOARD_S };
function board() {
    device.scale.setScalar(BOARD_S);
    device.rotation.set(-Math.PI / 2, 0, 0);
    device.position.set(0, 1.8, 0);
    device.updateMatrixWorld(true);
}
// point de l'interface (px) → monde, sur le plateau posé à plat
const bp = (px, py, y) => v3((px / 100 - 6.4) * BOARD_S, y, (py / 100 - 4.8) * BOARD_S);
const heading = (dx, dz) => Math.atan2(dx, dz);
// projecteur : cône de lumière et flaque au sol, sur le bonhomme
const SPOT = (() => {
    const g = new THREE.Group();
    const cone = new THREE.Mesh(new THREE.ConeGeometry(2.6, 11, 40, 1, true), new THREE.MeshBasicMaterial({ color: 0xfff4e8, transparent: true, opacity: 0.05, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
    cone.position.y = 5.5;
    cone.material.opacity = 0.022;
    const c = document.createElement('canvas'); c.width = c.height = 128;
    const gx = c.getContext('2d'), gr = gx.createRadialGradient(64, 64, 0, 64, 64, 64);
    gr.addColorStop(0, 'rgba(255,245,235,0.32)'); gr.addColorStop(0.6, 'rgba(255,245,235,0.08)'); gr.addColorStop(1, 'rgba(255,245,235,0)');
    gx.fillStyle = gr; gx.fillRect(0, 0, 128, 128);
    const pool = new THREE.Mesh(new THREE.PlaneGeometry(6, 6), new THREE.MeshBasicMaterial({ map: new THREE.CanvasTexture(c), transparent: true, blending: THREE.AdditiveBlending, depthWrite: false }));
    pool.rotation.x = -Math.PI / 2; pool.position.y = 0.03;
    g.add(cone, pool);
    scene.add(g);
    g.visible = false;
    return g;
})();
// passerelle orange le long de la forme d'onde (plan Wurlitzer)
const RAIL = new THREE.Mesh(new THREE.BoxGeometry(1.1, 0.12, 60), new THREE.MeshStandardMaterial({ color: 0x1a1a1a, roughness: 0.5, metalness: 0.4 }));
const RAIL_EDGE = new THREE.Mesh(new THREE.BoxGeometry(0.05, 0.05, 60), new THREE.MeshBasicMaterial({ color: HOT.clone().multiplyScalar(0.8) }));
RAIL.add(RAIL_EDGE); RAIL_EDGE.position.set(-0.55, 0.06, 0);
scene.add(RAIL); RAIL.visible = false;

function showDialog(t) {
    const d = $('dialog');
    const cur = HIST.dialogues.find(x => t >= x.t - 0.12 && t < x.fin + 0.15);
    if (!cur) { d.style.display = 'none'; return; }
    d.style.display = 'block';
    d.style.top = (PORTRAIT ? 0.73 : 0.7) * H + 'px';
    d.querySelector('.who').textContent = '◼ BONHOMME D\'' + HIST.nom;
    const n = Math.max(0, Math.floor((t - cur.t) * HIST.cps));
    const blink = Math.floor(t * 4) % 2 === 0;
    d.querySelector('.txt').innerHTML = esc(cur.texte.slice(0, n)) + (blink || n < cur.texte.length ? '<span class="cur"></span>' : '');
    const a = prog(t, cur.t - 0.12, cur.t + 0.05), b = 1 - prog(t, cur.fin, cur.fin + 0.15);
    d.style.opacity = String(Math.min(a, b));
    d.style.transform = `translateY(${(1 - eOut(a)) * 24 * U}px) scaleY(${0.6 + 0.4 * eOut(Math.min(a, b))})`;
}
// pose finale du bonhomme : P (repère du corps) + position monde + cap
function bon(P, pos, rotY = 0, extra = {}) {
    BON.root.visible = true;
    BON.pose(Object.assign(P, { pos, rotY }, extra));
}
function camTo(pos, look, roll = 0) { setCam(pos.add(shake(REALT, 0.12)), look, roll); }

function adHistoire(t, frame) {
    hideAll();
    resetDevice();
    BON.root.visible = false;
    SPOT.visible = false;
    RAIL.visible = false;
    BON.root.scale.setScalar(1);
    floorMat.uniforms.uScan.value = -999;
    $('dialog').style.display = 'none';
    const T = HIST.temps, P5 = HIST.points, beat = 60 / HIST.musique.bpm;
    const [p1, p2, p3, p4, p5] = T.plans;
    const calloutY = PORTRAIT ? 0.66 : 0.6;
    flashAt(t, [{ t: T.drop, d: 0.32, o: 0.85 }, ...T.plans.map(x => ({ t: x, d: 0.12, o: 0.45, c: '#FF4D00' })), { t: T.fin, d: 0.3, o: 0.6 }]);
    cutFx(t, [T.marche[0], T.saut[0] - 0.052, T.drop, ...T.plans, T.fin]);
    let extra = '';
    // machine éteinte : la pièce est dans le noir (le chrome du bonhomme reste visible) ; la lumière
    // revient avec la mise sous tension
    if (t < T.saut[0] - 0.052) {
        const lum = t < T.allumage[0] ? 0 : eOut(prog(t, T.allumage[0], T.allumage[1]));
        key.intensity = lerp(0.1, 1.6, lum); amb.intensity = lerp(0.03, 0.25, lum); fill.intensity = lerp(0.04, 0.35, lum);
        scene.environmentIntensity = lerp(0.05, 0.55, lum);
    }
    if (t < T.marche[0]) {
        // 1. arrivée : la tête tombe et rebondit, le corps surgit, coucou
        only(device);
        board();
        devPower = 0;
        ground(0, 0.4, 40);
        const at = bp(205, 290, surf.panel + 0.01);
        SPOT.visible = true; SPOT.position.copy(at);
        const [b1, b2, b3] = T.rebonds, R0 = 0.39;
        let hy;  // hauteur du centre de la tête au-dessus du sol (tête seule)
        if (t < T.tete) hy = 30;
        else if (t < b1) hy = lerp(11, R0, Math.pow(prog(t, T.tete, b1), 2));
        else if (t < b2) { const u = prog(t, b1, b2); hy = R0 + 4 * 0.95 * u * (1 - u); }
        else if (t < b3) { const u = prog(t, b2, b3); hy = R0 + 4 * 0.32 * u * (1 - u); }
        else hy = R0;
        if (t < T.corps) {
            const P = POSE.idle(0);
            const squash = t >= b1 && t < b1 + 0.08 ? 0.75 : 1;
            bon(P, at, 0.35, { head: v3(0, hy - 2.06, 0), show: { head: true, body: false, limbs: false }, shadow: clamp(1.4 - hy / 8) });
            BON.root.children[0].children[1].scale.set(1 / Math.sqrt(squash), squash, 1 / Math.sqrt(squash));
        } else {
            BON.root.children[0].children[1].scale.set(1, 1, 1);
            const pop = eBack(prog(t, T.corps, T.corps + 0.26));
            const P = t >= T.coucou ? POSE.wave(t, eOut(prog(t, T.coucou, T.coucou + 0.3))) : POSE.idle(t);
            const lp = eBack(prog(t, T.membres, T.membres + 0.2));   // les membres jaillissent du corps
            if (lp < 1) for (const k of [0, 1]) { P.hands[k].lerp(v3(0, 1.2, 0), 1 - lp); P.feet[k].lerp(v3(0, 0.7, 0), 1 - lp); }
            bon(P, at, lerp(0.35, 0.15, prog(t, T.corps, 3)), { show: { head: true, body: true, limbs: t >= T.membres }, squash: lerp(0.4, 1, pop) * (P.squash || 1) });
        }
        const u = prog(t, 0, T.marche[0]);
        const look = at.clone().add(v3(0, PORTRAIT ? 1.05 : 1.2, 0));
        const d = PORTRAIT ? lerp(9.5, 7.6, eIO(u)) : lerp(8.2, 6.6, eIO(u));
        camTo(look.clone().add(v3(lerp(2.2, 1.2, u), lerp(1.6, 0.7, u), d)), look.clone().add(v3(0, lerp(PORTRAIT ? 0.6 : 0.3, 0, u), 0)));
        FX.decay = 0.35;
        extra = ' · SIGNAL ABSENT';
    } else if (t < T.appui - 0.46) {
        // 2. il traverse l'écran éteint jusqu'au logo
        only(device);
        board();
        devPower = 0;
        ground(0, 0.4, 40);
        const [ta, tb] = T.marche;
        const pts = [[205, 290], [160, 170], [118, 98], [84, 46]];
        const u = eIO(prog(t, ta + 0.05, tb));
        const seg = Math.min(2, Math.floor(u * 3)), f = u * 3 - seg;
        const [x0, y0] = pts[seg], [x1, y1] = pts[seg + 1];
        const px = lerp(x0, x1, f), py = lerp(y0, y1, f);
        const y = (py >= 72 ? surf.panel : surf.chassis) + (Math.abs(py - 125) < 6 ? 0.12 : 0);
        const at = bp(px, py, y);
        const walking = t < tb;
        const P = walking ? POSE.walk((t - ta) * 2.2, 0.9) : POSE.idle(t);
        bon(P, at, heading(x1 - x0, y1 - y0));
        SPOT.visible = true; SPOT.position.copy(at); SPOT.position.y = y;
        const look = at.clone().add(v3(0, 1.1, 0));
        camTo(look.clone().add(v3(PORTRAIT ? 4.5 : 5.5, 2.4, PORTRAIT ? 9.5 : 8.5)), look);
        FX.decay = 0.4;
        extra = ' · SIGNAL ABSENT';
    } else if (t < T.saut[0] - 0.052) {
        // 3. il appuie sur le logo : la machine s'allume, en onde
        only(device);
        board();
        const [pa, pb] = T.allumage;
        devPower = eOut(prog(t, pa, pb));
        devAt = [44, 42];
        devLogo = t >= T.appui ? 2.5 * Math.exp(-(t - T.appui) * 3) : 0;
        ground(0, lerp(0.4, 1.1, devPower), 40);
        const at = bp(86, 46, surf.chassis);
        const P = POSE.stomp(clamp((t - (T.appui - 0.46)) / 0.66));
        if (t < T.appui + 0.3) P.hands[0].y -= 0.35 * clamp((t - T.appui + 0.1) / 0.1);
        bon(P, at, -Math.PI / 2 + 0.25);
        SPOT.visible = devPower < 0.6; SPOT.position.copy(at);
        floorMat.uniforms.uScan.value = lerp(-30, 40, prog(t, pa, pb));
        const u = eIO(prog(t, T.appui - 0.2, T.saut[0] - 0.2));
        const close = at.clone().add(v3(3.2, 1.6, 6.5)), wide = v3(-4, PORTRAIT ? 46 : 38, PORTRAIT ? 46 : 36);
        const look = vl(at.clone().add(v3(-0.6, 0.8, 0)), v3(-2, 2, PORTRAIT ? 2 : 0), u);
        camTo(vl(close, wide, u), look);
        FX.decay = 0.55; FX.zoom = 0.002;
        extra = ` · ALIMENTATION ${String(Math.round(devPower * 100)).padStart(3, ' ')} %`;
    } else if (t < T.drop) {
        // 4. il saute sur le clavier et frappe le do pile dans le silence d'avant le drop
        only(device);
        board();
        ground(0, 1.0, 40);
        const [sa, sb] = T.saut;
        const kF = SYN.whiteKey(3), kC = SYN.whiteKey(7);
        const pF = v3(kF.x * BOARD_S, surf.key, -kF.y * BOARD_S), pC = v3(kC.x * BOARD_S, surf.key, -kC.y * BOARD_S);
        const tHit = T.frappe, dn = t >= tHit ? 0.045 * BOARD_S * clamp((t - tHit) / 0.04) : 0;
        devKeys = { 7: t >= tHit ? 1 : 0 };
        let P, at, rot;
        if (t < sa) { P = POSE.jump(0.05); at = pF; rot = Math.PI / 2; }
        else if (t < sb) { const u = prog(t, sa, sb); P = POSE.jump(u, 2.6); at = vl(pF, pC, eIO(u)); rot = Math.PI / 2; }
        else if (t < tHit - 0.56) { P = POSE.idle(t); at = pC; rot = lerp(Math.PI / 2, 0.2, eOut(prog(t, sb, sb + 0.4))); }
        else { P = POSE.stomp(clamp((t - (tHit - 0.56)) / 0.8)); at = pC; rot = 0.2; P.hands[0].y -= 0.25 * clamp((t - tHit + 0.1) / 0.1); }
        at = at.clone(); at.y -= dn;
        bon(P, at, rot);
        const u = prog(t, sb, tHit);
        const look = pC.clone().add(v3(0, 1.2, 0));
        const d = PORTRAIT ? lerp(12.5, 8.5, eIO(u)) : lerp(11, 7.5, eIO(u));
        camTo(look.clone().add(v3(lerp(-5, 2.5, eIO(prog(t, sa, tHit))), lerp(1.2, 0.5, u), d)), look.clone().add(v3(0, PORTRAIT ? 0.6 : 0.2, -2)));
        FX.decay = 0.45;
        FX.dither = Math.max(FX.dither, t >= tHit ? 1 : 0);
        extra = ' · DO 4';
    } else if (t < p1) {
        // 5. DROP : tout s'illumine, le nuage de pixels explose, il saute sur chaque temps
        only(device, CLOUD.g);
        board();
        ground(0, 1.6, 40);
        const kC = SYN.whiteKey(7);
        const pC = v3(kC.x * BOARD_S, surf.key, -kC.y * BOARD_S);
        devKeys = { 7: clamp(1 - (t - T.drop) * 1.5) };
        const bt = ((t - T.drop) / beat) % 1;
        const P = POSE.jump(bt, 0.9);
        P.hands = [v3(0.75, 2.4, 0.1), v3(-0.75, 2.4, 0.1)];
        bon(P, pC, 0.1);
        CLOUD.follow(device);
        CLOUD.set({ uAsm: 1 - eOut(prog(t, T.drop, T.drop + 0.9)), uR: 34, uNoise: lerp(1, 6, prog(t, T.drop, T.drop + 0.6)), uSpin: (t - T.drop) * 0.5, uBright: lerp(1, 0.4, prog(t, T.drop + 0.2, p1)) });
        const u = prog(t, T.drop, p1);
        const a = lerp(-0.7, 0.35, eIO(u)), d = PORTRAIT ? 52 : 44;
        camTo(v3(Math.sin(a) * d - 4, lerp(26, 30, u), Math.cos(a) * d + 4), v3(-4, 2, PORTRAIT ? 6 : 3));
        FX.decay = t < T.drop + 0.9 ? (PORTRAIT ? 0.86 : 0.7) : 0.55; FX.zoom = t < T.drop + 0.9 ? 0.012 : 0.003;
        if (!PORTRAIT) FX.bloom *= 0.7;   // en carré, le plateau remplit l'image : moins de halo
        showTitle('RAW 2', t, T.drop, p1, { size: PORTRAIT ? 150 : 120, y: PORTRAIT ? 0.1 : 0.06, accentLast: true, frame });
        showSub(TEXTES.accroche, t, T.drop + 0.3, p1, { size: PORTRAIT ? 21 : 22, y: PORTRAIT ? 0.205 : 0.195 });
    } else if (t < p2) {
        // 6. il surfe sur la wavetable
        only(WT.g);
        const u = prog(t, p1, p2);
        const am = 0.8 + 0.5 * A.mid, ph = t * (1.6 + A.mid);
        WT.g.scale.setScalar(1.4);
        WT.update(1, WT.NF - 1, ph, 1, am);
        const xs = 0.5 + 0.12 * Math.sin(u * Math.PI * 2);
        const hy = WT.height(WT.NF - 1, xs, ph, am) * 1.4, hy2 = WT.height(WT.NF - 1, xs + 0.01, ph, am) * 1.4;
        const at = v3((xs - 0.5) * WT.WD * 1.4, hy + 0.04, (WT.NF - 1 - WT.NF / 2) * WT.DZ * 1.4);
        const P = POSE.surf(t);
        P.tilt = Math.atan2(hy2 - hy, 0.01 * WT.WD * 1.4) * 0.8;
        bon(P, at, 0.15, { shadow: 0 });
        ground(-5.5, 1.2, 12);
        const look = at.clone().add(v3(0, 0.8, 0));
        camTo(look.clone().add(v3(lerp(-6, -3, u), lerp(2.2, 3.4, u), PORTRAIT ? 11 : 9.5)), look.clone().add(v3(0, PORTRAIT ? -1.2 : -0.3, -3)));
        FX.decay = 0.72; FX.zoom = 0.002;
        showCallout(1, P5[0], t, p1 + 0.05, p2, calloutY);
        extra = ' · OSC A/B';
    } else if (t < p3) {
        // 7. VIE : il marche sur la matrice, des anneaux de LED partent de ses pieds sur chaque temps
        only(device);
        board();
        ground(0, 1.0, 40);
        const u = prog(t, p2, p3);
        const px = lerp(520, 760, u), py = lerp(285, 205, u);
        const at = bp(px, py, surf.led);
        bon(POSE.walk((t - p2) * 2.2, 0.8), at, heading(240, -80));
        devRipples = [];
        for (let j = 0; j < 5; ++j) {
            const t0 = p2 + j * beat;
            if (t >= t0) devRipples.push({ x: px, y: py, r: (t - t0) * 260, w: 7 });
        }
        const look = at.clone().add(v3(0, 1, 0));
        camTo(look.clone().add(v3(-4, PORTRAIT ? 5.5 : 4.5, PORTRAIT ? 10 : 9)), look.clone().add(v3(0, PORTRAIT ? -1.4 : -0.4, 0)));
        FX.decay = 0.5;
        showCallout(2, P5[1], t, p2 + 0.05, p3, calloutY);
        extra = ' · GEN ' + String(Math.floor(t * 8)).padStart(4, '0');
    } else if (t < p4) {
        // 8. il fait tourner un potard plus grand que lui (cliquets sur chaque cran)
        only(device);
        board();
        ground(0, 1.0, 40);
        const u = prog(t, p3, p4);
        const kc = SYN.knobs[0];
        const c = bp(kc.x, kc.y, surf.panel);
        const th = -0.6 + u * 3.2;   // il tourne autour du potard
        const rr = 0.84 + 0.62;
        const at = c.clone().add(v3(Math.cos(th) * rr, 0, Math.sin(th) * rr));
        devKnobs = { 0: clamp(0.15 + u * 0.8) };
        devMode = 'filter'; devCut = 0.2 + 0.7 * u;
        const P = POSE.push(t);
        bon(P, at, heading(-Math.sin(th), Math.cos(th)) + 0.9);
        const look = c.clone().add(v3(0, 1.0, 0));
        camTo(look.clone().add(v3(3.5, PORTRAIT ? 6.5 : 5, PORTRAIT ? 9 : 8)), look.clone().add(v3(0, PORTRAIT ? -1.2 : -0.3, 0)), 0.03);
        FX.decay = 0.25;
        showCallout(3, P5[2], t, p3 + 0.05, p4, calloutY);
        extra = ' · POSITION ' + Math.round(clamp(0.15 + u * 0.8) * 100) + ' %';
    } else if (t < p5) {
        // 9. il court le long de la forme d'onde du Wurlitzer, un saut par temps
        only(BARS.g);
        const u = prog(t, p4, p5);
        BARS.g.position.set(0, 0, 0); BARS.g.rotation.set(0, 0, 0);
        const zr = lerp(-14, 14, u);
        BARS.update(1, clamp((zr + 25.6) / 51.2));
        RAIL.visible = true; RAIL.position.set(1.5, -0.06, 0);
        const bt = ((t - p4) / beat) % 1;
        const P = POSE.jump(bt, 0.55);
        Object.assign(P, { lean: 0.15 });
        bon(P, v3(1.5, 0, zr), Math.PI, { shadow: 0.6 });
        ground(-6.5, 1.2, 10);
        const look = v3(1.2, 1.2, zr);
        camTo(look.clone().add(v3(PORTRAIT ? 9 : 8, PORTRAIT ? 3.5 : 2.8, 4)), look.clone().add(v3(0, PORTRAIT ? -1 : -0.2, -2)));
        FX.decay = 0.45;
        showCallout(4, P5[3], t, p4 + 0.05, p5, calloutY);
        extra = ' · ZONE C#4 · 36 ZONES';
    } else if (t < T.fin) {
        // 10. les 6 thèmes défilent derrière lui (carrousel espacé : aucun contact)
        only(FAN.g);
        const u = prog(t, p5, T.fin);
        FAN.g.position.set(0, 0, 0);
        FAN.update(t, p5, eIO(u));
        ground(-3.6, 1.0, 16);
        bon(POSE.wave(t, 1), v3(0, -3.6, 5.5), 0, {});
        camTo(v3(lerp(-2, 2, u), PORTRAIT ? 0.5 : 0.8, PORTRAIT ? 24 : 21), v3(0, PORTRAIT ? -3 : -1.5, -2));
        FX.decay = 0.15;
        showCallout(5, P5[4], t, p5 + 0.05, T.fin, calloutY);
        extra = ' · THÈME ' + ['ALU', 'NOTHING', 'TITANE', 'CRT', 'ULTRAVIOLET', 'ONIRIQUE'][Math.floor(u * 6) % 6];
    } else {
        // 11. fin : assis sur RAW 2, il fait coucou ; puis le carton ONIRIQUE
        endShot(t, T.fin, 'scope', -0.3, 0.1);
        if (!PORTRAIT) device.position.y = 0.95;   // en carré : un peu plus bas, loin du texte du haut
        device.updateMatrixWorld(true);
        const top = device.localToWorld(v3(PORTRAIT ? 2.4 : 4.4, 5.15, 0.05));
        const P = POSE.sit(t);
        const W = POSE.wave(t, eOut(prog(t, T.fin + 0.15, T.fin + 0.45)) * (PORTRAIT ? 1 : 0.5));
        P.hands[1] = W.hands[1]; P.elbows[1] = W.elbows[1];
        const bs = PORTRAIT ? 0.95 : 0.6;
        BON.root.scale.setScalar(bs);
        bon(P, top.add(v3(0, -0.07 * bs, 0)), device.rotation.y, { shadow: 0 });
        el.end.style.display = el.end2.style.display = 'none';
        el.end.querySelector('.tagline').style.display = PORTRAIT ? '' : 'none';   // en carré : place pour le bonhomme assis
        showEnd(t, T.carton, PORTRAIT ? 0.12 : 0.06, PORTRAIT ? 0.665 : 0.6);
        if (t < T.carton) { el.end.style.display = el.end2.style.display = 'none'; }
    }
    showDialog(t);
    hud(t, extra);
}

// ---------------------------------------------------------------- point d'entrée
const AD_FN = { lancement: adLancement, 'le-son': adLeSon, histoire: adHistoire }[AD];
const DURATION = (AD === 'histoire' ? HIST.temps.total : { lancement: 20, 'le-son': 16 }[AD]) / K;
window.DURATION = DURATION;
CLOUD.mat.uniforms.uScale.value = H / (2 * Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)));
window.renderFrame = rt => {
    // rt : temps réel de la vidéo (musique) ; t : temps du montage (écrit à 120 BPM)
    REALT = rt;
    const t = rt * K;
    const frame = Math.round(rt * FPS);
    A = aud(rt);
    KICK = hitEnv(rt, 10); KICK9 = hitEnv(rt, 9); KICK16 = hitEnv(rt, 16);
    fxDefaults(t, frame);
    key.intensity = 1.6; amb.intensity = 0.25; fill.intensity = 0.35; scene.environmentIntensity = 0.55;
    WT.g.position.set(0, 0, 0);
    LIFE.g.position.set(0, 0, 0);
    floorMat.uniforms.uT.value = t;
    floorMat.uniforms.uKick.value = KICK;
    CLOUD.mat.uniforms.uT.value = t;
    CLOUD.mat.uniforms.uLow.value = A.low;
    AD_FN(t, frame);
    if (device.visible) {
        const fi = Math.min(AUD.n - 1, Math.round(rt * AUD.fps)), sc = AUD.scope[fi] || [];
        const sm = sc.map((v, i) => { let s = 0, n = 0; for (let j = -6; j <= 6; ++j) { const x = sc[i + j]; if (x !== undefined) { s += x; ++n; } } return (s / n) * 1.6; });
        SYN.update({ t, A, kick: KICK, chroma: AUD.chroma[fi], scopeA: sc, scopeB: sm, mode: devMode, play: devPlay, cut: devCut, wurli,
                     power: devPower, powerAt: devAt, keyHit: devKeys, knobSet: devKnobs, ripples: devRipples, logoPress: devLogo });
    }
    spectrum();
    applyFx(t);
    grain(frame);
    composer.render();
    return frame;
};
await document.fonts.load(`20px PixB`);
await document.fonts.load(`20px Mono`);
await document.fonts.load(`20px MonoM`);
await document.fonts.load(`20px MonoB`);
await document.fonts.ready;
window.renderFrame(0);
window.READY = true;
