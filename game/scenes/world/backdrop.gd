## Décor lointain, sans collision : sol, ville en arrière-plan (un MultiMesh
## de centaines d'immeubles, léger à afficher), montagnes en triangles à
## l'horizon et la tour.
extends Node3D

const CityKit = preload("res://scenes/world/blocks/city_kit.gd")
const FACADE_SHADER = preload("res://vfx/facade.gdshader")
const Tower = preload("res://scenes/world/landmarks/tower.gd")

## Zone jouable à laisser libre (la rue test) : x min/max, z min/max.
@export var keep_clear := Rect2(-32.0, -150.0, 64.0, 170.0)
@export var city_center := Vector3(0.0, 0.0, -64.0)
@export var tower_position := Vector3(70.0, 0.0, -720.0)


func _ready() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	_build_ground()
	_build_distant_city(rng)
	_build_mountains(rng)
	var tower := Tower.new()
	tower.position = tower_position
	add_child(tower)


func _build_ground() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(4000.0, 4000.0)
	var ground := MeshInstance3D.new()
	ground.mesh = plane
	ground.material_override = CityKit.flat_material(Color(0.42, 0.42, 0.47), 0.95)
	ground.position = Vector3(0.0, -0.03, 0.0)
	add_child(ground)


func _build_distant_city(rng: RandomNumberGenerator) -> void:
	var transforms: Array[Transform3D] = []
	var customs: Array[Color] = []
	var tries := 0
	while transforms.size() < 450 and tries < 5000:
		tries += 1
		var angle := rng.randf() * TAU
		var radius := sqrt(rng.randf_range(60.0 * 60.0, 600.0 * 600.0))
		var pos := city_center + Vector3(cos(angle), 0.0, sin(angle)) * radius
		var footprint := Vector2(rng.randf_range(8.0, 24.0), rng.randf_range(8.0, 24.0))
		if keep_clear.grow(maxf(footprint.x, footprint.y)).has_point(Vector2(pos.x, pos.z)):
			continue
		if Vector2(pos.x - tower_position.x, pos.z - tower_position.z).length() < 70.0:
			continue
		# plus haut vers le centre-ville (la tour), plus bas en périphérie
		var to_tower := Vector2(pos.x - tower_position.x, pos.z - tower_position.z).length()
		var tall := clampf(1.0 - to_tower / 900.0, 0.2, 1.0)
		var size := Vector3(footprint.x, rng.randf_range(8.0, 20.0 + 40.0 * tall), footprint.y)
		var turn := Basis(Vector3.UP, (PI * 0.5) * rng.randi_range(0, 1) + rng.randf_range(-0.05, 0.05))
		transforms.append(Transform3D(turn * Basis.from_scale(size), pos + Vector3(0.0, size.y * 0.5, 0.0)))
		customs.append(Color(size.x, size.y, size.z, rng.randf() * 100.0))

	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_custom_data = true
	multimesh.mesh = CityKit.unit_box()
	multimesh.instance_count = transforms.size()
	for i in transforms.size():
		multimesh.set_instance_transform(i, transforms[i])
		multimesh.set_instance_custom_data(i, customs[i])

	var material := ShaderMaterial.new()
	material.shader = FACADE_SHADER
	material.set_shader_parameter("from_multimesh", true)
	material.set_shader_parameter("wall_color", Color(0.58, 0.57, 0.62))
	material.set_shader_parameter("roof_color", Color(0.42, 0.42, 0.47))
	var city := MultiMeshInstance3D.new()
	city.multimesh = multimesh
	city.material_override = material
	city.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	city.add_to_group("night_facades")
	add_child(city)


## Montagnes : pyramides gris-bleu tout autour, noyées dans la brume.
func _build_mountains(rng: RandomNumberGenerator) -> void:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var count := 26
	for i in count:
		var angle := TAU * float(i) / count + rng.randf_range(-0.08, 0.08)
		var radius := rng.randf_range(1150.0, 1500.0)
		var center := Vector3(cos(angle), 0.0, sin(angle)) * radius
		var height := rng.randf_range(220.0, 480.0)
		_add_pyramid(st, center, height * rng.randf_range(1.2, 1.7), height, rng.randf() * TAU)
	var mesh := st.commit()
	var mountains := MeshInstance3D.new()
	mountains.mesh = mesh
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.55, 0.58, 0.67)
	material.roughness = 1.0
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	mountains.material_override = material
	mountains.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mountains)


static func _add_pyramid(st: SurfaceTool, base_center: Vector3, half: float, height: float, rotation_offset: float) -> void:
	var apex := base_center + Vector3(0.0, height, 0.0)
	var corners: Array[Vector3] = []
	for k in 4:
		var a := rotation_offset + k * TAU / 4.0
		corners.append(base_center + Vector3(cos(a), 0.0, sin(a)) * half)
	var inside := base_center + Vector3(0.0, height * 0.25, 0.0)
	for k in 4:
		var c0 := corners[k]
		var c1 := corners[(k + 1) % 4]
		var normal := (c1 - apex).cross(c0 - apex).normalized()
		if normal.dot((apex + c0 + c1) / 3.0 - inside) < 0.0:
			normal = -normal
		for v in [apex, c1, c0]:
			st.set_normal(normal)
			st.add_vertex(v)
