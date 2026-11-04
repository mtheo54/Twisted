#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace raw2::ui {

namespace {
constexpr float kPi = 3.14159265358979f;

// Décodage UTF-8 minimal
uint32_t nextCp(const std::string& s, size_t& i) {
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : 1);
    uint32_t cp = c & (0x3F >> extra);
    while (extra-- > 0 && i < s.size()) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}
void putCp(std::string& o, uint32_t c) {
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
}
size_t prevCharStart(const std::string& s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
    return i;
}
size_t nextCharStart(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) ++i;
    return i;
}
} // namespace

std::string fmt(const char* f, ...) {
    char b[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

std::string upper(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        uint32_t c = nextCp(s, i);
        if (c >= 'a' && c <= 'z') c -= 32;
        else if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) c -= 32;  // à..þ -> À..Þ
        else if (c == 0x153) c = 0x152;                          // œ -> Œ
        else if (c == 0xFF) c = 0x178;
        putCp(o, c);
    }
    return o;
}

std::string foldForSearch(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        uint32_t c = nextCp(s, i);
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c >= 0xC0 && c <= 0xFF) {
            static const char* map = "aaaaaaaceeeeiiiidnooooo/ouuuuyps"  // C0..DF
                                     "aaaaaaaceeeeiiiidnooooo/ouuuuypy";  // E0..FF
            c = (uint32_t)map[c - 0xC0];
        } else if (c == 0x152 || c == 0x153) {
            o += "oe";
            continue;
        }
        putCp(o, c);
    }
    return o;
}

Id makeId(const char* s, int i) {
    uint64_t h = 1469598103934665603ull;
    for (; *s; ++s) h = (h ^ (unsigned char)*s) * 1099511628211ull;
    h = (h ^ (uint64_t)(uint32_t)i) * 1099511628211ull;
    return h ? h : 1;
}

void Ui::beginFrame(Canvas& c, const Theme& t, const Input& in) {
    c_ = &c;
    t_ = &t;
    in_ = in;
    layer_ = 0;
    cursor_ = Cursor::Arrow;
    activeSeen_ = false;
}

void Ui::endFrame() {
    if (in_.released || (!in_.down && !activeSeen_)) active_ = 0;
    blockers_.clear();
}

bool Ui::hit(const Rect& r) const {
    if (!r.contains(in_.mouse)) return false;
    for (const Blocker& b : blockers_)
        if (b.layer > layer_ && b.r.contains(in_.mouse)) return false;
    return true;
}

Ui::Interact Ui::interact(Id id, const Rect& r, Cursor cur) {
    Interact it;
    it.hovered = hit(r);
    if (it.hovered && in_.pressed && active_ == 0) {
        active_ = id;
        it.pressed = true;
    }
    if (active_ == id) {
        activeSeen_ = true;
        it.held = in_.down || it.pressed;
        if (in_.released) {
            it.released = true;
            it.clicked = r.contains(in_.mouse);
        }
    }
    if (it.hovered || active_ == id) cursor_ = cur;
    return it;
}

// --- dessin --------------------------------------------------------------------------------
void Ui::border(const Rect& r, Color col, float w) {
    c_->fillRect(Rect{r.x, r.y, r.w, w}, col);
    c_->fillRect(Rect{r.x, r.b() - w, r.w, w}, col);
    c_->fillRect(Rect{r.x, r.y + w, w, r.h - 2 * w}, col);
    c_->fillRect(Rect{r.r() - w, r.y + w, w, r.h - 2 * w}, col);
}

