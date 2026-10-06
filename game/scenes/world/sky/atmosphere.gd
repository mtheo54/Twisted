## Ciel, soleil, brume et post-traitement (bloom, occlusion ambiante).
## Étape actuelle : le jour seulement (ciel clair et brumeux, tons pêche et
## gris-bleu). La nuit et le passage jour/nuit viendront au jalon 9.
extends Node3D

var environment: Environment
var sun: DirectionalLight3D


func _init() -> void:
	var sky_material := ProceduralSkyMaterial.new()
	sky_material.sky_top_color = Color(0.50, 0.58, 0.70)
	sky_material.sky_horizon_color = Color(0.95, 0.82, 0.72)
	sky_material.sky_curve = 0.12
	sky_material.ground_bottom_color = Color(0.36, 0.37, 0.43)
	sky_material.ground_horizon_color = Color(0.88, 0.78, 0.72)
	sky_material.sun_angle_max = 25.0
	var sky := Sky.new()
	sky.sky_material = sky_material

	environment = Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	environment.ambient_light_energy = 1.0
	environment.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	environment.tonemap_exposure = 1.0
	environment.ssao_enabled = true
	environment.ssao_intensity = 1.5
	environment.glow_enabled = true
	environment.glow_intensity = 0.6
	environment.glow_bloom = 0.04
	environment.glow_hdr_threshold = 1.0
	environment.fog_enabled = true
	environment.fog_light_color = Color(0.87, 0.81, 0.79)
	environment.fog_density = 0.0012
	environment.fog_aerial_perspective = 0.35
	environment.fog_sky_affect = 0.3
	environment.adjustment_enabled = true
	environment.adjustment_saturation = 1.05

	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	add_child(world_environment)

	sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-34.0, -140.0, 0.0)
	sun.light_color = Color(1.0, 0.88, 0.76)
	sun.light_energy = 1.4
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 90.0
	add_child(sun)
