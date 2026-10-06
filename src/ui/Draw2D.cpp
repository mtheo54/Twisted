#include "Draw2D.h"

#include "Gl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace tw {

namespace {
const char* kVs = R"(#version 150
in vec2 aPos; in vec2 aUv; in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUv; out vec4 vColor;
void main() {
    vUv = aUv; vColor = aColor;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";
const char* kFs = R"(#version 150
in vec2 vUv; in vec4 vColor; out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    if (vUv.x < 0.0) fragColor = vColor;
    else fragColor = vec4(vColor.rgb, vColor.a * texture(uTex, vUv).r);
}
)";

constexpr int kAtlas = 1024;

std::vector<unsigned char> readFile(const std::string& path) {
    std::vector<unsigned char> data;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        data.resize((size_t)n);
        if (std::fread(data.data(), 1, (size_t)n, f) != (size_t)n) data.clear();
    }
    std::fclose(f);
    return data;
}

std::vector<std::string> fontCandidates(int family) {  // 0 sans, 1 mono, 2 serif
#if defined(_WIN32)
    const char* win = std::getenv("WINDIR");
    std::string dir = std::string(win ? win : "C:\\Windows") + "\\Fonts\\";
    if (family == 0) return {dir + "segoeui.ttf", dir + "arial.ttf"};
    if (family == 1) return {dir + "consola.ttf", dir + "cour.ttf"};
    return {dir + "georgia.ttf", dir + "times.ttf"};
#elif defined(__APPLE__)
    if (family == 0) return {"/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf"};
    if (family == 1) return {"/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Monaco.ttf", "/System/Library/Fonts/Supplemental/Courier New.ttf"};
    return {"/System/Library/Fonts/Supplemental/Georgia.ttf", "/Library/Fonts/Georgia.ttf", "/System/Library/Fonts/Times.ttc"};
#else
    if (family == 0) return {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"};
    if (family == 1) return {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"};
    return {"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf"};
#endif
}

// Symbol font for the few glyphs the text fonts lack (Segoe UI has no ◎ ▾ ●, for instance).
std::vector<std::string> symbolFontCandidates() {
#if defined(_WIN32)
    const char* win = std::getenv("WINDIR");
    std::string dir = std::string(win ? win : "C:\\Windows") + "\\Fonts\\";
    return {dir + "seguisym.ttf", dir + "arial.ttf"};
#elif defined(__APPLE__)
    return {"/System/Library/Fonts/Apple Symbols.ttf", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
            "/Library/Fonts/Arial Unicode.ttf"};
#else
    return {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
#endif
}

uint32_t nextCodepoint(const std::string& s, size_t& i) {
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
    uint32_t cp = c & (0x3F >> extra);
    while (extra-- > 0 && i < s.size()) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}
} // namespace

bool Draw2D::init(float scale) {
    scale_ = scale > 0 ? scale : 1.f;
    const char* attrs[] = {"aPos", "aUv", "aColor"};
    prog_ = compileProgram(kVs, kFs, attrs, 3);
    uScreen_ = GL.GetUniformLocation(prog_, "uScreen");
    uTex_ = GL.GetUniformLocation(prog_, "uTex");
    GL.GenVertexArrays(1, &vao_);
    GL.BindVertexArray(vao_);
    GL.GenBuffers(1, &vbo_);
    GL.BindBuffer(TW_GL_ARRAY_BUFFER, vbo_);
    const int s = sizeof(V);
    GL.EnableVertexAttribArray(0); GL.VertexAttribPointer(0, 2, GL_FLOAT, 0, s, (void*)0);
    GL.EnableVertexAttribArray(1); GL.VertexAttribPointer(1, 2, GL_FLOAT, 0, s, (void*)(2 * sizeof(float)));
    GL.EnableVertexAttribArray(2); GL.VertexAttribPointer(2, 4, GL_FLOAT, 0, s, (void*)(4 * sizeof(float)));
    GL.BindVertexArray(0);
    fontsOk_ = bakeFonts();
    return prog_ != 0;
}

void Draw2D::destroy() {
    if (vbo_) GL.DeleteBuffers(1, &vbo_);
    if (vao_) GL.DeleteVertexArrays(1, &vao_);
    if (tex_) glDeleteTextures(1, &tex_);
    if (prog_) GL.DeleteProgram(prog_);
    vbo_ = vao_ = tex_ = prog_ = 0;
}

bool Draw2D::bakeFonts() {
    struct Spec { int family; float px; };
    const Spec specs[kNumFonts] = {{0, 13.f}, {0, 11.f}, {0, 10.f}, {1, 11.f}, {2, 22.f}};
    std::vector<unsigned char> files[3];
    int offsets[3] = {0, 0, 0};
    for (int fam = 0; fam < 3; ++fam)
        for (const auto& path : fontCandidates(fam)) {
            files[fam] = readFile(path);
            if (!files[fam].empty()) { offsets[fam] = std::max(0, stbtt_GetFontOffsetForIndex(files[fam].data(), 0)); break; }
        }
    std::vector<unsigned char> symFile;
    stbtt_fontinfo symInfo{};
    for (const auto& path : symbolFontCandidates()) {
        symFile = readFile(path);
        if (!symFile.empty() &&
            stbtt_InitFont(&symInfo, symFile.data(), std::max(0, stbtt_GetFontOffsetForIndex(symFile.data(), 0))))
            break;
        symFile.clear();
    }

    // Latin-1 plus the few typographic characters the UI uses.
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c = 160; c < 256; ++c) cps.push_back(c);
    for (int c : {0x2212, 0x2026, 0x2014, 0x2013, 0x2192, 0x221E, 0x2248, 0x25CE, 0x25B6, 0x25A0, 0x2022, 0x25BE, 0x25CF,
                  0x014D})
        cps.push_back(c);

    std::vector<unsigned char> atlas((size_t)kAtlas * kAtlas, 0);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, atlas.data(), kAtlas, kAtlas, 0, 1, nullptr)) return false;
    stbtt_PackSetOversampling(&pc, 1, 1);
    bool any = false;
    std::vector<stbtt_packedchar> packed(cps.size());
    for (int f = 0; f < kNumFonts; ++f) {
        const int fam = specs[f].family;
        if (files[fam].empty()) continue;
        stbtt_pack_range range{};
        range.font_size = specs[f].px * scale_;
        range.array_of_unicode_codepoints = cps.data();
        range.num_chars = (int)cps.size();
        range.chardata_for_range = packed.data();
        stbtt_fontinfo info;
        if (!stbtt_InitFont(&info, files[fam].data(), offsets[fam])) continue;
        // Characters this font lacks are taken from the symbol font instead.
        std::vector<int> missing;
        std::vector<size_t> missingAt;
        if (!symFile.empty())
            for (size_t i = 0; i < cps.size(); ++i)
                if (cps[i] >= 0x100 && !stbtt_FindGlyphIndex(&info, cps[i]) && stbtt_FindGlyphIndex(&symInfo, cps[i])) {
                    missing.push_back(cps[i]);
                    missingAt.push_back(i);
                }
        if (!stbtt_PackFontRanges(&pc, files[fam].data(), offsets[fam], &range, 1)) continue;
        if (!missing.empty()) {
            std::vector<stbtt_packedchar> symPacked(missing.size());
            stbtt_pack_range sr{};
            sr.font_size = range.font_size;
            sr.array_of_unicode_codepoints = missing.data();
            sr.num_chars = (int)missing.size();
            sr.chardata_for_range = symPacked.data();
            if (stbtt_PackFontRanges(&pc, symFile.data(), (int)(symInfo.data ? symInfo.fontstart : 0), &sr, 1))
                for (size_t k = 0; k < missing.size(); ++k) packed[missingAt[k]] = symPacked[k];
        }
        int asc, desc, gap;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
        Font& font = fonts_[f];
        font.ascent = asc * stbtt_ScaleForPixelHeight(&info, range.font_size);
        font.glyphs.resize(cps.size());
        font.codepoints.assign(cps.begin(), cps.end());
        for (size_t i = 0; i < cps.size(); ++i) {
            const stbtt_packedchar& p = packed[i];
            font.glyphs[i] = {p.xoff, p.yoff, p.xoff2, p.yoff2, p.x0 / (float)kAtlas, p.y0 / (float)kAtlas,
                              p.x1 / (float)kAtlas, p.y1 / (float)kAtlas, p.xadvance};
        }
        font.ok = true;
        any = true;
    }
    stbtt_PackEnd(&pc);
    if (!any) return false;

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, TW_GL_R8, kAtlas, kAtlas, 0, TW_GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, TW_GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, TW_GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

