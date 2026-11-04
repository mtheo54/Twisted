#include "ui/CGCanvas.h"
#include "ui/EmbeddedFonts.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace raw2::ui {

namespace {
uint64_t fontKey(Face f, float size) { return ((uint64_t)f << 32) | (uint64_t)(uint32_t)std::lround(size * 64.f); }

uint64_t hashColor(uint64_t h, Color c) {
    const float v[4] = {c.r, c.g, c.b, c.a};
    for (float x : v) {
        uint32_t u;
        std::memcpy(&u, &x, 4);
        h = (h ^ u) * 0x100000001B3ull;
    }
    return h;
}
} // namespace

CGCanvas::CGCanvas() {
    space_ = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    for (int i = 0; i < (int)Face::Count; ++i) {
        const embedded::File* f = fontFile((Face)i);
        if (!f) continue;
        CGDataProviderRef prov = CGDataProviderCreateWithData(nullptr, f->data, f->size, nullptr);  // données statiques
        if (!prov) continue;
        faces_[(size_t)i] = CGFontCreateWithDataProvider(prov);
        CGDataProviderRelease(prov);
    }
}

CGCanvas::~CGCanvas() {
    for (auto& kv : lines_) CFRelease(kv.second);
    for (auto& kv : fonts_) CFRelease(kv.second);
    for (auto& kv : lenses_) CGLayerRelease(kv.second);
    for (CGFontRef f : faces_)
        if (f) CGFontRelease(f);
    if (space_) CGColorSpaceRelease(space_);
}

void CGCanvas::frameDone() {
    // les couches de lentilles dépendent de la résolution : on les garde tant qu'elles servent,
    // mais on borne le cache (changement de zoom, d'écran Retina, de thème)
    if (lenses_.size() > 24) {
        for (auto& kv : lenses_) CGLayerRelease(kv.second);
        lenses_.clear();
    }
}

void CGCanvas::fill(Color c) { CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a); }
void CGCanvas::stroke(Color c, float w) {
    CGContextSetRGBStrokeColor(ctx_, c.r, c.g, c.b, c.a);
    CGContextSetLineWidth(ctx_, w);
}

void CGCanvas::save() { CGContextSaveGState(ctx_); }
void CGCanvas::restore() { CGContextRestoreGState(ctx_); }
void CGCanvas::translate(float dx, float dy) { CGContextTranslateCTM(ctx_, dx, dy); }
void CGCanvas::rotate(float a) { CGContextRotateCTM(ctx_, a); }
void CGCanvas::scale(float s) { CGContextScaleCTM(ctx_, s, s); }
void CGCanvas::clip(const Rect& r) { CGContextClipToRect(ctx_, CGRectMake(r.x, r.y, r.w, r.h)); }

void CGCanvas::fillRect(const Rect& r, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    fill(c);
    CGContextFillRect(ctx_, CGRectMake(r.x, r.y, r.w, r.h));
}

void CGCanvas::roundRectPath(const Rect& r, float rad) {
    const CGFloat k = std::max(0.f, std::min(rad, std::min(r.w, r.h) * 0.5f));
    CGMutablePathRef p = CGPathCreateMutable();
    if (k > 0.f) CGPathAddRoundedRect(p, nullptr, CGRectMake(r.x, r.y, r.w, r.h), k, k);
    else CGPathAddRect(p, nullptr, CGRectMake(r.x, r.y, r.w, r.h));
    CGContextAddPath(ctx_, p);
    CGPathRelease(p);
}

void CGCanvas::fillRoundRect(const Rect& r, float rad, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    fill(c);
    CGContextBeginPath(ctx_);
    roundRectPath(r, rad);
    CGContextFillPath(ctx_);
}

void CGCanvas::line(Pt a, Pt b, Color c, float w) {
    stroke(c, w);
    CGContextSetLineCap(ctx_, kCGLineCapButt);
    CGContextBeginPath(ctx_);
    CGContextMoveToPoint(ctx_, a.x, a.y);
    CGContextAddLineToPoint(ctx_, b.x, b.y);
    CGContextStrokePath(ctx_);
}

