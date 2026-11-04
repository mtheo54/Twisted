#include "ui/D2DCanvas.h"
#include "ui/EmbeddedFonts.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_3.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw2::ui {

namespace {

template <class T>
void safeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

D2D1_COLOR_F col(Color c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

const wchar_t* familyFor(Face f, bool custom) {
    if (!custom) return L"Consolas";
    return f == Face::Pixel ? L"Silkscreen" : L"JetBrains Mono";
}

DWRITE_FONT_WEIGHT weightFor(Face f) {
    switch (f) {
        case Face::MonoMedium: return DWRITE_FONT_WEIGHT_MEDIUM;
        case Face::MonoBold: return DWRITE_FONT_WEIGHT_BOLD;
        default: return DWRITE_FONT_WEIGHT_NORMAL;
    }
}

// Chargement des polices embarquées : IDWriteFactory5 + chargeur en mémoire (Windows 10 1703+).
IDWriteFontCollection* loadEmbeddedFonts(IDWriteFactory* dw) {
    IDWriteFactory5* f5 = nullptr;
    if (FAILED(dw->QueryInterface(__uuidof(IDWriteFactory5), (void**)&f5)) || !f5) return nullptr;
    IDWriteInMemoryFontFileLoader* loader = nullptr;
    IDWriteFontSetBuilder1* builder = nullptr;
    IDWriteFontSet* set = nullptr;
    IDWriteFontCollection1* coll = nullptr;
    bool ok = SUCCEEDED(f5->CreateInMemoryFontFileLoader(&loader)) && SUCCEEDED(f5->RegisterFontFileLoader(loader)) &&
              SUCCEEDED(f5->CreateFontSetBuilder(&builder));
    if (ok) {
        for (int i = 0; i < (int)Face::Count; ++i) {
            const embedded::File* file = fontFile((Face)i);
            if (!file) continue;
            IDWriteFontFile* ff = nullptr;
            if (SUCCEEDED(loader->CreateInMemoryFontFileReference(f5, file->data, (UINT32)file->size, nullptr, &ff)) && ff) {
                builder->AddFontFile(ff);
                ff->Release();
            }
        }
        ok = SUCCEEDED(builder->CreateFontSet(&set)) && SUCCEEDED(f5->CreateFontCollectionFromFontSet(set, &coll));
    }
    safeRelease(set);
    safeRelease(builder);
    // le chargeur reste enregistré (les fichiers en mémoire en dépendent) : il vit avec la DLL
    f5->Release();
    if (!ok) {
        safeRelease(coll);
        return nullptr;
    }
    return coll;
}

} // namespace

D2DShared& D2DShared::get() {
    static D2DShared s = [] {
        D2DShared r;
        D2D1_FACTORY_OPTIONS opt{};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &opt, (void**)&r.d2d))) return r;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&r.dwrite))) return r;
        r.fonts = loadEmbeddedFonts(r.dwrite);
        r.ok = true;
        return r;
    }();
    return s;
}

D2DCanvas::D2DCanvas() = default;

D2DCanvas::~D2DCanvas() {
    releaseDeviceResources();
    for (auto& l : layouts_) safeRelease(l.second.layout);
    for (auto& f : formats_) safeRelease(f.second);
    for (auto& d : dashes_) safeRelease(d.second);
    safeRelease(roundJoin_);
}

void D2DCanvas::releaseDeviceResources() {
    safeRelease(brush_);
    for (auto& d : dots_) safeRelease(d.second);
    dots_.clear();
    for (auto& g : linear_) safeRelease(g.second);
    linear_.clear();
    for (auto& g : radial_) safeRelease(g.second);
    radial_.clear();
    for (auto& g : lens_) safeRelease(g.second);
    lens_.clear();
}

namespace {
uint64_t stopsHash(const GradStop* s, int n) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](float v) { h = (h ^ (uint64_t)std::lround(v * 4096.f)) * 1099511628211ull; };
    for (int i = 0; i < n; ++i) { mix(s[i].pos); mix(s[i].c.r); mix(s[i].c.g); mix(s[i].c.b); mix(s[i].c.a); }
    return h;
}
} // namespace

