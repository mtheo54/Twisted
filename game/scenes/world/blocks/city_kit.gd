## Boîte à outils pour construire la ville : matériaux partagés, immeubles
## à toit plat avec leurs détails (rebords, antennes, climatiseurs, citernes),
## lampadaires, collisions.
extends RefCounted

const FACADE_SHADER = preload("res://vfx/facade.gdshader")

## Tons gris, gris-bleu et pêche, comme la ville du plugin.
const WALL_COLORS := [
	Color(0.63, 0.62, 0.66),
	Color(0.55, 0.55, 0.60),
	Color(0.70, 0.65, 0.63),
	Color(0.48, 0.49, 0.54),
	Color(0.67, 0.66, 0.69),
	Color(0.60, 0.56, 0.58),
]

static var _facades: Array[ShaderMaterial] = []
static var _flat: Dictionary = {}
static var _unit_box: BoxMesh


static func facade_material(variant: int) -> ShaderMaterial:
	if _facades.is_empty():
		for color: Color in WALL_COLORS:
			var material := ShaderMaterial.new()
			material.shader = FACADE_SHADER
			material.set_shader_parameter("wall_color", color)
			material.set_shader_parameter("roof_color", color * 0.72)
			_facades.append(material)
	return _facades[absi(variant) % _facades.size()]


## Tous les matériaux de façade (pour régler la nuit d'un coup, plus tard).
static func all_facades() -> Array[ShaderMaterial]:
	facade_material(0)
	return _facades


static func flat_material(color: Color, roughness: float = 0.85, emission: Color = Color.BLACK) -> StandardMaterial3D:
	var key := "%s|%s|%s" % [color.to_html(), roughness, emission.to_html()]
	if _flat.has(key):
		return _flat[key]
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = roughness
	if emission != Color.BLACK:
		material.emission_enabled = true
		material.emission = emission
	_flat[key] = material
	return material


static func unit_box() -> BoxMesh:
	if _unit_box == null:
		_unit_box = BoxMesh.new()
	return _unit_box


## Ajoute une boîte simple (sans collision). Retourne le nœud.
static func add_box(parent: Node3D, center: Vector3, size: Vector3, material: Material) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = size
	var node := MeshInstance3D.new()
	node.mesh = mesh
	node.material_override = material
	node.position = center
	parent.add_child(node)
	return node


static func add_cylinder(parent: Node3D, base: Vector3, radius: float, height: float, material: Material) -> MeshInstance3D:
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = height
	mesh.radial_segments = 12
	mesh.rings = 1
	var node := MeshInstance3D.new()
	node.mesh = mesh
	node.material_override = material
	node.position = base + Vector3(0.0, height * 0.5, 0.0)
	parent.add_child(node)
	return node


## Collision invisible en forme de boîte (calque 1 : monde).
static func add_collider(parent: Node3D, center: Vector3, size: Vector3) -> void:
	var body := StaticBody3D.new()
	body.position = center
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = size
	shape.shape = box
	body.add_child(shape)
	parent.add_child(body)


## Immeuble à toit plat. base_center : centre de sa base au sol.
static func add_building(parent: Node3D, base_center: Vector3, size: Vector3, rng: RandomNumberGenerator, with_collision: bool = true) -> void:
	var body := MeshInstance3D.new()
	body.mesh = unit_box()
	body.material_override = facade_material(rng.randi())
	body.position = base_center + Vector3(0.0, size.y * 0.5, 0.0)
	body.scale = size
	parent.add_child(body)
	body.set_instance_shader_parameter("box_size", size)
	body.set_instance_shader_parameter("seed", rng.randf() * 100.0)
	if with_collision:
		add_collider(parent, body.position, size)
	_add_roof(parent, base_center + Vector3(0.0, size.y, 0.0), size, rng)