void CGCanvas::polyline(const Pt* p, int n, Color c, float w, bool closed) {
    if (n < 2) return;
    stroke(c, w);
    CGContextSetLineJoin(ctx_, kCGLineJoinRound);
    CGContextSetLineCap(ctx_, kCGLineCapButt);
    CGContextBeginPath(ctx_);
    CGContextMoveToPoint(ctx_, p[0].x, p[0].y);
    for (int i = 1; i < n; ++i) CGContextAddLineToPoint(ctx_, p[i].x, p[i].y);
    if (closed) CGContextClosePath(ctx_);
    CGContextStrokePath(ctx_);
}

void CGCanvas::dashedPolyline(const Pt* p, int n, Color c, float w, float on, float off) {
    if (n < 2) return;
    const CGFloat d[2] = {on, off};
    CGContextSetLineDash(ctx_, 0, d, 2);
    polyline(p, n, c, w, false);
    CGContextSetLineDash(ctx_, 0, nullptr, 0);
}

void CGCanvas::fillPolygon(const Pt* p, int n, Color c) {
    if (n < 3) return;
    fill(c);
    CGContextBeginPath(ctx_);
    CGContextMoveToPoint(ctx_, p[0].x, p[0].y);
    for (int i = 1; i < n; ++i) CGContextAddLineToPoint(ctx_, p[i].x, p[i].y);
    CGContextClosePath(ctx_);
    CGContextFillPath(ctx_);
}

void CGCanvas::fillEllipse(Pt ctr, float rx, float ry, Color c) {
    fill(c);
    CGContextFillEllipseInRect(ctx_, CGRectMake(ctr.x - rx, ctr.y - ry, 2 * rx, 2 * ry));
}

void CGCanvas::strokeEllipse(Pt ctr, float rx, float ry, Color c, float w) {
    stroke(c, w);
    CGContextStrokeEllipseInRect(ctx_, CGRectMake(ctr.x - rx, ctr.y - ry, 2 * rx, 2 * ry));
}

void CGCanvas::arc(Pt ctr, float r, float a0, float a1, Color c, float w) {
    stroke(c, w);
    CGContextSetLineCap(ctx_, kCGLineCapButt);
    CGContextBeginPath(ctx_);
    // angles croissants (y vers le bas : sens horaire à l'écran), comme cairo_arc
    CGContextAddArc(ctx_, ctr.x, ctr.y, r, a0, a1, 0);
    CGContextStrokePath(ctx_);
}

void CGCanvas::fillCircles(const Pt* p, int n, float r, Color c) {
    if (n <= 0) return;
    fill(c);
    CGContextBeginPath(ctx_);
    for (int i = 0; i < n; ++i) CGContextAddEllipseInRect(ctx_, CGRectMake(p[i].x - r, p[i].y - r, 2 * r, 2 * r));
    CGContextFillPath(ctx_);
}

void CGCanvas::strokeCircles(const Pt* p, int n, float r, Color c, float w) {
    if (n <= 0) return;
    stroke(c, w);
    CGContextBeginPath(ctx_);
    for (int i = 0; i < n; ++i) CGContextAddEllipseInRect(ctx_, CGRectMake(p[i].x - r, p[i].y - r, 2 * r, 2 * r));
    CGContextStrokePath(ctx_);
}

namespace {
CGGradientRef makeGradient(CGColorSpaceRef space, const GradStop* st, int n) {
    n = std::min(n, 8);
    CGFloat comps[32], locs[8];
    for (int i = 0; i < n; ++i) {
        comps[i * 4 + 0] = st[i].c.r;
        comps[i * 4 + 1] = st[i].c.g;
        comps[i * 4 + 2] = st[i].c.b;
        comps[i * 4 + 3] = st[i].c.a;
        locs[i] = std::clamp(st[i].pos, 0.f, 1.f);
    }
    return CGGradientCreateWithColorComponents(space, comps, locs, (size_t)n);
}
} // namespace

