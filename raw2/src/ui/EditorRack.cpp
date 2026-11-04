// Modulation libre (maquette mise à jour) : cartes LFO communes à la page ENV / LFO et au rack
// modulaire (MODULES, SIDE B), forme dessinée (ÉDITION / DRAW), poignée [✛ MOD] à glisser sur un
// potard, anneaux des routes libres sur les potards.
#include "ui/Editor.h"

#include <algorithm>
#include <cmath>

namespace raw2::ui {

namespace {

const Color kOrange = Color::rgb8(255, 122, 26);   // #ff7a1a, couleur des routes libres
const Color kOrangePale = Color::rgb8(255, 210, 168);

std::string timeTxt(float s) {
    if (s < 1.f) return fmt("%d ms", std::max(1, (int)std::lround(s * 1000.f)));
    return fmt("%.2f s", s);
}

float lfoShapeAt(int shape, double ph) {
    const float f = (float)(ph - std::floor(ph));
    switch (shape) {
        case kLfoSine: return std::sin(6.2831853f * (float)ph);
        case kLfoTri: return f < 0.5f ? 4.f * f - 1.f : 3.f - 4.f * f;
        case kLfoSaw: return 2.f * f - 1.f;
        case kLfoSqr: return f < 0.5f ? 1.f : -1.f;
        default: {  // S&H : même pseudo-hasard que la maquette
            const double x = std::sin(std::floor(ph * 4.0) * 127.1 + 311.7) * 43758.5453;
            return (float)(x - std::floor(x)) * 2.f - 1.f;
        }
    }
}

float snapX(int snap, float u) {
    const float dv = snap == 0 ? 16.f : (snap == 1 ? 8.f : 0.f);
    return dv > 0.f ? std::round(u * dv) / dv : u;
}

const char* kSnapNames[3] = {"1/16", "1/8", "FREE"};

} // namespace

// poignée [✛ MOD] : appuyer puis glisser vers un potard pour créer une route libre
bool Editor::modHandle(Id id, const Rect& r, int lfo) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const Ui::Interact it = ui_.interact(id, r, Cursor::Hand);
    if (it.pressed) {
        dd_ = DragMod{};
        dd_.on = true;
        dd_.src = lfo;
    }
    c.fillRect(r.inset(-3.f), kOrange.withAlpha(dd_.on && dd_.src == lfo ? 0.3f : 0.12f));
    ui_.box(r, T.btnBg, kOrange);
    // « [✛ MOD] » : la croix est dessinée (absente des polices embarquées)
    const Font f{Face::Mono, 9.f, 0.54f};
    const float wl = ui_.width("[", f), wr = ui_.width(" MOD]", f), cw = 7.f;
    float x = r.cx() - (wl + cw + wr) * 0.5f;
    const float ty = r.y + (r.h - 12.f) * 0.5f;
    x += ui_.text("[", x, ty, f, kOrange);
    const Pt cc{x + cw * 0.5f + 0.5f, r.cy()};
    c.line(Pt{cc.x - 3.5f, cc.y}, Pt{cc.x + 3.5f, cc.y}, kOrange, 1.6f);
    c.line(Pt{cc.x, cc.y - 3.5f}, Pt{cc.x, cc.y + 3.5f}, kOrange, 1.6f);
    ui_.text(" MOD]", x + cw, ty, f, kOrange);
    return it.pressed;
}

// bouton moleté 64 px du rack (nom à +66, afficheur à +84)
void Editor::rackKnob(uint32_t id, float cx, float top, const std::string& label, const std::string& value) {
    const Theme& T = *T_;
    knobParam(id, Pt{cx - 32.f, top}, 64.f, 0.f);
    ui_.textCenter(label, Rect{cx - 50.f, top + 66.f, 100.f, 12.f}, Font{Face::Mono, 9.f, 0.9f}, T.muted);
    ui_.lcd(value, cx, top + 84.f, true);
}

