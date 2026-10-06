"use strict";
// ============================================================================
// Jeu : état du joueur, sorts équipés, entrées
// ============================================================================
const sim = new Sim(DATA);
const SPAWN = new THREE.Vector3(2, 0, 16);
const P = {
  pos: SPAWN.clone(), vel: new THREE.Vector3(), onFloor: true, coyote: 0, jumpBuffer: 0, airJumps: 1, wallJumps: 0, airLock: 0, airDash: 1, superUsed: false,
  facingYaw: 0, prevYaw: 0, yawRate: 0, wish: new THREE.Vector3(), dodgeLeft: 0, dodgeVel: new THREE.Vector3(), dodgeAir: false, extraDodges: 0,
  lungeLeft: 0, lungeTotal: 1, lungeSpeed: 0, ghost: 0, attackSlow: 0, diving: false, aiming: false, wave: "sine",
  sprint: 0, idle: 0, lying: false, waved: 0, t: 0, phase: 0, speedRatio: 0, flatSpeed: 0, wall: null, secrets: 0, dead: false,
  mods: null, healAcc: 0, healTimer: 0, trailTimer: 0, ringTimer: 0,
  weapon: DATA.start_weapons[0], knock: 0, combat: 0, flailAt: -1, talking: null,
};
const R = 0.35, H = 1.8, STEP_UP = 0.42;
P.id = sim.spawn("player", P.pos);
dummy.id = sim.spawn("training_dummy", dummy.pos);
P.mods = sim.mods(sim.entities.get(P.id));
const cam = { yaw: 0, pitch: -0.2, pivot: SPAWN.clone().add(new THREE.Vector3(0, 1.4, 0)), dist: 4.8, fov: 70, shake: 0, aim: 0 };
// La Sim demande à l'affichage s'il y a un mur entre deux points (projectiles, rayons)
sim.world.blocked = (a, b) => {
  const d = b.clone().sub(a), len = d.length(); if (len < 1e-4) return false; d.divideScalar(len);
  for (const c of colliders) if (c.cam && rayBox([a.x, a.y, a.z], [d.x, d.y, d.z], len, c) < len) return true;
  return false;
};

// --- Sorts équipés (A E R T), choisis dans la fenêtre « Mes sorts » ----------------
// A et E : sorts de base · R et T : sorts déjà fusionnés
const SLOTS = [{ code: "KeyQ", label: "A", kind: "spell" }, { code: "KeyE", label: "E", kind: "spell" }, { code: "KeyR", label: "R", kind: "fusion" }, { code: "KeyT", label: "T", kind: "fusion" }];
const FUSION_BY_ID = Object.fromEntries(DATA.fusions.map((f) => [f.id, f]));
const FUSION_COLOR = "#ffd27a";
const WAVE_KEYS = { Digit1: "sine", Digit2: "square", Digit3: "triangle" };
let EQUIP = ["gater", "delay", "tp", "renvoi"];
try { const saved = JSON.parse(localStorage.getItem("twisted.loadout2") || "null"); if (Array.isArray(saved) && saved.length === 4 && saved.every((s, i) => (SLOTS[i].kind === "spell" ? DATA.spells[s] : FUSION_BY_ID[s]))) EQUIP = saved; } catch (e) { /* pas de stockage : sorts par défaut */ }
const nameOf = (id) => (DATA.spells[id] || DATA.waves[id] || {}).name || id;

const keys = new Set(), pressed = new Set();
let attackClicks = 0;
const locked = () => document.pointerLockElement === renderer.domElement;
const loadoutEl = document.getElementById("loadout");
let paused = false;
addEventListener("keydown", (e) => {
  if (e.code === "Tab") { e.preventDefault(); toggleLoadout(); return; }
  if (paused) { if (e.code === "Escape") toggleLoadout(false); return; }
  if (["Space", "ShiftLeft", "KeyW", "KeyA", "KeyS", "KeyD", "KeyQ", "KeyE", "KeyR", "KeyT", "KeyF", "KeyX"].includes(e.code)) e.preventDefault();
  if (!keys.has(e.code)) pressed.add(e.code);
  keys.add(e.code);
  if (e.code === "KeyP") setPixelMode(!PIX.on);
  if (e.code === "KeyH") { const h = document.getElementById("help"); h.hidden = !h.hidden; }
  if (e.code === "KeyM") { muted = !muted; showBanner(muted ? "Son coupé" : "Son activé", 0.8); }
  if (e.code === "KeyN") { skipTime(); showBanner(isNight() ? "Le jour se lève…" : "La nuit tombe…", 1.4); }
});
addEventListener("keyup", (e) => keys.delete(e.code));
addEventListener("blur", () => { keys.clear(); P.aiming = false; });
// Capture de la souris. Le navigateur peut refuser une capture (juste après
// l'avoir rendue, par exemple) : on réessaie au clic suivant au lieu
// d'abandonner. Après 3 refus d'affilée seulement, on bascule en secours
// (clic droit maintenu pour tourner la caméra), sans arrêter de réessayer.
let lockFails = 0, lockTime = 0;
const lockFallback = () => lockFails >= 3;
document.addEventListener("pointerlockerror", () => { lockFails++; updateResume(); });
document.addEventListener("pointerlockchange", () => { lockTime = performance.now(); if (locked()) lockFails = 0; updateResume(); });
function requestLock() {
  try { const p = renderer.domElement.requestPointerLock(); if (p && p.catch) p.catch(() => { lockFails++; updateResume(); }); } catch (err) { lockFails++; updateResume(); }
}
renderer.domElement.addEventListener("contextmenu", (e) => e.preventDefault());
renderer.domElement.addEventListener("mousedown", (e) => {
  initAudio();
  if (paused) return;
  if (!locked()) { requestLock(); if (!lockFallback()) return; }
  if (e.button === 0) attackClicks++;
  if (e.button === 2) P.aiming = true;
});
addEventListener("mouseup", (e) => { if (e.button === 2) P.aiming = false; });
// molette : arme suivante / précédente
addEventListener("wheel", (e) => { if (paused || !overlay.hidden || P.talking) return; cycleWeapon(e.deltaY > 0 ? 1 : -1); }, { passive: true });
addEventListener("mousemove", (e) => {
  if (paused || (!locked() && !(e.buttons & 2))) return;
  if (performance.now() - lockTime < 150 || Math.abs(e.movementX) > 300 || Math.abs(e.movementY) > 300) return;
  const s = P.aiming ? 0.0016 : 0.0025;
  cam.yaw -= e.movementX * s; cam.pitch = Math.min(0.6, Math.max(-1.25, cam.pitch - e.movementY * s));
});
const overlay = document.getElementById("overlay"), resume = document.getElementById("resume"), playBtn = document.getElementById("play");
function updateResume() { resume.textContent = lockFallback() ? "Clic droit maintenu pour tourner la caméra · clic pour réessayer de capturer la souris" : "Clique dans la scène pour reprendre la souris"; resume.hidden = !overlay.hidden || paused || locked(); }
playBtn.addEventListener("click", () => { overlay.hidden = true; initAudio(); if (audio && audio.resume) audio.resume(); requestLock(); updateResume(); });
document.getElementById("openLoadout").addEventListener("click", () => { overlay.hidden = true; initAudio(); toggleLoadout(true); });

// --- Fenêtre « Mes sorts » ------------------------------------------------------------
let selectedSlot = 0;
function toggleLoadout(open = !paused) {
  paused = open; loadoutEl.hidden = !open;
  if (open) { keys.clear(); P.aiming = false; try { document.exitPointerLock(); } catch (e) { /* rien */ } renderLoadout(); }
  else { try { localStorage.setItem("twisted.loadout2", JSON.stringify(EQUIP)); } catch (e) { /* rien */ } buildSpellBar(); if (overlay.hidden) requestLock(); }
  updateResume();
}
// Ce que contient un emplacement : clé de recharge, nom, abréviation, couleur
function slotInfo(i) {
  const id = EQUIP[i];
  if (SLOTS[i].kind === "spell") { const sp = DATA.spells[id]; return { key: id, name: sp.name, short: sp.short, color: sp.color }; }
  const f = FUSION_BY_ID[id]; return { key: "fusion:" + id, name: f.name, short: f.short, color: FUSION_COLOR, fusion: f };
}
function renderLoadout() {
  const slots = document.getElementById("loSlots"), grid = document.getElementById("loSpells"), waves = document.getElementById("loWaves"), title = document.getElementById("loGridTitle");
  slots.innerHTML = `<div class="lo-slot fixed"><kbd>Maj</kbd><b style="color:${DATA.spells.bitcrush.color}">Bitcrush</b><small>dash, 3 charges</small></div>` +
    SLOTS.map((s, i) => { const inf = slotInfo(i); return `<button type="button" class="lo-slot${i === selectedSlot ? " sel" : ""}" data-slot="${i}"><kbd>${s.label} · ${s.kind === "spell" ? "sort" : "fusion"}</kbd><b style="color:${inf.color}">${inf.name}</b><small>${i === selectedSlot ? "choisis ci-dessous ↓" : "changer"}</small></button>`; }).join("");
  const kind = SLOTS[selectedSlot].kind;
  const keyOf = (id) => { const at = EQUIP.findIndex((e, k) => e === id && SLOTS[k].kind === kind); return at >= 0 ? "touche " + SLOTS[at].label : ""; };
  if (kind === "spell") {
    title.innerHTML = `Sorts de base <small>pour A et E</small>`;
    grid.innerHTML = Object.entries(DATA.spells).filter(([id]) => id !== "bitcrush").map(([id, sp]) =>
      `<button type="button" class="lo-spell${keyOf(id) ? " on" : ""}" data-pick="${id}"><span class="lo-top"><b style="color:${sp.color}">${sp.name}</b><em>${keyOf(id)}</em></span><span class="lo-desc">${sp.desc}</span><span class="lo-cd">Recharge ${sp.cooldown} s</span></button>`).join("");
  } else {
    title.innerHTML = `Sorts fusionnés <small>pour R et T · déjà prêts, une seule touche</small>`;
    grid.innerHTML = DATA.fusions.map((f) =>
      `<button type="button" class="lo-spell${keyOf(f.id) ? " on" : ""}" data-pick="${f.id}"><span class="lo-top"><b style="color:${FUSION_COLOR}">${f.name}</b><em>${keyOf(f.id)}</em></span><span class="lo-mix">${nameOf(f.a)} + ${nameOf(f.b)}</span><span class="lo-desc">${f.desc}</span><span class="lo-cd">Recharge ${f.cooldown} s${f.combat_only ? " · en combat" : ""}</span></button>`).join("");
  }
  const wpEl = document.getElementById("loWeapons"), pe = sim.entities.get(P.id);
  wpEl.innerHTML = Object.entries(DATA.weapons).map(([id, w]) => pe.weapons.has(id)
    ? `<button type="button" class="lo-spell${P.weapon === id ? " on" : ""}" data-weapon="${id}"><span class="lo-top"><b style="color:${w.color}">${w.name}</b><em>${P.weapon === id ? "en main" : "prendre"}</em></span><span class="lo-desc">${w.desc}</span></button>`
    : `<div class="lo-spell locked"><span class="lo-top"><b>???</b><em>à trouver</em></span><span class="lo-desc">${w.hint || ""}</span></div>`).join("");
  wpEl.querySelectorAll("[data-weapon]").forEach((b) => b.addEventListener("click", () => { sim.queue({ type: "weapon", source: P.id, weapon: b.dataset.weapon }); sim.step(0); for (const ev of sim.drain()) onEvent(ev); renderLoadout(); }));
  waves.innerHTML = ["sine", "square", "triangle"].map((id) => { const w = DATA.waves[id]; return `<div class="lo-spell on"><span class="lo-top"><b style="color:${w.color}">${w.short} ${w.name}</b><em>touche ${w.key}</em></span><span class="lo-desc">${w.desc}</span><span class="lo-cd">Recharge ${w.cooldown} s</span></div>`; }).join("");
  slots.querySelectorAll("[data-slot]").forEach((b) => b.addEventListener("click", () => { selectedSlot = +b.dataset.slot; renderLoadout(); }));
  grid.querySelectorAll("[data-pick]").forEach((b) => b.addEventListener("click", () => {
    const id = b.dataset.pick, at = EQUIP.findIndex((e, k) => e === id && SLOTS[k].kind === kind);
    if (at >= 0 && at !== selectedSlot) EQUIP[at] = EQUIP[selectedSlot]; // déjà équipé ailleurs : on échange
    EQUIP[selectedSlot] = id; renderLoadout();
  }));
}
document.getElementById("loClose").addEventListener("click", () => toggleLoadout(false));

// --- Barre de sorts (HUD) ---------------------------------------------------------------
const spellbar = document.getElementById("spellbar");
let slotEls = [];
function buildSpellBar() {
  const sb = DATA.spells.bitcrush;
  const wp = DATA.weapons[P.weapon];
  const items = [{ key: "X", id: "weapon", short: wp.short, color: wp.color, weapon: true }, { sep: true }, { key: "Maj", id: "bitcrush", short: sb.short, color: sb.color }, ...SLOTS.map((s, i) => { const inf = slotInfo(i); return { key: s.label, id: inf.key, short: inf.short, color: inf.color, fusion: !!inf.fusion }; }),
    { sep: true }, ...["sine", "square", "triangle"].map((id) => ({ key: DATA.waves[id].key, id, wave: true, short: DATA.waves[id].short, color: DATA.waves[id].color }))];
  spellbar.innerHTML = items.map((it) => it.sep ? `<i class="sep"></i>` : `<div class="slot${it.wave ? " wave" : ""}${it.fusion ? " fusion" : ""}${it.weapon ? " weapon" : ""}" data-id="${it.id}"><span class="cd"></span><kbd>${it.key}</kbd><b style="color:${it.color}">${it.short}</b><small></small></div>`).join("");
  slotEls = [...spellbar.querySelectorAll(".slot")].map((el) => ({ el, id: el.dataset.id, cd: el.querySelector(".cd"), txt: el.querySelector("small") }));
}
buildSpellBar();
function updateWeaponSlot() { const s = slotEls.find((x) => x.id === "weapon"); if (!s) return; const w = DATA.weapons[P.weapon], b = s.el.querySelector("b"); b.textContent = w.short; b.style.color = w.color; s.el.title = w.name; }
function flashSlot(id) { const s = slotEls.find((x) => x.id === id); if (s) { s.el.classList.remove("deny"); void s.el.offsetWidth; s.el.classList.add("deny"); } }

const facing = () => new THREE.Vector3(-Math.sin(P.facingYaw), 0, -Math.cos(P.facingYaw));
const lerpAngle = (a, b, t) => { const d = ((((b - a + Math.PI) % (Math.PI * 2)) + Math.PI * 2) % (Math.PI * 2)) - Math.PI; return a + d * t; };
const wrapAngle = (a) => ((((a + Math.PI) % (Math.PI * 2)) + Math.PI * 2) % (Math.PI * 2)) - Math.PI;

// ============================================================================
// Animation : poses clés + ressorts (pour le bonhomme, son double fantôme,
// les PNJ et les monstres humanoïdes).
// Repères : épaule x+ = bras vers l'avant/le haut, épaule z+ = bras droit vers
// l'extérieur (z- pour le gauche), coude x+ = plier, taille y+ = buste vers la
// gauche, taille x+ = se pencher en avant, hanche x+ = jambe en avant, genou x- = plier.
// ============================================================================
const GUARD = { waist: { x: 0.08, y: 0.25 }, shoulderL: { x: 0.6, z: -0.25 }, elbowL: { x: 1.9 }, shoulderR: { x: 0.5, z: 0.25 }, elbowR: { x: 1.9 }, neck: { x: 0.08 },
  hipR: { x: -0.15, z: 0.08 }, hipL: { x: 0.3, z: -0.08 }, kneeR: { x: -0.35 }, kneeL: { x: -0.4 } };