// Bordure 1 px à coins « arrondis 2 px » : le pixel d'angle est laissé à demi-teinte.
static void roundedBorder(Canvas& c, const Rect& r, Color col) {
    c.fillRect(Rect{r.x + 1.f, r.y, r.w - 2.f, 1.f}, col);
    c.fillRect(Rect{r.x + 1.f, r.b() - 1.f, r.w - 2.f, 1.f}, col);
    c.fillRect(Rect{r.x, r.y + 1.f, 1.f, r.h - 2.f}, col);
    c.fillRect(Rect{r.r() - 1.f, r.y + 1.f, 1.f, r.h - 2.f}, col);
    const Color half = col.withAlpha(0.45f);
    c.fillRect(Rect{r.x, r.y, 1.f, 1.f}, half);
    c.fillRect(Rect{r.r() - 1.f, r.y, 1.f, 1.f}, half);
    c.fillRect(Rect{r.x, r.b() - 1.f, 1.f, 1.f}, half);
    c.fillRect(Rect{r.r() - 1.f, r.b() - 1.f, 1.f, 1.f}, half);
}

void Ui::dashedBorder(const Rect& r, Color col, float dash, float gap) {
    auto hline = [&](float y) {
        for (float x = r.x; x < r.r(); x += dash + gap) c_->fillRect(Rect{x, y, std::min(dash, r.r() - x), 1.f}, col);
    };
    auto vline = [&](float x) {
        for (float y = r.y; y < r.b(); y += dash + gap) c_->fillRect(Rect{x, y, 1.f, std::min(dash, r.b() - y)}, col);
    };
    hline(r.y);
    hline(r.b() - 1.f);
    vline(r.x);
    vline(r.r() - 1.f);
}

void Ui::box(const Rect& r, Color fill, Color line, float radius) {
    if (fill.a > 0.f) {
        if (line.a > 0.f) c_->fillRect(r.inset(1.f), fill);
        else c_->fillRoundRect(r, radius, fill);
    }
    if (line.a > 0.f) {
        if (radius > 0.f) roundedBorder(*c_, r, line);
        else border(r, line);
    }
}

float Ui::text(const std::string& s, float x, float top, const Font& f, Color col, float lh) {
    c_->text(s, x, baselineIn(f, top, lh), f, col);
    return c_->textWidth(s, f);
}

void Ui::textCenter(const std::string& s, const Rect& r, const Font& f, Color col) {
    const float w = c_->textWidth(s, f);
    const float lh = normalLineHeight(f);
    c_->text(s, r.cx() - w * 0.5f, baselineIn(f, r.cy() - lh * 0.5f), f, col);
}

void Ui::textRight(const std::string& s, float right, float top, const Font& f, Color col) {
    const float w = c_->textWidth(s, f);
    c_->text(s, right - w, baselineIn(f, top), f, col);
}

std::string Ui::fit(const std::string& s, const Font& f, float maxW) {
    if (c_->textWidth(s, f) <= maxW) return s;
    std::string t = s;
    const std::string ell = "\xE2\x80\xA6";
    while (!t.empty()) {
        t.erase(prevCharStart(t, t.size()));
        if (c_->textWidth(t + ell, f) <= maxW) break;
    }
    return t + ell;
}

void Ui::crosses(const Rect& r, Color col) {
    const Font f{Face::Mono, 13.f, 0.f};
    const float w = c_->textWidth("+", f);
    text("+", r.x + 7.f, r.y + 4.f, f, col, 13.f);
    text("+", r.r() - 7.f - w, r.b() - 4.f - 13.f, f, col, 13.f);
}

void Ui::card(const Rect& r) {
    box(r, t_->card, t_->line);
    screws(r, false);
}

void Ui::cardTerm(const Rect& r) {
    box(r, t_->scr, t_->scrLine);
    screws(r, true);
}

void Ui::cardHeader(const Rect& r, const std::string& title, bool term, float padX, float padTop) {
    const float x = r.x + 1.f + padX, y = r.y + 1.f + padTop, w = r.w - 2.f - 2.f * padX;
    text(upper(title), x, y + 2.5f, Font{Face::Pixel, 16.f, 1.28f}, term ? t_->scrText : t_->ink, 16.f);
    c_->fillRect(Rect{x, y + 31.f, w, 1.f}, term ? t_->scrRule : t_->rule);
}

