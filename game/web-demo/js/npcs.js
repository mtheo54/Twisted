"use strict";
// ============================================================================
// PNJ
//  - Passants : de petits bonshommes mats qui marchent sur les trottoirs,
//    s'arrêtent parfois, et s'enfuient quand un monstre approche. Moins
//    nombreux la nuit.
//  - Personnages importants : on leur parle avec F. Ils donnent des conseils,
//    des quêtes et des armes. Leurs dialogues sont des données (NPC_DEFS).
// ============================================================================
const NPC_MATS = {};
const npcMat = (c, extra) => (extra ? new THREE.MeshStandardMaterial(Object.assign({ color: c, roughness: 0.8, metalness: 0 }, extra)) : (NPC_MATS[c] = NPC_MATS[c] || new THREE.MeshLambertMaterial({ color: c })));
// Un bonhomme habillé : manteau, pantalon, peau (tête et mains)
function buildPerson(coat, pants, skin, scale = 1, shadows = true) {
  const rig = buildRig(npcMat(coat), 0, true);
  for (const side of ["R", "L"]) { rig.J["hip" + side].g.traverse((o) => { if (o.isMesh) o.material = npcMat(pants); }); rig["hand" + side].traverse((o) => { if (o.isMesh) o.material = npcMat(skin); }); }
  rig.head.material = npcMat(skin);
  for (const m of rig.meshes) m.castShadow = shadows;
  rig.root.scale.setScalar(scale);
  return rig;
}

