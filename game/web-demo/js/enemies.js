"use strict";
// ============================================================================
// Bestiaire : l'apparence et les mouvements des monstres. Leur cerveau (quand
// approcher, tourner autour, annoncer puis lancer une attaque) est dans la Sim
// (thinkMonster) ; ici on ne fait que l'affichage et la physique des corps.
//   Jour : Grésillon (petit parasite à pattes de cuivre), Câblé (humanoïde de câbles)
//   Nuit : Gueule-enceinte (enceinte affamée sur pattes de câble), Ombre sub
// Les pattes et les bras en câble sont posés par cinématique inverse (deux
// segments, le genou se place tout seul) et les pieds se plantent au sol.
// Les câbles qui pendent sont de petites cordes simulées (Verlet).
// ============================================================================
const MONSTERS = new Map();
const _mv = new THREE.Vector3();

// --- Outils : segments, genoux, cordes -------------------------------------------------
function segGeo(r0, r1, seg = 6) { const g = new THREE.CylinderGeometry(r1, r0, 1, seg, 1); g.translate(0, 0.5, 0); return g; }
function placeSeg(mesh, a, b) {
  _mv.copy(b).sub(a); const len = _mv.length() || 1e-4;
  mesh.position.copy(a); mesh.quaternion.setFromUnitVectors(UP, _mv.divideScalar(len)); mesh.scale.set(1, len, 1);
}
// genou (ou coude) entre la hanche et le pied, plié du côté de « pole »
function solveKnee(hip, foot, l1, l2, pole, out) {
  const d = foot.clone().sub(hip); let dist = d.length();
  const dir = dist > 1e-4 ? d.divideScalar(dist) : new THREE.Vector3(0, -1, 0);
  dist = Math.min(Math.max(dist, 0.05), l1 + l2 - 1e-3);
  const a = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist), h = Math.sqrt(Math.max(0, l1 * l1 - a * a));
  const bend = pole.clone().addScaledVector(dir, -pole.dot(dir)); if (bend.lengthSq() < 1e-6) bend.set(0, 1, 0); bend.normalize();
  return out.copy(hip).addScaledVector(dir, a).addScaledVector(bend, h);
}
const std = (color, rough = 0.6, metal = 0.1, extra = {}) => new THREE.MeshStandardMaterial(Object.assign({ color, roughness: rough, metalness: metal }, extra));
const glowM = (color, opacity = 1) => new THREE.MeshBasicMaterial({ color, transparent: opacity < 1, opacity, toneMapped: false });
class Rope {
  constructor(n, seg, color, opacity = 1) {
    this.n = n; this.seg = seg; this.p = []; this.o = [];
    for (let i = 0; i < n; i++) { this.p.push(new THREE.Vector3()); this.o.push(new THREE.Vector3()); }
    this.line = new THREE.Line(new THREE.BufferGeometry().setFromPoints(this.p), new THREE.LineBasicMaterial({ color, transparent: opacity < 1, opacity }));
    this.line.frustumCulled = false; scene.add(this.line);
  }
  reset(anchor, dir = new THREE.Vector3(0, -1, 0)) { for (let i = 0; i < this.n; i++) { this.p[i].copy(anchor).addScaledVector(dir, i * this.seg); this.o[i].copy(this.p[i]); } }
  // impulsion (fouet) : les nœuds partent dans une direction, plus fort au bout
  kick(v, dt = 1 / 60) { for (let i = 1; i < this.n; i++) this.o[i].addScaledVector(v, -dt * (i / this.n)); }
  update(dt, anchor, groundY, gravity = 16, damp = 0.985) {
    const g = gravity * dt * dt;
    this.p[0].copy(anchor); this.o[0].copy(anchor);
    for (let i = 1; i < this.n; i++) {
      const p = this.p[i], o = this.o[i], vx = (p.x - o.x) * damp, vy = (p.y - o.y) * damp, vz = (p.z - o.z) * damp;
      o.copy(p); p.x += vx; p.y += vy - g; p.z += vz;
    }
    for (let k = 0; k < 6; k++) {
      for (let i = 1; i < this.n; i++) {
        const a = this.p[i - 1], b = this.p[i]; _mv.copy(b).sub(a); const d = _mv.length() || 1e-5, diff = (d - this.seg) / d;
        if (i === 1) b.addScaledVector(_mv, -diff); else { a.addScaledVector(_mv, diff * 0.5); b.addScaledVector(_mv, -diff * 0.5); }
      }
      for (let i = 1; i < this.n; i++) if (this.p[i].y < groundY + 0.02) { this.p[i].y = groundY + 0.02; this.o[i].x += (this.p[i].x - this.o[i].x) * 0.3; this.o[i].z += (this.p[i].z - this.o[i].z) * 0.3; }
    }
    const pos = this.line.geometry.attributes.position;
    for (let i = 0; i < this.n; i++) pos.setXYZ(i, this.p[i].x, this.p[i].y, this.p[i].z);
    pos.needsUpdate = true;
  }
  dispose() { scene.remove(this.line); this.line.geometry.dispose(); }
}
// Patte ou bras à deux segments, dans le repère du monde
function makeLimb(m, l1, l2, r, mat, opts = {}) {
  const a = new THREE.Mesh(segGeo(r, r * 0.8), mat), b = new THREE.Mesh(segGeo(r * 0.8, r * (opts.tip ?? 0.45)), mat);
  a.castShadow = b.castShadow = true; scene.add(a, b); m.extra.push(a, b);
  const limb = { a, b, l1, l2, knee: new THREE.Vector3(), foot: new THREE.Vector3(), from: new THREE.Vector3(), to: new THREE.Vector3(), t: 1, group: opts.group || 0,
    hip: opts.hip || new THREE.Vector3(), rest: opts.rest || new THREE.Vector3(), pole: opts.pole || new THREE.Vector3(0, 1, 0), joint: null, strand: null };
  if (opts.joint) { limb.joint = new THREE.Mesh(new THREE.SphereGeometry(r * 1.35, 8, 6), opts.joint); scene.add(limb.joint); m.extra.push(limb.joint); }
  if (opts.strand) { limb.strand = [new THREE.Mesh(segGeo(r * 0.35, r * 0.3, 4), opts.strand), new THREE.Mesh(segGeo(r * 0.3, r * 0.25, 4), opts.strand)]; scene.add(...limb.strand); m.extra.push(...limb.strand); }
  return limb;
}
function poseLimb(limb, hip, foot) {
  solveKnee(hip, foot, limb.l1, limb.l2, limb.pole, limb.knee);
  placeSeg(limb.a, hip, limb.knee); placeSeg(limb.b, limb.knee, foot);
  if (limb.joint) limb.joint.position.copy(limb.knee);
  if (limb.strand) {
    // un fil de cuivre enroulé autour du câble, légèrement décalé
    const off = new THREE.Vector3(0.03, 0.02, 0);
    placeSeg(limb.strand[0], hip.clone().add(off), limb.knee.clone().add(off)); placeSeg(limb.strand[1], limb.knee.clone().add(off), foot.clone().add(off));
  }
}
// Marche procédurale : chaque pied reste planté, puis fait un pas quand le corps s'est trop éloigné.
// Deux groupes de pattes alternent (jamais toutes en l'air).
function stepLegs(m, legs, dt, o) {
  const yaw = m.yaw, cy = Math.cos(yaw), sy = Math.sin(yaw);
  const lead = m.vel.clone().setY(0).multiplyScalar(o.lead ?? 0.25);
  const busy = [false, false]; for (const l of legs) if (l.t < 1) busy[l.group] = true;
  const ground = m.pos.y;
  for (const l of legs) {
    const hip = l.hip.clone().applyMatrix4(m.body.matrixWorld);
    // position de repos du pied, tournée avec le corps
    const rest = new THREE.Vector3(l.rest.x * cy + l.rest.z * sy, 0, -l.rest.x * sy + l.rest.z * cy).add(m.pos).add(lead); rest.y = ground;
    if (o.tuck) { l.foot.lerp(hip.clone().add(new THREE.Vector3(0, -o.tuck, 0)).add(rest.clone().sub(hip).setY(0).multiplyScalar(0.45)), 1 - Math.exp(-14 * dt)); l.t = 1; }
    else if (l.t < 1) {
      l.t = Math.min(1, l.t + dt / o.stepDur); const f = l.t;
      l.foot.lerpVectors(l.from, l.to, easeInOut(f)); l.foot.y += Math.sin(f * Math.PI) * o.lift;
      if (l.t >= 1 && o.onStep) o.onStep(l);
    } else if (l.foot.distanceTo(rest) > o.stride && !busy[1 - l.group]) {
      l.from.copy(l.foot); l.to.copy(rest).add(lead.clone().multiplyScalar(0.6)); l.to.y = ground; l.t = 0; busy[l.group] = true;
    } else l.foot.y = Math.max(l.foot.y, ground);
    poseLimb(l, hip, l.foot);
  }
}

