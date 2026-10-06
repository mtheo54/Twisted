"use strict";
// ============================================================================
// Petit moteur d'animations : enchaîne des cibles (x, y, z) sur un objet.
// ============================================================================
const timelines = new Map();
const easeOut = (t) => 1 - Math.pow(1 - t, 3);
const easeInOut = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
const easeBack = (t) => { const c = 1.7; return 1 + (c + 1) * Math.pow(t - 1, 3) + c * Math.pow(t - 1, 2); };
const linear = (t) => t;
function play(target, steps, done) { timelines.set(target, { steps: steps.slice(), i: 0, t: 0, from: null, done }); }
function updateTimelines(dt) {
  for (const [target, tl] of timelines) {
    const step = tl.steps[tl.i];
    if (!tl.from) tl.from = { x: target.x, y: target.y, z: target.z };
    tl.t += dt;
    const k = step.dur > 0 ? Math.min(1, tl.t / step.dur) : 1, e = (step.ease || easeOut)(k);
    for (const a of ["x", "y", "z"]) if (step.to[a] !== undefined) target[a] = tl.from[a] + (step.to[a] - tl.from[a]) * e;
    if (k >= 1) { tl.i++; tl.t = 0; tl.from = null; if (tl.i >= tl.steps.length) { timelines.delete(target); if (tl.done) tl.done(); } }
  }
}

// ============================================================================
// Rendu, ciel, lumières
// ============================================================================
const container = document.getElementById("game");
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
renderer.outputEncoding = THREE.sRGBEncoding;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.08;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.shadowMap.autoUpdate = false;
container.appendChild(renderer.domElement);
const MAX_ANISO = renderer.capabilities.getMaxAnisotropy();

const scene = new THREE.Scene();
scene.fog = new THREE.FogExp2(new THREE.Color(0xe6cdbf), 0.0011);
const camera = new THREE.PerspectiveCamera(70, 1, 0.08, 4000);

const SUN_DIR = new THREE.Vector3(-0.55, 0.5, -0.66).normalize();
const sky = new THREE.Mesh(new THREE.SphereGeometry(2500, 32, 16), new THREE.ShaderMaterial({
  side: THREE.BackSide, depthWrite: false,
  uniforms: { top: { value: new THREE.Color(0x7d92b2) }, horizon: { value: new THREE.Color(0xf2d3bd) }, ground: { value: new THREE.Color(0x8d8794) }, sunDir: { value: SUN_DIR }, night: { value: 0 }, sunDisc: { value: 1 }, time: { value: 0 } },
  vertexShader: `varying vec3 vDir; void main(){ vDir = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }`,
  fragmentShader: `uniform vec3 top; uniform vec3 horizon; uniform vec3 ground; uniform vec3 sunDir; uniform float night; uniform float sunDisc; uniform float time; varying vec3 vDir;
    float hash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
    void main(){ vec3 d = normalize(vDir); float h = d.y;
      vec3 c = h > 0.0 ? mix(horizon, top, pow(min(h * 1.6, 1.0), 0.7)) : mix(horizon, ground, min(-h * 6.0, 1.0));
      c += mix(vec3(1.0, 0.85, 0.7), vec3(1.0, 0.45, 0.7), night) * exp(-abs(h) * 22.0) * (0.18 + 0.12 * night);
      float s = max(dot(d, normalize(sunDir)), 0.0);
      c += vec3(1.0, 0.86, 0.7) * (pow(s, 600.0) * 2.5 + pow(s, 12.0) * 0.18) * sunDisc;
      // étoiles, qui scintillent un peu
      vec3 cell = floor(d * 380.0); float st = hash(cell);
      c += vec3(0.9, 0.92, 1.0) * step(0.9968, st) * night * smoothstep(0.02, 0.2, h) * (0.6 + 0.4 * sin(time * 3.0 + st * 80.0));
      gl_FragColor = vec4(c, 1.0); }`,
}));
scene.add(sky);

