"use strict";
// ============================================================================
// Le bonhomme d'abrasion : chrome, tête séparée, corps arrondi. Il est
// articulé (taille, cou, épaules, coudes, hanches, genoux) et chaque
// articulation suit sa cible avec un ressort : les mouvements restent
// souples, avec un léger dépassement, comme un personnage d'animation.
// buildRig() sert aussi au double fantôme du Delay.
// ============================================================================
const MODEL_LAYER = 1;
function chromeMatcap() {
  return canvasTex(256, 256, (g) => {
    g.beginPath(); g.arc(128, 128, 128, 0, Math.PI * 2); g.clip();
    const base = g.createRadialGradient(118, 112, 0, 128, 128, 128);
    base.addColorStop(0, "#5a5c64"); base.addColorStop(0.62, "#2d2e34"); base.addColorStop(0.88, "#6f7480"); base.addColorStop(1, "#c9cdd8");
    g.fillStyle = base; g.fillRect(0, 0, 256, 256);
    const skyG = g.createLinearGradient(0, 20, 0, 128); skyG.addColorStop(0, "rgba(205,214,232,0.55)"); skyG.addColorStop(1, "rgba(205,214,232,0)"); g.fillStyle = skyG; g.fillRect(0, 0, 256, 128);
    const st = g.createRadialGradient(128, 230, 0, 128, 230, 90); st.addColorStop(0, "rgba(232,190,165,0.35)"); st.addColorStop(1, "rgba(232,190,165,0)"); g.fillStyle = st; g.fillRect(0, 128, 256, 128);
    const sh = g.createRadialGradient(92, 78, 0, 92, 78, 62); sh.addColorStop(0, "rgba(255,255,255,0.95)"); sh.addColorStop(0.25, "rgba(255,255,255,0.55)"); sh.addColorStop(1, "rgba(255,255,255,0)"); g.fillStyle = sh; g.fillRect(0, 0, 256, 256);
  }, false);
}
const MATCAP = chromeMatcap();
const chrome = new THREE.MeshMatcapMaterial({ matcap: MATCAP, color: 0xffffff });
function capsule(r, h) {
  const pts = [], straight = Math.max(0, h - 2 * r);
  for (let i = 0; i <= 10; i++) { const a = -Math.PI / 2 + (Math.PI / 2) * (i / 10); pts.push(new THREE.Vector2(Math.cos(a) * r, Math.sin(a) * r - straight / 2)); }
  for (let i = 0; i <= 10; i++) { const a = (Math.PI / 2) * (i / 10); pts.push(new THREE.Vector2(Math.cos(a) * r, Math.sin(a) * r + straight / 2)); }
  return new THREE.LatheGeometry(pts, 24);
}
const JOINTS = ["waist", "neck", "shoulderR", "shoulderL", "elbowR", "elbowL", "hipR", "hipL", "kneeR", "kneeL"];
// root (position, cap) > spin > flip (salto, pivot au centre) > pose (allongé) > body (écrasement / étirement)
function buildRig(material, layer) {
  const rig = { root: new THREE.Group(), spin: new THREE.Group(), flip: new THREE.Group(), flipInner: new THREE.Group(), pose: new THREE.Group(), body: new THREE.Group(),
    meshes: [], J: {}, action: null, sq: 1, sqV: 0 };
  rig.root.add(rig.spin); rig.spin.add(rig.flip); rig.flip.position.y = 0.95; rig.flip.add(rig.flipInner); rig.flipInner.position.y = -0.95;
  rig.flipInner.add(rig.pose); rig.pose.add(rig.body); scene.add(rig.root);
  const part = (parent, geo, x, y, z, sx = 1, sy = 1, sz = 1) => { const m = new THREE.Mesh(geo, material); m.position.set(x, y, z); m.scale.set(sx, sy, sz); m.layers.set(layer); parent.add(m); rig.meshes.push(m); return m; };
  const joint = (name, parent, x, y, z, k = 280, c = 24) => { const g = new THREE.Group(); g.position.set(x, y, z); parent.add(g); rig.J[name] = { g, k, c, v: new THREE.Vector3() }; return g; };
  rig.pelvis = new THREE.Group(); rig.pelvis.position.y = 0.55; rig.body.add(rig.pelvis);
  part(rig.pelvis, capsule(0.27, 0.5), 0, 0, 0);
  const waistJ = joint("waist", rig.pelvis, 0, 0, 0, 220, 19);
  part(waistJ, capsule(0.33, 0.84), 0, 0.33, 0);
  const neckJ = joint("neck", waistJ, 0, 0.78, 0, 110, 11);
  rig.head = part(neckJ, new THREE.SphereGeometry(0.27, 28, 18), 0, 0.3, 0);
  for (const [side, s] of [["R", 1], ["L", -1]]) {
    const sh = joint("shoulder" + side, waistJ, 0.33 * s, 0.53, 0, 300, 23);
    part(sh, capsule(0.11, 0.34), 0.02 * s, -0.13, 0);
    const el = joint("elbow" + side, sh, 0.02 * s, -0.28, 0, 320, 23);
    part(el, capsule(0.1, 0.3), 0, -0.12, 0);
    rig["hand" + side] = part(el, new THREE.SphereGeometry(0.125, 18, 12), 0, -0.29, 0);
    const hip = joint("hip" + side, rig.pelvis, 0.14 * s, -0.05, 0, 300, 25);
    part(hip, capsule(0.115, 0.3), 0, -0.11, 0);
    const kn = joint("knee" + side, hip, 0, -0.23, 0, 320, 25);
    part(kn, capsule(0.105, 0.28), 0, -0.1, 0);
    part(kn, new THREE.SphereGeometry(0.12, 16, 10), 0, -0.2, -0.045, 1, 0.6, 1.45);
  }
  return rig;
}
const model = buildRig(chrome, MODEL_LAYER);
// Double fantôme du Delay : même bonhomme, translucide et cyan, rendu normalement
const ghostMat = new THREE.MeshMatcapMaterial({ matcap: MATCAP, color: 0x7fe3ff, transparent: true, opacity: 0.55, depthWrite: false });
const ghostRig = buildRig(ghostMat, 0);
ghostRig.root.visible = false;

