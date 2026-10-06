// Façade d'immeuble : murs, bandeaux entre étages, fenêtres en grille.
// Le jour, vitres gris-bleu ; la nuit (uniform « night »), une partie des
// fenêtres s'allume en jaune chaud.
// Le maillage est un cube unité mis à l'échelle ; la taille réelle vient de
// box_size (immeubles proches) ou de INSTANCE_CUSTOM (immeubles lointains en
// MultiMesh) pour que les fenêtres gardent toujours la même taille.
shader_type spatial;

uniform vec3 wall_color : source_color = vec3(0.6, 0.6, 0.64);
uniform vec3 roof_color : source_color = vec3(0.42, 0.42, 0.47);
uniform vec3 glass_color : source_color = vec3(0.30, 0.35, 0.44);
uniform vec3 lit_color : source_color = vec3(1.0, 0.78, 0.45);
uniform float night : hint_range(0.0, 1.0) = 0.0;
uniform float lit_ratio : hint_range(0.0, 1.0) = 0.45;
uniform float floor_height = 3.3;
uniform float window_spacing = 2.8;
uniform bool from_multimesh = false;

instance uniform vec3 box_size = vec3(10.0, 20.0, 10.0);
instance uniform float seed = 0.0;

varying vec3 local_pos;
varying vec3 local_normal;
varying vec3 size_v;
varying float seed_v;

void vertex() {
	vec3 size = box_size;
	float s = seed;
	if (from_multimesh) {
		size = INSTANCE_CUSTOM.xyz;
		s = INSTANCE_CUSTOM.w;
	}
	size_v = size;
	seed_v = s;
	local_pos = (VERTEX + 0.5) * size;
	local_normal = NORMAL;
}

float hash21(vec2 p) {
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

void fragment() {
	vec3 col = wall_color;
	float rough = 0.9;
	float metal = 0.0;
	vec3 emit = vec3(0.0);

	if (abs(local_normal.y) > 0.5) {
		col = roof_color;
	} else {
		bool side_x = abs(local_normal.x) > 0.5;
		float u = side_x ? local_pos.z : local_pos.x;
		float width = side_x ? size_v.z : size_v.x;
		float face_id = side_x ? sign(local_normal.x) : 2.0 + sign(local_normal.z);
		float cols = max(1.0, floor(width / window_spacing));
		float rows = max(1.0, floor(size_v.y / floor_height));
		vec2 cuv = vec2(u / width * cols, local_pos.y / size_v.y * rows);
		vec2 cell = floor(cuv);
		vec2 f = fract(cuv);

		// bandeau sombre entre les étages
		col = mix(wall_color, wall_color * 0.8, step(f.y, 0.05));

		// rez-de-chaussée : vitrines plus larges
		bool ground = cell.y < 0.5;
		vec2 lo = ground ? vec2(0.08, 0.10) : vec2(0.24, 0.32);
		vec2 hi = ground ? vec2(0.92, 0.75) : vec2(0.76, 0.82);
		bool win = f.x > lo.x && f.x < hi.x && f.y > lo.y && f.y < hi.y;
		if (win) {
			float h = hash21(cell + vec2(face_id * 17.0 + seed_v, seed_v * 3.1));
			float lit = step(h, lit_ratio);
			col = glass_color * (0.85 + 0.3 * h);
			// léger reflet du ciel en haut de la vitre, le jour
			col += vec3(0.07) * smoothstep(lo.y + 0.2, hi.y, f.y) * (1.0 - night);
			rough = 0.2;
			metal = 0.35;
			emit = lit_color * lit * night * (1.6 + h);
		}
	}

	ALBEDO = col;
	ROUGHNESS = rough;
	METALLIC = metal;
	EMISSION = emit;
}
