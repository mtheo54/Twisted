"use strict";
// ============================================================================
// Sim : toute la logique de jeu, sans affichage (même principe que core/sim.gd).
// Ordres en entrée, événements en sortie. Les projectiles sont de vrais objets
// de la Sim : le Gater peut donc les annuler.
// ============================================================================
class Sim {
  constructor(data) {
    this.data = data; this.time = 0; this.entities = new Map(); this.nextId = 1; this.commands = []; this.pending = []; this.events = [];
    this.projectiles = new Map(); this.nextProj = 1;
    this.world = { blocked: () => false }; // fourni par l'affichage : un mur entre deux points ?
  }
  spawn(archetype, position) {
    const def = this.data.actors[archetype];
    const e = { id: this.nextId++, archetype, team: def.team, maxHealth: def.max_health, health: def.max_health, radius: def.radius,
      alive: true, position: position.clone(), facing: new THREE.Vector3(0, 0, -1), readyAt: {}, invulnerableUntil: -1,
      lastHitAt: -1000, diedAt: -1, respawnDelay: def.respawn_delay ?? -1, ai: def.ai || null, aiNext: 0, aiWindup: null,
      busyUntil: 0, chain: null, chainUntil: 0, queued: -1, queuedCount: 0, lastDodgeAt: -10, diving: false,
      statuses: new Map(), gateAt: -10, hitLog: [], combatUntil: -1 };
    // les combattants sans IA (le joueur) portent des armes
    if (!def.ai) { e.weapon = this.data.start_weapons[0]; e.weapons = new Set(this.data.start_weapons); }
    if (def.dodge && def.dodge.charges) e.stamina = def.dodge.charges;
    // les monstres : état de leur IA (voir thinkMonster)
    if (def.ai === "monster") e.mon = { def: this.data.bestiary[archetype], windup: null, dash: null, busyUntil: 0, staggerUntil: 0, cd: {}, next: this.time + 1,
      strafe: Math.random() < 0.5 ? 1 : -1, strafeAt: 0, wander: null, wanderAt: 0, intent: new THREE.Vector3(), run: false, state: "idle", aggro: false };
    this.entities.set(e.id, e);
    return e.id;
  }
  despawn(id) { this.entities.delete(id); }
  setTransform(id, position, facing) { const e = this.entities.get(id); if (!e) return; e.position.copy(position); if (facing) e.facing.set(facing.x, 0, facing.z).normalize(); }
  queue(command) { this.commands.push(command); }
  drain() { const out = this.events; this.events = []; return out; }
  emit(ev) { ev.time = this.time; this.events.push(ev); }
  ready(e, key) { return this.time >= (e.readyAt[key] ?? 0); }
  cooldownLeft(e, key) { return Math.max(0, (e.readyAt[key] ?? 0) - this.time); }

  // --- Statuts ---------------------------------------------------------------
  mods(e) {
    const m = { dealt: 1, taken: 1, speed: 1, dodge_cd: 1, jumps: 0, no_dodge: false, tired: false, stunned: false, phase: false, reflect: false, super_jump: false, delay_boost: false };
    for (const st of e.statuses.values()) {
      const def = this.data.statuses[st.id], sm = def.mods || {};
      for (const k of ["dealt", "taken", "speed", "dodge_cd"]) if (sm[k] !== undefined) m[k] *= sm[k];
      if (sm.jumps) m.jumps += sm.jumps;
      for (const k of ["no_dodge", "tired", "stunned", "phase", "reflect", "super_jump", "delay_boost"]) if (sm[k]) m[k] = true;
      if (st.id === "crescendo") { m.dealt *= 1 + 0.1 * st.stacks; m.taken *= Math.max(1, 1.4 - 0.1 * st.stacks); }
    }
    return m;
  }
  applyStatus(e, id, opts = {}) {
    const def = this.data.statuses[id];
    const old = e.statuses.get(id);
    const st = { id, until: this.time + (opts.duration ?? def.duration), stacks: old ? old.stacks : 0, tickAt: this.time + (def.tick ? def.tick.every : 0), source: opts.source ?? null, target: opts.target ?? null };
    e.statuses.set(id, st);
    this.emit({ type: "status_added", id: e.id, status: id, until: st.until, kind: def.kind });
    return st;
  }
  removeStatus(e, id, reason) {
    const st = e.statuses.get(id); if (!st) return;
    e.statuses.delete(id);
    this.emit({ type: "status_removed", id: e.id, status: id, reason });
    const def = this.data.statuses[id];
    if (reason === "expired" && def.then) this.applyStatus(e, def.then);
    if (reason === "expired" && id === "all_in") {
      const t = this.entities.get(st.target);
      if (t && t.alive && e.alive) { this.emit({ type: "all_in_lost", id: e.id }); e.health = 0; this.kill(e, e.id); }
    }
  }
  heal(e, amount) {
    if (!e.alive || amount <= 0) return;
    const before = e.health; e.health = Math.min(e.maxHealth, e.health + amount);
    if (e.health > before) this.emit({ type: "heal", id: e.id, amount: e.health - before, health: e.health, max_health: e.maxHealth });
  }
  inCombat(e) { for (const o of this.entities.values()) if (o.team !== e.team && o.alive && o.combatUntil > this.time && o.position.distanceTo(e.position) < 25) return true; return false; }
  nearestEnemy(e, maxDist = 25) {
    let best = null, bd = maxDist;
    for (const o of this.entities.values()) { if (o.team === e.team || !o.alive) continue; const d = o.position.distanceTo(e.position); if (d < bd) { bd = d; best = o; } }
    return best;
  }