const Draw2D::Glyph* Draw2D::glyph(FontId f, uint32_t cp) const {
    const Font& font = fonts_[f];
    if (!font.ok) return nullptr;
    // codepoints are sorted except for the trailing specials: binary search the Latin part
    if (cp < 256) {
        auto it = std::lower_bound(font.codepoints.begin(), font.codepoints.begin() + (127 - 32) + 96, cp);
        if (it != font.codepoints.end() && *it == cp) return &font.glyphs[(size_t)(it - font.codepoints.begin())];
    } else {
        for (size_t i = (127 - 32) + 96; i < font.codepoints.size(); ++i)
            if (font.codepoints[i] == cp) return &font.glyphs[i];
    }
    return glyph(f, '?');
}

void Draw2D::begin(int fbW, int fbH) {
    fbW_ = fbW > 0 ? fbW : 1;
    fbH_ = fbH > 0 ? fbH : 1;
    verts_.clear();
}

void Draw2D::tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
    const float s = scale_;
    verts_.push_back({x0 * s, y0 * s, -1, -1, c.r, c.g, c.b, c.a});
    verts_.push_back({x1 * s, y1 * s, -1, -1, c.r, c.g, c.b, c.a});
    verts_.push_back({x2 * s, y2 * s, -1, -1, c.r, c.g, c.b, c.a});
}

