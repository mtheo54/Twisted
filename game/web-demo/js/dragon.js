"use strict";
// ============================================================================
// Le dragon de jade (boss final, préparé ici) : un long dragon serpentin
// turquoise aux détails dorés. Pour l'instant il ne se combat pas : la nuit,
// il sort de derrière la tour et tourne autour, haut dans le ciel.
// Son corps est une chaîne d'anneaux qui suivent la tête sur sa trajectoire.
// ============================================================================
// DS : échelle du dragon (la tour fait 333 m ; le dragon fait plus de 400 m de long)
const DS = 1.9;
const DRAGON = { built: false, t: 0, vis: 0, announced: false, SEG: 70, SPACING: 3.3 * DS };
function scaleTex() {
  return canvasTex(256, 256, (g) => {
    g.fillStyle = "#2fc9b0"; g.fillRect(0, 0, 256, 256);
    for (let row = 0; row < 9; row++) for (let col = 0; col < 9; col++) {
      const x = col * 32 + (row % 2 ? 16 : 0), y = row * 32;
      const grd = g.createRadialGradient(x, y + 8, 2, x, y + 8, 22); grd.addColorStop(0, "#7ff2dc"); grd.addColorStop(0.7, "#27b39c"); grd.addColorStop(1, "#0f6b5e");
      g.fillStyle = grd; g.beginPath(); g.arc(x, y, 18, 0, Math.PI); g.fill();
      g.strokeStyle = "rgba(243,207,109,0.55)"; g.lineWidth = 1.5; g.beginPath(); g.arc(x, y, 17, 0.15, Math.PI - 0.15); g.stroke();
    }
  });
}
function buildDragon() {
  const jade = new THREE.MeshStandardMaterial({ map: scaleTex(), color: 0xffffff, roughness: 0.35, metalness: 0.35, emissive: 0x0b5a4c, emissiveIntensity: 0.9, fog: false });
  const gold = new THREE.MeshStandardMaterial({ color: 0xf3cf6d, roughness: 0.25, metalness: 0.9, emissive: 0x5a3c00, emissiveIntensity: 0.9, fog: false });
  const belly = new THREE.MeshStandardMaterial({ color: 0xe8d29a, roughness: 0.5, metalness: 0.3, emissive: 0x3a2a08, emissiveIntensity: 0.8, fog: false });
  const mane = new THREE.MeshStandardMaterial({ color: 0x9ff7e4, roughness: 0.6, metalness: 0.1, emissive: 0x1f7a6a, emissiveIntensity: 0.9, fog: false, side: THREE.DoubleSide });
  const eyeM = new THREE.MeshBasicMaterial({ color: 0xffc23a, toneMapped: false, fog: false });
  const N = DRAGON.SEG;
  DRAGON.group = new THREE.Group(); scene.add(DRAGON.group);
  DRAGON.body = new THREE.InstancedMesh(new THREE.SphereGeometry(1, 18, 12), jade, N);
  DRAGON.belly = new THREE.InstancedMesh(new THREE.SphereGeometry(1, 12, 8), belly, N);
  const spineGeo = new THREE.ConeGeometry(1, 1, 5); spineGeo.translate(0, 0.5, 0);
  DRAGON.spines = new THREE.InstancedMesh(spineGeo, gold, N);
  DRAGON.mane = new THREE.InstancedMesh(new THREE.ConeGeometry(1, 1, 4), mane, 14);
  for (const im of [DRAGON.body, DRAGON.belly, DRAGON.spines, DRAGON.mane]) { im.frustumCulled = false; DRAGON.group.add(im); }
  // la tête (construite vers +z, puis orientée avec lookAt)
  const H = new THREE.Group(); DRAGON.head = H; DRAGON.group.add(H);
  const m = (geo, mat, x, y, z, rx = 0, ry = 0, rz = 0, sx = 1, sy = 1, sz = 1) => { const o = new THREE.Mesh(geo, mat); o.position.set(x, y, z); o.rotation.set(rx, ry, rz); o.scale.set(sx, sy, sz); H.add(o); return o; };
  m(new THREE.SphereGeometry(1, 20, 14), jade, 0, 0.4, 0, 0, 0, 0, 3.4, 2.8, 3.8);
  m(new THREE.BoxGeometry(1, 1, 1), jade, 0, 0.1, 4.6, 0.08, 0, 0, 2.6, 1.7, 5.2);
  m(new THREE.SphereGeometry(1, 14, 10), jade, 0, 0.2, 7.2, 0, 0, 0, 1.5, 1.0, 1.0);
  DRAGON.jaw = m(new THREE.BoxGeometry(1, 1, 1), belly, 0, -1.2, 3.8, 0.25, 0, 0, 2.2, 0.7, 5);
  for (const s of [-1, 1]) {
    m(new THREE.SphereGeometry(0.45, 12, 8), eyeM, s * 1.55, 1.3, 2.0);
    m(new THREE.ConeGeometry(0.45, 7, 7), gold, s * 1.2, 2.6, -1.6, -1.1, 0, s * -0.25);
    m(new THREE.ConeGeometry(0.25, 2.6, 6), gold, s * 1.9, 3.8, -2.4, -0.4, 0, s * -0.9);
    m(new THREE.ConeGeometry(0.22, 1.1, 6), new THREE.MeshBasicMaterial({ color: 0xf4efe8, fog: false }), s * 0.8, -0.65, 6.6, Math.PI, 0, 0);
    m(new THREE.SphereGeometry(0.25, 8, 6), new THREE.MeshBasicMaterial({ color: 0x0b2a24, fog: false }), s * 0.6, 0.9, 7.7);
  }
  for (let i = 0; i < 7; i++) { const a = -1.2 + (i / 6) * 2.4; m(new THREE.ConeGeometry(0.7, 4.5, 5), mane, Math.sin(a) * 2.6, 1.2 + Math.cos(a) * 1.6, -2.4, -1.9, 0, -a * 0.6); }
  H.scale.setScalar(DS);
  // moustaches de dragon : deux longs filaments dorés qui ondulent
  DRAGON.whiskers = [-1, 1].map((s) => { const pts = []; for (let i = 0; i < 16; i++) pts.push(new THREE.Vector3(s * (1.2 + i * 0.45 + Math.sin(i * 0.6) * 0.6), 0.1 - i * 0.25 + Math.sin(i * 0.8) * 0.5, 7.4 - i * 0.9)); const l = new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), new THREE.LineBasicMaterial({ color: 0xf3cf6d, fog: false })); H.add(l); l.userData.s = s; return l; });
  // quatre pattes courtes aux griffes d'or
  DRAGON.legs = [9, 9, 34, 34].map((seg, i) => {
    const g = new THREE.Group(), s = i % 2 ? 1 : -1;
    const thigh = new THREE.Mesh(new THREE.CylinderGeometry(0.9, 0.6, 5, 8), jade); thigh.position.y = -2.5; g.add(thigh);
    for (let k = -1; k <= 1; k++) { const c = new THREE.Mesh(new THREE.ConeGeometry(0.3, 1.8, 5), gold); c.position.set(k * 0.6, -5.4, 0.6); c.rotation.x = 1.9; g.add(c); }
    g.scale.setScalar(DS); DRAGON.group.add(g); return { g, seg, s };
  });
  DRAGON.group.visible = false; DRAGON.built = true;
}
// trajectoire autour de la tour : un grand cercle qui ondule en hauteur
function dragonPath(s, out) {
  const R = 140, a = s / R, r = R + 22 * Math.sin(a * 2);
  return out.set(TOWER_POS.x + Math.cos(a) * r, 235 + 50 * Math.sin(a * 1.5 + 0.7), TOWER_POS.z + Math.sin(a) * r);
}
const _dm = new THREE.Matrix4(), _dq = new THREE.Quaternion(), _ds = new THREE.Vector3(), _dp = new THREE.Vector3(), _dn = new THREE.Vector3(), _dz = new THREE.Vector3(0, 0, 1);
function updateDragon(dt) {
  if (!DRAGON.built) return;
  const want = isNight() ? 1 : 0;
  DRAGON.vis += (want - DRAGON.vis) * Math.min(1, dt * 0.35);
  DRAGON.group.visible = DRAGON.vis > 0.01;
  if (!DRAGON.group.visible) return;
  if (want && !DRAGON.announced && DRAGON.vis > 0.3) {
    DRAGON.announced = true; showBanner("Au loin, le dragon de jade s'est réveillé…", 3);
    tone(70, 40, 2.2, 0.12, "sawtooth"); tone(105, 60, 2.0, 0.08, "sawtooth"); noiseHit(300, 0.4, 1.8, 0.12, "lowpass");
  }
  if (!want) DRAGON.announced = false;
  DRAGON.t += dt;
  const t = DRAGON.t, N = DRAGON.SEG, head = t * 34, rise = (1 - DRAGON.vis) * -220; // il monte de derrière la tour
  const pts = [];
  for (let i = 0; i <= N; i++) {
    const p = dragonPath(head - i * DRAGON.SPACING, new THREE.Vector3());
    // ondulation du corps (de côté et en hauteur) qui court de la tête à la queue
    const nxt = dragonPath(head - i * DRAGON.SPACING + 1, new THREE.Vector3()), tan = nxt.sub(p).normalize(), side = new THREE.Vector3(-tan.z, 0, tan.x).normalize();
    p.addScaledVector(side, Math.sin(i * 0.26 - t * 2.1) * 5 * DS * Math.min(1, i / 6)); p.y += Math.sin(i * 0.19 - t * 1.6) * 4.5 * DS + rise;
    pts.push(p);
  }
  for (let i = 0; i < N; i++) {
    const a = pts[i], b = pts[i + 1], dir = _dn.copy(a).sub(b).normalize();
    const f = i / (N - 1), r = ((i < 6 ? 2.6 + i * 0.25 : 4.1 * (1 - 0.8 * Math.pow(f, 1.6))) + 0.5) * DS;
    _dq.setFromUnitVectors(_dz, dir);
    _dm.compose(a, _dq, _ds.set(r, r * 0.92, DRAGON.SPACING * 0.95)); DRAGON.body.setMatrixAt(i, _dm);
    const down = new THREE.Vector3(0, -1, 0).applyQuaternion(_dq), up = down.clone().negate();
    _dm.compose(_dp.copy(a).addScaledVector(down, r * 0.45), _dq, _ds.set(r * 0.72, r * 0.5, DRAGON.SPACING * 0.9)); DRAGON.belly.setMatrixAt(i, _dm);
    // épines dorées, inclinées vers la queue
    const sq = new THREE.Quaternion().setFromUnitVectors(UP, up.clone().addScaledVector(dir, -0.6).normalize());
    const sz = r * (i < 10 ? 0.65 : 0.5);
    _dm.compose(_dp.copy(a).addScaledVector(up, r * 0.8), sq, _ds.set(sz * 0.35, sz * 1.3, sz * 0.35)); DRAGON.spines.setMatrixAt(i, _dm);
    if (i < 14) { const ms = new THREE.Quaternion().setFromUnitVectors(UP, dir.clone().negate().addScaledVector(up, 0.4 + 0.3 * Math.sin(t * 3 + i)).normalize()); _dm.compose(_dp.copy(a).addScaledVector(up, r * 0.7).addScaledVector(new THREE.Vector3(-dir.z, 0, dir.x), (i % 2 ? 1 : -1) * r * 0.6), ms, _ds.set(0.9 * DS, 3.5 * DS, 0.9 * DS)); DRAGON.mane.setMatrixAt(i, _dm); }
  }
  for (const im of [DRAGON.body, DRAGON.belly, DRAGON.spines, DRAGON.mane]) im.instanceMatrix.needsUpdate = true;
  const H = DRAGON.head; H.position.copy(pts[0]); H.lookAt(pts[0].clone().add(pts[0].clone().sub(pts[1])));
  DRAGON.jaw.rotation.x = 0.25 + Math.max(0, Math.sin(t * 0.7)) * 0.3;
  DRAGON.whiskers.forEach((l) => { l.rotation.y = Math.sin(t * 1.3 + l.userData.s) * 0.15; l.rotation.x = Math.sin(t * 1.7) * 0.1; });
  for (const L of DRAGON.legs) {
    const a = pts[L.seg], b = pts[L.seg + 1], dir = a.clone().sub(b).normalize(), side = new THREE.Vector3(-dir.z, 0, dir.x).normalize().multiplyScalar(L.s);
    L.g.position.copy(a).addScaledVector(side, 3.2 * DS).add(new THREE.Vector3(0, -1.5 * DS, 0));
    L.g.lookAt(L.g.position.clone().add(dir)); L.g.rotateX(0.5 + Math.sin(t * 2.5 + L.seg + L.s) * 0.4);
  }
}