  // --- Boucle ----------------------------------------------------------------
  step(dt) {
    this.time += dt;
    const commands = this.commands; this.commands = [];
    for (const c of commands) this.runCommand(c);
    const waiting = [];
    for (const p of this.pending) {
      if (p.at > this.time) { waiting.push(p); continue; }
      const src = this.entities.get(p.source);
      if (!src || !src.alive) continue;
      if (p.kind === "move") this.resolveMove(src, p.move, p.hit || 0);
      else if (p.kind === "delay_hit") { const t = this.entities.get(p.target); if (t && t.alive) this.dealDamage(src.id, t.id, p.damage, { move: p.move, direction: p.direction, push: 1.5, delay: true, hitstop: 0.04 }); }
      else if (p.kind === "saw_shot") this.spawnProjectile(src, "saw", p.origin, p.dir);
    }
    this.pending = waiting;
    for (const e of this.entities.values()) {
      if (e.stamina !== undefined) { const d = this.data.actors[e.archetype].dodge; e.stamina = Math.min(d.charges, e.stamina + dt / (d.regen * this.mods(e).dodge_cd)); }
      if (e.queuedCount > 0 && this.time >= e.busyUntil) { const q = e.queued; e.queuedCount--; if (!e.queuedCount) e.queued = -1; if (this.time - q < 0.6) this.attack(e, false); else { e.queuedCount = 0; e.queued = -1; } }
      // statuts : expiration et effets périodiques
      for (const st of [...e.statuses.values()]) {
        const def = this.data.statuses[st.id];
        if (this.time >= st.until) { this.removeStatus(e, st.id, "expired"); continue; }
        if (def.tick && this.time >= st.tickAt && e.alive) {
          st.tickAt += def.tick.every;
          if (def.tick.damage) this.dealDamage(st.source, e.id, def.tick.damage, { dot: st.id });
          if (def.tick.heal) this.heal(e, def.tick.heal);
        }
      }
      if (e.ai && e.alive) this.think(e);
      if (e.respawnDelay < 0) continue;
      if (!e.alive && this.time - e.diedAt >= e.respawnDelay) { e.alive = true; e.health = e.maxHealth; e.statuses.clear(); this.emit({ type: "revived", id: e.id, health: e.health, max_health: e.maxHealth }); }
      else if (e.alive && e.health < e.maxHealth && this.time - e.lastHitAt >= 8) { e.health = e.maxHealth; this.emit({ type: "healed", id: e.id, health: e.health, max_health: e.maxHealth }); }
    }
    this.stepProjectiles(dt);
  }

  runCommand(c) {
    const src = this.entities.get(c.source);
    if (!src) return;
    if (c.type === "respawn") { src.alive = true; src.health = src.maxHealth; src.statuses.clear(); this.emit({ type: "revived", id: src.id, health: src.health, max_health: src.maxHealth }); return; }
    if (!src.alive) return;
    if (c.type === "attack") this.attack(src, !!c.airborne);
    else if (c.type === "dive_land") this.landDive(src);
    else if (c.type === "dodge") this.dodge(src, false);
    else if (c.type === "cast") this.cast(src, c.spell);
    else if (c.type === "wave") this.fireWave(src, c.wave, c.origin, c.dir, true);
    else if (c.type === "fuse") this.fuse(src, c.fusion, c.origin, c.dir);
    else if (c.type === "weapon") this.switchWeapon(src, c.weapon);
    else if (c.type === "unlock") { if (src.weapons && !src.weapons.has(c.weapon)) { src.weapons.add(c.weapon); this.emit({ type: "weapon_unlocked", source: src.id, weapon: c.weapon }); } }
  }
  // --- Armes -----------------------------------------------------------------------
  switchWeapon(src, id) {
    if (!src.weapons || !src.weapons.has(id) || src.weapon === id || src.diving) return;
    src.weapon = id; src.chain = null; src.queued = -1; src.queuedCount = 0;
    this.emit({ type: "weapon_changed", source: src.id, weapon: id });
  }

