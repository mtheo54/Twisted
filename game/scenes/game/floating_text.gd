## Petits textes 3D qui montent et s'effacent (chiffres de dégâts, etc.).
extends RefCounted


static func spawn(parent: Node, at: Vector3, text: String, color: Color = Color.WHITE) -> void:
	var label := Label3D.new()
	label.text = text
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.no_depth_test = true
	label.fixed_size = true
	label.pixel_size = 0.0012
	label.font_size = 36
	label.outline_size = 10
	label.modulate = color
	label.outline_modulate = Color(0.08, 0.06, 0.12)
	parent.add_child(label)
	label.global_position = at + Vector3(randf_range(-0.3, 0.3), 0.0, randf_range(-0.3, 0.3))
	var tween := label.create_tween()
	tween.set_parallel(true)
	tween.tween_property(label, "global_position", label.global_position + Vector3.UP * 1.1, 0.8).set_ease(Tween.EASE_OUT).set_trans(Tween.TRANS_CUBIC)
	tween.tween_property(label, "modulate:a", 0.0, 0.8).set_delay(0.3)
	tween.chain().tween_callback(label.queue_free)