// Écran d'un LFO : deux cycles, phase, fondu d'entrée, lissage ; en ÉDITION / DRAW, les nœuds de
// la forme dessinée se déplacent (double-clic : ajouter / retirer, glisser un segment : courbure,
// Maj + glisser : adoucissement).
void Editor::lfoScreen(int n, const Rect& scr, bool rack) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ParamStore& P = m_.params;
    const uint32_t idPh = lfoP(n, kLfPhase), idFade = lfoP(n, kLfFade);
    const int sync = P.geti(lfoP(n, kLfSync)), shape = P.geti(lfoP(n, kLfShape));
    const bool draw = P.get(lfoP(n, kLfDraw)) > 0.5f;
    const float hz = sync > 0 ? lfoSyncFactor(sync) * m_.hostBpm.load() / 120.f : lfoRateHz(P.get(lfoP(n, kLfRate)));
    const float phase = P.get(idPh), fade = P.get(idFade), smo = P.get(lfoP(n, kLfSmooth)), fl = fade * 0.8f;
    LfoShapes& LS = m_.lfoShapes;
    std::vector<LfoNode>& pts = LS.pts[(size_t)n];
    if (draw && pts.size() < 2) LS.set(n, LfoShapes::seed(shape));
    const int snap = LS.snap[(size_t)n];

    scanScreen(scr);
    const float s = (scr.w - 2.f) / 640.f;
    const float ox = scr.x + 1.f, oy = scr.y + 1.f + (scr.h - 2.f - 160.f * s) * 0.5f;
    auto V = [&](float x, float y) { return Pt{ox + x * s, oy + y * s}; };
    const Color col = (n % 2 == 0) ? T.waveA : T.waveB;
    const Rect area{ox, oy, 640.f * s, 160.f * s};
    const Pt m = ui_.in().mouse;
    const float vx = (m.x - ox) / s, vy = (m.y - oy) / s;
    const Id gid = makeId("lfoGraph", n);
    const Ui::Interact it = ui_.interact(gid, area, Cursor::Cross);
    auto shapeY = [&](double ph) { return draw ? LfoShapes::eval(pts, (float)ph) : lfoShapeAt(shape, ph); };

    if (!draw && !rack) {
        // glisser : point de phase (milieu) ou de fondu (haut)
        if (it.pressed) {
            const float h0x = 30.f + phase * 295.f, h1x = 30.f + fl * 590.f;
            const float d0 = (h0x - vx) * (h0x - vx) + (80.f - vy) * (80.f - vy), d1 = (h1x - vx) * (h1x - vx) + (22.f - vy) * (22.f - vy);
            lfoHandle_ = d0 <= d1 ? 0 : 1;
            lfoDragging_ = n;
            beginDrag(lfoHandle_ == 0 ? idPh : idFade);
        }
        if ((it.pressed || it.held) && lfoDragging_ == n) {
            if (lfoHandle_ == 0) dragParam(idPh, std::clamp((vx - 30.f) / 295.f, 0.f, 1.f));
            else dragParam(idFade, std::clamp((vx - 30.f) / 590.f / 0.8f, 0.f, 1.f));
        }
        if (it.released && lfoDragging_ == n) {
            endDrag(lfoHandle_ == 0 ? idPh : idFade);
            lfoDragging_ = -1;
        }
    } else if (draw) {
        std::vector<LfoNode> p = pts;
        bool changed = false;
        auto ux = [&](float x) { float u = (x - 30.f) / 295.f; return u > 1.0001f ? u - 1.f : u; };
        if (it.pressed) {
            const bool dbl = now_ - dblT_ < 350.0 && dblLfo_ == n && std::fabs(dblP_.x - vx) < 10.f && std::fabs(dblP_.y - vy) < 10.f;
            dblT_ = now_;
            dblP_ = Pt{vx, vy};
            dblLfo_ = n;
            int hit = -1;
            for (size_t i = 0; i < p.size(); ++i)
                for (int k = 0; k < 2; ++k) {
                    const float dx = 30.f + (p[i].x + (float)k) * 295.f - vx, dy = 80.f - p[i].y * 58.f - vy;
                    if (dx * dx + dy * dy < 121.f) hit = (int)i;
                }
            le_ = LfoEdit{};
            if (dbl) {
                dblT_ = -1e9;
                if (hit > 0 && hit < (int)p.size() - 1) {
                    p.erase(p.begin() + hit);
                    changed = true;
                } else if (hit < 0 && p.size() < 64) {
                    const float u = snapX(snap, std::clamp(ux(vx), 0.005f, 0.995f)), y = std::clamp((80.f - vy) / 58.f, -1.f, 1.f);
                    size_t i = 0;
                    while (i + 1 < p.size() && p[i + 1].x < u) ++i;
                    if (i + 1 < p.size() && std::fabs(p[i].x - u) >= 0.004f && std::fabs(p[i + 1].x - u) >= 0.004f) {
                        p.insert(p.begin() + (long)i + 1, LfoNode{u, y, 0.f, 0.f});
                        changed = true;
                    }
                }
            } else if (hit >= 0) {
                le_.lfo = n;
                le_.mode = 1;
                le_.i = hit;
            } else {
                const float u = ux(vx);
                for (size_t i = 0; i + 1 < p.size(); ++i) {
                    if (u >= p[i].x && u <= p[i + 1].x) {
                        const float d = p[i + 1].x - p[i].x, t = d > 1e-6f ? (u - p[i].x) / d : 0.f;
                        const float cy = 80.f - (p[i].y + (p[i + 1].y - p[i].y) * LfoShapes::segG(t, p[i].c, p[i].s)) * 58.f;
                        if (std::fabs(cy - vy) < 14.f) le_ = LfoEdit{n, 2, (int)i, vx, vy, p[i].c, p[i].s};
                        break;
                    }
                }
            }
        }
        if (it.held && !it.pressed && le_.lfo == n) {
            if (le_.mode == 1 && le_.i >= 0 && le_.i < (int)p.size()) {
                LfoNode& q = p[(size_t)le_.i];
                q.y = std::clamp((80.f - vy) / 58.f, -1.f, 1.f);
                if (le_.i > 0 && le_.i < (int)p.size() - 1)
                    q.x = std::clamp(snapX(snap, ux(vx)), p[(size_t)le_.i - 1].x + 0.005f, p[(size_t)le_.i + 1].x - 0.005f);
                changed = true;
            } else if (le_.mode == 2 && le_.i >= 0 && le_.i + 1 < (int)p.size()) {
                LfoNode& a = p[(size_t)le_.i];
                const float dir = (p[(size_t)le_.i + 1].y - a.y) >= 0.f ? 1.f : -1.f;
                if (ui_.in().shift) a.s = std::clamp(le_.s0 + (vx - le_.vx0) / 150.f, 0.f, 1.f);
                else a.c = std::clamp(le_.c0 + (vy - le_.vy0) / 40.f * dir, -1.f, 1.f);
                changed = true;
            }
        }
        if (it.released && le_.lfo == n) le_ = LfoEdit{};
        if (changed) {
            LS.set(n, p);
            LS.edited[(size_t)n] = true;
        }
    }

    // tracé : deux cycles, fondu d'entrée et lissage (moyenne glissante)
    constexpr int NS = 240;
    std::vector<float> ya(NS + 1), ys(NS + 1);
    for (int k = 0; k <= NS; ++k) {
        const float xn = (float)k / NS, a = fl > 0.001f ? std::min(1.f, xn / fl) : 1.f;
        ya[(size_t)k] = shapeY((double)xn * 2.0 + (double)phase) * a;
    }
    const int rad = (int)std::lround(smo * 14.f);
    for (int k = 0; k <= NS; ++k) {
        if (!rad) { ys[(size_t)k] = ya[(size_t)k]; continue; }
        float sum = 0.f;
        int cnt = 0;
        for (int d = -rad; d <= rad; ++d) {
            const int q = k + d;
            if (q >= 0 && q <= NS) { sum += ya[(size_t)q]; ++cnt; }
        }
        ys[(size_t)k] = sum / (float)cnt;
    }
    auto Yl = [](float v) { return 80.f - v * 58.f; };
    c.save();
    c.clip(scr.inset(1.f));
    c.line(V(30.f, 80.f), V(620.f, 80.f), T.scrGrid, 1.f);
    for (float v : {-1.f, -0.5f, 0.5f, 1.f}) c.line(V(30.f, Yl(v)), V(620.f, Yl(v)), T.scrGrid, 1.f);
    const float dv = snap == 0 ? 16.f : (snap == 1 ? 8.f : 0.f);
    if (draw && dv > 0.f) {
        for (int k = 0; k < 2; ++k)
            for (int j = 0; j <= (int)dv; ++j) {
                const float x = 30.f + ((float)k + (float)j / dv) * 295.f;
                c.line(V(x, 22.f), V(x, 138.f), T.scrGrid, 1.f);
            }
    } else {
        for (float x = 30.f; x <= 620.f; x += 59.f) c.line(V(x, 22.f), V(x, 138.f), T.scrGrid, 1.f);
    }
    if (draw) {  // forme brute (sans phase ni fondu), en pointillés
        Pt prev{};
        for (int k = 0; k <= NS; ++k) {
            const Pt q = V(30.f + (float)k / NS * 590.f, Yl(LfoShapes::eval(pts, (float)k / NS * 2.f)));
            if (k > 0 && (k / 3) % 2 == 0) c.line(prev, q, T.scrMuted, 1.2f * s);
            prev = q;
        }
    }
    std::vector<Pt> path;
    for (int k = 0; k <= NS; ++k) path.push_back(V(30.f + (float)k / NS * 590.f, Yl(ys[(size_t)k])));
    if (n % 2 == 1) glowLine(path, col, 2.f * s);
    else {
        c.polyline(path.data(), (int)path.size(), col.withAlpha(0.18f), 6.f * s);
        c.polyline(path.data(), (int)path.size(), col, 2.f * s);
    }
    if (!draw && !rack) {
        for (const Pt& h : {V(30.f + phase * 295.f, 80.f), V(30.f + fl * 590.f, 22.f)}) {
            c.fillEllipse(h, 6.f * s, 6.f * s, T.scr);
            c.strokeEllipse(h, 6.f * s, 6.f * s, col, 2.f * s);
        }
    } else {
        for (const LfoNode& q : pts) {
            const Pt a = V(30.f + q.x * 295.f, Yl(q.y)), b = V(30.f + q.x * 295.f + 295.f, Yl(q.y));
            const Rect ra{a.x - 4.5f * s, a.y - 4.5f * s, 9.f * s, 9.f * s}, rb{b.x - 4.5f * s, b.y - 4.5f * s, 9.f * s, 9.f * s};
            c.fillRect(rb, T.scr);
            ui_.border(rb, col.withAlpha(0.4f), 1.5f * s);
            c.fillRect(ra, T.scr);
            ui_.border(ra, T.hl, 2.f * s);
        }
        const Font hf{Face::Mono, 9.f * s, 0.f};
        const std::string hint = "DBL-CLIC : +/\xE2\x88\x92 N\xC5\x92UD \xC2\xB7 GLISSER SEGMENT : COURBE \xC2\xB7 MAJ : S";
        c.text(hint, V(620.f, 154.f).x - c.textWidth(hint, hf), V(620.f, 154.f).y, hf, T.scrMuted);
    }
    {  // point de lecture
        const float xl = (float)std::fmod(now_ / 1000.0 * (double)hz / 2.0, 1.0), fi = xl * NS;
        const int f0 = std::min(NS - 1, (int)fi);
        const float vv = ys[(size_t)f0] + (ys[(size_t)f0 + 1] - ys[(size_t)f0]) * (fi - (float)f0);
        c.fillEllipse(V(30.f + xl * 590.f, Yl(vv)), 4.f * s, 4.f * s, T.hl);
    }
    if (!rack) {
        const Font af{Face::Mono, 9.f * s, 0.f};
        c.text("+1", V(3.f, 26.f).x, V(3.f, 26.f).y, af, T.scrMuted);
        c.text("0", V(3.f, 84.f).x, V(3.f, 84.f).y, af, T.scrMuted);
        c.text("-1", V(3.f, 142.f).x, V(3.f, 142.f).y, af, T.scrMuted);
    }
    c.restore();
}