void CGCanvas::fillLinear(const Rect& r, float radius, Pt p0, Pt p1, const GradStop* st, int n) {
    if (n < 2 || r.w <= 0 || r.h <= 0) return;
    CGGradientRef g = makeGradient(space_, st, n);
    if (!g) return;
    CGContextSaveGState(ctx_);
    CGContextBeginPath(ctx_);
    roundRectPath(r, radius);
    CGContextClip(ctx_);
    CGContextDrawLinearGradient(ctx_, g, CGPointMake(p0.x, p0.y), CGPointMake(p1.x, p1.y),
                                kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    CGContextRestoreGState(ctx_);
    CGGradientRelease(g);
}

void CGCanvas::fillRadial(Pt c, float rx, float ry, Pt o, float gr, const GradStop* st, int n) {
    if (n < 2 || rx <= 0 || ry <= 0 || gr <= 0) return;
    CGGradientRef g = makeGradient(space_, st, n);
    if (!g) return;
    CGContextSaveGState(ctx_);
    CGContextBeginPath(ctx_);
    CGContextAddEllipseInRect(ctx_, CGRectMake(c.x - rx, c.y - ry, 2 * rx, 2 * ry));
    CGContextClip(ctx_);
    CGContextDrawRadialGradient(ctx_, g, CGPointMake(o.x, o.y), 0, CGPointMake(o.x, o.y), gr, kCGGradientDrawsAfterEndLocation);
    CGContextRestoreGState(ctx_);
    CGGradientRelease(g);
}

void CGCanvas::lensGrid(const Rect& a, float ox, float oy, float sp, float rad, Color c0, Color c1, Color c2) {
    // une lentille dessinée une fois dans une couche (à la résolution de l'écran), puis répétée
    const CGAffineTransform dev = CGContextGetUserSpaceToDeviceSpaceTransform(ctx_);
    const float pxScale = (float)std::sqrt(std::fabs(dev.a * dev.d - dev.b * dev.c));
    uint64_t key = 0xcbf29ce484222325ull;
    key = hashColor(key, c0);
    key = hashColor(key, c1);
    key = hashColor(key, c2);
    key = hashColor(key, Color{sp, rad, pxScale, 0.f});
    CGLayerRef& layer = lenses_[key];
    if (!layer) {
        layer = CGLayerCreateWithContext(ctx_, CGSizeMake(sp * pxScale, sp * pxScale), nullptr);
        if (!layer) return;
        CGContextRef lc = CGLayerGetContext(layer);
        CGContextScaleCTM(lc, pxScale, pxScale);
        const float cx = sp * 0.5f, cy = sp * 0.5f;
        const GradStop st[3] = {{0.f, c0}, {0.65f, c1}, {1.f, c2}};
        CGGradientRef g = makeGradient(space_, st, 3);
        if (g) {
            CGContextSaveGState(lc);
            CGContextAddEllipseInRect(lc, CGRectMake(cx - rad, cy - rad, 2 * rad, 2 * rad));
            CGContextClip(lc);
            const CGPoint o = CGPointMake(cx - rad + 0.76f * rad, cy - rad + 0.64f * rad);
            CGContextDrawRadialGradient(lc, g, o, 0, o, 1.7f * rad, kCGGradientDrawsAfterEndLocation);
            CGContextRestoreGState(lc);
            CGGradientRelease(g);
        }
        CGContextSetRGBFillColor(lc, 1, 1, 1, 0.3);
        CGContextFillEllipseInRect(lc, CGRectMake(cx + rad * 0.38f - 0.8f, cy + rad * 0.4f - 0.8f, 1.6f, 1.6f));
    }
    CGContextSaveGState(ctx_);
    CGContextClipToRect(ctx_, CGRectMake(a.x, a.y, a.w, a.h));
    const float x0 = ox + std::floor((a.x - ox) / sp) * sp;
    const float y0 = oy + std::floor((a.y - oy) / sp) * sp;
    for (float y = y0; y < a.b(); y += sp)
        for (float x = x0; x < a.r(); x += sp) CGContextDrawLayerInRect(ctx_, CGRectMake(x, y, sp, sp), layer);
    CGContextRestoreGState(ctx_);
}

void CGCanvas::dotGrid(const Rect& a, float ox, float oy, float sp, float rad, Color c) {
    CGContextSaveGState(ctx_);
    CGContextClipToRect(ctx_, CGRectMake(a.x, a.y, a.w, a.h));
    fill(c);
    const float x0 = ox + sp * 0.5f + std::floor((a.x - ox) / sp) * sp;
    const float y0 = oy + sp * 0.5f + std::floor((a.y - oy) / sp) * sp;
    CGContextBeginPath(ctx_);
    for (float y = y0; y < a.b() + sp; y += sp)
        for (float x = x0; x < a.r() + sp; x += sp) CGContextAddEllipseInRect(ctx_, CGRectMake(x - rad, y - rad, 2 * rad, 2 * rad));
    CGContextFillPath(ctx_);
    CGContextRestoreGState(ctx_);
}

// --- texte ----------------------------------------------------------------------------------------
CTFontRef CGCanvas::font(const Font& f) {
    const uint64_t k = fontKey(f.face, f.size);
    auto it = fonts_.find(k);
    if (it != fonts_.end()) return it->second;
    CGFontRef cg = faces_[(size_t)f.face];
    CTFontRef ct = cg ? CTFontCreateWithGraphicsFont(cg, f.size, nullptr, nullptr) : CTFontCreateUIFontForLanguage(kCTFontUIFontUser, f.size, nullptr);
    fonts_[k] = ct;
    return ct;
}

// Ligne CoreText (avec le repli automatique sur les polices du système pour les symboles absents)
// et l'espacement des lettres (kCTKern : ajouté après chaque caractère, comme letter-spacing CSS).
CTLineRef CGCanvas::lineFor(const std::string& s, const Font& f) {
    std::string key = s;
    key += '\x01';
    key += std::to_string((int)f.face) + ':' + std::to_string(f.size) + ':' + std::to_string(f.spacing);
    auto it = lines_.find(key);
    if (it != lines_.end()) return it->second;
    if (lines_.size() > 4000) {
        for (auto& kv : lines_) CFRelease(kv.second);
        lines_.clear();
    }
    CFStringRef str = CFStringCreateWithBytes(nullptr, (const UInt8*)s.data(), (CFIndex)s.size(), kCFStringEncodingUTF8, false);
    if (!str) return nullptr;
    const CGFloat kern = f.spacing;
    CFNumberRef kernNum = CFNumberCreate(nullptr, kCFNumberCGFloatType, &kern);
    const void* keys[3] = {kCTFontAttributeName, kCTKernAttributeName, kCTForegroundColorFromContextAttributeName};
    const void* vals[3] = {font(f), kernNum, kCFBooleanTrue};
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, keys, vals, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = as ? CTLineCreateWithAttributedString(as) : nullptr;
    if (as) CFRelease(as);
    CFRelease(attrs);
    CFRelease(kernNum);
    CFRelease(str);
    if (line) lines_[key] = line;
    return line;
}

void CGCanvas::text(const std::string& s, float x, float baseline, const Font& f, Color c) {
    if (s.empty()) return;
    CTLineRef line = lineFor(s, f);
    if (!line) return;
    CGContextSaveGState(ctx_);
    CGContextTranslateCTM(ctx_, x, baseline);
    CGContextScaleCTM(ctx_, 1, -1);  // CoreText dessine y vers le haut
    CGContextSetTextMatrix(ctx_, CGAffineTransformIdentity);
    CGContextSetTextPosition(ctx_, 0, 0);
    fill(c);
    CTLineDraw(line, ctx_);
    CGContextRestoreGState(ctx_);
}

float CGCanvas::textWidth(const std::string& s, const Font& f) {
    if (s.empty()) return 0.f;
    CTLineRef line = lineFor(s, f);
    return line ? (float)CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr) : 0.f;
}

} // namespace raw2::ui