const hemi = new THREE.HemisphereLight(0xc9d2e6, 0x8a6f6a, 0.75);
const sun = new THREE.DirectionalLight(0xffd6b5, 1.7);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
Object.assign(sun.shadow.camera, { left: -45, right: 45, top: 45, bottom: -45, near: 1, far: 300 });
sun.shadow.bias = -0.0005;
hemi.layers.enableAll(); sun.layers.enableAll();
scene.add(hemi, sun, sun.target);

// ============================================================================
// Textures dessinées par code (style peint, pas photo)
// ============================================================================
let seed = 20261006;
const rand = () => { seed = (seed * 16807) % 2147483647; return (seed - 1) / 2147483646; };
const range = (a, b) => a + (b - a) * rand();
const pick = (arr) => arr[Math.floor(rand() * arr.length)];
const JP = '"Zen Kaku Gothic New", "Yu Gothic", "Meiryo", "Hiragino Sans", "Noto Sans JP", sans-serif';

function canvasTex(w, h, draw, repeat = true) {
  const c = document.createElement("canvas"); c.width = w; c.height = h;
  draw(c.getContext("2d"), w, h);
  const t = new THREE.CanvasTexture(c);
  t.encoding = THREE.sRGBEncoding; t.anisotropy = MAX_ANISO;
  if (repeat) t.wrapS = t.wrapT = THREE.RepeatWrapping;
  return t;
}
function speckle(g, w, h, n, colors, size) { for (let i = 0; i < n; i++) { g.fillStyle = pick(colors); g.fillRect(rand() * w, rand() * h, size, size); } }
function shade(hex, f) { const c = new THREE.Color(hex).multiplyScalar(f); return "#" + c.getHexString(); }