  // --- Corps à corps -----------------------------------------------------------
  breakChorus(src) { for (const st of [...src.statuses.values()]) if (this.data.statuses[st.id].break_on_attack) { this.removeStatus(src, st.id, "broken"); this.emit({ type: "chorus_broken", id: src.id }); } }
  attack(src, airborne) {
    this.breakChorus(src);
    if (airborne) {
      if (src.diving) return;
      src.diving = true; src.chain = null;
      this.emit({ type: "move_started", source: src.id, move: "dive", dive_speed: this.data.moves.dive.dive_speed });
      return;
    }
    if (this.time < src.busyUntil) { src.queued = this.time; src.queuedCount = Math.min(2, src.queuedCount + 1); return; }
    const wpn = this.data.weapons[src.weapon || "fists"];
    let move = wpn.combo;
    if (this.time - src.lastDodgeAt < 0.55) move = wpn.counter;
    else if (src.chain && this.time <= src.chainUntil) move = src.chain;
    const m = this.data.moves[move];
    src.busyUntil = this.time + m.windup + m.recover;
    src.chain = m.next || null; src.chainUntil = src.busyUntil + (m.window || 0);
    src.lastDodgeAt = -10;
    this.emit({ type: "move_started", source: src.id, move, windup: m.windup, recover: m.recover, lunge: m.lunge || 0, ghost: !!m.ghost });
    this.pending.push({ at: this.time + m.windup, kind: "move", source: src.id, move });
  }
  landDive(src) {
    if (!src.diving) return;
    src.diving = false; src.busyUntil = this.time + 0.35;
    const m = this.data.moves.dive;
    let hit = 0;
    for (const e of this.entities.values()) {
      if (e.id === src.id || !e.alive || e.team === src.team) continue;
      const to = e.position.clone().sub(src.position); to.y = 0;
      const dist = to.length();
      if (dist > m.radius + e.radius) continue;
      hit++;
      this.dealDamage(src.id, e.id, m.damage, { move: "dive", direction: dist > 0.01 ? to.divideScalar(dist) : src.facing.clone(), launch: m.launch, hitstop: m.hitstop, impact: true });
    }
    this.emit({ type: "dive_landed", source: src.id, hits: hit });
  }
  resolveMove(src, moveId, hitIndex = 0) {
    const m = this.data.moves[moveId], halfArc = THREE.MathUtils.degToRad(m.arc_deg / 2);
    // coups multiples (roulement, moulinet) : les frappes suivantes sont programmées
    const count = m.hits || 1, last = hitIndex === count - 1;
    if (hitIndex === 0) for (let i = 1; i < count; i++) this.pending.push({ at: this.time + i * m.every, kind: "move", source: src.id, move: moveId, hit: i });
    let hit = false;
    for (const e of this.entities.values()) {
      if (e.id === src.id || !e.alive || e.team === src.team) continue;
      const to = e.position.clone().sub(src.position); to.y = 0;
      const dist = to.length();
      if (dist > m.range + e.radius) continue;
      const dir = to.divideScalar(Math.max(dist, 0.001));
      if (dist > 0.05 && src.facing.angleTo(dir) > halfArc) continue;
      hit = true;
      const pull = m.pull ? Math.max(0, dist - e.radius - 1.0) : 0;
      this.dealDamage(src.id, e.id, m.damage, { move: moveId, hit: hitIndex, direction: m.pull ? dir.clone().negate() : dir, launch: last ? m.launch || null : null, push: m.pull ? 0 : m.push || 0,
        pull, hitstop: m.hitstop, impact: last && !!m.impact, stagger: m.stagger });
      if (m.stun && e.alive) { this.applyStatus(e, "stun", { duration: m.stun }); if (e.mon) { e.mon.windup = null; e.mon.dash = null; } e.aiWindup = null; }
    }
    if (!hit && hitIndex === 0) this.emit({ type: "attack_missed", source: src.id, move: moveId });
  }