function punchKeys(side, w) {
  const o = side === "R" ? "L" : "R", s = side === "R" ? 1 : -1;
  const strike = { waist: { x: 0.18, y: 0.65 * s }, chest: { y: 0.25 * s }, ["shoulder" + side]: { x: 1.5, z: 0.05 * s, s: 1.65 }, ["elbow" + side]: { x: 0.05 }, ["shoulder" + o]: { x: 0.5, z: -0.3 * s }, ["elbow" + o]: { x: 1.9 },
    ["hip" + o]: { x: 0.45 }, ["knee" + o]: { x: -0.5 }, ["hip" + side]: { x: -0.35 }, ["knee" + side]: { x: -0.15 } };
  return [
    { t: 0, k: 1.6, p: { waist: { x: 0.05, y: -0.45 * s }, chest: { y: -0.2 * s }, ["shoulder" + side]: { x: -0.3, z: 0.3 * s }, ["elbow" + side]: { x: 2.1 }, ["shoulder" + o]: { x: 0.6, z: -0.25 * s }, ["elbow" + o]: { x: 1.8 } } },
    { t: w, k: 4.5, p: strike }, { t: w + 0.12, p: strike }, { t: w + 0.32, p: GUARD },
  ];
}
// frappe de baguette : bras levé, poignet cassé, puis le coup part vers l'avant-bas
function stickKeys(side, w, hold = 0.08) {
  const o = side === "R" ? "L" : "R", s = side === "R" ? 1 : -1;
  const up = { waist: { x: -0.05, y: -0.3 * s }, ["shoulder" + side]: { x: 2.5, z: 0.25 * s }, ["elbow" + side]: { x: 1.5 }, ["shoulder" + o]: { x: 0.8, z: -0.2 * s }, ["elbow" + o]: { x: 1.3 } };
  const hit = { waist: { x: 0.2, y: 0.4 * s }, ["shoulder" + side]: { x: 0.95, z: 0.05 * s, s: 1.35 }, ["elbow" + side]: { x: 0.15 }, ["shoulder" + o]: { x: 1.1, z: -0.2 * s }, ["elbow" + o]: { x: 1.5 },
    ["hip" + o]: { x: 0.35 }, ["knee" + o]: { x: -0.45 }, ["hip" + side]: { x: -0.25 } };
  return [{ t: 0, k: 2, p: up }, { t: w, k: 6, p: hit }, { t: w + hold, p: hit }];
}
const ANIMS = {
  jab1: (w) => punchKeys("L", w),
  jab2: (w) => punchKeys("R", w),
  drop: (w) => {
    const a = { waist: { x: -0.15, y: -1.0 }, chest: { y: -0.4 }, shoulderR: { x: -1.2, z: 0.6 }, elbowR: { x: 1.4 }, shoulderL: { x: 0.9, z: -0.3 }, elbowL: { x: 1.3 }, hipL: { x: 0.5 }, kneeL: { x: -0.8 }, hipR: { x: -0.3 }, kneeR: { x: -1.0 } };
    const s = { waist: { x: 0.45, y: 1.05 }, chest: { y: 0.45 }, shoulderR: { x: 1.75, z: 0.1, s: 1.9 }, elbowR: { x: 0 }, shoulderL: { x: -0.7, z: -0.6 }, elbowL: { x: 0.9 }, hipL: { x: 0.85 }, kneeL: { x: -0.4 }, hipR: { x: -0.7 }, kneeR: { x: -0.25 } };
    return [{ t: 0, k: 1.4, p: a }, { t: w * 0.85, p: a }, { t: w, k: 6, p: s }, { t: w + 0.3, p: s }, { t: w + 0.6, p: GUARD }];
  },
  counter: (w) => {
    const legs = { hipR: { x: 0.55 }, kneeR: { x: -1.0 }, hipL: { x: -0.45 }, kneeL: { x: -0.5 } };
    const a = Object.assign({ waist: { x: 0.25, y: -1.3 }, shoulderR: { x: 0.4, z: 1.3 }, elbowR: { x: 0.9 }, shoulderL: { x: -0.4, z: -0.5 }, elbowL: { x: 1.2 } }, legs);
    const s = Object.assign({ waist: { x: 0.2, y: 1.1 }, shoulderR: { x: 1.2, z: 1.0, s: 1.6 }, elbowR: { x: 0.05 }, shoulderL: { x: -0.8, z: -0.7 }, elbowL: { x: 0.6 } }, legs);
    return [{ t: 0, k: 2, p: a }, { t: w + 0.05, k: 6, p: s }, { t: w + 0.2, p: s }, { t: w + 0.38, p: GUARD }];
  },
  // --- Baguettes ---
  st1: (w) => stickKeys("R", w), st2: (w) => stickKeys("L", w),
  // les deux baguettes ensemble
  st3: (w) => { const r = stickKeys("R", w), l = stickKeys("L", w); return r.map((k, i) => ({ t: k.t, k: k.k, p: Object.assign({}, l[i].p, k.p, { shoulderL: l[i].p.shoulderL, elbowL: l[i].p.elbowL, waist: { x: k.p.waist.x, y: 0 } }) })); },
  st4: (w) => {
    const keys = [{ t: 0, k: 2, p: { waist: { x: -0.1 }, shoulderR: { x: 2.4, z: 0.3 }, elbowR: { x: 1.4 }, shoulderL: { x: 2.4, z: -0.3 }, elbowL: { x: 1.4 }, hipR: { x: 0.3 }, kneeR: { x: -0.6 }, hipL: { x: 0.3 }, kneeL: { x: -0.6 } } }];
    for (let i = 0; i < 6; i++) { const r = i % 2 === 0; keys.push({ t: w + i * 0.06, k: 7, p: { waist: { x: 0.25, y: r ? 0.25 : -0.25 }, shoulderR: { x: r ? 0.9 : 2.0, z: 0.2 }, elbowR: { x: r ? 0.15 : 1.2 }, shoulderL: { x: r ? 2.0 : 0.9, z: -0.2 }, elbowL: { x: r ? 1.2 : 0.15 }, hipR: { x: 0.4 }, kneeR: { x: -0.7 }, hipL: { x: -0.2 }, kneeL: { x: -0.3 } } }); }
    keys.push({ t: w + 0.32, k: 8, p: { waist: { x: -0.2 }, shoulderR: { x: 2.9, z: 0.5 }, elbowR: { x: 0.2 }, shoulderL: { x: 2.9, z: -0.5 }, elbowL: { x: 0.2 }, neck: { x: -0.3 } } });
    keys.push({ t: w + 0.5, p: { waist: { x: -0.2 }, shoulderR: { x: 2.9, z: 0.5 }, elbowR: { x: 0.2 }, shoulderL: { x: 2.9, z: -0.5 }, elbowL: { x: 0.2 } } });
    return keys;
  },
  // --- Pied de micro ---
  ms1: (w) => {
    const a = { waist: { x: 0.05, y: -1.0 }, chest: { y: -0.4 }, shoulderR: { x: 0.9, z: 1.35 }, elbowR: { x: 0.2 }, shoulderL: { x: 0.8, z: -0.4 }, elbowL: { x: 1.6 }, hipL: { x: 0.35 }, kneeL: { x: -0.5 } };
    const s = { waist: { x: 0.15, y: 1.1 }, chest: { y: 0.45 }, shoulderR: { x: 0.95, z: -0.45 }, elbowR: { x: 0.15 }, shoulderL: { x: 0.3, z: -0.7 }, elbowL: { x: 0.9 }, hipL: { x: 0.45 }, kneeL: { x: -0.6 }, hipR: { x: -0.35 } };
    return [{ t: 0, k: 2, p: a }, { t: w, k: 5, p: s }, { t: w + 0.15, p: s }];
  },
  ms2: (w) => {
    const a = { waist: { x: -0.05, y: -0.6 }, shoulderR: { x: 0.5, z: 0.3 }, elbowR: { x: 2.0 }, shoulderL: { x: 1.0, z: -0.1 }, elbowL: { x: 1.4 }, hipR: { x: -0.3 }, kneeR: { x: -0.5 }, hipL: { x: 0.3 }, kneeL: { x: -0.4 } };
    const s = { waist: { x: 0.35, y: 0.35 }, shoulderR: { x: 1.05, z: 0.05, s: 1.45 }, elbowR: { x: 0 }, shoulderL: { x: 1.2, z: 0.1 }, elbowL: { x: 0.6 }, hipL: { x: 0.8 }, kneeL: { x: -0.6 }, hipR: { x: -0.6 }, kneeR: { x: -0.1 } };
    return [{ t: 0, k: 2, p: a }, { t: w, k: 7, p: s }, { t: w + 0.16, p: s }];
  },
  // moulinet : bras tendu sur le côté, tout le corps fait un tour (voir spinBody)
  ms3: (w) => { const p = { waist: { x: 0.15 }, shoulderR: { x: 0.95, z: 1.45 }, elbowR: { x: 0.1 }, shoulderL: { x: 0.5, z: -1.2 }, elbowL: { x: 0.4 }, hipR: { x: 0.35, z: 0.2 }, kneeR: { x: -0.7 }, hipL: { x: 0.35, z: -0.2 }, kneeL: { x: -0.7 } }; return [{ t: 0, k: 3, p }, { t: w + 0.42, p }]; },
  // --- Guitare-hache ---
  gt1: (w) => {
    const a = { waist: { x: -0.15, y: -0.75 }, chest: { y: -0.35 }, shoulderR: { x: 2.7, z: 0.6 }, elbowR: { x: 0.9 }, shoulderL: { x: 1.6, z: 0.3 }, elbowL: { x: 1.4 }, hipR: { x: -0.2 }, kneeR: { x: -0.5 } };
    const s = { waist: { x: 0.4, y: 0.75 }, chest: { y: 0.3 }, shoulderR: { x: 0.95, z: -0.3, s: 1.35 }, elbowR: { x: 0.1 }, shoulderL: { x: 0.6, z: -0.5 }, elbowL: { x: 1.0 }, hipL: { x: 0.6 }, kneeL: { x: -0.7 }, hipR: { x: -0.45 }, kneeR: { x: -0.2 } };
    return [{ t: 0, k: 1.4, p: a }, { t: w * 0.9, p: a }, { t: w, k: 6, p: s }, { t: w + 0.22, p: s }];
  },
  gt2: (w) => {
    const a = { waist: { x: 0.05, y: 0.9 }, chest: { y: 0.35 }, shoulderR: { x: 1.7, z: -0.7 }, elbowR: { x: 1.5 }, shoulderL: { x: 0.4, z: -0.5 }, elbowL: { x: 1.2 } };
    const s = { waist: { x: 0.35, y: -0.8 }, chest: { y: -0.35 }, shoulderR: { x: 1.2, z: 1.1, s: 1.35 }, elbowR: { x: 0.05 }, shoulderL: { x: 0.3, z: -0.4 }, elbowL: { x: 1.4 }, hipR: { x: 0.55 }, kneeR: { x: -0.7 }, hipL: { x: -0.4 } };
    return [{ t: 0, k: 1.4, p: a }, { t: w * 0.9, p: a }, { t: w, k: 6, p: s }, { t: w + 0.22, p: s }];
  },
  gt3: (w) => {
    const a = { waist: { x: -0.35 }, chest: { x: -0.15 }, shoulderR: { x: 3.1, z: 0.2 }, elbowR: { x: 0.6 }, shoulderL: { x: 3.0, z: -0.2 }, elbowL: { x: 0.8 }, hipR: { x: 0.6 }, kneeR: { x: -1.2 }, hipL: { x: 0.2 }, kneeL: { x: -0.6 }, neck: { x: -0.25 } };
    const s = { waist: { x: 0.75 }, chest: { x: 0.2 }, shoulderR: { x: 0.85, z: 0.05, s: 1.5 }, elbowR: { x: 0.05 }, shoulderL: { x: 0.9, z: -0.05 }, elbowL: { x: 0.2 }, hipR: { x: 1.0 }, kneeR: { x: -1.6 }, hipL: { x: -0.4 }, kneeL: { x: -0.9 }, neck: { x: 0.3 } };
    return [{ t: 0, k: 1.3, p: a }, { t: w * 0.85, p: a }, { t: w, k: 8, p: s }, { t: w + 0.4, p: s }];
  },
  // --- Twisted Sword : taillades horizontales, le bras s'étire ---
  tw1: (w) => {
    const a = { waist: { x: 0.05, y: -0.95 }, chest: { y: -0.4 }, shoulderR: { x: 1.5, z: 1.35 }, elbowR: { x: 0.9 }, shoulderL: { x: 0.7, z: -0.4 }, elbowL: { x: 1.6 } };
    const s = { waist: { x: 0.25, y: 1.0 }, chest: { y: 0.4 }, shoulderR: { x: 1.25, z: -0.5, s: 1.45 }, elbowR: { x: 0.1 }, shoulderL: { x: 0.3, z: -0.8 }, elbowL: { x: 0.9 }, hipL: { x: 0.5 }, kneeL: { x: -0.6 }, hipR: { x: -0.35 } };
    return [{ t: 0, k: 2, p: a }, { t: w, k: 7, p: s }, { t: w + 0.12, p: s }];
  },
  tw2: (w) => {
    const a = { waist: { x: 0.05, y: 0.9 }, chest: { y: 0.4 }, shoulderR: { x: 1.4, z: -0.6 }, elbowR: { x: 1.5 }, shoulderL: { x: 0.6, z: -0.3 }, elbowL: { x: 1.6 } };
    const s = { waist: { x: 0.25, y: -0.95 }, chest: { y: -0.4 }, shoulderR: { x: 1.35, z: 1.35, s: 1.45 }, elbowR: { x: 0.08 }, shoulderL: { x: 0.4, z: -0.4 }, elbowL: { x: 1.5 }, hipR: { x: 0.5 }, kneeR: { x: -0.6 }, hipL: { x: -0.35 } };
    return [{ t: 0, k: 2, p: a }, { t: w, k: 7, p: s }, { t: w + 0.12, p: s }];
  },
  // uppercut : accroupi, puis tout le corps se déplie vers le haut et le bras s'allonge
  uppercut: (w) => {
    const a = { waist: { x: 0.5, y: -0.4 }, hipR: { x: 0.7 }, kneeR: { x: -1.2 }, hipL: { x: 0.5 }, kneeL: { x: -1.0 }, shoulderR: { x: -0.3, z: 0.3 }, elbowR: { x: 1.7 }, shoulderL: { x: 0.7, z: -0.3 }, elbowL: { x: 1.8 } };
    const s = { waist: { x: -0.35, y: 0.4 }, chest: { x: -0.2 }, hipR: { x: -0.2 }, kneeR: { x: -0.1 }, hipL: { x: 0.2 }, kneeL: { x: -0.2 }, shoulderR: { x: 2.9, z: 0.1, s: 1.8 }, elbowR: { x: 0.25 }, shoulderL: { x: 0.3, z: -0.6 }, elbowL: { x: 1.4 }, neck: { x: -0.4 } };
    return [{ t: 0, k: 2, p: a }, { t: w * 0.9, p: a }, { t: w, k: 8, p: s }, { t: w + 0.3, p: s }];
  },
  // --- Micro-fléau ---
  fl1: (w) => {
    const a = { waist: { x: 0.05, y: -0.8 }, shoulderR: { x: 1.3, z: 1.3 }, elbowR: { x: 0.4 }, shoulderL: { x: 0.6, z: -0.3 }, elbowL: { x: 1.7 } };
    const s = { waist: { x: 0.2, y: 0.95 }, shoulderR: { x: 1.35, z: -0.35 }, elbowR: { x: 0.1 }, shoulderL: { x: 0.4, z: -0.6 }, elbowL: { x: 1.4 }, hipL: { x: 0.4 }, kneeL: { x: -0.5 } };
    return [{ t: 0, k: 2, p: a }, { t: w, k: 6, p: s }, { t: w + 0.15, p: s }];
  },
  fl2: (w) => {
    const a = { waist: { x: 0.05, y: 0.8 }, shoulderR: { x: 1.4, z: -0.6 }, elbowR: { x: 1.4 }, shoulderL: { x: 0.6, z: -0.3 }, elbowL: { x: 1.7 } };
    const s = { waist: { x: 0.2, y: -0.85 }, shoulderR: { x: 1.3, z: 1.3 }, elbowR: { x: 0.1 }, shoulderL: { x: 0.4, z: -0.4 }, elbowL: { x: 1.6 }, hipR: { x: 0.4 }, kneeR: { x: -0.5 } };
    return [{ t: 0, k: 2, p: a }, { t: w, k: 6, p: s }, { t: w + 0.15, p: s }];
  },
  fl3: (w) => {
    const a = { waist: { x: -0.25, y: -0.3 }, shoulderR: { x: 3.0, z: 0.4 }, elbowR: { x: 0.5 }, shoulderL: { x: 1.0, z: -0.3 }, elbowL: { x: 1.2 }, hipR: { x: -0.3 }, kneeR: { x: -0.4 } };
    const s = { waist: { x: 0.35, y: 0.2 }, shoulderR: { x: 1.3, z: 0.05, s: 1.6 }, elbowR: { x: 0 }, shoulderL: { x: 0.8, z: -0.3 }, elbowL: { x: 1.3 }, hipL: { x: 0.5 }, kneeL: { x: -0.6 } };
    const yank = { waist: { x: -0.2, y: -0.5 }, shoulderR: { x: 0.3, z: 0.4 }, elbowR: { x: 2.0 }, shoulderL: { x: 0.9, z: -0.2 }, elbowL: { x: 1.5 }, hipR: { x: -0.4 }, kneeR: { x: -0.6 }, hipL: { x: 0.3 }, kneeL: { x: -0.4 } };
    return [{ t: 0, k: 2, p: a }, { t: w, k: 6, p: s }, { t: w + 0.1, p: s }, { t: w + 0.25, k: 5, p: yank }, { t: w + 0.45, p: yank }];
  },
  dive: () => { const p = { waist: { x: 0.75 }, shoulderR: { x: 2.3, z: 0.15 }, shoulderL: { x: 2.3, z: -0.15 }, elbowR: { x: 0.1 }, elbowL: { x: 0.1 }, hipR: { x: 0.9 }, kneeR: { x: -1.6 }, hipL: { x: 0.9 }, kneeL: { x: -1.6 } }; return [{ t: 0, k: 2, p }, { t: 99, p }]; },
  slam: () => { const p = { waist: { x: 0.95 }, shoulderR: { x: 1.5, z: 0.2 }, elbowR: { x: 0 }, shoulderL: { x: 1.3, z: -0.4 }, elbowL: { x: 0.3 }, hipR: { x: 1.3 }, kneeR: { x: -1.9 }, hipL: { x: 0.6 }, kneeL: { x: -1.6 } }; return [{ t: 0, k: 6, p }, { t: 0.35, p }, { t: 0.65, p: GUARD }]; },
  dodge: (d) => { const p = { waist: { x: 0.6 }, shoulderR: { x: -1.1, z: 0.45, s: 1.5 }, shoulderL: { x: -1.1, z: -0.45, s: 1.5 }, elbowR: { x: 0.5 }, elbowL: { x: 0.5 }, hipR: { x: 1.0 }, kneeR: { x: -1.7 }, hipL: { x: -0.5 }, kneeL: { x: -0.9 } }; return [{ t: 0, k: 3, p }, { t: d, p }]; },
  land: () => { const p = { hipR: { x: 0.8 }, kneeR: { x: -1.3 }, hipL: { x: 0.8 }, kneeL: { x: -1.3 }, waist: { x: 0.35 }, shoulderR: { x: 0.3, z: 0.6 }, shoulderL: { x: 0.3, z: -0.6 } }; return [{ t: 0, k: 5, p }, { t: 0.12, p }]; },
  // touché : le buste encaisse vers l'arrière, la tête suit
  hurt: () => { const p = { waist: { x: -0.35, y: 0.25 }, chest: { x: -0.2 }, neck: { x: -0.45 }, shoulderR: { x: 0.2, z: 0.7 }, shoulderL: { x: 0.4, z: -0.6 }, elbowR: { x: 0.9 }, elbowL: { x: 1.2 }, kneeR: { x: -0.4 }, kneeL: { x: -0.25 } }; return [{ t: 0, k: 7, p }, { t: 0.12, p }, { t: 0.32, k: 1.5, p: GUARD }]; },
  // projeté au sol : bras et jambes en l'air pendant la chute
  knocked: () => { const p = { waist: { x: -0.3 }, neck: { x: 0.5 }, shoulderR: { x: 1.8, z: 0.9 }, shoulderL: { x: 1.8, z: -0.9 }, elbowR: { x: 0.5 }, elbowL: { x: 0.5 }, hipR: { x: 0.9 }, kneeR: { x: -0.9 }, hipL: { x: 0.6 }, kneeL: { x: -0.5 } }; return [{ t: 0, k: 4, p }, { t: 0.5, p }, { t: 0.8, k: 1, p: { waist: { x: 0.2 }, neck: { x: 0.2 }, shoulderR: { x: 0.3, z: 0.4 }, shoulderL: { x: 0.3, z: -0.4 }, hipR: { x: 0.3 }, kneeR: { x: -0.6 }, hipL: { x: 0.1 }, kneeL: { x: -0.2 } } }, { t: 1.2, p: {} }]; },
  // roulade : roulé en boule (genoux contre le torse-pilule plié), puis on se déplie
  roll: () => { const p = { waist: { x: 1.1 }, chest: { x: 0.6 }, neck: { x: 0.7 }, hipR: { x: 1.9 }, kneeR: { x: -2.3 }, hipL: { x: 1.9 }, kneeL: { x: -2.3 }, shoulderR: { x: 1.3, z: 0.4 }, shoulderL: { x: 1.3, z: -0.4 }, elbowR: { x: 1.6 }, elbowL: { x: 1.6 } };
    const out = { waist: { x: 0.2 }, shoulderR: { x: 0.9, z: 1.0, s: 1.5 }, shoulderL: { x: 0.9, z: -1.0, s: 1.5 }, elbowR: { x: 0.1 }, elbowL: { x: 0.1 } };
    return [{ t: 0, k: 4, p }, { t: 0.32, p }, { t: 0.42, k: 5, p: out }, { t: 0.6, p: out }]; },
  // dégainer une arme
  draw: () => { const p = { waist: { y: -0.4 }, shoulderR: { x: 1.2, z: 0.9 }, elbowR: { x: 1.2 }, neck: { y: 0.3 } }; return [{ t: 0, k: 4, p }, { t: 0.15, p }, { t: 0.35, k: 2, p: {} }]; },
  // sort lancé : paumes en avant
  cast: () => { const p = { waist: { x: 0.1 }, shoulderR: { x: 1.4, z: 0.25 }, shoulderL: { x: 1.4, z: -0.25 }, elbowR: { x: 0.2 }, elbowL: { x: 0.2 } }; return [{ t: 0, k: 4, p }, { t: 0.25, p }, { t: 0.45, p: {} }]; },
  // Gater : bras croisés devant, garde fermée
  gate: () => { const p = { waist: { x: 0.15 }, shoulderR: { x: 1.3, z: -0.35 }, shoulderL: { x: 1.3, z: 0.35 }, elbowR: { x: 1.6 }, elbowL: { x: 1.6 }, hipR: { x: 0.3 }, kneeR: { x: -0.5 }, hipL: { x: 0.3 }, kneeL: { x: -0.5 } }; return [{ t: 0, k: 6, p }, { t: 0.35, p }]; },
  shoot: () => { const a = { shoulderR: { x: 1.95, z: 0.1 }, elbowR: { x: 0 }, waist: { x: -0.08, y: 0.35 } }, b = { shoulderR: { x: 1.55, z: 0.1 }, elbowR: { x: 0.05 }, waist: { x: 0, y: 0.35 } }; return [{ t: 0, k: 6, p: a }, { t: 0.18, k: 1.5, p: b }]; },
  wave: () => {
    const up = (z, e) => ({ shoulderR: { x: 0, z }, elbowR: { x: e }, neck: { y: 0.25 } });
    const keys = [{ t: 0, k: 1.4, p: up(2.6, 0.4) }];
    for (let i = 0; i < 6; i++) keys.push({ t: 0.35 + i * 0.2, p: up(i % 2 ? 2.75 : 2.35, i % 2 ? 0.15 : 0.7) });
    keys.push({ t: 1.75, p: up(2.6, 0.4) });
    return keys;
  },
  lie: () => { const p = { shoulderR: { x: 0.3, z: 2.9 }, elbowR: { x: 2.3 }, shoulderL: { x: 0.3, z: -2.9 }, elbowL: { x: 2.3 }, hipR: { x: 0.2 }, kneeR: { x: -0.35 }, hipL: { x: 0.05, z: -0.08 }, kneeL: { x: -0.05 }, neck: { x: -0.2 }, waist: { x: 0 } }; return [{ t: 0, k: 0.6, p }, { t: 999, p }]; },
};
// le corps fait un tour complet (moulinet)
function spinBody(rig, dur, turns = 1) { rig.spin.rotation.y = 0; play(rig.spin.rotation, [{ to: { y: Math.PI * 2 * turns }, dur, ease: easeInOut }], () => { rig.spin.rotation.y = 0; }); }
const animOf = (move) => (DATA.moves[move] && DATA.moves[move].anim) || move;
function startAction(rig, name, arg) { rig.action = { name, keys: (ANIMS[name] || ANIMS.jab2)(arg ?? 0.08), t: 0 }; }

