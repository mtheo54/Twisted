// Affiche l'image basse résolution du bonhomme avec de gros pixels nets.
// Les pixels transparents sont jetés : la silhouette reste en escalier,
// comme sur l'icône d'origine.
shader_type spatial;
render_mode unshaded, cull_disabled, shadows_disabled;

uniform sampler2D pixels : filter_nearest, repeat_disable;
uniform float alpha_cut : hint_range(0.0, 1.0) = 0.5;

void fragment() {
	vec4 c = texture(pixels, UV);
	if (c.a < alpha_cut) {
		discard;
	}
	ALBEDO = c.rgb;
}
