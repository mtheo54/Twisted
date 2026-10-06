"use strict";
// ============================================================================
// Jour et nuit
// Une journée complète dure 7 minutes (environ 4 de jour, 3 de nuit).
// La touche N fait défiler le temps jusqu'à la nuit (ou jusqu'au matin).
// Tout ce qui change : ciel, étoiles, lune, soleil, brume, fenêtres,
// enseignes, lampadaires (vraie lumière près du joueur + halos), oiseaux.
// ============================================================================
const DN = { hour: 15.5, speed: 24 / 420, n: 0, target: null, lightDir: SUN_DIR.clone(), palette: null };
const MOON_DIR = new THREE.Vector3(-0.28, 0.16, -0.95).normalize();

// Palettes de référence, interpolées selon l'heure
const C = (h) => new THREE.Color(h);
// Dark fantasy : jour couvert et brumeux, crépuscule couleur sang, nuit d'encre sous une lune rouge
const PAL = {
  night:  { top: C(0x05040a), horizon: C(0x2a0c16), ground: C(0x0a0608), fog: C(0x120910), fogD: 0.0042, sun: C(0xff7a6a), sunI: 0.38, hs: C(0x2a1c3a), hg: C(0x140a0c), hi: 0.38, exp: 1.15, n: 1, disc: 0 },
  dawn:   { top: C(0x2a2838), horizon: C(0x8a4a44), ground: C(0x3a3036), fog: C(0x5a4448), fogD: 0.0034, sun: C(0xd88a6a), sunI: 0.7, hs: C(0x6a6478), hg: C(0x3a2a2a), hi: 0.5, exp: 1.1, n: 0.3, disc: 0.5 },
  day:    { top: C(0x3e4250), horizon: C(0x9a9088), ground: C(0x4a464c), fog: C(0x7a726e), fogD: 0.0028, sun: C(0xe8d8c8), sunI: 1.15, hs: C(0x8a8e9e), hg: C(0x4a3e3a), hi: 0.62, exp: 1.05, n: 0, disc: 0.35 },
  sunset: { top: C(0x2a1a2e), horizon: C(0xc0402a), ground: C(0x3a2226), fog: C(0x6a2a24), fogD: 0.0034, sun: C(0xff5a3a), sunI: 1.0, hs: C(0x7a4a5a), hg: C(0x3a1a1a), hi: 0.5, exp: 1.1, n: 0.35, disc: 1 },
};
const ANCHORS = [[0, PAL.night], [5, PAL.night], [6.3, PAL.dawn], [8.5, PAL.day], [16.5, PAL.day], [18.6, PAL.sunset], [20.2, PAL.night], [24, PAL.night]];
function paletteAt(hour) {
  let i = 0; while (i < ANCHORS.length - 2 && hour >= ANCHORS[i + 1][0]) i++;
  const [h0, a] = ANCHORS[i], [h1, b] = ANCHORS[i + 1], f = Math.min(1, Math.max(0, (hour - h0) / (h1 - h0))), out = {};
  for (const k of Object.keys(a)) out[k] = a[k] instanceof THREE.Color ? a[k].clone().lerp(b[k], f) : a[k] + (b[k] - a[k]) * f;
  return out;
}

// --- Lune : disque pâle et grand halo, derrière les montagnes au nord ----------
const moonTex = canvasTex(128, 128, (g) => {
  const grd = g.createRadialGradient(56, 54, 4, 64, 64, 62); grd.addColorStop(0, "#fffaf0"); grd.addColorStop(0.8, "#e8e4f6"); grd.addColorStop(1, "rgba(232,228,246,0)");
  g.fillStyle = grd; g.beginPath(); g.arc(64, 64, 62, 0, Math.PI * 2); g.fill();
  g.fillStyle = "rgba(170,165,200,0.35)"; for (const [x, y, r] of [[48, 50, 9], [78, 70, 12], [60, 84, 6], [84, 44, 5]]) { g.beginPath(); g.arc(x, y, r, 0, Math.PI * 2); g.fill(); }
}, false);
const haloTex = canvasTex(128, 128, (g) => { const grd = g.createRadialGradient(64, 64, 0, 64, 64, 64); grd.addColorStop(0, "rgba(255,255,255,0.9)"); grd.addColorStop(0.25, "rgba(255,240,230,0.35)"); grd.addColorStop(1, "rgba(255,255,255,0)"); g.fillStyle = grd; g.fillRect(0, 0, 128, 128); }, false);
// lune rouge
const moon = new THREE.Sprite(new THREE.SpriteMaterial({ map: moonTex, color: 0xff7a62, fog: false, transparent: true, depthWrite: false }));
const moonHalo = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, color: 0xff4a3a, fog: false, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending }));
moon.scale.setScalar(150); moonHalo.scale.setScalar(620); moon.renderOrder = -1; moonHalo.renderOrder = -1;
scene.add(moonHalo, moon);

// --- Lampadaires : quelques vraies lumières près du joueur, des halos partout ----
const lampLights = [], lampGlows = [];
function setupNight() {
  for (let i = 0; i < 8; i++) { const l = new THREE.PointLight(0xff8a3a, 0, 16, 2); l.layers.enableAll(); scene.add(l); lampLights.push(l); }
  for (const p of LAMP_POS) {
    const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, color: 0xff9a50, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, opacity: 0 }));
    s.position.copy(p); s.scale.setScalar(2.6); scene.add(s); lampGlows.push(s);
  }
}
let lampAssignTimer = 0;
function assignLampLights(center) {
  const sorted = LAMP_POS.map((p, i) => [p.distanceToSquared(center), i]).sort((a, b) => a[0] - b[0]);
  lampLights.forEach((l, k) => { const e = sorted[k]; if (e) l.position.copy(LAMP_POS[e[1]]).add(new THREE.Vector3(0, -0.3, 0)); });
}

