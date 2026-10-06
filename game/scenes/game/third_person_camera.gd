## Caméra à la 3e personne : orbite à la souris autour du personnage, avec un
## bras à ressort qui se raccourcit contre les murs.
## Échap libère la souris, un clic la reprend.
extends Node3D

@export var distance := 4.6
@export var pivot_height := 1.35
@export var shoulder_offset := 0.45
@export var mouse_sensitivity := 0.0025
@export var follow_sharpness := 14.0

var target: Node3D
var yaw := 0.0
var pitch := -0.22
var camera: Camera3D

var _arm: SpringArm3D


func _ready() -> void:
	_arm = SpringArm3D.new()
	_arm.spring_length = distance
	_arm.margin = 0.25
	_arm.collision_mask = 1
	var probe := SphereShape3D.new()
	probe.radius = 0.25
	_arm.shape = probe
	_arm.position = Vector3(shoulder_offset, 0.0, 0.0)
	add_child(_arm)

	camera = Camera3D.new()
	camera.fov = 70.0
	camera.near = 0.08
	camera.far = 3000.0
	# Le calque 2 contient le vrai modèle du bonhomme : seule sa caméra
	# « pixel » le voit (voir pixel_impostor.gd).
	camera.set_cull_mask_value(2, false)
	_arm.add_child(camera)
	camera.make_current()
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func snap_to_target() -> void:
	if target:
		global_position = target.global_position + Vector3.UP * pivot_height
		yaw = target.global_rotation.y
		rotation = Vector3(pitch, yaw, 0.0)


## Direction « devant la caméra », à plat.
func flat_forward() -> Vector3:
	return Vector3(-sin(yaw), 0.0, -cos(yaw))


func _unhandled_input(event: InputEvent) -> void:
	var captured := Input.mouse_mode == Input.MOUSE_MODE_CAPTURED
	if event is InputEventMouseMotion and captured:
		var motion := event as InputEventMouseMotion
		yaw -= motion.relative.x * mouse_sensitivity
		pitch = clampf(pitch - motion.relative.y * mouse_sensitivity, -1.25, 0.6)
	elif event.is_action_pressed("ui_cancel"):
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	elif event is InputEventMouseButton and (event as InputEventMouseButton).pressed and not captured:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
		get_viewport().set_input_as_handled()


func _process(delta: float) -> void:
	if target == null:
		return
	var goal := target.global_position + Vector3.UP * pivot_height
	global_position = global_position.lerp(goal, 1.0 - exp(-follow_sharpness * delta))
	rotation = Vector3(pitch, yaw, 0.0)
