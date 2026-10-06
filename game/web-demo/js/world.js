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
const PAL = {
  night:  { top: C(0x0e0b1e), horizon: C(0x4e2858), ground: C(0x120e1a), fog: C(0x1b1429), fogD: 0.0017, sun: C(0x8ea4ff), sunI: 0.3, hs: C(0x2c2c5c), hg: C(0x120e18), hi: 0.42, exp: 1.2, n: 1, disc: 0 },
  dawn:   { top: C(0x5a6a9a), horizon: C(0xf0b8a8), ground: C(0x6d6878), fog: C(0xc8b4b8), fogD: 0.0013, sun: C(0xffc6a0), sunI: 0.9, hs: C(0xaeb6d6), hg: C(0x6e5a5e), hi: 0.6, exp: 1.12, n: 0.3, disc: 0.6 },
  day:    { top: C(0x7d92b2), horizon: C(0xf2d3bd), ground: C(0x8d8794), fog: C(0xe6cdbf), fogD: 0.0011, sun: C(0xffd6b5), sunI: 1.7, hs: C(0xc9d2e6), hg: C(0x8a6f6a), hi: 0.75, exp: 1.08, n: 0, disc: 1 },
  sunset: { top: C(0x5d5f96), horizon: C(0xff9e7a), ground: C(0x6a5464), fog: C(0xd99a8a), fogD: 0.0013, sun: C(0xff9a66), sunI: 1.2, hs: C(0xb0a0c8), hg: C(0x6a4a50), hi: 0.62, exp: 1.1, n: 0.35, disc: 1 },
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
const moon = new THREE.Sprite(new THREE.SpriteMaterial({ map: moonTex, fog: false, transparent: true, depthWrite: false }));
const moonHalo = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, color: 0xe7d8ff, fog: false, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending }));
moon.scale.setScalar(150); moonHalo.scale.setScalar(620); moon.renderOrder = -1; moonHalo.renderOrder = -1;
scene.add(moonHalo, moon);

// --- Lampadaires : quelques vraies lumières près du joueur, des halos partout ----
const lampLights = [], lampGlows = [];
function setupNight() {
  for (let i = 0; i < 8; i++) { const l = new THREE.PointLight(0xffc98a, 0, 16, 2); l.layers.enableAll(); scene.add(l); lampLights.push(l); }
  for (const p of LAMP_POS) {
    const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, color: 0xffd9a0, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, opacity: 0 }));
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
  for (const l of lampLights) l.intensity = lampOn * 1.7;
  for (const g of lampGlows) g.material.opacity = lampOn * 0.85;
  if (TOWER_BEACON) TOWER_BEACON.material.color.setHex(ln > 0.5 && Math.floor(u.time.value * 1.2) % 2 ? 0xff4a3a : 0xfff4ec);
  for (const b of birds) b.visible = P0.n < 0.5;
  const ck = document.getElementById("clock"); if (ck) ck.textContent = clockText();
}