let TEX = {};
function buildTextures() {
  TEX.asphalt = canvasTex(512, 512, (g, w, h) => {
    g.fillStyle = "#4a4954"; g.fillRect(0, 0, w, h);
    for (let i = 0; i < 7; i++) { g.fillStyle = rand() < 0.5 ? "rgba(30,30,38,0.16)" : "rgba(110,108,120,0.10)"; g.fillRect(rand() * w, rand() * h, range(40, 160), range(30, 120)); }
    speckle(g, w, h, 12000, ["#5a5964", "#3c3b45", "#63616c", "#35343d", "#504f5a"], 2);
    g.strokeStyle = "rgba(24,23,30,0.75)"; g.lineWidth = 1.6;
    for (let i = 0; i < 7; i++) { let x = rand() * w, y = rand() * h; g.beginPath(); g.moveTo(x, y); for (let k = 0; k < 7; k++) { x += range(-28, 28); y += range(-28, 28); g.lineTo(x, y); } g.stroke(); }
  });
  TEX.tiles = canvasTex(256, 256, (g, w, h) => {
    g.fillStyle = "#7f7c84"; g.fillRect(0, 0, w, h);
    const n = 4, s = w / n;
    for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) { const v = Math.floor(range(168, 186)); g.fillStyle = `rgb(${v},${v - 4},${v + 2})`; g.fillRect(i * s + 2, j * s + 2, s - 4, s - 4); }
    speckle(g, w, h, 1500, ["rgba(120,115,125,0.5)", "rgba(220,215,225,0.4)"], 2);
  });
  TEX.cobble = canvasTex(256, 256, (g, w, h) => {
    g.fillStyle = "#4f4852"; g.fillRect(0, 0, w, h);
    const rows = 10, rh = h / rows;
    for (let r = 0; r < rows; r++) {
      let x = (r % 2) * -12;
      while (x < w) { const sw = range(18, 30); g.fillStyle = pick(["#8f838a", "#9b8e92", "#857980", "#a39497", "#7d7279"]);
        g.beginPath(); g.ellipse(x + sw / 2, r * rh + rh / 2, sw / 2 - 1.5, rh / 2 - 1.5, 0, 0, Math.PI * 2); g.fill(); x += sw; }
    }
    speckle(g, w, h, 800, ["rgba(255,255,255,0.12)"], 2);
  });
  TEX.plaza = canvasTex(256, 256, (g, w, h) => {
    for (let i = 0; i < 2; i++) for (let j = 0; j < 2; j++) { g.fillStyle = (i + j) % 2 ? "#cdb9a5" : "#c2ac97"; g.fillRect(i * 128, j * 128, 128, 128); }
    g.strokeStyle = "#a8927f"; g.lineWidth = 3; for (let i = 0; i <= 4; i++) { g.beginPath(); g.moveTo(i * 64, 0); g.lineTo(i * 64, h); g.moveTo(0, i * 64); g.lineTo(w, i * 64); g.stroke(); }
    speckle(g, w, h, 2500, ["rgba(120,95,80,0.25)", "rgba(255,240,225,0.3)"], 2);
  });
  TEX.grass = canvasTex(256, 256, (g, w, h) => {
    g.fillStyle = "#6f8f55"; g.fillRect(0, 0, w, h);
    for (let i = 0; i < 7000; i++) { g.strokeStyle = pick(["#7fa35f", "#5f7f48", "#8db56a", "#66874c"]); const x = rand() * w, y = rand() * h; g.beginPath(); g.moveTo(x, y); g.lineTo(x + range(-2, 2), y - range(3, 7)); g.stroke(); }
  });
  TEX.gravel = canvasTex(256, 256, (g, w, h) => { g.fillStyle = "#b5a993"; g.fillRect(0, 0, w, h); speckle(g, w, h, 9000, ["#9c907a", "#cbbfa8", "#8a7f6c", "#d8cdb8"], 3); });
  TEX.metal = canvasTex(128, 128, (g, w, h) => {
    g.fillStyle = "#5c5e67"; g.fillRect(0, 0, w, h);
    g.fillStyle = "#777a84";
    for (let y = 0; y < h; y += 16) for (let x = (y / 16) % 2 * 8; x < w; x += 16) { g.save(); g.translate(x, y); g.rotate(Math.PI / 4); g.fillRect(-6, -1.5, 12, 3); g.restore(); }
    speckle(g, w, h, 300, ["rgba(30,30,35,0.4)"], 2);
  });
  TEX.wood = canvasTex(256, 256, (g, w, h) => {
    const rows = 8, rh = h / rows;
    for (let r = 0; r < rows; r++) { g.fillStyle = pick(["#8a6448", "#7d5a40", "#946c4e", "#86603f"]); g.fillRect(0, r * rh, w, rh - 3);
      g.strokeStyle = "rgba(60,40,25,0.35)"; for (let k = 0; k < 4; k++) { g.beginPath(); const y = r * rh + range(4, rh - 6); g.moveTo(0, y); g.bezierCurveTo(w / 3, y + range(-3, 3), (2 * w) / 3, y + range(-3, 3), w, y); g.stroke(); } }
    g.fillStyle = "#3d2b1f"; for (let r = 0; r < rows; r++) g.fillRect(0, r * rh + rh - 3, w, 3);
  });
  // Fenêtres par blocs de 4 × 4 : la nuit, seules certaines s'allument (texture d'émission jumelle)
  const lit = [];
  for (let k = 0; k < 16; k++) lit.push(rand() < 0.42 ? pick(["#ffd28a", "#ffe2b0", "#ffc070", "#d8e6ff"]) : null);
  TEX.window = canvasTex(256, 256, (g) => {
    g.fillStyle = "#ffffff"; g.fillRect(0, 0, 256, 256);
    for (let r = 0; r < 4; r++) for (let q = 0; q < 4; q++) {
      const x = q * 64, y = r * 64;
      g.fillStyle = "#cfcfd3"; g.fillRect(x, y + 61, 64, 3);
      const grad = g.createLinearGradient(0, y + 14, 0, y + 44); grad.addColorStop(0, "#7d889c"); grad.addColorStop(1, "#4b5468");
      g.fillStyle = grad; g.fillRect(x + 16, y + 12, 32, 30);
      g.fillStyle = "rgba(255,255,255,0.18)"; g.fillRect(x + 16, y + 12, 32, 3);
      g.fillStyle = "#b9b9bf"; g.fillRect(x + 14, y + 42, 36, 3);
    }
  });
  TEX.windowLit = canvasTex(256, 256, (g) => {
    g.fillStyle = "#000000"; g.fillRect(0, 0, 256, 256);
    for (let r = 0; r < 4; r++) for (let q = 0; q < 4; q++) {
      const col = lit[r * 4 + q]; if (!col) continue;
      const x = q * 64, y = r * 64, grad = g.createLinearGradient(0, y + 12, 0, y + 42);
      grad.addColorStop(0, col); grad.addColorStop(1, "#7a4a20");
      g.fillStyle = grad; g.fillRect(x + 16, y + 12, 32, 30);
      if (rand() < 0.5) { g.fillStyle = "rgba(0,0,0,0.55)"; g.fillRect(x + 16 + rand() * 20, y + 12, 6 + rand() * 8, 30); } // rideau
    }
  });
  TEX.houseWindow = canvasTex(64, 64, (g) => {
    g.fillStyle = "#f2efe8"; g.fillRect(0, 0, 64, 64);
    g.fillStyle = "#56607a"; g.fillRect(6, 6, 52, 52);
    g.fillStyle = "#f2efe8"; g.fillRect(30, 6, 4, 52); g.fillRect(6, 30, 52, 4);
    g.fillStyle = "rgba(255,255,255,0.2)"; g.fillRect(8, 8, 20, 6);
  }, false);
  TEX.manhole = canvasTex(128, 128, (g) => {
    g.fillStyle = "#3a3a42"; g.beginPath(); g.arc(64, 64, 62, 0, Math.PI * 2); g.fill();
    g.strokeStyle = "#5b5b66"; g.lineWidth = 3; for (const r of [56, 40, 24]) { g.beginPath(); g.arc(64, 64, r, 0, Math.PI * 2); g.stroke(); }
    for (let a = 0; a < 8; a++) { g.beginPath(); g.moveTo(64, 64); g.lineTo(64 + Math.cos(a * Math.PI / 4) * 56, 64 + Math.sin(a * Math.PI / 4) * 56); g.stroke(); }
  }, false);
  TEX.tag = canvasTex(512, 256, (g, w, h) => {
    g.clearRect(0, 0, w, h);
    speckle(g, w, h, 500, ["rgba(255,120,170,0.5)", "rgba(110,220,255,0.5)", "rgba(255,230,120,0.5)"], 3);
    g.font = `900 150px ${JP}`; g.textAlign = "center"; g.textBaseline = "middle"; g.lineJoin = "round";
    g.lineWidth = 26; g.strokeStyle = "#16121d"; g.strokeText("TWST", w / 2, h / 2 + 6);
    const grad = g.createLinearGradient(0, 40, 0, 210); grad.addColorStop(0, "#ff7ab0"); grad.addColorStop(0.55, "#ffc36b"); grad.addColorStop(1, "#6fe0ff");
    g.fillStyle = grad; g.fillText("TWST", w / 2, h / 2 + 6);
    g.lineWidth = 6; g.strokeStyle = "#ffffff"; g.globalAlpha = 0.5; g.strokeText("TWST", w / 2 - 4, h / 2); g.globalAlpha = 1;
    g.fillStyle = "#ff7ab0"; for (let i = 0; i < 9; i++) { const x = range(80, 430), y = range(170, 200), l = range(15, 50); g.fillRect(x, y, 4, l); g.beginPath(); g.arc(x + 2, y + l, 4, 0, Math.PI * 2); g.fill(); }
    g.font = `700 34px ${JP}`; g.fillStyle = "#6fe0ff"; g.fillText("↑ ↑ ↑", w / 2, 36);
  }, false);
  TEX.fence = canvasTex(64, 64, (g, w, h) => {
    g.clearRect(0, 0, w, h); g.strokeStyle = "#c9ccd3"; g.lineWidth = 2;
    g.beginPath(); g.moveTo(0, 0); g.lineTo(w, h); g.moveTo(w, 0); g.lineTo(0, h); g.moveTo(-w / 2, h / 2); g.lineTo(w / 2, h * 1.5); g.stroke();
  });
  TEX.vending = canvasTex(128, 256, (g, w, h) => {
    g.fillStyle = "#e9eef5"; g.fillRect(0, 0, w, h);
    g.fillStyle = "#2f6fc0"; g.fillRect(0, 0, w, 14);
    g.fillStyle = "#fbfdff"; g.fillRect(8, 20, w - 16, 120);
    for (let r = 0; r < 3; r++) for (let c = 0; c < 5; c++) { const x = 14 + c * 21, y = 28 + r * 38; g.fillStyle = pick(["#e9473f", "#2f9e5b", "#f2b631", "#3d7fd9", "#8a4fc8", "#f07a2a"]); g.fillRect(x, y + 6, 14, 24); g.fillStyle = "#d8d8d8"; g.fillRect(x + 4, y, 6, 6); g.fillStyle = "#3a3a3a"; g.fillRect(x + 2, y + 32, 10, 3); }
    g.fillStyle = "#33363d"; g.fillRect(16, 150, 60, 30); g.fillStyle = "#8ef0a0"; g.fillRect(20, 156, 30, 8);
    g.fillStyle = "#2a2c31"; g.fillRect(14, 200, w - 28, 36);
  }, false);
}