void D2DCanvas::fillLinear(const Rect& r, float radius, Pt p0, Pt p1, const GradStop* s, int n) {
    if (n < 2 || r.w <= 0 || r.h <= 0) return;
    const uint64_t key = stopsHash(s, n);
    ID2D1LinearGradientBrush*& b = linear_[key];
    if (!b) {
        D2D1_GRADIENT_STOP st[8];
        n = std::min(n, 8);
        for (int i = 0; i < n; ++i) st[i] = D2D1::GradientStop(s[i].pos, col(s[i].c));
        ID2D1GradientStopCollection* coll = nullptr;
        if (FAILED(rt_->CreateGradientStopCollection(st, (UINT32)n, &coll)) || !coll) return;
        rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(p0.x, p0.y), D2D1::Point2F(p1.x, p1.y)), coll, &b);
        coll->Release();
        if (!b) return;
    }
    b->SetStartPoint(D2D1::Point2F(p0.x, p0.y));
    b->SetEndPoint(D2D1::Point2F(p1.x, p1.y));
    if (radius > 0.f) rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.x, r.y, r.r(), r.b()), radius, radius), b);
    else rt_->FillRectangle(D2D1::RectF(r.x, r.y, r.r(), r.b()), b);
}

void D2DCanvas::fillRadial(Pt c, float rx, float ry, Pt o, float gr, const GradStop* s, int n) {
    if (n < 2 || rx <= 0 || ry <= 0 || gr <= 0) return;
    const uint64_t key = stopsHash(s, n);
    ID2D1RadialGradientBrush*& b = radial_[key];
    if (!b) {
        D2D1_GRADIENT_STOP st[8];
        n = std::min(n, 8);
        for (int i = 0; i < n; ++i) st[i] = D2D1::GradientStop(s[i].pos, col(s[i].c));
        ID2D1GradientStopCollection* coll = nullptr;
        if (FAILED(rt_->CreateGradientStopCollection(st, (UINT32)n, &coll)) || !coll) return;
        rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(o.x, o.y), D2D1::Point2F(0, 0), gr, gr), coll, &b);
        coll->Release();
        if (!b) return;
    }
    b->SetCenter(D2D1::Point2F(o.x, o.y));
    b->SetGradientOriginOffset(D2D1::Point2F(0, 0));
    b->SetRadiusX(gr);
    b->SetRadiusY(gr);
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y), rx, ry), b);
}

void D2DCanvas::begin(ID2D1RenderTarget* rt, float scale) {
    if (rt != rt_) releaseDeviceResources();
    rt_ = rt;
    scale_ = scale;
    ++frame_;
    if (!brush_) rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush_);
    if (!roundJoin_) {
        D2D1_STROKE_STYLE_PROPERTIES p = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND);
        D2DShared::get().d2d->CreateStrokeStyle(p, nullptr, 0, &roundJoin_);
    }
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    stack_.clear();
    clips_ = 0;
    cur_ = Xf{scale, 0, 0, scale, 0, 0};
    applyTransform();
}

void D2DCanvas::end() {
    while (clips_ > 0) {
        rt_->PopAxisAlignedClip();
        --clips_;
    }
    // libère les mises en page de texte inutilisées depuis 2 s
    for (auto it = layouts_.begin(); it != layouts_.end();) {
        if (frame_ - it->second.used > 120) {
            safeRelease(it->second.layout);
            it = layouts_.erase(it);
        } else {
            ++it;
        }
    }
}

void D2DCanvas::applyTransform() {
    rt_->SetTransform(D2D1::Matrix3x2F(cur_.m11, cur_.m12, cur_.m21, cur_.m22, cur_.dx, cur_.dy));
}

void D2DCanvas::save() { stack_.push_back({cur_, clips_}); }

void D2DCanvas::restore() {
    if (stack_.empty()) return;
    const Saved s = stack_.back();
    stack_.pop_back();
    while (clips_ > s.clips) {
        rt_->PopAxisAlignedClip();
        --clips_;
    }
    cur_ = s.xf;
    applyTransform();
}

void D2DCanvas::translate(float dx, float dy) {
    cur_.dx += dx * cur_.m11 + dy * cur_.m21;
    cur_.dy += dx * cur_.m12 + dy * cur_.m22;
    applyTransform();
}

void D2DCanvas::rotate(float a) {
    const float c = std::cos(a), s = std::sin(a);
    const Xf o = cur_;
    cur_.m11 = c * o.m11 + s * o.m21;
    cur_.m12 = c * o.m12 + s * o.m22;
    cur_.m21 = -s * o.m11 + c * o.m21;
    cur_.m22 = -s * o.m12 + c * o.m22;
    applyTransform();
}