// ============================================================================
// Création d'un monstre
// ============================================================================
const MONSTER_SIZE = { gresillon: { r: 0.4, h: 0.9, top: 1.0 }, cable: { r: 0.42, h: 1.8, top: 2.15 }, gueule: { r: 0.75, h: 1.6, top: 2.75 }, ombre_sub: { r: 0.55, h: 1.5, top: 2.4 } };
function spawnMonster(kind, pos) {
  const id = sim.spawn(kind, pos), sz = MONSTER_SIZE[kind];
  const m = { id, kind, def: DATA.bestiary[kind], pos: pos.clone(), vel: new THREE.Vector3(), yaw: Math.random() * Math.PI * 2, grounded: true, knockT: 0, airborne: false, tumble: 0,
    r: sz.r, h: sz.h, top: sz.top, t: Math.random() * 10, phase: 0, hitFlash: 0, flashMats: [], eyes: [], extra: [], ropes: [], limbs: [], dying: false, deadT: 0,
    windup: null, strike: null, spawnT: 0, twitch: new THREE.Vector3(), twitchAt: 0, mouth: 0, night: DATA.bestiary[kind].night || isNight(), damaged: false, bar: null, stars: null };
  m.root = new THREE.Group(); m.body = new THREE.Group(); m.root.add(m.body); scene.add(m.root);
  BUILD[kind](m);
  m.root.position.copy(m.pos); m.root.rotation.y = m.yaw; m.root.updateMatrixWorld(true);
  for (const mat of m.flashMats) { mat.userData.em = mat.emissive.clone(); mat.userData.ei = mat.emissiveIntensity; }
  if (m.init) m.init(m);
  sim.setTransform(id, m.pos, new THREE.Vector3(-Math.sin(m.yaw), 0, -Math.cos(m.yaw)));
  MONSTERS.set(id, m);
  // apparition : un anneau de pixels, rouge la nuit
  pixelRing(m.pos, 2.2, 0.6, m.night ? 0xff3a2a : 0xbfe9ff); puff(m.pos, 6, 1.2, m.night ? 0x3a1020 : 0xd9cdbf);
  return m;
}
function removeMonster(m) {
  scene.remove(m.root); for (const x of m.extra) scene.remove(x); for (const r of m.ropes) r.dispose();
  if (m.stars) scene.remove(m.stars); if (m.bar) m.bar.remove();
  MONSTERS.delete(m.id); sim.despawn(m.id);
}
const addTo = (parent, geo, mat, x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, sx = 1, sy = 1, sz = 1, shadow = true) => {
  const mesh = new THREE.Mesh(geo, mat); mesh.position.set(x, y, z); mesh.rotation.set(rx, ry, rz); mesh.scale.set(sx, sy, sz); mesh.castShadow = shadow; parent.add(mesh); return mesh;
};
const eyeColor = (m, day) => (m.night ? 0xff2a1a : day);