const blobTex = canvasTex(64, 64, (g) => { const grd = g.createRadialGradient(32, 32, 0, 32, 32, 32); grd.addColorStop(0, "rgba(0,0,0,0.5)"); grd.addColorStop(1, "rgba(0,0,0,0)"); g.fillStyle = grd; g.fillRect(0, 0, 64, 64); }, false);
const blob = new THREE.Mesh(new THREE.PlaneGeometry(1.1, 1.1), new THREE.MeshBasicMaterial({ map: blobTex, transparent: true, depthWrite: false }));
blob.rotation.x = -Math.PI / 2; scene.add(blob);

// --- Rendu pixélisé (même principe que pixel_impostor.gd) -------------------
// La résolution chute pendant l'esquive : le bonhomme se « bit-crushe ».
const PIX = { res: 88, cur: 88, size: 2.6, on: true, crush: 0 };
const pixTarget = new THREE.WebGLRenderTarget(PIX.res, PIX.res, { minFilter: THREE.NearestFilter, magFilter: THREE.NearestFilter });
const pixCam = new THREE.PerspectiveCamera(); pixCam.layers.set(MODEL_LAYER);
const pixQuad = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), new THREE.MeshBasicMaterial({ map: pixTarget.texture, alphaTest: 0.5, toneMapped: false }));
scene.add(pixQuad);
function setPixelMode(on) { PIX.on = on; for (const m of model.meshes) { m.layers.set(MODEL_LAYER); if (!on) m.layers.enable(0); } pixQuad.visible = on; }
setPixelMode(true);
// Images fantômes pixélisées laissées par l'esquive
const ghosts = [0, 1, 2, 3, 4, 5].map((i) => {
  const rt = new THREE.WebGLRenderTarget(PIX.res, PIX.res, { minFilter: THREE.NearestFilter, magFilter: THREE.NearestFilter });
  const quad = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), new THREE.MeshBasicMaterial({ map: rt.texture, transparent: true, alphaTest: 0.3, depthWrite: false, toneMapped: false, color: i % 2 ? 0xff8fd0 : 0x8fe6ff }));
  quad.visible = false; scene.add(quad);
  return { rt, quad, life: 0, maxLife: 0.35, pos: new THREE.Vector3() };
});
let ghostIndex = 0, ghostRequest = false;

