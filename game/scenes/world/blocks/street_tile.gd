## Tuile de ville de 32 m × 32 m : une portion de rue droite (le long de Z)
## avec trottoirs, lampadaires et immeubles des deux côtés.
## Les tuiles s'assemblent bout à bout ; plus tard viendront carrefour,
## virage, place, quartier résidentiel… sur la même grille.
extends Node3D

const CityKit = preload("res://scenes/world/blocks/city_kit.gd")

const SIZE := 32.0
const ROAD_HALF := 5.0
const SIDEWALK := 3.0
## Distance du centre de la rue à la façade des immeubles.
const FRONT := ROAD_HALF + SIDEWALK + 0.3

@export var tile_seed := 0
@export var min_height := 9.0
@export var max_height := 30.0


func _ready() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = tile_seed
	_build_ground()
	for side: float in [-1.0, 1.0]:
		_build_side(side, rng)
		for i in 3:
			CityKit.add_street_lamp(self, Vector3(side * (ROAD_HALF + 0.6), 0.0, -SIZE * 0.5 + 5.0 + i * 11.0 + (5.5 if side > 0.0 else 0.0)), -side)


func _build_ground() -> void:
	var asphalt := CityKit.flat_material(Color(0.24, 0.24, 0.29), 0.95)
	var sidewalk := CityKit.flat_material(Color(0.58, 0.56, 0.58), 0.9)
	var curb := CityKit.flat_material(Color(0.72, 0.71, 0.72), 0.8)
	var paint := CityKit.flat_material(Color(0.9, 0.88, 0.82), 0.7)

	CityKit.add_box(self, Vector3(0.0, -0.05, 0.0), Vector3(ROAD_HALF * 2.0, 0.1, SIZE), asphalt)
	for side: float in [-1.0, 1.0]:
		# Trottoir surélevé visuellement de 12 cm. Pas de collision propre :
		# le bonhomme flotte au-dessus, il ne bute pas sur la bordure.
		CityKit.add_box(self, Vector3(side * (ROAD_HALF + SIDEWALK * 0.5 + 0.15), 0.06, 0.0), Vector3(SIDEWALK + 0.3, 0.12, SIZE), sidewalk)
		CityKit.add_box(self, Vector3(side * (ROAD_HALF + 0.08), 0.065, 0.0), Vector3(0.16, 0.13, SIZE), curb)
	# ligne centrale en pointillés
	var z := -SIZE * 0.5 + 1.0
	while z < SIZE * 0.5:
		CityKit.add_box(self, Vector3(0.0, 0.003, z + 1.0), Vector3(0.15, 0.01, 2.0), paint)
		z += 4.0
	# sol de collision qui couvre toute la tuile
	CityKit.add_collider(self, Vector3(0.0, -0.5, 0.0), Vector3(80.0, 1.0, SIZE))


## Rangée continue d'immeubles le long d'un côté de la rue.
func _build_side(side: float, rng: RandomNumberGenerator) -> void:
	var z := -SIZE * 0.5
	var end := SIZE * 0.5
	while z < end - 0.01:
		var width := rng.randf_range(7.0, 13.0)
		if end - (z + width) < 5.0:
			width = end - z
		var depth := rng.randf_range(10.0, 18.0)
		var height := rng.randf_range(min_height, max_height)
		var center := Vector3(side * (FRONT + depth * 0.5), 0.0, z + width * 0.5)
		CityKit.add_building(self, center, Vector3(depth, height, width), rng)
		z += width