// Carte LFO : page ENV / LFO (rack = false, 610 x 425.5) ou rack modulaire (610 x 358.5).
void Editor::lfoCard(int n, const Rect& card, bool rack) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ParamStore& P = m_.params;
    ui_.card(card);
    const uint32_t idRate = lfoP(n, kLfRate), idPh = lfoP(n, kLfPhase), idFade = lfoP(n, kLfFade), idSm = lfoP(n, kLfSmooth);
    const uint32_t idShape = lfoP(n, kLfShape), idSync = lfoP(n, kLfSync), idDraw = lfoP(n, kLfDraw);
    const int sync = P.geti(idSync), shape = P.geti(idShape);
    const bool draw = P.get(idDraw) > 0.5f;
    const bool bpm = m_.lfoBpm[(size_t)n].load();
    LfoShapes& LS = m_.lfoShapes;
    std::string title = n < 2 ? std::string("MODULATION \xC2\xB7 ") + lfoDisplayName(n) : std::string(lfoDisplayName(n));
    if (n >= 2 && LS.grp[(size_t)n - 2] > 0) title += fmt(" \xC2\xB7 G%d", LS.grp[(size_t)n - 2]);
    ui_.text(title, card.x + 15.f, card.y + (rack ? 15.f : 17.f), Font{Face::Pixel, 16.f, 1.28f}, T.ink, 16.f);
    c.fillRect(Rect{card.x + 15.f, card.y + (rack ? 48.f : 47.f), card.w - 30.f, 1.f}, T.rule);

    auto toggleDraw = [&] {
        if (!draw) {
            if (!LS.edited[(size_t)n] || LS.pts[(size_t)n].size() < 2) LS.set(n, LfoShapes::seed(shape));
            setParam(idDraw, 1.f);
        } else {
            setParam(idDraw, 0.f);
        }
    };
    auto drawBtnStyle = [&](bool on) {
        ButtonStyle s;
        s.bg = on ? T.ink : T.btnBg;
        s.fg = on ? T.card : T.ink;
        s.border = on ? kOrange : T.ink;
        s.font = Font{Face::Mono, 9.f, 0.54f};
        s.pressed = on;
        return s;
    };
    const float hzFree = lfoRateHz(P.get(idRate));
    const float hz = sync > 0 ? lfoSyncFactor(sync) * m_.hostBpm.load() / 120.f : hzFree;
    const float phase = P.get(idPh), fade = P.get(idFade), smo = P.get(idSm);
    std::string rateTxt;
    if (sync > 0) rateTxt = std::string(lfoSyncName(sync)) + " \xC2\xB7 " + fmt("%.2f Hz", hz);
    else rateTxt = bpm ? fmt("%d BPM", (int)std::lround(hz * 60.f)) : fmt("%.2f Hz", hz);
    const std::string vals[4] = {rateTxt, fmt("%d\xC2\xB0", (int)std::lround(phase * 360.f)), timeTxt(std::max(0.001f, fade * 4.f)), fmt("%d %%", (int)std::lround(smo * 100.f))};
    const char* labels[4] = {"VITESSE", "PHASE", "FADE IN", "LISSAGE"};
    const uint32_t ids[4] = {idRate, idPh, idFade, idSm};

    if (!rack) {
        float right = card.r() - 15.f, w = 0.f;
        if (envBtn(makeId("lfoSync", n), right, card.y + 13.f, std::string("[ SYNC : ") + lfoSyncName(sync) + " ]", sync > 0, &w)) setParam(idSync, (float)((sync + 1) % 6));
        right -= w + 6.f;
        if (envBtn(makeId("lfoUnit", n), right, card.y + 13.f, bpm ? "[ BPM ]" : "[ HZ ]", bpm, &w)) m_.lfoBpm[(size_t)n] = !bpm;
        right -= w + 6.f;
        modHandle(makeId("lfoMod", n), Rect{right - 59.6f, card.y + 14.f, 59.6f, 22.f}, n);

        lfoScreen(n, Rect{card.x + 15.f, card.y + 56.f, card.w - 30.f, 190.5f}, false);

        ui_.text("FORME", card.x + 15.f, card.y + 261.f, Font{Face::Mono, 10.f, 1.2f}, T.muted);
        float x = card.x + 61.f;
        for (int k = 0; k < kNumLfoShapes; ++k) {
            const float bw = k == 0 ? 53.8f : 47.8f;
            const Rect r{x, card.y + 254.5f, bw, 26.f};
            const bool on = k == shape && !draw;
            if (ui_.interact(makeId("lfoShape", n * 8 + k), r).clicked) {
                setParam(idShape, (float)k);
                setParam(idDraw, 0.f);
                LS.edited[(size_t)n] = false;
            }
            ui_.box(r, on ? T.ink : T.btnBg, T.ink);
            ui_.textCenter(lfoShapeName(k), Rect{r.x, r.y + 7.f, r.w, 12.f}, Font{Face::Mono, 9.f, 0.54f}, on ? T.card : T.ink);
            x += bw - 1.f;
        }
        if (ui_.button(makeId("lfoDraw", n), Rect{card.x + 312.1f, card.y + 255.5f, 122.9f, 24.f}, "[ \xC3\x89" "DITION / DRAW ]", drawBtnStyle(draw))) toggleDraw();
        {
            ButtonStyle st = drawBtnStyle(false);
            if (!draw) { st.fg = st.fg.withAlpha(0.45f); st.border = st.border.withAlpha(0.45f); }
            if (ui_.button(makeId("lfoSnap", n), Rect{card.x + 445.f, card.y + 255.5f, 105.1f, 24.f}, std::string("[ SNAP : ") + kSnapNames[LS.snap[(size_t)n]] + " ]", st))
                LS.snap[(size_t)n] = (LS.snap[(size_t)n] + 1) % 3;
        }
        for (int k = 0; k < 4; ++k) envKnob(ids[k], card.x + 15.f + 72.5f + (float)k * 145.f, card.y + 288.5f, labels[k], vals[k]);
        return;
    }

    // --- rack : [✛ MOD] [ GRP ] [ ✕ ] ---
    {
        const ButtonStyle st = drawBtnStyle(false);
        const Rect del{card.r() - 15.f - 45.7f, card.y + 13.f, 45.7f, 24.f};
        if (ui_.button(makeId("rkDel", n), del, "[ \xE2\x9C\x95 ]", st)) {
            removeRackLfo(n);
            return;
        }
        const int g = LS.grp[(size_t)n - 2];
        const Rect gr{del.x - 6.f - 81.3f, card.y + 13.f, 81.3f, 24.f};
        if (ui_.button(makeId("rkGrp", n), gr, g ? fmt("[ GRP : G%d ]", g) : std::string("[ GRP : \xE2\x80\x94 ]"), drawBtnStyle(g > 0))) LS.grp[(size_t)n - 2] = (g + 1) % 4;
        modHandle(makeId("rkMod", n), Rect{gr.x - 6.f - 59.6f, card.y + 14.f, 59.6f, 22.f}, n);
    }
    lfoScreen(n, Rect{card.x + 15.f, card.y + 56.f, card.w - 30.f, 146.5f}, true);
    {
        float x = card.x + 15.f;
        const float y = card.y + 210.5f;
        auto btn = [&](const char* key, const std::string& label, float w, const ButtonStyle& st) {
            const bool r = ui_.button(makeId(key, n), Rect{x, y, w, 24.f}, label, st);
            x += w + 6.f;
            return r;
        };
        if (btn("rkShape", std::string("[ FORME : ") + (draw ? "DRAW" : lfoShapeName(shape)) + " ]", 111.f, drawBtnStyle(false))) {
            setParam(idShape, (float)((shape + 1) % kNumLfoShapes));
            setParam(idDraw, 0.f);
            LS.edited[(size_t)n] = false;
        }
        if (btn("rkDraw", "[ \xC3\x89" "DITION / DRAW ]", 122.9f, drawBtnStyle(draw))) toggleDraw();
        ButtonStyle sn = drawBtnStyle(false);
        if (!draw) { sn.fg = sn.fg.withAlpha(0.45f); sn.border = sn.border.withAlpha(0.45f); }
        if (btn("rkSnap", std::string("[ SNAP : ") + kSnapNames[LS.snap[(size_t)n]] + " ]", 105.1f, sn)) LS.snap[(size_t)n] = (LS.snap[(size_t)n] + 1) % 3;
        if (btn("rkUnit", bpm ? "[ BPM ]" : "[ HZ ]", 51.6f, drawBtnStyle(bpm))) m_.lfoBpm[(size_t)n] = !bpm;
        if (btn("rkSync", std::string("[ SYNC : ") + lfoSyncName(sync) + " ]", 99.2f, drawBtnStyle(sync > 0))) setParam(idSync, (float)((sync + 1) % 6));
    }
    for (int k = 0; k < 4; ++k) rackKnob(ids[k], card.x + 88.3f + (float)k * 144.8f, card.y + 242.5f, labels[k], vals[k]);
}