void D2DCanvas::scale(float s) {
    cur_.m11 *= s; cur_.m12 *= s; cur_.m21 *= s; cur_.m22 *= s;
    applyTransform();
}

void D2DCanvas::clip(const Rect& r) {
    rt_->PushAxisAlignedClip(D2D1::RectF(r.x, r.y, r.r(), r.b()), D2D1_ANTIALIAS_MODE_ALIASED);
    ++clips_;
}

void D2DCanvas::setColor(Color c) { brush_->SetColor(col(c)); }

void D2DCanvas::fillRect(const Rect& r, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    setColor(c);
    rt_->FillRectangle(D2D1::RectF(r.x, r.y, r.r(), r.b()), brush_);
}

void D2DCanvas::fillRoundRect(const Rect& r, float rad, Color c) {
    setColor(c);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.x, r.y, r.r(), r.b()), rad, rad), brush_);
}

void D2DCanvas::line(Pt a, Pt b, Color c, float w) {
    setColor(c);
    rt_->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), brush_, w);
}

void D2DCanvas::strokePath(const Pt* p, int n, Color c, float w, bool closed, ID2D1StrokeStyle* style) {
    if (n < 2) return;
    ID2D1PathGeometry* g = nullptr;
    if (FAILED(D2DShared::get().d2d->CreatePathGeometry(&g)) || !g) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(g->Open(&sink)) && sink) {
        sink->BeginFigure(D2D1::Point2F(p[0].x, p[0].y), D2D1_FIGURE_BEGIN_HOLLOW);
        for (int i = 1; i < n; ++i) sink->AddLine(D2D1::Point2F(p[i].x, p[i].y));
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        sink->Close();
        sink->Release();
        setColor(c);
        rt_->DrawGeometry(g, brush_, w, style);
    }
    g->Release();
}

void D2DCanvas::polyline(const Pt* p, int n, Color c, float w, bool closed) { strokePath(p, n, c, w, closed, roundJoin_); }

void D2DCanvas::dashedPolyline(const Pt* p, int n, Color c, float w, float on, float off) {
    if (n < 2 || w <= 0.f) return;
    // tirets relatifs à l'épaisseur (convention Direct2D)
    const float dashes[2] = {on / w, off / w};
    ID2D1StrokeStyle* s = nullptr;
    D2D1_STROKE_STYLE_PROPERTIES pr = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND, 10.f, D2D1_DASH_STYLE_CUSTOM, 0.f);
    const auto key = std::make_pair((int)std::lround(dashes[0] * 1000.f), (int)std::lround(dashes[1] * 1000.f));
    auto it = dashes_.find(key);
    if (it != dashes_.end() && it->second) s = it->second;
    else if (SUCCEEDED(D2DShared::get().d2d->CreateStrokeStyle(pr, dashes, 2, &s))) dashes_[key] = s;
    strokePath(p, n, c, w, false, s);
}

void D2DCanvas::fillPolygon(const Pt* p, int n, Color c) {
    if (n < 3) return;
    ID2D1PathGeometry* g = nullptr;
    if (FAILED(D2DShared::get().d2d->CreatePathGeometry(&g)) || !g) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(g->Open(&sink)) && sink) {
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        sink->BeginFigure(D2D1::Point2F(p[0].x, p[0].y), D2D1_FIGURE_BEGIN_FILLED);
        for (int i = 1; i < n; ++i) sink->AddLine(D2D1::Point2F(p[i].x, p[i].y));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
        setColor(c);
        rt_->FillGeometry(g, brush_);
    }
    g->Release();
}

void D2DCanvas::fillEllipse(Pt ctr, float rx, float ry, Color c) {
    setColor(c);
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ctr.x, ctr.y), rx, ry), brush_);
}

void D2DCanvas::strokeEllipse(Pt ctr, float rx, float ry, Color c, float w) {
    setColor(c);
    rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ctr.x, ctr.y), rx, ry), brush_, w);
}