const BUILD = {
  // --- Grésillon : un petit tweeter sur six pattes de fil de cuivre ------------------------
  gresillon(m) {
    const shell = std(0x2c2b31, 0.45, 0.6), copper = std(0xc87533, 0.35, 0.85), dome = std(0xd8dce6, 0.25, 0.9), dark = std(0x16151a, 0.7, 0.2), tooth = std(0xe8e0d0, 0.4, 0);
    const eye = glowM(eyeColor(m, 0xffb35c));
    m.flashMats.push(shell, dome); m.eyes.push(eye);
    m.body.position.y = 0.45;
    addTo(m.body, new THREE.SphereGeometry(0.26, 18, 12), shell, 0, 0, 0, 0, 0, 0, 1.05, 0.72, 1.15);
    addTo(m.body, new THREE.SphereGeometry(0.13, 16, 10), dome, 0, 0.02, -0.24, 0, 0, 0, 1, 1, 0.6);
    addTo(m.body, new THREE.TorusGeometry(0.135, 0.022, 6, 20), copper, 0, 0.02, -0.25);
    addTo(m.body, new THREE.CylinderGeometry(0.14, 0.1, 0.12, 14), dark, 0, 0, 0.26, Math.PI / 2);
    for (const s of [-1, 1]) addTo(m.body, new THREE.SphereGeometry(0.036, 8, 6), eye, s * 0.09, 0.11, -0.22, 0, 0, 0, 1, 1, 1, false);
    const ant = addTo(m.body, segGeo(0.008, 0.006, 4), copper, 0, 0.16, 0.06, -0.6, 0, 0.2, 1, 0.38, 1, false);
    addTo(m.body, new THREE.SphereGeometry(0.026, 8, 6), copper, 0.03, 0.16 + Math.cos(0.6) * 0.38, 0.06 + Math.sin(0.6) * 0.38, 0, 0, 0, 1, 1, 1, false);
    // mandibules
    m.jaw = [];
    for (const s of [-1, 1]) { const j = new THREE.Group(); j.position.set(s * 0.07, -0.08, -0.22); m.body.add(j); addTo(j, new THREE.ConeGeometry(0.025, 0.12, 6), tooth, 0, -0.04, -0.02, Math.PI - 0.3, 0, s * 0.3, 1, 1, 1, false); m.jaw.push(j); }
    const legs = [];
    [[-1, -0.13], [1, 0], [-1, 0.13], [1, -0.13], [-1, 0], [1, 0.13]].forEach(([s, z], i) => {
      legs.push(makeLimb(m, 0.34, 0.42, 0.017, copper, { group: i < 3 ? 0 : 1, hip: new THREE.Vector3(s * 0.17, -0.04, z), rest: new THREE.Vector3(s * 0.55, 0, z * 2.4 - 0.05), pole: new THREE.Vector3(s * 0.4, 1, 0) }));
    });
    m.limbs = legs;
    m.init = () => { for (const l of legs) { const hip = l.hip.clone().applyMatrix4(m.body.matrixWorld); l.foot.copy(hip).setY(m.pos.y).add(new THREE.Vector3(Math.sign(l.hip.x) * 0.35, 0, 0)); } };
    m.animate = (m, e, dt) => {
      const st = e.mon.state, sp = m.vel.clone().setY(0).length();
      let y = 0.45 + (sp > 0.3 ? Math.abs(Math.sin(m.t * 16)) * 0.02 : Math.sin(m.t * 3) * 0.01), rx = 0, open = 0;
      if (m.windup) {
        const f = Math.min(1, m.windup.t / m.windup.dur);
        if (m.windup.attack === "bond") { y = 0.45 - 0.18 * f; rx = -0.25 * f; } else { rx = -0.45 * f; open = f; }
        m.body.position.x = (Math.random() - 0.5) * 0.02 * f; // il grésille avant de bondir
      } else m.body.position.x = 0;
      if (st === "dash") { const f = Math.min(1, (m.strike ? m.strike.t : 0) / 0.38); y += Math.sin(f * Math.PI) * 0.7; rx = 0.35; open = 1; }
      if (m.strike && m.strike.t < 0.2 && m.strike.attack === "morsure") { rx = 0.35; open = 1 - m.strike.t * 5; }
      m.body.position.y += (y - m.body.position.y) * Math.min(1, dt * 20);
      m.body.rotation.x += (rx - m.body.rotation.x) * Math.min(1, dt * 18);
      m.body.rotation.z = m.dying ? Math.min(Math.PI, m.deadT * 7) : m.twitch.z + (m.night ? Math.sin(m.t * 23) * 0.03 : 0);
      m.jaw.forEach((j, i) => (j.rotation.z = (i ? -1 : 1) * open * 0.5 + Math.sin(m.t * 30) * 0.05));
      m.body.updateMatrixWorld(true);
      stepLegs(m, legs, dt, { stride: 0.32, stepDur: 0.11, lift: 0.14, lead: 0.12, tuck: st === "dash" || m.airborne || m.dying ? 0.25 : 0 });
    };
  },

  // --- Câblé : un humanoïde de câbles noirs, tête en fiche jack ---------------------------------
  cable(m) {
    const rubber = std(0x1e1d24, 0.55, 0.2);
    const rig = buildRig(rubber, 0, true); m.rig = rig; scene.remove(rig.root); m.body.add(rig.root);
    for (const mesh of rig.meshes) mesh.castShadow = true;
    rig.root.scale.set(0.8, 1.06, 0.8); // plus maigre et plus grand qu'un humain : il met mal à l'aise
    m.flashMats.push(rubber);
    rig.head.visible = false;
    const silver = std(0xc9ced8, 0.25, 0.9), black = std(0x101014, 0.5, 0.2), eye = glowM(eyeColor(m, 0x7ae8ff));
    m.eyes.push(eye);
    const neck = rig.J.neck.g;
    addTo(neck, new THREE.CylinderGeometry(0.085, 0.07, 0.24, 12), black, 0, 0.14, 0);
    addTo(neck, new THREE.CylinderGeometry(0.048, 0.048, 0.2, 12), silver, 0, 0.35, 0);
    addTo(neck, new THREE.CylinderGeometry(0.05, 0.05, 0.025, 12), black, 0, 0.4, 0);
    addTo(neck, new THREE.SphereGeometry(0.05, 12, 8), silver, 0, 0.47, 0, 0, 0, 0, 1, 1.3, 1);
    for (const s of [-1, 1]) addTo(neck, new THREE.SphereGeometry(0.022, 8, 6), eye, s * 0.035, 0.17, -0.075, 0, 0, 0, 1, 1, 1, false);
    // câbles de couleur enroulés autour des membres
    const colors = [0x7a2a22, 0x8a6a22, 0x24406a, 0x2a2a30];
    for (const [joint, y0, n, r] of [["shoulderR", -0.1, 3, 0.075], ["shoulderL", -0.1, 3, 0.075], ["elbowR", -0.1, 2, 0.065], ["elbowL", -0.1, 2, 0.065], ["hipR", -0.15, 3, 0.095], ["hipL", -0.15, 3, 0.095], ["chest", 0.05, 4, 0.2], ["waist", 0.08, 2, 0.17]]) {
      for (let i = 0; i < n; i++) addTo(rig.J[joint].g, new THREE.TorusGeometry(r, 0.012, 5, 18), std(colors[(i + joint.length) % 4], 0.5, 0.1), 0, y0 - i * 0.07, 0, Math.PI / 2 + 0.25, 0, 0.15 * (i % 2 ? 1 : -1), joint === "chest" ? 1.25 : 1, 1, joint === "chest" ? 0.84 : 1, false);
    }
    // le fouet : un long câble qui part de la main droite, et des câbles qui pendent du dos
    m.whip = new Rope(13, 0.23, 0x18171c); m.ropes.push(m.whip);
    m.backRopes = [new Rope(7, 0.14, 0x2a2930), new Rope(6, 0.14, 0x7a2a22), new Rope(8, 0.14, 0x18171c)]; m.ropes.push(...m.backRopes);
    m.init = () => { const h = rig.handR.getWorldPosition(new THREE.Vector3()); m.whip.reset(h); for (const r of m.backRopes) r.reset(rig.J.chest.g.getWorldPosition(new THREE.Vector3())); };
    m.animate = (m, e, dt) => {
      const sp = m.vel.clone().setY(0).length(), run = Math.min(1, sp / 4);
      m.phase += dt * (4 + sp * 1.6);
      const s = Math.sin(m.phase), c = Math.cos(m.phase), amp = sp > 0.3 ? 0.25 + 0.4 * run : 0, kb = sp > 0.3 ? 0.3 + 0.9 * run : 0;
      const b = {}, set = (j, x = 0, y = 0, z = 0) => { b[j] = { x, y, z }; };
      const look = lookYaw(m);
      set("waist", 0.32 + 0.15 * run, s * 0.12 * run, m.twitch.z * 0.5); set("chest", 0.12);
      set("neck", -0.45 - 0.1 * run, look * 0.6, m.twitch.z + (m.night ? Math.sin(m.t * 0.9) * 0.35 : 0.1));
      set("hipR", 0.25 + s * amp, 0, 0.06); set("hipL", 0.25 - s * amp, 0, -0.06);
      set("kneeR", -0.45 - kb * Math.max(0, c)); set("kneeL", -0.45 - kb * Math.max(0, -c));
      set("shoulderR", 0.35 - s * amp * 0.6, 0, 0.2); set("shoulderL", 0.35 + s * amp * 0.6, 0, -0.2); set("elbowR", 0.6 + 0.3 * run); set("elbowL", 0.6 + 0.3 * run);
      if (e.mon.state === "stunned") { set("waist", 0.1, Math.sin(m.t * 3) * 0.3); set("neck", 0.4, 0, Math.sin(m.t * 3) * 0.5); }
      m.body.position.y = sp > 0.3 ? Math.abs(Math.cos(m.phase)) * 0.04 : 0;
      animateRig(rig, b, dt);
      m.root.updateMatrixWorld(true);
      const hand = rig.handR.getWorldPosition(new THREE.Vector3());
      m.whip.update(dt, hand, m.pos.y, 14);
      const back = new THREE.Vector3(0, 0.15, 0.18).applyMatrix4(rig.J.chest.g.matrixWorld);
      m.backRopes.forEach((r, i) => r.update(dt, i === 2 ? rig.handL.getWorldPosition(new THREE.Vector3()) : back.clone().add(new THREE.Vector3((i - 0.5) * 0.12, 0, 0)), m.pos.y));
    };
  },

  // --- Gueule-enceinte : enceinte affamée, pattes et bras en câble, la membrane est une gueule ---
  gueule(m) {
    const tolex = std(0x141317, 0.85, 0.05), metal = std(0x8d929e, 0.3, 0.85), rubber = std(0x1b1a1f, 0.6, 0.1), coneM = std(0x2a2830, 0.6, 0.1, { side: THREE.DoubleSide });
    const tooth = std(0xe6dccb, 0.4, 0.0), cableM = std(0x18171c, 0.5, 0.25), copper = std(0xc87533, 0.35, 0.85);
    const throat = glowM(0xff2a1a), eyeL = glowM(0xff2a1a), eyeR = glowM(0x5a0a08);
    m.flashMats.push(tolex, coneM); m.eyes.push(eyeL); m.throat = throat; m.deadEye = eyeR;
    const B = m.body; B.position.y = 1.7;
    const cab = new THREE.Group(); B.add(cab); m.cab = cab;
    addTo(cab, new THREE.BoxGeometry(0.95, 1.25, 0.7), tolex);
    for (const x of [-1, 1]) for (const y of [-1, 1]) for (const z of [-1, 1]) addTo(cab, new THREE.BoxGeometry(0.08, 0.08, 0.08), metal, x * 0.46, y * 0.6, z * 0.33, 0, 0, 0, 1, 1, 1, false);
    // la gueule : suspension, cône, gorge rougeoyante, deux mâchoires de dents
    const mouth = new THREE.Group(); mouth.position.set(0, -0.12, -0.36); cab.add(mouth); m.mouthG = mouth;
    addTo(mouth, new THREE.TorusGeometry(0.34, 0.055, 8, 28), rubber, 0, 0, 0, 0, 0, 0, 1, 1, 1, false);
    m.coneMesh = addTo(mouth, new THREE.CylinderGeometry(0.31, 0.08, 0.24, 24, 1, true), coneM, 0, 0, 0.12, -Math.PI / 2, 0, 0, 1, 1, 1, false);
    m.throatMesh = addTo(mouth, new THREE.CircleGeometry(0.1, 16), throat, 0, 0, 0.23, 0, Math.PI, 0, 1, 1, 1, false);
    m.jaws = [];
    for (const [a0, a1] of [[0.15, Math.PI - 0.15], [Math.PI + 0.15, Math.PI * 2 - 0.15]]) {
      const jaw = new THREE.Group(); mouth.add(jaw); m.jaws.push(jaw);
      for (let i = 0; i < 9; i++) {
        const a = a0 + ((a1 - a0) * i) / 8, len = i % 2 ? 0.13 : 0.19, p = new THREE.Vector3(Math.cos(a) * 0.3, Math.sin(a) * 0.3, -0.02);
        const t = addTo(jaw, new THREE.ConeGeometry(0.035, len, 6), tooth, p.x, p.y, p.z, 0, 0, 0, 1, 1, 1, false);
        t.quaternion.setFromUnitVectors(UP, p.clone().negate().setZ(0).normalize().add(new THREE.Vector3(0, 0, -0.45)).normalize());
        t.position.addScaledVector(p.clone().negate().normalize(), len * 0.35);
      }
    }
    // yeux : deux tweeters, l'un rouge vif, l'autre presque éteint
    addTo(cab, new THREE.SphereGeometry(0.075, 12, 8), eyeL, -0.25, 0.42, -0.35, 0, 0, 0, 1, 1, 0.5, false);
    addTo(cab, new THREE.SphereGeometry(0.07, 12, 8), eyeR, 0.25, 0.43, -0.35, 0, 0, 0, 1, 1, 0.5, false);
    for (const x of [-0.25, 0.25]) addTo(cab, new THREE.TorusGeometry(0.085, 0.015, 6, 16), metal, x, 0.425, -0.355, 0, 0, 0, 1, 1, 1, false);
    addTo(cab, new THREE.TorusGeometry(0.06, 0.018, 6, 14), rubber, 0.32, -0.5, -0.355, 0, 0, 0, 1, 1, 1, false);
    // halo rouge devant la gueule (bien visible la nuit)
    m.glow = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, color: 0xff3a2a, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, opacity: 0.5 }));
    m.glow.scale.setScalar(1.9); m.glow.position.set(0, -0.12, -0.6); cab.add(m.glow);
    // langue de câble qui pend de la gueule, câbles traînés dans le dos
    m.tongue = new Rope(7, 0.11, 0x3a0d10); m.ropes.push(m.tongue);
    m.tails = [new Rope(10, 0.2, 0x18171c), new Rope(9, 0.2, 0x2a2930), new Rope(8, 0.2, 0x5a1a14)]; m.ropes.push(...m.tails);
    // quatre longues pattes en câble, genoux hauts comme une araignée
    const legs = [];
    [[-1, -1], [1, 1], [1, -1], [-1, 1]].forEach(([sx, sz], i) => {
      legs.push(makeLimb(m, 1.2, 1.35, 0.045, cableM, { group: i < 2 ? 0 : 1, hip: new THREE.Vector3(sx * 0.4, -0.6, sz * 0.25), rest: new THREE.Vector3(sx * 1.15, 0, sz < 0 ? -0.8 : 0.9),
        pole: new THREE.Vector3(sx * 0.5, 1, sz * 0.2), joint: metal, strand: copper }));
    });
    // deux bras maigres terminés par des griffes
    const arms = [];
    for (const s of [-1, 1]) {
      const arm = makeLimb(m, 0.95, 1.05, 0.03, cableM, { hip: new THREE.Vector3(s * 0.5, 0.3, -0.15), pole: new THREE.Vector3(s * 0.6, 0.2, 0.8), joint: metal, tip: 0.6 });
      arm.claw = new THREE.Group(); scene.add(arm.claw); m.extra.push(arm.claw); arm.side = s;
      for (let k = -1; k <= 1; k++) { const f = addTo(arm.claw, new THREE.ConeGeometry(0.025, 0.4, 5), tooth, 0, 0.18, 0, 0, 0, 0, 1, 1, 1, false); f.position.set(Math.sin(k * 0.5) * 0.05, 0.18, Math.cos(k * 0.5) * 0.02); f.rotation.z = -k * 0.35; }
      arm.hand = new THREE.Vector3(); arms.push(arm);
    }
    m.limbs = legs; m.arms = arms;
    m.init = () => {
      m.body.updateMatrixWorld(true);
      for (const l of legs) { const r = l.rest; l.foot.set(r.x * Math.cos(m.yaw) + r.z * Math.sin(m.yaw), 0, -r.x * Math.sin(m.yaw) + r.z * Math.cos(m.yaw)).add(m.pos); }
      for (const a of arms) a.hand.copy(a.hip).applyMatrix4(m.body.matrixWorld).add(new THREE.Vector3(0, -1.2, 0));
      m.tongue.reset(new THREE.Vector3(0, -0.4, -0.36).applyMatrix4(m.cab.matrixWorld));
      for (const r of m.tails) r.reset(new THREE.Vector3(0, -0.5, 0.36).applyMatrix4(m.cab.matrixWorld), new THREE.Vector3(0, -0.6, 0.6).normalize());
    };
    m.animate = (m, e, dt) => {
      const st = e.mon.state, sp = m.vel.clone().setY(0).length(), run = Math.min(1, sp / 5);
      let y = 1.7 - 0.25 * run, rx = 0.3 + 0.3 * run, open = 0.12 + Math.sin(m.t * 1.7) * 0.06, shake = 0;
      // respiration et sursauts : la tête se penche, puis se redresse d'un coup
      let rz = Math.sin(m.t * 0.6) * 0.14 + m.twitch.z, ry = m.twitch.y;
      const w = m.windup, wf = w ? Math.min(1, w.t / w.dur) : 0;
      if (w && w.attack === "morsure") { y += 0.25 * wf; rx = 0.05; open = 0.3 + 0.9 * wf; shake = 0.02 * wf; }
      if (w && w.attack === "cri") { y += 0.15 * wf; rx = -0.15 * wf; open = 0.4 + 1.0 * wf; shake = 0.04 * wf; }
      if (w && w.attack === "griffes") { rx = 0.1; open = 0.5 * wf; }
      if (st === "dash") { rx = 0.75; open = 1.3; y -= 0.15; }
      if (m.strike) {
        const f = m.strike.t;
        if (m.strike.attack === "morsure" && st !== "dash") open = Math.max(0, 1.1 - f * 6);
        if (m.strike.attack === "cri" && f < 0.6) { open = 1.4; shake = 0.05; }
      }
      if (st === "stunned" || st === "stagger") { rz += Math.sin(m.t * 9) * 0.12; open = 0.6; }
      if (m.dying) { const f = Math.min(1, m.deadT / 0.9); y = 1.7 - 1.2 * easeOut(f); rx = 0.3 + 1.0 * f; open = 0.9; rz = 0.4 * f; }
      m.mouth += (open - m.mouth) * Math.min(1, dt * 14);
      B.position.y += (y + (sp > 0.3 ? Math.sin(m.phase * 2) * 0.05 : 0) - B.position.y) * Math.min(1, dt * 10);
      B.position.x = (Math.random() - 0.5) * shake; B.position.z = (Math.random() - 0.5) * shake;
      cab.rotation.x += (rx - cab.rotation.x) * Math.min(1, dt * 8); cab.rotation.z += (rz - cab.rotation.z) * Math.min(1, dt * 10); cab.rotation.y += (ry - cab.rotation.y) * Math.min(1, dt * 10);
      m.jaws[0].position.y = m.mouth * 0.12; m.jaws[1].position.y = -m.mouth * 0.15; m.jaws[0].rotation.x = -m.mouth * 0.35; m.jaws[1].rotation.x = m.mouth * 0.35;
      m.coneMesh.scale.set(1, 1 + m.mouth * 0.5, 1); m.throatMesh.scale.setScalar(1 + m.mouth * 0.8);
      const pulse = 0.6 + 0.4 * Math.sin(m.t * (w ? 25 : 3));
      m.throat.color.setRGB(1, 0.16, 0.1).multiplyScalar(0.5 + pulse * 0.5 + m.mouth * 0.5);
      m.glow.material.opacity = (0.2 + 0.7 * DN.n) * (0.6 + m.mouth * 0.6) * (m.dying ? Math.max(0, 1 - m.deadT) : 1);
      m.deadEye.color.setHex(Math.random() < 0.04 ? 0xff2a1a : 0x3a0806);
      m.phase += dt * (2.5 + sp * 1.2);
      m.root.updateMatrixWorld(true);
      stepLegs(m, legs, dt, { stride: 0.75, stepDur: 0.2 - 0.07 * run, lift: 0.45, lead: 0.3, tuck: m.airborne ? 0.6 : 0, onStep: (l) => { if (m.pos.distanceTo(P.pos) < 25) noiseHit(700, 4, 0.05, 0.05); } });
      // bras : traînent devant, griffes au sol ; se lèvent pour griffer
      const cy = Math.cos(m.yaw), sy = Math.sin(m.yaw), toW = (x, y2, z) => new THREE.Vector3(x * cy + z * sy, y2, -x * sy + z * cy).add(m.pos);
      for (const a of m.arms) {
        let target = toW(a.side * 0.75, 0.15 + Math.sin(m.t * 2 + a.side) * 0.08, -1.0 - 0.3 * run);
        if (w && w.attack === "griffes") target = toW(a.side * 1.0, 2.6 + 0.4 * wf, -0.2);
        if (w && w.attack === "cri") target = toW(a.side * 1.3, 1.6, -0.4);
        if (m.strike && m.strike.attack === "griffes" && m.strike.t < 0.35) target = toW(-a.side * 0.3, 0.6, -1.9);
        if (st === "dash") target = toW(a.side * 0.6, 1.2, -1.6);
        a.hand.lerp(target, 1 - Math.exp(-(m.strike ? 26 : 9) * dt));
        const sh = a.hip.clone().applyMatrix4(m.body.matrixWorld);
        poseLimb(a, sh, a.hand);
        a.claw.position.copy(a.hand); a.claw.quaternion.setFromUnitVectors(UP, a.hand.clone().sub(a.knee).normalize());
      }
      m.tongue.update(dt, new THREE.Vector3(0, -0.12 - 0.3 - m.mouth * 0.15, -0.38).applyMatrix4(m.cab.matrixWorld), m.pos.y, 12);
      m.tails.forEach((r, i) => r.update(dt, new THREE.Vector3((i - 1) * 0.25, -0.55, 0.36).applyMatrix4(m.cab.matrixWorld), m.pos.y, 16, 0.97));
    };
  },

  // --- Ombre sub : une basse fréquence qui flotte, entourée d'ombre, traînant ses câbles --------
  ombre_sub(m) {
    const shell = std(0x0b0a10, 0.25, 0.6), coneM = std(0x1a1622, 0.5, 0.2, { side: THREE.DoubleSide });
    const core = glowM(0x9a4dff), ring = glowM(0x3a1580), eye = glowM(0xf0e0ff);
    m.flashMats.push(shell, coneM); m.eyes.push(eye); m.core = core;
    const B = m.body; B.position.y = 1.5;
    const ball = new THREE.Group(); B.add(ball); m.ball = ball;
    addTo(ball, new THREE.SphereGeometry(0.55, 22, 16), shell);
    m.coneMesh = addTo(ball, new THREE.CylinderGeometry(0.47, 0.12, 0.3, 24, 1, true), coneM, 0, 0, -0.38, -Math.PI / 2, 0, 0, 1, 1, 1, false);
    m.coreMesh = addTo(ball, new THREE.SphereGeometry(0.14, 14, 10), core, 0, 0, -0.3, 0, 0, 0, 1, 1, 0.6, false);
    addTo(ball, new THREE.TorusGeometry(0.48, 0.022, 6, 32), ring, 0, 0, -0.52, 0, 0, 0, 1, 1, 1, false);
    for (let i = 0; i < 3; i++) { const a = Math.PI / 2 + (i * Math.PI * 2) / 3; addTo(ball, new THREE.SphereGeometry(0.03, 8, 6), eye, Math.cos(a) * 0.24, Math.sin(a) * 0.24, -0.47, 0, 0, 0, 1, 1, 1, false); }
    // ombre qui tourne autour
    m.aura = [];
    for (let i = 0; i < 6; i++) { const sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, color: 0x0c0614, transparent: true, depthWrite: false, opacity: 0.55 })); sp.scale.setScalar(1.4 + (i % 3) * 0.4); B.add(sp); m.aura.push(sp); }
    m.tendrils = [];
    for (let i = 0; i < 7; i++) { const r = new Rope(10, 0.17, i % 3 ? 0x1a1024 : 0x3a1a5a); m.tendrils.push(r); m.ropes.push(r); }
    m.init = () => { m.root.updateMatrixWorld(true); m.tendrils.forEach((r, i) => { const a = (i / 7) * Math.PI * 2; r.reset(new THREE.Vector3(Math.cos(a) * 0.35, -0.4, Math.sin(a) * 0.35).applyMatrix4(ball.matrixWorld)); }); };
    m.animate = (m, e, dt) => {
      const w = m.windup, wf = w ? Math.min(1, w.t / w.dur) : 0;
      let scale = 1, y = 1.45 + Math.sin(m.t * 1.6) * 0.12, glow = 0.6 + 0.2 * Math.sin(m.t * 4), pump = 0;
      if (w && w.attack === "pulsation") { scale = 1 + 0.3 * wf; glow = 0.6 + wf; pump = Math.sin(m.t * 40) * 0.2 * wf; }
      if (w && w.attack === "infrabasse") { pump = Math.sin(m.t * 30) * 0.3 * wf; glow = 0.8 + 0.5 * wf; }
      if (w && w.attack === "fondu") scale = 1 - 0.85 * wf;
      if (m.blinkIn > 0) { m.blinkIn = Math.max(0, m.blinkIn - dt); scale = 1 - m.blinkIn / 0.35; }
      if (m.strike && m.strike.attack === "pulsation" && m.strike.t < 0.25) scale = 1.3 - m.strike.t * 1.2;
      if (m.dying) { const f = Math.min(1, m.deadT / 1.0); scale = 1 - 0.7 * f; y = 1.45 - 1.0 * f; }
      if (e.mon.state === "stunned" || e.mon.state === "stagger") ball.rotation.z = Math.sin(m.t * 10) * 0.3; else ball.rotation.z *= 0.9;
      B.position.y += (y - B.position.y) * Math.min(1, dt * 6);
      ball.scale.setScalar(Math.max(0.05, scale)); ball.rotation.x = 0.15 + m.twitch.x;
      m.coneMesh.scale.y = 1 + pump; m.coreMesh.scale.setScalar(1 + pump * 0.6);
      m.core.color.setRGB(0.6, 0.3, 1).multiplyScalar(glow);
      m.aura.forEach((sp, i) => { const a = m.t * (0.5 + i * 0.1) + i; sp.position.set(Math.cos(a) * 0.35, Math.sin(a * 1.3) * 0.25, Math.sin(a) * 0.35); sp.material.opacity = 0.5 * Math.max(0, scale); });
      m.root.updateMatrixWorld(true);
      m.tendrils.forEach((r, i) => { const a = (i / 7) * Math.PI * 2 + m.t * 0.3; r.update(dt, new THREE.Vector3(Math.cos(a) * 0.35 * scale, -0.42 * scale, Math.sin(a) * 0.35 * scale).applyMatrix4(ball.matrixWorld), m.pos.y, 9, 0.96); });
    };
  },
};
// angle de la tête vers le joueur (bornée)
function lookYaw(m) { const to = P.pos.clone().sub(m.pos); const a = wrapAngle(Math.atan2(-to.x, -to.z) - m.yaw); return Math.max(-0.9, Math.min(0.9, a)); }