float Ui::tag(const std::string& label, float right, float top, Tag kind, bool) {
    const Font f{Face::Mono, 10.f, 1.f};
    const std::string s = upper(label);
    const float w = std::round(c_->textWidth(s, f) + 18.f);
    const Rect r{right - w, top, w, 21.f};
    Color line = t_->rule, fg = t_->ink;
    if (kind == Tag::Dark) {
        box(r, t_->tabBg, t_->tabBg);
        line = t_->tabBg;
        fg = t_->tabFg;
    } else {
        if (kind == Tag::Accent) line = fg = t_->tagAccent();
        box(r, Color{0, 0, 0, 0}, line);
    }
    text(s, r.x + 9.f, top + 4.f, f, fg);
    return w;
}

void Ui::screen(const Rect& r, float sp) {
    box(r, t_->scr, t_->scrLine);
    c_->dotGrid(r.inset(1.f), r.x + 1.f, r.y + 1.f, sp, 1.1f, t_->scrGrid);
}

// --- boutons ---------------------------------------------------------------------------------
ButtonStyle Ui::keyStyle() const {
    ButtonStyle s;
    s.bg = t_->btnBg;
    s.fg = t_->ink;
    s.border = t_->btnEdge;
    s.font = Font{Face::Mono, 12.f, 1.2f};
    return s;
}

ButtonStyle Ui::miniStyle(bool on) const {
    ButtonStyle s;
    s.bg = on ? t_->tabBg : t_->btnBg;
    s.fg = on ? t_->tabFg : t_->ink;
    s.border = t_->btnEdge;
    s.font = Font{Face::Mono, 11.f, 0.88f};
    s.pressed = on;
    return s;
}

ButtonStyle Ui::accentStyle() const {
    ButtonStyle s = keyStyle();
    s.bg = t_->acc;
    s.fg = t_->onAcc;
    s.font = Font{Face::MonoBold, 12.f, 1.2f};
    return s;
}

void Ui::drawButton(const Rect& r0, const std::string& label, const ButtonStyle& st, bool pressed) {
    Rect r = r0;
    if (pressed) r = r.moved(2.f, 2.f);
    else if (st.shadow) c_->fillRect(r.moved(st.shadowSize, st.shadowSize), st.border);
    if (st.dashed) {
        if (st.bg.a > 0.f) c_->fillRect(r, st.bg);
        dashedBorder(r, st.border);
    } else {
        box(r, st.bg, st.border);
    }
    if (!label.empty()) textCenter(label, r, st.font, st.fg);
}

bool Ui::button(Id id, const Rect& r, const std::string& label, const ButtonStyle& st, Cursor cur) {
    const Interact it = interact(id, r, cur);
    drawButton(r, label, st, st.pressed || it.held);
    return it.clicked;
}

// --- potards ---------------------------------------------------------------------------------
// Bouton moleté (maquette mise à jour) : couronne de graduations, disque métallique, molette
// striée sur le bord, capuchon concentrique et repère coloré qui tourne de -135° à +135°.
void Ui::drawKnob(Pt p, float size, float f, float mod, bool) {
    const float s = size / 84.f;
    const Pt c{p.x + 42.f * s, p.y + 42.f * s};
    const int N = 21;
    for (int i = 0; i < N; ++i) {
        const float fr = (float)i / (N - 1);
        const float a = (135.f + 270.f * fr) * kPi / 180.f;
        const bool end = i == 0 || i == N - 1;
        const float r1 = 33.f, r2 = end ? 41.f : ((i % 5 == 0) ? 40.f : 37.f);
        const bool on = fr <= f + 0.001f;
        c_->line(Pt{c.x + r1 * s * std::cos(a), c.y + r1 * s * std::sin(a)}, Pt{c.x + r2 * s * std::cos(a), c.y + r2 * s * std::sin(a)},
                 on ? t_->ink : t_->tickOff, (on ? 1.6f : 1.f) * s);
    }
    if (mod > 0.004f) {
        const float f1 = std::max(0.f, f - mod), f2 = std::min(1.f, f + mod);
        c_->arc(c, 30.f * s, (135.f + 270.f * f1) * kPi / 180.f, (135.f + 270.f * f2) * kPi / 180.f, t_->ptr, 2.5f * s);
    }
    drawKnobCap(c, size * 0.32f, f);
}