  // --- Esquive (Bitcrush) --------------------------------------------------------
  dodge(src, free) {
    const d = this.data.actors[src.archetype].dodge, m = this.mods(src);
    if (!d || m.no_dodge) return false;
    // dash à charges (façon Ultrakill) : 3 barres d'endurance qui se rechargent
    if (d.charges) {
      if (!free) { if (src.stamina < 1) { this.emit({ type: "spell_not_ready", source: src.id, spell: "bitcrush" }); return false; } src.stamina -= 1; }
    } else {
      if (!free && !this.ready(src, "bitcrush")) return false;
      src.readyAt.bitcrush = this.time + this.data.spells.bitcrush.cooldown * m.dodge_cd;
    }
    src.invulnerableUntil = Math.max(src.invulnerableUntil, this.time + d.iframes);
    src.lastDodgeAt = this.time; src.busyUntil = this.time; src.queued = -1; src.queuedCount = 0;
    this.pending = this.pending.filter((p) => !(p.source === src.id && p.kind === "move"));
    for (const st of [...src.statuses.values()]) if (this.data.statuses[st.id].clear_on_dodge) this.removeStatus(src, st.id, "dodged");
    this.emit({ type: "dodge_started", source: src.id, distance: d.distance, duration: d.duration });
    return true;
  }

  // --- Sorts ---------------------------------------------------------------------
  cast(src, id) {
    const sp = this.data.spells[id];
    if (!sp) return;
    if (id === "bitcrush") { this.dodge(src, false); return; }
    if (!this.ready(src, id)) { this.emit({ type: "spell_not_ready", source: src.id, spell: id }); return; }
    src.readyAt[id] = this.time + sp.cooldown;
    if (id === "gater") { src.gateAt = this.time; this.emit({ type: "spell_cast", source: src.id, spell: id, window: sp.window }); return; }
    if (id === "delay") {
      const boost = this.mods(src).delay_boost;
      const hits = src.hitLog.filter((h) => this.time - h.t < sp.window + 1).slice(-3);
      const target = hits.length ? this.entities.get(hits[hits.length - 1].target) : null;
      if (!target || !target.alive || this.time - hits[hits.length - 1].t > sp.window) {
        src.readyAt[id] = this.time + sp.cooldown / 2;
        this.emit({ type: "spell_fizzle", source: src.id, spell: id }); return;
      }
      const dir = target.position.clone().sub(src.position).setY(0).normalize();
      hits.forEach((h, i) => this.pending.push({ at: this.time + 0.45 + i * 0.32, kind: "delay_hit", source: src.id, target: target.id, move: h.move, damage: Math.round(h.damage * (boost ? 1 : sp.mult)), direction: dir }));
      this.emit({ type: "delay_ghost", source: src.id, target: target.id, moves: hits.map((h) => h.move), from: src.position.clone() });
      return;
    }
    this.applyStatus(src, sp.status, { source: src.id });
    this.emit({ type: "spell_cast", source: src.id, spell: id });
  }
  // Sorts déjà fusionnés : équipés tels quels, ils ont leur propre recharge.
  fuse(src, fid, origin, dir) {
    const f = this.data.fusions.find((x) => x.id === fid);
    if (!f) return;
    const key = "fusion:" + f.id;
    if (!this.ready(src, key)) { this.emit({ type: "spell_not_ready", source: src.id, spell: key }); return; }
    if (f.combat_only && !this.inCombat(src)) { this.emit({ type: "fusion_refused", source: src.id, name: f.name, reason: "Seulement en combat" }); return; }
    if (f.effect === "teleport_behind") {
      const t = this.nearestEnemy(src, 25); if (!t) { this.emit({ type: "fusion_refused", source: src.id, name: f.name, reason: "Aucun ennemi proche" }); return; }
      const away = t.position.clone().sub(src.position).setY(0).normalize();
      this.emit({ type: "teleport", source: src.id, from: src.position.clone(), to: t.position.clone().addScaledVector(away, t.radius + 1.1), face: t.position.clone() });
      src.invulnerableUntil = this.time + 0.3;
    } else if (f.effect === "phase") { this.applyStatus(src, "phase"); this.dodge(src, true); }
    else if (f.effect === "triple_dodge") { src.invulnerableUntil = this.time + 0.9; this.dodge(src, true); this.emit({ type: "triple_dodge", source: src.id, count: 2 }); }
    else if (f.effect === "destabilize") {
      for (const e of this.entities.values()) if (e.team !== src.team && e.alive && e.position.distanceTo(src.position) < f.radius) { this.applyStatus(e, "stun", { duration: 2 }); e.aiWindup = null; }
      this.emit({ type: "destabilize", source: src.id, radius: f.radius });
    } else if (f.effect === "reverb_loop") {
      const recent = src.hitLog.length ? this.entities.get(src.hitLog[src.hitLog.length - 1].target) : null;
      const t = recent && recent.alive ? recent : this.nearestEnemy(src, 15);
      if (!t) { this.emit({ type: "fusion_refused", source: src.id, name: f.name, reason: "Aucune cible" }); return; }
      this.applyStatus(t, "reverb_loop", { source: src.id });
    } else if (f.effect === "all_in") {
      const recent = src.hitLog.length ? this.entities.get(src.hitLog[src.hitLog.length - 1].target) : null;
      const t = recent && recent.alive ? recent : this.nearestEnemy(src, 25);
      if (!t) { this.emit({ type: "fusion_refused", source: src.id, name: f.name, reason: "Aucune cible : trop risqué" }); return; }
      this.applyStatus(src, "all_in", { target: t.id });
    } else if (f.effect === "status") this.applyStatus(src, f.status, { source: src.id });
    else if (f.effect === "wave") { this.breakChorus(src); this.fireWave(src, f.wave, origin, dir, false); }
    src.readyAt[key] = this.time + f.cooldown;
    this.emit({ type: "fusion", source: src.id, name: f.name, a: f.a, b: f.b, effect: f.effect, wave: f.wave });
  }