// Garde de combat, selon l'arme tenue
const WEAPON_GUARD = {
  fists: GUARD,
  twisted_sword: Object.assign({}, GUARD, { waist: { x: 0.1, y: 0.4 }, shoulderR: { x: 0.9, z: 0.35 }, elbowR: { x: 1.2 }, shoulderL: { x: 0.5, z: -0.35 }, elbowL: { x: 1.7 } }),
  boombox: Object.assign({}, GUARD, { shoulderR: { x: 0.2, z: 0.3 }, elbowR: { x: 0.4 } }),
  scythe: Object.assign({}, GUARD, { waist: { x: 0.12, y: 0.5 }, shoulderR: { x: 0.6, z: 0.4 }, elbowR: { x: 1.0 }, shoulderL: { x: 1.0, z: 0.2 }, elbowL: { x: 1.2 } }),
  fork: Object.assign({}, GUARD, { waist: { x: 0.1, y: 0.45 }, shoulderR: { x: 0.65, z: 0.15 }, elbowR: { x: 1.1 }, shoulderL: { x: 1.0, z: 0.25 }, elbowL: { x: 1.2 } }),
  sub_fists: GUARD,
  sticks: Object.assign({}, GUARD, { shoulderR: { x: 0.75, z: 0.3 }, elbowR: { x: 1.3 }, shoulderL: { x: 0.8, z: -0.3 }, elbowL: { x: 1.35 } }),
  mic_stand: Object.assign({}, GUARD, { waist: { x: 0.1, y: 0.45 }, shoulderR: { x: 0.65, z: 0.15 }, elbowR: { x: 0.9 }, shoulderL: { x: 1.0, z: 0.25 }, elbowL: { x: 1.2 } }),
  guitar: Object.assign({}, GUARD, { shoulderR: { x: 1.1, z: 0.35 }, elbowR: { x: 2.3 }, shoulderL: { x: 0.7, z: -0.25 }, elbowL: { x: 1.8 } }),
  flail: Object.assign({}, GUARD, { shoulderR: { x: 0.55, z: 0.35 }, elbowR: { x: 1.0 }, shoulderL: { x: 0.65, z: -0.25 }, elbowL: { x: 1.9 } }),
};
// cible proche (monstre ou enceinte) : pour la garde, l'orientation et l'aide à la visée
function nearestFoe(maxDist, needFront = false) {
  const fwd = new THREE.Vector3(-Math.sin(cam.yaw), 0, -Math.cos(cam.yaw));
  let best = null, bd = maxDist;
  for (const e of sim.entities.values()) {
    if (e.team === 1 || !e.alive) continue;
    const to = e.position.clone().sub(P.pos); to.y = 0; const d = to.length();
    if (d >= bd || d < 0.01) continue;
    if (needFront && fwd.angleTo(to.divideScalar(d)) > 1.2) continue;
    bd = d; best = e;
  }
  return best;
}
function basePose() {
  const b = {}, set = (j, x = 0, y = 0, z = 0) => { b[j] = { x, y, z }; };
  const M = P.mods, run = Math.min(1, P.flatSpeed / 5), spr = P.sprint, bank = Math.max(-0.35, Math.min(0.35, -P.yawRate * 0.07));
  const guard = P.combat > 0.5 && P.onFloor && P.sprint < 0.3 && !P.aiming && !P.sliding;
  if (P.sliding) {
    // glissade : jambe d'appui tendue devant (et étirée), buste en arrière, bras qui traînent
    set("hipR", 1.45); b.hipR.s = 1.35; set("kneeR", -0.1); set("hipL", 0.45, 0, -0.1); set("kneeL", -1.9);
    set("waist", -0.55); set("chest", -0.2); set("neck", 0.55);
    set("shoulderR", -0.7, 0, 0.6); set("shoulderL", -0.7, 0, -0.6); set("elbowR", 0.4); set("elbowL", 0.4); b.shoulderR.s = b.shoulderL.s = 1.3;
    return b;
  }
  if (P.onFloor && P.flatSpeed > 0.3) {
    // foulée : la jambe suit la direction de la marche (en arrière si l'on recule en garde)
    const f = facing(), dirDot = (P.vel.x * f.x + P.vel.z * f.z) / Math.max(0.01, P.flatSpeed), side = (P.vel.x * -f.z + P.vel.z * f.x) / Math.max(0.01, P.flatSpeed);
    const s = Math.sin(P.phase), c = Math.cos(P.phase), amp = (0.3 + 0.5 * run) * (dirDot < -0.3 ? -1 : 1) * Math.max(0.35, Math.abs(dirDot)), kb = 0.15 + 1.1 * run;
    set("hipR", s * amp, 0, 0.04 + Math.max(0, side * s) * 0.3); set("hipL", -s * amp, 0, -0.04 + Math.min(0, side * s) * 0.3);
    set("kneeR", -kb * Math.max(0, c) - 0.08); set("kneeL", -kb * Math.max(0, -c) - 0.08);
    // bras : balancier opposé aux jambes, coudes de plus en plus pliés en courant
    set("shoulderR", -s * amp * (0.8 + 0.5 * spr), 0, 0.1); set("shoulderL", s * amp * (0.8 + 0.5 * spr), 0, -0.1);
    set("elbowR", 0.3 + 0.9 * run + 0.4 * spr); set("elbowL", 0.3 + 0.9 * run + 0.4 * spr);
    set("waist", 0.08 * run + 0.3 * spr, s * 0.18 * run, bank); set("chest", 0.04 * spr, -s * 0.22 * run, 0);
    set("neck", -0.08 * run - 0.25 * spr);
    // très vite : les jambes s'allongent à chaque foulée, les bras traînent derrière comme du liquide
    b.hipR.s = 1 + 0.25 * spr * Math.max(0, s); b.hipL.s = 1 + 0.25 * spr * Math.max(0, -s);
    if (spr > 0.3) { set("shoulderR", -1.1 * spr, 0, 0.35); set("shoulderL", -1.1 * spr, 0, -0.35); set("elbowR", 0.2); set("elbowL", 0.2); b.shoulderR.s = b.shoulderL.s = 1 + 0.4 * spr; }
  } else if (P.onFloor) {
    const br = Math.sin(P.t * 1.9);
    set("hipR", 0, 0, 0.04); set("hipL", 0, 0, -0.04); set("kneeR", -0.06); set("kneeL", -0.06);
    set("shoulderR", 0.05, 0, 0.1 + 0.03 * br); set("shoulderL", 0.05, 0, -0.1 - 0.03 * br);
    set("elbowR", 0.2); set("elbowL", 0.2);
    set("waist", 0.03 * br, Math.sin(P.t * 0.5) * 0.05, 0); set("chest", -0.02 * br); set("neck", -0.03 * br, Math.sin(P.t * 0.37) * 0.15);
    // Chorus : bras ouverts, tête levée, il « chante »
    if (hasStatus("chorus")) { set("shoulderR", 0.5, 0, 0.7); set("shoulderL", 0.5, 0, -0.7); set("elbowR", 1.0); set("elbowL", 1.0); set("neck", -0.35); }
    // essoufflé : mains sur les genoux
    if (M && M.tired) { set("waist", 0.55 + Math.sin(P.t * 7) * 0.06); set("shoulderR", 1.0, 0, 0.15); set("shoulderL", 1.0, 0, -0.15); set("elbowR", 0.25); set("elbowL", 0.25); set("hipR", 0.55); set("hipL", 0.55); set("kneeR", -0.7); set("kneeL", -0.7); set("neck", 0.25); }
  } else {
    const fall = P.vel.y < 0 ? 1 : 0;
    set("hipR", 0.7, 0, 0.05); set("kneeR", -1.2); set("hipL", -0.15, 0, -0.05); set("kneeL", -0.5);
    set("shoulderR", -0.3, 0, 0.7 + 0.4 * fall); set("shoulderL", -0.3, 0, -0.7 - 0.4 * fall);
    set("elbowR", 0.7); set("elbowL", 0.7); set("waist", 0.15, 0, bank); set("neck", 0.1 * fall);
    // chute rapide : les bras s'étirent vers le haut
    const st = Math.min(1, Math.max(0, -P.vel.y - 8) / 20); if (st > 0) { set("shoulderR", 2.6 * st, 0, 0.4); set("shoulderL", 2.6 * st, 0, -0.4); b.shoulderR.s = b.shoulderL.s = 1 + 0.5 * st; }
  }
  if (M && M.tired && P.onFloor && P.flatSpeed > 0.3) { b.waist.x += 0.35 + Math.sin(P.t * 7) * 0.07; b.neck.x += 0.2; }
  // garde de combat : le haut du corps se met en garde, les genoux rebondissent
  if (guard && !(M && M.tired)) {
    const g = WEAPON_GUARD[P.weapon] || GUARD, bounce = Math.sin(P.t * 7) * 0.05, moving = P.flatSpeed > 0.3;
    for (const [j, v] of Object.entries(g)) {
      if (moving && (j.startsWith("hip") || j.startsWith("knee"))) continue;
      const cur = b[j] || { x: 0, y: 0, z: 0 }; b[j] = { x: v.x ?? cur.x, y: (v.y ?? 0) + (j === "waist" ? cur.y * 0.4 : 0), z: v.z ?? cur.z };
    }
    if (!moving) { b.kneeR.x += -bounce - 0.05; b.kneeL.x += -bounce - 0.05; b.hipR.x += bounce * 0.5; b.hipL.x += bounce * 0.5; }
  } else if (P.weapon === "guitar" && P.onFloor) { set("shoulderR", 1.1, 0, 0.35); set("elbowR", 2.3); } // guitare posée sur l'épaule
  else if (P.weapon === "mic_stand" && P.onFloor && P.flatSpeed > 3) { set("shoulderR", 0.5, 0, 0.15); set("elbowR", 0.9); } // bâton tenu en courant
  // visée : bras droit tendu vers le viseur
  if (P.aiming) { set("shoulderR", 1.57 + cam.pitch, 0, 0.1); set("elbowR", 0.05); b.waist = b.waist || { x: 0, y: 0, z: 0 }; b.waist.y = 0.35; set("shoulderL", 0.6, 0, -0.25); set("elbowL", 1.6); }
  return b;
}
function guardBase() { const o = {}; for (const [j, v] of Object.entries(GUARD)) o[j] = { x: v.x || 0, y: v.y || 0, z: v.z || 0 }; return o; }
function actionPose(a) {
  const keys = a.keys, last = keys[keys.length - 1];
  let i = 0; while (i < keys.length - 1 && a.t >= keys[i + 1].t) i++;
  const A = keys[i], B = keys[Math.min(i + 1, keys.length - 1)];
  const f = B === A ? 1 : easeInOut(Math.min(1, (a.t - A.t) / Math.max(0.0001, B.t - A.t)));
  const out = {}, names = new Set([...Object.keys(A.p), ...Object.keys(B.p)]);
  for (const j of names) {
    out[j] = {};
    for (const ax of ["x", "y", "z", "s"]) { const va = A.p[j]?.[ax] ?? B.p[j]?.[ax], vb = B.p[j]?.[ax] ?? A.p[j]?.[ax]; if (va !== undefined) out[j][ax] = va + (vb - va) * f; }
  }
  const weight = a.t <= last.t ? 1 : Math.max(0, 1 - (a.t - last.t) / 0.15);
  return { out, k: (B !== A ? B.k : A.k) || 1, weight, done: a.t > last.t + 0.15 };
}
// Ressorts : chaque articulation rejoint sa cible avec un léger retard et un
// petit dépassement. Si la pose ne précise pas le buste, la rotation de la
// taille est partagée entre taille (55 %) et buste (45 %) : le haut du corps
// « suit » le bassin avec un temps de retard, comme un vrai corps.
function animateRig(rig, base, dt) {
  let act = null;
  if (rig.action) { rig.action.t += dt; act = actionPose(rig.action); if (act.done) rig.action = null; }
  const targets = {}, kMuls = {};
  for (const name of JOINTS) {
    const tgt = base[name] || { x: 0, y: 0, z: 0 }, target = { x: tgt.x || 0, y: tgt.y || 0, z: tgt.z || 0, s: tgt.s || 1 };
    const o = act && act.out[name];
    if (o) { for (const ax of ["x", "y", "z", "s"]) if (o[ax] !== undefined) target[ax] = target[ax] + (o[ax] - target[ax]) * act.weight; kMuls[name] = act.k; }
    targets[name] = target;
  }
  const chestSet = base.chest || (act && act.out.chest);
  const w = targets.waist, c = targets.chest;
  if (chestSet) { c.x += w.x * 0.4; c.y += w.y * 0.4; c.z += w.z * 0.4; w.x *= 0.6; w.y *= 0.6; w.z *= 0.6; }
  else { c.x = w.x * 0.45; c.y = w.y * 0.45; c.z = w.z * 0.45; w.x *= 0.55; w.y *= 0.55; w.z *= 0.55; kMuls.chest = kMuls.waist; }
  // la tête compense la rotation du buste (le regard reste vers l'avant)
  const wr = rig.J.waist.g.rotation, cr = rig.J.chest.g.rotation, n = targets.neck;
  n.y += -(wr.y + cr.y) * 0.5; n.z += -(wr.z + cr.z) * 0.6; n.x += -rig.J.waist.v.x * 0.02;
  const steps = dt > 1 / 50 ? 2 : 1, h = dt / steps;
  for (const name of JOINTS) {
    const jt = rig.J[name], target = targets[name], kMul = kMuls[name] || 1;
    const k = jt.k * kMul, cc = jt.c * Math.sqrt(kMul);
    for (let s = 0; s < steps; s++) for (const ax of ["x", "y", "z"]) { const r = jt.g.rotation; jt.v[ax] += (k * (target[ax] - r[ax]) - cc * jt.v[ax]) * h; r[ax] += jt.v[ax] * h; }
    // étirement du membre (corps semi-liquide) : ressort plus mou, qui rebondit
    if (rig.shape === "abrasion") {
      jt.s = jt.s ?? 1;
      for (let s = 0; s < steps; s++) { jt.vs += (k * 0.6 * (target.s - jt.s) - cc * 0.5 * jt.vs) * h; jt.s = Math.max(0.4, jt.s + jt.vs * h); }
      const st = rig.stretch[name];
      if (st) { st.mesh.scale.y = jt.s; st.mesh.position.y = st.meshY * jt.s; st.child.position.y = st.childY * jt.s; } else jt.g.scale.y = jt.s;
    }
  }
  rig.sqV += ((1 - rig.sq) * 260 - rig.sqV * (rig.shape === "abrasion" ? 9 : 14)) * dt; rig.sq += rig.sqV * dt;
  const lim = rig.shape === "abrasion" ? [0.4, 1.9] : [0.6, 1.4];
  const sq = Math.max(lim[0], Math.min(lim[1], rig.sq)), sm = 1 + (rig.smear || 0);
  const vs = 1 + (rig.vstretch || 0);
  rig.body.scale.set(1 / Math.sqrt(sq * vs), sq * vs, sm / Math.sqrt(sq * vs));
}
function squash(amount) { model.sqV += amount; }
function hasStatus(id) { const e = sim.entities.get(P.id); return !!(e && e.statuses.has(id)); }