// --- Attaques du Câblé (poses clés pour son squelette) ------------------------------------
Object.assign(ANIMS, {
  c_fouet: (w) => { const up = { waist: { x: -0.1, y: -0.5 }, shoulderR: { x: 2.9, z: 0.35 }, elbowR: { x: 1.2 }, shoulderL: { x: 0.5, z: -0.4 }, elbowL: { x: 0.8 } }, hit = { waist: { x: 0.45, y: 0.6 }, shoulderR: { x: 0.8, z: -0.25 }, elbowR: { x: 0.1 }, shoulderL: { x: 0.2, z: -0.6 }, elbowL: { x: 0.5 }, hipL: { x: 0.5 }, kneeL: { x: -0.7 } }; return [{ t: 0, k: 1.2, p: up }, { t: w * 0.9, p: up }, { t: w, k: 7, p: hit }, { t: w + 0.3, p: hit }]; },
  c_griffe: (w) => { const up = { waist: { x: 0.2, y: 0.55 }, shoulderL: { x: 1.9, z: -1.0 }, elbowL: { x: 1.0 } }, hit = { waist: { x: 0.45, y: -0.6 }, shoulderL: { x: 1.0, z: 0.35 }, elbowL: { x: 0.15 }, hipR: { x: 0.5 }, kneeR: { x: -0.7 } }; return [{ t: 0, k: 1.6, p: up }, { t: w * 0.9, p: up }, { t: w, k: 7, p: hit }, { t: w + 0.25, p: hit }]; },
  c_zap: (w) => { const p = (j) => ({ waist: { x: 0.1 }, shoulderR: { x: 1.5 + j, z: 0.1 }, shoulderL: { x: 1.5 - j, z: -0.1 }, elbowR: { x: 0.1 }, elbowL: { x: 0.1 }, neck: { x: -0.2 } }); const k = [{ t: 0, k: 2, p: p(0) }]; for (let i = 1; i < 8; i++) k.push({ t: (w * i) / 8, k: 6, p: p(i % 2 ? 0.12 : -0.12) }); k.push({ t: w + 0.25, p: p(0) }); return k; },
  c_hurt: () => { const p = { waist: { x: -0.3, y: 0.3 }, neck: { x: -0.6, z: 0.4 }, shoulderR: { x: 0.3, z: 0.9 }, shoulderL: { x: 0.3, z: -0.9 } }; return [{ t: 0, k: 7, p }, { t: 0.1, p }, { t: 0.3, k: 1.5, p: {} }]; },
  c_die: () => { const p = { waist: { x: 1.1 }, chest: { x: 0.4 }, neck: { x: 0.6, z: 0.5 }, hipR: { x: 1.4 }, kneeR: { x: -2.3 }, hipL: { x: 1.2 }, kneeL: { x: -2.0 }, shoulderR: { x: 0.2, z: 0.4 }, shoulderL: { x: 0.6, z: -0.2 }, elbowR: { x: 0.3 }, elbowL: { x: 0.8 } }; return [{ t: 0, k: 1.2, p }, { t: 9, p }]; },
});