function isNight() { return DN.n > 0.6; }
function clockText() { const h = Math.floor(DN.hour), m = Math.floor((DN.hour - h) * 60); return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")} ${isNight() ? "☾" : "☀"}`; }
function skipTime() { DN.target = isNight() || DN.hour < 6 ? 8.5 : 21.5; }

function updateDayNight(dt, center) {
  if (DN.target !== null) {
    DN.hour = (DN.hour + dt * 4) % 24; // 4 heures de jeu par seconde
    if (Math.abs(DN.hour - DN.target) < 0.08) DN.target = null;
  } else DN.hour = (DN.hour + dt * DN.speed) % 24;
  const P0 = paletteAt(DN.hour); DN.palette = P0; DN.n = P0.n;
  const u = sky.material.uniforms;
  u.top.value.copy(P0.top); u.horizon.value.copy(P0.horizon); u.ground.value.copy(P0.ground);
  u.night.value = Math.max(0, (P0.n - 0.3) / 0.7); u.sunDisc.value = P0.disc; u.time.value += dt;
  scene.fog.color.copy(P0.fog); scene.fog.density = P0.fogD;
  sun.color.copy(P0.sun); sun.intensity = P0.sunI;
  hemi.color.copy(P0.hs); hemi.groundColor.copy(P0.hg); hemi.intensity = P0.hi;
  renderer.toneMappingExposure = P0.exp;
  // lumière principale : le soleil le jour, la lune la nuit (pour les ombres)
  DN.lightDir.copy(SUN_DIR).lerp(MOON_DIR.clone().setY(0.55).normalize(), Math.min(1, P0.n * 1.4)).normalize();
  const ln = u.night.value;
  moon.material.opacity = ln; moonHalo.material.opacity = ln * 0.55;
  moon.position.copy(camera.position).addScaledVector(MOON_DIR, 2200); moonHalo.position.copy(moon.position);
  for (const nm of NIGHT_MATS) nm.m.emissiveIntensity = nm.day + (nm.night - nm.day) * P0.n;
  lampAssignTimer -= dt; if (lampAssignTimer <= 0) { lampAssignTimer = 0.5; assignLampLights(center); }
  const lampOn = Math.max(0, (P0.n - 0.25) / 0.75);
  // flammes : la lumière vacille
  const fl = (k) => 0.82 + 0.18 * Math.sin(u.time.value * 13 + k * 2.1) * Math.sin(u.time.value * 7.3 + k);
  lampLights.forEach((l, k) => (l.intensity = lampOn * 1.9 * fl(k)));
  lampGlows.forEach((g, k) => (g.material.opacity = lampOn * 0.85 * fl(k)));
  updateEmbers(dt, center);
  if (TOWER_BEACON) TOWER_BEACON.material.color.setHex(ln > 0.5 && Math.floor(u.time.value * 1.2) % 2 ? 0xff2a1a : 0x6a1010);
  for (const b of birds) b.visible = P0.n < 0.5;
  const ck = document.getElementById("clock"); if (ck) ck.textContent = clockText();
}

// --- Braises qui montent et cendres qui tombent, autour du joueur -----------------------------
const EMBERS = { n: 260, pts: null, vel: [] };
{
  const pos = new Float32Array(EMBERS.n * 3), col = new Float32Array(EMBERS.n * 3);
  for (let i = 0; i < EMBERS.n; i++) {
    pos.set([range(-25, 25), range(0, 18), range(-25, 25)], i * 3);
    const ember = i % 3 === 0; EMBERS.vel.push(ember ? [range(-0.3, 0.3), range(0.6, 1.6), range(-0.3, 0.3)] : [range(-0.4, 0.4), range(-0.9, -0.4), range(-0.4, 0.4)]);
    const c = ember ? new THREE.Color(0xff7a2a) : new THREE.Color(0x8a8288); col.set([c.r, c.g, c.b], i * 3);
  }
  const g = new THREE.BufferGeometry(); g.setAttribute("position", new THREE.BufferAttribute(pos, 3)); g.setAttribute("color", new THREE.BufferAttribute(col, 3));
  EMBERS.pts = new THREE.Points(g, new THREE.PointsMaterial({ size: 0.09, vertexColors: true, transparent: true, opacity: 0.85, depthWrite: false, blending: THREE.AdditiveBlending }));
  EMBERS.pts.frustumCulled = false; scene.add(EMBERS.pts);
}
function updateEmbers(dt, center) {
  const a = EMBERS.pts.geometry.attributes.position, t = sky.material.uniforms.time.value;
  for (let i = 0; i < EMBERS.n; i++) {
    const v = EMBERS.vel[i]; let x = a.getX(i) + (v[0] + Math.sin(t * 0.7 + i) * 0.3) * dt, y = a.getY(i) + v[1] * dt, z = a.getZ(i) + v[2] * dt;
    // on recycle autour du joueur
    if (y > center.y + 18) y = center.y; if (y < center.y) y = center.y + 18;
    if (x - center.x > 25) x -= 50; if (x - center.x < -25) x += 50; if (z - center.z > 25) z -= 50; if (z - center.z < -25) z += 50;
    a.setXYZ(i, x, y, z);
  }
  a.needsUpdate = true;
  EMBERS.pts.material.opacity = 0.45 + 0.45 * DN.n;
}
