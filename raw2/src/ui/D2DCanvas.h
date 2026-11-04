#pragma once
// Rendu Direct2D + DirectWrite (Windows). Les polices JetBrains Mono et Silkscreen sont chargées
// depuis la DLL (aucune police installée n'est nécessaire).
#include "ui/Canvas.h"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

struct ID2D1Factory;
struct ID2D1RenderTarget;
struct ID2D1SolidColorBrush;
struct ID2D1StrokeStyle;
struct ID2D1BitmapBrush;
struct ID2D1LinearGradientBrush;
struct ID2D1RadialGradientBrush;
struct IDWriteFactory;
struct IDWriteFontCollection;
struct IDWriteTextFormat;
struct IDWriteTextLayout;

namespace raw2::ui {

// Ressources indépendantes de la fenêtre, partagées par toutes les instances du plugin.
struct D2DShared {
    ID2D1Factory* d2d = nullptr;
    IDWriteFactory* dwrite = nullptr;
    IDWriteFontCollection* fonts = nullptr;  // nullptr : repli sur Consolas
    bool ok = false;
    static D2DShared& get();
};

class D2DCanvas final : public Canvas {
public:
    D2DCanvas();
    ~D2DCanvas() override;

    // Début / fin d'une image sur une cible de rendu ; scale = facteur logique -> pixels.
    void begin(ID2D1RenderTarget* rt, float scale);
    void end();
    void releaseDeviceResources();  // à appeler si la cible de rendu est recréée

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
    struct Xf {
        float m11, m12, m21, m22, dx, dy;
    };
    struct Layout {
        IDWriteTextLayout* layout = nullptr;
        float width = 0.f, baseline = 0.f;
        uint64_t used = 0;
    };
    void applyTransform();
    void setColor(Color c);
    Layout* layoutFor(const std::string& s, const Font& f);
    IDWriteTextFormat* formatFor(const Font& f);
    void strokePath(const Pt* p, int n, Color c, float width, bool closed, ID2D1StrokeStyle* style);

    ID2D1RenderTarget* rt_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    ID2D1StrokeStyle* roundJoin_ = nullptr;
    std::map<std::pair<int, int>, ID2D1StrokeStyle*> dashes_;
    float scale_ = 1.f;
    Xf cur_{1, 0, 0, 1, 0, 0};
    struct Saved {
        Xf xf;
        int clips;
    };
    std::vector<Saved> stack_;
    int clips_ = 0;
    std::unordered_map<std::string, Layout> layouts_;
    std::map<std::pair<int, int>, IDWriteTextFormat*> formats_;
    struct DotKey {
        int sp, r, rgba, sc;
        bool operator<(const DotKey& o) const { return std::tie(sp, r, rgba, sc) < std::tie(o.sp, o.r, o.rgba, o.sc); }
    };
    std::map<DotKey, ID2D1BitmapBrush*> dots_;
    // dégradés : un pinceau par jeu d'arrêts (la géométrie est mise à jour à chaque usage)
    std::unordered_map<uint64_t, ID2D1LinearGradientBrush*> linear_;
    std::unordered_map<uint64_t, ID2D1BitmapBrush*> lens_;
    std::unordered_map<uint64_t, ID2D1RadialGradientBrush*> radial_;
    uint64_t frame_ = 0;
};

} // namespace raw2::ui
