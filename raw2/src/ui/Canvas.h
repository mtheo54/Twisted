#pragma once
// Surface de dessin 2D abstraite. Implémentations : Direct2D/DirectWrite (Windows, le plugin)
// et Cairo (Linux, outil de développement pour l'aperçu et les captures).
// Coordonnées logiques : la plaque mesure 1280 x 960, l'échelle (zoom, écran) est appliquée
// par l'implémentation.
#include "core/Color.h"

#include <string>

namespace raw2::ui {

struct Pt {
    float x = 0.f, y = 0.f;
};

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    float r() const { return x + w; }
    float b() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool contains(Pt p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
    Rect inset(float d) const { return Rect{x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(float dx, float dy) const { return Rect{x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect moved(float dx, float dy) const { return Rect{x + dx, y + dy, w, h}; }
};

enum class Face { Mono = 0, MonoMedium, MonoBold, Pixel, Count };

struct Font {
    Face face = Face::Mono;
    float size = 12.f;
    float spacing = 0.f;  // espacement des lettres en px (letter-spacing CSS : ajouté après chaque caractère)
};

// Métriques verticales des polices embarquées (table OS/2 « typo », comme les navigateurs).
struct FaceMetrics {
    float ascent, descent;  // en em
};
inline FaceMetrics faceMetrics(Face f) { return f == Face::Pixel ? FaceMetrics{1.03f, 0.25f} : FaceMetrics{1.02f, 0.30f}; }

// Ligne de base d'un texte placé dans une boîte CSS (top = haut de la boîte, lineHeight 0 = « normal »).
inline float baselineIn(const Font& f, float top, float lineHeight = 0.f) {
    const FaceMetrics m = faceMetrics(f.face);
    const float content = (m.ascent + m.descent) * f.size;
    const float lh = lineHeight > 0.f ? lineHeight : content;
    return top + (lh - content) * 0.5f + m.ascent * f.size;
}
inline float normalLineHeight(const Font& f) {
    const FaceMetrics m = faceMetrics(f.face);
    return (m.ascent + m.descent) * f.size;
}

// Arrêt d'un dégradé (position 0..1).
struct GradStop {
    float pos;
    Color c;
};

class Canvas {
public:
    virtual ~Canvas() = default;

    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void translate(float dx, float dy) = 0;
    virtual void rotate(float radians) = 0;
    virtual void scale(float s) = 0;
    virtual void clip(const Rect& r) = 0;

    virtual void fillRect(const Rect& r, Color c) = 0;
    virtual void fillRoundRect(const Rect& r, float radius, Color c) = 0;
    virtual void line(Pt a, Pt b, Color c, float width) = 0;
    virtual void polyline(const Pt* p, int n, Color c, float width, bool closed = false) = 0;
    virtual void dashedPolyline(const Pt* p, int n, Color c, float width, float on, float off) = 0;
    virtual void fillPolygon(const Pt* p, int n, Color c) = 0;
    virtual void fillEllipse(Pt center, float rx, float ry, Color c) = 0;
    virtual void strokeEllipse(Pt center, float rx, float ry, Color c, float width) = 0;
    // arc de a0 à a1 (radians, sens horaire à l'écran, y vers le bas)
    virtual void arc(Pt center, float radius, float a0, float a1, Color c, float width) = 0;
    virtual void fillCircles(const Pt* centers, int n, float radius, Color c) = 0;
    virtual void strokeCircles(const Pt* centers, int n, float radius, Color c, float width) = 0;
    // dégradés (boutons moletés, touches, molettes, écrans) ; n <= 8 arrêts
    virtual void fillLinear(const Rect& r, float radius, Pt p0, Pt p1, const GradStop* s, int n) = 0;
    virtual void fillRadial(Pt center, float rx, float ry, Pt origin, float gradRadius, const GradStop* s, int n) = 0;
    // motif de « lentilles » (cellules mortes de la vie) : un disque à dégradé radial par case,
    // dégradé c0 (au point 38 % / 32 %) -> c1 (65 %) -> c2 (bord), petit reflet clair
    virtual void lensGrid(const Rect& area, float ox, float oy, float pitch, float radius, Color c0, Color c1, Color c2) = 0;
    // motif de points (radial-gradient CSS) : points centrés à ox + spacing/2 + k*spacing
    virtual void dotGrid(const Rect& area, float ox, float oy, float spacing, float radius, Color c) = 0;

    virtual void text(const std::string& utf8, float x, float baseline, const Font& f, Color c) = 0;
    virtual float textWidth(const std::string& utf8, const Font& f) = 0;
};

} // namespace raw2::ui
