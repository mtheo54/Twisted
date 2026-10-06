"use strict";
// ============================================================================
// Effets physiques et violents (façon Ultrakill), uniquement de l'affichage :
//  - sang (particules qui retombent et tachent le sol), soin quand on frappe de près
//  - monstres qui explosent en morceaux (chaque pièce rebondit et saigne)
//  - impacts au sol : fissures, débris qui rebondissent, onde de choc, poussière
//  - étincelles de glissade, gouttes de chrome du bonhomme liquide
//  - jauge de style (D → TWISTED)
// Aucun mouvement de caméra : la puissance passe par le sol et les objets.
// ============================================================================
const _fm = new THREE.Matrix4(), _fq = new THREE.Quaternion(), _fs = new THREE.Vector3(), _fe = new THREE.Euler();
// Particules instanciées : un seul objet à dessiner pour des centaines de morceaux
class FxParticles {
  constructor(max, geo, mat, opts = {}) {
    this.max = max; this.items = []; this.o = opts;
    this.mesh = new THREE.InstancedMesh(geo, mat, max); this.mesh.count = 0; this.mesh.frustumCulled = false;
    this.mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    if (opts.colors) { for (let i = 0; i < max; i++) this.mesh.setColorAt(i, new THREE.Color(1, 1, 1)); }
    scene.add(this.mesh);
  }
  spawn(pos, vel, life, size, color) {
    if (this.items.length >= this.max) this.items.shift();
    const p = { pos: pos.clone(), vel: vel.clone(), life, max: life, size, color: color ?? null, rot: new THREE.Vector3(Math.random() * 6, Math.random() * 6, 0), spin: new THREE.Vector3(range(-12, 12), range(-12, 12), 0), ground: groundAt(pos), landed: false };
    this.items.push(p); return p;
  }
  update(dt) {
    const g = this.o.gravity ?? 22, list = this.items;
    for (let i = list.length - 1; i >= 0; i--) {
      const p = list[i]; p.life -= dt;
      if (p.life <= 0) { list.splice(i, 1); continue; }
      if (!p.landed || this.o.bounce) {
        p.vel.y -= g * dt; p.pos.addScaledVector(p.vel, dt); p.rot.addScaledVector(p.spin, dt);
        if (this.o.drag) p.vel.multiplyScalar(Math.exp(-this.o.drag * dt));
        if (p.pos.y < p.ground + p.size * 0.5 && g > 0) {
          p.pos.y = p.ground + p.size * 0.5;
          if (this.o.bounce && p.vel.y < -2) { p.vel.y *= -this.o.bounce; p.vel.x *= 0.6; p.vel.z *= 0.6; p.spin.multiplyScalar(0.6); }
          else { p.landed = true; p.vel.set(0, 0, 0); if (this.o.onLand) this.o.onLand(p); if (this.o.dieOnLand) { list.splice(i, 1); continue; } }
        }
      }
    }
    const n = Math.min(list.length, this.max);
    for (let i = 0; i < n; i++) {
      const p = list[i], k = this.o.shrink ? Math.min(1, p.life / (p.max * 0.4)) : 1, s = p.size * k;
      _fq.setFromEuler(_fe.set(p.rot.x, p.rot.y, p.rot.z));
      _fm.compose(p.pos, _fq, this.o.flat && p.landed ? _fs.set(s * 1.6, s * 0.15, s * 1.6) : this.o.streak ? _fs.set(s * 0.3, s * 0.3, s * 2.5) : _fs.set(s, s, s));
      if (this.o.streak) { _fm.lookAt(p.pos, p.pos.clone().add(p.vel), UP); _fm.scale(_fs); _fm.setPosition(p.pos); }
      this.mesh.setMatrixAt(i, _fm);
      if (this.o.colors && p.color !== null) this.mesh.setColorAt(i, _fc.setHex(p.color));
    }
    this.mesh.count = n; this.mesh.instanceMatrix.needsUpdate = true;
    if (this.o.colors && this.mesh.instanceColor) this.mesh.instanceColor.needsUpdate = true;
  }
}
const _fc = new THREE.Color();
// sol sous un point (trottoirs, toits…), calculé une fois par particule
function groundAt(p) { return typeof dummyGround === "function" ? dummyGround(p.x, p.z, p.y + 0.2) : 0; }

