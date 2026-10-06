## La tour rouge et blanche en lattis, repère visible de toute la ville.
## Treillis de poutres (un seul MultiMesh, très léger à afficher), deux
## plateformes, antenne bicolore et anneaux d'énergie lumineux au sommet.
## La sphère de lignes fines viendra avec la passe de finition (jalon 10).
extends Node3D

const LATTICE_TOP := 250.0
const ANTENNA_TOP := 333.0
const BASE_HALF := 34.0
const LEVELS := 16
const RED := Color(0.80, 0.22, 0.17)
const WHITE := Color(0.93, 0.91, 0.89)

var _rings: Array[Node3D] = []
var _t := 0.0


func _ready() -> void:
	var beams: Array[Transform3D] = []
	var colors: Array[Color] = []

	for i in LEVELS:
		var h0 := LATTICE_TOP * float(i) / LEVELS
		var h1 := LATTICE_TOP * float(i + 1) / LEVELS
		var band := RED if (i / 2) % 2 == 0 else WHITE
		var c0 := _corners(h0)
		var c1 := _corners(h1)
		var leg := lerpf(1.8, 0.6, float(i) / LEVELS)
		for k in 4:
			var n := (k + 1) % 4
			beams.append(_beam(c0[k], c1[k], leg))
			beams.append(_beam(c1[k], c1[n], 0.6))
			beams.append(_beam(c0[k], c1[n], 0.4))
			beams.append(_beam(c0[n], c1[k], 0.4))
			for j in 4:
				colors.append(band)

	# antenne en segments rouges et blancs
	var segments := 8
	for i in segments:
		var a := Vector3(0.0, lerpf(LATTICE_TOP, ANTENNA_TOP, float(i) / segments), 0.0)
		var b := Vector3(0.0, lerpf(LATTICE_TOP, ANTENNA_TOP, float(i + 1) / segments), 0.0)
		beams.append(_beam(a, b, lerpf(2.2, 0.5, float(i) / segments)))
		colors.append(RED if i % 2 == 0 else WHITE)

	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.mesh = BoxMesh.new()
	multimesh.instance_count = beams.size()
	for i in beams.size():
		multimesh.set_instance_transform(i, beams[i])
		multimesh.set_instance_color(i, colors[i])
	var lattice := MultiMeshInstance3D.new()
	lattice.multimesh = multimesh
	var steel := StandardMaterial3D.new()
	steel.vertex_color_use_as_albedo = true
	steel.roughness = 0.6
	lattice.material_override = steel
	lattice.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(lattice)

	# plateformes d'observation
	_deck(125.0, 11.0, 9.0)
	_deck(223.0, 5.0, 4.0)

	# balise au sommet
	var beacon := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 1.6
	sphere.height = 3.2
	beacon.mesh = sphere
	beacon.material_override = _glow_material(Color(1.0, 0.9, 0.85), 4.0)
	beacon.position = Vector3(0.0, ANTENNA_TOP, 0.0)
	add_child(beacon)

	# anneaux d'énergie
	for ring in [[22.0, 318.0], [36.0, 305.0], [52.0, 292.0]]:
		_add_ring(float(ring[0]), float(ring[1]))


func _process(delta: float) -> void:
	_t += delta
	for i in _rings.size():
		var ring := _rings[i]
		ring.rotation.y += delta * (0.15 + 0.07 * i) * (1.0 if i % 2 == 0 else -1.0)
		ring.rotation.x = sin(_t * 0.3 + i) * 0.04


## Les 4 coins du treillis à une hauteur donnée (profil qui s'affine).
func _corners(h: float) -> Array[Vector3]:
	var w := 2.2 + (BASE_HALF - 2.2) * pow(1.0 - h / LATTICE_TOP, 2.2)
	return [Vector3(w, h, w), Vector3(-w, h, w), Vector3(-w, h, -w), Vector3(w, h, -w)]


## Poutre (cube unité étiré) de a à b.
static func _beam(a: Vector3, b: Vector3, thickness: float) -> Transform3D:
	var d := b - a
	var length := d.length()
	var y := d / length
	var x := y.cross(Vector3.FORWARD)
	if x.length() < 0.01:
		x = y.cross(Vector3.RIGHT)
	x = x.normalized()
	var z := x.cross(y).normalized()
	return Transform3D(Basis(x * thickness, y * length, z * thickness), (a + b) * 0.5)


func _deck(height: float, half: float, thickness: float) -> void:
	var mesh := BoxMesh.new()
	mesh.size = Vector3(half * 2.0, thickness, half * 2.0)
	var deck := MeshInstance3D.new()
	deck.mesh = mesh
	var material := StandardMaterial3D.new()
	material.albedo_color = WHITE
	material.roughness = 0.5
	deck.material_override = material
	deck.position = Vector3(0.0, height, 0.0)
	deck.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(deck)
	# bande vitrée sombre
	var glass := MeshInstance3D.new()
	var glass_mesh := BoxMesh.new()
	glass_mesh.size = Vector3(half * 2.0 + 0.4, thickness * 0.35, half * 2.0 + 0.4)
	glass.mesh = glass_mesh
	var glass_material := StandardMaterial3D.new()
	glass_material.albedo_color = Color(0.25, 0.28, 0.36)
	glass_material.metallic = 0.5
	glass_material.roughness = 0.15
	glass.material_override = glass_material
	glass.position = Vector3(0.0, height + thickness * 0.1, 0.0)
	add_child(glass)


func _add_ring(radius: float, height: float) -> void:
	var torus := TorusMesh.new()
	torus.inner_radius = radius - 0.5
	torus.outer_radius = radius + 0.5
	torus.rings = 96
	torus.ring_segments = 6
	var ring := MeshInstance3D.new()
	ring.mesh = torus
	ring.material_override = _glow_material(Color(0.9, 0.95, 1.0), 2.5)
	ring.position = Vector3(0.0, height, 0.0)
	ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(ring)
	_rings.append(ring)


func _glow_material(color: Color, energy: float) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	material.emission_enabled = true
	material.emission = color
	material.emission_energy_multiplier = energy
	return material
