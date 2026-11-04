#pragma once
// Rendu CoreGraphics + CoreText (macOS) : le plugin (vue Cocoa du VST3 et de l'Audio Unit) et
// l'outil de capture. Le contexte doit avoir l'axe y vers le bas (vue « flipped », ou contexte
// bitmap retourné par l'appelant) ; l'échelle (zoom) est appliquée par l'appelant.
#include "ui/Canvas.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

namespace raw2::ui {

class CGCanvas final : public Canvas {
public:
    CGCanvas();
    ~CGCanvas() override;
    CGCanvas(const CGCanvas&) = delete;
    CGCanvas& operator=(const CGCanvas&) = delete;
    void setContext(CGContextRef ctx) { ctx_ = ctx; }  // à chaque image (le contexte change)
    void frameDone();  // fin d'image : libère les couches mises en cache si le contexte change

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
    void fill(Color c);
    void stroke(Color c, float width);
    CTFontRef font(const Font& f);
    CTLineRef lineFor(const std::string& s, const Font& f);
    void roundRectPath(const Rect& r, float radius);
    float pixelScale() const;

    CGContextRef ctx_ = nullptr;
    CGColorSpaceRef space_ = nullptr;
    std::array<CGFontRef, (size_t)Face::Count> faces_{};
    std::unordered_map<uint64_t, CTFontRef> fonts_;      // (face, taille)
    std::unordered_map<std::string, CTLineRef> lines_;   // texte mis en forme (couleur prise du contexte)
    std::unordered_map<uint64_t, CGImageRef> tiles_;     // motifs répétés (points du châssis, lentilles de VIE)
};

} // namespace raw2::ui