// --- Taches au sol : sang, fissures, brûlures (réserve circulaire) ---------------------------------
function splatTex(seed) {
  return canvasTex(128, 128, (g) => {
    let s = seed; const r = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
    g.fillStyle = "rgba(110,6,8,0.92)";
    g.beginPath(); g.arc(64, 64, 22 + r() * 10, 0, Math.PI * 2); g.fill();
    for (let i = 0; i < 14; i++) { const a = r() * Math.PI * 2, d = 20 + r() * 38, rr = 3 + r() * 9; g.beginPath(); g.arc(64 + Math.cos(a) * d, 64 + Math.sin(a) * d, rr, 0, Math.PI * 2); g.fill(); }
    g.fillStyle = "rgba(40,0,2,0.5)"; g.beginPath(); g.arc(60, 60, 14, 0, Math.PI * 2); g.fill();
  }, false);
}
const SPLAT_TEX = [11, 47, 91].map(splatTex);
const CRACK_TEX = canvasTex(256, 256, (g) => {
  g.strokeStyle = "rgba(12,10,12,0.95)"; g.lineCap = "round";
  for (let i = 0; i < 11; i++) {
    let x = 128, y = 128, a = (i / 11) * Math.PI * 2 + Math.random() * 0.3; g.lineWidth = 5;
    g.beginPath(); g.moveTo(x, y);
    for (let k = 0; k < 7; k++) { a += (Math.random() - 0.5) * 0.7; x += Math.cos(a) * 17; y += Math.sin(a) * 17; g.lineTo(x, y); g.lineWidth = Math.max(1, 5 - k * 0.7); }
    g.stroke();
  }
  const grd = g.createRadialGradient(128, 128, 0, 128, 128, 46); grd.addColorStop(0, "rgba(10,8,10,0.85)"); grd.addColorStop(1, "rgba(10,8,10,0)"); g.fillStyle = grd; g.fillRect(0, 0, 256, 256);
}, false);
const decals = [];
function makeDecalPool(n, tex, life) {
  const pool = [];
  for (let i = 0; i < n; i++) {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), new THREE.MeshLambertMaterial({ map: Array.isArray(tex) ? tex[i % tex.length] : tex, transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 }));
    m.rotation.x = -Math.PI / 2; m.visible = false; m.renderOrder = 1; scene.add(m); pool.push({ m, t: 0, life });
  }
  return { pool, i: 0 };
}
const BLOOD_DECALS = makeDecalPool(90, SPLAT_TEX, 45), CRACK_DECALS = makeDecalPool(24, CRACK_TEX, 30);
function decalAt(P0, pos, size, y) {
  const d = P0.pool[P0.i++ % P0.pool.length];
  d.m.position.set(pos.x, (y ?? groundAt(pos)) + 0.012 + Math.random() * 0.004, pos.z); d.m.scale.set(size, size, 1); d.m.rotation.z = Math.random() * Math.PI * 2;
  d.m.visible = true; d.m.material.opacity = 1; d.t = 0;
}
function updateDecals(dt) {
  for (const P0 of [BLOOD_DECALS, CRACK_DECALS]) for (const d of P0.pool) {
    if (!d.m.visible) continue; d.t += dt;
    if (d.t > d.life - 5) d.m.material.opacity = Math.max(0, (d.life - d.t) / 5);
    if (d.t > d.life) d.m.visible = false;
  }
}

// --- Les systèmes de particules -----------------------------------------------------------------
const BLOOD = new FxParticles(700, new THREE.BoxGeometry(1, 1, 1), new THREE.MeshBasicMaterial({ color: 0xffffff }), { colors: true, gravity: 20, drag: 0.4, dieOnLand: true,
  onLand: (p) => { if (Math.random() < 0.18) decalAt(BLOOD_DECALS, p.pos, range(0.25, 0.8), p.ground); } });