void Editor::addRackLfo() {
    for (int r = 0; r < kNumRackLfos; ++r) {
        const int n = 2 + r;
        if (P(lfoP(n, kLfOn)) > 0.5f) continue;
        for (int f = 0; f < kLfFieldsPer; ++f) setParam(lfoP(n, f), paramDef(lfoP(n, f)).def);
        setParam(lfoP(n, kLfOn), 1.f);
        m_.lfoShapes.reset(n);
        m_.lfoShapes.snap[(size_t)n] = 0;
        m_.lfoShapes.grp[(size_t)r] = 0;
        return;
    }
}

void Editor::removeRackLfo(int n) {
    if (n < 2) return;
    m_.routes.removeSource(n);
    m_.routes.removeTarget(lfoP(n, 0), lfoP(n, 0) + (uint32_t)kLfFieldsPer);
    for (int f = 0; f < kLfFieldsPer; ++f) setParam(lfoP(n, f), paramDef(lfoP(n, f)).def);
    setParam(lfoP(n, kLfOn), 0.f);
    m_.lfoShapes.reset(n);
    m_.lfoShapes.grp[(size_t)n - 2] = 0;
}

// MODULES, SIDE B : rack modulaire (jusqu'à 6 LFO, deux colonnes, défilement à la molette)
void Editor::drawRack() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    std::vector<int> act;
    for (int r = 0; r < kNumRackLfos; ++r)
        if (P(lfoP(2 + r, kLfOn)) > 0.5f) act.push_back(2 + r);
    const bool canAdd = act.size() < (size_t)kNumRackLfos;
    const int cells = (int)act.size() + (canAdd ? 1 : 0);
    const Rect view{24.f, 115.f, 1232.f, 821.f};
    const float rowH = 358.5f + 12.f;
    const float content = (float)((cells + 1) / 2) * rowH - 12.f;
    if (ui_.hit(view) && ui_.in().wheel != 0.f && !dd_.on) rackScroll_ -= ui_.in().wheel * 60.f;
    rackScroll_ = std::clamp(rackScroll_, 0.f, std::max(0.f, content - view.h));
    c.save();
    c.clip(Rect{view.x - 4.f, view.y, view.w + 8.f, view.h + 2.f});
    for (int i = 0; i < cells; ++i) {
        const Rect cell{24.f + (float)(i % 2) * 622.f, 115.f + (float)(i / 2) * rowH - rackScroll_, 610.f, 358.5f};
        if (cell.b() < view.y - 4.f || cell.y > view.b()) continue;
        if (i < (int)act.size()) {
            lfoCard(act[(size_t)i], cell, true);
        } else {
            const Rect add{cell.x, cell.y, cell.w, 150.f};
            const Ui::Interact it = ui_.interact(makeId("rkAdd"), add);
            if (it.clicked) addRackLfo();
            const Pt q[5] = {{add.x + 1, add.y + 1}, {add.r() - 1, add.y + 1}, {add.r() - 1, add.b() - 1}, {add.x + 1, add.b() - 1}, {add.x + 1, add.y + 1}};
            c.dashedPolyline(q, 5, T.rule, 2.f, 6.f, 4.f);
            ui_.textCenter("[ + AJOUTER UN LFO ]", Rect{add.x, add.y + 67.f, add.w, 16.f}, Font{Face::Mono, 12.f, 1.68f}, it.hovered ? T.ink : T.muted);
        }
    }
    c.restore();
}

