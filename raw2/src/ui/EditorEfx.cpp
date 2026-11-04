// Page EFX (maquette mise à jour) : rack de 10 effets réordonnables, éditeur de l'effet choisi
// (écran, 4 boutons, modulation rapide), sortie avec DRY / WET global.
#include "ui/Editor.h"

#include <algorithm>
#include <cmath>

namespace raw2::ui {

namespace {
constexpr float kTau = 6.2831853f;

std::string pctTxt(float v) { return fmt("%d %%", (int)std::lround(v * 100.f)); }

float distShape(int mode, float x, float dr) {
    switch (mode) {
        case 0: return std::atan(x * dr) / std::atan(dr);
        case 1: return (x >= 0.f ? std::tanh(x * dr) : std::tanh(x * dr * 0.6f) * 0.8f) / std::tanh(dr);
        case 2: return std::sin(x * dr * 1.2f);
        default: return std::clamp(x * dr, -1.f, 1.f);
    }
}

const char* kCaps[kNumFx] = {"VOIX EN UNISSON \xC2\xB7 \xC3\x89" "CARTEMENT", "TRANSFERT  IN \xE2\x86\x92 OUT", "PEIGNE \xC2\xB7 R\xC3\x89PONSE EN FR\xC3\x89QUENCE",
                             "NOTCHS \xC2\xB7 R\xC3\x89PONSE EN FR\xC3\x89QUENCE", "MODULATION DU RETARD", "\xC3\x89" "CHOS \xC2\xB7 TEMPS \xE2\x86\x92",
                             "R\xC3\x89PONSE IMPULSIONNELLE 3D \xC2\xB7 temps \xE2\x86\x97 \xC2\xB7 fr\xC3\xA9quence \xE2\x86\x92", "COURBE  IN \xE2\x86\x92 OUT",
                             "COURBE PARAM\xC3\x89TRIQUE \xC2\xB7 GLISSER LES POINTS", "COUPURE \xC2\xB7 R\xC3\x89SONANCE",
                             "BANDE \xC2\xB7 PLEURAGE \xC2\xB7 SOUFFLE \xC2\xB7 BITS", "GAIN SUR LE TEMPO \xC2\xB7 2 MESURES"};
const char* kDistModes[4] = {"Tape", "Tube", "Fold", "Hard Clip"};
std::string upperAscii(std::string s) {
    for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}
std::string hzTxt(float f) {
    const float h = cutoffToHz(f);
    return h >= 1000.f ? fmt("%.1f kHz", h / 1000.f) : fmt("%d Hz", (int)std::lround(h));
}
} // namespace

// valeur 0..1 d'une source de modulation (même lecture que le moteur)
float Editor::fxModValue(int src) const {
    const auto& R = m_.readouts;
    switch (src) {
        case 0: return std::min(1.f, std::max(0.f, R.src[0].load() * 0.5f + 0.5f) * 2.f);
        case 1: return 0.5f + 0.5f * R.src[1].load();
        case 2: return (R.src[2].load() + 1.f) * 0.5f;
        case 3: return std::min(1.f, R.src[3].load() / 0.6f);
        case 4: return std::clamp((R.src[4].load() + 1.f) * 0.5f, 0.f, 1.f);
        default: return std::clamp((R.src[5].load() + 1.f) * 0.5f, 0.f, 1.f);
    }
}

void Editor::fxSwitch(Id id, const Rect& r, bool on, float fontSize, uint32_t param) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    if (ui_.interact(id, r).clicked) setParam(param, on ? 0.f : 1.f);
    ui_.box(r, on ? T.hl : T.btnBg, T.btnEdge);
    const float tw = std::round(r.w * 0.42f);
    const Rect th{on ? r.r() - 2.f - tw : r.x + 2.f, r.y + 2.f, tw, r.h - 4.f};
    ui_.box(th, T.card, T.btnEdge, 0.f);
    const Font f{Face::MonoBold, fontSize, fontSize * 0.06f};
    const std::string s = on ? "ON" : "OFF";
    const float w = ui_.width(s, f);
    const float x = on ? r.x + 7.f : r.r() - 7.f - w;
    ui_.text(s, x, r.cy() - normalLineHeight(f) * 0.5f, f, on ? T.scr : T.muted);
    (void)c;
}