// ============================================================================
// Physique des corps (pas fixe, avec le joueur)
// ============================================================================
function bodyHits(b, m, x, y, z) { return x + m.r > b.min[0] && x - m.r < b.max[0] && z + m.r > b.min[2] && z - m.r < b.max[2] && y + m.h > b.min[1] && y < b.max[1]; }
function bodyFree(m, x, y, z) { for (const b of colliders) if (bodyHits(b, m, x, y, z)) return false; return true; }
function moveBodyAxis(m, ax, delta) {
  if (!delta) return;
  const key = ax === 0 ? "x" : "z"; m.pos[key] += delta;
  for (const b of colliders) {
    if (!bodyHits(b, m, m.pos.x, m.pos.y, m.pos.z)) continue;
    const rise = b.max[1] - m.pos.y;
    if (rise > 0 && rise <= 0.45 && m.grounded) { const oy = m.pos.y; m.pos.y = b.max[1]; if (bodyFree(m, m.pos.x, m.pos.y, m.pos.z)) continue; m.pos.y = oy; }
    m.pos[key] = delta > 0 ? b.min[ax] - m.r - 1e-4 : b.max[ax] + m.r + 1e-4;
    m.vel[key] *= m.knockT > 0 ? -0.4 : 0;
  }
}
function stepMonsters(dt) {
  const list = [...MONSTERS.values()];
  for (const m of list) {
    const e = sim.entities.get(m.id); if (!e) continue;
    const steer = !m.dying && m.knockT <= 0 && !m.airborne;
    m.knockT = Math.max(0, m.knockT - dt);
    if (steer) {
      const it = e.mon.intent, rate = e.mon.state === "dash" ? 80 : m.grounded ? 14 : 3;
      m.vel.x += (it.x - m.vel.x) * Math.min(1, rate * dt); m.vel.z += (it.z - m.vel.z) * Math.min(1, rate * dt);
    } else if (m.grounded) { const k = Math.exp(-(m.dying ? 6 : 4.5) * dt); m.vel.x *= k; m.vel.z *= k; }
    m.vel.y -= 22 * dt;
    // les autres monstres et le joueur : on ne se traverse pas
    for (const o of list) {
      if (o === m || o.dying) continue;
      const dx = m.pos.x - o.pos.x, dz = m.pos.z - o.pos.z, d = Math.hypot(dx, dz), min = m.r + o.r;
      if (d < min && d > 1e-4) { const push = ((min - d) / d) * 0.5; m.pos.x += dx * push; m.pos.z += dz * push; }
    }
    if (!m.dying && e.mon.state !== "dash" && !(P.mods && P.mods.phase) && P.ghost <= 0) {
      const dx = m.pos.x - P.pos.x, dz = m.pos.z - P.pos.z, d = Math.hypot(dx, dz), min = m.r + R;
      if (d < min && d > 1e-4 && Math.abs(m.pos.y - P.pos.y) < 1.5) { const push = (min - d) / d; m.pos.x += dx * push; m.pos.z += dz * push; }
    }
    moveBodyAxis(m, 0, m.vel.x * dt); moveBodyAxis(m, 2, m.vel.z * dt);
    m.pos.y += m.vel.y * dt;
    let landed = false;
    for (const b of colliders) { if (!bodyHits(b, m, m.pos.x, m.pos.y, m.pos.z)) continue; if (m.vel.y <= 0) { m.pos.y = b.max[1]; landed = true; } else { m.pos.y = b.min[1] - m.h - 1e-4; m.vel.y = 0; } }
    if (m.pos.y <= 0) { m.pos.y = 0; landed = true; }
    if (landed) {
      if (m.airborne && m.vel.y < -6) { puff(m.pos, 6, 1.4); noiseHit(240, 1, 0.12, 0.18); m.vel.y *= -0.25; m.vel.x *= 0.6; m.vel.z *= 0.6; }
      else { if (m.airborne) { m.airborne = false; m.tumble = 0; } m.vel.y = 0; }
      m.grounded = true;
    } else m.grounded = false;
    if (m.pos.y < -10) m.pos.set(0, 0, 20);
    sim.setTransform(m.id, m.pos, null);
  }
}

