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
const SCORE = scoreHits(scoreTxt);
const hitEnv = (t, k = 16) => {
    let s = 0;
    for (const h of SCORE.hits) if (t >= h.t) s = Math.max(s, h.v * Math.exp(-(t - h.t) * k));
    return s;
};

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
const bloom = new UnrealBloomPass(new THREE.Vector2(W, H), 0.85, 0.55, 1.0);
composer.addPass(bloom);
composer.addPass(new OutputPass());

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
scene.add(new THREE.AmbientLight(0xffffff, 0.25));
const key = new THREE.DirectionalLight(0xffffff, 1.6);
key.position.set(-8, 14, 12);
scene.add(key);
const rim = new THREE.PointLight(0xff4d00, 260, 60, 1.6);
rim.position.set(6, 4, -8);
scene.add(rim);
const fill = new THREE.DirectionalLight(0x9fb4ff, 0.35);
fill.position.set(10, -4, 6);
scene.add(fill);

// ---------------------------------------------------------------- sol : grille de points du châssis + balayage orange
const floorMat = new THREE.ShaderMaterial({
    uniforms: { uScan: { value: -999 }, uScanW: { value: 1.5 }, uDot: { value: 0.22 } },
    vertexShader: `varying vec3 vW; void main(){ vec4 w = modelMatrix * vec4(position,1.); vW = w.xyz; gl_Position = projectionMatrix * viewMatrix * w; }`,
    fragmentShader: `varying vec3 vW; uniform float uScan, uScanW, uDot;
      void main(){
        vec2 g = fract(vW.xz) - .5; float d = length(g);
        float px = max(fwidth(vW.x), fwidth(vW.z));
        float dots = smoothstep(.075 + px, .035, d) * uDot * clamp(.16 / px - .4, 0., 1.);  // pas de moiré au loin
        vec2 q = vW.xz / 8.; vec2 l = abs(fract(q) - .5); vec2 fw = fwidth(q);
        vec2 ln = 1. - smoothstep(vec2(0.), fw * 1.2, .5 - l);
        float big = max(ln.x, ln.y) * .10 * clamp(.06 / max(fw.x, fw.y), 0., 1.);
        float fade = exp(-length(vW.xz) * .028);
        float s = exp(-pow((vW.z - uScan) / uScanW, 2.));
        vec3 c = vec3(dots + big) * fade + vec3(3.2, .95, .06) * s * (dots * 4. + .05) * fade;
        gl_FragColor = vec4(c, 1.);
      }`,
});
const floor = new THREE.Mesh(new THREE.PlaneGeometry(400, 400), floorMat);
floor.rotation.x = -Math.PI / 2;
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
const TX = {};
for (const n of ['son', 'vie', 'wavetable', 'synth', 'inst', 'theme0', 'theme1', 'theme2', 'theme3', 'theme4', 'theme5']) TX[n] = await tex(n + '.png');