// --- Effets : poussière, éclats de pixels -----------------------------------
const dustTex = canvasTex(64, 64, (g) => { const grd = g.createRadialGradient(32, 32, 0, 32, 32, 32); grd.addColorStop(0, "rgba(255,255,255,0.9)"); grd.addColorStop(1, "rgba(255,255,255,0)"); g.fillStyle = grd; g.fillRect(0, 0, 64, 64); }, false);
const dust = [];
for (let i = 0; i < 48; i++) { const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: dustTex, color: 0xd9cdbf, transparent: true, depthWrite: false, opacity: 0 })); s.visible = false; scene.add(s); dust.push({ s, life: 0, vel: new THREE.Vector3() }); }
let dustIndex = 0;
function puff(pos, count, spread, color = 0xd9cdbf) {
  for (let i = 0; i < count; i++) {
    const d = dust[dustIndex++ % dust.length]; d.life = 0.55; d.s.visible = true; d.s.material.color.setHex(color);
    d.s.position.copy(pos).add(new THREE.Vector3(range(-0.2, 0.2), range(0, 0.15), range(-0.2, 0.2)));
    d.vel.set(range(-spread, spread), range(0.3, 1.0), range(-spread, spread)); d.s.scale.setScalar(0.3);
  }
}
const shards = [];
for (let i = 0; i < 24; i++) { const m = new THREE.Mesh(new THREE.BoxGeometry(0.09, 0.09, 0.09), new THREE.MeshBasicMaterial({ color: 0xe8eefc, transparent: true })); m.visible = false; scene.add(m); shards.push({ m, life: 0, vel: new THREE.Vector3() }); }
function pixelBurst(pos) {
  for (const s of shards) { s.life = 0.45; s.m.visible = true; s.m.position.copy(pos); s.vel.set(range(-3, 3), range(-2.5, 0.5), range(-3, 3)); s.m.material.color.setHex(pick([0xe8eefc, 0x9fb4ff, 0xff9fc8, 0xffffff])); }
}

// ============================================================================
// Enceinte d'entraînement (sur la place du parc). Elle a sa propre petite
// physique : elle recule sous les coups, s'envole sur le finisher, rebondit
// sur le sol et les murs, puis revient à sa place.
// ============================================================================
const DUMMY_HOME = new THREE.Vector3(-15, SW + 0.008, 16);
const dummy = { root: new THREE.Group(), visual: new THREE.Group(), pos: DUMMY_HOME.clone(), vel: new THREE.Vector3(), grounded: true,
  tilt: 0, tiltVel: 0, target: 0, sq: 1, sqV: 0, lastHit: -10, col: { min: [0, 0, 0], max: [0, 0, 0], chain: false, cam: false, dummy: true } };
