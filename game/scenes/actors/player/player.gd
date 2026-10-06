## Le bonhomme d'abrasion contrôlé par le joueur.
## Ce script gère les déplacements (physique Godot) et envoie les ordres de
## combat à la Sim. La vie, les dégâts et les recharges sont dans la Sim :
## ce script ne fait que réagir à ses événements.
extends CharacterBody3D

const Sim = preload("res://core/sim.gd")
const CameraRig = preload("res://scenes/game/third_person_camera.gd")
const BonhommeModel = preload("res://scenes/actors/player/bonhomme_model.gd")
const PixelImpostor = preload("res://scenes/actors/player/pixel_impostor.gd")

const MODEL_LAYER := 2

var sim: Sim
var entity_id := 0
var camera_rig: CameraRig
var environment: Environment
var stats: Dictionary = {}

var model: BonhommeModel
var impostor: PixelImpostor
var pixel_mode := true

var _facing_yaw := 0.0
var _wish := Vector3.ZERO
var _dodge_left := 0.0
var _dodge_velocity := Vector3.ZERO
var _was_on_floor := true
var _spawn_point := Vector3.ZERO


## À appeler avant d'ajouter le joueur à la scène.
func setup(p_sim: Sim, p_entity_id: int, p_camera_rig: CameraRig, p_environment: Environment) -> void:
	sim = p_sim
	entity_id = p_entity_id
	camera_rig = p_camera_rig
	environment = p_environment
	stats = sim.data.actor("player")


func _ready() -> void:
	collision_layer = 0
	set_collision_layer_value(2, true)
	collision_mask = 0
	set_collision_mask_value(1, true)
	set_collision_mask_value(3, true)
	floor_snap_length = 0.3

	var shape := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.38
	capsule.height = 1.6
	shape.shape = capsule
	shape.position = Vector3(0.0, 0.8, 0.0)
	add_child(shape)

	model = BonhommeModel.new()
	add_child(model)
	impostor = PixelImpostor.new()
	impostor.base_environment = environment
	add_child(impostor)
	_add_blob_shadow()

	_spawn_point = global_position
	set_pixel_mode(true)


## F2 : bascule entre le rendu pixélisé et le modèle 3D lisse (pour comparer
## ou en cas de souci d'affichage).
func set_pixel_mode(on: bool) -> void:
	pixel_mode = on
	impostor.enabled = on
	model.set_render_layers(1 << (MODEL_LAYER - 1) if on else 1)


func facing() -> Vector3:
	return Vector3(-sin(_facing_yaw), 0.0, -cos(_facing_yaw))


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("toggle_pixel"):
		set_pixel_mode(not pixel_mode)


func _physics_process(delta: float) -> void:
	var walk_speed := float(stats.get("walk_speed", 5.0))
	var sprint_speed := float(stats.get("sprint_speed", 8.0))
	var gravity := float(stats.get("gravity", 22.0))

	var input := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	_wish = Vector3(input.x, 0.0, input.y).rotated(Vector3.UP, camera_rig.yaw)

	var on_floor := is_on_floor()
	if not on_floor:
		velocity.y -= gravity * delta

	if _dodge_left > 0.0:
		_dodge_left -= delta
		velocity.x = _dodge_velocity.x
		velocity.z = _dodge_velocity.z
	else:
		var speed := sprint_speed if Input.is_action_pressed("sprint") else walk_speed
		var accel := float(stats.get("accel", 40.0)) if on_floor else float(stats.get("air_accel", 14.0))
		var horizontal := Vector3(velocity.x, 0.0, velocity.z).move_toward(_wish * speed, accel * delta)
		velocity.x = horizontal.x
		velocity.z = horizontal.z
		if on_floor and Input.is_action_just_pressed("jump"):
			velocity.y = float(stats.get("jump_velocity", 8.0))
			model.play_jump()

	move_and_slide()

	if is_on_floor() and not _was_on_floor:
		model.play_land()
	_was_on_floor = is_on_floor()

	# Orientation : le bonhomme regarde là où il va.
	var flat_velocity := Vector3(velocity.x, 0.0, velocity.z)
	if flat_velocity.length() > 0.5 and _dodge_left <= 0.0:
		var target_yaw := atan2(-flat_velocity.x, -flat_velocity.z)
		_facing_yaw = lerp_angle(_facing_yaw, target_yaw, 1.0 - exp(-12.0 * delta))
	model.rotation.y = _facing_yaw
	model.set_motion(flat_velocity.length() / sprint_speed, is_on_floor())

	# Ordres de combat : c'est la Sim qui décide s'ils sont acceptés.
	if Input.is_action_just_pressed("dodge"):
		sim.queue_command({"type": "dodge", "source": entity_id})
	if Input.is_action_just_pressed("attack") and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		# on frappe dans la direction où regarde la caméra
		_facing_yaw = camera_rig.yaw
		model.rotation.y = _facing_yaw
		sim.queue_command({"type": "melee", "source": entity_id, "weapon": "fist"})

	sim.set_transform(entity_id, global_position, facing())

	if global_position.y < -30.0:
		global_position = _spawn_point
		velocity = Vector3.ZERO


func on_sim_event(event: Dictionary) -> void:
	match str(event["type"]):
		"dodge_started":
			if int(event["source"]) == entity_id:
				_start_dodge(float(event["distance"]), float(event["duration"]))
		"attack_started":
			if int(event["source"]) == entity_id:
				model.play_punch(float(event["windup"]))
		"damage":
			if int(event["target"]) == entity_id:
				model.flash_hit()


func _start_dodge(distance: float, duration: float) -> void:
	var direction := _wish.normalized() if _wish.length() > 0.1 else facing()
	_dodge_velocity = direction * (distance / maxf(duration, 0.01))
	_dodge_left = duration
	_facing_yaw = atan2(-direction.x, -direction.z)
	model.play_dodge(duration)


## Ombre ronde sous le bonhomme (il flotte, une ombre douce le pose au sol).
func _add_blob_shadow() -> void:
	var gradient := Gradient.new()
	gradient.set_color(0, Color(0.0, 0.0, 0.0, 0.55))
	gradient.set_color(1, Color(0.0, 0.0, 0.0, 0.0))
	var texture := GradientTexture2D.new()
	texture.gradient = gradient
	texture.fill = GradientTexture2D.FILL_RADIAL
	texture.fill_from = Vector2(0.5, 0.5)
	texture.fill_to = Vector2(1.0, 0.5)
	texture.width = 64
	texture.height = 64
	var decal := Decal.new()
	decal.texture_albedo = texture
	decal.size = Vector3(1.1, 6.0, 1.1)
	decal.position = Vector3(0.0, 0.5, 0.0)
	decal.cull_mask = 1
	add_child(decal)