// --- Passants ------------------------------------------------------------------------------
const SIDEWALKS = [[-6.4, -70, -6.4, -9], [6.4, -70, 6.4, -9], [-6.4, 9, -6.4, 70], [6.4, 9, 6.4, 70], [-70, -6.0, -9, -6.0], [9, -6.0, 70, -6.0], [-70, 6.3, -9, 6.3], [9, 6.3, 70, 6.3]];
const COATS = [0x3d4a6b, 0x7a3b3b, 0x4f6b4a, 0xb08d57, 0x2b2b30, 0x8a8f99, 0xc9b79c, 0x5b4a6b, 0x2f5f6e, 0xd9cfc0];
const PANTS = [0x2b2b30, 0x3a3a42, 0x3d4a6b, 0x5a5560, 0x6b5a4a];
const SKINS = [0xf0d8c0, 0xd9b391, 0xa8794f, 0xe8d6c8, 0x8a5e3c];
const peds = [];
function setupPedestrians() {
  for (let i = 0; i < 12; i++) {
    const rig = buildPerson(pick(COATS), pick(PANTS), pick(SKINS), range(0.9, 1.04), false);
    const hair = new THREE.Mesh(new THREE.SphereGeometry(0.16, 14, 8, 0, Math.PI * 2, 0, Math.PI * 0.55), npcMat(pick([0x1a1614, 0x3a2a20, 0x6b4a2a, 0xb9b0a8])));
    hair.position.set(0, 0.19, 0.01); rig.J.neck.g.add(hair);
    if (rand() < 0.35) { const bag = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.36, 0.14), npcMat(pick([0x2b2b30, 0x7a3b3b, 0x3d4a6b]))); bag.position.set(0, 0.12, 0.22); rig.J.chest.g.add(bag); }
    const path = SIDEWALKS[i % SIDEWALKS.length], a = new THREE.Vector3(path[0], SW, path[1]), b = new THREE.Vector3(path[2], SW, path[3]);
    const p = { rig, a, b, t: rand(), dir: rand() < 0.5 ? 1 : -1, speed: range(1.1, 1.5), pos: new THREE.Vector3(), yaw: 0, phase: rand() * 6, pause: 0, nextPause: range(6, 18), flee: 0, fleeDir: 1, night: true, shown: true }; // la nuit, tout le monde se barricade
    p.pos.lerpVectors(a, b, p.t); rig.root.position.copy(p.pos);
    peds.push(p);
  }
}
function updatePedestrians(dt) {
  const night = isNight();
  for (const p of peds) {
    // la nuit, la plupart rentrent chez eux (ils disparaissent hors de vue)
    const far = p.pos.distanceTo(P.pos) > 40;
    if (p.night && night && p.shown && far) { p.shown = false; p.rig.root.visible = false; }
    if (!(p.night && night) && !p.shown && far) { p.shown = true; p.rig.root.visible = true; }
    if (!p.shown) continue;
    // un monstre approche : on s'enfuit en courant, les bras en l'air
    let threat = null;
    for (const m of MONSTERS.values()) if (!m.dying && m.pos.distanceTo(p.pos) < 9) { threat = m; break; }
    if (threat) { const along = p.b.clone().sub(p.a).normalize(); p.fleeDir = threat.pos.clone().sub(p.pos).dot(along) > 0 ? -1 : 1; p.flee = 4; p.pause = 0; }
    p.flee = Math.max(0, p.flee - dt);
    const len = p.a.distanceTo(p.b);
    let speed = 0;
    if (p.flee > 0) { p.dir = p.fleeDir; speed = 3.8; }
    else if (p.pause > 0) p.pause -= dt;
    else { speed = p.speed; p.nextPause -= dt; if (p.nextPause <= 0) { p.nextPause = range(8, 22); p.pause = range(1.5, 4); } }
    p.t += (p.dir * speed * dt) / len;
    if (p.t > 1) { p.t = 1; p.dir = -1; } if (p.t < 0) { p.t = 0; p.dir = 1; }
    p.pos.lerpVectors(p.a, p.b, p.t);
    // on s'écarte du joueur
    const dx = p.pos.x - P.pos.x, dz = p.pos.z - P.pos.z, d = Math.hypot(dx, dz), side = new THREE.Vector3(-(p.b.z - p.a.z), 0, p.b.x - p.a.x).normalize();
    p.dodge = (p.dodge || 0) + ((d < 1.2 ? Math.sign(dx * side.x + dz * side.z || 1) * 0.6 : 0) - (p.dodge || 0)) * Math.min(1, dt * 4);
    const pos = p.pos.clone().addScaledVector(side, p.dodge);
    const fwd = p.b.clone().sub(p.a).multiplyScalar(p.dir);
    const wantYaw = speed > 0 ? Math.atan2(-fwd.x, -fwd.z) : d < 4 ? Math.atan2(-(P.pos.x - pos.x), -(P.pos.z - pos.z)) : p.yaw;
    p.yaw = lerpAngle(p.yaw, wantYaw, 1 - Math.exp(-6 * dt));
    p.rig.root.position.copy(pos); p.rig.root.rotation.y = p.yaw;
    if (d > 45) continue; // trop loin : pas besoin d'animer
    p.phase += dt * (4 + speed * 2);
    const s = Math.sin(p.phase), c = Math.cos(p.phase), run = Math.min(1, speed / 4), amp = speed > 0 ? 0.3 + 0.4 * run : 0, kb = speed > 0 ? 0.2 + 0.9 * run : 0;
    const b = {}, set = (j, x = 0, y = 0, z = 0) => { b[j] = { x, y, z }; };
    set("hipR", s * amp); set("hipL", -s * amp); set("kneeR", -kb * Math.max(0, c) - 0.05); set("kneeL", -kb * Math.max(0, -c) - 0.05);
    set("shoulderR", -s * amp * 0.8, 0, 0.1); set("shoulderL", s * amp * 0.8, 0, -0.1); set("elbowR", 0.25 + run); set("elbowL", 0.25 + run);
    set("waist", 0.05 + 0.15 * run, s * 0.1 * run); set("neck", 0, 0, 0);
    if (p.flee > 0) { set("shoulderR", 2.6, 0, 0.5); set("shoulderL", 2.6, 0, -0.5); set("elbowR", 0.6); set("elbowL", 0.6); }
    else if (speed === 0) { set("shoulderR", 1.1, 0, 0.1); set("elbowR", 1.9); set("neck", 0.35); } // il regarde son téléphone
    p.rig.body.position.y = speed > 0 ? Math.abs(c) * 0.03 * (1 + run) : 0;
    animateRig(p.rig, b, dt);
  }
}