// Allongé sur le dos (repos, K.O., projeté) : le corps pivote autour des pieds,
// on le relève de l'épaisseur du dos et on le recentre.
const LIE = { y: 0.19, z: -0.95 };
function lieDown() {
  P.lying = true; startAction(model, "lie");
  play(model.pose.rotation, [{ to: { x: 0 }, dur: 0.2 }, { to: { x: Math.PI / 2 }, dur: 0.9, ease: easeInOut }]);
  play(model.pose.position, [{ to: { y: 0.12 }, dur: 0.2 }, { to: { y: LIE.y, z: LIE.z }, dur: 0.9, ease: easeInOut }]);
}
function wakeUp() {
  if (!P.lying || P.dead || P.knock > 0) return;
  P.lying = false; P.idle = 0; P.waved = 0; model.action = null; squash(4);
  play(model.pose.rotation, [{ to: { x: 0 }, dur: 0.28, ease: easeBack }]);
  play(model.pose.position, [{ to: { y: 0.25, z: 0 }, dur: 0.18 }, { to: { y: 0 }, dur: 0.15 }]);
}
// Projeté au sol par un gros coup : chute sur le dos, puis il se relève d'un bond
function knockDown(dir) {
  P.knock = 1.0; P.lying = false; P.dodgeLeft = 0; P.lungeLeft = 0; P.diving = false; startAction(model, "knocked");
  if (dir) P.facingYaw = Math.atan2(dir.x, dir.z); // face à l'attaquant, il tombe en arrière
  play(model.pose.rotation, [{ to: { x: Math.PI / 2 }, dur: 0.32, ease: easeOut }, { to: { x: Math.PI / 2 }, dur: 0.45 }, { to: { x: 0 }, dur: 0.3, ease: easeBack }]);
  play(model.pose.position, [{ to: { y: 0.45, z: LIE.z * 0.5 }, dur: 0.16 }, { to: { y: LIE.y, z: LIE.z }, dur: 0.16 }, { to: { y: LIE.y, z: LIE.z }, dur: 0.45 }, { to: { y: 0, z: 0 }, dur: 0.3, ease: easeBack }]);
}
// ============================================================================
// Effets visuels
// ============================================================================
const ringGeo = new THREE.BoxGeometry(0.17, 0.05, 0.17);
const ringPool = [0, 1, 2, 3, 4, 5].map(() => {
  const g = new THREE.Group(), mat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, depthWrite: false, toneMapped: false });
  const cells = [];
  for (let i = 0; i < 32; i++) { const m = new THREE.Mesh(ringGeo, mat); g.add(m); cells.push({ m, a: (i / 32) * Math.PI * 2, h: Math.random() }); }
  g.visible = false; scene.add(g);
  return { g, mat, cells, life: 0, dur: 0.4, r0: 0.3, r1: 2.2 };
});
let ringIndex = 0;
function pixelRing(pos, r1 = 2.2, dur = 0.4, color = 0xbfe9ff, y = 0.08) {
  const r = ringPool[ringIndex++ % ringPool.length];
  r.g.position.copy(pos).add(new THREE.Vector3(0, y, 0)); r.life = dur; r.dur = dur; r.r1 = r1; r.mat.color.setHex(color); r.g.visible = true;
  for (const c of r.cells) c.h = Math.random();
}
function updateRings(dt) {
  for (const r of ringPool) {
    if (r.life <= 0) continue;
    r.life -= dt;
    const f = 1 - Math.max(0, r.life) / r.dur, rad = r.r0 + (r.r1 - r.r0) * easeOut(f);
    for (const c of r.cells) { c.m.position.set(Math.cos(c.a) * rad, c.h * 0.5 * (1 - f) + Math.sin(f * 3 + c.a * 5) * 0.05, Math.sin(c.a) * rad); const s = 1 + (1 - f) * 0.8 * c.h; c.m.scale.set(s, 1, s); }
    r.mat.opacity = Math.max(0, 1 - f);
    if (r.life <= 0) r.g.visible = false;
  }
}
function burst(pos, colors, speed = 3, count = 24) {
  for (let i = 0; i < Math.min(count, shards.length); i++) {
    const s = shards[i]; s.life = 0.45; s.m.visible = true; s.m.position.copy(pos);
    s.vel.set(range(-speed, speed), range(-speed * 0.4, speed * 0.8), range(-speed, speed)); s.m.material.color.setHex(pick(colors));
  }
}
// Gater : lamelles noires et blanches qui hachent l'air autour du bonhomme
const gateFx = new THREE.Group();
for (let i = 0; i < 12; i++) {
  const m = new THREE.Mesh(new THREE.PlaneGeometry(0.09, 2.1), new THREE.MeshBasicMaterial({ color: i % 2 ? 0x16121d : 0xf4f0ea, transparent: true, side: THREE.DoubleSide, depthWrite: false, toneMapped: false }));
  const a = (i / 12) * Math.PI * 2; m.position.set(Math.cos(a) * 0.95, 1.0, Math.sin(a) * 0.95); m.rotation.y = -a; gateFx.add(m);
}
gateFx.visible = false; scene.add(gateFx);
let gateFxLife = 0;
// Chorus : trois voix (orbes) qui tournent autour de lui
const chorusOrbs = [0, 1, 2].map((i) => { const m = new THREE.Mesh(new THREE.SphereGeometry(0.11, 12, 8), new THREE.MeshBasicMaterial({ color: 0x8ff0b0, transparent: true, opacity: 0.8, toneMapped: false })); m.visible = false; scene.add(m); return m; });
// Étoiles d'étourdissement au-dessus de l'enceinte
const stunStars = [0, 1, 2].map(() => { const m = new THREE.Mesh(new THREE.OctahedronGeometry(0.1), new THREE.MeshBasicMaterial({ color: 0xffe28a, toneMapped: false })); m.visible = false; scene.add(m); return m; });
// Signal d'attaque de l'enceinte (anneau rouge au sol pour le « boom »)
const warnRing = new THREE.Mesh(new THREE.RingGeometry(0.9, 1.0, 48), new THREE.MeshBasicMaterial({ color: 0xff4a3a, transparent: true, opacity: 0.6, side: THREE.DoubleSide, depthWrite: false }));
warnRing.rotation.x = -Math.PI / 2; warnRing.visible = false; scene.add(warnRing);
const WARN = { kind: null, t: 0, dur: 1 };

// Projectiles : chaque objet de la Sim a son apparence
const projMeshes = new Map();
const glowMat = (c, o = 1) => new THREE.MeshBasicMaterial({ color: c, transparent: o < 1, opacity: o, toneMapped: false });
function makeProjMesh(kind) {
  const g = new THREE.Group();
  if (kind === "sine") { for (let i = 0; i < 7; i++) { const s = new THREE.Mesh(new THREE.SphereGeometry(0.07 - i * 0.006, 8, 6), glowMat(0x8fe6ff)); g.add(s); } }
  else if (kind === "triangle") { const c = new THREE.Mesh(new THREE.ConeGeometry(0.2, 0.55, 3), glowMat(0xff9fc8)); c.rotation.x = -Math.PI / 2; g.add(c); }
  else if (kind === "soft") { g.add(new THREE.Mesh(new THREE.SphereGeometry(0.2, 12, 8), glowMat(0xa8ffd0, 0.85))); g.add(new THREE.Mesh(new THREE.SphereGeometry(0.34, 12, 8), glowMat(0xa8ffd0, 0.25))); }
  else if (kind === "saw") { const b = new THREE.Mesh(new THREE.BoxGeometry(0.07, 0.22, 0.6), glowMat(0xffb35c)); b.rotation.z = 0.5; g.add(b); }
  else if (kind === "spark") { const c = new THREE.Mesh(new THREE.OctahedronGeometry(0.16), glowMat(0xfff27a)); g.add(c); g.add(new THREE.Mesh(new THREE.SphereGeometry(0.3, 10, 8), glowMat(0x7ae8ff, 0.35))); g.userData.spin = c; }
  else if (kind === "sub") { g.add(new THREE.Mesh(new THREE.SphereGeometry(0.32, 14, 10), new THREE.MeshBasicMaterial({ color: 0x140a22 }))); const t = new THREE.Mesh(new THREE.TorusGeometry(0.45, 0.05, 6, 24), glowMat(0xb36bff, 0.9)); g.add(t); g.userData.ring = t; g.add(new THREE.Mesh(new THREE.SphereGeometry(0.55, 12, 8), glowMat(0x7a3dff, 0.18))); }
  else if (kind === "larsen") { g.add(new THREE.Mesh(new THREE.SphereGeometry(0.28, 14, 10), glowMat(0xff5a3a))); const t = new THREE.Mesh(new THREE.TorusGeometry(0.42, 0.04, 6, 24), glowMat(0xffb08a, 0.8)); g.add(t); g.userData.ring = t; }
  scene.add(g); return g;
}
function updateProjMeshes(dt) {
  for (const [id, mesh] of projMeshes) {
    const p = sim.projectiles.get(id);
    if (!p) { scene.remove(mesh); disposeObject(mesh); projMeshes.delete(id); continue; }
    mesh.position.copy(p.pos); mesh.lookAt(p.pos.clone().add(p.vel));
    if (p.kind === "sine") mesh.children.forEach((s, i) => s.position.set(Math.sin(time * 18 + p.phase - i * 0.7) * 0.22, 0, -i * 0.16 + 0.3));
    if (p.kind === "larsen") { mesh.userData.ring.rotation.x += dt * 6; const s = 1 + Math.sin(time * 20) * 0.12; mesh.scale.setScalar(s); }
    if (p.kind === "triangle") mesh.rotation.z += dt * 8;
    if (p.kind === "spark") { mesh.userData.spin.rotation.x += dt * 20; mesh.visible = Math.random() > 0.15; }
    if (p.kind === "sub") { mesh.userData.ring.rotation.y += dt * 4; mesh.scale.setScalar(1 + Math.sin(time * 9) * 0.15); }
  }
}
// Rayons des ondes carrées / PWM : tracé en créneaux
const beams = [];
function spawnBeam(from, to, wave) {
  const dir = to.clone().sub(from), len = dir.length(); dir.normalize();
  const side = new THREE.Vector3().crossVectors(dir, UP).normalize(), up = new THREE.Vector3().crossVectors(side, dir).normalize();
  const pts = [], amp = wave === "pwm" ? 0.45 : 0.22, step = wave === "pwm" ? 0.9 : 0.55;
  let hi = true;
  for (let s = 0; s <= len; s += step) { const a = from.clone().addScaledVector(dir, s), o = up.clone().multiplyScalar(hi ? amp : -amp); pts.push(a.clone().add(o)); pts.push(from.clone().addScaledVector(dir, Math.min(len, s + step)).add(o)); hi = !hi; pts.push(from.clone().addScaledVector(dir, Math.min(len, s + step)).add(o)); pts.push(from.clone().addScaledVector(dir, Math.min(len, s + step)).add(up.clone().multiplyScalar(hi ? amp : -amp))); }
  const color = wave === "pwm" ? 0x8fe6ff : 0xffffff;
  const line = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(pts), new THREE.LineBasicMaterial({ color, transparent: true, toneMapped: false }));
  const core = new THREE.Mesh(new THREE.BoxGeometry(wave === "pwm" ? 0.5 : 0.07, wave === "pwm" ? 0.5 : 0.07, len), glowMat(color, 0.8));
  core.position.copy(from).addScaledVector(dir, len / 2); core.lookAt(to);
  scene.add(line, core); beams.push({ line, core, life: 0.3 });
}
function updateBeams(dt) {
  for (let i = beams.length - 1; i >= 0; i--) {
    const b = beams[i]; b.life -= dt;
    b.line.material.opacity = b.core.material.opacity = Math.max(0, b.life / 0.3);
    if (b.life <= 0) { scene.remove(b.line, b.core); disposeObject(b.line); disposeObject(b.core); beams.splice(i, 1); }
  }
}
const FX = { freeze: 0, impact: 0, impactDur: 0.13, impactWorld: new THREE.Vector3() };
function hitFeedback(ev) {
  const te = sim.entities.get(ev.target), tpos = te ? te.position : dummy.pos;
  const contact = tpos.clone().add(new THREE.Vector3(0, 1.0, 0)).addScaledVector(ev.direction || new THREE.Vector3(), -0.35);
  FX.freeze = Math.max(FX.freeze, ev.hitstop || 0.04);
  burst(contact, ev.impact ? [0xffffff, 0xffe28a, 0xff9fc8, 0x9fe6ff] : ev.delay ? [0x8fe6ff, 0xffffff] : [0xffffff, 0xffe28a], ev.impact ? 5 : 3, ev.impact ? 24 : 10);
  if (ev.impact) {
    FX.impact = FX.impactDur; FX.impactWorld.copy(contact);
    pixelRing(tpos, 3.2, 0.5, 0xffe28a);
    tone(80, 30, 0.5, 0.45, "sine"); noiseHit(160, 0.7, 0.4, 0.4, "lowpass"); noiseHit(4200, 0.5, 0.2, 0.15, "highpass");
  } else { tone(150, 60, 0.15, 0.22, "sine"); noiseHit(380, 1, 0.1, 0.18); }
}

// ============================================================================
// Armes : changement (X ou molette), effets propres à certains coups, sons
// ============================================================================
function cycleWeapon(dir) {
  const pe = sim.entities.get(P.id), list = Object.keys(DATA.weapons).filter((w) => pe.weapons.has(w));
  if (list.length < 2 || P.dead) return;
  const i = list.indexOf(P.weapon);
  sim.queue({ type: "weapon", source: P.id, weapon: list[(i + dir + list.length) % list.length] });
}
function weaponMoveFx(ev) {
  const w = ev.windup;
  if (ev.move === "ms3" || ev.move === "sc3") spinBody(model, w + 0.36, ev.move === "sc3" ? 2 : 1);
  if (ev.move === "tw4") { spinBody(model, w + 0.3, 2); for (let i = 0; i < 4; i++) setTimeout(() => { hitSparks(P.pos.clone().add(new THREE.Vector3(0, 1.1, 0)), facing().applyAxisAngle(UP, i * 1.6), 6, [0xd07bff, 0xffffff]); noiseHit(4200, 3, 0.05, 0.12); }, (w + i * 0.07) * 1000); }
  if (ev.move === "sg3") { play(model.pose.position, [{ to: { y: -0.15 }, dur: w * 0.8 }, { to: { y: 0.6 }, dur: 0.12, ease: easeOut }, { to: { y: 0 }, dur: 0.3 }]); setTimeout(() => { groundImpact(P.pos, 1.0, 0xff9a5a); tone(60, 30, 0.4, 0.3, "sine"); }, w * 1000); }
  if (ev.move === "df3") setTimeout(() => { pixelRing(P.pos.clone().addScaledVector(facing(), 3), 2.4, 0.5, 0x8fe6ff, 1.1); tone(440, 440, 0.6, 0.08, "sine"); tone(443, 443, 0.6, 0.06, "sine"); }, w * 1000);
  if (ev.move === "bb3") {
    play(model.pose.position, [{ to: { y: 0.4 }, dur: w * 0.6, ease: easeOut }, { to: { y: 0 }, dur: w * 0.4, ease: (t) => t * t }]);
    // l'onde des basses : une ligne d'impacts qui part devant et fend le sol
    setTimeout(() => { if (P.dead) return; const f = facing(), o = P.pos.clone(); for (let i = 0; i < 5; i++) setTimeout(() => groundImpact(o.clone().addScaledVector(f, 1.2 + i * 1.4), 1.3 - i * 0.12, 0xff5a3a), i * 55); powerChord(); }, w * 1000);
  }
  if (ev.move === "st4") for (let i = 0; i < 5; i++) setTimeout(() => { noiseHit(2600 + i * 250, 4, 0.04, 0.13); tone(1900, 1500, 0.03, 0.03); }, (w + i * 0.06) * 1000);
  if (ev.move === "gt3") {
    // petit bond, puis la guitare frappe le sol : onde de choc
    play(model.pose.position, [{ to: { y: 0.4 }, dur: w * 0.6, ease: easeOut }, { to: { y: 0 }, dur: w * 0.4, ease: (t) => t * t }]);
    setTimeout(() => { if (P.dead) return; pixelRing(P.pos, 4.2, 0.55, 0xff7a5c); pixelRing(P.pos, 2.6, 0.4, 0xffe28a); puff(P.pos, 14, 3); powerChord(); }, w * 1000);
  }
  if (P.weapon === "flail") { P.flailAt = sim.time + w * 0.55; P.flailMove = ev.move; }
}
// le micro du fléau part vers l'ennemi visé (ou droit devant)
function flailThrow() {
  const m = DATA.moves[P.flailMove] || DATA.moves.fl1, foe = nearestFoe(m.range + 0.6, false);
  const f = facing(), to = foe && foe.position.clone().sub(P.pos).setY(0).normalize().dot(f) > 0.5 ? foe.position.clone().add(new THREE.Vector3(0, 1.0, 0)) : P.pos.clone().addScaledVector(f, m.range * 0.9).add(new THREE.Vector3(0, 1.1, 0));
  throwFlail(to, P.flailMove === "fl3" ? 0.16 : 0.12);
}
const SWING = {
  fists: () => noiseHit(2400, 0.6, 0.09, 0.08, "highpass"),
  sticks: () => { noiseHit(3600, 3, 0.05, 0.1); tone(1800, 1500, 0.03, 0.03); },
  mic_stand: () => { noiseHit(900, 0.8, 0.2, 0.13); tone(300, 200, 0.15, 0.02, "sine"); },
  guitar: () => { noiseHit(500, 0.6, 0.26, 0.16); tone(110, 82, 0.3, 0.06, "sawtooth"); },
  flail: () => { noiseHit(1400, 0.5, 0.22, 0.1); tone(700, 1500, 0.14, 0.03, "sine"); },
  twisted_sword: () => { noiseHit(5200, 1.5, 0.12, 0.12, "highpass"); tone(1800, 900, 0.08, 0.03, "sawtooth"); },
  boombox: () => { noiseHit(400, 0.6, 0.28, 0.16); tone(55, 45, 0.3, 0.1, "sine"); },
  scythe: () => { noiseHit(2200, 0.6, 0.25, 0.12); tone(660, 330, 0.2, 0.03, "triangle"); },
  fork: () => { noiseHit(4000, 2, 0.08, 0.1, "highpass"); tone(880, 880, 0.15, 0.03, "sine"); },
  sub_fists: () => { noiseHit(300, 0.8, 0.12, 0.14); tone(80, 50, 0.15, 0.1, "sine"); },
};
function swingSound(w) { (SWING[w] || SWING.fists)(); }
function powerChord() { for (const f of [82.4, 123.5, 164.8, 207.7]) tone(f, f * 0.98, 0.7, 0.06, "sawtooth"); noiseHit(150, 0.7, 0.5, 0.35, "lowpass"); }

