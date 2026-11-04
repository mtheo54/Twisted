#include "ui/CairoCanvas.h"
#include "ui/EmbeddedFonts.h"

#include <algorithm>
#include <cairo-ft.h>
#include <cairo.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cmath>
#include <vector>

namespace raw2::ui {

namespace {
FT_Library ftLib() {
    static FT_Library lib = [] {
        FT_Library l = nullptr;
        FT_Init_FreeType(&l);
        return l;
    }();
    return lib;
}
} // namespace

CairoCanvas::CairoCanvas(cairo_t* cr) : cr_(cr) {
    for (int i = 0; i < (int)Face::Count; ++i) {
        const embedded::File* f = fontFile((Face)i);
        FT_Face face = nullptr;
        if (f && FT_New_Memory_Face(ftLib(), f->data, (FT_Long)f->size, 0, &face) == 0)
            faces_[(size_t)i] = cairo_ft_font_face_create_for_ft_face(face, 0);
    }
}

CairoCanvas::~CairoCanvas() {
    for (auto* f : faces_)
        if (f) cairo_font_face_destroy(f);
}

void CairoCanvas::setColor(Color c) { cairo_set_source_rgba(cr_, c.r, c.g, c.b, c.a); }
void CairoCanvas::save() { cairo_save(cr_); }
void CairoCanvas::restore() { cairo_restore(cr_); }
void CairoCanvas::translate(float dx, float dy) { cairo_translate(cr_, dx, dy); }
void CairoCanvas::rotate(float a) { cairo_rotate(cr_, a); }
void CairoCanvas::scale(float s) { cairo_scale(cr_, s, s); }
void CairoCanvas::clip(const Rect& r) {
    cairo_rectangle(cr_, r.x, r.y, r.w, r.h);
    cairo_clip(cr_);
}

void CairoCanvas::fillRect(const Rect& r, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    setColor(c);
    cairo_rectangle(cr_, r.x, r.y, r.w, r.h);
    cairo_fill(cr_);
}

void CairoCanvas::fillRoundRect(const Rect& r, float rad, Color c) {
    setColor(c);
    const double x = r.x, y = r.y, w = r.w, h = r.h, k = std::min<double>(rad, std::min(w, h) / 2);
    cairo_new_sub_path(cr_);
    cairo_arc(cr_, x + w - k, y + k, k, -M_PI / 2, 0);
    cairo_arc(cr_, x + w - k, y + h - k, k, 0, M_PI / 2);
    cairo_arc(cr_, x + k, y + h - k, k, M_PI / 2, M_PI);
    cairo_arc(cr_, x + k, y + k, k, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr_);
    cairo_fill(cr_);
}

void CairoCanvas::line(Pt a, Pt b, Color c, float w) {
    setColor(c);
    cairo_set_line_width(cr_, w);
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_BUTT);
    cairo_move_to(cr_, a.x, a.y);
    cairo_line_to(cr_, b.x, b.y);
    cairo_stroke(cr_);
}

void CairoCanvas::polyline(const Pt* p, int n, Color c, float w, bool closed) {
    if (n < 2) return;
    setColor(c);
    cairo_set_line_width(cr_, w);
    cairo_set_line_join(cr_, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_BUTT);
    cairo_move_to(cr_, p[0].x, p[0].y);
    for (int i = 1; i < n; ++i) cairo_line_to(cr_, p[i].x, p[i].y);
    if (closed) cairo_close_path(cr_);
    cairo_stroke(cr_);
}

void CairoCanvas::dashedPolyline(const Pt* p, int n, Color c, float w, float on, float off) {
    if (n < 2) return;
    const double d[2] = {on, off};
    cairo_set_dash(cr_, d, 2, 0);
    polyline(p, n, c, w, false);
    cairo_set_dash(cr_, nullptr, 0, 0);
}

void CairoCanvas::fillPolygon(const Pt* p, int n, Color c) {
    if (n < 3) return;
    setColor(c);
    cairo_move_to(cr_, p[0].x, p[0].y);
    for (int i = 1; i < n; ++i) cairo_line_to(cr_, p[i].x, p[i].y);
    cairo_close_path(cr_);
    cairo_fill(cr_);
}