// écran de l'effet : courbes dans un repère 100 x 30, comme la maquette
void Editor::fxScreen(int fx, const Rect& area, float a, float b, float cc, float m, bool on) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const float W = 100.f, H = 30.f;
    auto X = [&](float x) { return area.x + x / W * area.w; };
    auto Yv = [&](float v) { return area.y + (H - 3.f - std::clamp(v, 0.f, 1.f) * (H - 6.f)) / H * area.h; };
    const float tt = (float)(now_ / 1000.0), tq = std::floor(tt * 10.f) / 10.f;
    const Color wc = T.waveA.withAlpha(on ? 1.f : 0.6f);
    // grille
    for (int k = 1; k < 10; ++k) c.fillRect(Rect{X((float)k * 10.f), area.y, 1.f, area.h}, T.scrMid.withAlpha(0.5f));
    for (int k = 1; k < 4; ++k) c.fillRect(Rect{area.x, area.y + area.h * (float)k / 4.f, area.w, 1.f}, T.scrMid.withAlpha(0.5f));
    std::vector<Pt> pts;
    auto curve = [&](auto fn) {
        pts.clear();
        for (int i = 0; i <= 100; ++i) pts.push_back(Pt{X((float)i), Yv(fn((float)i / 100.f))});
        glowLine(pts, wc, 2.6f);
    };
    auto bar = [&](float x, float v) {
        c.line(Pt{X(x), area.y + (H - 2.f) / H * area.h}, Pt{X(x), Yv(v)}, wc.withAlpha(0.3f), 5.f);
        c.line(Pt{X(x), area.y + (H - 2.f) / H * area.h}, Pt{X(x), Yv(v)}, wc, 2.6f);
    };
    if (fx == kFxDist || fx == kFxComp) {
        const Pt d[2] = {{X(0), area.y + 27.f / H * area.h}, {X(100), area.y + 3.f / H * area.h}};
        c.dashedPolyline(d, 2, T.scrMuted, 1.f, 3.f, 3.f);
    }
    switch (fx) {
        case kFxHyper:
            for (int k = -3; k <= 3; ++k) {
                const float off = (float)((((int)std::floor(tq * (3.f + a * 6.f)) + k * 3) % 3 + 3) % 3 - 1) * 0.8f;
                bar(50.f + (float)k * (3.f + b * 12.f) + off, (0.95f - (float)std::abs(k) * 0.12f * (1.f - cc)) * (k == 0 ? 1.f : 0.4f + 0.6f * m + 0.2f));
            }
            break;
        case kFxDist: {
            const float dr = 1.f + a * 7.f;
            const int md = m_.params.geti(kFxDistMode);
            curve([&](float u) {
                const float t = u * 2.f - 1.f;
                const float w = t * (1.f - m) + distShape(md, t + (cc - 0.5f) * 0.5f, dr) * m;
                return (w + 1.f) * 0.5f;
            });
            // point lumineux : le signal qui traverse la courbe
            const auto& L = an_.scopeL();
            float sv = L.empty() ? 0.f : L.back();
            sv = std::clamp(sv * 1.6f * 3.f, -1.f, 1.f);
            const float w = sv * (1.f - m) + distShape(md, sv + (cc - 0.5f) * 0.5f, dr) * m;
            const Pt p{area.x + (sv + 1.f) * 0.5f * area.w, Yv((w + 1.f) * 0.5f)};
            c.fillEllipse(p, 9.f, 9.f, T.hl.withAlpha(0.25f));
            c.fillEllipse(p, 7.5f, 7.5f, T.scr);
            c.fillEllipse(p, 5.5f, 5.5f, T.hl);
            break;
        }
        case kFxFlang:
            curve([&](float u) { return 0.5f + 0.4f * std::cos(kTau * (u * (2.f + b * 8.f) + tq * (0.3f + a * 1.5f))) * (0.4f + 0.6f * cc) * (0.4f + m); });
            break;
        case kFxPhase:
            curve([&](float u) {
                float v = 0.9f;
                for (int j = 0; j < 3; ++j) {
                    float p = 0.2f + 0.25f * (float)j + cc * 0.1f + 0.1f * std::sin(tq * (0.4f + a * 2.f) * kTau + (float)j);
                    p -= std::floor(p);
                    v -= b * m * 0.9f * std::exp(-(u - p) * (u - p) / 0.003f);
                }
                return v;
            });
            break;
        case kFxChor:
            curve([&](float u) {
                return 0.5f + 0.2f * (std::sin(kTau * (2.f * u + tq * (0.2f + a))) + std::sin(kTau * (2.f * u + cc * 0.3f + b * 0.2f + tq * (0.25f + a * 0.8f)))) * (0.4f + m);
            });
            break;
        case kFxDly: {
            const float tm = 0.1f + a * 0.2f;
            for (int k = 0; k < 6; ++k) {
                const float x = 4.f + (float)k * tm * 60.f;
                if (x > 98.f) break;
                bar(x, k == 0 ? 0.95f : std::pow(0.2f + b * 0.75f, (float)k) * (0.3f + m * 0.7f));
            }
            break;
        }
        case kFxRev: {
            // réponse impulsionnelle en 3D : 22 tranches, de l'arrière vers l'avant
            const int N = 22;
            const float dec = 1.2f / (0.2f + a * 1.2f), pre = cc * 0.3f;
            for (int j = N - 1; j >= 0; --j) {
                const float tz = (float)j / (float)(N - 1);
                const float y0 = 27.f - (float)j * 0.55f, x0 = 6.f + (float)j * 1.1f;
                std::vector<Pt> poly;
                for (int k = 0; k <= 44; ++k) {
                    const float u = (float)k / 44.f;
                    const float rnd = std::fmod(std::fabs(std::sin((float)(j * 7 + k) * 12.9898f) * 43758.5453f), 1.f);
                    const float amp = tz < pre ? 0.03f : std::exp(-(tz - pre) * dec * (1.f + b * u * 2.5f)) * (0.5f + 0.5f * rnd) * (0.3f + 0.7f * m) * (1.f - 0.35f * u);
                    poly.push_back(Pt{X(x0 + u * 62.f), area.y + (y0 - amp * 15.f) / H * area.h});
                }
                poly.push_back(Pt{X(x0 + 62.f), area.y + y0 / H * area.h});
                poly.push_back(Pt{X(x0), area.y + y0 / H * area.h});
                c.fillPolygon(poly.data(), (int)poly.size(), T.scr);
                c.polyline(poly.data(), (int)poly.size(), wc.withAlpha(0.3f + 0.7f * (1.f - tz)), 1.4f, true);
            }
            break;
        }
        case kFxComp: {
            const float th = 1.f - a * 0.7f;
            curve([&](float u) {
                const float y = u < th ? u : th + (u - th) / (1.f + b * 7.f);
                return std::clamp(y * (1.f + cc * 0.4f * m), 0.f, 1.f);
            });
            break;
        }
        case kFxEq: {
            const float f0 = P(kFxEqF0), f1 = P(kFxEqF1), f2 = P(kFxEqF2);
            const float g0 = P(fxParam(kFxEq, kFxP0)), g1 = P(fxParam(kFxEq, kFxP1)), g2 = P(fxParam(kFxEq, kFxP2));
            curve([&](float u) {
                return 0.5f + (g0 - 0.5f) * 0.8f * std::exp(-(u - f0) * (u - f0) / 0.02f) + (g1 - 0.5f) * 0.8f * std::exp(-(u - f1) * (u - f1) / 0.02f) +
                       (g2 - 0.5f) * 0.8f * std::exp(-(u - f2) * (u - f2) / 0.02f);
            });
            break;
        }
        case kFxVintage: {
            // onde qui ondule (pleurage), aplatie par la bande, marches du lo-fi, souffle
            const float steps = 4.f + (1.f - cc) * 60.f;
            curve([&](float u) {
                const float w = std::sin(kTau * (2.f * u + 0.03f * a * std::sin(kTau * (u * 0.5f + tq * 0.55f))));
                float y = std::tanh(w * (1.f + 3.f * a)) / std::tanh(1.f + 3.f * a);
                if (cc > 0.01f) y = std::round(y * steps) / steps;
                const float nz = (std::fmod(std::fabs(std::sin(u * 917.f + tq * 37.f) * 43758.5453f), 1.f) - 0.5f) * b * 0.12f;
                return 0.5f + 0.38f * y * (0.4f + 0.6f * m) + nz;
            });
            break;
        }
        case kFxPump: {
            // gain sur deux mesures : creux juste après chaque temps (RYTHME), remontée (COURBE)
            static const float per[4] = {2.f, 1.f, 0.5f, 0.25f};
            const float p = per[std::clamp((int)(b * 4.f), 0, 3)], ex = 1.f + 5.f * cc;
            curve([&](float u) {
                float ph = u * 8.f / p;
                ph -= std::floor(ph);
                const float duck = ph < 0.03f ? ph / 0.03f : std::pow(1.f - (ph - 0.03f) / 0.97f, ex);
                return 0.12f + 0.8f * (1.f - a * duck * (0.3f + 0.7f * m));
            });
            break;
        }
        default: {
            const float co = 0.15f + 0.8f * a;
            curve([&](float u) { return 1.f / (1.f + std::pow(u / co, 4.f)) * (1.f - 0.1f * m) + b * 0.5f * std::exp(-(u - co) * (u - co) / 0.003f); });
            break;
        }
    }
}