void D2DCanvas::arc(Pt ctr, float r, float a0, float a1, Color c, float w) {
    if (a1 <= a0) return;
    ID2D1PathGeometry* g = nullptr;
    if (FAILED(D2DShared::get().d2d->CreatePathGeometry(&g)) || !g) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(g->Open(&sink)) && sink) {
        sink->BeginFigure(D2D1::Point2F(ctr.x + r * std::cos(a0), ctr.y + r * std::sin(a0)), D2D1_FIGURE_BEGIN_HOLLOW);
        // découpé en arcs de moins de 180° pour rester sans ambiguïté
        const int parts = (int)std::ceil((a1 - a0) / 3.0f);
        for (int k = 1; k <= parts; ++k) {
            const float a = a0 + (a1 - a0) * (float)k / (float)parts;
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(ctr.x + r * std::cos(a), ctr.y + r * std::sin(a)), D2D1::SizeF(r, r), 0.f,
                                          D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        sink->Release();
        setColor(c);
        rt_->DrawGeometry(g, brush_, w);
    }
    g->Release();
}

void D2DCanvas::fillCircles(const Pt* p, int n, float r, Color c) {
    if (n <= 0) return;
    setColor(c);
    for (int i = 0; i < n; ++i) rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(p[i].x, p[i].y), r, r), brush_);
}

void D2DCanvas::strokeCircles(const Pt* p, int n, float r, Color c, float w) {
    if (n <= 0) return;
    setColor(c);
    for (int i = 0; i < n; ++i) rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(p[i].x, p[i].y), r, r), brush_, w);
}

void D2DCanvas::dotGrid(const Rect& a, float ox, float oy, float sp, float rad, Color c) {
    const int rgba = ((int)(c.r * 255.f) << 24) | ((int)(c.g * 255.f) << 16) | ((int)(c.b * 255.f) << 8) | (int)(c.a * 255.f);
    const float pxScale = scale_ * std::sqrt(std::fabs(cur_.m11 * cur_.m22 - cur_.m12 * cur_.m21)) / scale_;
    const DotKey key{(int)std::lround(sp * 10.f), (int)std::lround(rad * 100.f), rgba, (int)std::lround(pxScale * 100.f)};
    ID2D1BitmapBrush* bb = nullptr;
    auto it = dots_.find(key);
    if (it != dots_.end()) bb = it->second;
    else {
        ID2D1BitmapRenderTarget* tile = nullptr;
        const UINT32 px = (UINT32)std::max(1L, std::lround(sp * pxScale));
        if (SUCCEEDED(rt_->CreateCompatibleRenderTarget(D2D1::SizeF(sp, sp), D2D1::SizeU(px, px), &tile)) && tile) {
            ID2D1SolidColorBrush* b = nullptr;
            tile->BeginDraw();
            tile->Clear(D2D1::ColorF(0, 0, 0, 0));
            if (SUCCEEDED(tile->CreateSolidColorBrush(col(c), &b)) && b) {
                tile->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sp * 0.5f, sp * 0.5f), rad, rad), b);
                b->Release();
            }
            tile->EndDraw();
            ID2D1Bitmap* bmp = nullptr;
            if (SUCCEEDED(tile->GetBitmap(&bmp)) && bmp) {
                rt_->CreateBitmapBrush(bmp, D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR), &bb);
                bmp->Release();
            }
            tile->Release();
        }
        dots_[key] = bb;
    }
    if (!bb) return;
    bb->SetTransform(D2D1::Matrix3x2F::Translation(ox, oy));
    rt_->FillRectangle(D2D1::RectF(a.x, a.y, a.r(), a.b()), bb);
}