  // --- Ondes de visée ------------------------------------------------------------
  fireWave(src, id, origin, dir, checkCooldown) {
    const w = this.data.waves[id];
    if (checkCooldown) { if (!this.ready(src, id)) { this.emit({ type: "spell_not_ready", source: src.id, spell: id }); return; } src.readyAt[id] = this.time + w.cooldown; this.breakChorus(src); }
    dir = dir.clone().normalize();
    if (id === "sine") {
      for (let i = 0; i < w.pellets; i++) {
        const a = THREE.MathUtils.degToRad(((i / (w.pellets - 1)) - 0.5) * w.spread_deg);
        this.spawnProjectile(src, "sine", origin, dir.clone().applyAxisAngle(new THREE.Vector3(0, 1, 0), a), { phase: i });
      }
    } else if (id === "square" || id === "pwm") this.beam(src, id, origin, dir);
    else if (id === "triangle") this.spawnProjectile(src, "triangle", origin, dir);
    else if (id === "soft") for (let i = 0; i < w.count; i++) this.spawnProjectile(src, "soft", origin, dir.clone().applyAxisAngle(new THREE.Vector3(0, 1, 0), (i - 1.5) * 0.35).add(new THREE.Vector3(0, 0.3, 0)).normalize());
    else if (id === "saw") for (let i = 0; i < w.count; i++) this.pending.push({ at: this.time + i * w.every, kind: "saw_shot", source: src.id, origin: origin.clone(), dir: dir.clone() });
    this.emit({ type: "wave_fired", source: src.id, wave: id, origin: origin.clone(), dir: dir.clone() });
  }
  beam(src, id, origin, dir) {
    const w = this.data.waves[id];
    let end = origin.clone().addScaledVector(dir, w.range);
    const hits = [];
    for (const e of this.entities.values()) {
      if (e.team === src.team || !e.alive) continue;
      const c = e.position.clone().add(new THREE.Vector3(0, 0.9, 0)), t = c.clone().sub(origin).dot(dir);
      if (t < 0 || t > w.range) continue;
      const closest = origin.clone().addScaledVector(dir, t);
      if (closest.distanceTo(c) < e.radius + (w.width ? w.width / 2 : 0.35) && !this.world.blocked(origin, closest)) hits.push({ e, t });
    }
    hits.sort((p, q) => p.t - q.t);
    const struck = id === "pwm" ? hits : hits.slice(0, 1);
    if (id === "square" && struck.length) end = origin.clone().addScaledVector(dir, struck[0].t);
    for (const h of struck) this.dealDamage(src.id, h.e.id, w.damage, { wave: id, direction: dir.clone().setY(0).normalize(), push: id === "square" ? 3 : 2, hitstop: id === "square" ? 0.09 : 0.06, impact: id === "square" });
    this.emit({ type: "beam", source: src.id, wave: id, from: origin.clone(), to: end, hit: struck.length });
  }
  spawnProjectile(src, kind, origin, dir, extra = {}) {
    const w = this.data.waves[kind] || this.data.enemy_attacks[kind];
    const p = { id: this.nextProj++, kind, owner: src.id, team: src.team, pos: origin.clone(), vel: dir.clone().normalize().multiplyScalar(w.speed), traveled: 0, range: w.range, homing: w.homing || 0, attack: !!extra.attack, phase: extra.phase || 0, born: this.time };
    this.projectiles.set(p.id, p);
    this.emit({ type: "projectile_spawned", id: p.id, kind, pos: p.pos.clone() });
  }
  projectileDamage(p) {
    if (p.kind === "sine") { const w = this.data.waves.sine; const f = Math.min(1, Math.max(0, (p.traveled - w.near) / (w.range - w.near))); return Math.round(w.dmg_near + (w.dmg_far - w.dmg_near) * f); }
    return (this.data.waves[p.kind] || this.data.enemy_attacks[p.kind]).damage;
  }
  stepProjectiles(dt) {
    for (const p of [...this.projectiles.values()]) {
      if (p.homing) {
        let best = null, bd = 30;
        for (const e of this.entities.values()) { if (e.team === p.team || !e.alive) continue; const d = e.position.distanceTo(p.pos); if (d < bd) { bd = d; best = e; } }
        if (best) {
          const want = best.position.clone().add(new THREE.Vector3(0, 0.9, 0)).sub(p.pos).normalize(), cur = p.vel.clone().normalize();
          const ang = cur.angleTo(want), turn = Math.min(ang, p.homing * dt);
          if (ang > 1e-4 && (p.kind !== "triangle" || ang < 1.2)) { const axis = cur.clone().cross(want).normalize(); if (axis.lengthSq() > 0.5) p.vel.applyAxisAngle(axis, turn); }
        }
      }
      const prev = p.pos.clone();
      p.pos.addScaledVector(p.vel, dt); p.traveled += p.vel.length() * dt;
      let ended = p.traveled > p.range || p.pos.y < -0.2;
      if (!ended && this.world.blocked(prev, p.pos)) ended = true;
      if (!ended) for (const e of this.entities.values()) {
        if (e.team === p.team || !e.alive) continue;
        const dx = e.position.x - p.pos.x, dz = e.position.z - p.pos.z;
        if (Math.hypot(dx, dz) < e.radius + 0.35 && p.pos.y > e.position.y - 0.3 && p.pos.y < e.position.y + 2.0) {
          if (this.mods(e).phase) continue;
          this.dealDamage(p.owner, e.id, this.projectileDamage(p), { wave: p.kind, attack: p.attack, projectile: p.id, direction: p.vel.clone().setY(0).normalize(), push: p.kind === "sine" ? 1.2 : 1.5, hitstop: 0.03 });
          ended = true; break;
        }
      }
      if (ended) { this.projectiles.delete(p.id); this.emit({ type: "projectile_end", id: p.id, kind: p.kind, pos: p.pos.clone() }); }
    }
  }