// anneaux des routes libres d'un potard : arc de dosage (orange) et point de la valeur modulée
void Editor::knobRings(uint32_t id, Pt p, float size) {
    Canvas& c = ui_.c();
    const FreeRoutes& R = m_.routes;
    const int nr = R.count();
    const float s = size / 84.f;
    const Pt ctr{p.x + 42.f * s, p.y + 42.f * s};
    const float f = toNormalized(id, P(id));
    float fe = f;
    bool any = false;
    auto ang = [](float ff) { return (135.f + 270.f * ff) * 3.14159265f / 180.f; };
    for (int i = 0; i < nr; ++i) {
        if ((uint32_t)R.r[(size_t)i].id.load() != id) continue;
        any = true;
        const int src = R.r[(size_t)i].src.load();
        const float amt = (float)R.r[(size_t)i].amt.load() / 100.f;
        const float f2 = std::clamp(f + amt, 0.f, 1.f), lo = std::min(f, f2), hi = std::max(f, f2);
        if (hi - lo > 0.004f) c.arc(ctr, 31.f * s, ang(lo), ang(hi), kOrange, 3.f * s);
        if (src >= 0 && src < kNumLfos) fe += amt * m_.readouts.lfoVal[(size_t)src].load();
        else if (src >= kRouteSrcMacro && src < kRouteSrcVel) fe += amt * P(kMacro1 + (uint32_t)(src - kRouteSrcMacro));
        else if (src == kRouteSrcWheel) fe += amt * P(kModWheel);
    }
    if (!any) return;
    const float a = ang(std::clamp(fe, 0.f, 1.f));
    c.fillEllipse(Pt{ctr.x + 31.f * s * std::cos(a), ctr.y + 31.f * s * std::sin(a)}, 2.5f * s, 2.5f * s, kOrangePale);
}

