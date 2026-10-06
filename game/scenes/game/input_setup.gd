## Crée les actions de jeu au démarrage (chargé automatiquement, voir
## project.godot). Les touches sont lues par POSITION PHYSIQUE : Z Q S D sur
## un clavier AZERTY correspondent à W A S D sur un clavier QWERTY.
extends Node

const KEYS := {
	"move_forward": [KEY_W],      # Z en AZERTY
	"move_back": [KEY_S],
	"move_left": [KEY_A],         # Q en AZERTY
	"move_right": [KEY_D],
	"jump": [KEY_SPACE],
	"sprint": [KEY_SHIFT],
	"dodge": [KEY_CTRL],
	"spell_1": [KEY_Q],           # A en AZERTY : Bit crush
	"spell_2": [KEY_E],           # Gater
	"interact": [KEY_F],
	"weapon_1": [KEY_1],
	"weapon_2": [KEY_2],
	"weapon_3": [KEY_3],
	"toggle_day_night": [KEY_N],
	"toggle_help": [KEY_F1],
	"toggle_pixel": [KEY_F2],
}

const MOUSE_BUTTONS := {
	"attack": MOUSE_BUTTON_LEFT,
	"fusion_mode": MOUSE_BUTTON_RIGHT,
}


func _enter_tree() -> void:
	for action: String in KEYS:
		_ensure(action)
		for code: int in KEYS[action]:
			var key := InputEventKey.new()
			key.physical_keycode = code
			InputMap.action_add_event(action, key)
	for action: String in MOUSE_BUTTONS:
		_ensure(action)
		var button := InputEventMouseButton.new()
		button.button_index = MOUSE_BUTTONS[action]
		InputMap.action_add_event(action, button)


func _ensure(action: String) -> void:
	if not InputMap.has_action(action):
		InputMap.add_action(action)
