"use strict";
// ============================================================================
// Données (équivalent de game/data/*.json)
// ============================================================================
const DATA = {
  actors: {
    player: { team: 1, max_health: 100, radius: 0.35, walk_speed: 5, sprint_speed: 9.5, sprint_ramp: 0.7, accel: 40, air_accel: 14,
      jump_velocity: 8.2, double_jump_velocity: 7.6, super_jump_velocity: 17, gravity: 22, wall_jump: { up: 8.6, push: 7.2 }, wall_slide_speed: 3.2,
      dodge: { distance: 5.2, duration: 0.26, cooldown: 0.7, iframes: 0.22 } },
    training_dummy: { team: 2, max_health: 160, radius: 0.55, respawn_delay: 3, ai: "speaker" },
  },
  // Coups et enchaînements : « next » = coup suivant si l'on reclique à temps.
  moves: {
    jab1:    { damage: 8,  range: 1.9, arc_deg: 120, windup: 0.08, recover: 0.2,  window: 0.45, next: "jab2", lunge: 3.2, push: 1.4, hitstop: 0.05 },
    jab2:    { damage: 10, range: 1.9, arc_deg: 120, windup: 0.08, recover: 0.22, window: 0.45, next: "drop", lunge: 3.2, push: 1.8, hitstop: 0.06 },
    drop:    { damage: 24, range: 2.2, arc_deg: 150, windup: 0.2,  recover: 0.5,  lunge: 5.5, launch: [17, 7], hitstop: 0.14, impact: true, label: "DROP!" },
    counter: { damage: 14, range: 2.4, arc_deg: 170, windup: 0.04, recover: 0.3,  window: 0.45, next: "jab2", lunge: 10, push: 5, hitstop: 0.08, ghost: true, label: "CONTRE-GLITCH" },
    dive:    { damage: 18, radius: 3.2, launch: [9, 6.5], hitstop: 0.12, impact: true, dive_speed: 26, label: "PLONGÉE" },
  },
  // Sorts (effets audio). Le Bitcrush est l'esquive de base, toujours sur C.
  spells: {
    bitcrush:   { name: "Bitcrush", short: "BIT", color: "#9fe6ff", cooldown: 0.7, desc: "Esquive de base : onde de pixels qui te propulse." },
    gater:      { name: "Gater", short: "GATE", color: "#f4f0ea", cooldown: 4, window: 0.35, perfect: 0.15, reduce: 0.7, stun: 1.2,
      desc: "Contre : lance-le juste avant le coup adverse. Parfait (0,15 s) : attaque annulée et ennemi étourdi. Bon (0,35 s) : 70 % de dégâts en moins." },
    reverb:     { name: "Reverb", short: "REV", color: "#b9a6ff", cooldown: 14, status: "reverb_aura", desc: "10 s : tes coups laissent un écho, 3 dégâts par seconde pendant 4 s." },
    delay:      { name: "Delay", short: "DLY", color: "#8fe6ff", cooldown: 12, window: 2, mult: 0.6,
      desc: "Juste après un combo : un double fantôme se colle à l'ennemi et rejoue tes 3 derniers coups (60 %). Raté : recharge divisée par deux." },
    distortion: { name: "Distortion", short: "DIST", color: "#ff7a5c", cooldown: 15, status: "distortion", desc: "8 s : dégâts infligés ×1,5, mais dégâts reçus ×1,5." },
    chorus:     { name: "Chorus", short: "CHO", color: "#8ff0b0", cooldown: 18, status: "chorus", desc: "Soin de 10 PV par seconde pendant 6 s. Attaquer coupe le soin." },
    saturation: { name: "Saturation", short: "SAT", color: "#ffb35c", cooldown: 20, status: "saturation",
      desc: "6 s : vitesse folle, esquives quasi sans recharge, 3 sauts. Puis 5 s d'essoufflement : lent, sans esquive." },
  },
  // Ondes de visée (clic droit pour viser, clic gauche pour tirer, 1 2 3 pour choisir)
  waves: {
    sine:     { name: "Sinus", key: "1", short: "∿", color: "#8fe6ff", cooldown: 1.0, pellets: 5, spread_deg: 34, speed: 38, range: 14, dmg_near: 12, dmg_far: 3, near: 3,
      desc: "Fusil à pompe : 5 ondes qui s'écartent. Fort de près, faible à moyenne distance." },
    square:   { name: "Carré", key: "2", short: "⊓", color: "#f4f0ea", cooldown: 3.5, damage: 34, range: 80, desc: "Sniper : rayon instantané, dégâts énormes, longue recharge." },
    triangle: { name: "Triangle", key: "3", short: "△", color: "#ff9fc8", cooldown: 0.45, speed: 22, damage: 9, range: 45, homing: 1.4,
      desc: "Dégâts de base à toute distance, suit légèrement la cible." },
    // ondes de fusion
    pwm:  { name: "PWM", damage: 20, range: 40, width: 1.6 },
    soft: { name: "Onde douce", count: 4, speed: 12, damage: 7, range: 40, homing: 3 },
    saw:  { name: "Dent de scie", count: 8, every: 0.1, speed: 30, damage: 6, range: 45, homing: 0.8 },
  },
  // Buffs et débuffs : génériques, pour le joueur comme pour les ennemis.
  statuses: {
    reverb_aura:     { name: "Reverb", kind: "buff", duration: 10 },
    reverb_dot:      { name: "Écho", kind: "debuff", duration: 4, tick: { damage: 3, every: 1 } },
    distortion:      { name: "Distortion", kind: "buff", duration: 8, mods: { dealt: 1.5, taken: 1.5 } },
    distortion_x:    { name: "Distorsion glitch", kind: "buff", duration: 8, mods: { dealt: 2.2, taken: 2.2 } },
    distortion_soft: { name: "Distorsion douce", kind: "buff", duration: 12, mods: { dealt: 1.25, taken: 1.1 } },
    chorus:          { name: "Chorus", kind: "buff", duration: 6, tick: { heal: 2.5, every: 0.25 }, break_on_attack: true },
    chorus_soft:     { name: "Chœur lointain", kind: "buff", duration: 15, tick: { heal: 2, every: 1 } },
    saturation:      { name: "Saturation", kind: "buff", duration: 6, mods: { speed: 1.6, dodge_cd: 0.3, jumps: 1 }, then: "fatigue" },
    saturation_x:    { name: "Surchauffe", kind: "buff", duration: 6, mods: { speed: 2.0, dodge_cd: 0.15, jumps: 2 }, then: "fatigue_x" },
    saturation_soft: { name: "Saturation douce", kind: "buff", duration: 10, mods: { speed: 1.3, jumps: 1 }, then: "fatigue_light" },
    fatigue:         { name: "Essoufflé", kind: "debuff", duration: 5, mods: { speed: 0.55, no_dodge: true, tired: true } },
    fatigue_x:       { name: "Épuisé", kind: "debuff", duration: 7, mods: { speed: 0.5, no_dodge: true, tired: true } },
    fatigue_light:   { name: "Fatigué", kind: "debuff", duration: 2.5, mods: { speed: 0.8, tired: true } },
    stun:            { name: "Étourdi", kind: "debuff", duration: 1.2, mods: { stunned: true } },
    phase:           { name: "Peau d'oignon", kind: "buff", duration: 1.5, mods: { phase: true } },
    reverb_loop:     { name: "Écho infini", kind: "debuff", duration: 600, tick: { damage: 3, every: 1 }, clear_on_dodge: true },
    crescendo:       { name: "Crescendo", kind: "buff", duration: 10, stacking: true },
    super_jump:      { name: "Super saut", kind: "buff", duration: 10, mods: { super_jump: true } },
    tempo:           { name: "Tempo", kind: "buff", duration: 8, mods: { speed: 1.25, delay_boost: true }, then: "fatigue_light" },
    reflect:         { name: "Renvoi", kind: "buff", duration: 8, mods: { reflect: true } },
    all_in:          { name: "Quitte ou double", kind: "buff", duration: 10, mods: { dealt: 3, speed: 1.8, jumps: 1 } },
  },
  // Fusions : A + B (l'ordre ne compte pas). Ajouter une fusion = ajouter une ligne.
  fusions: [
    { a: "bitcrush", b: "gater", id: "tp", short: "TP", name: "Saut glitch", cooldown: 10, effect: "teleport_behind", combat_only: true, desc: "Téléportation derrière l'ennemi le plus proche (en combat seulement)." },
    { a: "bitcrush", b: "reverb", id: "oignon", short: "OIGN", name: "Peau d'oignon", cooldown: 12, effect: "phase", desc: "Esquive en traînée de doubles : 1,5 s intouchable, tu traverses les ennemis." },
    { a: "bitcrush", b: "delay", id: "triple", short: "×3", name: "Triple glitch", cooldown: 9, effect: "triple_dodge", desc: "Trois esquives d'affilée, sans recharge entre elles." },
    { a: "bitcrush", b: "distortion", id: "dist_x", short: "DIST+", name: "Distorsion glitch", cooldown: 20, effect: "status", status: "distortion_x", desc: "Distortion poussée : dégâts infligés et reçus ×2,2." },
    { a: "bitcrush", b: "chorus", id: "dephasage", short: "DÉPH", name: "Déphasage", cooldown: 14, effect: "destabilize", radius: 6, desc: "Déstabilise les ennemis proches : étourdis 2 s." },
    { a: "bitcrush", b: "saturation", id: "surchauffe", short: "SURCH", name: "Surchauffe glitch", cooldown: 24, effect: "status", status: "saturation_x", desc: "Saturation poussée (vitesse ×2, 4 sauts), puis 7 s d'épuisement." },
    { a: "reverb", b: "delay", id: "echo_infini", short: "∞ÉCHO", name: "Écho infini", cooldown: 40, effect: "reverb_loop", desc: "3 dégâts par seconde à l'infini, tant que l'ennemi n'esquive pas." },
    { a: "reverb", b: "distortion", id: "dist_soft", short: "DIST−", name: "Distorsion douce", cooldown: 16, effect: "status", status: "distortion_soft", desc: "12 s : dégâts infligés ×1,25, reçus ×1,1." },
    { a: "reverb", b: "chorus", id: "choeur", short: "CHŒUR", name: "Chœur lointain", cooldown: 22, effect: "status", status: "chorus_soft", desc: "Soin de 2 PV par seconde pendant 15 s, et tu peux attaquer." },
    { a: "reverb", b: "saturation", id: "sat_soft", short: "SAT−", name: "Saturation douce", cooldown: 18, effect: "status", status: "saturation_soft", desc: "10 s : vitesse ×1,3 et 3 sauts, puis 2,5 s de fatigue." },
    { a: "delay", b: "distortion", id: "crescendo", short: "CRESC", name: "Crescendo", cooldown: 20, effect: "status", status: "crescendo", desc: "10 s : chaque coup +10 % de dégâts, le malus de dégâts reçus baisse de 10 % à chaque coup." },
    { a: "delay", b: "chorus", id: "super_saut", short: "SAUT↑", name: "Super saut", cooldown: 15, effect: "status", status: "super_jump", desc: "10 s : après le double saut, un super saut, même en l'air." },
    { a: "delay", b: "saturation", id: "tempo", short: "TEMPO", name: "Tempo", cooldown: 18, effect: "status", status: "tempo", desc: "8 s : vitesse ×1,25 et Delay à 100 %, puis 2,5 s de fatigue." },
    { a: "distortion", b: "chorus", id: "renvoi", short: "RENV", name: "Renvoi", cooldown: 22, effect: "status", status: "reflect", desc: "8 s : les dégâts reçus repartent vers l'attaquant et te soignent de moitié." },
    { a: "distortion", b: "saturation", id: "all_in", short: "×2?", name: "Quitte ou double", cooldown: 45, effect: "all_in", desc: "10 s : dégâts ×3, vitesse ×1,8. Si l'ennemi visé n'est pas tombé à la fin… c'est toi qui tombes." },
    { a: "sine", b: "square", id: "pwm", short: "PWM", name: "PWM", cooldown: 8, effect: "wave", wave: "pwm", desc: "Rayon large qui traverse tout sur 40 m (20 dégâts à chaque cible)." },
    { a: "sine", b: "triangle", id: "onde_douce", short: "ORBES", name: "Onde douce", cooldown: 7, effect: "wave", wave: "soft", desc: "4 orbes lents qui suivent la cible (7 dégâts chacun)." },
    { a: "square", b: "triangle", id: "dent_scie", short: "SAW", name: "Dent de scie", cooldown: 9, effect: "wave", wave: "saw", desc: "Rafale de 8 tirs rapides qui suivent un peu la cible." },
  ],
  // Attaques de l'enceinte en mode combat (signalées avant de partir)
  enemy_attacks: {
    larsen: { windup: 0.6, speed: 11, damage: 12, homing: 0.6, range: 30 },
    boom:   { windup: 0.55, radius: 3.4, damage: 10, push: 4 },
    every: 2.6, aggro: 12,
  },
};
const STATS = DATA.actors.player;

