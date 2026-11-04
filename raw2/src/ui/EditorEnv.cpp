// Page ENV / LFO (maquette mise à jour) : une enveloppe ADSR par oscillateur (nœuds déplaçables,
// courbe, RETRIG / FREE, liaison A <-> B) et deux LFO (forme, vitesse libre ou calée sur le
// tempo, phase, fade in, lissage), avec leur point de lecture en direct.
#include "ui/Editor.h"

#include <algorithm>
#include <cmath>

namespace raw2::ui {

namespace {

const uint32_t kEnvIds[2][5] = {{kAttack, kDecay, kSustain, kRelease, kEnvACurve},
                                {kEnvBAttack, kEnvBDecay, kEnvBSustain, kEnvBRelease, kEnvBCurve}};
const float kCurves[4] = {0.5f, 0.8f, 0.2f, 1.f};  // LIN, EXP, LOG, EXTR

std::string timeTxt(float s) {
    if (s < 1.f) return fmt("%d ms", std::max(1, (int)std::lround(s * 1000.f)));
    return fmt("%.2f s", s);
}

// géométrie du graphe d'enveloppe (repère 640 x 200 de la maquette)
struct EnvGeo {
    float va, vd, vs, vr, c;
    float x0 = 30.f, yT = 22.f, yB = 170.f, h = 148.f, wS = 80.f, K = 510.f;
    float rA, rD, rR, wA, wD, wR, q;
    float ta, td, tr;
};
EnvGeo envGeo(const ParamStore& P, int i) {
    EnvGeo g;
    g.va = P.get(kEnvIds[i][0]);
    g.vd = P.get(kEnvIds[i][1]);
    g.vs = P.get(kEnvIds[i][2]);
    g.vr = P.get(kEnvIds[i][3]);
    g.c = P.get(kEnvIds[i][4]);
    g.rA = 8.f + 100.f * g.va;
    g.rD = 8.f + 100.f * g.vd;
    g.rR = 8.f + 100.f * g.vr;
    const float sc = g.K / (g.rA + g.rD + g.rR);
    g.wA = sc * g.rA;
    g.wD = sc * g.rD;
    g.wR = sc * g.rR;
    g.q = envCurveQ(g.c);
    g.ta = attackSeconds(g.va);
    g.td = decaySeconds(g.vd);
    g.tr = releaseSeconds(g.vr);
    return g;
}

} // namespace

// petit bouton entre crochets de la page ENV / LFO (police 9 px, ombre dure)
bool Editor::envBtn(Id id, float right, float top, const std::string& label, bool on, float* outW) {
    const Theme& T = *T_;
    ButtonStyle s;
    s.bg = on ? T.ink : T.btnBg;
    s.fg = on ? T.card : T.ink;
    s.border = T.ink;
    s.font = Font{Face::Mono, 9.f, 0.54f};
    s.pressed = on;
    const float w = std::round(ui_.width(label, s.font) + 16.f);
    if (outW) *outW = w;
    return ui_.button(id, Rect{right - w, top, w, 24.f}, label, s);
}

// colonne bouton moleté 84 px + nom + afficheur (comme la maquette)
void Editor::envKnob(uint32_t id, float cx, float top, const std::string& label, const std::string& value) {
    const Theme& T = *T_;
    knobParam(id, Pt{cx - 42.f, top}, 84.f, 0.f);
    ui_.textCenter(label, Rect{cx - 60.f, top + 86.f, 120.f, 13.f}, Font{Face::Mono, 10.f, 1.2f}, T.muted);
    ui_.lcd(value, cx, top + 105.f, true);
}

void Editor::envCard(int i, const Rect& card) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ParamStore& P = m_.params;
    ui_.card(card);
    ui_.text(i == 0 ? "ENV \xC2\xB7 OSC A" : "ENV \xC2\xB7 OSC B", card.x + 15.f, card.y + 17.f, Font{Face::Pixel, 16.f, 1.28f}, T.ink, 16.f);
    c.fillRect(Rect{card.x + 15.f, card.y + 47.f, card.w - 30.f, 1.f}, T.rule);
    const bool linked = P.get(kEnvLink) > 0.5f;