// ============================================================================
// Réactions aux événements de la Sim
// ============================================================================
function monsterTop(id) { const m = MONSTERS.get(id); return m ? m.pos.clone().add(new THREE.Vector3(0, m.top, 0)) : new THREE.Vector3(); }
function monsterOnDamage(ev) {
  const m = MONSTERS.get(ev.target); if (!m) return;
  m.damaged = true; m.hitFlash = 1;
  const dir = ev.direction || new THREE.Vector3(1, 0, 0), heavy = m.def.heavy ? 0.45 : 1;
  if (ev.pull) { m.vel.x = dir.x * ev.pull * 5; m.vel.z = dir.z * ev.pull * 5; m.vel.y = 2.5; m.knockT = 0.35; m.grounded = false; }
  else if (ev.launch) { m.vel.set(dir.x * ev.launch[0] * 0.7 * heavy, ev.launch[1] * (m.def.heavy ? 0.6 : 1), dir.z * ev.launch[0] * 0.7 * heavy); m.airborne = true; m.grounded = false; m.knockT = 0.5; m.tumble = 1; }
  else { m.vel.x += dir.x * (ev.push || 1) * 2.0 * heavy; m.vel.z += dir.z * (ev.push || 1) * 2.0 * heavy; m.knockT = Math.max(m.knockT, 0.18); }
  // réaction du corps
  m.twitch.z += (Math.random() < 0.5 ? -1 : 1) * 0.35; m.twitch.x -= 0.3;
  if (m.rig) startAction(m.rig, "c_hurt");
  if (m.kind === "gresillon") noiseHit(3800, 2, 0.06, 0.1);
}
function monsterDied(ev) {
  const m = MONSTERS.get(ev.id); if (!m) return;
  m.dying = true; m.deadT = 0; m.windup = null;
  if (m.rig) { startAction(m.rig, "c_die"); }
  PROGRESS.kills[m.kind] = (PROGRESS.kills[m.kind] || 0) + 1; saveProgress();
  floatText(m.def.name + " vaincu", monsterTop(m.id), "float big");
  burst(m.pos.clone().add(new THREE.Vector3(0, m.top * 0.5, 0)), m.night ? [0xff3a2a, 0x2a0a10, 0xffffff] : [0xffffff, 0xffe28a, 0x9fe6ff], 3, 16);
  tone(m.def.heavy ? 160 : 400, 40, 0.6, 0.12, "sawtooth"); noiseHit(1200, 0.6, 0.4, 0.15);
  if (typeof npcOnKill === "function") npcOnKill(m.kind);
}
function monsterEvent(ev) {
  const m = MONSTERS.get(ev.id); if (!m) return;
  const near = m.pos.distanceTo(P.pos) < 30;
  if (ev.type === "enemy_windup") {
    m.windup = { attack: ev.attack, kind: ev.kind, t: 0, dur: ev.windup, armor: ev.armor }; m.strike = null;
    flashGlint(m, ev.armor);
    if (m.rig) startAction(m.rig, ev.attack === "fouet" ? "c_fouet" : ev.attack === "griffe" ? "c_griffe" : "c_zap", ev.windup);
    if (near) {
      if (m.kind === "gresillon") tone(2400, 3200, ev.windup, 0.03, "square");
      else if (m.kind === "gueule") { if (ev.attack === "cri") { tone(60, 140, ev.windup, 0.12, "sawtooth"); noiseHit(300, 0.5, ev.windup, 0.08); } else tone(90, 70, ev.windup, 0.1, "sawtooth"); }
      else if (m.kind === "ombre_sub") tone(40, 70, ev.windup, 0.2, "sine");
      else tone(900, 1400, ev.windup * 0.8, 0.03, "triangle");
    }
  } else if (ev.type === "enemy_attack") {
    m.windup = null; m.strike = { attack: ev.attack, kind: ev.kind, t: 0 };
    const e = sim.entities.get(m.id), f = e ? e.facing : new THREE.Vector3(0, 0, -1);
    if (ev.attack === "fouet" && m.whip) m.whip.kick(f.clone().multiplyScalar(30).add(new THREE.Vector3(0, -6, 0)));
    if (ev.kind === "aoe") { pixelRing(m.pos, ev.radius, 0.45, 0x9a4dff); pixelRing(m.pos, ev.radius * 0.6, 0.35, 0x2a0a40); if (near) { tone(55, 30, 0.5, 0.35, "sine"); noiseHit(120, 0.7, 0.4, 0.3, "lowpass"); } }
    if (ev.kind === "cone") {
      for (let i = 1; i <= 4; i++) setTimeout(() => { if (!m.dying) pixelRing(m.pos.clone().addScaledVector(f, i * 1.8), 0.6 + i * 0.45, 0.35, 0xff3a2a, 1.4); }, i * 60);
      if (near) { tone(1200, 200, 0.7, 0.1, "sawtooth"); tone(1500, 260, 0.7, 0.08, "square"); noiseHit(2500, 0.4, 0.7, 0.18); }
    }
    if (ev.kind === "melee" && near) noiseHit(m.kind === "gresillon" ? 3000 : 900, 0.8, 0.12, 0.12);
    if (ev.kind === "projectile" && near) { if (m.kind === "ombre_sub") tone(80, 35, 0.4, 0.25, "sine"); else { noiseHit(5000, 2, 0.08, 0.1); tone(2000, 600, 0.1, 0.04, "square"); } }
    if (ev.kind === "lunge" && near) noiseHit(m.kind === "gueule" ? 400 : 2500, 0.6, 0.2, 0.15);
    if (ev.kind === "blink") { puff(m.pos.clone().add(new THREE.Vector3(0, 1.2, 0)), 8, 1, 0x1a1024); }
  } else if (ev.type === "enemy_interrupt") { m.windup = null; }
  else if (ev.type === "enemy_blink") {
    // réapparaît près de sa cible, sur un endroit libre
    const to = ev.to.clone(); to.y = 0;
    const tries = [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1], [2, 2], [-2, -2]];
    for (const [dx, dz] of tries) { const q = new THREE.Vector3(to.x + dx, 0, to.z + dz); q.y = dummyGround(q.x, q.z, 3); if (bodyFree(m, q.x, q.y + 0.05, q.z)) { m.pos.copy(q); break; } }
    m.vel.set(0, 0, 0); m.blinkIn = 0.35; puff(m.pos.clone().add(new THREE.Vector3(0, 1.2, 0)), 8, 1, 0x1a1024); pixelRing(m.pos, 1.6, 0.4, 0x6a2dff);
    for (const r of m.ropes) r.reset(m.pos.clone().add(new THREE.Vector3(0, 1.2, 0)));
  }
}
// Éclat au-dessus de la tête quand il prépare un coup : jaune = on peut l'interrompre, rouge = non
const glintTex = canvasTex(64, 64, (g) => { g.fillStyle = "#fff"; g.beginPath(); for (let i = 0; i < 8; i++) { const a = (i / 8) * Math.PI * 2, r = i % 2 ? 9 : 31; g.lineTo(32 + Math.cos(a) * r, 32 + Math.sin(a) * r); } g.fill(); }, false);
const glints = [0, 1, 2, 3].map(() => { const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: glintTex, transparent: true, depthWrite: false, depthTest: false, toneMapped: false })); s.visible = false; scene.add(s); return { s, life: 0, m: null }; });
let glintIndex = 0;
function flashGlint(m, armor) { const g = glints[glintIndex++ % glints.length]; g.life = 0.35; g.m = m; g.s.visible = true; g.s.material.color.setHex(armor ? 0xff3a2a : 0xffe28a); }

