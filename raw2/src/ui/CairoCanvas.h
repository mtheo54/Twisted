#pragma once
// Rendu Cairo + FreeType (Linux) : sert à l'aperçu de développement et aux captures d'écran.
#include "ui/Canvas.h"

#include <array>
#include <string>
#include <unordered_map>

typedef struct _cairo cairo_t;
typedef struct _cairo_font_face cairo_font_face_t;
typedef struct _cairo_surface cairo_surface_t;

namespace raw2::ui {

class CairoCanvas final : public Canvas {
public:
    explicit CairoCanvas(cairo_t* cr);
    ~CairoCanvas() override;
    void setContext(cairo_t* cr) { cr_ = cr; }

    void save() override;
    void restore() override;
    void translate(float dx, float dy) override;
    void rotate(float radians) override;
    void scale(float s) override;
    void clip(const Rect& r) override;
    void fillRect(const Rect& r, Color c) override;
    void fillRoundRect(const Rect& r, float radius, Color c) override;
    void line(Pt a, Pt b, Color c, float width) override;
    void polyline(const Pt* p, int n, Color c, float width, bool closed) override;
    void dashedPolyline(const Pt* p, int n, Color c, float width, float on, float off) override;
    void fillPolygon(const Pt* p, int n, Color c) override;
    void fillEllipse(Pt center, float rx, float ry, Color c) override;
    void strokeEllipse(Pt center, float rx, float ry, Color c, float width) override;
    void arc(Pt center, float radius, float a0, float a1, Color c, float width) override;
    void fillCircles(const Pt* centers, int n, float radius, Color c) override;
    void strokeCircles(const Pt* centers, int n, float radius, Color c, float width) override;
    void dotGrid(const Rect& area, float ox, float oy, float spacing, float radius, Color c) override;
    void fillLinear(const Rect& r, float radius, Pt p0, Pt p1, const GradStop* s, int n) override;
    void lensGrid(const Rect& area, float ox, float oy, float pitch, float radius, Color c0, Color c1, Color c2) override;
    void fillRadial(Pt center, float rx, float ry, Pt origin, float gradRadius, const GradStop* s, int n) override;
    void text(const std::string& utf8, float x, float baseline, const Font& f, Color c) override;
    float textWidth(const std::string& utf8, const Font& f) override;

private:
    void setColor(Color c);
    void selectFont(const Font& f);
    cairo_t* cr_;
    std::array<cairo_font_face_t*, (size_t)Face::Count> faces_{};
    std::unordered_map<std::string, float> widthCache_;
};

} // namespace raw2::ui