    // boutons de l'en-tête, alignés à droite
    {
        float right = card.r() - 15.f, w = 0.f;
        const uint32_t curveId = kEnvIds[i][4], modeId = i == 0 ? kEnvAMode : kEnvBMode;
        const std::string cn = envCurveName(P.get(curveId));
        if (envBtn(makeId("envCurve", i), right, card.y + 13.f, "[ COURBE : " + cn + " ]", false, &w)) {
            int n = -1;
            for (int k = 0; k < 4; ++k)
                if (std::fabs(P.get(curveId) - kCurves[k]) < 0.04f) n = k;
            const float nv = kCurves[(n + 1) % 4];
            setParam(curveId, nv);
            if (linked) setParam(kEnvIds[1 - i][4], nv);
        }
        right -= w + 6.f;
        const bool free = P.get(modeId) > 0.5f;
        if (envBtn(makeId("envMode", i), right, card.y + 13.f, free ? "[ FREE ]" : "[ RETRIG ]", free, &w)) setParam(modeId, free ? 0.f : 1.f);
        right -= w + 6.f;
        if (i == 1) {
            if (envBtn(makeId("envCopy"), right, card.y + 13.f, "[ COPIER A\xE2\x86\x92" "B ]", false, &w))
                for (int k = 0; k < 5; ++k) setParam(kEnvIds[1][k], P.get(kEnvIds[0][k]));
            right -= w + 6.f;
            if (envBtn(makeId("envLink"), right, card.y + 13.f, "[ LIER A\xE2\x86\x94" "B ]", linked, &w)) {
                setParam(kEnvLink, linked ? 0.f : 1.f);
                if (!linked)
                    for (int k = 0; k < 5; ++k) setParam(kEnvIds[1][k], P.get(kEnvIds[0][k]));
            }
        }
    }

    // écran : courbe ADSR, nœuds déplaçables, point de lecture
    const Rect scr{card.x + 15.f, card.y + 56.f, card.w - 30.f, 224.5f};
    scanScreen(scr);
    const float s = (scr.w - 2.f) / 640.f;
    const float ox = scr.x + 1.f, oy = scr.y + 1.f + (scr.h - 2.f - 200.f * s) * 0.5f;
    auto V = [&](float x, float y) { return Pt{ox + x * s, oy + y * s}; };
    const Color col = i == 0 ? T.waveA : T.waveB;
    const Rect area{ox, oy, 640.f * s, 200.f * s};

    // glisser les nœuds (formules de la maquette)
    {
        const Ui::Interact it = ui_.interact(makeId("envGraph", i), area, Cursor::Cross);
        const Pt m = ui_.in().mouse;
        const float vx = (m.x - ox) / s, vy = (m.y - oy) / s;
        const EnvGeo g = envGeo(P, i);
        if (it.pressed) {
            const float xA = g.x0 + g.wA, xD = xA + g.wD, xS = xD + g.wS, ys = g.yB - g.vs * g.h;
            const float hp[3][2] = {{xA, g.yT}, {xD, ys}, {xS, ys}};
            float bd = 1e9f;
            envHandle_ = 0;
            for (int n = 0; n < 3; ++n) {
                const float d = (hp[n][0] - vx) * (hp[n][0] - vx) + (hp[n][1] - vy) * (hp[n][1] - vy);
                if (d < bd) { bd = d; envHandle_ = n; }
            }
            for (int k = 0; k < 4; ++k) beginDrag(kEnvIds[i][k]);
            envDragging_ = i;
        }
        if ((it.pressed || it.held) && envDragging_ == i) {
            auto cl = [](float v) { return std::clamp(v, 0.f, 1.f); };
            auto cr = [&](float r) { return cl((r - 8.f) / 100.f); };
            auto put = [&](int k, float v) {
                dragParam(kEnvIds[i][k], v);
                if (linked) setParam(kEnvIds[1 - i][k], v);
            };
            if (envHandle_ == 0) {
                const float u = std::clamp((vx - g.x0) / g.K, 0.03f, 0.9f);
                put(0, cr(u / (1.f - u) * (g.rD + g.rR)));
            } else if (envHandle_ == 1) {
                const float u = std::clamp((vx - g.x0) / g.K, 0.05f, 0.95f);
                put(1, cr(u / (1.f - u) * g.rR - g.rA));
                put(2, cl((g.yB - vy) / g.h));
            } else {
                const float u = std::clamp((vx - g.wS - g.x0) / g.K, 0.05f, 0.95f);
                put(3, cr((g.rA + g.rD) * (1.f - u) / u));
                put(2, cl((g.yB - vy) / g.h));
            }
        }
        if (it.released && envDragging_ == i) {
            for (int k = 0; k < 4; ++k) endDrag(kEnvIds[i][k]);
            envDragging_ = -1;
        }
    }

