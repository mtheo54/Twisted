## État d'une entité dans la Sim (joueur, monstre, cible) : vie, position,
## recharges. Aucune donnée d'affichage ici.
extends RefCounted

var id := 0
var archetype := ""
var team := 0
var max_health := 100.0
var health := 100.0
var radius := 0.5
var alive := true
var position := Vector3.ZERO
var facing := Vector3.FORWARD
## nom d'action -> instant de la Sim où elle redevient disponible
var ready_at: Dictionary = {}
var invulnerable_until := -1.0
var last_hit_at := -1000.0
var died_at := -1.0
## < 0 : ne revient pas après sa mort
var respawn_delay := -1.0


func can_use(action: String, now: float) -> bool:
	return now >= float(ready_at.get(action, 0.0))


func is_invulnerable(now: float) -> bool:
	return now < invulnerable_until
