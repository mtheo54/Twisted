## La Sim : toute la logique de jeu (vie, dégâts, attaques, esquives), sans
## aucun affichage.
##
## Les scènes lui envoient des ORDRES (queue_command), elle avance par pas
## fixes (step) et produit des ÉVÉNEMENTS (drain_events) que l'affichage
## illustre. Rien ici ne dépend d'un nœud Godot : en multijoueur, le serveur
## fera tourner la Sim et les joueurs n'enverront que leurs ordres.
extends RefCounted

const EntityState = preload("res://core/entity_state.gd")
const DataDB = preload("res://core/data_db.gd")

var data: DataDB
var time := 0.0
var entities: Dictionary = {}
var _next_id := 1
var _commands: Array[Dictionary] = []
## actions différées, par exemple le coup qui part à la fin de l'élan
var _pending: Array[Dictionary] = []
var _events: Array[Dictionary] = []


func _init(p_data: DataDB) -> void:
	data = p_data


func spawn(archetype: String, position: Vector3) -> int:
	var def := data.actor(archetype)
	if def.is_empty():
		push_error("Sim : archétype inconnu %s" % archetype)
	var e := EntityState.new()
	e.id = _next_id
	_next_id += 1
	e.archetype = archetype
	e.team = int(def.get("team", 0))
	e.max_health = float(def.get("max_health", 100.0))
	e.health = e.max_health
	e.radius = float(def.get("radius", 0.5))
	e.respawn_delay = float(def.get("respawn_delay", -1.0))
	e.position = position
	entities[e.id] = e
	_emit({"type": "spawned", "id": e.id, "archetype": archetype})
	return e.id


func get_entity(id: int) -> EntityState:
	return entities.get(id)


## Recopie la position venue de la physique de Godot.
func set_transform(id: int, position: Vector3, facing: Vector3) -> void:
	var e := get_entity(id)
	if e == null:
		return
	e.position = position
	var flat := Vector3(facing.x, 0.0, facing.z)
	if flat.length_squared() > 0.0001:
		e.facing = flat.normalized()


func queue_command(command: Dictionary) -> void:
	_commands.append(command)


func step(dt: float) -> void:
	time += dt
	var commands := _commands
	_commands = []
	for command in commands:
		_run_command(command)
	_run_pending()
	_update_respawns()


func drain_events() -> Array[Dictionary]:
	var out := _events
	_events = []
	return out


func deal_damage(source_id: int, target_id: int, amount: float, extra: Dictionary = {}) -> void:
	var target := get_entity(target_id)
	if target == null or not target.alive:
		return
	if target.is_invulnerable(time):
		_emit({"type": "damage_avoided", "source": source_id, "target": target_id})
		return
	target.health = maxf(0.0, target.health - amount)
	target.last_hit_at = time
	var event := {
		"type": "damage",
		"source": source_id,
		"target": target_id,
		"amount": amount,
		"health": target.health,
		"max_health": target.max_health,
		"position": target.position,
	}
	event.merge(extra)
	_emit(event)
	if target.health <= 0.0:
		target.alive = false
		target.died_at = time
		_emit({"type": "died", "id": target_id, "source": source_id})


# --- Ordres -----------------------------------------------------------------

func _run_command(command: Dictionary) -> void:
	var source := get_entity(int(command.get("source", 0)))
	if source == null or not source.alive:
		return
	match str(command.get("type", "")):
		"melee":
			_start_melee(source, str(command.get("weapon", "fist")))
		"dodge":
			_start_dodge(source)
		_:
			push_warning("Sim : ordre inconnu %s" % command)


func _start_melee(source: EntityState, weapon_id: String) -> void:
	var weapon := data.weapon(weapon_id)
	if weapon.is_empty():
		return
	var key := "weapon:" + weapon_id
	if not source.can_use(key, time):
		return
	source.ready_at[key] = time + float(weapon.get("cooldown", 0.5))
	var windup := float(weapon.get("windup", 0.0))
	_emit({"type": "attack_started", "source": source.id, "weapon": weapon_id, "windup": windup})
	_pending.append({"at": time + windup, "kind": "melee_hit", "source": source.id, "weapon": weapon_id})


func _start_dodge(source: EntityState) -> void:
	var dodge: Dictionary = data.actor(source.archetype).get("dodge", {})
	if dodge.is_empty() or not source.can_use("dodge", time):
		return
	source.ready_at["dodge"] = time + float(dodge.get("cooldown", 0.8))
	source.invulnerable_until = maxf(source.invulnerable_until, time + float(dodge.get("iframes", 0.0)))
	_emit({
		"type": "dodge_started",
		"source": source.id,
		"distance": float(dodge.get("distance", 4.0)),
		"duration": float(dodge.get("duration", 0.3)),
	})


# --- Actions différées ------------------------------------------------------

func _run_pending() -> void:
	var waiting: Array[Dictionary] = []
	for action in _pending:
		if float(action["at"]) > time:
			waiting.append(action)
			continue
		match str(action["kind"]):
			"melee_hit":
				var source := get_entity(int(action["source"]))
				if source != null and source.alive:
					_resolve_melee(source, str(action["weapon"]))
	_pending = waiting


## Touche toutes les entités adverses dans la portée et l'arc de l'arme.
func _resolve_melee(source: EntityState, weapon_id: String) -> void:
	var weapon := data.weapon(weapon_id)
	var reach := float(weapon.get("range", 1.5))
	var half_arc := deg_to_rad(float(weapon.get("arc_deg", 90.0)) * 0.5)
	var hit_any := false
	for e: EntityState in entities.values():
		if e.id == source.id or not e.alive or e.team == source.team:
			continue
		var to := e.position - source.position
		to.y = 0.0
		var dist := to.length()
		if dist > reach + e.radius:
			continue
		var dir := to / maxf(dist, 0.001)
		if dist > 0.05 and source.facing.angle_to(dir) > half_arc:
			continue
		hit_any = true
		deal_damage(source.id, e.id, float(weapon.get("damage", 10.0)), {
			"knockback": float(weapon.get("knockback", 0.0)),
			"direction": dir,
		})
	if not hit_any:
		_emit({"type": "attack_missed", "source": source.id, "weapon": weapon_id})


func _update_respawns() -> void:
	for e: EntityState in entities.values():
		if e.respawn_delay < 0.0:
			continue
		if not e.alive and time - e.died_at >= e.respawn_delay:
			e.alive = true
			e.health = e.max_health
			_emit({"type": "revived", "id": e.id, "health": e.health, "max_health": e.max_health})
		elif e.alive and e.health < e.max_health and time - e.last_hit_at >= e.respawn_delay * 2.0:
			e.health = e.max_health
			_emit({"type": "healed", "id": e.id, "health": e.health, "max_health": e.max_health})


func _emit(event: Dictionary) -> void:
	event["time"] = time
	_events.append(event)