    const EnvGeo g = envGeo(P, i);
    const float xA = g.x0 + g.wA, xD = xA + g.wD, xS = xD + g.wS, xR = xS + g.wR;
    auto Y = [&](float lv) { return g.yB - lv * g.h; };
    c.save();
    c.clip(scr.inset(1.f));
    for (float v : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) c.line(V(g.x0, Y(v)), V(620.f, Y(v)), T.scrGrid, 1.f);
    for (float x = g.x0; x <= 620.f; x += 40.f) c.line(V(x, g.yT), V(x, g.yB), T.scrGrid, 1.f);
    std::vector<Pt> L;
    constexpr int N = 24;
    for (int n = 0; n <= N; ++n) {
        const float u = (float)n / N;
        L.push_back(V(g.x0 + u * g.wA, Y(std::pow(u, 1.f / g.q))));
    }
    for (int n = 1; n <= N; ++n) {
        const float u = (float)n / N;
        L.push_back(V(xA + u * g.wD, Y(g.vs + (1.f - g.vs) * std::pow(1.f - u, g.q))));
    }
    L.push_back(V(xS, Y(g.vs)));
    for (int n = 1; n <= N; ++n) {
        const float u = (float)n / N;
        L.push_back(V(xS + u * g.wR, Y(g.vs * std::pow(1.f - u, g.q))));
    }
    std::vector<Pt> poly = L;
    poly.push_back(V(xR, g.yB));
    poly.push_back(V(g.x0, g.yB));
    c.fillPolygon(poly.data(), (int)poly.size(), col.withAlpha(0.1f));
    if (i == 1) glowLine(L, col, 2.f * s);
    else {
        c.polyline(L.data(), (int)L.size(), col.withAlpha(0.18f), 6.f * s);
        c.polyline(L.data(), (int)L.size(), col, 2.f * s);
    }
    for (const Pt& h : {V(xA, g.yT), V(xD, Y(g.vs)), V(xS, Y(g.vs))}) {
        c.fillEllipse(h, 6.f * s, 6.f * s, T.scr);
        c.strokeEllipse(h, 6.f * s, 6.f * s, col, 2.f * s);
    }
    c.line(V(30.f, g.yB), V(620.f, g.yB), col.withAlpha(0.5f), 1.f);
    c.fillEllipse(V(xR, g.yB), 3.5f * s, 3.5f * s, col);
    // point de lecture (voix la plus récente)
    {
        const auto& R = m_.readouts;
        const int st = R.envStage[(size_t)i].load();
        const float t = R.envT[(size_t)i].load(), lv = R.envLv[(size_t)i].load();
        if (st >= 0 && R.voices.load() > 0) {
            float x = g.x0;
            if (st == 0) x = g.x0 + std::min(1.f, t / g.ta) * g.wA;
            else if (st == 1) x = xA + std::min(1.f, t / g.td) * g.wD;
            else if (st == 2) x = xD + std::min(1.f, std::max(0.f, t - g.td) / 1.5f) * g.wS;
            else x = xS + std::min(1.f, t / g.tr) * g.wR;
            c.fillEllipse(V(x, Y(lv)), 5.f * s, 5.f * s, T.hl);
        }
    }
    const Font lf{Face::Mono, 10.f * s, 1.f * s}, af{Face::Mono, 9.f * s, 0.f};
    const char* letters[4] = {"A", "D", "S", "R"};
    const float lx[4] = {g.x0 + g.wA / 2.f, xA + g.wD / 2.f, xD + g.wS / 2.f, xS + g.wR / 2.f};
    for (int k = 0; k < 4; ++k) {
        const Pt p = V(lx[k], 192.f);
        c.text(letters[k], p.x - ui_.width(letters[k], lf) / 2.f, p.y, lf, T.scrMuted);
    }
    c.text("100", V(3.f, 26.f).x, V(3.f, 26.f).y, af, T.scrMuted);
    c.text("50", V(3.f, 100.f).x, V(3.f, 100.f).y, af, T.scrMuted);
    c.text("0", V(3.f, 174.f).x, V(3.f, 174.f).y, af, T.scrMuted);
    c.restore();

    // boutons ATTACK / DECAY / SUSTAIN / RELEASE
    const char* labels[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
    const std::string vals[4] = {timeTxt(g.ta), timeTxt(g.td), fmt("%d %%", (int)std::lround(g.vs * 100.f)), timeTxt(g.tr)};
    for (int k = 0; k < 4; ++k) envKnob(kEnvIds[i][k], card.x + 15.f + 72.5f + (float)k * 145.f, card.y + 288.5f, labels[k], vals[k]);
}

void Editor::drawEnv() {
    // LIER A <-> B : un réglage fait par un bouton moleté est recopié sur l'autre enveloppe
    if (m_.params.get(kEnvLink) > 0.5f) {
        for (int k = 0; k < 5; ++k) {
            const float a = P(kEnvIds[0][k]), b = P(kEnvIds[1][k]);
            if (std::fabs(a - b) < 1e-6f) { envLast_[k] = a; continue; }
            if (std::fabs(a - envLast_[k]) > 1e-6f) setParam(kEnvIds[1][k], a);
            else setParam(kEnvIds[0][k], b);
            envLast_[k] = P(kEnvIds[0][k]);
        }
    }
    envCard(0, Rect{24.f, 73.f, 610.f, 425.5f});
    lfoCard(0, Rect{646.f, 73.f, 610.f, 425.5f}, false);
    envCard(1, Rect{24.f, 510.5f, 610.f, 425.5f});
    lfoCard(1, Rect{646.f, 510.5f, 610.f, 425.5f}, false);
}

} // namespace raw2::ui