// --- Personnages importants ---------------------------------------------------------------
// talk() renvoie { lines, onEnd, fresh } ; fresh = il a quelque chose de nouveau à dire.
const NPC_DEFS = [
  { id: "kenji", name: "Kenji", role: "batteur de rue", pos: [7.5, 10.2], face: Math.PI / 2, coat: 0x2b2b30, pants: 0x3d4a6b, skin: 0xd9b391,
    talk: () => {
      if (!hasWeapon("sticks")) return { fresh: true, lines: ["Hé, toi ! T'es un bonhomme d'abrasion, non ? Ça se voit au chrome.", "Le jour, ça va. Mais quand la nuit tombe, les monstres sortent : Grésillons, Câblés, Gueules-enceintes… Tout le monde s'enferme.", "Tiens, prends mes baguettes de rechange. Trois frappes rapides, puis un roulement qui envoie valser !", "X ou la molette pour changer d'arme. Tab pour voir tout ton attirail."], onEnd: () => unlockWeapon("sticks") };
      return { lines: pick([["Quand un monstre fait briller une étoile jaune au-dessus de sa tête, frappe-le : tu l'interromps.", "Une étoile rouge ? Il est lancé, rien ne l'arrête. Esquive avec C, ou place un Gater juste avant le coup."], ["Taro mixe sur la place du parc. Il a toujours besoin d'un coup de main.", "Et Yuna… elle ne chante que la nuit, devant le karaoké."], ["Frappe-les de près : leur sang te soigne. Et ne t'arrête jamais de bouger : dash, glissade, écrasement."], ["Il y a des autels dans la ville, avec des armes dessus. Cherche bien, même sur les toits."]]) };
    } },
  { id: "taro", name: "DJ Taro", role: "DJ du parc", pos: [-11.2, 21.5], face: -Math.PI / 2, coat: 0x5b4a6b, pants: 0x2b2b30, skin: 0xa8794f,
    talk: () => {
      const q = PROGRESS.quests.taro;
      if (hasWeapon("mic_stand")) return { lines: ["La nuit, les monstres sortent pour de vrai. Écoute bien : ils grondent avant de frapper.", "Et si tu croises une Gueule-enceinte… ne reste pas devant sa gueule quand elle crie."] };
      if (!q) return { fresh: true, lines: ["Yo ! DJ Taro. Ces parasites brouillent mes platines, c'est l'enfer.", "La nuit tombée, débarrasse le quartier de 3 monstres et je te file mon vieux pied de micro. Il a une sacrée allonge."], onEnd: () => { PROGRESS.quests.taro = { start: killCount() }; saveProgress(); showBanner("Quête : vaincre 3 monstres pour DJ Taro", 2.4); } };
      const left = 3 - (killCount() - q.start);
      if (left > 0) return { lines: [`Encore ${left} monstre${left > 1 ? "s" : ""}, l'ami. Ils sortent la nuit, dans les grandes rues et dans le parc.`] };
      return { fresh: true, lines: ["Ça, c'est du nettoyage ! Le son est revenu.", "Tiens, le pied de micro : balayage, estoc, puis un moulinet qui touche tout autour de toi."], onEnd: () => unlockWeapon("mic_stand") };
    } },
  { id: "rin", name: "Rin", role: "luthière", pos: [-7.55, -36.5], face: -Math.PI / 2, coat: 0x4f6b4a, pants: 0x3a3a42, skin: 0xf0d8c0,
    talk: () => {
      if (hasWeapon("guitar")) return { lines: ["Prends soin d'elle. Et ne frappe pas trop fort les enceintes… sauf celles qui mordent."] };
      if (!PROGRESS.vinyl) return { fresh: !PROGRESS.quests.rin, lines: ["Je suis Rin, je répare les instruments du quartier.", "On raconte qu'un vinyle doré traîne sur un toit, du côté des murs tagués… Ceux où l'on peut rebondir à l'infini.", "Si tu me le fais écouter, je te fabrique quelque chose de spécial."], onEnd: () => { if (!PROGRESS.quests.rin) { PROGRESS.quests.rin = true; saveProgress(); showBanner("Quête : trouver le vinyle doré pour Rin", 2.4); } } };
      return { fresh: true, lines: ["Le vinyle doré ! … Écoute ce grain. Merci.", "Tiens : une guitare-hache. Lourde et lente, mais son troisième coup fait trembler le sol."], onEnd: () => unlockWeapon("guitar") };
    } },
  { id: "yuna", name: "Yuna", role: "chanteuse", pos: [7.5, -46], face: Math.PI / 2, night: true, coat: 0xb9a6ff, pants: 0x6a4a9a, skin: 0xe8d0c8, shiny: true,
    talk: () => {
      if (hasWeapon("flail")) return { lines: ["Fais-le chanter. Et regarde vers la tour, parfois : quelque chose y tourne, la nuit."] };
      const q = PROGRESS.quests.yuna;
      if (!q) return { fresh: true, lines: ["… La nuit, la ville chante faux. Tu l'entends ?", "Les Gueules-enceintes. Elles ont dévoré les amplis de mon groupe.", "Fais taire l'une d'elles et mon micro est à toi. Au bout de son câble, il frappe loin… et ramène ce qu'il touche."], onEnd: () => { PROGRESS.quests.yuna = { start: killCount("gueule") }; saveProgress(); showBanner("Quête : vaincre une Gueule-enceinte pour Yuna", 2.4); } };
      if (killCount("gueule") <= q.start) return { lines: ["Une Gueule-enceinte. Elles rôdent sur les grandes rues, une fois la nuit tombée."] };
      return { fresh: true, lines: ["Tu l'as fait taire… Le silence, enfin.", "Tiens, mon micro-fléau. Le troisième coup ramène l'ennemi vers toi et l'étourdit."], onEnd: () => unlockWeapon("flail") };
    } },
  { id: "sato", name: "Vieux Sato", role: "le guetteur", pos: [-7.3, -71], face: 0, coat: 0x8a8f99, pants: 0x5a5560, skin: 0xe0c8b0, scale: 0.93,
    talk: () => isNight()
      ? { lines: ["Le voilà… Regarde ! Le dragon de jade tourne autour de la tour.", "Il n'est pas encore prêt à descendre. Toi non plus, peut-être. Entraîne-toi."] }
      : { lines: ["Tu vois la tour, là-bas ? Rouge et blanche, avec ses anneaux de lumière.", "Les anciens disent qu'un dragon de jade y dort. Turquoise, avec des écailles d'or.", "La nuit, il se réveille et tourne autour. Un jour, il faudra l'affronter… mais pas aujourd'hui.", "(N pour avancer jusqu'à la nuit.)"] } },
  { id: "kiko", name: "Mamie Kiko", role: "les ramen", pos: [-7.55, -56.5], face: -Math.PI / 2, coat: 0xb8342a, pants: 0x3a2a2a, skin: 0xf0dcc8, scale: 0.86,
    talk: () => {
      const pe = sim.entities.get(P.id), ready = time - KIKO.at > 45;
      if (pe.health >= pe.maxHealth) return { lines: ["Tu es en pleine forme, mon petit ! Reviens me voir après une bagarre."] };
      if (!ready) return { lines: ["Encore faim ? Reviens dans un moment, le bouillon chauffe."] };
      return { fresh: true, lines: ["Oh, mon petit bonhomme, tu as l'air épuisé.", "Tiens, un bol de ramen bien chaud. Ça répare tout, même le chrome."], onEnd: () => { KIKO.at = time; sim.heal(pe, pe.maxHealth); pixelRing(P.pos, 2, 0.6, 0x8ff0b0); [523, 659, 784].forEach((f, i) => setTimeout(() => tone(f, f, 0.15, 0.05, "triangle"), i * 90)); } };
    } },
];
const KIKO = { at: -999 }; // dernier bol de ramen (pas sauvegardé)
const hasWeapon = (w) => sim.entities.get(P.id).weapons.has(w);
function unlockWeapon(w) { sim.queue({ type: "unlock", source: P.id, weapon: w }); }
const npcs = [];
function setupNpcs() {
  setupPedestrians();
  for (const def of NPC_DEFS) {
    const rig = buildPerson(def.coat, def.pants, def.skin, def.scale || 1);
    if (def.shiny) rig.meshes.filter((m) => m.material === npcMat(def.coat)).forEach((m) => (m.material = npcMat(def.coat, { metalness: 0.5, roughness: 0.3, emissive: 0x2a1a4a })));
    const y = dummyGround(def.pos[0], def.pos[1], 1);
    const n = { def, rig, pos: new THREE.Vector3(def.pos[0], y, def.pos[1]), yaw: def.face, t: rand() * 5, tag: null, col: null, props: new THREE.Group() };
    rig.root.position.copy(n.pos); rig.root.rotation.y = n.yaw;
    n.props.position.copy(n.pos); n.props.rotation.y = def.face; scene.add(n.props);
    NPC_DRESS[def.id](n);
    colliders.push(n.col = { min: [n.pos.x - 0.32, n.pos.y, n.pos.z - 0.32], max: [n.pos.x + 0.32, n.pos.y + 1.8, n.pos.z + 0.32], chain: false, cam: false });
    npcs.push(n);
  }
}
const dress = (parent, geo, mat, x, y, z, rx = 0, ry = 0, rz = 0) => { const m = new THREE.Mesh(geo, mat); m.position.set(x, y, z); m.rotation.set(rx, ry, rz); m.castShadow = true; parent.add(m); return m; };
// Accessoires et décor de chaque personnage
const NPC_DRESS = {
  kenji(n) {
    dress(n.rig.J.neck.g, new THREE.TorusGeometry(0.155, 0.025, 6, 20), npcMat(0xd8402f), 0, 0.2, 0, Math.PI / 2 - 0.1, 0, 0);
    setRigWeapon(n.rig, "sticks");
    // pad d'entraînement sur son pied
    dress(n.props, new THREE.CylinderGeometry(0.02, 0.02, 0.8, 6), npcMat(0x8d929e), 0, 0.4, -0.5);
    dress(n.props, new THREE.CylinderGeometry(0.17, 0.17, 0.06, 18), npcMat(0x2b2b30), 0, 0.82, -0.5);
    dress(n.props, new THREE.CylinderGeometry(0.15, 0.15, 0.01, 18), npcMat(0xd9cfc0), 0, 0.855, -0.5);
  },
  taro(n) {
    const head = n.rig.J.neck.g;
    dress(head, new THREE.TorusGeometry(0.17, 0.022, 6, 20, Math.PI), npcMat(0x2b2b30), 0, 0.19, 0, 0, Math.PI / 2, 0);
    for (const s of [-1, 1]) dress(head, new THREE.CylinderGeometry(0.06, 0.06, 0.05, 14), npcMat(0xd8402f), s * 0.165, 0.17, 0, 0, 0, Math.PI / 2);
    dress(head, new THREE.SphereGeometry(0.162, 14, 8, 0, Math.PI * 2, 0, Math.PI * 0.45), npcMat(0xf2b631), 0, 0.2, 0);
    dress(head, new THREE.BoxGeometry(0.2, 0.02, 0.14), npcMat(0xf2b631), 0, 0.25, -0.15);
    // table de mixage et enceintes
    const t = n.props;
    dress(t, new THREE.BoxGeometry(1.5, 0.9, 0.6), npcMat(0x2b2b30), 0, 0.45, -0.75);
    for (const s of [-1, 1]) { dress(t, new THREE.CylinderGeometry(0.2, 0.2, 0.04, 22), npcMat(0x8d929e), s * 0.42, 0.92, -0.75); n["disc" + s] = dress(t, new THREE.CylinderGeometry(0.17, 0.17, 0.02, 22), npcMat(0x111114), s * 0.42, 0.95, -0.75); }
    dress(t, new THREE.BoxGeometry(0.3, 0.04, 0.3), npcMat(0x3a3a42), 0, 0.92, -0.75);
    for (const s of [-1, 1]) { dress(t, new THREE.BoxGeometry(0.55, 1.1, 0.5), npcMat(0x1d1c22), s * 1.25, 0.55, -0.7); dress(t, new THREE.CylinderGeometry(0.18, 0.18, 0.02, 20), npcMat(0x4a4a52), s * 1.25, 0.6, -0.96, Math.PI / 2, 0, 0); }
    collider(n.pos.x + 0.4, n.pos.y, n.pos.z - 1.55, n.pos.x + 1.1, n.pos.y + 1.1, n.pos.z + 1.55, { noCam: true });
  },
  rin(n) {
    dress(n.rig.J.chest.g, new THREE.BoxGeometry(0.36, 0.5, 0.04), npcMat(0x7a5a3a), 0, 0.05, -0.17);
    dress(n.rig.J.neck.g, new THREE.SphereGeometry(0.08, 10, 8), npcMat(0x1a1614), 0, 0.25, 0.12);
    dress(n.rig.J.neck.g, new THREE.SphereGeometry(0.16, 14, 8, 0, Math.PI * 2, 0, Math.PI * 0.55), npcMat(0x1a1614), 0, 0.19, 0.01);
    // guitare dans le dos
    const g = new THREE.Group(); g.position.set(0.05, 0.1, 0.22); g.rotation.z = 0.5; n.rig.J.chest.g.add(g);
    dress(g, new THREE.BoxGeometry(0.4, 0.45, 0.08), npcMat(0xc8762a), 0, -0.2, 0); dress(g, new THREE.BoxGeometry(0.06, 0.6, 0.04), npcMat(0x5a3a1a), 0, 0.3, 0);
  },
  yuna(n) {
    dress(n.rig.J.neck.g, new THREE.SphereGeometry(0.165, 14, 8, 0, Math.PI * 2, 0, Math.PI * 0.6), npcMat(0x2a1a3a), 0, 0.19, 0.01);
    dress(n.rig.J.neck.g, capsule(0.1, 0.5), npcMat(0x2a1a3a), 0, 0.02, 0.11);
    setRigWeapon(n.rig, "flail");
    n.glow = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, color: 0xb36bff, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, opacity: 0.6 }));
    n.glow.scale.setScalar(3); n.glow.position.y = 1.2; n.props.add(n.glow);
  },
  sato(n) {
    dress(n.rig.J.neck.g, new THREE.ConeGeometry(0.07, 0.22, 8), npcMat(0xf4f0ea), 0, 0.02, -0.1, Math.PI, 0, 0);
    dress(n.rig.J.neck.g, new THREE.SphereGeometry(0.163, 14, 8, 0, Math.PI * 2, 0, Math.PI * 0.35), npcMat(0xd9d4cc), 0, 0.19, 0.02);
    dress(n.rig.handR, new THREE.CylinderGeometry(0.018, 0.018, 1.0, 6), npcMat(0x5a3a1a), 0, -0.45, -0.05);
  },
  kiko(n) {
    dress(n.rig.J.chest.g, new THREE.BoxGeometry(0.38, 0.55, 0.04), npcMat(0xf4f0ea), 0, 0.02, -0.17);
    dress(n.rig.J.neck.g, new THREE.SphereGeometry(0.1, 10, 8), npcMat(0xf4f0ea), 0, 0.33, 0.03);
    dress(n.rig.J.neck.g, new THREE.SphereGeometry(0.163, 14, 8, 0, Math.PI * 2, 0, Math.PI * 0.5), npcMat(0xe8e4e0), 0, 0.19, 0.01);
  },
};
// Poses de repos : chacun a sa petite activité
const NPC_IDLE = {
  kenji: (n, set) => { const r = Math.sin(n.t * 11), l = Math.sin(n.t * 11 + Math.PI); set("shoulderR", 0.9 + r * 0.25, 0, 0.25); set("elbowR", 1.2 + r * 0.2); set("shoulderL", 0.9 + l * 0.25, 0, -0.25); set("elbowL", 1.2 + l * 0.2); set("neck", 0.3 + Math.abs(r) * 0.05); set("waist", 0.1); },
  taro: (n, set) => { const beat = Math.sin(n.t * 8); set("neck", 0.2 + beat * 0.12); set("shoulderL", 2.2, 0, -0.6); set("elbowL", 2.4); set("shoulderR", 0.9 + Math.sin(n.t * 3) * 0.15, 0, 0.2); set("elbowR", 1.4); set("waist", 0.15, Math.sin(n.t * 4) * 0.1); set("kneeR", -0.15 - Math.max(0, beat) * 0.15); set("kneeL", -0.15 - Math.max(0, -beat) * 0.15); },
  rin: (n, set) => { set("shoulderR", 0.95, 0, -0.35); set("shoulderL", 0.95, 0, 0.35); set("elbowR", 1.9); set("elbowL", 1.9); set("hipL", 0.1, 0, -0.1); set("waist", 0, 0, 0.05); },
  yuna: (n, set) => { const sway = Math.sin(n.t * 2.2); set("shoulderR", 1.2, 0, 0.3); set("elbowR", 2.3); set("shoulderL", 0.4 + sway * 0.2, 0, -0.5); set("elbowL", 0.6); set("waist", 0, sway * 0.15, sway * 0.08); set("neck", -0.15 + Math.sin(n.t * 4) * 0.05); set("hipR", 0, 0, sway * 0.05); set("hipL", 0, 0, sway * 0.05); },
  sato: (n, set) => { set("waist", 0.4); set("chest", 0.15); set("neck", -0.55); set("shoulderR", 0.55, 0, 0.15); set("elbowR", 0.3); set("shoulderL", -0.2, 0, -0.1); set("elbowL", 0.5); set("kneeR", -0.15); set("kneeL", -0.15); },
  kiko: (n, set) => { set("shoulderR", 1.0 + Math.sin(n.t * 3) * 0.2, Math.cos(n.t * 3) * 0.3, 0.2); set("elbowR", 1.3); set("shoulderL", 0.6, 0, -0.2); set("elbowL", 1.6); set("waist", 0.25); set("neck", 0.2); },
};
function updateNpcs(dt) {
  if (!npcs.length) return;
  updatePedestrians(dt);
  const night = isNight();
  for (const n of npcs) {
    const present = !n.def.night || night;
    n.rig.root.visible = n.props.visible = present;
    // le collisionneur disparaît avec le personnage
    n.col.max[1] = present ? n.pos.y + 1.8 : n.pos.y - 5;
    if (!present) continue;
    n.t += dt;
    const d = n.pos.distanceTo(P.pos), talking = DIALOG.npc === n;
    // il se tourne vers le joueur quand on lui parle (ou quand on s'approche)
    const toP = Math.atan2(-(P.pos.x - n.pos.x), -(P.pos.z - n.pos.z));
    n.yaw = lerpAngle(n.yaw, talking || (d < 3.5 && n.def.id !== "kenji" && n.def.id !== "taro") ? toP : n.def.face, 1 - Math.exp(-4 * dt));
    n.rig.root.rotation.y = n.yaw;
    if (n.disc1) { n.disc1.rotation.y += dt * 3.5; n["disc-1"].rotation.y += dt * 3.5; }
    if (n.glow) n.glow.material.opacity = 0.4 + 0.2 * Math.sin(n.t * 3);
    if (d > 50) continue;
    const b = {}, set = (j, x = 0, y = 0, z = 0) => { b[j] = { x, y, z }; };
    const br = Math.sin(n.t * 1.8);
    set("hipR", 0, 0, 0.04); set("hipL", 0, 0, -0.04); set("kneeR", -0.06); set("kneeL", -0.06);
    set("shoulderR", 0.05, 0, 0.1 + 0.03 * br); set("shoulderL", 0.05, 0, -0.1 - 0.03 * br); set("elbowR", 0.2); set("elbowL", 0.2);
    set("waist", 0.02 * br); set("neck", 0);
    if (!talking) NPC_IDLE[n.def.id](n, set);
    else { set("neck", -0.05, 0, Math.sin(n.t * 2) * 0.06); set("shoulderR", 0.4 + Math.max(0, Math.sin(n.t * 5)) * 0.4, 0, 0.3); set("elbowR", 1.2); if (n.def.id === "sato") { set("waist", 0.35); set("neck", -0.4); } }
    // le regard suit le joueur quand il est proche
    if (d < 6) b.neck.y = Math.max(-0.8, Math.min(0.8, wrapAngle(toP - n.yaw)));
    animateRig(n.rig, b, dt);
  }
}