// ---------------------------------------------------------------- l'appareil RAW 2
const bodyMat = new THREE.MeshStandardMaterial({ color: 0xb9b9b3, metalness: 0.55, roughness: 0.5, envMapIntensity: 0.4 });
const darkMat = new THREE.MeshStandardMaterial({ color: 0x1a1a1a, metalness: 0.6, roughness: 0.35 });
const bodyGeo = new RoundedBoxGeometry(13.5, 10.3, 0.95, 5, 0.34);
const screwGeo = new THREE.CylinderGeometry(0.15, 0.15, 0.08, 20);
function makeDevice(map) {
    const g = new THREE.Group();
    g.add(new THREE.Mesh(bodyGeo, bodyMat));
    const face = new THREE.Mesh(new THREE.PlaneGeometry(12.8, 9.6), new THREE.MeshBasicMaterial({ map, color: 0xededed }));
    face.position.z = 0.478;
    g.add(face);
    const strip = new THREE.Mesh(new THREE.BoxGeometry(11.6, 0.07, 0.06), new THREE.MeshBasicMaterial({ color: HOT.clone() }));
    strip.position.set(0, -5.02, 0.45);
    g.add(strip);
    for (const [x, y] of [[-6.45, 4.85], [6.45, 4.85], [-6.45, -4.85], [6.45, -4.85]]) {
        const s = new THREE.Mesh(screwGeo, darkMat);
        s.rotation.x = Math.PI / 2;
        s.position.set(x, y, 0.47);
        g.add(s);
    }
    g.userData = { face, strip };
    return g;
}
const device = makeDevice(TX.son);
scene.add(device);
// point de l'écran (coordonnées de l'interface 1280 x 960) en coordonnées de l'appareil
const uiPt = (px, py) => v3((px / 1280 - 0.5) * 12.8, (0.5 - py / 960) * 9.6, 0.478);

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
    function update(reveal, hot, ph, squash = 1) {
        for (let k = 0; k < NF; ++k) {
            const s = strips[k];
            const vis = clamp(reveal * NF - k);
            s.m.visible = vis > 0;
            if (!s.m.visible) continue;
            const f = k / (NF - 1);
            const isHot = Math.abs(k - hot) < 0.5;
            const th = isHot ? TH * 2.6 : TH;
            const amp = AMP * eOut(vis) * squash;
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
    return { g, update, NF };
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
    function update(gt) {
        const gi = clamp(Math.floor(gt), 0, GENS - 1), fr = gt - Math.floor(gt);
        const A = gens[gi];
        for (let i = 0; i < N * N; ++i) {
            const x = i % N, y = (i / N) | 0, a = A[i];
            let h, c, cap;
            if (a) {
                const grow = a === 1 ? eOut(clamp(fr * 2.5)) : 1;
                h = (0.35 + 2.4 * Math.max(0, 1 - (a - 1) / 6)) * grow + 0.06;
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

// ---------------------------------------------------------------- les 6 thèmes en éventail
const FAN = (() => {
    const g = new THREE.Group();
    const items = [];
    for (let i = 0; i < 6; ++i) {
        const d = makeDevice(TX['theme' + i]);
        d.scale.setScalar(0.62);
        g.add(d);
        items.push(d);
    }
    function update(open, spin) {
        items.forEach((d, i) => {
            const k = i - 2.5;
            const a = k * 0.42 * open + spin;
            d.position.set(Math.sin(a) * 16 * open, (PORTRAIT ? -k * 2.4 : -k * 0.6) * open, Math.cos(a) * 16 * open - 16 * open);
            d.rotation.set(0, a, 0);
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
    const beat = (t - t0) * SCORE.bpm / 60, pulse = Math.exp(-(beat % 1) * 6);
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
    el.hudBL.textContent = `T+00:${String(s).padStart(2, '0')}:${String(f).padStart(2, '0')} · ${SCORE.bpm} BPM${extra}`;
    el.rec.style.opacity = Math.floor(t * SCORE.bpm / 60 * 2) % 2 === 0 ? '1' : '0.25';
}
function shadeBottom(on) { $('shade').style.opacity = on ? '1' : '0'; }
function hideAll() {
    shadeBottom(false);
    for (const k of ['term', 'title', 'sub', 'callout', 'list', 'end', 'end2']) el[k].style.display = 'none';
    el.hudTL.style.display = 'block';
    $('hudBot').style.display = 'block';
}

// ---------------------------------------------------------------- scène : visibilité et petites aides
function only(...gs) {
    for (const g of [device, WT.g, LIFE.g, BARS.g, FAN.g]) g.visible = gs.includes(g);
}
function shake(t, amt = 0.35) {
    const e = hitEnv(t) * amt;
    return v3(Math.sin(t * 91.7) * e, Math.cos(t * 73.3) * e, 0);
}
function pulseStrip(t, obj = device) {
    const e = hitEnv(t, 9);
    obj.userData.strip.material.color.copy(HOT).multiplyScalar(0.5 + 1.2 * e);
    rim.intensity = 180 + 520 * e;
}
function resetDevice() {
    device.position.set(0, 0, 0);
    device.rotation.set(0, 0, 0);
    device.scale.setScalar(1);
    device.userData.face.material.map = TX.son;
}
const DEV_D = fitDist(13.5 * 1.12, 10.3 * 1.12);

// carton de fin commun : l'appareil tourne lentement entre le logo et l'appel à l'action
function endShot(t, t0, map, r0, r1) {
    only(device);
    floor.position.y = -6.5;
    const u = prog(t, t0, t0 + 4);
    device.userData.face.material.map = map;
    device.rotation.set(-0.08, lerp(r0, r1, eIO(u)), 0);
    device.scale.setScalar(PORTRAIT ? 0.78 : 0.5);
    device.position.set(0, PORTRAIT ? 0.5 : 1.15, 0);
    setCam(v3(0, 0.6, DEV_D * lerp(1.0, 0.94, u)), v3(0, 0.2, 0));
    pulseStrip(t);
    floorMat.uniforms.uScan.value = lerp(-20, 30, prog(t, t0, t0 + 2));
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
    const cuts = [{ t: 4, d: 0.35 }, { t: 6, d: 0.12, o: 0.5, c: '#FF4D00' }, { t: 8, d: 0.12, o: 0.5, c: '#FF4D00' }, { t: 10, d: 0.12, o: 0.5, c: '#FF4D00' },
                  { t: 12, d: 0.12, o: 0.5, c: '#FF4D00' }, { t: 14, d: 0.12, o: 0.5, c: '#FF4D00' }, { t: 16, d: 0.4 }];
    flashAt(t, cuts);
    const calloutY = PORTRAIT ? 0.66 : 0.6;
    let extra = '';
    if (t < 4) {
        // intro : la wavetable se dessine frame après frame
        only(WT.g);
        const u = t / 4;
        const squash = t > 3.55 ? 1 - eIO(prog(t, 3.55, 4)) * 0.92 : 1;
        WT.update(eOut(prog(t, 0.1, 3.2)), Math.floor(prog(t, 0.1, 3.2) * (WT.NF - 1)), t * 1.3, squash);
        const d = PORTRAIT ? 24 : 21;
        const a = lerp(-0.55, 0.35, eIO(u));
        setCam(v3(Math.sin(a) * d, lerp(7, 4, u), Math.cos(a) * d).add(v3(0, 0, 0)), v3(0, -0.3, 0), lerp(0.04, -0.03, u));
        floor.position.y = -4.2;
        floorMat.uniforms.uScan.value = lerp(-30, 30, prog(t, 0.2, 3.8));
        showTerm(L.intro, t, 0.15, 4);
        showTitle(L.presente, t, 2.1, 4, { size: 34, y: PORTRAIT ? 0.74 : 0.72, frame });
        extra = ' · FRAME ' + String(Math.floor(prog(t, 0.1, 3.2) * 255) + 1).padStart(3, '0') + '/256';
    } else if (t < 6) {
        // drop : l'appareil tombe, le logo s'abrase
        only(device);
        floor.position.y = -6.5;
        const p = prog(t, 4, 4.22);
        device.position.y = lerp(16, PORTRAIT ? 0 : -1.5, eBack(p));
        if (!PORTRAIT) device.scale.setScalar(0.74);
        device.rotation.set(lerp(-0.5, -0.12, eOut(p)), lerp(0.4, -0.28, eIO(prog(t, 4, 6))), lerp(0.18, 0, eOut(p)));
        const u = prog(t, 4, 6);
        const d = DEV_D * lerp(1.05, 1.18, u);
        setCam(v3(lerp(-2, 2, u), lerp(-1.2, 0.4, u) - (PORTRAIT ? 1.2 : 0), d).add(shake(t, 0.6)), v3(0, PORTRAIT ? -1.6 : -0.3, 0));
        pulseStrip(t);
        showTitle('RAW 2', t, 4, 6, { size: PORTRAIT ? 150 : 120, y: PORTRAIT ? 0.13 : 0.08, accentLast: true, frame });
        showSub(T.accroche, t, 4.35, 6, { size: PORTRAIT ? 21 : 22, y: PORTRAIT ? 0.235 : 0.215 });
        floorMat.uniforms.uScan.value = lerp(-10, 40, prog(t, 4, 5));
    } else if (t < 8) {
        // [01] oscillateurs : la wavetable en grand, la frame jouée en orange
        only(WT.g);
        const u = prog(t, 6, 8);
        WT.update(1, Math.floor(u * 40) % WT.NF, t * 2.2, 1);
        floor.position.y = -4.2;
        const a = lerp(0.65, 0.2, eIO(u));
        const d = PORTRAIT ? lerp(17, 14, u) : lerp(15, 12, u);
        setCam(v3(Math.sin(a) * d, lerp(2.5, 3.6, u), Math.cos(a) * d).add(shake(t, 0.25)), v3(0, PORTRAIT ? 1.2 : 0, -1), 0.05);
        showCallout(1, pts[0], t, 6.05, 8, calloutY);
        extra = ' · OSC A/B';
    } else if (t < 10) {
        // [02] VIE : la grille vit à la double croche (8 générations par seconde)
        only(LIFE.g);
        const u = prog(t, 8, 10);
        const st = LIFE.update(12 + (t - 8) * 8);
        const a = lerp(-0.5, 0.25, u);
        const d = PORTRAIT ? 34 : 30;
        floor.position.y = -0.01;
        setCam(v3(Math.sin(a) * d, lerp(24, 18, u), Math.cos(a) * d).add(shake(t, 0.3)), v3(0, PORTRAIT ? -6 : 0, PORTRAIT ? 4 : 0));
        showCallout(2, pts[1], t, 8.05, 10, calloutY);
        extra = ` · GEN ${String(st.gen).padStart(4, '0')} · ${st.alive}`;
    } else if (t < 12) {
        // [03] instruments : gros plan sur l'écran de l'OSC A qui joue le Wurlitzer
        only(device, BARS.g);
        device.userData.face.material.map = TX.inst;
        const u = prog(t, 10, 12);
        const target = uiPt(205, 270);
        const d = PORTRAIT ? lerp(9.5, 7.2, eIO(u)) : lerp(8.5, 6.5, eIO(u));
        setCam(target.clone().add(v3(lerp(3.2, 1.0, u), lerp(-1.5, -0.6, u), d)).add(shake(t, 0.15)), target.clone().add(v3(0, PORTRAIT ? -2.2 : -0.8, 0)), -0.03);
        BARS.g.position.set(10, 0, -18);
        BARS.g.rotation.set(0, 0.5, 0);
        BARS.update(1, u);
        pulseStrip(t);
        showCallout(3, pts[2], t, 10.05, 12, calloutY);
        extra = ' · ZONE C#4 · 36 ZONES';
    } else if (t < 14) {
        // [04] synthèse : travelling sur la page SYNTH (filtre puis warps)
        only(device);
        device.userData.face.material.map = TX.synth;
        const u = prog(t, 12, 14);
        const a = uiPt(300, 230), b = uiPt(1000, 250);
        const look = vl(a, b, eIO(u));
        const d = PORTRAIT ? 10.5 : 9;
        setCam(look.clone().add(v3(-2.5 + 5 * u, -2.2, d)).add(shake(t, 0.15)), look.clone().add(v3(0, PORTRAIT ? -2.4 : -0.6, 0)), 0.04);
        pulseStrip(t);
        showCallout(4, pts[3], t, 12.05, 14, calloutY);
        extra = ' · FILTRE · WARP';
    } else if (t < 16) {
        // [05] thèmes : six appareils en éventail
        only(FAN.g);
        const u = prog(t, 14, 16);
        FAN.update(eOut(prog(t, 14, 14.6)), lerp(-0.25, 0.25, eIO(u)));
        const d = PORTRAIT ? 30 : 26;
        setCam(v3(lerp(-3, 3, u), PORTRAIT ? 1 : 2, d).add(shake(t, 0.25)), v3(0, PORTRAIT ? -2.5 : 0, -6));
        floor.position.y = -6.5;
        showCallout(5, pts[4], t, 14.05, 16, calloutY);
        extra = ' · THÈME ' + ['ALU', 'NOTHING', 'TITANE', 'CRT', 'ULTRAVIOLET', 'ONIRIQUE'][Math.floor(u * 6) % 6];
    } else {
        endShot(t, 16, TX.son, -0.42, 0.18);
    }
    hud(t, extra);
}

// ---------------------------------------------------------------- pub 2 : LE SON (16 s)
function adLeSon(t, frame) {
    hideAll();
    resetDevice();
    floorMat.uniforms.uScan.value = -999;
    const S = T['le-son'];
    flashAt(t, [{ t: 2, d: 0.35 }, { t: 8, d: 0.16, o: 0.6, c: '#FF4D00' }, { t: 12, d: 0.4 }]);
    let extra = '';
    if (t < 2) {
        // intro : l'échantillon de Wurlitzer se dresse note après note
        only(BARS.g);
        const notes = [0, 0.25, 0.5, 0.875, 1.25, 1.5];
        let rev = 0;
        notes.forEach((n, i) => { if (t >= n) rev = Math.max(rev, lerp((i) / notes.length, (i + 1) / notes.length, eOut(prog(t, n, n + 0.22)))); });
        BARS.g.position.set(0, 0, 0);
        BARS.g.rotation.set(0, 0, 0);
        BARS.update(rev, rev);
        floor.position.y = -5.8;
        const u = t / 2;
        const a = lerp(1.2, 1.02, eIO(u)), d = PORTRAIT ? lerp(33, 27, u) : lerp(30, 25, u);
        setCam(v3(Math.sin(a) * d, lerp(5, 3, u), Math.cos(a) * d), v3(0, PORTRAIT ? -2.5 : 0, -3), -0.06);
        showTitle(S.intro, t, 0.3, 2, { size: PORTRAIT ? 92 : 80, y: PORTRAIT ? 0.16 : 0.12, frame });
        extra = ' · ' + wurli.name.toUpperCase();
    } else if (t < 8) {
        // 23 instruments : l'appareil tombe, la liste défile sur les temps
        only(device);
        device.userData.face.material.map = TX.inst;
        floorMat.uniforms.uScan.value = lerp(-25, 35, ((t - 2) / 2) % 1);
        const p = prog(t, 2, 2.22);
        const u = prog(t, 2, 8);
        device.position.set(0, lerp(16, PORTRAIT ? 1.2 : 2.0, eBack(p)), 0);
        device.rotation.set(lerp(-0.5, -0.1, eOut(p)), lerp(-0.35, 0.3, eIO(u)), lerp(-0.15, 0, eOut(p)));
        device.scale.setScalar(PORTRAIT ? 0.82 : 0.5);
        setCam(v3(0, PORTRAIT ? 0.5 : 0.8, DEV_D * (PORTRAIT ? 0.95 : 1.05)).add(shake(t, 0.5)), v3(0, PORTRAIT ? -1.2 : 0.4, 0));
        pulseStrip(t);
        showTitle(PORTRAIT ? S.titre.replace(' RÉELS', '\nRÉELS') : S.titre, t, 2, 8, { size: PORTRAIT ? 84 : 54, y: PORTRAIT ? 0.1 : 0.085, frame });
        // liste : un instrument par temps (0,5 s)
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
        // fusion : la wavetable au-dessus de la grille vivante
        only(WT.g, LIFE.g);
        const u = prog(t, 8, 12);
        WT.g.position.set(0, 6.5, 0);
        WT.update(1, Math.floor(u * 60) % WT.NF, t * 2, 1);
        LIFE.g.position.set(0, -1, 0);
        const st = LIFE.update(30 + (t - 8) * 8);
        floor.position.y = -1.01;
        const a = lerp(0.6, -0.3, eIO(u));
        const d = PORTRAIT ? 36 : 30;
        setCam(v3(Math.sin(a) * d, lerp(15, 11, u), Math.cos(a) * d).add(shake(t, 0.3)), v3(0, PORTRAIT ? 0.5 : 2.5, 0));
        showTitle(S.fusion, t, 8, 12, { size: PORTRAIT ? 96 : 80, y: PORTRAIT ? 0.13 : 0.1, frame });
        showSub(S.fusionDetail, t, 8.4, 12, { size: PORTRAIT ? 21 : 22, y: PORTRAIT ? 0.2 : 0.2 });
        extra = ` · GEN ${String(st.gen).padStart(4, '0')}`;
    } else {
        endShot(t, 12, TX.inst, 0.4, -0.15);
    }
    hud(t, extra);
}

// ---------------------------------------------------------------- point d'entrée
WT.g.position.set(0, 0, 0);
LIFE.g.position.set(0, 0, 0);
const AD_FN = { lancement: adLancement, 'le-son': adLeSon }[AD];
const DURATION = { lancement: 20, 'le-son': 16 }[AD];
window.DURATION = DURATION;
window.renderFrame = t => {
    const frame = Math.round(t * FPS);
    WT.g.position.set(0, 0, 0);
    LIFE.g.position.set(0, 0, 0);
    AD_FN(t, frame);
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