void CairoCanvas::fillLinear(const Rect& r, float radius, Pt p0, Pt p1, const GradStop* st, int n) {
    if (n < 2 || r.w <= 0 || r.h <= 0) return;
    cairo_pattern_t* pat = cairo_pattern_create_linear(p0.x, p0.y, p1.x, p1.y);
    for (int i = 0; i < n; ++i) cairo_pattern_add_color_stop_rgba(pat, st[i].pos, st[i].c.r, st[i].c.g, st[i].c.b, st[i].c.a);
    cairo_set_source(cr_, pat);
    if (radius > 0.f) {
        const float rr = std::min(radius, std::min(r.w, r.h) * 0.5f);
        cairo_new_sub_path(cr_);
        cairo_arc(cr_, r.r() - rr, r.y + rr, rr, -M_PI / 2, 0);
        cairo_arc(cr_, r.r() - rr, r.b() - rr, rr, 0, M_PI / 2);
        cairo_arc(cr_, r.x + rr, r.b() - rr, rr, M_PI / 2, M_PI);
        cairo_arc(cr_, r.x + rr, r.y + rr, rr, M_PI, 3 * M_PI / 2);
        cairo_close_path(cr_);
    } else {
        cairo_rectangle(cr_, r.x, r.y, r.w, r.h);
    }
    cairo_fill(cr_);
    cairo_pattern_destroy(pat);
}

void CairoCanvas::fillRadial(Pt c, float rx, float ry, Pt o, float gr, const GradStop* st, int n) {
    if (n < 2 || rx <= 0 || ry <= 0 || gr <= 0) return;
    cairo_pattern_t* pat = cairo_pattern_create_radial(o.x, o.y, 0, o.x, o.y, gr);
    for (int i = 0; i < n; ++i) cairo_pattern_add_color_stop_rgba(pat, st[i].pos, st[i].c.r, st[i].c.g, st[i].c.b, st[i].c.a);
    cairo_set_source(cr_, pat);
    cairo_save(cr_);
    cairo_translate(cr_, c.x, c.y);
    cairo_scale(cr_, rx, ry);
    cairo_arc(cr_, 0, 0, 1, 0, 2 * M_PI);
    cairo_restore(cr_);
    cairo_fill(cr_);
    cairo_pattern_destroy(pat);
}

void CairoCanvas::fillEllipse(Pt ctr, float rx, float ry, Color c) {
    setColor(c);
    cairo_save(cr_);
    cairo_translate(cr_, ctr.x, ctr.y);
    cairo_scale(cr_, rx, ry);
    cairo_arc(cr_, 0, 0, 1, 0, 2 * M_PI);
    cairo_restore(cr_);
    cairo_fill(cr_);
}

void CairoCanvas::strokeEllipse(Pt ctr, float rx, float ry, Color c, float w) {
    setColor(c);
    cairo_save(cr_);
    cairo_translate(cr_, ctr.x, ctr.y);
    cairo_scale(cr_, rx, ry);
    cairo_arc(cr_, 0, 0, 1, 0, 2 * M_PI);
    cairo_restore(cr_);
    cairo_set_line_width(cr_, w);
    cairo_stroke(cr_);
}

void CairoCanvas::arc(Pt ctr, float r, float a0, float a1, Color c, float w) {
    setColor(c);
    cairo_set_line_width(cr_, w);
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_BUTT);
    cairo_new_path(cr_);
    cairo_arc(cr_, ctr.x, ctr.y, r, a0, a1);
    cairo_stroke(cr_);
}

void CairoCanvas::fillCircles(const Pt* p, int n, float r, Color c) {
    if (n <= 0) return;
    setColor(c);
    for (int i = 0; i < n; ++i) {
        cairo_new_sub_path(cr_);
        cairo_arc(cr_, p[i].x, p[i].y, r, 0, 2 * M_PI);
    }
    cairo_fill(cr_);
}

void CairoCanvas::strokeCircles(const Pt* p, int n, float r, Color c, float w) {
    if (n <= 0) return;
    setColor(c);
    cairo_set_line_width(cr_, w);
    for (int i = 0; i < n; ++i) {
        cairo_new_sub_path(cr_);
        cairo_arc(cr_, p[i].x, p[i].y, r, 0, 2 * M_PI);
    }
    cairo_stroke(cr_);
}