void D2DCanvas::lensGrid(const Rect& a, float ox, float oy, float sp, float rad, Color c0, Color c1, Color c2) {
    const float pxScale = std::sqrt(std::fabs(cur_.m11 * cur_.m22 - cur_.m12 * cur_.m21));
    const GradStop key3[4] = {{sp, c0}, {rad, c1}, {pxScale, c2}, {0.f, c0}};
    const uint64_t key = stopsHash(key3, 3);
    ID2D1BitmapBrush*& bb = lens_[key];
    if (!bb) {
        ID2D1BitmapRenderTarget* tile = nullptr;
        const UINT32 px = (UINT32)std::max(1L, std::lround(sp * pxScale));
        if (SUCCEEDED(rt_->CreateCompatibleRenderTarget(D2D1::SizeF(sp, sp), D2D1::SizeU(px, px), &tile)) && tile) {
            tile->BeginDraw();
            tile->Clear(D2D1::ColorF(0, 0, 0, 0));
            const D2D1_GRADIENT_STOP st[3] = {D2D1::GradientStop(0.f, col(c0)), D2D1::GradientStop(0.65f, col(c1)), D2D1::GradientStop(1.f, col(c2))};
            ID2D1GradientStopCollection* coll = nullptr;
            ID2D1RadialGradientBrush* rb = nullptr;
            const float cx = sp * 0.5f, cy = sp * 0.5f;
            if (SUCCEEDED(tile->CreateGradientStopCollection(st, 3, &coll)) && coll) {
                const D2D1_POINT_2F o = D2D1::Point2F(cx - rad + 0.76f * rad, cy - rad + 0.64f * rad);
                tile->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(o, D2D1::Point2F(0, 0), 1.7f * rad, 1.7f * rad), coll, &rb);
                coll->Release();
            }
            if (rb) {
                tile->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), rb);
                rb->Release();
            }
            ID2D1SolidColorBrush* hb = nullptr;
            if (SUCCEEDED(tile->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.3f), &hb)) && hb) {
                tile->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + rad * 0.38f, cy + rad * 0.4f), 0.8f, 0.8f), hb);
                hb->Release();
            }
            tile->EndDraw();
            ID2D1Bitmap* bmp = nullptr;
            if (SUCCEEDED(tile->GetBitmap(&bmp)) && bmp) {
                rt_->CreateBitmapBrush(bmp, D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR), &bb);
                bmp->Release();
            }
            tile->Release();
        }
    }
    if (!bb) return;
    bb->SetTransform(D2D1::Matrix3x2F::Translation(ox, oy));
    rt_->FillRectangle(D2D1::RectF(a.x, a.y, a.r(), a.b()), bb);
}

IDWriteTextFormat* D2DCanvas::formatFor(const Font& f) {
    const auto key = std::make_pair((int)f.face, (int)std::lround(f.size * 100.f));
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    D2DShared& sh = D2DShared::get();
    IDWriteTextFormat* fmt = nullptr;
    sh.dwrite->CreateTextFormat(familyFor(f.face, sh.fonts != nullptr), sh.fonts, weightFor(f.face), DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                f.size, L"fr-fr", &fmt);
    if (fmt) {
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    formats_[key] = fmt;
    return fmt;
}

D2DCanvas::Layout* D2DCanvas::layoutFor(const std::string& s, const Font& f) {
    std::string key = s;
    key += '\x01';
    key += std::to_string((int)f.face) + ':' + std::to_string((int)std::lround(f.size * 100.f)) + ':' + std::to_string((int)std::lround(f.spacing * 100.f));
    auto it = layouts_.find(key);
    if (it != layouts_.end()) {
        it->second.used = frame_;
        return &it->second;
    }
    IDWriteTextFormat* fmt = formatFor(f);
    if (!fmt) return nullptr;
    const std::wstring w = widen(s);
    Layout L;
    if (FAILED(D2DShared::get().dwrite->CreateTextLayout(w.c_str(), (UINT32)w.size(), fmt, 100000.f, 1000.f, &L.layout)) || !L.layout) return nullptr;
    if (f.spacing != 0.f) {
        IDWriteTextLayout1* l1 = nullptr;
        if (SUCCEEDED(L.layout->QueryInterface(__uuidof(IDWriteTextLayout1), (void**)&l1)) && l1) {
            l1->SetCharacterSpacing(0.f, f.spacing, 0.f, DWRITE_TEXT_RANGE{0, (UINT32)w.size()});
            l1->Release();
        }
    }
    DWRITE_TEXT_METRICS tm{};
    L.layout->GetMetrics(&tm);
    L.width = tm.widthIncludingTrailingWhitespace;
    DWRITE_LINE_METRICS lm{};
    UINT32 count = 0;
    L.layout->GetLineMetrics(&lm, 1, &count);
    L.baseline = lm.baseline;
    L.used = frame_;
    if (layouts_.size() > 3000) {  // garde-fou
        for (auto& e : layouts_) safeRelease(e.second.layout);
        layouts_.clear();
    }
    return &(layouts_[key] = L);
}

void D2DCanvas::text(const std::string& s, float x, float baseline, const Font& f, Color c) {
    if (s.empty()) return;
    Layout* L = layoutFor(s, f);
    if (!L || !L->layout) return;
    setColor(c);
    rt_->DrawTextLayout(D2D1::Point2F(x, baseline - L->baseline), L->layout, brush_, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

float D2DCanvas::textWidth(const std::string& s, const Font& f) {
    if (s.empty()) return 0.f;
    Layout* L = layoutFor(s, f);
    return L ? L->width : 0.f;
}

} // namespace raw2::ui