dummy.root.position.copy(dummy.pos); dummy.root.rotation.y = Math.PI / 2; dummy.root.add(dummy.visual); scene.add(dummy.root);
const coneMat = new THREE.MeshStandardMaterial({ color: 0x3a3a42, roughness: 0.35, emissive: 0xff8c4d, emissiveIntensity: 0 });
function dbox(x, y, z, sx, sy, sz, mat) { const m = new THREE.Mesh(new THREE.BoxGeometry(sx, sy, sz), mat); m.position.set(x, y, z); m.castShadow = true; dummy.visual.add(m); }
dbox(0, 0.11, 0, 1.1, 0.22, 0.9, lambert(0x4d4d55)); dbox(0, 0.97, 0, 0.9, 1.5, 0.7, new THREE.MeshStandardMaterial({ color: 0x24212a, roughness: 0.6 })); dbox(0, 1.72, 0, 0.92, 0.05, 0.72, lambert(0xd96b40));
for (const [r, y] of [[0.3, 0.72], [0.13, 1.38]]) { const c = new THREE.Mesh(new THREE.CylinderGeometry(r, r * 0.8, 0.06, 24), coneMat); c.rotation.x = Math.PI / 2; c.position.set(0, y, 0.36); dummy.visual.add(c); }
colliders.push(dummy.col);
function updateDummyCollider() { const p = dummy.pos; dummy.col.min = [p.x - 0.45, p.y, p.z - 0.45]; dummy.col.max = [p.x + 0.45, p.y + 1.75, p.z + 0.45]; }
updateDummyCollider();

// --- Le secret : un vinyle doré sur le toit de la boulangerie ---------------
const vinyl = new THREE.Group();
{ const disc = new THREE.Mesh(new THREE.CylinderGeometry(0.55, 0.55, 0.05, 40), new THREE.MeshBasicMaterial({ color: 0xf3cf6d }));
  const label = new THREE.Mesh(new THREE.CylinderGeometry(0.18, 0.18, 0.06, 24), new THREE.MeshBasicMaterial({ color: 0xc8322a }));
  const halo = new THREE.Mesh(new THREE.RingGeometry(0.7, 0.9, 40), new THREE.MeshBasicMaterial({ color: 0xfff1b8, transparent: true, opacity: 0.5, side: THREE.DoubleSide }));
  disc.rotation.x = label.rotation.x = Math.PI / 2; vinyl.add(disc, label, halo); vinyl.position.copy(SECRET_POS); scene.add(vinyl); }

// ============================================================================
// Son (créé au premier clic, comme l'exigent les navigateurs)
// ============================================================================
let audio = null, noise = null, muted = false;
function initAudio() {
  if (audio) return;
  try { audio = new (window.AudioContext || window.webkitAudioContext)(); } catch (e) { return; }
  noise = audio.createBuffer(1, audio.sampleRate * 0.4, audio.sampleRate);
  const d = noise.getChannelData(0); for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
}
function blip(fn) { if (!audio || muted) return; try { fn(audio, audio.currentTime); } catch (e) { /* son facultatif */ } }
function noiseHit(freq, q, dur, vol, type = "bandpass") {
  blip((a, t) => { const s = a.createBufferSource(); s.buffer = noise; const f = a.createBiquadFilter(); f.type = type; f.frequency.value = freq; f.Q.value = q;
    const g = a.createGain(); g.gain.setValueAtTime(vol, t); g.gain.exponentialRampToValueAtTime(0.001, t + dur); s.connect(f).connect(g).connect(a.destination); s.start(t); s.stop(t + dur + 0.02); });
}
function tone(f0, f1, dur, vol, type = "square") {
  blip((a, t) => { const o = a.createOscillator(); o.type = type; o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(f1, t + dur);
    const g = a.createGain(); g.gain.setValueAtTime(vol, t); g.gain.exponentialRampToValueAtTime(0.001, t + dur); o.connect(g).connect(a.destination); o.start(t); o.stop(t + dur + 0.02); });
}
const STEP_SOUND = { asphalt: [900, 1.2, 0.05], tiles: [1700, 2, 0.045], cobble: [1300, 1.5, 0.05], plaza: [1500, 1.8, 0.045], grass: [500, 0.8, 0.07], gravel: [3200, 0.7, 0.08], metal: [2600, 6, 0.09], wood: [420, 3, 0.07] };
function stepSound(surface, loud) { const [f, q, d] = STEP_SOUND[surface] || STEP_SOUND.asphalt; noiseHit(f * range(0.9, 1.1), q, d, loud ? 0.22 : 0.13); }