void Ui::drawKnobCap(Pt c, float R, float f) {
    const Color KF = t_->knobFace;
    const Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
    const Color kHi = KF.mix(white, 0.28f), kLo = KF.mix(black, 0.16f), kMd = KF.mix(white, 0.08f);
    // ombre portée douce, puis disque à reflets (dégradé conique approché par un dégradé linéaire)
    c_->fillEllipse(Pt{c.x, c.y + 2.f}, R + 1.5f, R + 1.5f, Color{0, 0, 0, 0.13f});
    c_->fillEllipse(c, R + 1.f, R + 1.f, t_->knobLine);
    const float a = 15.f * kPi / 180.f;
    const Pt d{std::sin(a) * R, -std::cos(a) * R};
    const GradStop disc[7] = {{0.f, kHi}, {0.17f, kMd}, {0.34f, kLo}, {0.5f, kHi}, {0.66f, kLo}, {0.83f, kMd}, {1.f, kHi}};
    c_->fillLinear(Rect{c.x - R, c.y - R, 2.f * R, 2.f * R}, R, Pt{c.x - d.x, c.y - d.y}, Pt{c.x + d.x, c.y + d.y}, disc, 7);
    // molette : stries radiales tous les 5° sur la couronne extérieure (21 % du rayon)
    const float r0 = R * 0.79f, w = std::max(0.45f, R * 0.026f);
    for (int k = 0; k < 72; ++k) {
        const float an = (float)k * 5.f * kPi / 180.f;
        const float ca = std::cos(an), sa = std::sin(an);
        c_->line(Pt{c.x + r0 * ca, c.y + r0 * sa}, Pt{c.x + R * ca, c.y + R * sa}, Color{0, 0, 0, 0.32f}, w);
        const float an2 = an + 1.5f * kPi / 180.f;
        c_->line(Pt{c.x + r0 * std::cos(an2), c.y + r0 * std::sin(an2)}, Pt{c.x + R * std::cos(an2), c.y + R * std::sin(an2)}, Color{1, 1, 1, 0.24f}, w);
    }
    c_->arc(c, R - 0.5f, kPi * 1.1f, kPi * 1.9f, Color{1, 1, 1, 0.65f}, 1.f);
    c_->arc(c, R - 1.f, kPi * 0.1f, kPi * 0.9f, Color{0, 0, 0, 0.35f}, 1.5f);
    // capuchon : dégradé radial décentré et cercles concentriques très légers
    const float ri = R * 0.7f, box = 2.f * ri;
    const GradStop cap[3] = {{0.f, kHi}, {0.6f, KF}, {1.f, kLo}};
    c_->fillEllipse(Pt{c.x, c.y + 0.6f}, ri + 0.8f, ri + 0.8f, Color{1, 1, 1, 0.5f});
    c_->fillRadial(c, ri, ri, Pt{c.x - ri + 0.35f * box, c.y - ri + 0.30f * box}, 0.955f * box, cap, 3);
    for (float rr = 2.f; rr < ri - 0.5f; rr += 2.f) c_->strokeEllipse(c, rr, rr, Color{0, 0, 0, 0.045f}, 1.f);
    c_->arc(c, ri - 0.5f, kPi * 1.1f, kPi * 1.9f, Color{0, 0, 0, 0.3f}, 1.f);
    // repère : barrette colorée de 3 px du bord vers le centre
    const float ang = (270.f * f - 135.f) * kPi / 180.f;
    const Pt u{std::sin(ang), -std::cos(ang)}, n{u.y * -1.f, u.x};
    const float rOut = R * 0.94f, rIn = R * 0.38f, hw = 1.5f;
    const Pt q[4] = {{c.x + u.x * rOut + n.x * hw, c.y + u.y * rOut + n.y * hw}, {c.x + u.x * rOut - n.x * hw, c.y + u.y * rOut - n.y * hw},
                     {c.x + u.x * rIn - n.x * hw, c.y + u.y * rIn - n.y * hw}, {c.x + u.x * rIn + n.x * hw, c.y + u.y * rIn + n.y * hw}};
    c_->fillPolygon(q, 4, Color{0, 0, 0, 0.3f});
    const Pt q2[4] = {{q[0].x - n.x * 0.5f, q[0].y - n.y * 0.5f}, {q[1].x + n.x * 0.5f, q[1].y + n.y * 0.5f}, {q[2].x + n.x * 0.5f, q[2].y + n.y * 0.5f},
                      {q[3].x - n.x * 0.5f, q[3].y - n.y * 0.5f}};
    c_->fillPolygon(q2, 4, t_->ptr);
}