const DEBRIS = new FxParticles(220, new THREE.BoxGeometry(1, 1, 1), new THREE.MeshLambertMaterial({ color: 0xffffff }), { colors: true, gravity: 24, bounce: 0.35, shrink: true });
const SPARKS = new FxParticles(240, new THREE.BoxGeometry(1, 1, 1), new THREE.MeshBasicMaterial({ color: 0xffffff, toneMapped: false }), { colors: true, gravity: 9, streak: true, shrink: true });
const DRIPS = new FxParticles(140, new THREE.SphereGeometry(1, 8, 6), new THREE.MeshMatcapMaterial({ matcap: MATCAP, color: 0xd8dce6 }), { gravity: 14, flat: true, shrink: true });
const BLOOD_COLORS = [0xd01a1a, 0xa01014, 0x7a080c, 0xe8281e, 0x5a0406];
// gerbe de sang dans la direction du coup
function bloodSpray(pos, dir, amount, spread = 1) {
  const n = Math.max(6, Math.min(70, Math.round(amount * 1.6)));
  for (let i = 0; i < n; i++) {
    const v = new THREE.Vector3(dir.x * range(2, 9) + range(-3, 3) * spread, range(1.5, 7), dir.z * range(2, 9) + range(-3, 3) * spread);
    BLOOD.spawn(pos.clone().add(new THREE.Vector3(range(-0.15, 0.15), range(-0.2, 0.2), range(-0.15, 0.15))), v, range(1.2, 2.4), range(0.04, 0.11), pick(BLOOD_COLORS));
  }
  if (Math.random() < 0.7) decalAt(BLOOD_DECALS, pos.clone().addScaledVector(dir, range(0.5, 1.5)), range(0.6, 1.4));
}
function slideSparks(pos, dir) {
  for (let i = 0; i < 3; i++) SPARKS.spawn(pos.clone().addScaledVector(dir, -0.2).add(new THREE.Vector3(range(-0.2, 0.2), 0.05, range(-0.2, 0.2))), new THREE.Vector3(-dir.x * range(3, 7) + range(-1.5, 1.5), range(1, 4), -dir.z * range(3, 7) + range(-1.5, 1.5)), range(0.15, 0.35), range(0.04, 0.07), pick([0xffb35c, 0xffe28a, 0xff7a3a]));
  if (Math.random() < 0.3) puff(pos.clone().addScaledVector(dir, -0.4), 1, 0.4, 0x8a8078);
}
function hitSparks(pos, dir, n = 8, colors = [0xffe28a, 0xffffff, 0xff9a5a]) {
  for (let i = 0; i < n; i++) SPARKS.spawn(pos, new THREE.Vector3(dir.x * range(3, 10) + range(-4, 4), range(-1, 6), dir.z * range(3, 10) + range(-4, 4)), range(0.12, 0.3), range(0.03, 0.06), pick(colors));
}
function chromeDrip(pos, vel) { DRIPS.spawn(pos.clone().addScaledVector(vel, -0.03), new THREE.Vector3(-vel.x * 0.08 + range(-0.5, 0.5), range(-0.5, 1), -vel.z * 0.08 + range(-0.5, 0.5)), range(0.5, 1.0), range(0.015, 0.04)); }

