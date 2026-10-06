"use strict";
// ============================================================================
// Armes : modèles 3D tenus en main (aucune arme à feu).
// Chaque arme décrit ce que tient la main droite (R) et/ou gauche (L).
// Repère de la main : l'origine est la poigne, l'objet part vers -z (devant le
// poing quand le bras pend), -y prolonge l'avant-bras.
// Les coups (dégâts, portée, enchaînements) sont dans data.js (DATA.weapons).
// ============================================================================
const WEAPON_MATS = [];
function weaponMat(color) { const m = new THREE.MeshMatcapMaterial({ matcap: MATCAP, color }); m.userData.base = new THREE.Color(color); WEAPON_MATS.push(m); return m; }
const WM = {
  wood: weaponMat(0xe0a868), tip: weaponMat(0xfff1d6), steel: weaponMat(0x9aa3b4), dark: weaponMat(0x3a3a44), black: weaponMat(0x232228),
  red: weaponMat(0xe0453a), white: weaponMat(0xf4efe8), gold: weaponMat(0xb8963a), violet: weaponMat(0xb9a6ff), blade: weaponMat(0x5a5468),
  // lueurs : pas assombries la nuit (elles brillent dans le noir)
  glow: new THREE.MeshBasicMaterial({ color: 0xd07bff, toneMapped: false }), glowBlue: new THREE.MeshBasicMaterial({ color: 0x8fe6ff, toneMapped: false }), redglow: new THREE.MeshBasicMaterial({ color: 0xff3a2a, toneMapped: false }),
};
const AX = -Math.PI / 2; // couche un cylindre (axe y) le long de -z
const WEAPON_MODELS = {
  fists: null,
  // deux baguettes de batterie, une par main
  // (g.rotation.x incline l'objet dans le poing, comme un poignet cassé vers l'avant)
  sticks: Object.fromEntries(["R", "L"].map((side) => [side, (add, g) => {
    g.rotation.x = -0.8;
    add(new THREE.CylinderGeometry(0.012, 0.019, 0.46, 8), WM.wood, 0, 0, -0.16, AX);
    add(new THREE.SphereGeometry(0.024, 8, 6), WM.tip, 0, 0, -0.39, 0, 0, 0, 1, 1, 1.4);
  }])),
  // pied de micro : bâton tenu au tiers, micro et pince d'un côté, trépied de l'autre
  mic_stand: {
    R: (add, g) => {
      g.rotation.x = -0.8;
      add(new THREE.CylinderGeometry(0.02, 0.02, 1.8, 8), WM.steel, 0, 0, -0.28, AX);
      add(new THREE.CylinderGeometry(0.03, 0.03, 0.12, 8), WM.black, 0, 0, 0.06, AX);
      add(new THREE.CylinderGeometry(0.035, 0.022, 0.2, 10), WM.black, 0, 0.02, -1.24, AX);
      add(new THREE.SphereGeometry(0.06, 12, 8), WM.dark, 0, 0.02, -1.36);
      add(new THREE.SphereGeometry(0.05, 10, 8), WM.steel, 0, 0.02, -1.38, 0, 0, 0, 1.05, 1.05, 1.05);
      for (let i = 0; i < 3; i++) { const a = (i / 3) * Math.PI * 2; add(new THREE.CylinderGeometry(0.012, 0.012, 0.32, 6), WM.steel, Math.cos(a) * 0.08, Math.sin(a) * 0.08, 0.72, AX + 0.4 * Math.sin(a), 0, 0.4 * Math.cos(a)); }
    },
  },
  // guitare-hache : le manche sert de poignée, le corps en V fait la lame
  guitar: {
    R: (add, g) => {
      g.rotation.x = -0.8;
      add(new THREE.BoxGeometry(0.055, 0.03, 0.8), WM.wood, 0, 0, -0.3);
      add(new THREE.BoxGeometry(0.09, 0.025, 0.16), WM.black, 0, 0, 0.16);
      for (let i = 0; i < 3; i++) add(new THREE.CylinderGeometry(0.01, 0.01, 0.05, 6), WM.steel, (i - 1) * 0.03, 0.025, 0.12 + i * 0.03);
      add(new THREE.BoxGeometry(0.42, 0.06, 0.34), WM.red, 0.12, 0, -0.82, 0, 0.45, 0);
      add(new THREE.BoxGeometry(0.42, 0.06, 0.34), WM.red, -0.12, 0, -0.82, 0, -0.45, 0);
      add(new THREE.BoxGeometry(0.18, 0.065, 0.22), WM.white, 0, 0, -0.74);
      add(new THREE.BoxGeometry(0.62, 0.07, 0.035), WM.steel, 0, 0, -1.02);
      add(new THREE.BoxGeometry(0.1, 0.075, 0.03), WM.black, 0, 0, -0.66);
    },
  },
  // Twisted Sword : poignée noire, garde d'or sombre, lame torsadée (ruban qui tourne) et cœur lumineux
  twisted_sword: {
    R: (add, g) => {
      g.rotation.x = -0.8;
      add(new THREE.CylinderGeometry(0.022, 0.026, 0.24, 8), WM.black, 0, 0, 0.02, AX);
      add(new THREE.SphereGeometry(0.035, 8, 6), WM.gold, 0, 0, 0.16);
      add(new THREE.BoxGeometry(0.26, 0.03, 0.04), WM.gold, 0, 0, -0.11, 0, 0, 0);
      for (let i = 0; i < 16; i++) { const z = -0.15 - i * 0.07; add(new THREE.BoxGeometry(0.075 - i * 0.003, 0.012, 0.085), WM.blade, 0, 0, z, 0, 0, i * 0.42); }
      add(new THREE.ConeGeometry(0.03, 0.14, 4), WM.blade, 0, 0, -1.33, AX);
      add(new THREE.CylinderGeometry(0.009, 0.009, 1.12, 5), WM.glow, 0, 0, -0.72, AX);
    },
  },
  // Boom Box : tenue par sa poignée, deux haut-parleurs, une lumière rouge
  boombox: {
    R: (add, g) => {
      g.rotation.x = -0.8;
      add(new THREE.BoxGeometry(0.05, 0.05, 0.3), WM.steel, 0, 0, -0.1);
      add(new THREE.BoxGeometry(0.62, 0.34, 0.2), WM.black, 0, -0.02, -0.42, 0, 0, 0);
      for (const x of [-0.18, 0.18]) { add(new THREE.CylinderGeometry(0.12, 0.12, 0.03, 18), WM.dark, x, -0.02, -0.31, Math.PI / 2); add(new THREE.CylinderGeometry(0.05, 0.05, 0.035, 12), WM.steel, x, -0.02, -0.305, Math.PI / 2); }
      add(new THREE.BoxGeometry(0.16, 0.08, 0.02), WM.steel, 0, 0.1, -0.31);
      add(new THREE.BoxGeometry(0.08, 0.03, 0.02), WM.redglow, 0, -0.14, -0.31);
      add(new THREE.CylinderGeometry(0.006, 0.006, 0.4, 4), WM.steel, 0.27, 0.32, -0.5, 0.3, 0, -0.3);
    },
  },
  // Faux à cordes : long manche, lame courbe faite de segments, cordes tendues
  scythe: {
    R: (add, g) => {
      g.rotation.x = -0.8;
      add(new THREE.CylinderGeometry(0.022, 0.024, 1.95, 7), WM.wood, 0, 0, -0.45, AX);
      for (let i = 0; i < 9; i++) { const a = (i / 8) * 1.5; add(new THREE.BoxGeometry(0.16 - i * 0.012, 0.018, 0.12), WM.blade, Math.sin(a) * 0.55, 0, -1.4 + (1 - Math.cos(a)) * 0.3, 0, -a, 0); }
      for (let i = 0; i < 3; i++) add(new THREE.CylinderGeometry(0.003, 0.003, 0.62, 3), WM.gold, 0.18 + i * 0.05, 0.02, -1.22, 0, 0, Math.PI / 2 - 0.25);
      add(new THREE.CylinderGeometry(0.008, 0.008, 0.6, 4), WM.glow, 0.3, 0, -1.47, 0, 0, Math.PI / 2 - 0.1);
    },
  },
  // Diapason : longue tige d'acier, fourche à deux branches qui vibrent
  fork: {
    R: (add, g) => {
      g.rotation.x = -0.8;
      add(new THREE.CylinderGeometry(0.018, 0.018, 1.7, 7), WM.steel, 0, 0, -0.45, AX);
      add(new THREE.SphereGeometry(0.035, 8, 6), WM.steel, 0, 0, 0.4);
      add(new THREE.BoxGeometry(0.18, 0.03, 0.04), WM.steel, 0, 0, -1.3);
      for (const x of [-0.075, 0.075]) { add(new THREE.BoxGeometry(0.03, 0.03, 0.5), WM.steel, x, 0, -1.55); add(new THREE.SphereGeometry(0.025, 6, 5), WM.glowBlue, x, 0, -1.81); }
    },
  },
  // Poings sub : gros gants-enceintes autour des poings
  sub_fists: Object.fromEntries(["R", "L"].map((side) => [side, (add) => {
    add(new THREE.CylinderGeometry(0.11, 0.095, 0.26, 14), WM.black, 0, -0.02, 0);
    add(new THREE.CylinderGeometry(0.09, 0.05, 0.05, 14, 1, true), WM.dark, 0, -0.16, 0);
    add(new THREE.TorusGeometry(0.1, 0.014, 6, 16), WM.redglow, 0, -0.14, 0, Math.PI / 2);
    add(new THREE.TorusGeometry(0.112, 0.01, 6, 16), WM.steel, 0, 0.06, 0, Math.PI / 2);
  }])),
  // micro-fléau : la poignée est le micro ; la tête au bout du câble est animée à part
  flail: {
    R: (add) => {
      add(new THREE.CylinderGeometry(0.03, 0.022, 0.24, 10), WM.black, 0, 0, -0.08, AX);
      add(new THREE.CylinderGeometry(0.034, 0.034, 0.03, 10), WM.gold, 0, 0, -0.21, AX);
    },
  },
};