// --- Progression sauvegardée (armes reçues, monstres vaincus, quêtes) ----------------
const PROGRESS_KEY = "twisted.progress1";
const PROGRESS = { weapons: [], kills: {}, quests: {}, vinyl: false };
function loadProgress() {
  try { Object.assign(PROGRESS, JSON.parse(localStorage.getItem(PROGRESS_KEY) || "{}")); } catch (e) { /* pas de sauvegarde */ }
  const pe = sim.entities.get(P.id); for (const w of PROGRESS.weapons || []) if (DATA.weapons[w]) pe.weapons.add(w);
}
function saveProgress() {
  PROGRESS.weapons = [...sim.entities.get(P.id).weapons];
  try { localStorage.setItem(PROGRESS_KEY, JSON.stringify(PROGRESS)); } catch (e) { /* pas de stockage */ }
}
loadProgress();
const killCount = (kind) => (kind ? PROGRESS.kills[kind] || 0 : Object.values(PROGRESS.kills).reduce((a, b) => a + b, 0));

// ============================================================================
// Événements de la Sim -> affichage
// ============================================================================
const floats = [];
function floatText(text, at, cls = "float") { const el = document.createElement("div"); el.className = cls; el.textContent = text; document.body.appendChild(el); floats.push({ el, pos: at.clone().add(new THREE.Vector3(range(-0.3, 0.3), 0, range(-0.3, 0.3))), age: 0, life: cls === "float" ? 0.8 : 1.4 }); }
let hitFlash = 0;
// aide à la visée : le coup part vers l'ennemi devant soi (portée de l'arme + un peu)
function softAim() {
  const wpn = DATA.weapons[P.weapon], reach = Math.max(4.5, DATA.moves[wpn.combo].range + 1.5);
  const t = nearestFoe(reach, true); if (!t) return cam.yaw;
  const to = t.position.clone().sub(P.pos); return Math.atan2(-to.x, -to.z);
}
function startDodge(distance, duration) {
  wakeUp();
  const dir = P.wish.lengthSq() > 0.01 ? P.wish.clone().normalize() : facing();
  P.dodgeAir = !P.onFloor; P.sliding = false;
  P.dodgeVel.copy(dir).multiplyScalar(distance / duration); P.dodgeLeft = duration; P.dodgeDur = duration; P.dodgeDist = distance;
  P.facingYaw = Math.atan2(-dir.x, -dir.z); P.idle = 0; P.lungeLeft = 0; startAction(model, "dodge", duration);
  pixelRing(P.pos, 2.4, 0.4, 0xbfe9ff); burst(P.pos.clone().add(new THREE.Vector3(0, 0.6, 0)), [0x9fe6ff, 0xff9fc8, 0xffffff], 2.5, 14);
  PIX.crush = duration + 0.06; ghostRequest = true; P.ghostTimes = [0.08, 0.16];
  tone(1400, 180, 0.2, 0.06, "square"); noiseHit(3000, 0.5, 0.15, 0.08, "highpass");
}
const DELAY_GHOST = { until: -1, queue: [], target: null, side: new THREE.Vector3() };
const SPELL_SOUND = {
  reverb: () => { [0, 120, 240, 360].forEach((d, i) => setTimeout(() => tone(660, 640, 0.3, 0.05 / (i + 1), "sine"), d)); },
  distortion: () => { tone(110, 90, 0.4, 0.12, "sawtooth"); noiseHit(1800, 0.3, 0.3, 0.1, "bandpass"); },
  chorus: () => { tone(523, 523, 0.6, 0.05, "triangle"); tone(527, 527, 0.6, 0.05, "triangle"); tone(784, 784, 0.6, 0.03, "sine"); },
  saturation: () => { tone(220, 880, 0.35, 0.08, "sawtooth"); },
};
function onEvent(ev) {
  const me = ev.source === P.id;
  switch (ev.type) {
    case "dodge_started": if (me) startDodge(ev.distance, ev.duration); break;
    case "triple_dodge": if (me) P.extraDodges = ev.count; break;
    case "move_started":
      if (!me) break;
      wakeUp(); P.idle = 0;
      if (ev.move === "dive") { P.diving = true; P.sliding = false; startAction(model, "dive"); P.vel.set(0, -ev.dive_speed, 0); squash(5); tone(900, 200, 0.25, 0.07, "sawtooth"); break; }
      if (!P.onFloor) P.vel.y = Math.max(P.vel.y, 2.5); // coups en l'air : on reste suspendu un instant
      if (ev.move === "roll") {
        // roulade avant : on garde l'élan de la glissade, le corps se roule en boule puis se relève
        const d = P.slideDir ? P.slideDir.clone() : facing(), v = Math.max(P.flatSpeed, 13);
        P.sliding = false; P.rollT = 0.5; P.vel.x = d.x * v; P.vel.z = d.z * v; P.facingYaw = Math.atan2(-d.x, -d.z);
        model.flip.rotation.x = 0; play(model.flip.rotation, [{ to: { x: -Math.PI * 2 }, dur: 0.42, ease: easeInOut }], () => { model.flip.rotation.x = 0; });
        play(model.pose.position, [{ to: { y: -0.45 }, dur: 0.12 }, { to: { y: -0.45 }, dur: 0.2 }, { to: { y: 0 }, dur: 0.18, ease: easeBack }]);
        startAction(model, "roll", ev.windup); squash(-3); noiseHit(900, 0.6, 0.3, 0.14); styleAdd(6, "ROULADE");
        setTimeout(() => { if (!P.dead) groundImpact(P.pos, 0.8, 0xd07bff); }, ev.windup * 1000);
        break;
      }
      P.facingYaw = softAim(); startAction(model, animOf(ev.move), ev.windup); weaponMoveFx(ev);
      P.lungeLeft = P.lungeTotal = ev.ghost ? 0.34 : ev.windup + 0.12; P.lungeSpeed = ev.lunge; P.attackSlow = ev.windup + ev.recover * 0.6;
      if (ev.ghost) P.ghost = ev.windup + 0.3;
      if (ev.move === "drop") { squash(-2.5); noiseHit(900, 0.6, 0.3, 0.12, "lowpass"); setTimeout(() => noiseHit(2200, 0.5, 0.12, 0.14, "highpass"), ev.windup * 1000); }
      else if (!DATA.moves[ev.move].hits) swingSound(P.weapon);
      break;
    case "damage":
      if (MONSTERS.has(ev.target)) {
        const top = monsterTop(ev.target);
        floatText(String(ev.amount), top, ev.dot ? "float dot" : ev.delay ? "float delay" : "float");
        if (ev.dot) { pixelRing(sim.entities.get(ev.target).position, 1.6, 0.5, 0xb9a6ff, 1.0); break; }
        const label = (DATA.moves[ev.move] && DATA.moves[ev.move].label) || (ev.wave === "square" ? "SNIPE!" : null);
        if (label && !ev.delay && (ev.hit || 0) === 0) floatText(label, top.clone().add(new THREE.Vector3(0, 0.5, 0)), "float big");
        monsterOnDamage(ev); hitFeedback(ev);
        // comme dans Ultrakill : frapper de près soigne (on se baigne dans les éclaboussures)
        if (ev.source === P.id) {
          styleAdd(ev.amount * 0.5);
          const te = sim.entities.get(ev.target), pe = sim.entities.get(P.id);
          if (te && pe && te.position.distanceTo(P.pos) < 4.5 && !ev.dot) sim.heal(pe, Math.round(ev.amount * 0.35));
        }
      } else if (ev.target === dummy.id) {
        setDummyHealth(ev.health, ev.max_health);
        const top = dummy.pos.clone().add(new THREE.Vector3(0, 1.9, 0));
        floatText(String(ev.amount), top, ev.dot ? "float dot" : ev.delay ? "float delay" : "float");
        if (ev.dot) { pixelRing(dummy.pos, 1.6, 0.5, 0xb9a6ff, 1.0); break; }
        const label = (DATA.moves[ev.move] && DATA.moves[ev.move].label) || (ev.wave === "square" ? "SNIPE!" : null);
        if (label && !ev.delay) floatText(label, top.clone().add(new THREE.Vector3(0, 0.5, 0)), "float big");
        hitDummy(ev); hitFeedback(ev);
      } else if (ev.target === P.id) {
        hitFlash = 1; setLife(ev.health, ev.max_health);
        floatText("-" + ev.amount, P.pos.clone().add(new THREE.Vector3(0, 2.0, 0)), "float hurt");
        tone(220, 90, 0.2, 0.18, "square"); squash(-2); if (ev.attack) styleHit();
        if (ev.direction && ev.push) { P.vel.x += ev.direction.x * ev.push; P.vel.z += ev.direction.z * ev.push; }
        // réaction : il encaisse, ou il est projeté au sol par les gros coups
        if (ev.knockdown && !P.dead) { knockDown(ev.direction); P.vel.y = 4; P.onFloor = false; noiseHit(180, 0.8, 0.3, 0.3, "lowpass"); FX.freeze = Math.max(FX.freeze, 0.08); }
        else if (!P.dead && P.knock <= 0 && P.dodgeLeft <= 0) { startAction(model, "hurt"); if (ev.direction) P.facingYaw = lerpAngle(P.facingYaw, Math.atan2(ev.direction.x, ev.direction.z), 0.6); }
        if (ev.attack) FX.freeze = Math.max(FX.freeze, 0.05);
      }
      break;
    case "heal": if (ev.id === P.id) { setLife(ev.health, ev.max_health); P.healAcc += ev.amount; } break;
    case "dive_landed": break;
    case "died":
      if (ev.id === dummy.id) { dummy.target = -1.45; showBanner("K.O."); }
      if (MONSTERS.has(ev.id)) monsterDied(ev);
      if (ev.id === P.id) playerDied();
      break;
    case "revived": case "healed":
      if (ev.id === dummy.id) { setDummyHealth(ev.health, ev.max_health); if (ev.type === "revived") dummy.target = 0; }
      if (ev.id === P.id) setLife(ev.health, ev.max_health);
      break;
    case "spell_cast":
      if (!me) break;
      wakeUp();
      if (ev.spell === "gater") { startAction(model, "gate"); gateFxLife = ev.window; gateFx.visible = true; noiseHit(1200, 2, 0.05, 0.1); }
      else { startAction(model, "cast"); pixelRing(P.pos, 1.8, 0.45, new THREE.Color(DATA.spells[ev.spell].color).getHex()); (SPELL_SOUND[ev.spell] || (() => {}))(); showBanner(DATA.spells[ev.spell].name, 0.9); }
      break;
    case "spell_not_ready": if (me) { flashSlot(ev.spell); tone(180, 160, 0.06, 0.05, "square"); } break;
    case "spell_fizzle": if (me) showBanner("Delay raté : lance-le juste après un combo", 1.8); break;
    case "chorus_broken": if (ev.id === P.id) showBanner("Chorus coupé : tu as attaqué", 1.4); break;
    case "gate_success":
      if (ev.id === P.id) {
        floatText(ev.perfect ? "GATE PARFAIT !" : "GATE", P.pos.clone().add(new THREE.Vector3(0, 2.3, 0)), "float big");
        burst(P.pos.clone().add(new THREE.Vector3(0, 1.1, 0)), [0xffffff, 0x16121d], 4, 24); FX.freeze = Math.max(FX.freeze, 0.08);
        // le son de l'attaque est haché puis coupé net
        for (let i = 0; i < 6; i++) setTimeout(() => noiseHit(700, 1, 0.025, 0.25 - i * 0.03), i * 45);
      }
      break;
    case "delay_ghost": {
      if (!me) break;
      const t = sim.entities.get(ev.target); if (!t) break;
      const toP = ev.from.clone().sub(t.position).setY(0).normalize();
      DELAY_GHOST.target = t.id; DELAY_GHOST.side.set(-toP.z, 0, toP.x);
      DELAY_GHOST.queue = ev.moves.map((m, i) => ({ at: sim.time + 0.45 + i * 0.32 - 0.1, move: DATA.moves[m] ? m : "jab2" }));
      DELAY_GHOST.until = sim.time + 0.45 + ev.moves.length * 0.32 + 0.5;
      ghostRig.root.visible = true; startAction(model, "cast"); showBanner("Delay : double fantôme", 1.0);
      tone(600, 600, 0.12, 0.05, "triangle"); setTimeout(() => tone(600, 600, 0.12, 0.03, "triangle"), 250);
      break;
    }
    case "fusion":
      if (!me) break;
      showBanner(ev.name, 1.3); pixelRing(P.pos, 3, 0.5, 0xffe28a); burst(P.pos.clone().add(new THREE.Vector3(0, 1, 0)), [0xffe28a, 0xff9fc8, 0x9fe6ff], 3, 16);
      [392, 523, 659].forEach((f, i) => setTimeout(() => tone(f, f, 0.12, 0.05, "triangle"), i * 60));
      if (ev.effect !== "wave") startAction(model, "cast");
      break;
    case "fusion_unknown": if (me) showBanner(`Pas de fusion ${nameOf(ev.a)} + ${nameOf(ev.b)}`, 1.4); break;
    case "fusion_refused": if (me) showBanner(`${ev.name} : ${ev.reason}`, 1.6); break;
    case "teleport":
      if (!me) break;
      burst(P.pos.clone().add(new THREE.Vector3(0, 1, 0)), [0x9fe6ff, 0xffffff], 3, 12); ghostRequest = true;
      P.pos.copy(findFreeSpot(ev.to)); P.vel.set(0, 0, 0);
      P.facingYaw = Math.atan2(-(ev.face.x - P.pos.x), -(ev.face.z - P.pos.z)); cam.yaw = P.facingYaw;
      PIX.crush = 0.25; pixelRing(P.pos, 2, 0.4, 0x9fe6ff); tone(1800, 300, 0.15, 0.06, "square");
      break;
    case "destabilize": if (me) { pixelRing(P.pos, ev.radius, 0.6, 0x8ff0b0); pixelRing(P.pos, ev.radius * 0.6, 0.5, 0xff9fc8); dummy.tiltVel -= 6; tone(300, 120, 0.4, 0.1, "sawtooth"); } break;
    case "reflect": if (ev.id === P.id) floatText("RENVOI", P.pos.clone().add(new THREE.Vector3(0, 2.2, 0)), "float big"); break;
    case "all_in_lost": if (ev.id === P.id) showBanner("Quitte ou double… perdu", 2.2); break;
    case "all_in_won": if (ev.id === P.id) showBanner("Quitte ou double : gagné !", 2.2); break;
    case "status_added":
      if (ev.id === P.id && ev.status === "all_in") showBanner("QUITTE OU DOUBLE : 10 s pour l'abattre", 2);
      if (ev.id === P.id && ev.status.startsWith("fatigue")) showBanner("Essoufflé…", 1.2);
      break;
    case "wave_fired":
      if (!me) break;
      startAction(model, "shoot");
      if (ev.wave === "sine") { tone(300, 600, 0.18, 0.06, "sine"); noiseHit(900, 0.5, 0.12, 0.08); }
      else if (ev.wave === "square") { tone(880, 110, 0.25, 0.08, "square"); }
      else if (ev.wave === "triangle") tone(700, 900, 0.1, 0.05, "triangle");
      else if (ev.wave === "pwm") tone(220, 110, 0.4, 0.1, "square");
      break;
    case "beam": spawnBeam(ev.from, ev.to, ev.wave); burst(ev.to, [0xffffff, 0x8fe6ff], 2, 8); break;
    case "projectile_spawned": projMeshes.set(ev.id, makeProjMesh(ev.kind)); break;
    case "projectile_end": burst(ev.pos, ev.kind === "larsen" ? [0xff5a3a, 0xffb08a] : ev.kind === "spark" ? [0xfff27a, 0x7ae8ff] : ev.kind === "sub" ? [0x7a3dff, 0x140a22] : [0xffffff, 0x8fe6ff, 0xff9fc8], 2, 6); break;
    case "weapon_changed":
      if (!me) break;
      P.weapon = ev.weapon; setRigWeapon(model, ev.weapon); setFlailVisible(ev.weapon === "flail"); startAction(model, "draw");
      showBanner(DATA.weapons[ev.weapon].name, 0.9); updateWeaponSlot();
      burst(model.handR.getWorldPosition(new THREE.Vector3()), [0xffffff, new THREE.Color(DATA.weapons[ev.weapon].color).getHex()], 1.5, 8);
      tone(500, 900, 0.08, 0.05, "triangle"); noiseHit(3000, 1, 0.06, 0.06, "highpass");
      break;
    case "weapon_unlocked":
      if (!me) break;
      saveProgress(); showBanner(`Nouvelle arme : ${DATA.weapons[ev.weapon].name} · X ou molette pour changer`, 3);
      [392, 523, 659, 784].forEach((f, i) => setTimeout(() => tone(f, f, 0.14, 0.06, "triangle"), i * 80));
      sim.queue({ type: "weapon", source: P.id, weapon: ev.weapon });
      break;
    case "enemy_interrupt": case "enemy_blink": if (MONSTERS.has(ev.id)) monsterEvent(ev); break;
    case "enemy_windup":
      if (MONSTERS.has(ev.id)) { monsterEvent(ev); break; }
      if (ev.id === dummy.id) { WARN.kind = ev.attack; WARN.t = 0; WARN.dur = ev.windup; tone(ev.attack === "boom" ? 120 : 500, ev.attack === "boom" ? 180 : 900, ev.windup, 0.05, "sine"); }
      break;
    case "enemy_attack":
      if (MONSTERS.has(ev.id)) { monsterEvent(ev); break; }
      if (ev.id === dummy.id) {
        WARN.kind = null; dummy.sqV -= 4;
        if (ev.attack === "boom") { pixelRing(dummy.pos, ev.radius, 0.35, 0xff4a3a); tone(90, 40, 0.3, 0.3, "sine"); }
        else noiseHit(2600, 3, 0.2, 0.12);
      }
      break;
  }
}
function setDummyHealth(h, max) { document.getElementById("dummyFill").style.width = (100 * h) / max + "%"; }
function setLife(h, max) { document.getElementById("lifeFill").style.width = (100 * h) / max + "%"; document.getElementById("lifeText").textContent = `Vie ${Math.round(h)} / ${max}`; }
let bannerTimer = 0;
function showBanner(text, dur = 1.6) { const b = document.getElementById("banner"); b.textContent = text; b.style.opacity = 1; bannerTimer = dur; }
function playerDied() {
  P.dead = true; P.lying = true; startAction(model, "lie");
  play(model.pose.rotation, [{ to: { x: Math.PI / 2 }, dur: 0.4, ease: easeOut }]); play(model.pose.position, [{ to: { y: LIE.y, z: LIE.z }, dur: 0.4 }]);
  showBanner("K.O. — retour dans 2 s", 2);
  setTimeout(() => {
    P.dead = false; P.lying = false; P.knock = 0; model.action = null; model.pose.rotation.x = 0; model.pose.position.set(0, 0, 0);
    P.pos.copy(SPAWN); P.vel.set(0, 0, 0); sim.queue({ type: "respawn", source: P.id });
  }, 2000);
}
function findFreeSpot(p) {
  const tries = [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1], [1.5, 1.5], [-1.5, -1.5], [1.5, -1.5], [-1.5, 1.5]];
  for (const [dx, dz] of tries) { const q = new THREE.Vector3(p.x + dx, Math.max(0, p.y), p.z + dz); if (!overlapsAny(q.x, q.y + 0.05, q.z)) { q.y = Math.max(q.y, 0); return q; } }
  return P.pos.clone();
}

