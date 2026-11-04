#pragma once
// Couleur RVBA en flottants 0..1, partagée par les thèmes et le rendu.
#include <cstdint>

namespace raw2 {

struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;

    constexpr Color withAlpha(float alpha) const { return Color{r, g, b, a * alpha}; }
    // Mélange linéaire : k = 0 donne *this, k = 1 donne o.
    constexpr Color mix(const Color& o, float k) const {
        return Color{r + (o.r - r) * k, g + (o.g - g) * k, b + (o.b - b) * k, a + (o.a - a) * k};
    }
    static constexpr Color rgb8(int R, int G, int B, float A = 1.f) { return Color{R / 255.f, G / 255.f, B / 255.f, A}; }
};

struct Rgb8 {
    int r = 0, g = 0, b = 0;
};

} // namespace raw2