void Draw2D::rect(float x, float y, float w, float h, Color c) {
    tri(x, y, x + w, y, x + w, y + h, c);
    tri(x, y, x + w, y + h, x, y + h, c);
}

void Draw2D::rectGradient(float x, float y, float w, float h, Color t, Color b) {
    const float s = scale_;
    const V a{x * s, y * s, -1, -1, t.r, t.g, t.b, t.a}, bb{(x + w) * s, y * s, -1, -1, t.r, t.g, t.b, t.a};
    const V c{(x + w) * s, (y + h) * s, -1, -1, b.r, b.g, b.b, b.a}, d{x * s, (y + h) * s, -1, -1, b.r, b.g, b.b, b.a};
    for (const V& v : {a, bb, c, a, c, d}) verts_.push_back(v);
}

void Draw2D::roundRect(float x, float y, float w, float h, float r, Color c) {
    r = std::min(r, std::min(w, h) * 0.5f);
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, r, h - 2 * r, c);
    rect(x + w - r, y + r, r, h - 2 * r, c);
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
    for (int k = 0; k < 4; ++k) {
        const float a0 = (float)k * 1.5707963f - 1.5707963f;  // quarter per corner
        for (int i = 0; i < 6; ++i) {
            const float t0 = a0 + i * 1.5707963f / 6, t1 = a0 + (i + 1) * 1.5707963f / 6;
            tri(cx[k], cy[k], cx[k] + std::sin(t0) * r, cy[k] - std::cos(t0) * r, cx[k] + std::sin(t1) * r, cy[k] - std::cos(t1) * r, c);
        }
    }
}

void Draw2D::roundRectStroke(float x, float y, float w, float h, float r, float t, Color c) {
    r = std::min(r, std::min(w, h) * 0.5f);
    rect(x + r, y, w - 2 * r, t, c);
    rect(x + r, y + h - t, w - 2 * r, t, c);
    rect(x, y + r, t, h - 2 * r, c);
    rect(x + w - t, y + r, t, h - 2 * r, c);
    arc(x + r, y + r, r - t / 2, -1.5707963f, 0, t, c);
    arc(x + w - r, y + r, r - t / 2, 0, 1.5707963f, t, c);
    arc(x + w - r, y + h - r, r - t / 2, 1.5707963f, 3.1415926f, t, c);
    arc(x + r, y + h - r, r - t / 2, 3.1415926f, 4.712389f, t, c);
}