// --- Réaction et physique de l'enceinte -----------------------------------------
function hitDummy(ev) {
  const d = dummy, dir = ev.direction || new THREE.Vector3(1, 0, 0);
  d.lastHit = sim.time; coneMat.emissiveIntensity = 3;
  d.root.rotation.y = Math.atan2(-dir.x, -dir.z);
  if (ev.launch) { d.vel.set(dir.x * ev.launch[0], ev.launch[1], dir.z * ev.launch[0]); d.grounded = false; d.tiltVel = -15; }
  else { d.vel.x += dir.x * (ev.push || 1) * 2.2; d.vel.z += dir.z * (ev.push || 1) * 2.2; d.vel.y += 1.5; d.grounded = false; d.tiltVel -= 5; }
  d.sqV -= ev.launch ? 5 : 3;
}
function dummyGround(x, z, y) { let top = 0; for (const b of colliders) if (!b.dummy && x > b.min[0] && x < b.max[0] && z > b.min[2] && z < b.max[2] && b.max[1] <= y + 0.3 && b.max[1] > top) top = b.max[1]; return top; }
function stepDummy(dt) {
  const d = dummy;
  if (!d.grounded) {
    d.vel.y -= 22 * dt;
    const next = d.pos.clone().addScaledVector(d.vel, dt);
    for (const b of colliders) {
      if (b.dummy || b.max[1] - b.min[1] < 1.0) continue;
      if (next.x > b.min[0] - 0.45 && next.x < b.max[0] + 0.45 && next.z > b.min[2] - 0.45 && next.z < b.max[2] + 0.45 && next.y < b.max[1] && next.y + 1.7 > b.min[1]) {
        const fromX = d.pos.x <= b.min[0] - 0.45 || d.pos.x >= b.max[0] + 0.45;
        if (fromX) { d.vel.x *= -0.5; next.x = d.pos.x; } else { d.vel.z *= -0.5; next.z = d.pos.z; }
        d.tiltVel *= -0.6; puff(next.clone().add(new THREE.Vector3(0, 1, 0)), 6, 1.5); noiseHit(200, 1, 0.15, 0.25);
      }
    }
    const g = dummyGround(next.x, next.z, d.pos.y);
    if (next.y <= g) {
      next.y = g;
      if (d.vel.y < -5) { d.vel.y *= -0.38; d.vel.x *= 0.7; d.vel.z *= 0.7; puff(next, 6, 1.4); noiseHit(260, 1, 0.12, 0.2); d.sqV -= 3; }
      else { d.vel.set(0, 0, 0); d.grounded = true; }
    }
    d.pos.copy(next);
  } else if (sim.entities.get(d.id).alive && sim.time - d.lastHit > 2.5 && d.pos.distanceTo(DUMMY_HOME) > 0.3 && !(sim.entities.get(d.id).combatUntil > sim.time)) {
    const next = d.pos.clone().lerp(DUMMY_HOME, 1 - Math.exp(-1.6 * dt)); next.y = dummyGround(next.x, next.z, d.pos.y + 0.5); d.pos.copy(next);
  }
  // en combat, l'enceinte se tourne vers le joueur
  const de = sim.entities.get(d.id);
  if (de.combatUntil > sim.time && de.alive && d.grounded) { const to = P.pos.clone().sub(d.pos); d.root.rotation.y = lerpAngle(d.root.rotation.y, Math.atan2(to.x, to.z), 1 - Math.exp(-6 * dt)); }
  if (d.grounded) { const tgt = d.target + Math.PI * 2 * Math.round((d.tilt - d.target) / (Math.PI * 2)); d.tiltVel += ((tgt - d.tilt) * 120 - d.tiltVel * 9) * dt; }
  d.tilt += d.tiltVel * dt;
  d.sqV += ((1 - d.sq) * 220 - d.sqV * 10) * dt; d.sq += d.sqV * dt;
  const sq = Math.max(0.6, Math.min(1.4, d.sq));
  d.root.position.copy(d.pos); d.visual.rotation.x = d.tilt; d.visual.scale.set(1 / Math.sqrt(sq), sq, 1 / Math.sqrt(sq));
  updateDummyCollider();
  sim.setTransform(d.id, d.pos, null);
}