function signTex(text, o) {
  const w = o.w || 512, h = o.h || 128;
  return canvasTex(w, h, (g) => {
    g.fillStyle = o.bg; g.fillRect(0, 0, w, h);
    if (o.border) { g.strokeStyle = o.border; g.lineWidth = Math.max(6, w * 0.025); g.strokeRect(g.lineWidth / 2, g.lineWidth / 2, w - g.lineWidth, h - g.lineWidth); }
    g.fillStyle = o.fg; g.textAlign = "center"; g.textBaseline = "middle";
    if (o.vertical) {
      const chars = [...text], size = Math.min(w * 0.72, (h * 0.86) / chars.length);
      g.font = `900 ${size}px ${JP}`;
      chars.forEach((ch, i) => g.fillText(ch, w / 2, h * 0.07 + size * (i + 0.5) + (h * 0.86 - size * chars.length) / 2));
    } else {
      const lines = text.split("\n"), size = Math.min((h * 0.8) / lines.length, (w * 0.92) / Math.max(...lines.map((l) => l.length)) * (o.wide ? 1.7 : 1));
      g.font = `900 ${size}px ${JP}`;
      lines.forEach((l, i) => g.fillText(l, w / 2, h / 2 + (i - (lines.length - 1) / 2) * size * 1.08));
    }
  }, false);
}
function norenTex(text, color) {
  return canvasTex(256, 128, (g, w, h) => {
    g.clearRect(0, 0, w, h);
    const chars = [...text], n = Math.max(chars.length, 3), gap = 5, pw = (w - gap * (n - 1)) / n;
    g.fillStyle = shade(color, 0.7); g.fillRect(0, 0, w, 14);
    for (let i = 0; i < n; i++) {
      g.fillStyle = color; g.fillRect(i * (pw + gap), 10, pw, h - 10);
      if (chars[i]) { g.fillStyle = "#f6f1e7"; g.font = `900 ${pw * 0.72}px ${JP}`; g.textAlign = "center"; g.textBaseline = "middle"; g.fillText(chars[i], i * (pw + gap) + pw / 2, h * 0.58); }
    }
  }, false);
}
function menuTex(title, lines) {
  return canvasTex(128, 192, (g, w, h) => {
    g.fillStyle = "#6b4a32"; g.fillRect(0, 0, w, h); g.fillStyle = "#26302b"; g.fillRect(8, 8, w - 16, h - 16);
    g.fillStyle = "#f4f0e6"; g.textAlign = "center"; g.font = `700 17px ${JP}`; g.fillText(title, w / 2, 32);
    g.font = `400 12px ${JP}`; g.textAlign = "left";
    lines.forEach((l, i) => { g.fillStyle = i % 2 ? "#f5d47a" : "#f4f0e6"; g.fillText(l, 16, 60 + i * 22); });
    g.strokeStyle = "rgba(244,240,230,0.6)"; g.beginPath(); g.moveTo(18, 42); g.lineTo(w - 18, 42); g.stroke();
  }, false);
}
function stripeTex(a, b) { return canvasTex(64, 64, (g) => { for (let i = 0; i < 4; i++) { g.fillStyle = i % 2 ? b : a; g.fillRect(i * 16, 0, 16, 64); } }); }
function shopInteriorTex(kind) {
  return canvasTex(256, 128, (g, w, h) => {
    const light = kind === "konbini" || kind === "boutique" || kind === "dept" ? ["#fbfbf4", "#d9dccf"] : ["#f6dcae", "#b98a5c"];
    const grad = g.createLinearGradient(0, 0, 0, h); grad.addColorStop(0, light[0]); grad.addColorStop(1, light[1]);
    g.fillStyle = grad; g.fillRect(0, 0, w, h);
    const colors = ["#e9473f", "#2f9e5b", "#f2b631", "#3d7fd9", "#8a4fc8", "#f07a2a", "#e9e2d0"];
    if (kind === "konbini" || kind === "dept") {
      for (let r = 0; r < 3; r++) { g.fillStyle = "#b9bcc4"; g.fillRect(0, 40 + r * 28, w, 4); for (let x = 4; x < w; x += 9) { g.fillStyle = pick(colors); g.fillRect(x, 26 + r * 28, 7, 14); } }
    } else if (kind === "boutique") {
      g.fillStyle = "#3a3a3a"; g.fillRect(10, 26, w - 20, 3);
      for (let x = 18; x < w - 18; x += 18) { g.fillStyle = pick(colors); g.fillRect(x, 30, 14, range(40, 60)); }
    } else if (kind === "bakery") {
      for (let r = 0; r < 3; r++) { g.fillStyle = "#7b5536"; g.fillRect(0, 48 + r * 26, w, 5); for (let x = 10; x < w; x += 20) { g.fillStyle = pick(["#d39b5a", "#b97a3e", "#e2b67a"]); g.beginPath(); g.ellipse(x, 42 + r * 26, 8, 5, 0, 0, Math.PI * 2); g.fill(); } }
    } else {
      for (let x = 20; x < w; x += 46) { g.fillStyle = "#fff2c8"; g.beginPath(); g.arc(x, 18, 8, 0, Math.PI * 2); g.fill(); g.fillStyle = "#3b2a20"; g.fillRect(x - 1, 0, 2, 10); }
      g.fillStyle = kind === "sushi" ? "#d8b98c" : "#6b4630"; g.fillRect(0, 74, w, 16);
      g.fillStyle = "#3a2a22"; for (let x = 16; x < w; x += 30) { g.fillRect(x, 92, 10, 4); g.fillRect(x + 4, 96, 2, 26); }
      if (kind === "cafe") for (let x = 30; x < w; x += 70) { g.fillStyle = "#5d8a4e"; g.beginPath(); g.arc(x, 60, 12, 0, Math.PI * 2); g.fill(); }
    }
    g.fillStyle = "rgba(40,40,48,0.85)"; for (let x = 0; x <= w; x += 64) g.fillRect(x - 2, 0, 4, h); g.fillRect(0, 0, w, 4);
    g.fillStyle = "rgba(255,255,255,0.12)"; g.beginPath(); g.moveTo(30, 0); g.lineTo(80, 0); g.lineTo(20, h); g.lineTo(0, h); g.fill();
  }, false);
}