// --- Le câble et la tête du micro-fléau --------------------------------------------
// La tête suit la main comme un pendule ; pendant un coup, elle est lancée vers la
// cible puis revient. Rendue normalement (pas dans la passe pixel : elle va trop loin).
const FLAIL = { on: false, pos: new THREE.Vector3(), vel: new THREE.Vector3(), throwT: 0, throwDur: 0, throwFrom: new THREE.Vector3(), throwTo: new THREE.Vector3() };
const flailHead = new THREE.Group();
{ const grille = new THREE.Mesh(new THREE.SphereGeometry(0.11, 14, 10), new THREE.MeshStandardMaterial({ color: 0xb9bfcc, metalness: 0.8, roughness: 0.35 }));
  const ring = new THREE.Mesh(new THREE.TorusGeometry(0.11, 0.018, 6, 20), new THREE.MeshStandardMaterial({ color: 0xf3cf6d, metalness: 0.9, roughness: 0.3 }));
  const glow = new THREE.Mesh(new THREE.SphereGeometry(0.16, 12, 8), new THREE.MeshBasicMaterial({ color: 0xb9a6ff, transparent: true, opacity: 0.0, depthWrite: false, toneMapped: false }));
  flailHead.add(grille, ring, glow); flailHead.userData.glow = glow; }