void CairoCanvas::lensGrid(const Rect& a, float ox, float oy, float sp, float rad, Color c0, Color c1, Color c2) {
    cairo_save(cr_);
    cairo_rectangle(cr_, a.x, a.y, a.w, a.h);
    cairo_clip(cr_);
    const float x0 = ox + sp * 0.5f + std::floor((a.x - ox) / sp) * sp;
    const float y0 = oy + sp * 0.5f + std::floor((a.y - oy) / sp) * sp;
    for (float y = y0; y < a.b() + sp; y += sp)
        for (float x = x0; x < a.r() + sp; x += sp) {
            cairo_pattern_t* pat = cairo_pattern_create_radial(x - rad + 0.76f * rad, y - rad + 0.64f * rad, 0, x - rad + 0.76f * rad, y - rad + 0.64f * rad, 1.7f * rad);
            cairo_pattern_add_color_stop_rgba(pat, 0, c0.r, c0.g, c0.b, c0.a);
            cairo_pattern_add_color_stop_rgba(pat, 0.65, c1.r, c1.g, c1.b, c1.a);
            cairo_pattern_add_color_stop_rgba(pat, 1, c2.r, c2.g, c2.b, c2.a);
            cairo_set_source(cr_, pat);
            cairo_new_sub_path(cr_);
            cairo_arc(cr_, x, y, rad, 0, 2 * M_PI);
            cairo_fill(cr_);
            cairo_pattern_destroy(pat);
            cairo_set_source_rgba(cr_, 1, 1, 1, 0.3);
            cairo_new_sub_path(cr_);
            cairo_arc(cr_, x + rad * 0.38f, y + rad * 0.4f, 0.8, 0, 2 * M_PI);
            cairo_fill(cr_);
        }
    cairo_restore(cr_);
}

void CairoCanvas::dotGrid(const Rect& a, float ox, float oy, float sp, float rad, Color c) {
    cairo_save(cr_);
    cairo_rectangle(cr_, a.x, a.y, a.w, a.h);
    cairo_clip(cr_);
    setColor(c);
    const float x0 = ox + sp * 0.5f + std::floor((a.x - ox) / sp) * sp;
    const float y0 = oy + sp * 0.5f + std::floor((a.y - oy) / sp) * sp;
    for (float y = y0; y < a.b() + sp; y += sp)
        for (float x = x0; x < a.r() + sp; x += sp) {
            cairo_new_sub_path(cr_);
            cairo_arc(cr_, x, y, rad, 0, 2 * M_PI);
        }
    cairo_fill(cr_);
    cairo_restore(cr_);
}

void CairoCanvas::selectFont(const Font& f) {
    cairo_set_font_face(cr_, faces_[(size_t)f.face]);
    cairo_set_font_size(cr_, f.size);
    static cairo_font_options_t* opt = [] {
        cairo_font_options_t* o = cairo_font_options_create();
        cairo_font_options_set_antialias(o, CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_style(o, CAIRO_HINT_STYLE_SLIGHT);
        cairo_font_options_set_hint_metrics(o, CAIRO_HINT_METRICS_OFF);
        return o;
    }();
    cairo_set_font_options(cr_, opt);
}

void CairoCanvas::text(const std::string& s, float x, float baseline, const Font& f, Color c) {
    if (s.empty() || !faces_[(size_t)f.face]) return;
    selectFont(f);
    cairo_scaled_font_t* sf = cairo_get_scaled_font(cr_);
    cairo_glyph_t* glyphs = nullptr;
    int num = 0;
    if (cairo_scaled_font_text_to_glyphs(sf, x, baseline, s.c_str(), (int)s.size(), &glyphs, &num, nullptr, nullptr, nullptr) != CAIRO_STATUS_SUCCESS)
        return;
    for (int i = 0; i < num; ++i) glyphs[i].x += f.spacing * (float)i;
    setColor(c);
    cairo_show_glyphs(cr_, glyphs, num);
    cairo_glyph_free(glyphs);
}

float CairoCanvas::textWidth(const std::string& s, const Font& f) {
    if (s.empty() || !faces_[(size_t)f.face]) return 0.f;
    const std::string key = s + '\x01' + std::to_string((int)f.face) + ':' + std::to_string(f.size) + ':' + std::to_string(f.spacing);
    auto it = widthCache_.find(key);
    if (it != widthCache_.end()) return it->second;
    selectFont(f);
    cairo_scaled_font_t* sf = cairo_get_scaled_font(cr_);
    cairo_glyph_t* glyphs = nullptr;
    int num = 0;
    float w = 0.f;
    if (cairo_scaled_font_text_to_glyphs(sf, 0, 0, s.c_str(), (int)s.size(), &glyphs, &num, nullptr, nullptr, nullptr) == CAIRO_STATUS_SUCCESS) {
        cairo_text_extents_t ext;
        cairo_scaled_font_glyph_extents(sf, glyphs, num, &ext);
        w = (float)ext.x_advance + f.spacing * (float)num;
        cairo_glyph_free(glyphs);
    }
    if (widthCache_.size() > 4000) widthCache_.clear();
    widthCache_[key] = w;
    return w;
}

} // namespace raw2::ui
