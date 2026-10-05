// Immediate-mode 2D drawing on top of the scene: rectangles, arcs, text.
// Text uses the system's own fonts (Segoe UI / Consolas / Georgia on Windows,
// Helvetica / Menlo / Georgia on macOS, DejaVu on Linux) rasterized with stb_truetype.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tw {

struct Color {
    float r, g, b, a;
};
inline Color hexColor(uint32_t hex, float a = 1.f) {
    return {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a};
}
inline Color withAlpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }

enum FontId { kFontUi = 0, kFontSmall, kFontLabel, kFontMono, kFontTitle, kNumFonts };

class Draw2D {
public:
    bool init(float scale);  // GL context current; scale = device pixels per logical pixel
    void destroy();
    bool hasFonts() const { return fontsOk_; }

    void begin(int fbW, int fbH);
    void end();

    void rect(float x, float y, float w, float h, Color c);
    void rectGradient(float x, float y, float w, float h, Color top, Color bottom);
    void roundRect(float x, float y, float w, float h, float r, Color c);
    void roundRectStroke(float x, float y, float w, float h, float r, float t, Color c);
    void circle(float cx, float cy, float r, Color c);
    // angles in radians, 0 = up, clockwise
    void arc(float cx, float cy, float r, float a0, float a1, float thickness, Color c);
    void line(float x0, float y0, float x1, float y1, float thickness, Color c);

    // align: 0 left, 1 center, 2 right. y is the baseline. Returns the advance width.
    float text(FontId f, float x, float y, const std::string& utf8, Color c, int align = 0);
    float textWidth(FontId f, const std::string& utf8) const;

private:
    struct V { float x, y, u, v, r, g, b, a; };
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c);
    void flush();
    bool bakeFonts();

    float scale_ = 1.f;
    int fbW_ = 1, fbH_ = 1;
    std::vector<V> verts_;
    unsigned prog_ = 0, vao_ = 0, vbo_ = 0, tex_ = 0;
    int uScreen_ = -1, uTex_ = -1;
    bool fontsOk_ = false;

    struct Glyph { float x0, y0, x1, y1, u0, v0, u1, v1, adv; };
    struct Font { std::vector<Glyph> glyphs; std::vector<uint32_t> codepoints; float ascent = 0; bool ok = false; };
    Font fonts_[kNumFonts];
    const Glyph* glyph(FontId f, uint32_t cp) const;
};

} // namespace tw