// ============================================================================
// Mise à jour visuelle (à chaque image)
// ============================================================================
function updateMonsters(dt) {
  for (const m of [...MONSTERS.values()]) {
    const e = sim.entities.get(m.id); if (!e) continue;
    m.t += dt;
    if (m.windup) m.windup.t += dt;
    if (m.strike) { m.strike.t += dt; if (m.strike.t > 1.2) m.strike = null; }
    if (m.dying) m.deadT += dt;
    // orientation : suit la Sim, lentement pendant l'annonce (on voit où il vise)
    const f = e.facing, want = Math.atan2(-f.x, -f.z);
    if (!m.dying) m.yaw = lerpAngle(m.yaw, want, 1 - Math.exp(-(m.windup ? 5 : 9) * dt));
    // sursauts nerveux (surtout la nuit)
    if (m.t > m.twitchAt && !m.dying) { m.twitchAt = m.t + (m.night ? 0.6 : 2) + Math.random() * 2.5; m.twitch.set((Math.random() - 0.5) * 0.25, (Math.random() - 0.5) * (m.night ? 0.6 : 0.2), (Math.random() - 0.5) * (m.night ? 0.6 : 0.25)); }
    m.twitch.multiplyScalar(Math.exp(-3 * dt));
    m.spawnT = Math.min(1, m.spawnT + dt / 0.5);
    m.root.position.copy(m.pos); m.root.rotation.y = m.yaw;
    m.root.rotation.x = m.airborne ? m.root.rotation.x - dt * 7 * m.tumble : m.root.rotation.x * Math.exp(-10 * dt);
    const s = easeBack(m.spawnT) * (m.dying && m.deadT > 1.1 ? Math.max(0.01, 1 - (m.deadT - 1.1) / 0.4) : 1);
    m.root.scale.setScalar(s);
    m.root.updateMatrixWorld(true);
    if (m.rig && m.dying) { m.rig.pose.position.y += (-0.35 - m.rig.pose.position.y) * Math.min(1, dt * 3); }
    m.animate(m, e, dt);
    if (m.dying) for (const x of m.extra) x.scale.multiplyScalar(m.deadT > 1.1 ? 0.9 : 1);
    // flash blanc quand il est touché, yeux qui s'allument pendant l'annonce
    m.hitFlash = Math.max(0, m.hitFlash - dt * 6);
    for (const mat of m.flashMats) { if (m.hitFlash > 0) { mat.emissive.setRGB(1, 1, 1); mat.emissiveIntensity = m.hitFlash * 0.9; } else { mat.emissive.copy(mat.userData.em); mat.emissiveIntensity = mat.userData.ei; } }
    const eyeBoost = m.windup ? 1.6 + Math.sin(m.t * 40) * 0.4 : 1, base = m.night ? new THREE.Color(0xff2a1a) : new THREE.Color(m.kind === "cable" ? 0x7ae8ff : m.kind === "gresillon" ? 0xffb35c : 0xff2a1a);
    for (const eye of m.eyes) eye.color.copy(base).multiplyScalar(m.dying ? Math.max(0, 1 - m.deadT) : eyeBoost);
    // étoiles d'étourdissement
    const stunned = e.statuses.has("stun");
    if (stunned && !m.stars) { m.stars = new THREE.Group(); for (let i = 0; i < 3; i++) { const st = new THREE.Mesh(new THREE.OctahedronGeometry(0.1), glowM(0xffe28a)); m.stars.add(st); } scene.add(m.stars); }
    if (m.stars) { m.stars.visible = stunned; if (stunned) { m.stars.position.copy(m.pos).add(new THREE.Vector3(0, m.top + 0.1, 0)); m.stars.children.forEach((st, i) => { const a = m.t * 5 + (i * Math.PI * 2) / 3; st.position.set(Math.cos(a) * 0.5, 0, Math.sin(a) * 0.5); st.rotation.y += dt * 6; }); } }
    if (e.statuses.has("reverb_loop") && Math.floor(m.t / 0.9) !== Math.floor((m.t - dt) / 0.9)) pixelRing(m.pos, 1.8, 0.7, 0x9a7bff, 1.2);
    if (m.dying && m.deadT > 1.5) { pixelBurst(m.pos.clone().add(new THREE.Vector3(0, m.top * 0.4, 0))); dropNote(m.pos); removeMonster(m); }
  }
  for (const g of glints) {
    if (g.life <= 0) continue;
    g.life -= dt; g.s.visible = g.life > 0 && g.m && MONSTERS.has(g.m.id);
    if (g.s.visible) { g.s.position.copy(g.m.pos).add(new THREE.Vector3(0, g.m.top + 0.35, 0)); const k = 1 - g.life / 0.35; g.s.scale.setScalar(0.3 + Math.sin(k * Math.PI) * 0.6); g.s.material.rotation = k * 2; }
  }
  updateNotes(dt);
  updateSpawner(dt);
}

