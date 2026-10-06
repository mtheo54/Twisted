## Fait avancer la Sim à chaque pas de physique, puis diffuse ses événements
## aux scènes (personnages, effets, HUD) via le signal sim_event.
## Passe après les personnages (priorité haute) pour recevoir leurs ordres
## du même pas.
extends Node

signal sim_event(event: Dictionary)

const Sim = preload("res://core/sim.gd")

var sim: Sim


func _ready() -> void:
	process_physics_priority = 100


func _physics_process(delta: float) -> void:
	if sim == null:
		return
	sim.step(delta)
	for event in sim.drain_events():
		sim_event.emit(event)