// ============================================================================
// Physique du joueur : collisions, sauts, wall jump, esquives, sorts
// ============================================================================
const solid = (b) => !(b.dummy && (P.ghost > 0 || (P.mods && P.mods.phase)));
function overlaps(b, px, py, pz) { return px + R > b.min[0] && px - R < b.max[0] && pz + R > b.min[2] && pz - R < b.max[2] && py + H > b.min[1] && py < b.max[1]; }
function overlapsAny(px, py, pz) { for (const b of colliders) if (solid(b) && overlaps(b, px, py, pz)) return true; return false; }
function moveAxis(ax, delta) {
  if (!delta) return;
  const key = ax === 0 ? "x" : "z";
  P.pos[key] += delta;
  for (const b of colliders) {
    if (!solid(b) || !overlaps(b, P.pos.x, P.pos.y, P.pos.z)) continue;
    const rise = b.max[1] - P.pos.y;
    if (rise > 0 && rise <= STEP_UP && (P.onFloor || P.coyote > 0)) { const oldY = P.pos.y; P.pos.y = b.max[1]; if (!overlapsAny(P.pos.x, P.pos.y, P.pos.z)) continue; P.pos.y = oldY; }
    P.pos[key] = delta > 0 ? b.min[ax] - R - 1e-4 : b.max[ax] + R + 1e-4;
    P.vel[key] = 0;
  }
}
function probeWall() {
  for (const [dx, dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
    const px = P.pos.x + dx * 0.14, pz = P.pos.z + dz * 0.14;
    for (const b of colliders) if (!b.dummy && b.max[1] > P.pos.y + 0.7 && b.max[1] - b.min[1] > 1.5 && overlaps(b, px, P.pos.y + 0.3, pz) && !overlaps(b, P.pos.x, P.pos.y + 0.3, P.pos.z)) return { normal: new THREE.Vector3(-dx, 0, -dz), chain: b.chain };
  }
  return null;
}
function groundUnder() { let top = 0; for (const b of colliders) if (P.pos.x > b.min[0] && P.pos.x < b.max[0] && P.pos.z > b.min[2] && P.pos.z < b.max[2] && b.max[1] <= P.pos.y + 0.05 && b.max[1] > top) top = b.max[1]; return top; }
// Viseur : rayon depuis la caméra vers le centre de l'écran
function aimShot() {
  const o = camera.position.clone(), d = new THREE.Vector3(0, 0, -1).applyQuaternion(camera.quaternion);
  let t = 80; for (const c of colliders) if (c.cam) t = Math.min(t, rayBox([o.x, o.y, o.z], [d.x, d.y, d.z], 80, c));
  const target = o.clone().addScaledVector(d, Math.max(2, t));
  const right = new THREE.Vector3(Math.cos(P.facingYaw), 0, -Math.sin(P.facingYaw));
  const origin = P.pos.clone().add(new THREE.Vector3(0, 1.3, 0)).addScaledVector(facing(), 0.45).addScaledVector(right, 0.25);
  return { origin, dir: target.sub(origin).normalize() };
}
// Touches de sorts : C esquive, A E sorts, R T sorts fusionnés (visés au centre de l'écran), 1 2 3 ondes.
function readSpellKeys() {
  if (pressed.has("ShiftLeft") || pressed.has("ShiftRight")) { if (!P.mods.no_dodge) sim.queue({ type: "dodge", source: P.id }); else flashSlot("bitcrush"); }
  SLOTS.forEach((s, i) => {
    if (!pressed.has(s.code)) return;
    if (s.kind === "spell") sim.queue({ type: "cast", source: P.id, spell: EQUIP[i] });
    else { const a = aimShot(); sim.queue({ type: "fuse", source: P.id, fusion: EQUIP[i], origin: a.origin, dir: a.dir }); }
  });
  for (const [code, w] of Object.entries(WAVE_KEYS)) if (pressed.has(code)) { P.wave = w; tone(1000, 1000, 0.03, 0.03, "sine"); }
}

function physicsStep(dt) {
  const pe = sim.entities.get(P.id);
  P.mods = sim.mods(pe);
  const M = P.mods;
  const fwd = (keys.has("KeyW") ? 1 : 0) - (keys.has("KeyS") ? 1 : 0), right = (keys.has("KeyD") ? 1 : 0) - (keys.has("KeyA") ? 1 : 0);
  const c = Math.cos(cam.yaw), s = Math.sin(cam.yaw), iz = -fwd;
  P.wish.set(right * c + iz * s, 0, -right * s + iz * c);
  if (P.wish.length() > 1) P.wish.normalize();
  if (P.dead || P.knock > 0 || P.talking) { P.wish.set(0, 0, 0); if (!P.talking) pressed.delete("KeyF"); for (const k of [...pressed]) if (k !== "KeyF") pressed.delete(k); attackClicks = 0; }
  P.knock = Math.max(0, P.knock - dt);
  if (pressed.has("KeyF")) interact();
  if (pressed.has("KeyX")) cycleWeapon(1);
  // mode combat : un ennemi proche et actif
  P.combat += ((sim.inCombat(pe) && nearestFoe(12) ? 1 : 0) - P.combat) * Math.min(1, dt * 4);
  const moving = P.wish.lengthSq() > 0.01;
  if (moving || pressed.has("Space") || pressed.has("KeyC") || pressed.has("ShiftLeft")) wakeUp();

  // course rapide permanente (pas de marche lente) : Maj sert au dash
  let speed = STATS.walk_speed * M.speed;
  if (P.aiming) speed *= 0.75;
  P.attackSlow = Math.max(0, P.attackSlow - dt); if (P.attackSlow > 0) speed *= 0.7;
  // C : glissade au sol, écrasement en l'air
  if (pressed.has("KeyC") && !P.onFloor && !P.diving && !P.dead && P.knock <= 0 && P.pos.y - groundUnder() > 0.8) { P.slamFrom = P.pos.y; sim.queue({ type: "attack", source: P.id, airborne: true }); }
  P.rollT = Math.max(0, (P.rollT || 0) - dt);
  const wantSlide = keys.has("KeyC") && P.rollT <= 0 && P.onFloor && !P.diving && P.dodgeLeft <= 0 && !P.dead && P.knock <= 0;
  if (wantSlide && !P.sliding) {
    const dir = P.flatSpeed > 3 ? new THREE.Vector3(P.vel.x, 0, P.vel.z).normalize() : moving ? P.wish.clone().normalize() : facing();
    P.sliding = true; P.slideDir = dir; P.slideSpeed = Math.max(STATS.slide_speed, P.flatSpeed); P.facingYaw = Math.atan2(-dir.x, -dir.z);
    noiseHit(2600, 0.6, 0.3, 0.12, "highpass"); squash(-2);
  }
  if (P.sliding && !wantSlide) P.sliding = false;
  P.ghost = Math.max(0, P.ghost - dt);

  if (pressed.has("Space")) P.jumpBuffer = 0.12;
  P.jumpBuffer = Math.max(0, P.jumpBuffer - dt);
  P.coyote = P.onFloor ? 0.1 : Math.max(0, P.coyote - dt);
  P.airLock = Math.max(0, P.airLock - dt);
  P.wall = P.onFloor ? null : probeWall();

  if (!(P.dodgeLeft > 0 && P.dodgeAir)) P.vel.y -= STATS.gravity * dt;
  if (P.sliding) {
    // glissade : on garde sa vitesse, on peut tourner un peu ; étincelles au sol
    const want = moving ? P.wish.clone().normalize() : P.slideDir, ang = Math.atan2(P.slideDir.x * want.z - P.slideDir.z * want.x, P.slideDir.dot(want));
    P.slideDir.applyAxisAngle(UP, -Math.max(-1.6 * dt, Math.min(1.6 * dt, ang))).normalize();
    P.slideSpeed += (STATS.slide_speed - P.slideSpeed) * Math.min(1, dt * 1.2);
    P.vel.x = P.slideDir.x * P.slideSpeed; P.vel.z = P.slideDir.z * P.slideSpeed; P.facingYaw = Math.atan2(-P.slideDir.x, -P.slideDir.z);
    P.slideFx = (P.slideFx || 0) - dt; if (P.slideFx <= 0) { P.slideFx = 0.04; slideSparks(P.pos, P.slideDir); }
  } else if (P.dodgeLeft > 0) {
    P.dodgeLeft -= dt; P.vel.x = P.dodgeVel.x; P.vel.z = P.dodgeVel.z; if (P.dodgeAir) P.vel.y = 0;
    // fin du dash : on retombe à une vitesse de course (un peu plus en l'air)
    if (P.dodgeLeft <= 0 && !P.extraDodges) { const d = P.dodgeVel.clone().normalize(), v = P.dodgeAir ? 16 : STATS.walk_speed * 1.3; P.vel.x = d.x * v; P.vel.z = d.z * v; }
    // Triple glitch : l'esquive suivante part dès la fin de la précédente
    if (P.dodgeLeft <= 0 && P.extraDodges > 0) { P.extraDodges--; P.airDash = 1; startDodge(P.dodgeDist, P.dodgeDur); }
  } else if (P.lungeLeft > 0 && P.flatSpeed < P.lungeSpeed * 1.3) {
    P.lungeLeft -= dt; const f = facing(), k = Math.max(0, P.lungeLeft / P.lungeTotal);
    const foe = nearestFoe(6), toFoe = foe ? Math.hypot(foe.position.x - P.pos.x, foe.position.z - P.pos.z) - foe.radius + 0.5 : 99, brake = P.ghost > 0 ? 1 : Math.min(1, Math.max(0, (toFoe - 0.9) / 0.6));
    P.vel.x = f.x * P.lungeSpeed * k * brake; P.vel.z = f.z * P.lungeSpeed * k * brake;
  } else if (!P.diving) {
    // au sol : accélération franche, l'excès de vitesse retombe doucement ;
    // en l'air : l'élan est conservé (on peut seulement l'orienter)
    const cur = Math.hypot(P.vel.x, P.vel.z), over = cur > speed + 0.5;
    let accel, max = speed;
    if (P.onFloor) accel = over ? STATS.overspeed_decay : STATS.accel * Math.max(1, M.speed);
    else { accel = STATS.air_accel * (P.airLock > 0 ? 0.15 : 1); max = Math.max(speed, cur); }
    let tx = P.wish.x * max, tz = P.wish.z * max;
    if (!P.onFloor && !moving) { tx = P.vel.x * (1 - 0.25 * dt); tz = P.vel.z * (1 - 0.25 * dt); }
    else if (P.onFloor && over && moving) { tx = P.wish.x * (cur - STATS.overspeed_decay * dt); tz = P.wish.z * (cur - STATS.overspeed_decay * dt); accel = STATS.accel; }
    accel *= dt;
    const dx = tx - P.vel.x, dz = tz - P.vel.z, dl = Math.hypot(dx, dz);
    if (dl <= accel) { P.vel.x = tx; P.vel.z = tz; } else { P.vel.x += (dx / dl) * accel; P.vel.z += (dz / dl) * accel; }
  }
  if (P.wall && !P.diving && P.vel.y < -STATS.wall_slide_speed && P.wish.dot(P.wall.normal) < -0.3) P.vel.y = -STATS.wall_slide_speed;

  // dash-saut : sauter pendant un dash au sol garde presque toute la vitesse du dash
  if (P.jumpBuffer > 0 && P.dodgeLeft > 0 && !P.dodgeAir && !P.dead) {
    P.dodgeLeft = 0; P.vel.x = P.dodgeVel.x * STATS.dash_jump; P.vel.z = P.dodgeVel.z * STATS.dash_jump; P.vel.y = STATS.jump_velocity * 0.9;
    P.onFloor = false; P.coyote = 0; P.jumpBuffer = 0; P.airJumps = 1 + M.jumps; P.wallJumps = 0; squash(5); tone(300, 900, 0.12, 0.06, "square"); styleAdd(8, "DASH-SAUT");
  }
  if (P.jumpBuffer > 0 && P.dodgeLeft <= 0 && !P.diving && !P.dead && P.knock <= 0) {
    if (P.onFloor || P.coyote > 0) {
      // rebond d'écrasement : sauter juste après un écrasement renvoie très haut
      const bounce = time - (P.slamLandAt ?? -9) < 0.25 ? Math.min(20, (P.slamHeight || 0) * STATS.slam_bounce + 6) : 0;
      P.vel.y = STATS.jump_velocity + bounce; P.onFloor = false; P.coyote = 0; P.jumpBuffer = 0; P.airJumps = 1 + M.jumps; P.wallJumps = 0; P.superUsed = false;
      if (P.sliding) { P.sliding = false; styleAdd(5, "GLISSADE-SAUT"); }
      if (bounce) { P.slamLandAt = -9; pixelRing(P.pos, 2.5, 0.4, 0xff5a3a); tone(200, 1200, 0.25, 0.08, "square"); styleAdd(10, "REBOND"); }
      squash(4); tone(320, 620, 0.09, 0.05); puff(P.pos, 4, 0.8);
    } else if (P.wall && (P.wallJumps < STATS.wall_jumps || P.wall.chain)) {
      const n = P.wall.normal;
      P.vel.set(n.x * STATS.wall_jump.push, STATS.wall_jump.up, n.z * STATS.wall_jump.push);
      P.wallJumps++; P.airLock = 0.22; P.jumpBuffer = 0; P.facingYaw = Math.atan2(-n.x, -n.z);
      if (P.wall.chain) { P.airJumps = 1 + M.jumps; P.airDash = 1; }
      puff(P.pos.clone().addScaledVector(n, -0.35).add(new THREE.Vector3(0, 0.9, 0)), 5, 0.6, P.wall.chain ? 0xff9fc8 : 0xd9cdbf);
      squash(-3); tone(420, 820, 0.08, 0.05); noiseHit(1500, 1, 0.06, 0.12);
      if (!P.wall.chain && P.wallJumps === STATS.wall_jumps) P.hintSingle = (P.hintSingle || 0) + 1;
    } else if (P.airJumps > 0) {
      P.vel.y = STATS.double_jump_velocity; P.airJumps--; P.jumpBuffer = 0; squash(3);
      model.flip.rotation.x = 0; play(model.flip.rotation, [{ to: { x: -Math.PI * 2 }, dur: 0.38, ease: linear }], () => { model.flip.rotation.x = 0; });
      pixelBurst(P.pos.clone().add(new THREE.Vector3(0, 0.4, 0))); tone(500, 950, 0.1, 0.05);
    } else if (M.super_jump && !P.superUsed) {
      // Super saut (Delay + Chorus)
      P.superUsed = true; P.vel.y = STATS.super_jump_velocity; P.jumpBuffer = 0; squash(6);
      model.flip.rotation.x = 0; play(model.flip.rotation, [{ to: { x: -Math.PI * 4 }, dur: 0.7, ease: easeOut }], () => { model.flip.rotation.x = 0; });
      pixelRing(P.pos, 2.6, 0.5, 0x8ff0b0, -0.5); burst(P.pos.clone(), [0x8ff0b0, 0x8fe6ff, 0xffffff], 4, 24); tone(300, 1500, 0.35, 0.08, "triangle");
    }
  }

  const wasOnFloor = P.onFloor, fallSpeed = P.vel.y;
  moveAxis(0, P.vel.x * dt); moveAxis(2, P.vel.z * dt);
  P.pos.y += P.vel.y * dt;
  let landed = false;
  for (const b of colliders) {
    if (!solid(b) || !overlaps(b, P.pos.x, P.pos.y, P.pos.z)) continue;
    if (P.vel.y <= 0) { P.pos.y = b.max[1]; landed = true; } else { P.pos.y = b.min[1] - H - 1e-4; P.vel.y = 0; }
  }
  if (P.pos.y <= 0) { P.pos.y = 0; landed = true; }
  if (landed) {
    if (P.diving) {
      P.diving = false; sim.queue({ type: "dive_land", source: P.id }); startAction(model, "slam");
      P.vel.x = P.vel.z = 0; P.attackSlow = 0.15; squash(-7);
      P.slamLandAt = time; P.slamHeight = Math.max(0, (P.slamFrom ?? P.pos.y) - P.pos.y);
      groundImpact(P.pos, 1.5 + Math.min(2.5, P.slamHeight / 5), 0xff5a3a);
      tone(110, 40, 0.35, 0.35, "sine"); noiseHit(220, 0.8, 0.3, 0.3, "lowpass");
    } else if (!wasOnFloor && fallSpeed < -6) {
      squash(-Math.min(6, -fallSpeed * 0.3)); puff(P.pos, fallSpeed < -12 ? 10 : 5, 1.6); stepSound(surfaceAt(P.pos.x, P.pos.z), true);
      if (fallSpeed < -11 && !model.action) startAction(model, "land");
      if (fallSpeed < -20) groundImpact(P.pos, 0.6, 0xd9cdbf);
    }
    P.vel.y = 0; P.onFloor = true; P.airJumps = 1 + M.jumps; P.wallJumps = 0; P.airDash = 1; P.superUsed = false;
  } else P.onFloor = false;

  P.flatSpeed = Math.hypot(P.vel.x, P.vel.z);
  P.sprint = Math.max(0, Math.min(1, (P.flatSpeed - 9) / 8)); // « très vite » : sert aux animations
  // en combat (sans foncer), le bonhomme reste tourné vers l'ennemi le plus proche : on tourne autour en garde
  const foe = P.combat > 0.5 && P.flatSpeed < 7 && !P.sliding ? nearestFoe(9) : null;
  if (P.aiming && !P.dead) P.facingYaw = lerpAngle(P.facingYaw, cam.yaw, 1 - Math.exp(-20 * dt));
  else if (foe && !P.dead && P.knock <= 0 && P.dodgeLeft <= 0 && P.lungeLeft <= 0 && P.airLock <= 0 && P.attackSlow <= 0) P.facingYaw = lerpAngle(P.facingYaw, Math.atan2(-(foe.position.x - P.pos.x), -(foe.position.z - P.pos.z)), 1 - Math.exp(-8 * dt));
  else if (P.flatSpeed > 0.5 && P.dodgeLeft <= 0 && P.airLock <= 0 && P.lungeLeft <= 0 && P.attackSlow <= 0) P.facingYaw = lerpAngle(P.facingYaw, Math.atan2(-P.vel.x, -P.vel.z), 1 - Math.exp(-12 * dt));
  P.yawRate = P.yawRate * 0.8 + (wrapAngle(P.facingYaw - P.prevYaw) / dt) * 0.2; P.prevYaw = P.facingYaw;
  P.speedRatio = Math.min(1.3, P.flatSpeed / STATS.walk_speed);

  if (P.onFloor && P.flatSpeed > 0.8) {
    const before = Math.sin(P.phase);
    P.phase += dt * (5 + P.flatSpeed * 1.25) * (M.tired ? 0.8 : 1);
    if (Math.sign(Math.sin(P.phase)) !== Math.sign(before) && P.lungeLeft <= 0) { stepSound(surfaceAt(P.pos.x, P.pos.z), P.sprint > 0.5); if (P.flatSpeed > 7) puff(P.pos.clone().addScaledVector(facing(), -0.3), 2, 0.4); }
  }

  if (!P.dead) {
    readSpellKeys();
    for (; attackClicks > 0; attackClicks--) {
      if (P.aiming) { const a = aimShot(); sim.queue({ type: "wave", source: P.id, wave: P.wave, origin: a.origin, dir: a.dir }); continue; }
      // en l'air, le clic donne des coups normaux (l'écrasement est sur C)
      if (!P.diving) sim.queue({ type: "attack", source: P.id, airborne: false, slide: P.sliding });
    }
  }
  pressed.clear();

  if (P.pos.y < -20) P.pos.copy(SPAWN);
  if (vinyl.visible && P.pos.distanceTo(SECRET_POS) < 1.6) { vinyl.visible = false; P.secrets++; PROGRESS.vinyl = true; saveProgress(); document.getElementById("secrets").textContent = `Secrets ${P.secrets} / 1`; showBanner("Secret trouvé : le vinyle doré", 2.6); [523, 659, 784, 1046].forEach((f, i) => setTimeout(() => tone(f, f * 1.01, 0.18, 0.06, "triangle"), i * 90)); }
  if (!P.alleyHint && P.pos.x > ALLEY.x0 && P.pos.x < ALLEY.x1 && P.pos.z > ALLEY.z0 && P.pos.z < ALLEY.z1) { P.alleyHint = true; showBanner("Murs tagués : wall jumps à l'infini", 2.4); }
  if (P.hintSingle === 3) { P.hintSingle = 4; showBanner("3 wall jumps par saut… sauf sur les murs tagués : à l'infini", 2.6); }

  if (P.flailAt > 0 && sim.time >= P.flailAt) { P.flailAt = -1; flailThrow(); }
  stepDummy(dt); stepMonsters(dt);
  sim.setTransform(P.id, P.pos, facing());
  sim.step(dt);
  for (const ev of sim.drain()) onEvent(ev);
}

// --- Corps semi-liquide -------------------------------------------------------------------
// Il s'étale dans le sens de la vitesse, s'étire quand il tombe ou saute, sa tête
// traîne derrière (ressort) et il laisse des gouttes de chrome quand il fonce.
const LIQ = { prev: new THREE.Vector3(), off: new THREE.Vector3(), offV: new THREE.Vector3(), drip: 0 };
function liquidBody(dt) {
  if (dt <= 0) return;
  const acc = P.vel.clone().sub(LIQ.prev).divideScalar(dt); LIQ.prev.copy(P.vel);
  const dash = P.dodgeLeft > 0 ? 1 : 0;
  model.smear += ((dash ? 0.9 : Math.min(0.5, Math.max(0, (P.flatSpeed - 10) / 28)) + (P.sliding ? 0.35 : 0)) - model.smear) * Math.min(1, dt * 12);
  model.vstretch = Math.min(0.55, Math.abs(P.vel.y) / 45) * (P.diving ? 1.4 : 1);
  // tête : ressort contre l'accélération, exprimé dans le repère du bonhomme
  const want = acc.clone().multiplyScalar(-0.0035).applyAxisAngle(UP, -P.facingYaw); want.clampLength(0, 0.32);
  LIQ.offV.addScaledVector(want.sub(LIQ.off).multiplyScalar(140), dt).multiplyScalar(Math.exp(-9 * dt)); LIQ.off.addScaledVector(LIQ.offV, dt); LIQ.off.clampLength(0, 0.4);
  model.head.position.copy(model.headRest).add(LIQ.off);
  const j = Math.min(0.35, LIQ.offV.length() * 0.08); model.head.scale.set(1 + j, 1 - j * 0.6, 1 + j);
  LIQ.drip -= dt;
  if ((P.flatSpeed > 14 || dash || P.diving) && LIQ.drip <= 0) { LIQ.drip = 0.035; chromeDrip(P.pos.clone().add(new THREE.Vector3(range(-0.15, 0.15), range(0.3, 1.6), range(-0.15, 0.15))), P.vel); }
}

// --- Animation du bonhomme et de son double -------------------------------------------
function animatePlayer(dt) {
  P.t += dt;
  model.root.position.copy(P.pos); model.root.rotation.y = P.facingYaw;
  const run = Math.min(1, P.speedRatio);
  model.body.position.y += ((P.sliding ? -0.5 : P.onFloor && !P.lying ? Math.abs(Math.cos(P.phase)) * 0.05 * run : 0) - model.body.position.y) * Math.min(1, dt * 18);
  liquidBody(dt);
  if (!P.lying) {
    if (P.flatSpeed < 0.3 && P.onFloor && P.dodgeLeft <= 0 && !model.action && !P.aiming && P.combat < 0.5 && P.knock <= 0 && !P.talking && !(P.mods && P.mods.tired) && !hasStatus("chorus")) {
      P.idle += dt;
      if (P.idle > 5 && P.waved === 0) { P.waved = 1; startAction(model, "wave"); }
      if (P.idle > 30) lieDown();
    } else if (P.flatSpeed >= 0.3 || !P.onFloor || P.aiming) { P.idle = 0; P.waved = 0; }
  } else if (!P.dead) {
    P.zzz = (P.zzz || 0) + dt;
    if (P.zzz > 2.2) { P.zzz = 0; floatText("z", model.head.getWorldPosition(new THREE.Vector3()).add(new THREE.Vector3(0, 0.5, 0)), "float zzz"); }
  }
  animateRig(model, basePose(), dt);
  // double fantôme du Delay : collé à l'ennemi, il rejoue le combo
  if (ghostRig.root.visible) {
    const t = sim.entities.get(DELAY_GHOST.target);
    if (!t || sim.time > DELAY_GHOST.until) { ghostRig.root.visible = false; }
    else {
      const pos = t.position.clone().addScaledVector(DELAY_GHOST.side, 1.15); pos.y = t.position.y;
      ghostRig.root.position.lerp(pos, 1 - Math.exp(-20 * dt));
      ghostRig.root.rotation.y = Math.atan2(-(t.position.x - pos.x), -(t.position.z - pos.z));
      while (DELAY_GHOST.queue.length && sim.time >= DELAY_GHOST.queue[0].at) startAction(ghostRig, animOf(DELAY_GHOST.queue.shift().move), 0.08);
      const fade = Math.min(1, (DELAY_GHOST.until - sim.time) / 0.3);
      ghostMat.opacity = 0.55 * fade * (0.85 + Math.sin(time * 30) * 0.15);
      animateRig(ghostRig, guardBase(), dt);
    }
  }
  hitFlash = Math.max(0, hitFlash - dt * 3);
  blob.position.set(P.pos.x, groundUnder() + 0.015, P.pos.z);
}
// Effets attachés aux statuts : traînées, orbes, anneaux, lamelles, étoiles
function statusEffects(dt) {
  const pe = sim.entities.get(P.id);
  // Gater
  gateFxLife -= dt;
  gateFx.visible = gateFxLife > 0;
  if (gateFx.visible) { gateFx.position.copy(P.pos); gateFx.rotation.y += dt * 9; const on = Math.floor(time * 24) % 2 === 0; gateFx.children.forEach((m, i) => (m.material.opacity = on ? 0.9 : 0.15)); }
  // Chorus : orbes et soins affichés une fois par seconde
  const chorusOn = pe.statuses.has("chorus") || pe.statuses.has("chorus_soft");
  chorusOrbs.forEach((m, i) => { m.visible = chorusOn; if (chorusOn) { const a = time * 2.2 + (i * Math.PI * 2) / 3; m.position.set(P.pos.x + Math.cos(a) * 0.85, P.pos.y + 1.2 + Math.sin(time * 3 + i) * 0.2, P.pos.z + Math.sin(a) * 0.85); } });
  P.healTimer += dt; if (P.healTimer > 1) { P.healTimer = 0; if (P.healAcc > 0) { floatText("+" + Math.round(P.healAcc), P.pos.clone().add(new THREE.Vector3(0, 2.1, 0)), "float heal"); P.healAcc = 0; } }
  // traînée de fantômes : Saturation, Peau d'oignon, Quitte ou double
  const trail = pe.statuses.has("phase") ? 0.05 : (pe.statuses.has("saturation") || pe.statuses.has("saturation_x") || pe.statuses.has("all_in")) && P.flatSpeed > 3 ? 0.12 : 0;
  if (trail) { P.trailTimer -= dt; if (P.trailTimer <= 0) { P.trailTimer = trail; ghostRequest = true; } }
  // anneaux d'écho (Reverb) autour du joueur et des cibles
  P.ringTimer -= dt;
  if (P.ringTimer <= 0) {
    P.ringTimer = 0.9;
    if (pe.statuses.has("reverb_aura")) pixelRing(P.pos, 1.3, 0.6, 0xb9a6ff, 0.05);
    const de = sim.entities.get(dummy.id); if (de.statuses.has("reverb_loop")) pixelRing(dummy.pos, 1.8, 0.7, 0x9a7bff, 1.2);
    if (P.mods.tired) puff(P.pos.clone().add(new THREE.Vector3(0, 1.7, 0)), 1, 0.3, 0xdfe9ff);
  }
  // étoiles d'étourdissement
  const stunned = sim.entities.get(dummy.id).statuses.has("stun");
  stunStars.forEach((m, i) => { m.visible = stunned; if (stunned) { const a = time * 5 + (i * Math.PI * 2) / 3; m.position.set(dummy.pos.x + Math.cos(a) * 0.5, dummy.pos.y + 2.05, dummy.pos.z + Math.sin(a) * 0.5); m.rotation.y += dt * 6; } });
  // signal d'attaque de l'enceinte
  if (WARN.kind) {
    WARN.t += dt; const f = Math.min(1, WARN.t / WARN.dur);
    coneMat.emissive.setHex(0xff3a2a); coneMat.emissiveIntensity = 0.5 + f * 3 * (0.7 + Math.sin(time * 40) * 0.3);
    warnRing.visible = WARN.kind === "boom";
    if (warnRing.visible) { warnRing.position.set(dummy.pos.x, dummy.pos.y + 0.05, dummy.pos.z); const r = DATA.enemy_attacks.boom.radius * f; warnRing.scale.set(r, r, r); }
  } else { warnRing.visible = false; coneMat.emissive.setHex(0xff8c4d); }
}
// Teinte du chrome selon les statuts (Distortion rouge, Saturation orange…)
function chromeTint() {
  const pe = sim.entities.get(P.id), c = new THREE.Color(1, 1, 1);
  if (pe.statuses.has("all_in")) c.setRGB(1.25, 1.05, 0.55);
  else if (pe.statuses.has("distortion") || pe.statuses.has("distortion_x") || pe.statuses.has("distortion_soft") || pe.statuses.has("crescendo")) c.setRGB(1.25, 0.72, 0.62);
  else if (pe.statuses.has("saturation") || pe.statuses.has("saturation_x") || pe.statuses.has("saturation_soft") || pe.statuses.has("tempo")) c.setRGB(1.25, 0.95, 0.7);
  else if (pe.statuses.has("reflect")) c.setRGB(1.05, 0.8, 1.25);
  else if (pe.statuses.has("phase")) c.setRGB(0.8, 1.1, 1.3);
  else if (P.mods && P.mods.tired) c.setRGB(0.85, 0.88, 1.0);
  c.multiplyScalar(1 - 0.45 * DN.n); c.b += 0.06 * DN.n; // la nuit, le chrome s'assombrit et bleuit
  c.lerp(new THREE.Color(1.3, 0.35, 0.4), hitFlash * 0.8);
  return c;
}

// --- Caméra : suit le joueur, se raccourcit contre les murs, zoom de visée ------------
function rayBox(o, d, len, b) {
  let tmin = 0, tmax = len;
  for (let i = 0; i < 3; i++) {
    if (Math.abs(d[i]) < 1e-6) { if (o[i] < b.min[i] || o[i] > b.max[i]) return Infinity; }
    else { let t1 = (b.min[i] - o[i]) / d[i], t2 = (b.max[i] - o[i]) / d[i]; if (t1 > t2) [t1, t2] = [t2, t1]; tmin = Math.max(tmin, t1); tmax = Math.min(tmax, t2); if (tmin > tmax) return Infinity; }
  }
  return tmin;
}
function setFov(f) { if (Math.abs(camera.fov - f) > 0.01) { camera.fov = f; camera.updateProjectionMatrix(); } }
function updateCamera(realDt) {
  cam.aim += ((P.aiming ? 1 : 0) - cam.aim) * (1 - Math.exp(-12 * realDt));
  const fast = Math.max(0, Math.min(1, (P.flatSpeed - STATS.walk_speed) / (STATS.sprint_speed - STATS.walk_speed))) * (1 - cam.aim);
  cam.fov += (70 + 12 * fast - 14 * cam.aim - cam.fov) * (1 - Math.exp(-8 * realDt)); setFov(cam.fov);
  const goal = P.pos.clone().add(new THREE.Vector3(0, 1.45 - 0.2 * fast + 0.1 * cam.aim, 0));
  cam.pivot.lerp(goal, 1 - Math.exp(-14 * realDt));
  const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(cam.pitch, cam.yaw, 0, "YXZ"));
  const want = new THREE.Vector3(0.45 + 0.35 * cam.aim, 0, cam.dist - 0.5 * fast - 2.0 * cam.aim).applyQuaternion(q);
  const len = want.length(), d = want.clone().divideScalar(len), o = [cam.pivot.x, cam.pivot.y, cam.pivot.z], da = [d.x, d.y, d.z];
  let hit = len;
  for (const b of colliders) if (b.cam) hit = Math.min(hit, rayBox(o, da, len, b));
  camera.position.copy(cam.pivot).addScaledVector(d, Math.min(Math.max(0.35, hit - 0.3), len));
  if (camera.position.y < 0.3) camera.position.y = 0.3;
  if (cam.shake > 0) { camera.position.add(new THREE.Vector3(range(-1, 1), range(-1, 1), range(-1, 1)).multiplyScalar(cam.shake)); cam.shake = Math.max(0, cam.shake - realDt * 1.6); }
  camera.quaternion.copy(q);
  camera.updateMatrixWorld();
}

