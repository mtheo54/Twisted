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
  red: weaponMat(0xe0453a), white: weaponMat(0xf4efe8), gold: weaponMat(0xf3cf6d), violet: weaponMat(0xb9a6ff),
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
