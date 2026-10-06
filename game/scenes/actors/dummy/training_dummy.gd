## Enceinte d'entraînement : une cible immobile pour tester les attaques.
## Sa vie est dans la Sim ; elle revient debout quelques secondes après être
## tombée (voir data/actors.json, « training_dummy »).
extends StaticBody3D

const Sim = preload("res://core/sim.gd")
const FloatingText = preload("res://scenes/game/floating_text.gd")

var sim: Sim
var entity_id := 0

var _visual: Node3D
var _label: Label3D
var _cone_material: StandardMaterial3D
var _tween: Tween


func setup(p_sim: Sim, p_entity_id: int) -> void:
	sim = p_sim
	entity_id = p_entity_id


func _ready() -> void:
	collision_layer = 0
	set_collision_layer_value(3, true)
	collision_mask = 0
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(0.9, 1.75, 0.7)
	shape.shape = box
	shape.position = Vector3(0.0, 0.875, 0.0)
	add_child(shape)

	_visual = Node3D.new()
	add_child(_visual)
	var cabinet_material := _material(Color(0.13, 0.12, 0.15), 0.6)
	_cone_material = _material(Color(0.22, 0.22, 0.25), 0.35)
	_cone_material.emission_enabled = true
	_cone_material.emission = Color(1.0, 0.55, 0.3)
	_cone_material.emission_energy_multiplier = 0.0
	var trim_material := _material(Color(0.85, 0.42, 0.25), 0.4)

	_box(Vector3(1.1, 0.22, 0.9), Vector3(0.0, 0.11, 0.0), _material(Color(0.3, 0.3, 0.34), 0.8))
	_box(Vector3(0.9, 1.5, 0.7), Vector3(0.0, 0.97, 0.0), cabinet_material)
	_box(Vector3(0.92, 0.05, 0.72), Vector3(0.0, 1.72, 0.0), trim_material)
	_cone(0.3, Vector3(0.0, 0.72, 0.36))
	_cone(0.13, Vector3(0.0, 1.38, 0.36))

	_label = Label3D.new()
	_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_label.position = Vector3(0.0, 2.15, 0.0)
	_label.pixel_size = 0.006
	_label.font_size = 40
	_label.outline_size = 10
	_label.outline_modulate = Color(0.08, 0.06, 0.12)
	add_child(_label)
	_update_label(1.0, 1.0)
	if sim:
		var e := sim.get_entity(entity_id)
		if e:
			_update_label(e.health, e.max_health)


func on_sim_event(event: Dictionary) -> void:
	match str(event["type"]):
		"damage":
			if int(event["target"]) != entity_id:
				return
			_update_label(float(event["health"]), float(event["max_health"]))
			FloatingText.spawn(get_parent(), global_position + Vector3.UP * 1.9, str(int(event["amount"])), Color(1.0, 0.85, 0.5))
			_hit_reaction()
		"died":
			if int(event["id"]) == entity_id:
				_topple(true)
		"revived", "healed":
			if int(event["id"]) == entity_id:
				_update_label(float(event["health"]), float(event["max_health"]))
				if str(event["type"]) == "revived":
					_topple(false)


func _hit_reaction() -> void:
	_cone_material.emission_energy_multiplier = 3.0
	var flash := create_tween()
	flash.tween_property(_cone_material, "emission_energy_multiplier", 0.0, 0.35)
	if _tween:
		_tween.kill()
	_tween = create_tween()
	_tween.tween_property(_visual, "rotation:x", -0.22, 0.05)
	_tween.tween_property(_visual, "rotation:x", 0.0, 0.5).set_trans(Tween.TRANS_ELASTIC).set_ease(Tween.EASE_OUT)


func _topple(down: bool) -> void:
	if _tween:
		_tween.kill()
	_tween = create_tween()
	if down:
		_tween.tween_property(_visual, "rotation:x", -1.45, 0.45).set_trans(Tween.TRANS_BOUNCE).set_ease(Tween.EASE_OUT)
	else:
		_tween.tween_property(_visual, "rotation:x", 0.0, 0.5).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)


func _update_label(health: float, max_health: float) -> void:
	var filled := int(round(10.0 * health / maxf(max_health, 1.0)))
	_label.text = "%s\n%d / %d" % ["|".repeat(filled) + ".".repeat(10 - filled), int(health), int(max_health)]
	_label.modulate = Color(1.0, 0.9, 0.75) if health > 0.0 else Color(0.6, 0.6, 0.65)


func _material(color: Color, roughness: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	m.roughness = roughness
	return m


func _box(size: Vector3, at: Vector3, material: Material) -> void:
	var mesh := BoxMesh.new()
	mesh.size = size
	var part := MeshInstance3D.new()
	part.mesh = mesh
	part.material_override = material
	part.position = at
	_visual.add_child(part)


## Haut-parleur sur la face avant (+Z).
func _cone(radius: float, at: Vector3) -> void:
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius * 0.8
	mesh.height = 0.06
	var part := MeshInstance3D.new()
	part.mesh = mesh
	part.material_override = _cone_material
	part.position = at
	part.rotation.x = PI * 0.5
	_visual.add_child(part)