  // --- IA de l'enceinte (mode combat) ---------------------------------------------
  think(e) {
    if (e.ai === "monster") { this.thinkMonster(e); return; }
    const ea = this.data.enemy_attacks, m = this.mods(e);
    if (m.stunned) { e.aiWindup = null; return; }
    let player = null; for (const o of this.entities.values()) if (o.team !== e.team && o.alive) player = o;
    if (!player || e.combatUntil < this.time) { e.aiWindup = null; return; }
    const dist = player.position.distanceTo(e.position);
    if (dist > ea.aggro * 2) return;
    if (e.aiWindup) {
      if (this.time < e.aiWindup.at) return;
      const kind = e.aiWindup.kind; e.aiWindup = null; e.aiNext = this.time + ea.every;
      if (kind === "boom") {
        const a = ea.boom;
        this.emit({ type: "enemy_attack", id: e.id, attack: "boom", radius: a.radius });
        if (dist < a.radius + player.radius) this.dealDamage(e.id, player.id, a.damage, { attack: true, direction: player.position.clone().sub(e.position).setY(0).normalize(), push: a.push });
      } else {
        const origin = e.position.clone().add(new THREE.Vector3(0, 1.2, 0)), dir = player.position.clone().add(new THREE.Vector3(0, 1.0, 0)).sub(origin);
        this.spawnProjectile(e, "larsen", origin, dir, { attack: true });
        this.emit({ type: "enemy_attack", id: e.id, attack: "larsen" });
      }
      return;
    }
    if (this.time >= e.aiNext) {
      const kind = dist < ea.boom.radius ? "boom" : "larsen", w = ea[kind].windup;
      e.aiWindup = { kind, at: this.time + w };
      this.emit({ type: "enemy_windup", id: e.id, attack: kind, windup: w });
    }
  }