// Glisser-déposer en cours : potard visé (cadre clignotant), étiquette sous la souris ; au
// relâchement, la route est créée. Réglage du dosage par l'anneau.
void Editor::dragOverlay() {
    Canvas& c = ui_.c();
    const Input& in = ui_.in();
    const Font bf{Face::Mono, 11.f, 1.1f};
    std::string badge;
    Pt at = in.mouse;
    if (rd_.route >= 0) {
        FreeRoutes& R = m_.routes;
        if (rd_.route < R.count() && in.down) {
            const int amt = std::clamp(rd_.a0 + (int)std::lround(rd_.y0 - in.mouse.y), -100, 100);
            R.r[(size_t)rd_.route].amt.store(amt);
            const int src = R.r[(size_t)rd_.route].src.load();
            badge = std::string(routeSrcName(src)) + " \xE2\x86\x92 " + upper(paramDef(rd_.id).name) + "   " + (amt > 0 ? "+" : "") + std::to_string(amt) + " %";
        } else {
            rd_ = RingDrag{};
        }
    }
    if (dd_.on) {
        const bool hov = dd_.hov != 0xFFFFFFFFu && ddAllowed();
        badge = std::string("[ ") + routeSrcName(dd_.src) + " ]" + (hov ? std::string("  \xE2\x86\x92  ") + upper(paramDef(dd_.hov).name) : std::string());
        if (hov) {
            const bool pulse = (int)(now_ / 200.0) % 2 == 0;
            const Rect h = dd_.hovRect.inset(-3.f);
            c.fillRect(h.inset(-4.f), kOrange.withAlpha(pulse ? 0.16f : 0.06f));
            ui_.border(h, kOrange.withAlpha(pulse ? 1.f : 0.45f), 2.f);
        }
        if (!in.down) {
            if (hov) m_.routes.add(dd_.src, dd_.hov, 50);
            dd_ = DragMod{};
        }
    }
    if (badge.empty()) return;
    const float w = ui_.width(badge, bf) + 18.f;
    const Rect r{std::min(at.x + 14.f, (float)kWidth - w - 4.f), at.y + 14.f, w, 21.f};
    c.fillRect(r.inset(-3.f), kOrange.withAlpha(0.12f));
    c.fillRect(r, Color{20.f / 255.f, 20.f / 255.f, 20.f / 255.f, 0.72f});
    ui_.border(r, kOrange);
    ui_.text(badge, r.x + 9.f, r.y + 4.f, bf, kOrange);
}

} // namespace raw2::ui