flailHead.visible = false; scene.add(flailHead);
const FLAIL_SEGS = 14;
const flailCable = new THREE.Line(new THREE.BufferGeometry().setFromPoints(Array.from({ length: FLAIL_SEGS + 1 }, () => new THREE.Vector3())), new THREE.LineBasicMaterial({ color: 0x1b1a20 }));
flailCable.frustumCulled = false; flailCable.visible = false; scene.add(flailCable);
function setFlailVisible(on) { FLAIL.on = on; flailHead.visible = flailCable.visible = on; if (on) FLAIL.pos.copy(model.handR.getWorldPosition(new THREE.Vector3())).add(new THREE.Vector3(0, -0.6, 0)); }
// lance la tête vers un point (coups du fléau)
function throwFlail(to, dur) { FLAIL.throwFrom.copy(FLAIL.pos); FLAIL.throwTo.copy(to); FLAIL.throwT = 0; FLAIL.throwDur = dur; }
function updateFlail(dt) {
  if (!FLAIL.on) return;
  const hand = model.handR.getWorldPosition(new THREE.Vector3());
  if (FLAIL.throwT < FLAIL.throwDur * 2) {
    // aller (rapide) puis retour (plus lent)
    FLAIL.throwT += dt; const f = FLAIL.throwT / FLAIL.throwDur;
    const out = f < 1 ? easeOut(f) : 1 - easeInOut(Math.min(1, f - 1));
    const prev = FLAIL.pos.clone();
    FLAIL.pos.copy(hand).lerp(FLAIL.throwTo, out).add(new THREE.Vector3(0, Math.sin(Math.min(1, f) * Math.PI) * 0.4, 0));
    FLAIL.vel.copy(FLAIL.pos).sub(prev).divideScalar(Math.max(dt, 1e-4));
    flailHead.userData.glow.material.opacity = f < 1.2 ? 0.45 : 0;
  } else {
    // pendule amorti, longueur de câble ~0,75 m
    FLAIL.vel.y -= 18 * dt; FLAIL.vel.multiplyScalar(Math.exp(-2.2 * dt));
    FLAIL.pos.addScaledVector(FLAIL.vel, dt);
    const d = FLAIL.pos.clone().sub(hand), len = d.length(), L = 0.75;
    if (len > L) { d.multiplyScalar(L / len); const np = hand.clone().add(d); FLAIL.vel.add(np.clone().sub(FLAIL.pos).divideScalar(Math.max(dt, 1e-4)).multiplyScalar(0.5)); FLAIL.pos.copy(np); }
    if (FLAIL.pos.y < P.pos.y + 0.08) { FLAIL.pos.y = P.pos.y + 0.08; FLAIL.vel.y = Math.abs(FLAIL.vel.y) * 0.3; }
    flailHead.userData.glow.material.opacity = 0;
  }
  flailHead.position.copy(FLAIL.pos); flailHead.lookAt(hand);
  // câble : courbe qui pend un peu entre la main et la tête
  const pts = flailCable.geometry.attributes.position, span = FLAIL.pos.distanceTo(hand), sag = Math.max(0, 0.75 - span) * 0.6 + 0.04;
  for (let i = 0; i <= FLAIL_SEGS; i++) {
    const t = i / FLAIL_SEGS, p = hand.clone().lerp(FLAIL.pos, t); p.y -= Math.sin(t * Math.PI) * sag;
    pts.setXYZ(i, p.x, p.y, p.z);
  }
  pts.needsUpdate = true;
}
// teinte des armes : comme le chrome du bonhomme (assombries la nuit)
function tintWeapons(c) { for (const m of WEAPON_MATS) m.color.copy(m.userData.base).multiply(c); }