// ============================================================================
// Nuages : de grands amas doux qui dérivent, teintés selon l'heure
// ============================================================================
const cloudTex = canvasTex(256, 128, (g) => {
  for (let i = 0; i < 22; i++) {
    const x = 40 + Math.random() * 176, y = 50 + Math.random() * 40, r = 18 + Math.random() * 34;
    const grd = g.createRadialGradient(x, y, 0, x, y, r); grd.addColorStop(0, "rgba(255,255,255,0.55)"); grd.addColorStop(1, "rgba(255,255,255,0)");
    g.fillStyle = grd; g.beginPath(); g.arc(x, y, r, 0, Math.PI * 2); g.fill();
  }
}, false);
const clouds = [];
function setupClouds() {
  for (let i = 0; i < 18; i++) {
    const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: cloudTex, transparent: true, depthWrite: false, fog: false, opacity: 0.8 }));
    const a = Math.random() * Math.PI * 2, r = 350 + Math.random() * 900;
    s.position.set(Math.cos(a) * r, 230 + Math.random() * 170, Math.sin(a) * r); s.scale.set(380 + Math.random() * 300, 120 + Math.random() * 70, 1); s.renderOrder = -1;
    scene.add(s); clouds.push(s);
  }
}
function updateClouds(dt) {
  if (!clouds.length || !DN.palette) return;
  const pal = DN.palette, c = new THREE.Color(1, 1, 1).lerp(pal.horizon, 0.35).lerp(new THREE.Color(0x2a2440), DN.n * 0.85);
  for (const s of clouds) {
    s.position.x += dt * 3; if (s.position.x > 1300) s.position.x = -1300;
    s.material.color.copy(c); s.material.opacity = 0.75 - 0.35 * DN.n;
  }
}
