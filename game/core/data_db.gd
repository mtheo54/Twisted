## Lit les fichiers d'équilibrage de data/ (JSON).
## Tous les nombres du jeu (vie, dégâts, coûts, durées) viennent d'ici.
extends RefCounted

const TABLES := ["actors", "weapons"]

var _tables: Dictionary = {}


func _init(base_path: String = "res://data") -> void:
	for table: String in TABLES:
		_tables[table] = _load_json("%s/%s.json" % [base_path, table])


func actor(id: String) -> Dictionary:
	return _table("actors").get(id, {})


func weapon(id: String) -> Dictionary:
	return _table("weapons").get(id, {})


func _table(table: String) -> Dictionary:
	return _tables.get(table, {})


static func _load_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		push_error("Données : fichier introuvable %s" % path)
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed is Dictionary:
		return parsed
	push_error("Données : %s n'est pas un objet JSON valide" % path)
	return {}