// --- Impacts au sol : fissure, débris, onde de choc -------------------------------------------------
const shockRings = [0, 1, 2, 3, 4, 5].map(() => {
  const m = new THREE.Mesh(new THREE.RingGeometry(0.85, 1, 48), new THREE.MeshBasicMaterial({ color: 0xff7a3a, transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending, toneMapped: false }));
  m.rotation.x = -Math.PI / 2; m.visible = false; scene.add(m); return { m, t: 0, dur: 0.4, r: 4 };
});
let shockIndex = 0;
function groundImpact(pos, power, color = 0xff7a3a) {
  const y = groundAt(pos.clone().add(new THREE.Vector3(0, 0.5, 0)));
  const at = new THREE.Vector3(pos.x, y, pos.z);
  if (power >= 0.9) decalAt(CRACK_DECALS, at, 1.4 + power * 1.3, y);
  const n = Math.round(5 + power * 9), surf = surfaceAt(pos.x, pos.z);
  const col = surf === "grass" ? [0x3a4a2a, 0x2a3320, 0x5a4a3a] : surf === "wood" ? [0x5a4030, 0x3a2a20] : [0x3a3840, 0x4a4650, 0x2a282e, 0x6a6460];
  for (let i = 0; i < n; i++) {
    const a = Math.random() * Math.PI * 2, sp = range(2, 5 + power * 3);
    DEBRIS.spawn(at.clone().add(new THREE.Vector3(Math.cos(a) * 0.4, 0.1, Math.sin(a) * 0.4)), new THREE.Vector3(Math.cos(a) * sp, range(3, 6 + power * 3), Math.sin(a) * sp), range(1.8, 3.2), range(0.06, 0.12 + power * 0.05), pick(col));
  }
  const s = shockRings[shockIndex++ % shockRings.length]; s.t = 0; s.dur = 0.3 + power * 0.08; s.r = 2 + power * 2.2; s.m.material.color.setHex(color); s.m.position.set(at.x, y + 0.06, at.z); s.m.visible = true;
  pixelRing(at, 1.5 + power * 1.6, 0.45, color); puff(at, Math.round(4 + power * 4), 1 + power, 0x6a6460);
  tone(70 + 30 / power, 30, 0.3 + power * 0.1, Math.min(0.4, 0.12 + power * 0.08), "sine"); noiseHit(160, 0.7, 0.25 + power * 0.08, Math.min(0.35, 0.1 + power * 0.08), "lowpass");
  FX.freeze = Math.max(FX.freeze, Math.min(0.1, 0.02 * power));
}
function updateShocks(dt) {
  for (const s of shockRings) {
    if (!s.m.visible) continue; s.t += dt; const f = s.t / s.dur;
    if (f >= 1) { s.m.visible = false; continue; }
    const r = 0.3 + s.r * easeOut(f); s.m.scale.set(r, r, 1); s.m.material.opacity = 1 - f;
  }
}

// --- Monstres en morceaux -------------------------------------------------------------------------
// Chaque pièce garde sa position dans le monde, puis vole, rebondit et saigne.
const gibs = [];
function gibify(m, dir, power = 1) {
  const pieces = [];
  m.root.updateMatrixWorld(true);
  m.root.traverse((o) => { if (o.isMesh && !(o.material && o.material.blending === THREE.AdditiveBlending)) pieces.push(o); });
  for (const x of m.extra) if (x.isMesh) pieces.push(x);
  const center = m.pos.clone().add(new THREE.Vector3(0, m.top * 0.45, 0)), taken = new Set();
  // trop de petites pièces : on en garde une quarantaine (les plus grosses d'abord)
  pieces.sort((a, b) => (b.geometry.boundingSphere || (b.geometry.computeBoundingSphere(), b.geometry.boundingSphere)).radius * b.scale.length() - (a.geometry.boundingSphere || (a.geometry.computeBoundingSphere(), a.geometry.boundingSphere)).radius * a.scale.length());
  for (const o of pieces.slice(0, 42)) {
    o.updateMatrixWorld(true);
    const wp = new THREE.Vector3(), wq = new THREE.Quaternion(), ws = new THREE.Vector3(); o.matrixWorld.decompose(wp, wq, ws);
    o.parent.remove(o); o.position.copy(wp); o.quaternion.copy(wq); o.scale.copy(ws); scene.add(o); taken.add(o);
    const out = wp.clone().sub(center).setY(0); if (out.lengthSq() < 1e-4) out.set(range(-1, 1), 0, range(-1, 1)); out.normalize();
    const vel = out.multiplyScalar(range(3, 8) * power).addScaledVector(dir || new THREE.Vector3(), range(2, 7) * power).add(new THREE.Vector3(0, range(4, 10) * power, 0));
    gibs.push({ o, vel, spin: new THREE.Vector3(range(-14, 14), range(-14, 14), range(-14, 14)), life: range(5, 8), ground: m.pos.y, bleed: range(0.4, 1.4), drip: 0, landed: 0, s0: ws.clone() });
  }
  m.extra = m.extra.filter((x) => !taken.has(x));
  while (gibs.length > 140) { const g = gibs.shift(); scene.remove(g.o); }
}
function updateGibs(dt) {
  for (let i = gibs.length - 1; i >= 0; i--) {
    const g = gibs[i]; g.life -= dt;
    if (g.life <= 0) { scene.remove(g.o); gibs.splice(i, 1); continue; }
    g.vel.y -= 22 * dt; g.o.position.addScaledVector(g.vel, dt);
    g.o.rotation.x += g.spin.x * dt; g.o.rotation.y += g.spin.y * dt; g.o.rotation.z += g.spin.z * dt;
    if (g.o.position.y < g.ground + 0.05) {
      g.o.position.y = g.ground + 0.05;
      if (g.vel.y < -3) { if (!g.landed++) decalAt(BLOOD_DECALS, g.o.position, range(0.5, 1.1), g.ground); g.vel.y *= -0.3; g.vel.x *= 0.55; g.vel.z *= 0.55; g.spin.multiplyScalar(0.5); }
      else { g.vel.set(0, 0, 0); g.spin.multiplyScalar(0.9); }
    }
    // les morceaux saignent en vol
    g.bleed -= dt; g.drip -= dt;
    if (g.bleed > 0 && g.drip <= 0) { g.drip = 0.05; BLOOD.spawn(g.o.position, new THREE.Vector3(range(-1, 1), range(0, 2), range(-1, 1)), 1.5, range(0.04, 0.08), pick(BLOOD_COLORS)); }
    if (g.life < 1) g.o.scale.copy(g.s0).multiplyScalar(Math.max(0.01, g.life));
  }
}