// Afficheur LCD sous un bouton (fond d'écran, ombre intérieure, liseré clair en bas).
void Ui::lcd(const std::string& s, float cx, float top, bool large) {
    const Font f = large ? Font{Face::MonoBold, 13.f, 0.52f} : Font{Face::Mono, 10.f, 0.4f};
    const float padX = large ? 8.f : 6.f, h = large ? 23.f : 17.f, minW = large ? 72.f : 46.f;
    const float w = std::max(minW, std::round(c_->textWidth(s, f) + 2.f * padX));
    const Rect r{std::round(cx - w * 0.5f), top, w, h};
    c_->fillRect(Rect{r.x, r.b(), r.w, 1.f}, Color{1, 1, 1, 0.5f});
    c_->fillRect(r, t_->scr);
    c_->fillRect(Rect{r.x, r.y, r.w, 1.f}, Color{0, 0, 0, 0.65f});
    c_->fillRect(Rect{r.x, r.y, 1.f, r.h}, Color{0, 0, 0, 0.65f});
    border(r, Color{0, 0, 0, 0.45f});
    textCenter(s, r, f, t_->scrText);
}

// Vis de fixation (coins des cartes).
void Ui::screw(Pt c, bool dark) {
    c_->fillEllipse(c, 4.4f, 4.4f, Color{0, 0, 0, dark ? 0.35f : 0.12f});
    c_->strokeEllipse(c, 4.4f, 4.4f, Color{0, 0, 0, 0.5f}, 0.7f);
    c_->strokeEllipse(c, 3.9f, 3.9f, Color{1, 1, 1, dark ? 0.2f : 0.5f}, 0.4f);
    Pt h[6];
    for (int k = 0; k < 6; ++k) {
        const float a = (-90.f + 60.f * (float)k) * kPi / 180.f;
        h[k] = Pt{c.x + 2.5f * std::cos(a), c.y + 0.25f + 2.5f * std::sin(a)};
    }
    c_->fillPolygon(h, 6, Color{0, 0, 0, 0.62f});
    c_->polyline(h, 6, Color{1, 1, 1, 0.35f}, 0.35f, true);
    c_->line(Pt{c.x - 1.8f, c.y + 0.25f}, Pt{c.x + 1.8f, c.y + 0.25f}, Color{0, 0, 0, 0.45f}, 0.3f);
}

void Ui::screws(const Rect& r, bool dark) {
    screw(Pt{r.x + 9.f, r.y + 9.f}, dark);
    screw(Pt{r.r() - 9.f, r.y + 9.f}, dark);
    screw(Pt{r.x + 9.f, r.b() - 9.f}, dark);
    screw(Pt{r.r() - 9.f, r.b() - 9.f}, dark);
}