void Draw2D::circle(float cx, float cy, float r, Color c) {
    const int n = std::max(12, (int)(r * 1.5f));
    for (int i = 0; i < n; ++i) {
        const float a0 = i * 6.2831853f / n, a1 = (i + 1) * 6.2831853f / n;
        tri(cx, cy, cx + std::sin(a0) * r, cy - std::cos(a0) * r, cx + std::sin(a1) * r, cy - std::cos(a1) * r, c);
    }
}

void Draw2D::arc(float cx, float cy, float r, float a0, float a1, float t, Color c) {
    if (a1 < a0) std::swap(a0, a1);
    const int n = std::max(2, (int)((a1 - a0) * r * 0.6f));
    const float ri = r - t / 2, ro = r + t / 2;
    for (int i = 0; i < n; ++i) {
        const float b0 = a0 + (a1 - a0) * i / n, b1 = a0 + (a1 - a0) * (i + 1) / n;
        const float s0 = std::sin(b0), c0 = -std::cos(b0), s1 = std::sin(b1), c1 = -std::cos(b1);
        tri(cx + s0 * ri, cy + c0 * ri, cx + s0 * ro, cy + c0 * ro, cx + s1 * ro, cy + c1 * ro, c);
        tri(cx + s0 * ri, cy + c0 * ri, cx + s1 * ro, cy + c1 * ro, cx + s1 * ri, cy + c1 * ri, c);
    }
    // round caps
    const float capR = t / 2;
    for (float a : {a0, a1}) {
        const float px = cx + std::sin(a) * r, py = cy - std::cos(a) * r;
        for (int i = 0; i < 8; ++i) {
            const float b0 = i * 6.2831853f / 8, b1 = (i + 1) * 6.2831853f / 8;
            tri(px, py, px + std::sin(b0) * capR, py - std::cos(b0) * capR, px + std::sin(b1) * capR, py - std::cos(b1) * capR, c);
        }
    }
}

void Draw2D::line(float x0, float y0, float x1, float y1, float t, Color c) {
    const float dx = x1 - x0, dy = y1 - y0, l = std::sqrt(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    const float nx = -dy / l * t / 2, ny = dx / l * t / 2;
    tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

float Draw2D::textWidth(FontId f, const std::string& s) const {
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        const Glyph* g = glyph(f, nextCodepoint(s, i));
        if (g) w += g->adv;
    }
    return w / scale_;
}

float Draw2D::text(FontId f, float x, float y, const std::string& s, Color c, int align) {
    if (!fonts_[f].ok) return 0;
    const float w = textWidth(f, s);
    if (align == 1) x -= w / 2;
    else if (align == 2) x -= w;
    float px = std::floor(x * scale_);
    const float py = std::floor(y * scale_);
    for (size_t i = 0; i < s.size();) {
        const Glyph* g = glyph(f, nextCodepoint(s, i));
        if (!g) continue;
        const float x0 = px + g->x0, y0 = py + g->y0, x1 = px + g->x1, y1 = py + g->y1;
        const V a{x0, y0, g->u0, g->v0, c.r, c.g, c.b, c.a}, b{x1, y0, g->u1, g->v0, c.r, c.g, c.b, c.a};
        const V cc{x1, y1, g->u1, g->v1, c.r, c.g, c.b, c.a}, d{x0, y1, g->u0, g->v1, c.r, c.g, c.b, c.a};
        for (const V& v : {a, b, cc, a, cc, d}) verts_.push_back(v);
        px += g->adv;
    }
    return w;
}

void Draw2D::flush() {
    if (verts_.empty()) return;
    GL.UseProgram(prog_);
    GL.Uniform2f(uScreen_, (float)fbW_, (float)fbH_);
    GL.Uniform1i(uTex_, 0);
    GL.ActiveTexture(TW_GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    GL.BindVertexArray(vao_);
    GL.BindBuffer(TW_GL_ARRAY_BUFFER, vbo_);
    GL.BufferData(TW_GL_ARRAY_BUFFER, (GLsizeiptrT)(verts_.size() * sizeof(V)), verts_.data(), TW_GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (int)verts_.size());
    GL.BindVertexArray(0);
    verts_.clear();
}

void Draw2D::end() {
    glViewport(0, 0, fbW_, fbH_);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    flush();
}

} // namespace tw
