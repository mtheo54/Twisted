## Interface à l'écran : vie du joueur et aide des commandes (F1).
extends CanvasLayer

const HELP_TEXT := """COMMANDES
Z Q S D  se déplacer
Souris  caméra
Maj  courir
Espace  sauter
Ctrl  esquive
Clic gauche  frapper
F1  afficher / cacher cette aide
F2  bonhomme pixélisé / lisse
Échap  libérer la souris (clic pour reprendre)"""

var player_entity_id := 0

var _health_bar: ProgressBar
var _health_label: Label
var _help: PanelContainer


func _ready() -> void:
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)

	var title := Label.new()
	title.text = "TWISTED · rue test"
	title.position = Vector2(24, 18)
	title.add_theme_font_size_override("font_size", 22)
	title.add_theme_color_override("font_color", Color(0.95, 0.92, 0.88))
	title.add_theme_color_override("font_outline_color", Color(0.1, 0.08, 0.14))
	title.add_theme_constant_override("outline_size", 6)
	root.add_child(title)

	var bottom := VBoxContainer.new()
	bottom.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	bottom.offset_left = 24
	bottom.offset_right = 344
	bottom.offset_top = -84
	bottom.offset_bottom = -24
	bottom.grow_vertical = Control.GROW_DIRECTION_BEGIN
	root.add_child(bottom)
	_health_label = Label.new()
	_health_label.add_theme_color_override("font_color", Color(0.95, 0.92, 0.88))
	_health_label.add_theme_color_override("font_outline_color", Color(0.1, 0.08, 0.14))
	_health_label.add_theme_constant_override("outline_size", 6)
	bottom.add_child(_health_label)
	_health_bar = ProgressBar.new()
	_health_bar.custom_minimum_size = Vector2(320, 14)
	_health_bar.show_percentage = false
	_health_bar.add_theme_stylebox_override("background", _style(Color(0.08, 0.07, 0.11, 0.75)))
	_health_bar.add_theme_stylebox_override("fill", _style(Color(0.45, 0.85, 0.55)))
	bottom.add_child(_health_bar)
	_set_health(100.0, 100.0)

	_help = PanelContainer.new()
	_help.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	_help.offset_left = -360
	_help.offset_right = -18
	_help.offset_top = 18
	_help.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	_help.add_theme_stylebox_override("panel", _style(Color(0.08, 0.07, 0.11, 0.7), 12))
	var help_label := Label.new()
	help_label.text = HELP_TEXT
	help_label.add_theme_color_override("font_color", Color(0.92, 0.9, 0.86))
	_help.add_child(help_label)
	root.add_child(_help)


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("toggle_help"):
		_help.visible = not _help.visible


func on_sim_event(event: Dictionary) -> void:
	match str(event["type"]):
		"damage":
			if int(event["target"]) == player_entity_id:
				_set_health(float(event["health"]), float(event["max_health"]))
		"revived", "healed":
			if int(event["id"]) == player_entity_id:
				_set_health(float(event["health"]), float(event["max_health"]))


func _set_health(health: float, max_health: float) -> void:
	_health_bar.max_value = max_health
	_health_bar.value = health
	_health_label.text = "Vie  %d / %d" % [int(health), int(max_health)]


func _style(color: Color, margin: int = 0) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = color
	box.set_corner_radius_all(6)
	box.set_content_margin_all(margin)
	return box