  // --- IA des monstres ----------------------------------------------------------------
  // La Sim décide (s'approcher, tourner autour, annoncer puis lancer une attaque) ;
  // l'affichage déplace le corps en suivant « intent » et gère les collisions.
  thinkMonster(e) {
    const M = e.mon, B = M.def, now = this.time, mods = this.mods(e);
    M.intent.set(0, 0, 0); M.run = false;
    let target = null, dist = Infinity;
    for (const o of this.entities.values()) {
      if (o.team === e.team || !o.alive || o.ai) continue;
      const d = o.position.distanceTo(e.position); if (d < dist) { dist = d; target = o; }
    }
    if (mods.stunned) { this.cancelWindup(e); M.dash = null; M.state = "stunned"; return; }
    if (now < M.staggerUntil) { M.state = "stagger"; return; }
    if (target && (dist < B.aggro || (M.aggro && dist < B.aggro * 1.8) || e.combatUntil > now)) M.aggro = true; else M.aggro = false;
    const to = target ? target.position.clone().sub(e.position).setY(0) : new THREE.Vector3(), flat = to.length();
    if (flat > 0.01) to.divideScalar(flat);
    // bond en cours : on fonce, la morsure part au contact
    if (M.dash) {
      M.state = "dash"; M.intent.copy(M.dash.dir).multiplyScalar(M.dash.speed); M.run = true;
      if (!M.dash.hit && target && flat < e.radius + target.radius + 0.55) { M.dash.hit = true; this.monsterHit(e, target, M.dash.atk, to); }
      if (now >= M.dash.until) { M.busyUntil = now + M.dash.atk.recover; M.dash = null; }
      return;
    }
    if (now < M.busyUntil) { M.state = "recover"; return; }
    if (M.windup) {
      M.state = "windup";
      const w = M.windup, atk = w.atk;
      // il suit sa cible pendant l'annonce, puis se fige juste avant de frapper
      if (target && now < w.at - atk.windup * 0.3) e.facing.copy(to);
      if (now < w.at) return;
      M.windup = null; M.cd[atk.id] = now + atk.cooldown; M.next = now + B.every * (0.7 + Math.random() * 0.6); M.busyUntil = now + atk.recover;
      this.emit({ type: "enemy_attack", id: e.id, attack: atk.id, kind: atk.kind, radius: atk.reach, arc: atk.arc });
      if (!target) return;
      const ang = e.facing.angleTo(to);
      if (atk.kind === "melee" || atk.kind === "cone") { if (flat <= atk.reach + target.radius && ang <= THREE.MathUtils.degToRad(atk.arc / 2)) this.monsterHit(e, target, atk, to); }
      else if (atk.kind === "aoe") { if (flat <= atk.reach + target.radius) this.monsterHit(e, target, atk, to); }
      else if (atk.kind === "lunge") { M.dash = { dir: e.facing.clone(), speed: atk.speed, until: now + atk.dur, atk, hit: false }; M.busyUntil = 0; }
      else if (atk.kind === "projectile") {
        const origin = e.position.clone().add(new THREE.Vector3(0, B.flying ? 1.4 : 1.3, 0)).addScaledVector(e.facing, e.radius + 0.2);
        this.spawnProjectile(e, atk.projectile, origin, target.position.clone().add(new THREE.Vector3(0, 1.0, 0)).sub(origin), { attack: true });
      } else if (atk.kind === "blink") {
        const a = Math.random() * Math.PI * 2, r = 3 + Math.random() * 1.5;
        this.emit({ type: "enemy_blink", id: e.id, to: target.position.clone().add(new THREE.Vector3(Math.cos(a) * r, 0, Math.sin(a) * r)) });
      }
      return;
    }
    if (!M.aggro) {
      // promenade : quelques pas au hasard, puis une pause
      M.state = "idle";
      if (now >= M.wanderAt) { M.wanderAt = now + 2 + Math.random() * 3; const a = Math.random() * Math.PI * 2; M.wander = Math.random() < 0.6 ? new THREE.Vector3(Math.cos(a), 0, Math.sin(a)) : null; }
      if (M.wander) { M.intent.copy(M.wander).multiplyScalar(B.speed * 0.5); e.facing.copy(M.wander); M.state = "walk"; }
      return;
    }
    e.combatUntil = Math.max(e.combatUntil, now + 6);
    e.facing.copy(to);
    // choisir une attaque à portée et prête
    if (now >= M.next) {
      const ok = B.attacks.filter((a) => flat >= (a.min || 0) && flat <= a.range && now >= (M.cd[a.id] || 0));
      if (ok.length) {
        const atk = ok[Math.floor(Math.random() * ok.length)];
        M.windup = { atk, at: now + atk.windup }; M.state = "windup";
        this.emit({ type: "enemy_windup", id: e.id, attack: atk.id, kind: atk.kind, windup: atk.windup, armor: !!atk.armor });
        return;
      }
    }
    // déplacement : approcher, reculer, ou tourner autour de la cible
    if (now >= M.strafeAt) { M.strafeAt = now + 1.2 + Math.random() * 2; if (Math.random() < 0.5) M.strafe *= -1; }
    if (flat > B.keep + 0.8) { M.run = flat > 5; M.intent.copy(to).multiplyScalar(M.run ? B.run : B.speed); M.state = M.run ? "run" : "walk"; }
    else if (flat < B.keep - 0.7) { M.intent.copy(to).multiplyScalar(-B.speed * 0.7); M.state = "walk"; }
    else { M.intent.set(-to.z, 0, to.x).multiplyScalar(M.strafe * B.speed * 0.55); M.state = "strafe"; }
  }
  cancelWindup(e) { if (e.mon && e.mon.windup) { e.mon.windup = null; this.emit({ type: "enemy_interrupt", id: e.id }); } }
  monsterHit(e, target, atk, dir) {
    this.dealDamage(e.id, target.id, atk.damage, { attack: true, enemy_attack: atk.id, direction: dir.clone(), push: atk.push || 2, knockdown: !!atk.knockdown });
  }