// ============================================================================
// Dialogues (F pour parler et passer à la suite, Échap pour fermer)
// ============================================================================
const DIALOG = { npc: null, lines: [], i: 0, onEnd: null };
const dialogEl = document.getElementById("dialog");
function nearestNpc() {
  let best = null, bd = 2.8;
  for (const n of npcs) { if (n.def.night && !isNight()) continue; const d = n.pos.distanceTo(P.pos); if (d < bd) { bd = d; best = n; } }
  return best;
}
function interact() {
  if (DIALOG.npc) { nextLine(); return; }
  const n = nearestNpc(); if (!n || P.dead) return;
  if (P.combat > 0.5) { showBanner(`${n.def.name} : « Pas en plein combat ! »`, 1.4); return; }
  const d = n.def.talk();
  Object.assign(DIALOG, { npc: n, lines: d.lines, i: 0, onEnd: d.onEnd || null });
  P.talking = n; P.aiming = false; renderDialog(); tone(660, 660, 0.05, 0.03, "triangle");
}
function nextLine() {
  DIALOG.i++;
  if (DIALOG.i >= DIALOG.lines.length) { closeDialog(true); return; }
  renderDialog(); tone(560 + 60 * (DIALOG.i % 3), 560, 0.04, 0.025, "triangle");
}
function closeDialog(finished) {
  const end = finished ? DIALOG.onEnd : null;
  DIALOG.npc = null; DIALOG.onEnd = null; P.talking = null; dialogEl.hidden = true;
  if (end) end();
}
function renderDialog() {
  const n = DIALOG.npc;
  dialogEl.hidden = false;
  dialogEl.innerHTML = `<div class="dl-name"><b>${n.def.name}</b><small>${n.def.role}</small></div><p>${DIALOG.lines[DIALOG.i]}</p><div class="dl-next">${DIALOG.i < DIALOG.lines.length - 1 ? "F ou clic : suite" : "F ou clic : fermer"}</div>`;
}
addEventListener("keydown", (e) => { if (e.code === "Escape" && DIALOG.npc) closeDialog(false); });
renderer.domElement.addEventListener("mousedown", (e) => { if (DIALOG.npc && e.button === 0 && locked()) nextLine(); });
// Étiquettes au-dessus des personnages importants (◆ = quelque chose de nouveau à dire)
function updateNpcTags() {
  if (DIALOG.npc && (DIALOG.npc.pos.distanceTo(P.pos) > 4.5 || P.knock > 0)) closeDialog(false);
  const near = nearestNpc();
  for (const n of npcs) {
    const present = !n.def.night || isNight(), d = n.pos.distanceTo(P.pos);
    if (!n.tag) { n.tag = document.createElement("div"); n.tag.className = "hud npctag"; document.body.appendChild(n.tag); }
    const show = present && d < 22 && DIALOG.npc !== n;
    const p = project(n.pos.clone().add(new THREE.Vector3(0, 2.25 * (n.def.scale || 1), 0)));
    n.tag.hidden = !show || p.behind;
    if (n.tag.hidden) continue;
    n.tagTimer = (n.tagTimer || 0) - 1;
    if (n.tagTimer <= 0) { n.tagTimer = 20; n.fresh = !!n.def.talk().fresh; }
    n.tag.innerHTML = `<b class="${n.fresh ? "new" : ""}">${n.fresh ? "◆ " : ""}${n.def.name}</b>${near === n && P.combat < 0.5 ? "<em>F parler</em>" : ""}`;
    n.tag.style.transform = `translate(${p.x}px, ${p.y}px) translate(-50%, -100%)`;
  }
}
function npcOnKill(kind) {
  const q = PROGRESS.quests.taro;
  if (q && !hasWeapon("mic_stand")) { const n = killCount() - q.start; if (n <= 3) showBanner(n >= 3 ? "Quête de Taro : terminée, retourne le voir" : `Quête de Taro : ${n} / 3`, 1.8); }
  const y = PROGRESS.quests.yuna;
  if (y && kind === "gueule" && !hasWeapon("flail")) showBanner("Quête de Yuna : terminée, retourne la voir", 2);
}