// --- Lignes de vitesse et lignes d'impact (façon animé) ---------------------------------
const speedCanvas = document.getElementById("speed"), sg = speedCanvas.getContext("2d");
function drawOverlay() {
  const w = speedCanvas.width, h = speedCanvas.height;
  sg.clearRect(0, 0, w, h);
  if (FX.impact > 0) {
    const p = project(FX.impactWorld), maxR = Math.hypot(w, h);
    sg.fillStyle = "#000";
    for (let i = 0; i < 70; i++) {
      const a = Math.random() * Math.PI * 2, spread = 0.008 + Math.random() * 0.02, r0 = maxR * (0.12 + Math.random() * 0.18);
      sg.beginPath(); sg.moveTo(p.x + Math.cos(a) * r0, p.y + Math.sin(a) * r0);
      sg.lineTo(p.x + Math.cos(a - spread) * maxR, p.y + Math.sin(a - spread) * maxR); sg.lineTo(p.x + Math.cos(a + spread) * maxR, p.y + Math.sin(a + spread) * maxR); sg.fill();
    }
    return;
  }
  const f = Math.max(0, Math.min(1, (P.flatSpeed - 13) / 6)) * (1 - cam.aim);
  if (f <= 0) return;
  const cx = w / 2, cy = h / 2, maxR = Math.hypot(cx, cy);
  sg.lineCap = "round";
  for (let i = 0; i < 46; i++) {
    const a = Math.random() * Math.PI * 2, r0 = maxR * (0.55 + Math.random() * 0.2), r1 = maxR * (0.85 + Math.random() * 0.2);
    sg.strokeStyle = `rgba(255,250,244,${(0.1 + Math.random() * 0.25) * f})`; sg.lineWidth = 1 + Math.random() * 2.5;
    sg.beginPath(); sg.moveTo(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0); sg.lineTo(cx + Math.cos(a) * r1, cy + Math.sin(a) * r1); sg.stroke();
  }
}
let lastFilter = "";
function applyImpactFilter() {
  let f = "saturate(0.82) contrast(1.1)"; // étalonnage dark fantasy
  if (FX.impact > 0) f = FX.impact > FX.impactDur * 0.5 ? "invert(1) grayscale(1) contrast(3)" : "grayscale(1) contrast(5) brightness(1.15)";
  if (f !== lastFilter) { renderer.domElement.style.filter = f; lastFilter = f; }
}

// --- HUD : sorts, recharges, statuts, viseur, fusion -------------------------------------
const statusesEl = document.getElementById("statuses"), crosshair = document.getElementById("crosshair"), combatEl = document.getElementById("combat");
let hudTimer = 0;
const staminaEls = [...document.querySelectorAll("#stamina i")];
function updateHud(realDt) {
  hudTimer -= realDt; if (hudTimer > 0) return; hudTimer = 0.08;
  const pe = sim.entities.get(P.id);
  // endurance du dash : 3 barres
  const st = pe.stamina ?? 3;
  staminaEls.forEach((el, k) => { const f = Math.max(0, Math.min(1, st - k)) * 100; el.style.background = `linear-gradient(90deg, ${f >= 100 ? "#8fe6ff" : "#4a7a8a"} ${f}%, rgba(143,230,255,0.12) ${f}%)`; });
  for (const s of slotEls) {
    if (s.id === "weapon") continue;
    if (s.id === "bitcrush") { s.cd.style.background = st < 1 ? "rgba(10,8,14,0.6)" : "transparent"; s.txt.textContent = Math.floor(st); s.el.classList.toggle("blocked", P.mods.no_dodge); continue; }
    const def = DATA.spells[s.id] || DATA.waves[s.id] || FUSION_BY_ID[s.id.replace("fusion:", "")], left = sim.cooldownLeft(pe, s.id), max = s.id === "bitcrush" ? def.cooldown * P.mods.dodge_cd : def.cooldown;
    const pct = max > 0 ? Math.min(100, (left / max) * 100) : 0;
    s.cd.style.background = pct > 0 ? `conic-gradient(rgba(10,8,14,0.78) ${pct}%, transparent 0)` : "transparent";
    s.txt.textContent = left > 0.05 && max > 1 ? left.toFixed(left < 10 ? 1 : 0) : "";
    s.el.classList.toggle("active", s.id === P.wave);
    s.el.classList.toggle("blocked", s.id === "bitcrush" && P.mods.no_dodge);
  }
  statusesEl.innerHTML = [...pe.statuses.values()].map((st) => {
    const def = DATA.statuses[st.id], left = st.until - sim.time;
    const extra = st.id === "crescendo" ? ` ×${(1 + 0.1 * st.stacks).toFixed(1)}` : "";
    return `<span class="chip ${def.kind}">${def.name}${extra}<em>${left < 100 ? Math.ceil(left) + " s" : "∞"}</em></span>`;
  }).join("");
  crosshair.hidden = !P.aiming;
  if (P.aiming) crosshair.dataset.wave = DATA.waves[P.wave].short + " " + DATA.waves[P.wave].name;
  combatEl.hidden = !sim.inCombat(pe);
}

// --- Bonhomme en basse résolution, posé à sa place ----------------------------------------
function renderPixelPass(dt) {
  if (!PIX.on) return;
  PIX.crush = Math.max(0, PIX.crush - dt);
  const want = PIX.crush > 0 ? 30 : PIX.res;
  if (want !== PIX.cur) { PIX.cur = want; pixTarget.setSize(want, want); }
  const center = P.pos.clone().add(new THREE.Vector3(0, 0.95, 0)), cc = center.clone().applyMatrix4(camera.matrixWorldInverse), d = -cc.z;
  if (d < 0.3) { pixQuad.visible = false; return; }
  pixQuad.visible = true;
  const size = renderer.getSize(new THREE.Vector2()), fullW = size.x, fullH = size.y;
  const pxPerM = fullH / (2 * Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2) * d);
  const s = PIX.size * pxPerM, sx = fullW / 2 + cc.x * pxPerM, sy = fullH / 2 - cc.y * pxPerM;
  pixCam.fov = camera.fov; pixCam.aspect = camera.aspect; pixCam.near = 0.05; pixCam.far = d + PIX.size * 2;
  pixCam.position.copy(camera.position); pixCam.quaternion.copy(camera.quaternion);
  pixCam.setViewOffset(fullW, fullH, sx - s / 2, sy - s / 2, s, s); pixCam.updateMatrixWorld();
  const tint = chromeTint(); chrome.color.copy(tint); chromeBody.color.copy(tint).multiplyScalar(0.85); tintWeapons(tint);
  const fog = scene.fog; scene.fog = null;
  renderer.setClearColor(0x000000, 0);
  renderer.setRenderTarget(pixTarget); renderer.clear(); renderer.render(scene, pixCam);
  if (P.ghostTimes && P.dodgeLeft > 0) { const el = P.dodgeTotalT = (P.dodgeTotalT || 0) + dt; if (P.ghostTimes.length && el >= P.ghostTimes[0]) { P.ghostTimes.shift(); ghostRequest = true; } }
  if (P.dodgeLeft <= 0) P.dodgeTotalT = 0;
  if (ghostRequest) {
    ghostRequest = false;
    const gh = ghosts[ghostIndex++ % ghosts.length];
    if (gh.rt.width !== PIX.cur) gh.rt.setSize(PIX.cur, PIX.cur);
    renderer.setRenderTarget(gh.rt); renderer.clear(); renderer.render(scene, pixCam);
    gh.pos.copy(center); gh.life = gh.maxLife = hasStatus("phase") ? 0.5 : 0.35; gh.quad.visible = true;
  }
  renderer.setRenderTarget(null); scene.fog = fog;
  const dq = Math.max(d - 1.0, d * 0.5), kk = dq / d;
  pixQuad.position.set(cc.x * kk, cc.y * kk, -dq).applyMatrix4(camera.matrixWorld);
  // Distortion : l'image du bonhomme grésille
  const pe = sim.entities.get(P.id);
  if (pe.statuses.has("distortion") || pe.statuses.has("distortion_x") || pe.statuses.has("all_in")) pixQuad.position.add(new THREE.Vector3(range(-1, 1), range(-1, 1), 0).applyQuaternion(camera.quaternion).multiplyScalar(0.03));
  pixQuad.quaternion.copy(camera.quaternion); pixQuad.scale.set(PIX.size * kk, PIX.size * kk, 1);
  for (const gh of ghosts) {
    if (gh.life <= 0) { gh.quad.visible = false; continue; }
    gh.life -= dt; gh.quad.position.copy(gh.pos); gh.quad.quaternion.copy(camera.quaternion); gh.quad.scale.set(PIX.size, PIX.size, 1);
    gh.quad.material.opacity = Math.max(0, gh.life / gh.maxLife) * 0.65;
  }
}

// ============================================================================
// Boucle : pause d'impact (hitstop) puis rendu. Pas de mouvement de caméra.
// ============================================================================
function resize() {
  const w = container.clientWidth || innerWidth, h = container.clientHeight || innerHeight;
  renderer.setSize(w, h, false); camera.aspect = w / h; camera.updateProjectionMatrix();
  speedCanvas.width = w; speedCanvas.height = h;
}
addEventListener("resize", resize);
function project(world) { const v = world.clone().project(camera); return { x: (v.x * 0.5 + 0.5) * container.clientWidth, y: (-v.y * 0.5 + 0.5) * container.clientHeight, behind: v.z > 1 }; }
const dummyBar = document.getElementById("dummyBar");
const STEP = 1 / 60;
let last = performance.now(), acc = 0, time = 0;
// La boucle ne doit jamais s'arrêter : on redemande l'image suivante d'abord,
// et une erreur éventuelle est affichée discrètement (à me transmettre) au lieu de figer le jeu.
const ERRORS = { n: 0, el: null };
function reportError(e) {
  ERRORS.n++;
  if (ERRORS.n <= 3) console.error(e);
  if (!ERRORS.el) { ERRORS.el = document.createElement("div"); ERRORS.el.className = "hud"; ERRORS.el.style.cssText = "left:20px;top:96px;font-size:11px;color:#ff9a8a;max-width:60vw"; document.body.appendChild(ERRORS.el); }
  ERRORS.el.textContent = `Erreur (×${ERRORS.n}) : ${e && e.message} — ${((e && e.stack) || "").split("\n")[1] || ""}`.slice(0, 220);
}
function frame_(now) {
  requestAnimationFrame(frame_);
  try { tick(now); } catch (e) { reportError(e); }
}
function tick(now) {
  const realDt = Math.min(0.1, (now - last) / 1000); last = now;
  let dt = paused ? 0 : realDt;
  // pause d'impact, jamais plus d'un quart de seconde d'affilée (sécurité anti-blocage)
  if (FX.freeze > 0 && (FX.frozen || 0) < 0.25) { FX.freeze -= realDt; FX.frozen = (FX.frozen || 0) + realDt; dt = 0; }
  else { FX.freeze = 0; FX.frozen = 0; }
  FX.impact = Math.max(0, FX.impact - realDt);
  time += dt; acc += dt;
  while (acc >= STEP) { physicsStep(STEP); acc -= STEP; }
  updateTimelines(dt); animatePlayer(dt); updateFlail(dt); updateMonsters(dt); updateFx(dt); updateAltars(dt); updateNpcs(dt); updateDragon(dt); updateClouds(dt);
  updateCamera(realDt); updateRings(dt); updateProjMeshes(dt); updateBeams(dt); statusEffects(dt);
  if (!WARN.kind) coneMat.emissiveIntensity = Math.max(0, coneMat.emissiveIntensity - dt * 9);
  for (const r of rings) r.rotation.z += dt * 0.12;
  vinyl.rotation.y += dt * 2; vinyl.position.y = SECRET_POS.y + Math.sin(time * 2) * 0.12;
  for (const b of birds) { const u = b.userData, a = u.phase + time * u.speed; b.position.set(u.center.x + Math.cos(a) * u.radius, u.height + Math.sin(a * 3) * 3, u.center.z + Math.sin(a) * u.radius); b.rotation.y = -a + (u.speed > 0 ? 0 : Math.PI); const fl = Math.sin(time * 9 + u.phase * 7) * 0.6; u.l.rotation.z = fl; u.r.rotation.z = -fl; }
  const phaseT = Math.floor(time / 6) % 2;
  trafficLamps.forEach((lamps, i) => { const go = (phaseT + (i < 2 ? 0 : 1)) % 2 === 0; lamps[0].material.color.setHex(go ? 0x2fd06a : 0x1d3a2a); lamps[2].material.color.setHex(go ? 0x3a1d1d : 0xe9473f); lamps[1].material.color.setHex(0x3a321d); });
  for (const p of dust) if (p.life > 0) { p.life -= dt; p.s.position.addScaledVector(p.vel, dt); p.s.scale.setScalar(0.3 + (0.55 - p.life) * 1.6); p.s.material.opacity = Math.max(0, p.life / 0.55) * 0.55; if (p.life <= 0) p.s.visible = false; }
  for (const s of shards) if (s.life > 0) { s.life -= dt; s.vel.y -= 9 * dt; s.m.position.addScaledVector(s.vel, dt); s.m.material.opacity = Math.max(0, s.life / 0.45); if (s.life <= 0) s.m.visible = false; }
  sky.position.copy(camera.position);
  updateDayNight(dt, P.pos);
  sun.position.copy(P.pos).addScaledVector(DN.lightDir, 140); sun.target.position.copy(P.pos);
  renderPixelPass(dt);
  renderer.shadowMap.needsUpdate = true;
  renderer.render(scene, camera);
  applyImpactFilter(); drawOverlay(); updateHud(realDt); updateMonsterBars(); updateNpcTags();
  const bp = project(dummy.pos.clone().add(new THREE.Vector3(0, 2.2, 0)));
  dummyBar.style.transform = `translate(${bp.x - 35}px, ${bp.y}px)`; dummyBar.hidden = bp.behind || P.pos.distanceTo(dummy.pos) > 30;
  for (let i = floats.length - 1; i >= 0; i--) {
    const f = floats[i]; f.age += realDt;
    const p = project(f.pos.clone().add(new THREE.Vector3(0, easeOut(Math.min(1, f.age / f.life)) * 1.1, 0)));
    f.el.style.transform = `translate(${p.x}px, ${p.y}px) translate(-50%, -50%)`;
    f.el.style.opacity = f.age < f.life * 0.4 ? 1 : Math.max(0, 1 - (f.age - f.life * 0.4) / (f.life * 0.6));
    if (f.age > f.life || p.behind) { f.el.remove(); floats.splice(i, 1); }
  }
  if (bannerTimer > 0) { bannerTimer -= realDt; if (bannerTimer <= 0) document.getElementById("banner").style.opacity = 0; }
}
