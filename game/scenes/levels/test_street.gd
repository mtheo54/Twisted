## La rue test (étape 1) : une rue de 4 tuiles (128 m), le bonhomme,
## une enceinte d'entraînement, le décor lointain et l'interface.
## Tout est construit par code à partir des blocs de scenes/.
extends Node3D

const DataDB = preload("res://core/data_db.gd")
const Sim = preload("res://core/sim.gd")
const GameWorld = preload("res://scenes/game/game_world.gd")
const CameraRig = preload("res://scenes/game/third_person_camera.gd")
const Atmosphere = preload("res://scenes/world/sky/atmosphere.gd")
const StreetTile = preload("res://scenes/world/blocks/street_tile.gd")
const CityKit = preload("res://scenes/world/blocks/city_kit.gd")
const Backdrop = preload("res://scenes/world/backdrop.gd")
const Player = preload("res://scenes/actors/player/player.gd")
const TrainingDummy = preload("res://scenes/actors/dummy/training_dummy.gd")
const Hud = preload("res://ui/hud.gd")

const TILE_COUNT := 4
const PLAYER_SPAWN := Vector3(0.0, 0.2, -8.0)
const DUMMY_SPAWN := Vector3(-2.5, 0.0, -18.0)

var sim: Sim
var world: GameWorld


func _ready() -> void:
	sim = Sim.new(DataDB.new())
	world = GameWorld.new()
	world.sim = sim
	add_child(world)

	var atmosphere := Atmosphere.new()
	add_child(atmosphere)
	_build_street()
	add_child(Backdrop.new())

	var camera_rig := CameraRig.new()
	add_child(camera_rig)

	var player := Player.new()
	player.setup(sim, sim.spawn("player", PLAYER_SPAWN), camera_rig, atmosphere.environment)
	player.position = PLAYER_SPAWN
	add_child(player)
	camera_rig.target = player
	camera_rig.snap_to_target()
	world.sim_event.connect(player.on_sim_event)

	var dummy := TrainingDummy.new()
	dummy.setup(sim, sim.spawn("training_dummy", DUMMY_SPAWN))
	dummy.position = DUMMY_SPAWN
	add_child(dummy)
	world.sim_event.connect(dummy.on_sim_event)

	var hud := Hud.new()
	hud.player_entity_id = player.entity_id
	add_child(hud)
	world.sim_event.connect(hud.on_sim_event)


func _build_street() -> void:
	var length := StreetTile.SIZE * TILE_COUNT
	for i in TILE_COUNT:
		var tile := StreetTile.new()
		tile.tile_seed = 1000 + i
		tile.position = Vector3(0.0, 0.0, -StreetTile.SIZE * (i + 0.5))
		add_child(tile)
	# immeubles qui ferment la rue aux deux bouts
	var rng := RandomNumberGenerator.new()
	rng.seed = 42
	var width := 2.0 * StreetTile.FRONT + 24.0
	CityKit.add_building(self, Vector3(0.0, 0.0, 7.0), Vector3(width, 24.0, 14.0), rng)
	CityKit.add_building(self, Vector3(0.0, 0.0, -length - 7.0), Vector3(width, 18.0, 14.0), rng)