// --- Notes de musique lâchées par les monstres : elles soignent -------------------------------
const noteTex = canvasTex(64, 64, (g) => { g.fillStyle = "#8ff0b0"; g.font = "bold 52px serif"; g.textAlign = "center"; g.textBaseline = "middle"; g.shadowColor = "#0c2a1a"; g.shadowBlur = 6; g.fillText("♪", 32, 34); }, false);
const notes = [];
function dropNote(pos) {
  const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: noteTex, transparent: true, depthWrite: false, toneMapped: false }));
  s.scale.setScalar(0.6); s.position.copy(pos).add(new THREE.Vector3(0, 0.8, 0)); scene.add(s); notes.push({ s, t: 0, base: pos.clone() });
}
function updateNotes(dt) {
  for (let i = notes.length - 1; i >= 0; i--) {
    const n = notes[i]; n.t += dt; n.s.position.y = n.base.y + 0.8 + Math.sin(n.t * 3) * 0.12;
    const d = Math.hypot(P.pos.x - n.base.x, P.pos.z - n.base.z);
    if (d < 1.3 && Math.abs(P.pos.y - n.base.y) < 2) { const pe = sim.entities.get(P.id); sim.heal(pe, 10); tone(784, 1046, 0.12, 0.05, "triangle"); scene.remove(n.s); notes.splice(i, 1); continue; }
    if (n.t > 40) { scene.remove(n.s); notes.splice(i, 1); }
  }
}

// ============================================================================
// Apparitions : le jour, Grésillons et Câblés ; la nuit, les vrais monstres sortent.
// Ils apparaissent hors de vue, dans les rues, jamais trop près du joueur.
// ============================================================================
const SPAWN_ZONES = [[-4, 4, -72, -12], [-4, 4, 12, 72], [-72, -12, -3, 3], [12, 72, -3, 3], [-38, -26, 20, 38]];
const SPAWNER = { timer: 14, on: true, wasNight: false };
function pickSpawn(kind) {
  const r = MONSTER_SIZE[kind].r;
  for (let k = 0; k < 20; k++) {
    const z = SPAWN_ZONES[Math.floor(Math.random() * SPAWN_ZONES.length)], p = new THREE.Vector3(z[0] + Math.random() * (z[1] - z[0]), 0, z[2] + Math.random() * (z[3] - z[2]));
    const d = p.distanceTo(P.pos); if (d < 22 || d > 60) continue;
    p.y = dummyGround(p.x, p.z, 1);
    if (bodyFree({ r, h: 1.5 }, p.x, p.y + 0.05, p.z)) return p;
  }
  return null;
}
function updateSpawner(dt) {
  const night = isNight();
  // à l'aube, les monstres de la nuit se dissolvent (sauf en plein combat)
  if (SPAWNER.wasNight && !night) for (const m of MONSTERS.values()) if (m.def.night && !m.dying) { const e = sim.entities.get(m.id); if (!(e.combatUntil > sim.time)) { e.alive = false; m.dying = true; m.deadT = 0.9; } }
  SPAWNER.wasNight = night;
  SPAWNER.timer -= dt; if (SPAWNER.timer > 0 || !SPAWNER.on || overlay.hidden === false) return;
  SPAWNER.timer = 4;
  // trop loin : on retire
  for (const m of [...MONSTERS.values()]) { const e = sim.entities.get(m.id); if (!m.dying && m.pos.distanceTo(P.pos) > 85 && !(e.combatUntil > sim.time)) removeMonster(m); }
  const alive = [...MONSTERS.values()].filter((m) => !m.dying).length, want = night ? 6 : 3;
  if (alive >= want) return;
  const table = night ? [["gueule", 2], ["ombre_sub", 2], ["gresillon", 2], ["cable", 1]] : [["gresillon", 3], ["cable", 2]];
  let total = table.reduce((a, b) => a + b[1], 0), roll = Math.random() * total, kind = table[0][0];
  for (const [k, w] of table) { roll -= w; if (roll <= 0) { kind = k; break; } }
  const p = pickSpawn(kind); if (!p) return;
  spawnMonster(kind, p);
  // les Grésillons arrivent souvent par deux
  if (kind === "gresillon" && Math.random() < 0.6) { const q = p.clone().add(new THREE.Vector3(1.2, 0, 0.8)); if (bodyFree({ r: 0.4, h: 1 }, q.x, q.y + 0.05, q.z)) spawnMonster("gresillon", q); }
}

// --- Barres de vie au-dessus des monstres --------------------------------------------------
function updateMonsterBars() {
  for (const m of MONSTERS.values()) {
    const e = sim.entities.get(m.id); if (!e) continue;
    const show = !m.dying && (m.damaged || e.combatUntil > sim.time) && m.pos.distanceTo(P.pos) < 30;
    if (show && !m.bar) { m.bar = document.createElement("div"); m.bar.className = "hud mbar" + (m.def.night ? " night" : ""); m.bar.innerHTML = `<span>${m.def.name}</span><div class="bar"><i></i></div>`; document.body.appendChild(m.bar); }
    if (!m.bar) continue;
    const p = project(m.pos.clone().add(new THREE.Vector3(0, m.top + 0.25, 0)));
    m.bar.hidden = !show || p.behind;
    if (!m.bar.hidden) { m.bar.style.transform = `translate(${p.x - 45}px, ${p.y - 18}px)`; m.bar.querySelector("i").style.width = (100 * e.health) / e.maxHealth + "%"; }
  }
}