// ============================================================================
// Construction : regroupement par matériau (peu d'appels de dessin) + collisions
// ============================================================================
const batches = new Map();
const colliders = [];
const zones = [];
const UP = new THREE.Vector3(0, 1, 0);
const MATS = {};
function lambert(hex, extra = {}) {
  const key = "L" + hex + JSON.stringify(extra);
  if (!MATS[key]) MATS[key] = new THREE.MeshLambertMaterial(Object.assign({ color: hex }, extra));
  return MATS[key];
}
// Matériaux qui s'allument la nuit (fenêtres, enseignes, vitrines, lampes) :
// world.js fait varier leur émission selon l'heure.
const NIGHT_MATS = [];
function nightMat(m, day, night) { NIGHT_MATS.push({ m, day, night }); m.emissiveIntensity = day; return m; }
function texMat(tex, extra = {}) { return new THREE.MeshLambertMaterial(Object.assign({ map: tex }, extra)); }
function litMat(tex) { return nightMat(new THREE.MeshLambertMaterial({ map: tex, emissive: 0xffffff, emissiveMap: tex }), 0.45, 1.15); }

function addGeo(geo, mat, matrix, cast = true) {
  let g = geo.index ? geo.toNonIndexed() : geo;
  if (matrix) g.applyMatrix4(matrix);
  const key = mat.uuid + (cast ? "c" : "n");
  if (!batches.has(key)) batches.set(key, { mat, cast, parts: [] });
  batches.get(key).parts.push(g);
}
function sliceGeo(g, start, count) {
  const out = new THREE.BufferGeometry();
  for (const name of ["position", "normal", "uv"]) { const a = g.attributes[name]; out.setAttribute(name, new THREE.BufferAttribute(a.array.slice(start * a.itemSize, (start + count) * a.itemSize), a.itemSize)); }
  return out;
}
function addGrouped(geo, mats, matrix, cast = true) {
  const g = geo.toNonIndexed(); if (matrix) g.applyMatrix4(matrix);
  for (const grp of g.groups) addGeo(sliceGeo(g, grp.start, grp.count), mats[grp.materialIndex], null, cast);
}
function flushBatches() {
  for (const b of batches.values()) {
    let n = 0; for (const g of b.parts) n += g.attributes.position.count;
    const pos = new Float32Array(n * 3), nor = new Float32Array(n * 3), uv = new Float32Array(n * 2);
    let o = 0;
    for (const g of b.parts) { const c = g.attributes.position.count; pos.set(g.attributes.position.array, o * 3); nor.set(g.attributes.normal.array, o * 3); uv.set(g.attributes.uv.array, o * 2); o += c; }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute("position", new THREE.BufferAttribute(pos, 3)); geo.setAttribute("normal", new THREE.BufferAttribute(nor, 3)); geo.setAttribute("uv", new THREE.BufferAttribute(uv, 2));
    geo.computeBoundingSphere();
    const mesh = new THREE.Mesh(geo, b.mat); mesh.castShadow = b.cast; mesh.receiveShadow = true; mesh.matrixAutoUpdate = false;
    scene.add(mesh);
  }
  batches.clear();
}
function collider(x0, y0, z0, x1, y1, z1, opts = {}) {
  colliders.push({ min: [Math.min(x0, x1), Math.min(y0, y1), Math.min(z0, z1)], max: [Math.max(x0, x1), Math.max(y0, y1), Math.max(z0, z1)], chain: !!opts.chain, cam: Math.abs(y1 - y0) > 3 && !opts.noCam });
}
// Boîte alignée sur les axes du monde (centre, taille)
function boxW(cx, cy, cz, sx, sy, sz, mat, cast = true) { addGeo(new THREE.BoxGeometry(sx, sy, sz), mat, new THREE.Matrix4().makeTranslation(cx, cy, cz), cast); }