// ============================================================================
// Autels : des armes posées dans la ville, qu'on ramasse en passant dessus.
// Pierre noire, bougies, l'arme flotte et tourne au-dessus.
// ============================================================================
const ALTARS = [
  { weapon: "boombox", x: -35, z: 26 },
  { weapon: "scythe", x: 15, z: -28.5 },
  { weapon: "fork", x: 15, z: -46, roof: true },
  { weapon: "sub_fists", x: 0, z: -73 },
];
const altarObjs = [];
function setupAltars() {
  const stone = new THREE.MeshStandardMaterial({ color: 0x2a2730, roughness: 0.9 }), wax = new THREE.MeshLambertMaterial({ color: 0xd8cfc0 });
  for (const a of ALTARS) {
    const y = dummyGround(a.x, a.z, a.roof ? 200 : 1), g = new THREE.Group(); g.position.set(a.x, y, a.z); scene.add(g);
    const m = (geo, mat, px, py, pz) => { const o = new THREE.Mesh(geo, mat); o.position.set(px, py, pz); o.castShadow = true; g.add(o); return o; };
    m(new THREE.BoxGeometry(1.3, 0.25, 1.3), stone, 0, 0.125, 0); m(new THREE.BoxGeometry(0.8, 0.75, 0.8), stone, 0, 0.62, 0); m(new THREE.BoxGeometry(1.0, 0.12, 1.0), stone, 0, 1.05, 0);
    const flames = [];
    for (const [cx, cz] of [[-0.42, -0.42], [0.42, -0.42], [-0.42, 0.42], [0.42, 0.42]]) {
      m(new THREE.CylinderGeometry(0.04, 0.045, 0.22, 8), wax, cx, 1.22, cz);
      const f = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, color: 0xffa040, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending })); f.position.set(cx, 1.4, cz); f.scale.setScalar(0.25); g.add(f); flames.push(f);
    }
    const halo = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, color: new THREE.Color(DATA.weapons[a.weapon].color), transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, opacity: 0.5 }));
    halo.position.y = 1.9; halo.scale.setScalar(2.2); g.add(halo);
    // l'arme qui flotte (même modèle que dans la main, rendu normalement)
    const w = new THREE.Group(); w.position.y = 1.9; g.add(w);
    for (const [side, build] of Object.entries(WEAPON_MODELS[a.weapon])) {
      const sub = new THREE.Group(); sub.position.x = side === "L" ? -0.25 : 0.15; w.add(sub);
      build((geo, mat, x, yy, z, rx = 0, ry = 0, rz = 0, sx = 1, sy = 1, sz = 1) => { const o = new THREE.Mesh(geo, mat); o.position.set(x, yy, z); o.rotation.set(rx, ry, rz); o.scale.set(sx, sy, sz); sub.add(o); return o; }, sub);
      sub.rotation.x = Math.PI / 2 - 0.2;
    }
    collider(a.x - 0.5, y, a.z - 0.5, a.x + 0.5, y + 1.1, a.z + 0.5, { noCam: true });
    altarObjs.push({ a, g, w, halo, flames, y, t: Math.random() * 5 });
  }
}
function updateAltars(dt) {
  const pe = sim.entities.get(P.id);
  for (const o of altarObjs) {
    o.t += dt; const owned = pe.weapons.has(o.a.weapon);
    o.w.visible = !owned; o.halo.visible = !owned;
    o.w.rotation.y += dt * 1.4; o.w.position.y = 1.9 + Math.sin(o.t * 2) * 0.1;
    o.flames.forEach((f, i) => f.scale.setScalar(0.22 + Math.sin(o.t * 17 + i * 3) * 0.04 + Math.random() * 0.03));
    if (!owned && Math.hypot(P.pos.x - o.a.x, P.pos.z - o.a.z) < 1.4 && Math.abs(P.pos.y - (o.y + 1.1)) < 2) {
      sim.queue({ type: "unlock", source: P.id, weapon: o.a.weapon });
      pixelRing(o.g.position, 3, 0.6, new THREE.Color(DATA.weapons[o.a.weapon].color).getHex()); groundImpact(o.g.position, 1, 0xc86bff);
    }
  }
}