// --- Jauge de style ---------------------------------------------------------------------------------
const STYLE_RANKS = [["D", "Détraqué", "#b9afbb"], ["C", "Cruel", "#8fe6ff"], ["B", "Brutal", "#8ff0b0"], ["A", "Atroce", "#ffe28a"], ["S", "Sanguinaire", "#ff9a5a"], ["SS", "Sadique", "#ff5a4a"], ["SSS", "Sans pitié", "#ff2a2a"], ["TWISTED", "", "#c86bff"]];
const STYLE = { rank: 0, pts: 0, quiet: 99, feed: [] };
const styleEl = document.getElementById("style");
function styleAdd(points, label) {
  STYLE.pts += points; STYLE.quiet = 0;
  while (STYLE.pts >= 100 && STYLE.rank < STYLE_RANKS.length - 1) { STYLE.pts -= 100; STYLE.rank++; }
  if (STYLE.rank === STYLE_RANKS.length - 1) STYLE.pts = Math.min(STYLE.pts, 100);
  if (label) { STYLE.feed.unshift({ label, t: 3 }); STYLE.feed.length = Math.min(STYLE.feed.length, 6); }
}
function styleHit() { STYLE.pts -= 40; if (STYLE.pts < 0) { if (STYLE.rank > 0) { STYLE.rank--; STYLE.pts += 70; } else STYLE.pts = 0; } STYLE.quiet = 0; }
let styleHud = 0;
function updateStyle(dt) {
  STYLE.quiet += dt;
  STYLE.pts -= dt * (STYLE.quiet > 1.5 ? 6 + STYLE.rank * 4 : 2);
  if (STYLE.pts < 0) { if (STYLE.rank > 0) { STYLE.rank--; STYLE.pts += 100; } else STYLE.pts = 0; }
  for (const f of STYLE.feed) f.t -= dt;
  STYLE.feed = STYLE.feed.filter((f) => f.t > 0);
  styleHud -= dt; if (styleHud > 0 || !styleEl) return; styleHud = 0.06;
  const show = STYLE.rank > 0 || STYLE.pts > 1 || STYLE.feed.length;
  styleEl.hidden = !show; if (!show) return;
  const [l, name, col] = STYLE_RANKS[STYLE.rank];
  styleEl.innerHTML = `<b style="color:${col}">${l}</b><span style="color:${col}">${name}</span><div class="bar"><i style="width:${Math.max(0, Math.min(100, STYLE.pts))}%;background:${col}"></i></div>` + STYLE.feed.map((f) => `<em style="opacity:${Math.min(1, f.t)}">+ ${f.label}</em>`).join("");
}

function updateFx(dt) { BLOOD.update(dt); DEBRIS.update(dt); SPARKS.update(dt); DRIPS.update(dt); updateDecals(dt); updateShocks(dt); updateGibs(dt); updateStyle(dt); }