static func _add_roof(parent: Node3D, top: Vector3, size: Vector3, rng: RandomNumberGenerator) -> void:
	var parapet := flat_material(Color(0.40, 0.40, 0.45))
	var equipment := flat_material(Color(0.72, 0.72, 0.74), 0.7)
	var dark := flat_material(Color(0.16, 0.15, 0.18), 0.6)

	# rebord tout autour du toit
	var t := 0.25
	var h := 0.6
	add_box(parent, top + Vector3(0.0, h * 0.5, size.z * 0.5 - t * 0.5), Vector3(size.x, h, t), parapet)
	add_box(parent, top + Vector3(0.0, h * 0.5, -size.z * 0.5 + t * 0.5), Vector3(size.x, h, t), parapet)
	add_box(parent, top + Vector3(size.x * 0.5 - t * 0.5, h * 0.5, 0.0), Vector3(t, h, size.z - 2.0 * t), parapet)
	add_box(parent, top + Vector3(-size.x * 0.5 + t * 0.5, h * 0.5, 0.0), Vector3(t, h, size.z - 2.0 * t), parapet)

	var inner := Vector2(size.x * 0.5 - 1.5, size.z * 0.5 - 1.5)
	if inner.x <= 0.5 or inner.y <= 0.5:
		return

	# cage d'escalier
	if rng.randf() < 0.55:
		var cage := Vector3(rng.randf_range(2.5, 4.5), rng.randf_range(2.2, 3.2), rng.randf_range(2.5, 4.0))
		var at := top + Vector3(rng.randf_range(-inner.x, inner.x) * 0.5, cage.y * 0.5, rng.randf_range(-inner.y, inner.y) * 0.5)
		add_box(parent, at, cage, parapet)

	# climatiseurs
	for i in rng.randi_range(1, 4):
		var at := top + Vector3(rng.randf_range(-inner.x, inner.x), 0.35, rng.randf_range(-inner.y, inner.y))
		add_box(parent, at, Vector3(1.0, 0.7, 0.8), equipment)

	# citerne d'eau
	if rng.randf() < 0.3:
		var at := top + Vector3(rng.randf_range(-inner.x, inner.x), 0.0, rng.randf_range(-inner.y, inner.y))
		add_cylinder(parent, at + Vector3(0.0, 0.6, 0.0), 1.0, 1.7, equipment)
		add_box(parent, at + Vector3(0.0, 0.3, 0.0), Vector3(1.6, 0.6, 1.6), dark)

	# antenne avec balise rouge au sommet
	if rng.randf() < 0.65:
		var at := top + Vector3(rng.randf_range(-inner.x, inner.x), 0.0, rng.randf_range(-inner.y, inner.y))
		var height := rng.randf_range(3.0, 8.0)
		add_cylinder(parent, at, 0.06, height, dark)
		var tip := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radius = 0.16
		sphere.height = 0.32
		sphere.radial_segments = 8
		sphere.rings = 4
		tip.mesh = sphere
		tip.material_override = flat_material(Color(0.9, 0.3, 0.25), 0.5, Color(1.0, 0.35, 0.25))
		tip.position = at + Vector3(0.0, height, 0.0)
		parent.add_child(tip)


## Lampadaire : poteau courbé et tête de lampe (la lumière viendra avec la nuit).
static func add_street_lamp(parent: Node3D, base: Vector3, toward_road: float) -> void:
	var pole := flat_material(Color(0.22, 0.22, 0.26), 0.5)
	add_cylinder(parent, base, 0.07, 5.2, pole)
	add_box(parent, base + Vector3(toward_road * 0.55, 5.15, 0.0), Vector3(1.2, 0.1, 0.12), pole)
	var head := add_box(parent, base + Vector3(toward_road * 1.1, 5.05, 0.0), Vector3(0.5, 0.14, 0.26), flat_material(Color(0.95, 0.9, 0.8), 0.4, Color(0.25, 0.22, 0.18)))
	head.add_to_group("street_lamp_heads")
