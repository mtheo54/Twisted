## Le bonhomme d'abrasion en volumes simples, d'après game/reference/ :
## tête ronde séparée du corps, corps en gélule sans jambes (il flotte),
## bras épais et arrondis, finition chrome gris foncé.
## Toutes les animations sont faites par code (pas de squelette).
extends Node3D

const HOVER := 0.16
const BODY_RADIUS := 0.34
const BODY_HEIGHT := 0.98
const HEAD_RADIUS := 0.28
const NECK_GAP := 0.07
const ARM_REST_Z := 0.18

var arm_left: Node3D
var arm_right: Node3D

var _spin: Node3D        # rotation pendant l'esquive
var _body_root: Node3D   # tout ce qui flotte et se penche
var _meshes: Array[MeshInstance3D] = []
var _chrome: StandardMaterial3D
var _render_layers := 1

var _t := 0.0
var _speed_ratio := 0.0
var _grounded := true
var _idle_time := 0.0
var _left_busy := false
var _right_busy := false
var _next_punch_right := true
var _arm_tweens := {}
var _fx_tween: Tween


func _ready() -> void:
	_chrome = StandardMaterial3D.new()
	_chrome.albedo_color = Color(0.30, 0.31, 0.34)
	_chrome.metallic = 1.0
	_chrome.metallic_specular = 0.7
	_chrome.roughness = 0.16
	_chrome.emission_enabled = true
	_chrome.emission = Color(1.0, 0.25, 0.35)
	_chrome.emission_energy_multiplier = 0.0

	_spin = Node3D.new()
	add_child(_spin)
	_body_root = Node3D.new()
	_spin.add_child(_body_root)

	var body := CapsuleMesh.new()
	body.radius = BODY_RADIUS
	body.height = BODY_HEIGHT
	_add_part(_body_root, body, Vector3(0.0, HOVER + BODY_HEIGHT * 0.5, 0.0))

	var head := SphereMesh.new()
	head.radius = HEAD_RADIUS
	head.height = HEAD_RADIUS * 2.0
	_add_part(_body_root, head, Vector3(0.0, HOVER + BODY_HEIGHT + NECK_GAP + HEAD_RADIUS, 0.0))

	arm_left = _make_arm(-1.0)
	arm_right = _make_arm(1.0)


func set_render_layers(mask: int) -> void:
	_render_layers = mask
	for mesh in _meshes:
		mesh.layers = mask


## speed_ratio : 0 = immobile, 1 = course.
func set_motion(speed_ratio: float, grounded: bool) -> void:
	_speed_ratio = clampf(speed_ratio, 0.0, 1.2)
	_grounded = grounded


func play_punch(windup: float) -> void:
	_idle_time = 0.0
	var right := _next_punch_right
	_next_punch_right = not _next_punch_right
	var arm := arm_right if right else arm_left
	var side := 1.0 if right else -1.0
	_set_busy(right, true)
	var tween := _arm_tween(arm)
	tween.tween_property(arm, "rotation", Vector3(-0.7, 0.0, 0.35 * side), maxf(windup, 0.05))
	tween.tween_property(arm, "rotation", Vector3(1.65, 0.0, 0.05 * side), 0.07)
	tween.tween_interval(0.12)
	tween.tween_property(arm, "rotation", Vector3(0.0, 0.0, ARM_REST_Z * side), 0.2)
	tween.tween_callback(_set_busy.bind(right, false))


func play_jump() -> void:
	var tween := create_tween()
	tween.tween_property(_body_root, "scale", Vector3(1.12, 0.85, 1.12), 0.06)
	tween.tween_property(_body_root, "scale", Vector3(0.92, 1.12, 0.92), 0.1)
	tween.tween_property(_body_root, "scale", Vector3.ONE, 0.2)


func play_land() -> void:
	var tween := create_tween()
	tween.tween_property(_body_root, "scale", Vector3(1.15, 0.82, 1.15), 0.05)
	tween.tween_property(_body_root, "scale", Vector3.ONE, 0.18).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)


## Esquive : le bonhomme tourne sur lui-même comme une toupie.
func play_dodge(duration: float) -> void:
	_idle_time = 0.0
	_spin.rotation.y = 0.0
	var tween := create_tween()
	tween.tween_property(_spin, "rotation:y", -TAU, duration).set_ease(Tween.EASE_OUT)
	tween.tween_callback(func() -> void: _spin.rotation.y = 0.0)


func flash_hit() -> void:
	if _fx_tween:
		_fx_tween.kill()
	_chrome.emission_energy_multiplier = 2.5
	_fx_tween = create_tween()
	_fx_tween.tween_property(_chrome, "emission_energy_multiplier", 0.0, 0.3)


func _process(delta: float) -> void:
	_t += delta
	# flottement et inclinaison vers l'avant en mouvement
	_body_root.position.y = sin(_t * 2.4) * 0.035 if _grounded else 0.0
	var lean := -0.22 * _speed_ratio
	_body_root.rotation.x = lerpf(_body_root.rotation.x, lean, 1.0 - exp(-8.0 * delta))

	# balancement des bras en marchant
	var swing := sin(_t * 9.0) * 0.55 * _speed_ratio
	if not _right_busy:
		arm_right.rotation.x = lerpf(arm_right.rotation.x, swing, 1.0 - exp(-10.0 * delta))
	if not _left_busy:
		arm_left.rotation.x = lerpf(arm_left.rotation.x, -swing, 1.0 - exp(-10.0 * delta))

	# immobile un moment : il fait coucou, comme sur l'icône d'abrasion
	if _speed_ratio < 0.05 and _grounded:
		_idle_time += delta
		if _idle_time > 5.0 and not _right_busy:
			_idle_time = -4.0
			_play_wave()
	else:
		_idle_time = 0.0


func _play_wave() -> void:
	_set_busy(true, true)
	var tween := _arm_tween(arm_right)
	tween.tween_property(arm_right, "rotation", Vector3(0.0, 0.0, 2.55), 0.3).set_trans(Tween.TRANS_BACK)
	for i in 3:
		tween.tween_property(arm_right, "rotation", Vector3(0.0, 0.0, 2.25), 0.18)
		tween.tween_property(arm_right, "rotation", Vector3(0.0, 0.0, 2.6), 0.18)
	tween.tween_property(arm_right, "rotation", Vector3(0.0, 0.0, ARM_REST_Z), 0.3)
	tween.tween_callback(_set_busy.bind(true, false))


func _arm_tween(arm: Node3D) -> Tween:
	var previous: Tween = _arm_tweens.get(arm)
	if previous:
		previous.kill()
	var tween := create_tween()
	_arm_tweens[arm] = tween
	return tween


func _set_busy(right: bool, busy: bool) -> void:
	if right:
		_right_busy = busy
	else:
		_left_busy = busy


func _make_arm(side: float) -> Node3D:
	var pivot := Node3D.new()
	pivot.position = Vector3(0.33 * side, HOVER + 0.78, 0.0)
	pivot.rotation.z = ARM_REST_Z * side
	_body_root.add_child(pivot)
	var arm := CapsuleMesh.new()
	arm.radius = 0.12
	arm.height = 0.62
	_add_part(pivot, arm, Vector3(0.06 * side, -0.24, 0.0))
	return pivot


func _add_part(parent: Node3D, mesh: Mesh, at: Vector3) -> void:
	var part := MeshInstance3D.new()
	part.mesh = mesh
	part.material_override = _chrome
	part.position = at
	part.layers = _render_layers
	part.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	parent.add_child(part)
	_meshes.append(part)
