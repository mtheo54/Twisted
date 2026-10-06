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
function capsule(r, h, radial = 24, steps = 10) {
  const pts = [], straight = Math.max(0, h - 2 * r);
  for (let i = 0; i <= steps; i++) { const a = -Math.PI / 2 + (Math.PI / 2) * (i / steps); pts.push(new THREE.Vector2(Math.cos(a) * r, Math.sin(a) * r - straight / 2)); }
  for (let i = 0; i <= steps; i++) { const a = (Math.PI / 2) * (i / steps); pts.push(new THREE.Vector2(Math.cos(a) * r, Math.sin(a) * r + straight / 2)); }
  return new THREE.LatheGeometry(pts, radial);
}
const JOINTS = ["waist", "chest", "neck", "shoulderR", "shoulderL", "elbowR", "elbowL", "hipR", "hipL", "kneeR", "kneeL"];
// Deux silhouettes partagent le même squelette (mêmes articulations) :
//  - "abrasion" : le bonhomme d'abrasion, grosse tête ronde de chrome sur un
//    corps-tige très fin (comme sur l'image de référence). Il est semi-liquide :
//    ses membres s'étirent (canal « s » des articulations), il s'écrase, s'allonge.
//  - "human" : proportions d'adulte, pour les passants et le Câblé.
// Les mains ont un point d'accroche pour les armes.
// root (position, cap) > spin > flip (salto, pivot au centre) > pose (chute, allongé) > body (écrasement / étirement)
const RIG_GEO = new Map(); // géométries partagées des squelettes allégés
const RIG_SHAPES = {
  human: { pelvisY: 0.9, pelvis: [0.15, 0.34, 1.25, 0.85], waist: [0.15, 0.3, 0.12, 1.15, 0.82], chestY: 0.2, chest: [0.2, 0.44, 0.17, 1.25, 0.84], neckY: 0.42, head: [0.155, 0.17],
    shoulder: [0.25, 0.33, 0.085], upper: [0.07, 0.32, -0.14], elbowY: -0.29, fore: [0.062, 0.3, -0.13], handY: -0.29, hand: 0.08, hip: [0.11, -0.06], thigh: [0.09, 0.46, -0.2], kneeY: -0.42, shin: [0.075, 0.42, -0.18], foot: [0.09, -0.38, -0.05, 1.65] },
  abrasion: { pelvisY: 0.88, pelvis: [0.06, 0.2, 1, 1], waist: [0.062, 0.34, 0.15, 1, 1], chestY: 0.25, chest: [0.07, 0.46, 0.2, 1, 1], neckY: 0.38, head: [0.2, 0.23],
    shoulder: [0.09, 0.36, 0.04], upper: [0.036, 0.34, -0.15], elbowY: -0.31, fore: [0.033, 0.32, -0.14], handY: -0.3, hand: 0.042, hip: [0.052, -0.05], thigh: [0.048, 0.46, -0.2], kneeY: -0.42, shin: [0.043, 0.44, -0.19], foot: [0.05, -0.41, -0.07, 2.4] },
};
// lowPoly : version allégée (passants, monstres) — moins de facettes, même silhouette.
function buildRig(material, layer, lowPoly = false, shape = "human") {
  const S = RIG_SHAPES[shape];
  const cached = (key, make) => RIG_GEO.get(key) || RIG_GEO.set(key, make()).get(key);
  const cap = (r, h) => (lowPoly ? cached(`c${r},${h}`, () => capsule(r, h, 10, 4)) : capsule(r, h)), sph = (r, a = 16, b = 10) => (lowPoly ? cached(`s${r}`, () => new THREE.SphereGeometry(r, 10, 7)) : new THREE.SphereGeometry(r, a, b));
  const rig = { root: new THREE.Group(), spin: new THREE.Group(), flip: new THREE.Group(), flipInner: new THREE.Group(), pose: new THREE.Group(), body: new THREE.Group(),
    meshes: [], J: {}, stretch: {}, action: null, sq: 1, sqV: 0, smear: 0, material, layer, shape };
  rig.root.add(rig.spin); rig.spin.add(rig.flip); rig.flip.position.y = 0.95; rig.flip.add(rig.flipInner); rig.flipInner.position.y = -0.95;
  rig.flipInner.add(rig.pose); rig.pose.add(rig.body); scene.add(rig.root);
  const part = (parent, geo, x, y, z, sx = 1, sy = 1, sz = 1) => { const m = new THREE.Mesh(geo, material); m.position.set(x, y, z); m.scale.set(sx, sy, sz); m.layers.set(layer); parent.add(m); rig.meshes.push(m); return m; };
  const joint = (name, parent, x, y, z, k = 280, c = 24) => { const g = new THREE.Group(); g.position.set(x, y, z); parent.add(g); rig.J[name] = { g, k, c, v: new THREE.Vector3(), vs: 0 }; return g; };
  rig.part = part;
  rig.pelvis = new THREE.Group(); rig.pelvis.position.y = S.pelvisY; rig.body.add(rig.pelvis);
  part(rig.pelvis, cap(S.pelvis[0], S.pelvis[1]), 0, -0.02, 0, S.pelvis[2], 1, S.pelvis[3]);
  const waistJ = joint("waist", rig.pelvis, 0, 0.05, 0, 220, 19);
  part(waistJ, cap(S.waist[0], S.waist[1]), 0, S.waist[2], 0, S.waist[3], 1, S.waist[4]);
  const chestJ = joint("chest", waistJ, 0, S.chestY, 0, 200, 18);
  part(chestJ, cap(S.chest[0], S.chest[1]), 0, S.chest[2], 0, S.chest[3], 1, S.chest[4]);
  const neckJ = joint("neck", chestJ, 0, S.neckY, 0, 110, 11);
  rig.head = part(neckJ, sph(S.head[0], 28, 18), 0, S.head[1], 0);
  rig.headRest = rig.head.position.clone();
  for (const [side, s] of [["R", 1], ["L", -1]]) {
    const sh = joint("shoulder" + side, chestJ, S.shoulder[0] * s, S.shoulder[1], 0, 300, 23);
    part(sh, sph(S.shoulder[2], 14, 10), 0, 0, 0);
    const upper = part(sh, cap(S.upper[0], S.upper[1]), 0.0, S.upper[2], 0);
    const el = joint("elbow" + side, sh, 0, S.elbowY, 0, 320, 23);
    const fore = part(el, cap(S.fore[0], S.fore[1]), 0, S.fore[2], 0);
    const hand = new THREE.Group(); hand.position.set(0, S.handY, 0); el.add(hand);
    // étirement d'un membre : on allonge le segment et on éloigne l'articulation suivante (pas de déformation de l'arme)
    rig.stretch["shoulder" + side] = { mesh: upper, child: el, meshY: S.upper[2], childY: S.elbowY };
    rig.stretch["elbow" + side] = { mesh: fore, child: hand, meshY: S.fore[2], childY: S.handY };
    part(hand, sph(S.hand, 16, 10), 0, 0, 0, 1, 1.1, 0.9);
    rig["hand" + side] = hand;
    const hip = joint("hip" + side, rig.pelvis, S.hip[0] * s, S.hip[1], 0, 300, 25);
    const thigh = part(hip, cap(S.thigh[0], S.thigh[1]), 0, S.thigh[2], 0);
    const kn = joint("knee" + side, hip, 0, S.kneeY, 0, 320, 25);
    rig.stretch["hip" + side] = { mesh: thigh, child: kn, meshY: S.thigh[2], childY: S.kneeY };
    part(kn, cap(S.shin[0], S.shin[1]), 0, S.shin[2], 0);
    part(kn, sph(S.foot[0], 16, 10), 0, S.foot[1], S.foot[2], 1, 0.6, S.foot[3]);
  }
  return rig;
}
// Change l'arme tenue : les maillages vont dans les points d'accroche des mains.
function setRigWeapon(rig, weaponId) {
  for (const k of ["weaponR", "weaponL"]) if (rig[k]) { rig[k].parent.remove(rig[k]); rig.meshes = rig.meshes.filter((m) => !rig[k].userData.meshes.includes(m)); rig[k] = null; }
  const make = WEAPON_MODELS[weaponId];
  if (!make) return;
  for (const [side, build] of Object.entries(make)) {
    const g = new THREE.Group(), meshes = [];
    build((geo, mat, x, y, z, rx = 0, ry = 0, rz = 0, sx = 1, sy = 1, sz = 1) => {
      const m = new THREE.Mesh(geo, mat || rig.material); m.position.set(x, y, z); m.rotation.set(rx, ry, rz); m.scale.set(sx, sy, sz); m.layers.set(rig.layer); g.add(m); meshes.push(m); return m;
    }, g);
    g.userData.meshes = meshes; rig["hand" + side].add(g); rig["weapon" + side] = g; rig.meshes.push(...meshes);
  }
}
const model = buildRig(chrome, MODEL_LAYER, false, "abrasion");
// comme sur l'image : tête de chrome clair, corps-tige d'un métal plus sombre
const chromeBody = new THREE.MeshMatcapMaterial({ matcap: MATCAP, color: 0x9a9ea8 });
for (const m of model.meshes) if (m !== model.head) m.material = chromeBody;
// Double fantôme du Delay : même bonhomme, translucide et cyan, rendu normalement
const ghostMat = new THREE.MeshMatcapMaterial({ matcap: MATCAP, color: 0x7fe3ff, transparent: true, opacity: 0.55, depthWrite: false });
const ghostRig = buildRig(ghostMat, 0, false, "abrasion");
ghostRig.root.visible = false;

const blobTex = canvasTex(64, 64, (g) => { const grd = g.createRadialGradient(32, 32, 0, 32, 32, 32); grd.addColorStop(0, "rgba(0,0,0,0.5)"); grd.addColorStop(1, "rgba(0,0,0,0)"); g.fillStyle = grd; g.fillRect(0, 0, 64, 64); }, false);
const blob = new THREE.Mesh(new THREE.PlaneGeometry(1.1, 1.1), new THREE.MeshBasicMaterial({ map: blobTex, transparent: true, depthWrite: false }));
blob.rotation.x = -Math.PI / 2; scene.add(blob);

// --- Rendu pixélisé (même principe que pixel_impostor.gd) -------------------
// La résolution chute pendant l'esquive : le bonhomme se « bit-crushe ».
const PIX = { res: 136, cur: 136, size: 3.4, on: true, crush: 0 };
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