  // --- Dégâts ------------------------------------------------------------------------
  kill(t, sourceId) {
    t.alive = false; t.diedAt = this.time; t.aiWindup = null;
    this.emit({ type: "died", id: t.id, source: sourceId });
    // Quitte ou double gagné : la cible est tombée à temps
    for (const e of this.entities.values()) { const st = e.statuses.get("all_in"); if (st && st.target === t.id) { this.removeStatus(e, "all_in", "won"); this.emit({ type: "all_in_won", id: e.id }); } }
    for (const id of [...t.statuses.keys()]) this.removeStatus(t, id, "death");
  }
  dealDamage(sourceId, targetId, amount, extra = {}) {
    const t = this.entities.get(targetId), src = this.entities.get(sourceId);
    if (!t || !t.alive) return;
    // Gater : contre placé juste avant le coup adverse
    if (extra.attack) {
      const sp = this.data.spells.gater, dg = this.time - t.gateAt;
      if (dg >= 0 && dg <= sp.window) {
        t.gateAt = -10;
        if (dg <= sp.perfect) {
          t.readyAt.gater = this.time + 1;
          if (src) { this.applyStatus(src, "stun", { duration: sp.stun }); src.aiWindup = null; }
          this.emit({ type: "gate_success", id: t.id, perfect: true, attacker: sourceId, projectile: extra.projectile ?? null });
          return;
        }
        amount *= 1 - sp.reduce;
        this.emit({ type: "gate_success", id: t.id, perfect: false, attacker: sourceId, projectile: extra.projectile ?? null });
      }
    }
    const tm = this.mods(t);
    if (tm.phase || this.time < t.invulnerableUntil) { this.emit({ type: "damage_avoided", source: sourceId, target: targetId }); return; }
    if (src && !extra.dot) amount *= this.mods(src).dealt;
    amount *= tm.taken;
    amount = Math.max(1, Math.round(amount));
    // Renvoi (Distortion + Chorus) : les dégâts repartent vers l'attaquant
    if (tm.reflect && src && src !== t && !extra.reflected) {
      this.emit({ type: "reflect", id: t.id, attacker: src.id, amount });
      this.heal(t, Math.round(amount * 0.5));
      this.dealDamage(t.id, src.id, amount, { reflected: true, direction: src.position.clone().sub(t.position).setY(0).normalize(), push: 2 });
      return;
    }
    t.health = Math.max(0, t.health - amount); t.lastHitAt = this.time;
    // monstres : un coup les fait vaciller et annule leur attaque, sauf s'ils sont « blindés »
    if (t.mon && !extra.dot) {
      const armored = t.mon.windup && t.mon.windup.atk.armor && !extra.impact;
      if (!armored) { t.mon.staggerUntil = this.time + (extra.stagger ?? (extra.impact ? 0.75 : 0.32)) * (t.mon.def.heavy ? 0.6 : 1); this.cancelWindup(t); t.mon.dash = null; }
      t.mon.aggro = true;
    }
    // le joueur touché : court étourdissement, ou chute (knockdown) avec invincibilité pour se relever
    if (!t.ai && extra.attack) {
      t.busyUntil = Math.max(t.busyUntil, this.time + (extra.knockdown ? 1.0 : 0.18));
      if (extra.knockdown) { t.invulnerableUntil = this.time + 1.25; t.queued = -1; t.queuedCount = 0; t.chain = null; }
    }
    if (src && src.team !== t.team) { t.combatUntil = this.time + 12; src.combatUntil = this.time + 12; }
    if (src && !extra.dot && !extra.delay && !extra.reflected) {
      src.hitLog.push({ t: this.time, target: t.id, move: extra.move || extra.wave || "jab1", damage: amount });
      if (src.hitLog.length > 6) src.hitLog.shift();
      const cr = src.statuses.get("crescendo"); if (cr) cr.stacks = Math.min(10, cr.stacks + 1);
      if (src.statuses.has("reverb_aura") && t.alive) this.applyStatus(t, "reverb_dot", { source: src.id });
    }
    this.emit(Object.assign({ type: "damage", source: sourceId, target: targetId, amount, health: t.health, max_health: t.maxHealth }, extra));
    if (t.health <= 0) this.kill(t, sourceId);
  }
}