// Repère local d'une façade ou d'un objet : u le long, v vers le haut, w vers l'extérieur.
function frame(origin, normal) {
  const n = normal.clone().normalize(), t = UP.clone().cross(n).normalize();
  return {
    o: origin.clone(), n, t,
    p(u, v, w) { return this.o.clone().addScaledVector(this.t, u).addScaledVector(UP, v).addScaledVector(this.n, w); },
    m(u, v, w, tilt = 0, spin = 0) {
      const m = new THREE.Matrix4().makeBasis(this.t, UP, this.n);
      if (spin) m.multiply(new THREE.Matrix4().makeRotationY(spin));
      if (tilt) m.multiply(new THREE.Matrix4().makeRotationX(tilt));
      return m.setPosition(this.p(u, v, w));
    },
  };
}
const dirVec = (a) => new THREE.Vector3(Math.sin(a), 0, Math.cos(a));
function fbox(F, u, v, w, su, sv, sw, mat, cast = true, tilt = 0) { addGeo(new THREE.BoxGeometry(su, sv, sw), mat, F.m(u, v, w, tilt), cast); }
function fplane(F, u, v, w, su, sv, mat) { addGeo(new THREE.PlaneGeometry(su, sv), mat, F.m(u, v, w), false); }
function fcyl(F, u, v, w, rTop, rBot, height, mat, cast = true, seg = 12) { addGeo(new THREE.CylinderGeometry(rTop, rBot, height, seg, 1), mat, F.m(u, v + height / 2, w), cast); }
function fcollider(F, u, v, w, su, sv, sw, opts) {
  const pts = [];
  for (const a of [-0.5, 0.5]) for (const c of [-0.5, 0.5]) pts.push(F.p(u + a * su, 0, w + c * sw));
  collider(Math.min(...pts.map((p) => p.x)), F.o.y + v - sv / 2, Math.min(...pts.map((p) => p.z)), Math.max(...pts.map((p) => p.x)), F.o.y + v + sv / 2, Math.max(...pts.map((p) => p.z)), opts);
}
// Panneau à deux faces perpendiculaire à une façade (enseigne « drapeau »)
function bladeSign(F, u, v, w, width, height, tex) {
  const center = F.p(u, v, w), mat = nightMat(texMat(tex, { emissive: 0xffffff, emissiveMap: tex }), 0.25, 1.3);
  for (const s of [1, -1]) { const S = frame(center, F.t.clone().multiplyScalar(s)); fplane(S, 0, 0, 0.07, width, height, mat); }
  addGeo(new THREE.BoxGeometry(0.12, height + 0.1, width + 0.1), lambert(0x2a2830), F.m(u, v, w, 0, Math.PI / 2), true);
}

// Sol : rectangle texturé (+ zone pour le bruit des pas)
function ground(x0, x1, z0, z1, y, type, tex, tile) {
  const sx = x1 - x0, sz = z1 - z0, geo = new THREE.PlaneGeometry(sx, sz, Math.ceil(sx / 8), Math.ceil(sz / 8));
  geo.rotateX(-Math.PI / 2);
  const uv = geo.attributes.uv; for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) * sx / tile, uv.getY(i) * sz / tile);
  addGeo(geo, MATS["ground_" + type] || (MATS["ground_" + type] = texMat(tex)), new THREE.Matrix4().makeTranslation((x0 + x1) / 2, y, (z0 + z1) / 2), false);
  zones.push({ x0, x1, z0, z1, type });
}
function surfaceAt(x, z) { for (let i = zones.length - 1; i >= 0; i--) { const s = zones[i]; if (x >= s.x0 && x <= s.x1 && z >= s.z0 && z <= s.z1) return s.type; } return "asphalt"; }