void Editor::drawEfx() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    {  // nouveau preset : on affiche le premier effet de son rack si l'effet affiché est coupé
        int pi;
        {
            std::lock_guard<std::mutex> lk(m_.presetMutex);
            pi = m_.presetIndex;
        }
        if (pi != fxSelPreset_) {
            fxSelPreset_ = pi;
            if (P(fxParam(std::clamp(st_.fxSel, 0, kNumFx - 1), kFxOn)) < 0.5f) st_.fxSel = std::clamp(m_.fxOrder[0].load(), 0, kNumFx - 1);
        }
    }

    // --- RACK ---
    const Rect rack{24.f, 73.f, 369.6f, 693.f};
    ui_.card(rack);
    ui_.cardHeader(rack, "RACK", false, 10.f, 10.f);
    int active = 0;
    for (int f = 0; f < kNumFx; ++f) active += P(fxParam(f, kFxOn)) > 0.5f;
    {
        const Font tf{Face::Mono, 10.f, 1.f};
        const std::string by = st_.fxBypass ? "[ BYPASS \xE2\x97\x8F ]" : "[ BYPASS ]";
        const float bw = std::round(ui_.width(by, tf) + 14.f);
        ButtonStyle s;
        s.bg = st_.fxBypass ? T.ink : Color{0, 0, 0, 0}; s.fg = st_.fxBypass ? T.card : T.ink; s.border = T.ink; s.shadow = false; s.dashed = true;
        s.font = tf;
        if (ui_.button(makeId("fxBypass"), Rect{rack.r() - 11.f - bw, 84.f, bw, 21.f}, by, s)) {
            if (st_.fxBypass) {
                for (int f = 0; f < kNumFx; ++f) setParam(fxParam(f, kFxOn), st_.fxSaved[(size_t)f] ? 1.f : 0.f);
                st_.fxBypass = false;
            } else {
                for (int f = 0; f < kNumFx; ++f) {
                    st_.fxSaved[(size_t)f] = P(fxParam(f, kFxOn)) > 0.5f;
                    setParam(fxParam(f, kFxOn), 0.f);
                }
                st_.fxBypass = true;
            }
        }
        ui_.tag(fmt("%d / %d", active, kNumFx), rack.r() - 11.f - bw - 6.f, 84.f, Ui::Tag::Line);
    }
    int order[kNumFx];
    for (int k = 0; k < kNumFx; ++k) order[k] = std::clamp(m_.fxOrder[(size_t)k].load(), 0, kNumFx - 1);
    auto moveFx = [&](int from, int to) {
        if (to < 0 || to >= kNumFx || from == to) return;
        const int id = order[from];
        if (from < to) for (int k = from; k < to; ++k) order[k] = order[k + 1];
        else for (int k = from; k > to; --k) order[k] = order[k - 1];
        order[to] = id;
        for (int k = 0; k < kNumFx; ++k) m_.fxOrder[(size_t)k].store(order[k]);
    };
    // 12 effets : lignes resserrées pour tenir dans la carte (maquette : 10 lignes de 58,6)
    const float rowH = 48.f, rowGap = 4.3f, listY = 124.f;
    for (int k = 0; k < kNumFx; ++k) {
        const int f = order[k];
        const bool on = P(fxParam(f, kFxOn)) > 0.5f, sel = st_.fxSel == f;
        const Rect row{35.f, listY + (float)k * (rowH + rowGap), 347.6f, rowH};
        if (sel) {
            c.fillRect(row.moved(3.f, 3.f), T.btnEdge);
            ui_.box(row, T.card, T.ink, 2.f);
            ui_.border(row.inset(1.f), T.ink);
        } else {
            ui_.box(row, T.railBg, T.rule);
        }
        if (fxDragRow_ == f) ui_.dashedBorder(row.inset(2.f), T.hl, 4.f, 3.f);
        const float ry = row.y + 6.f;
        // poignée : glisser pour réordonner
        {
            const Rect hr{44.f, ry, 12.f, 20.f};
            const Ui::Interact it = ui_.interact(makeId("fxGrip", f), hr, Cursor::ResizeNS);
            if (it.pressed) fxDragRow_ = f;
            if (it.held && fxDragRow_ == f) {
                const int to = std::clamp((int)std::floor((ui_.in().mouse.y - listY) / (rowH + rowGap)), 0, kNumFx - 1);
                if (to != k) moveFx(k, to);
            }
            if (it.released) fxDragRow_ = -1;
            ui_.textCenter("\xE2\x8B\xAE\xE2\x8B\xAE", Rect{hr.x - 2.f, ry + 4.f, 16.f, 12.f}, Font{Face::Mono, 12.f, -1.8f}, fxDragRow_ == f ? T.hl : T.muted);
        }
        ui_.box(Rect{62.f, ry + 5.f, 10.f, 10.f}, sel ? T.acc : Color{0, 0, 0, 0}, sel ? T.rule : T.muted, 0.f);
        {
            const Rect nr{78.f, ry, 191.6f, 20.f};
            if (ui_.interact(makeId("fxSel", f), nr).clicked) {
                st_.fxSel = f;
                fxMenu_ = 0;
            }
            const std::string nm = std::string(sel ? "\xE2\x96\xB8 " : "") + fxName(f);
            ui_.text(nm, nr.x, ry + 2.f, Font{Face::MonoBold, 12.f, 0.72f}, on || sel ? T.ink : T.muted);
        }
        fxSwitch(makeId("fxOn", f), Rect{275.6f, ry, 46.f, 20.f}, on, 8.f, fxParam(f, kFxOn));
        ButtonStyle sq = ui_.keyStyle();
        sq.font = Font{Face::Mono, 10.f, 1.f};
        if (ui_.button(makeId("fxUp", f), Rect{327.6f, ry, 20.f, 20.f}, "\xE2\x96\xB2", sq)) moveFx(k, k - 1);
        if (ui_.button(makeId("fxDn", f), Rect{353.6f, ry, 20.f, 20.f}, "\xE2\x96\xBC", sq)) moveFx(k, k + 1);
        // MIX de l'effet
        ui_.text("MIX", 44.f, ry + 23.5f, Font{Face::Mono, 9.f, 0.9f}, T.muted);
        const uint32_t mid = fxParam(f, kFxMix);
        float mv = P(mid);
        const Rect tr{74.f, ry + 24.5f, 257.6f, 10.f};
        bool b0 = false, b1 = false;
        const Ui::Interact it = ui_.interact(makeId("fxMix", f), tr, Cursor::ResizeEW);
        if (it.pressed) { beginDrag(mid); b0 = true; }
        if (it.held) {
            mv = std::clamp((ui_.in().mouse.x - tr.x) / tr.w, 0.f, 1.f);
            if (mv != P(mid)) dragParam(mid, mv);
        }
        if (it.released) { endDrag(mid); b1 = true; }
        (void)b0; (void)b1;
        ui_.box(tr, T.card, T.rule, 0.f);
        c.fillRect(Rect{tr.x + 1.f, tr.y + 1.f, (tr.w - 2.f) * mv, tr.h - 2.f}, on ? T.ptr : T.muted);
        ui_.textRight(pctTxt(mv), 373.6f, ry + 25.f, Font{Face::Mono, 10.f, 0.f}, T.ink);
    }

    // --- éditeur de l'effet choisi ---
    const int fx = std::clamp(st_.fxSel, 0, kNumFx - 1);
    const bool on = P(fxParam(fx, kFxOn)) > 0.5f;
    const Rect ed{403.6f, 73.f, 852.4f, 693.f};
    ui_.card(ed);
    int pos = 0;
    for (int k = 0; k < kNumFx; ++k) if (order[k] == fx) pos = k;
    {
        const Font nf{Face::Pixel, 22.f, 1.76f};
        const float nw = ui_.text(fxName(fx), 418.6f, 89.5f, nf, T.ink, 22.f);
        const Font sf{Face::Mono, 10.f, 1.f};
        const std::string sub = fmt("POSITION %d / %d DANS LA CHA\xC3\x8ENE", pos + 1, kNumFx);
        ui_.text(sub, 418.6f + nw + 12.f, 98.5f, sf, T.muted);
        if (fx == kFxDist) {
            float x = 418.6f + nw + 12.f + ui_.width(sub, sf) + 12.f;
            ButtonStyle s;
            s.bg = Color{0, 0, 0, 0}; s.fg = T.ink; s.border = T.ink; s.shadow = false; s.dashed = true;
            s.font = sf;
            const std::string ml = std::string("[ MODE : ") + upperAscii(kDistModes[m_.params.geti(kFxDistMode)]) + " \xE2\x96\xBE ]";
            const float mw = std::round(ui_.width(ml, sf) + 16.f);
            if (ui_.button(makeId("fxMode"), Rect{x, 91.5f, mw, 21.f}, ml, s)) fxMenu_ = fxMenu_ == 1 ? 0 : 1;
            fxMenuX_[0] = x;
            x += mw + 8.f;
            static const char* os[3] = {"1\xC3\x97", "2\xC3\x97", "4\xC3\x97"};
            const std::string ol = std::string("[ OVERSAMPLE : ") + os[m_.params.geti(kFxOversample)] + " \xE2\x96\xBE ]";
            const float ow = std::round(ui_.width(ol, sf) + 16.f);
            if (ui_.button(makeId("fxOs"), Rect{x, 91.5f, ow, 21.f}, ol, s)) fxMenu_ = fxMenu_ == 2 ? 0 : 2;
            fxMenuX_[1] = x;
        }
        fxSwitch(makeId("fxEdOn"), Rect{1165.f, 86.f, 76.f, 30.f}, on, 11.f, fxParam(fx, kFxOn));
        c.fillRect(Rect{418.6f, 126.f, 822.4f, 1.f}, T.rule);
    }
    // écran
    const float amt = P(fxParam(fx, kFxModDepth));
    const int src = m_.params.geti(fxParam(fx, kFxModSrc));
    const float a0 = P(fxParam(fx, kFxP0));
    const float a = std::clamp(a0 + amt / 100.f * fxModValue(src), 0.f, 1.f);
    const float b = P(fxParam(fx, kFxP1)), cc = P(fxParam(fx, kFxP2)), m = P(fxParam(fx, kFxMix));
    {
        const Rect scr{418.6f, 137.f, 822.4f, 379.f};
        scanScreen(scr);
        const Rect area{429.6f, 148.f, 800.4f, 357.f};
        c.save();
        c.clip(scr.inset(1.f));
        fxScreen(fx, area, a, b, cc, fx == kFxEq ? 1.f : m, on);
        c.restore();
        if (!on) c.fillRect(scr.inset(1.f), T.scr.withAlpha(0.45f));
        // EQ : trois points à glisser (fréquence, gain)
        if (fx == kFxEq) {
            static const uint32_t fq[3] = {kFxEqF0, kFxEqF1, kFxEqF2};
            for (int i = 0; i < 3; ++i) {
                const float g = P(fxParam(kFxEq, kFxP0 + i)), f = P(fq[i]);
                const float yv = std::clamp(0.5f + (g - 0.5f) * 0.8f, 0.f, 1.f);
                const Pt p{area.x + f * area.w, area.y + (27.f - yv * 24.f) / 30.f * area.h};
                const Rect pr{p.x - 10.f, p.y - 10.f, 20.f, 20.f};
                const Ui::Interact it = ui_.interact(makeId("eqPt", i), pr, Cursor::Hand);
                if (it.pressed) { beginDrag(fq[i]); beginDrag(fxParam(kFxEq, kFxP0 + i)); }
                if (it.held) {
                    const float fx2 = std::clamp((ui_.in().mouse.x - area.x) / area.w, 0.03f, 0.97f);
                    const float fy = std::clamp((ui_.in().mouse.y - area.y) / area.h, 0.f, 1.f);
                    const float v = (27.f - fy * 30.f) / 24.f;
                    dragParam(fq[i], fx2);
                    dragParam(fxParam(kFxEq, kFxP0 + i), std::clamp(0.5f + (v - 0.5f) / 0.8f, 0.f, 1.f));
                }
                if (it.released) { endDrag(fq[i]); endDrag(fxParam(kFxEq, kFxP0 + i)); }
                ui_.box(pr, it.held ? T.hl : T.scr, T.hl, 0.f);
                ui_.border(pr.inset(1.f), T.hl);
                ui_.textCenter(std::to_string(i + 1), pr, Font{Face::MonoBold, 10.f, 0.f}, it.held ? T.scr : T.hl);
            }
        }
    }
    // légende
    {
        const Font f{Face::Mono, 10.f, 1.f};
        std::string capL = kCaps[fx];
        if (fx == kFxDist) capL += std::string(" \xC2\xB7 ") + upperAscii(kDistModes[m_.params.geti(kFxDistMode)]) + " \xC2\xB7 OS " +
                                    (m_.params.geti(kFxOversample) == 0 ? "1" : (m_.params.geti(kFxOversample) == 1 ? "2" : "4")) + "\xC3\x97";
        ui_.text(capL, 418.6f, 524.f, f, T.muted);
        const std::string capR = fx == kFxEq ? hzTxt(P(kFxEqF0)) + " \xC2\xB7 " + hzTxt(P(kFxEqF1)) + " \xC2\xB7 " + hzTxt(P(kFxEqF2)) : "MIX " + pctTxt(m);
        ui_.textRight(capR, 1241.f, 524.f, f, T.muted);
        c.fillRect(Rect{418.6f, 547.f, 822.4f, 1.f}, T.rule);
    }
    // 4 boutons (le premier montre sa modulation rapide)
    {
        const float colW = 198.1f, gap = 10.f;
        for (int k = 0; k < 4; ++k) {
            const float cx = 418.6f + (float)k * (colW + gap) + colW * 0.5f;
            const uint32_t id = k < 3 ? fxParam(fx, kFxP0 + k) : fxParam(fx, kFxMix);
            const std::string lbl = k < 3 ? fxKnobName(fx, k) : "MIX";
            const std::string val = k < 3 ? fxValueText(fx, k, P(id)) : pctTxt(P(id));
            knobCol(id, cx, 560.f, 108.f, lbl, val, k == 0 ? std::fabs(amt) / 200.f : 0.f, true);
        }
        c.fillRect(Rect{418.6f, 723.f, 822.4f, 1.f}, T.rule);
    }
    // modulation rapide : source -> premier réglage, dosage glissé à l'horizontale
    {
        const Font f{Face::Mono, 10.f, 1.f};
        ui_.text("MODULATION RAPIDE :", 418.6f, 736.f, f, T.muted);
        ButtonStyle s;
        s.bg = Color{0, 0, 0, 0}; s.fg = T.ink; s.border = T.ink; s.shadow = false; s.dashed = true;
        s.font = f;
        const std::string sl = std::string("[ ") + modSourceName(src) + " \xE2\x96\xBE ]";
        const float sw = std::round(ui_.width(sl, f) + 16.f);
        if (ui_.button(makeId("fxSrc"), Rect{561.6f, 732.f, sw, 21.f}, sl, s)) fxMenu_ = fxMenu_ == 3 ? 0 : 3;
        float x = 561.6f + sw + 10.f;
        x += ui_.text("\xE2\x86\x92 ", x, 736.f, f, T.muted);
        x += ui_.text(fxKnobName(fx, 0), x, 736.f, f, T.muted) + 10.f;
        const uint32_t did = fxParam(fx, kFxModDepth);
        const Font vf{Face::MonoBold, 11.f, 0.66f};
        const std::string vt = std::string("[") + (amt > 0.5f ? "+" : "") + fmt("%d %%", (int)std::lround(amt)) + "]";
        const Rect vr{x, 732.5f, std::round(ui_.width(vt, vf) + 14.f), 20.f};
        const Ui::Interact it = ui_.interact(makeId("fxDepth"), vr, Cursor::ResizeEW);
        static float startX = 0.f, startV = 0.f;
        if (it.pressed) { startX = ui_.in().mouse.x; startV = amt; beginDrag(did); }
        if (it.held && !it.pressed) {
            const float nv = std::clamp(std::round(startV + (ui_.in().mouse.x - startX) * 1.2f), -100.f, 100.f);
            if (nv != P(did)) dragParam(did, nv);
        }
        if (it.released) endDrag(did);
        ui_.box(vr, Color{0, 0, 0, 0}, T.ink);
        ui_.textCenter(vt, vr, vf, T.ink);
    }

    // --- SORTIE FX : sec / traité et DRY / WET ---
    const Rect so{24.f, 776.f, 1232.f, 160.f};
    ui_.card(so);
    ui_.text("SORTIE FX", 39.f, 795.5f, Font{Face::Pixel, 16.f, 1.28f}, T.ink, 16.f);
    {
        ui_.text("DRY / WET", 884.f, 797.f, Font{Face::Mono, 10.f, 1.f}, T.muted);
        const Rect tr{957.f, 787.f, 230.f, 12.f};
        float wv = P(kFxWet);
        const Ui::Interact it = ui_.interact(makeId("fxWet"), tr.inset(0.f, -4.f), Cursor::ResizeEW);
        if (it.pressed) beginDrag(kFxWet);
        if (it.held) {
            wv = std::clamp((ui_.in().mouse.x - tr.x) / tr.w, 0.f, 1.f);
            if (wv != P(kFxWet)) dragParam(kFxWet, wv);
        }
        if (it.released) endDrag(kFxWet);
        ui_.box(tr, T.card, T.rule, 0.f);
        c.fillRect(Rect{tr.x + 1.f, tr.y + 1.f, (tr.w - 2.f) * wv, tr.h - 2.f}, T.ptr);
        for (int k = 0; k <= 10; ++k) c.fillRect(Rect{std::min(tr.r() - 1.f, tr.x + (float)k * tr.w / 10.f), 801.f, 1.f, 6.f}, T.ink);
        const Font lf{Face::Mono, 9.f, 0.f};
        ui_.text("0 %", 957.f, 808.f, lf, T.muted);
        ui_.text("50 %", 1055.8f, 808.f, lf, T.muted);
        ui_.text("100 %", 1160.f, 808.f, lf, T.muted);
        ui_.textRight(pctTxt(wv), 1241.f, 795.f, Font{Face::MonoBold, 13.f, 0.f}, T.ink);
    }
    {
        const Rect scr{39.f, 839.f, 832.f, 70.f};
        scanScreen(scr);
        const Rect area{48.f, 848.f, 814.f, 52.f};
        c.save();
        c.clip(scr.inset(1.f));
        c.line(Pt{area.x, area.cy()}, Pt{area.r(), area.cy()}, T.scrMid, 1.f);
        auto trace = [&](const std::vector<float>& L, std::vector<Pt>& pts) {
            const int n = (int)L.size();
            float pk = 0.05f;
            for (float v : L) pk = std::max(pk, std::fabs(v));
            for (int i = 0; i <= 200 && n > 0; ++i) {
                const float v = std::clamp(L[(size_t)std::min(n - 1, i * n / 201)] / pk * 0.9f, -1.15f, 1.15f);
                pts.push_back(Pt{area.x + (float)i / 200.f * area.w, area.cy() - v * area.h * 0.5f});
            }
        };
        // même fenêtre (1024 échantillons) dans les deux anneaux : sec et traité alignés
        dryScope_.resize(1024);
        wetScope_.resize(1024);
        m_.dryScope.read(dryScope_.data(), nullptr, 1024);
        m_.scope.read(wetScope_.data(), nullptr, 1024);
        std::vector<Pt> dry, wet;
        trace(dryScope_, dry);
        trace(wetScope_, wet);
        if (dry.size() > 1) c.dashedPolyline(dry.data(), (int)dry.size(), T.scrMuted, 1.5f, 4.f, 3.f);
        glowLine(wet, T.waveA, 2.5f);
        c.restore();
    }
    fxBars(Rect{883.f, 839.f, 358.f, 86.f});

    // --- menus (calque 3) ---
    ui_.setLayer(3);
    auto menu = [&](float x, float y, float w, int n, const char* const* items, int cur, bool up, auto onPick) {
        const float ih = 27.f;
        const Rect mr{x, up ? y - 4.f - ih * (float)n - 2.f : y, w, ih * (float)n + 2.f};
        fxMenuRect_ = mr;
        c.fillRect(mr.moved(3.f, 3.f), T.btnEdge);
        ui_.box(mr, T.card, T.ink, 0.f);
        for (int k = 0; k < n; ++k) {
            const Rect r{mr.x + 1.f, mr.y + 1.f + (float)k * ih, mr.w - 2.f, ih};
            if (k == cur) c.fillRect(r, T.ink);
            c.fillRect(Rect{r.x, r.b() - 1.f, r.w, 1.f}, T.rule);
            if (ui_.interact(makeId("fxMenuIt", k), r).clicked) { onPick(k); fxMenu_ = 0; }
            ui_.text(items[k], r.x + 11.f, r.y + 6.5f, Font{Face::Mono, 11.f, 0.66f}, k == cur ? T.card : T.ink);
        }
    };
    if (fxMenu_ == 1 && fx == kFxDist)
        menu(fxMenuX_[0], 116.5f, 140.f, 4, kDistModes, m_.params.geti(kFxDistMode), false, [&](int k) { setParam(kFxDistMode, (float)k); });
    else if (fxMenu_ == 2 && fx == kFxDist) {
        static const char* os[3] = {"1\xC3\x97", "2\xC3\x97", "4\xC3\x97"};
        menu(fxMenuX_[1], 116.5f, 140.f, 3, os, m_.params.geti(kFxOversample), false, [&](int k) { setParam(kFxOversample, (float)k); });
    } else if (fxMenu_ == 3) {
        static const char* srcs[6] = {"VIE", "LFO 1", "LFO 2", "ENV", "CLAVIER", "AL\xC3\x89" "A"};
        menu(561.6f, 732.f, 120.f, 6, srcs, src, true, [&](int k) { setParam(fxParam(fx, kFxModSrc), (float)k); });
    } else {
        fxMenu_ = 0;
    }
    ui_.setLayer(0);
}

// barres de la sortie FX (20 bandes, crantées)
void Editor::fxBars(const Rect& box) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    scanScreen(box);
    fxb_.resize(20, now_);
    an_.bands(20, bandTmp_);
    for (int i = 0; i < 20; ++i) {
        const int nT = std::clamp((int)std::lround(1.f + (bandTmp_[(size_t)i] + 60.f) / 54.f * 9.f), 1, 10);
        fxb_.push(i, nT, now_, 35.0, 60.0);
        const int n = fxb_.sh[(size_t)i], p = fxb_.pk[(size_t)i];
        const float x = box.x + 13.f + (float)i * 12.f, bottom = box.b() - 10.f;
        for (int k = 0; k < n; ++k) c.fillRect(Rect{x, bottom - (float)k * 7.f - 5.f, 8.f, 5.f}, n >= 6 && k >= 5 ? T.barHi : (k >= 7 ? T.barPk : T.barLo));
        if (p > n) c.fillRect(Rect{x, bottom - (float)(p - 1) * 7.f - 5.f, 8.f, 5.f}, T.barPk);
    }
}

} // namespace raw2::ui
