## Rendu « pixélisé » du bonhomme, fidèle à l'icône d'abrasion.
##
## Principe : une petite caméra, placée exactement comme la caméra principale,
## ne voit que le bonhomme (calque de rendu 2) et le dessine dans une image
## minuscule (pixel_height pixels de haut). Cette image est ensuite posée dans
## la scène, pile à l'emplacement du bonhomme, sur un panneau tourné vers la
## caméra, avec de gros pixels nets. Les murs qui passent devant le cachent
## normalement.
##
## La résolution est réglable à tout moment : c'est ce que le sort Bit crush
## fera varier.
extends Node3D

const SHADER = preload("res://vfx/pixel_sprite.gdshader")
const MODEL_LAYER := 2          # calque où vit le vrai modèle
const SPRITE_LAYER := 3         # calque du panneau affiché

## Nombre de pixels sur la hauteur de la fenêtre (window_size mètres).
@export var pixel_height := 88
## Taille en mètres de la zone capturée autour du bonhomme.
@export var window_size := 2.4
@export var center_offset := Vector3(0.0, 0.9, 0.0)
## Le panneau est avancé vers la caméra pour ne pas s'enfoncer dans le sol.
@export var front_offset := 0.9

var base_environment: Environment
var enabled := true

var _viewport: SubViewport
var _camera: Camera3D
var _quad: MeshInstance3D
var _material: ShaderMaterial


func _ready() -> void:
	process_priority = 100   # après la caméra principale

	_viewport = SubViewport.new()
	_viewport.size = Vector2i(pixel_height, pixel_height)
	_viewport.transparent_bg = true
	_viewport.use_hdr_2d = true
	_viewport.msaa_3d = Viewport.MSAA_DISABLED
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(_viewport)

	_camera = Camera3D.new()
	_camera.cull_mask = 0
	_camera.set_cull_mask_value(MODEL_LAYER, true)
	_camera.projection = Camera3D.PROJECTION_FRUSTUM
	_camera.environment = _make_environment()
	_viewport.add_child(_camera)
	_camera.current = true

	_material = ShaderMaterial.new()
	_material.shader = SHADER
	_material.set_shader_parameter("pixels", _viewport.get_texture())

	_quad = MeshInstance3D.new()
	var quad_mesh := QuadMesh.new()
	quad_mesh.size = Vector2.ONE
	_quad.mesh = quad_mesh
	_quad.material_override = _material
	_quad.top_level = true
	_quad.layers = 0
	_quad.set_layer_mask_value(SPRITE_LAYER, true)
	_quad.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_quad)


func set_pixel_height(value: int) -> void:
	pixel_height = maxi(4, value)
	if _viewport:
		_viewport.size = Vector2i(pixel_height, pixel_height)


func _process(_delta: float) -> void:
	var main_camera := get_viewport().get_camera_3d()
	if not enabled or main_camera == null:
		_quad.visible = false
		return
	var cam_xform := main_camera.global_transform
	var center := cam_xform.affine_inverse() * (global_position + center_offset)
	var depth := -center.z
	if depth < 0.3:
		_quad.visible = false
		return
	_quad.visible = true

	# La petite caméra regarde depuis le même point que la principale, mais
	# son cadre (frustum décalé) ne couvre que la zone du bonhomme : la
	# perspective est identique, l'image se superpose au pixel près.
	var near := 0.05
	var k := near / depth
	_camera.global_transform = cam_xform
	# Vue limitée à quelques mètres derrière le bonhomme : ses ombres du
	# soleil ne sont calculées que sur cette petite zone (bien plus léger).
	_camera.set_frustum(window_size * k, Vector2(center.x, center.y) * k, near, depth + window_size * 2.0)

	# Panneau sur le même cadre, juste devant le bonhomme.
	var quad_depth := maxf(depth - front_offset, depth * 0.5)
	var s := quad_depth / depth
	var quad_basis := cam_xform.basis * Basis.from_scale(Vector3(window_size * s, window_size * s, 1.0))
	_quad.global_transform = Transform3D(quad_basis, cam_xform * Vector3(center.x * s, center.y * s, -quad_depth))


## Même lumière que la scène principale (reflets du ciel sur le chrome), mais
## fond transparent, sans brume ni bloom : la scène principale les ajoute
## déjà au panneau.
func _make_environment() -> Environment:
	var env: Environment
	if base_environment:
		env = base_environment.duplicate() as Environment
	else:
		env = Environment.new()
	env.background_mode = Environment.BG_CLEAR_COLOR
	if env.sky:
		env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
		env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.glow_enabled = false
	env.fog_enabled = false
	env.volumetric_fog_enabled = false
	env.ssao_enabled = false
	env.ssr_enabled = false
	env.sdfgi_enabled = false
	env.adjustment_enabled = false
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.tonemap_exposure = 1.0
	return env
