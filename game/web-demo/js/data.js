"use strict";
// ============================================================================
// Données (équivalent de game/data/*.json)
// ============================================================================
const DATA = {
  actors: {
    // Déplacements façon Ultrakill : course rapide permanente, dash à 3 charges (Maj),
    // glissade (C au sol), écrasement (C en l'air), élan conservé en l'air.
    player: { team: 1, max_health: 100, radius: 0.35, walk_speed: 11, sprint_speed: 24, slide_speed: 17, accel: 110, air_accel: 40, overspeed_decay: 14,
      jump_velocity: 9.6, double_jump_velocity: 9, super_jump_velocity: 19, gravity: 27, wall_jump: { up: 10, push: 10 }, wall_jumps: 3, wall_slide_speed: 3.2,
      slam_speed: 55, slam_bounce: 0.55, dash_jump: 0.8,
      dodge: { distance: 7, duration: 0.18, cooldown: 0.7, iframes: 0.22, charges: 3, regen: 0.9 } },
    training_dummy: { team: 2, max_health: 160, radius: 0.55, respawn_delay: 3, ai: "speaker" },
    // Bestiaire : voir « bestiary » plus bas pour leurs attaques
    gresillon:  { team: 2, max_health: 40,  radius: 0.4,  ai: "monster" },
    cable:      { team: 2, max_health: 95,  radius: 0.45, ai: "monster" },
    gueule:     { team: 2, max_health: 200, radius: 0.75, ai: "monster" },
    ombre_sub:  { team: 2, max_health: 120, radius: 0.6,  ai: "monster" },
  },
  // Coups et enchaînements : « next » = coup suivant si l'on reclique à temps.
  moves: {
    jab1:    { damage: 8,  range: 1.9, arc_deg: 120, windup: 0.08, recover: 0.2,  window: 0.45, next: "jab2", lunge: 3.2, push: 1.4, hitstop: 0.05 },
    jab2:    { damage: 10, range: 1.9, arc_deg: 120, windup: 0.08, recover: 0.22, window: 0.45, next: "drop", lunge: 3.2, push: 1.8, hitstop: 0.06 },
    drop:    { damage: 24, range: 2.2, arc_deg: 150, windup: 0.2,  recover: 0.5,  lunge: 5.5, launch: [17, 7], hitstop: 0.14, impact: true, label: "DROP!" },
    counter: { damage: 14, range: 2.4, arc_deg: 170, windup: 0.04, recover: 0.3,  window: 0.45, next: "jab2", lunge: 10, push: 5, hitstop: 0.08, ghost: true, label: "CONTRE-GLITCH" },
    dive:    { damage: 22, radius: 4.2, launch: [9, 9], hitstop: 0.12, impact: true, dive_speed: 55, label: "ÉCRASEMENT" },
    // Baguettes : rapides, quatre coups, le dernier est un roulement de 5 frappes
    st1: { damage: 6, range: 2.0, arc_deg: 120, windup: 0.05, recover: 0.13, window: 0.4, next: "st2", lunge: 3, push: 0.8, hitstop: 0.035 },
    st2: { damage: 6, range: 2.0, arc_deg: 120, windup: 0.05, recover: 0.13, window: 0.4, next: "st3", lunge: 3, push: 0.8, hitstop: 0.035 },
    st3: { damage: 7, range: 2.0, arc_deg: 140, windup: 0.06, recover: 0.16, window: 0.4, next: "st4", lunge: 3.4, push: 1, hitstop: 0.04 },
    st4: { damage: 4, hits: 5, every: 0.06, range: 2.2, arc_deg: 150, windup: 0.08, recover: 0.4, lunge: 4, push: 0.5, launch: [9, 6], hitstop: 0.03, impact: true, label: "ROULEMENT!" },
    // Pied de micro : bâton, grande allonge, balayage puis moulinet tout autour
    ms1: { damage: 11, range: 2.9, arc_deg: 200, windup: 0.12, recover: 0.24, window: 0.45, next: "ms2", lunge: 2.5, push: 2.4, hitstop: 0.06 },
    ms2: { damage: 13, range: 3.4, arc_deg: 50, windup: 0.1, recover: 0.26, window: 0.45, next: "ms3", lunge: 5, push: 3.5, hitstop: 0.07 },
    ms3: { damage: 9, hits: 3, every: 0.12, range: 3.2, arc_deg: 360, windup: 0.14, recover: 0.42, lunge: 1.5, push: 1.5, launch: [8, 7], hitstop: 0.05, impact: true, label: "MOULINET!" },
    // Guitare-hache : lente et lourde, le 3e coup est un accord qui fait trembler le sol
    gt1: { damage: 17, range: 2.7, arc_deg: 150, windup: 0.2, recover: 0.32, window: 0.5, next: "gt2", lunge: 3, push: 3, hitstop: 0.09, stagger: 0.55 },
    gt2: { damage: 19, range: 2.7, arc_deg: 150, windup: 0.2, recover: 0.34, window: 0.5, next: "gt3", lunge: 3, push: 3.2, hitstop: 0.09, stagger: 0.55 },
    gt3: { damage: 34, range: 3.8, arc_deg: 360, windup: 0.36, recover: 0.6, lunge: 2, launch: [12, 9], hitstop: 0.16, impact: true, label: "POWER CHORD!" },
    // Micro-fléau : micro au bout de son câble, très longue portée, le 3e coup ramène l'ennemi
    fl1: { damage: 9, range: 4.8, arc_deg: 60, windup: 0.12, recover: 0.22, window: 0.45, next: "fl2", lunge: 1, push: 1.5, hitstop: 0.05 },
    fl2: { damage: 10, range: 4.8, arc_deg: 60, windup: 0.12, recover: 0.22, window: 0.45, next: "fl3", lunge: 1, push: 1.5, hitstop: 0.05 },
    fl3: { damage: 14, range: 5.6, arc_deg: 70, windup: 0.2, recover: 0.4, lunge: 0, pull: 7, stun: 1, hitstop: 0.1, impact: true, label: "LARSEN-CROCHET!" },
    // Twisted Sword : l'arme signature, lame torsadée. Deux taillades, un estoc, puis la torsion (2 tours)
    tw1: { damage: 13, range: 2.7, arc_deg: 150, windup: 0.07, recover: 0.15, window: 0.45, next: "tw2", lunge: 4.5, push: 2, hitstop: 0.05 },
    tw2: { damage: 14, range: 2.7, arc_deg: 150, windup: 0.07, recover: 0.15, window: 0.45, next: "tw3", lunge: 4.5, push: 2, hitstop: 0.05 },
    tw3: { damage: 17, range: 3.3, arc_deg: 50, windup: 0.08, recover: 0.18, window: 0.45, next: "tw4", lunge: 8, push: 3, hitstop: 0.06, anim: "ms2" },
    tw4: { damage: 9, hits: 4, every: 0.07, range: 3.1, arc_deg: 360, windup: 0.1, recover: 0.38, lunge: 2, push: 1, launch: [10, 10], hitstop: 0.04, impact: true, label: "TWIST!", anim: "ms3" },
    // Boom Box : lourde, le 3e coup « lâche les basses » : une onde qui part loin devant
    bb1: { damage: 20, range: 2.6, arc_deg: 150, windup: 0.2, recover: 0.3, window: 0.5, next: "bb2", lunge: 3, push: 4, hitstop: 0.09, stagger: 0.6, anim: "gt1" },
    bb2: { damage: 22, range: 2.6, arc_deg: 150, windup: 0.2, recover: 0.32, window: 0.5, next: "bb3", lunge: 3, push: 4.5, hitstop: 0.09, stagger: 0.6, anim: "gt2" },
    bb3: { damage: 36, range: 7.5, arc_deg: 70, windup: 0.34, recover: 0.55, lunge: 1, launch: [16, 8], hitstop: 0.16, impact: true, label: "DROP THE BASS!", anim: "gt3" },
    // Faux à cordes : balayages immenses, la moisson ramène tout le monde vers toi
    sc1: { damage: 15, range: 3.7, arc_deg: 240, windup: 0.14, recover: 0.24, window: 0.45, next: "sc2", lunge: 2, push: 2, hitstop: 0.06, anim: "ms1" },
    sc2: { damage: 16, range: 3.7, arc_deg: 240, windup: 0.14, recover: 0.26, window: 0.45, next: "sc3", lunge: 2, push: 2, hitstop: 0.06, anim: "gt2" },
    sc3: { damage: 13, hits: 2, every: 0.18, range: 4, arc_deg: 360, windup: 0.16, recover: 0.45, lunge: 0, pull: 4, hitstop: 0.07, impact: true, label: "MOISSON!", anim: "ms3" },
    // Diapason : lance rapide et précise, le 3e coup fait vibrer la cible (étourdie)
    df1: { damage: 12, range: 4, arc_deg: 40, windup: 0.07, recover: 0.15, window: 0.4, next: "df2", lunge: 6, push: 3, hitstop: 0.05, anim: "ms2" },
    df2: { damage: 12, range: 4, arc_deg: 40, windup: 0.07, recover: 0.15, window: 0.4, next: "df3", lunge: 6, push: 3, hitstop: 0.05, anim: "ms2" },
    df3: { damage: 18, range: 4.4, arc_deg: 50, windup: 0.14, recover: 0.32, lunge: 10, push: 2, stun: 1.6, hitstop: 0.1, impact: true, label: "RÉSONANCE!", anim: "ms2" },
    // Poings sub : gros gants-enceintes, l'uppercut envoie en l'air
    sg1: { damage: 14, range: 2.1, arc_deg: 120, windup: 0.09, recover: 0.2, window: 0.45, next: "sg2", lunge: 4, push: 4, hitstop: 0.07, anim: "jab1" },
    sg2: { damage: 16, range: 2.1, arc_deg: 120, windup: 0.09, recover: 0.2, window: 0.45, next: "sg3", lunge: 4, push: 5, hitstop: 0.07, anim: "jab2" },
    sg3: { damage: 30, range: 2.4, arc_deg: 140, windup: 0.22, recover: 0.45, lunge: 3, launch: [3, 16], hitstop: 0.14, impact: true, label: "INFRA-UPPERCUT!", anim: "uppercut" },
  },
  // Armes (pas d'armes à feu). « combo » = premier coup ; les suivants viennent de « next ».
  // On en change avec X ou la molette. Au départ : les poings ; les autres armes sont données par les PNJ.
  weapons: {
    twisted_sword: { name: "Twisted Sword", short: "TWST", color: "#c86bff", combo: "tw1", counter: "counter", desc: "La lame torsadée : deux taillades, un estoc qui fonce, puis la torsion qui hache tout autour et projette." },
    fists:     { name: "Poings", short: "✊", color: "#f3ece6", combo: "jab1", counter: "counter", desc: "Jab, jab, puis un drop qui projette." },
    sticks:    { name: "Baguettes", short: "BAG", color: "#e8c08a", combo: "st1", counter: "counter", hint: "Kenji, le batteur du carrefour, en a toujours une paire de rechange.", desc: "Très rapides : trois frappes puis un roulement de 5 coups qui projette." },
    mic_stand: { name: "Pied de micro", short: "PIED", color: "#c9d2e6", combo: "ms1", counter: "counter", hint: "DJ Taro, sur la place du parc, t'en confiera une… si tu l'aides.", desc: "Bâton à grande allonge : balayage, estoc, puis moulinet tout autour." },
    guitar:    { name: "Guitare-hache", short: "GTR", color: "#ff7a5c", combo: "gt1", counter: "counter", hint: "Rin, la luthière de la rue commerçante, cherche quelque chose de précieux.", desc: "Lente et lourde : deux grands coups, puis un power chord qui fait trembler le sol." },
    flail:     { name: "Micro-fléau", short: "FLÉAU", color: "#b9a6ff", combo: "fl1", counter: "counter", hint: "Yuna chante devant le karaoké, la nuit tombée.", desc: "Le micro au bout de son câble : frappe de loin, le 3e coup ramène l'ennemi et l'étourdit." },
    boombox:   { name: "Boom Box", short: "BOOM", color: "#ff5a3a", combo: "bb1", counter: "counter", hint: "Sur un autel, au fond du parc, entre les arbres.", desc: "Lourde : deux grands coups puis « Drop the bass », une onde qui balaie tout loin devant." },
    scythe:    { name: "Faux à cordes", short: "FAUX", color: "#9a7bff", combo: "sc1", counter: "counter", hint: "Sur un autel, dans la ruelle aux murs tagués.", desc: "Balayages immenses ; la moisson tourne deux fois et ramène les ennemis vers toi." },
    fork:      { name: "Diapason", short: "DIAP", color: "#8fe6ff", combo: "df1", counter: "counter", hint: "Sur un autel, sur le toit du karaoké. Il faut grimper.", desc: "Lance rapide qui fonce sur la cible ; le 3e coup la fait vibrer : étourdie." },
    sub_fists: { name: "Poings sub", short: "SUB", color: "#ff9a5a", combo: "sg1", counter: "counter", hint: "Sur un autel, tout au bout de la rue, au nord.", desc: "Gants-enceintes : coups très lourds, l'infra-uppercut envoie l'ennemi en l'air." },
  },
  start_weapons: ["twisted_sword", "fists"],
  // Sorts (effets audio). Le Bitcrush est l'esquive de base, toujours sur C.
  spells: {
    bitcrush:   { name: "Bitcrush", short: "DASH", color: "#9fe6ff", cooldown: 0.9, desc: "Dash (Maj) : 3 charges qui se rechargent. Invincible pendant le dash. Saute pendant un dash au sol pour un dash-saut." },
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
    // projectiles des monstres
    spark: { speed: 15, damage: 9, homing: 0.35, range: 22 },
    sub:   { speed: 6.5, damage: 12, homing: 1.3, range: 24 },
  },
  // Bestiaire. Chaque attaque est annoncée (windup) : on peut l'esquiver ou la contrer au Gater.
  //  kind : melee (coup devant soi), lunge (bond sur la cible), projectile, aoe (onde autour), cone (cri devant), blink (téléportation)
  //  armor : le monstre n'est pas interrompu par tes coups pendant l'annonce.
  bestiary: {
    gresillon: { name: "Grésillon", night: true, desc: "Petit parasite de larsen, pattes en fil de cuivre. Bondit sur toi.",
      speed: 3, run: 7.5, keep: 1.2, aggro: 30, every: 1.0,
      attacks: [
        { id: "morsure", kind: "melee", range: 1.4, reach: 1.5, arc: 100, windup: 0.32, recover: 0.5, damage: 6, push: 2, cooldown: 1.2 },
        { id: "bond", kind: "lunge", min: 2.4, range: 6.5, windup: 0.5, recover: 0.7, damage: 9, speed: 13, dur: 0.38, push: 3, cooldown: 3.5 },
      ] },
    cable: { name: "Câblé", night: true, desc: "Un corps de câbles emmêlés et une tête de jack. Fouette de loin, envoie des étincelles.",
      speed: 2.4, run: 6, keep: 2.6, aggro: 30, every: 1.4,
      attacks: [
        { id: "fouet", kind: "melee", range: 3.4, reach: 3.6, arc: 70, windup: 0.55, recover: 0.6, damage: 11, push: 3.5, cooldown: 2.4 },
        { id: "griffe", kind: "melee", range: 1.8, reach: 2.0, arc: 120, windup: 0.32, recover: 0.45, damage: 8, push: 2, cooldown: 1.4 },
        { id: "etincelle", kind: "projectile", min: 5, range: 15, windup: 0.75, recover: 0.6, projectile: "spark", cooldown: 4 },
      ] },
    gueule: { name: "Gueule-enceinte", night: true, desc: "Une enceinte affamée sur de longues pattes de câble. Sa membrane est une gueule.",
      speed: 2.2, run: 7.5, keep: 2.2, aggro: 35, every: 1.3, heavy: true,
      attacks: [
        { id: "morsure", kind: "lunge", min: 2.2, range: 6, windup: 0.6, recover: 0.9, damage: 17, speed: 14, dur: 0.36, push: 6, knockdown: true, armor: true, cooldown: 3.2 },
        { id: "griffes", kind: "melee", range: 2.4, reach: 2.7, arc: 130, windup: 0.42, recover: 0.55, damage: 11, push: 3, cooldown: 1.5 },
        { id: "cri", kind: "cone", min: 2, range: 8, reach: 8.5, arc: 60, windup: 0.95, recover: 0.8, damage: 14, push: 7, armor: true, cooldown: 6 },
      ] },
    ombre_sub: { name: "Ombre sub", night: true, flying: true, desc: "Basse fréquence qui flotte, traînant ses câbles. Pulse, disparaît, réapparaît.",
      speed: 3, run: 5.5, keep: 4.5, aggro: 30, every: 1.5,
      attacks: [
        { id: "pulsation", kind: "aoe", range: 3.4, reach: 3.8, windup: 0.85, recover: 0.7, damage: 13, push: 6, armor: true, cooldown: 3.5 },
        { id: "infrabasse", kind: "projectile", min: 3, range: 16, windup: 0.7, recover: 0.6, projectile: "sub", cooldown: 3 },
        { id: "fondu", kind: "blink", min: 7, range: 30, windup: 0.5, recover: 0.4, cooldown: 7 },
      ] },
  },
};
const STATS = DATA.actors.player;