bool Ui::knob(Id id, Pt p, float size, float& v, float mod, bool* began, bool* ended) {
    const Rect r{p.x, p.y, size, size};
    const Interact it = interact(id, r, Cursor::ResizeNS);
    bool changed = false;
    if (it.pressed) {
        dragStartY_ = in_.mouse.y;
        dragStartV_ = v;
        if (began) *began = true;
    }
    if (it.held && !it.pressed) {
        const float nv = std::clamp(dragStartV_ + (dragStartY_ - in_.mouse.y) / 160.f, 0.f, 1.f);
        if (nv != v) { v = nv; changed = true; }
    }
    if (it.released && ended) *ended = true;
    drawKnob(p, size, v, mod, true);
    return changed;
}

bool Ui::hslider(Id id, const Rect& r, float& v, bool* began, bool* ended) {
    const Interact it = interact(id, r, Cursor::ResizeEW);
    bool changed = false;
    if (it.pressed && began) *began = true;
    if (it.held) {
        const float nv = std::clamp((in_.mouse.x - r.x) / r.w, 0.f, 1.f);
        if (nv != v) { v = nv; changed = true; }
    }
    if (it.released && ended) *ended = true;
    box(r, t_->railBg, t_->rule);
    c_->fillRect(Rect{r.x + 1.f, r.y + 1.f, (r.w - 2.f) * v, r.h - 2.f}, t_->acc);
    return changed;
}

bool Ui::textField(Id id, const Rect& r, std::string& value, const std::string& ph, const Font& f, Color bg, Color fg, Color line, Color phc) {
    const Interact it = interact(id, r, Cursor::Text);
    if (it.pressed) {
        focus = id;
        caretOwner_ = id;
        caret_ = value.size();
        caretT_ = in_.timeMs;
    } else if (in_.pressed && !it.hovered && focus == id) {
        focus = 0;
    }
    if (focus != id && caretOwner_ == id) caretOwner_ = 0;
    bool enter = false;
    if (focus == id && caretOwner_ != id) {  // focus donné par le code (fenêtre ENREGISTRER) : curseur en fin
        caretOwner_ = id;
        caret_ = value.size();
        caretT_ = in_.timeMs;
    }
    if (focus == id) {
        if (!in_.typed.empty()) {
            std::string clean;
            for (size_t i = 0; i < in_.typed.size();) {
                const size_t st = i;
                const uint32_t cp = nextCp(in_.typed, i);
                if (cp >= 0x20 && cp != 0x7F) clean.append(in_.typed, st, i - st);
            }
            if (value.size() + clean.size() <= 96) {
                value.insert(caret_, clean);
                caret_ += clean.size();
            }
            caretT_ = in_.timeMs;
        }
        if (in_.keyBack && caret_ > 0) {
            const size_t p = prevCharStart(value, caret_);
            value.erase(p, caret_ - p);
            caret_ = p;
            caretT_ = in_.timeMs;
        }
        if (in_.keyDelete && caret_ < value.size()) value.erase(caret_, nextCharStart(value, caret_) - caret_);
        if (in_.keyLeft) caret_ = prevCharStart(value, caret_);
        if (in_.keyRight) caret_ = nextCharStart(value, caret_);
        if (in_.keyHome) caret_ = 0;
        if (in_.keyEnd) caret_ = value.size();
        if (in_.keyEnter) enter = true;
        caret_ = std::min(caret_, value.size());
    }
    box(r, bg, line);
    const float lh = normalLineHeight(f);
    const float top = r.cy() - lh * 0.5f;
    c_->save();
    c_->clip(r.inset(1.f));
    if (value.empty()) text(ph, r.x + 14.f, top, f, phc);
    else text(value, r.x + 14.f, top, f, fg);
    if (focus == id && std::fmod((in_.timeMs - caretT_) / 1000.0, 1.0) < 0.5) {
        const float cx = r.x + 14.f + c_->textWidth(value.substr(0, caret_), f);
        c_->fillRect(Rect{cx, top + 1.f, 1.f, lh - 2.f}, fg);
    }
    c_->restore();
    return enter;
}

} // namespace raw2::ui
