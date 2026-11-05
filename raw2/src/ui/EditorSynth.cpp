// Page SYNTH : filtre multimode (10 types, 12 / 24 dB, morph, mix, suivi du clavier), modes de warp
// des deux oscillateurs (FM / AM / RM croisées), oscillateur de bruit, unison jusqu'à 16 voix,
// expression (MPE, pression, timbre) avec leurs poignées [+ MOD].
#include "ui/Editor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

namespace raw2::ui {

namespace {
constexpr float kPadS = 15.f;
constexpr float kR1 = 73.f, kR1H = 440.f;   // FILTRE · OSCILLATEURS
constexpr float kR2 = 525.f, kR2H = 411.f;  // BRUIT · UNISON · EXPRESSION
using cd = std::complex<double>;

// Réponse (module) du filtre de voix à la fréquence f : prototypes analogiques des étages du moteur
double filterMag(int type, double f, double fc, float reso, bool s24, float morph, float mix) {
    const double k = 2.0 - 1.94 * reso;
    auto svf = [&](double fcut, double kk, int t) {
        const cd s(0.0, f / fcut);
        const cd D = s * s + kk * s + 1.0;
        const cd lp = 1.0 / D, hp = s * s / D, bp = kk * s / D;
        switch (t) {
            case kFtHp: return hp;
            case kFtBp: return bp;
            case kFtNotch: return lp + hp;
            case kFtPeak: return lp - hp;
            case kFtMorph: return morph < 0.5f ? lp + (bp - lp) * (2.0 * morph) : bp + (hp - bp) * (2.0 * morph - 1.0);
            default: return lp;
        }
    };
    cd H;
    switch (type) {
        case kFtPhase: {
            const int n = s24 ? 4 : 2;
            const double spread = 0.25 + 1.5 * morph;
            cd ap(1.0, 0.0);
            for (int i = 0; i < n; ++i) {
                const double fi = fc * std::exp2(((double)i - 0.5 * (n - 1)) * spread);
                const cd s(0.0, f / fi);
                ap *= (s * s - k * s + 1.0) / (s * s + k * s + 1.0);
            }
            H = 0.5 * (1.0 + ap);
            break;
        }
        case kFtCombP: case kFtCombN: {
            const double fb = (type == kFtCombP ? 1.0 : -1.0) * (0.2 + 0.77 * reso) * (1.0 - 0.6 * morph);
            const cd z = std::polar(1.0, -2.0 * 3.14159265358979 * f / fc);
            H = (1.0 - 0.5 * std::fabs(fb)) / (1.0 - fb * z);
            break;
        }
        case kFtFormant: {
            static const double F[5][3] = {{600, 1040, 2250}, {400, 1620, 2400}, {250, 1750, 2600}, {400, 750, 2400}, {350, 600, 2400}};
            static const double A[5][3] = {{1, 0.447, 0.355}, {1, 0.251, 0.355}, {1, 0.0316, 0.158}, {1, 0.282, 0.0891}, {1, 0.1, 0.0251}};
            const double p = std::clamp((double)morph, 0.0, 1.0) * 4.0;
            const int i0 = std::min(3, (int)p);
            const double fr = p - i0, shift = std::clamp(std::sqrt(fc / 632.0), 0.3, 3.0), kf = 2.0 / (5.0 + 15.0 * reso);
            H = 0.0;
            for (int b = 0; b < 3; ++b) {
                const double fb = std::exp(std::log(F[i0][b]) + (std::log(F[i0 + 1][b]) - std::log(F[i0][b])) * fr) * shift;
                const double a = A[i0][b] + (A[i0 + 1][b] - A[i0][b]) * fr;
                const cd s(0.0, f / fb);
                H += a * kf * s / (s * s + kf * s + 1.0);
            }
            H *= 1.6;
            break;
        }
        default:
            H = s24 ? svf(fc, 1.848, type) * svf(fc, k * 0.3825, type) : svf(fc, k, type);
    }
    H = 1.0 + (H - 1.0) * (double)mix;
    return std::abs(H);
}
} // namespace

void Editor::drawSynth() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const Font lf{Face::Mono, 10.f, 1.2f};
    const Font sf{Face::Mono, 9.f, 0.f};
    auto routesOf = [&](int src) {
        int nr = 0;
        for (int i = 0; i < m_.routes.count(); ++i) nr += m_.routes.r[(size_t)i].src.load() == src;
        return nr;
    };
    auto routeText = [](int nr) { return nr ? fmt("%d route%s", nr, nr > 1 ? "s" : "") : std::string("aucune route"); };
    auto knobs = [&](float x0, float w, float top, const uint32_t* ids, const char* const* names, int n) {
        for (int k = 0; k < n; ++k) {
            const float cx = x0 + w * ((float)k + 0.5f) / (float)n;
            knobCol(ids[k], cx, top, 52.f, names[k], formatParam(ids[k], P(ids[k])), 0.f, false);
        }
    };
    // grille d'un choix (n boutons sur rows rangées)
    auto grid = [&](uint32_t id, int n, int cols, float x, float y, float w, float h, auto&& name) {
        const int cur = (int)std::lround(P(id));
        const float bw = (w - (float)(cols - 1) * 5.f) / (float)cols;
        for (int k = 0; k < n; ++k) {
            const Rect r{x + (float)(k % cols) * (bw + 5.f), y + (float)(k / cols) * (h + 5.f), bw, h};
            ButtonStyle s = ui_.miniStyle(k == cur);
            s.font = Font{Face::Mono, bw < 70.f ? 8.5f : 9.5f, bw < 70.f ? 0.2f : 0.5f};
            if (ui_.button(makeId("synGrid", (int)id * 32 + k), r, name(k), s)) setParam(id, (float)k);
        }
    };

    // ================= FILTRE =================
    {
        const Rect card{24.f, kR1, 610.f, kR1H};
        const float x0 = card.x + kPadS, w = card.w - 2.f * kPadS;
        ui_.card(card);
        ui_.cardHeader(card, "FILTRE", false, 14.f, 12.f);
        static const char* sl[2] = {"12 dB", "24 dB"};
        choiceRow(kFiltSlope, sl, 2, card.r() - kPadS - 150.f, kR1 + 11.f, 150.f, 26.f);
        const int ft = std::clamp((int)std::lround(P(kFiltType)), 0, kNumFiltTypes - 1);
        grid(kFiltType, kNumFiltTypes, 5, x0, kR1 + 58.f, w, 28.f, [](int k) { return filtTypeName(k); });
        // écran : réponse en fréquence (20 Hz .. 20 kHz, -36 .. +18 dB)
        const Rect scr{x0, kR1 + 126.f, w, 158.f};
        scanScreen(scr);
        const Rect plot = scr.inset(10.f);
        for (double hz : {100.0, 1000.0, 10000.0}) {
            const float x = plot.x + plot.w * (float)(std::log(hz / 20.0) / std::log(1000.0));
            c.line(Pt{x, plot.y}, Pt{x, plot.b()}, T.scrGrid, 1.f);
            ui_.text(hz >= 1000.0 ? fmt("%.0fk", hz / 1000.0) : fmt("%.0f", hz), x + 3.f, plot.b() - 11.f, sf, T.scrMuted);
        }
        auto yDb = [&](double db) { return plot.y + plot.h * (float)((18.0 - std::clamp(db, -36.0, 18.0)) / 54.0); };
        c.line(Pt{plot.x, yDb(0.0)}, Pt{plot.r(), yDb(0.0)}, T.scrMid, 1.f);
        const double fc = cutoffToHz(P(kCutoff));
        std::vector<Pt> pts;
        for (int i = 0; i <= 200; ++i) {
            const double f = 20.0 * std::pow(1000.0, i / 200.0);
            const double mag = filterMag(ft, f, fc, P(kReso), P(kFiltSlope) > 0.5f, P(kFiltMorph), P(kFiltMix));
            pts.push_back(Pt{plot.x + plot.w * (float)i / 200.f, yDb(20.0 * std::log10(mag + 1e-9))});
        }
        glowLine(pts, T.waveA, 1.8f);
        ui_.text(std::string(filtTypeName(ft)) + (ft <= kFtPeak || ft == kFtMorph || ft == kFtPhase ? (P(kFiltSlope) > 0.5f ? " \xC2\xB7 24 dB" : " \xC2\xB7 12 dB") : ""),
                 scr.x + 8.f, scr.y + 6.f, sf, T.scrText);
        static const char* morphRole[kNumFiltTypes] = {"", "", "", "", "", "MORPH = \xC3\xA9" "cart", "MORPH = amorti", "MORPH = amorti",
                                                        "MORPH = voyelle A E I O U", "MORPH = LP \xE2\x86\x92 BP \xE2\x86\x92 HP"};
        ui_.textRight(std::string(morphRole[ft]) + (morphRole[ft][0] ? "  \xC2\xB7  " : "") + formatParam(kCutoff, P(kCutoff)), scr.r() - 8.f, scr.y + 6.f, sf,
                      T.hl);
        static const uint32_t ids[6] = {kCutoff, kReso, kFiltDrive, kFiltMorph, kFiltMix, kFiltKey};
        static const char* nm[6] = {"CUTOFF", "R\xC3\x89SO", "DRIVE", "MORPH", "MIX", "CLAVIER"};
        knobs(x0, w, kR1 + 300.f, ids, nm, 6);
    }

    // ================= OSCILLATEURS · WARP =================
    {
        const Rect card{646.f, kR1, 610.f, kR1H};
        ui_.card(card);
        ui_.cardHeader(card, "OSCILLATEURS \xC2\xB7 WARP", false, 14.f, 12.f);
        ui_.tag("12 modes \xC2\xB7 FM / AM / RM crois\xC3\xA9" "es", card.r() - 15.f, kR1 + 13.f, Ui::Tag::Line);
        const float hw = (card.w - 2.f * kPadS - 20.f) / 2.f;
        for (int osc = 0; osc < 2; ++osc) {
            const float x0 = card.x + kPadS + (float)osc * (hw + 20.f);
            const uint32_t idM = osc ? kBWarpMode : kAWarpMode, idW = osc ? kBWarp : kAWarp, idP = osc ? kBPos : kAPos;
            const int wm = std::clamp((int)std::lround(P(idM)), 0, kNumWarpModes - 1);
            ui_.text(osc ? "OSC B" : "OSC A", x0, kR1 + 58.f, Font{Face::MonoBold, 11.f, 0.8f}, osc ? T.acc : T.ink);
            // source de l'oscillateur : table d'onde (synthé) ou instrument réel, pour mêler les deux
            {
                const uint32_t idPlay = osc ? kBPlay : kAPlay;
                const bool inst = P(idPlay) > 0.5f && !m_.instruments.name(osc).empty();
                ButtonStyle ta = ui_.miniStyle(!inst), ti = ui_.miniStyle(inst);
                ta.font = ti.font = Font{Face::Mono, 9.f, 0.4f};
                const std::string iname = inst ? ui_.fit(upper(m_.instruments.name(osc)), ti.font, 118.f) : std::string("INSTRUMENT \xE2\x96\xBE");
                if (ui_.button(makeId("synSrc", osc * 2), Rect{x0 + hw - 210.f, kR1 + 52.f, 66.f, 22.f}, "TABLE", ta)) setParam(idPlay, 0.f);
                if (ui_.button(makeId("synSrc", osc * 2 + 1), Rect{x0 + hw - 140.f, kR1 + 52.f, 140.f, 22.f}, iname, ti)) {
                    instOpen_ = true;
                    instOsc_ = osc;
                    m_.instruments.scan();
                }
            }
            grid(idM, kNumWarpModes, 4, x0, kR1 + 80.f, hw, 24.f, [osc](int k) { return warpModeName(k, osc); });
            if (osc == 1) c.fillRect(Rect{x0 - 10.5f, kR1 + 58.f, 1.f, kR1H - 74.f}, T.rule.withAlpha(0.35f));
            // écran : un cycle de la table à sa position, déformé par le mode (l'autre oscillateur
            // vu comme un sinus pour FM / AM / RM)
            const Rect scr{x0, kR1 + 170.f, hw, 130.f};
            scanScreen(scr);
            const Rect a = scr.inset(8.f);
            c.line(Pt{a.x, a.cy()}, Pt{a.r(), a.cy()}, T.scrMid, 1.f);
            {
                std::vector<Pt> pts, ref;
                const float wv = P(idW), pos = P(idP);
                std::lock_guard<std::mutex> lk(m_.tableMutex);
                const Wavetable& tab = m_.edit[(size_t)osc];
                for (int i = 0; i <= 160; ++i) {
                    const float u0 = (float)i / 160.f * 0.99999f;
                    const float other = std::sin(6.2831853f * u0);
                    const float u = warpPhasePreview(u0, wm, wv, other);
                    const float v = warpAmpPreview(sampleTable(tab, pos, u), u, wm, wv, other);
                    pts.push_back(Pt{a.x + a.w * (float)i / 160.f, a.cy() - std::clamp(v, -1.2f, 1.2f) * a.h * 0.42f});
                    ref.push_back(Pt{a.x + a.w * (float)i / 160.f, a.cy() - sampleTable(tab, pos, u0) * a.h * 0.42f});
                }
                c.dashedPolyline(ref.data(), (int)ref.size(), T.scrMuted, 1.f, 2.f, 3.f);
                glowLine(pts, osc ? T.waveB : T.waveA, 1.6f);
            }
            ui_.text(fmt("WARP %.0f %%", P(idW) * 100.f), scr.x + 8.f, scr.y + 6.f, sf, T.scrText);
            const uint32_t ids[2] = {idW, idP};
            const char* nm[2] = {"WARP", "POSITION"};
            knobs(x0, hw, kR1 + 316.f, ids, nm, 2);
        }
    }

    // ================= BRUIT =================
    {
        const Rect card{24.f, kR2, 397.f, kR2H};
        const float x0 = card.x + kPadS, w = card.w - 2.f * kPadS;
        ui_.card(card);
        ui_.cardHeader(card, "BRUIT", false, 14.f, 12.f);
        ui_.tag("avant le filtre \xC2\xB7 env. A", card.r() - 15.f, kR2 + 13.f, Ui::Tag::Line);
        static const char* nt[4] = {"BLANC", "ROSE", "CRAQUE", "NUM\xC3\x89R."};
        choiceRow(kNoiseType, nt, 4, x0, kR2 + 58.f, w, 28.f);
        // écran : allure du bruit (couleur appliquée), niveau
        const Rect scr{x0, kR2 + 100.f, w, 150.f};
        scanScreen(scr);
        {
            const Rect a = scr.inset(8.f);
            const int type = (int)std::lround(P(kNoiseType));
            const float col = P(kNoiseColor), lvl = P(kNoiseLvl);
            uint32_t r = 0x2545F491u;
            auto rnd = [&]() {
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                return (float)(r & 0xFFFFFF) / 8388608.f - 1.f;
            };
            float lp = 0.f, b0 = 0.f, b1 = 0.f, b2 = 0.f, ck = 0.f, hold = 0.f;
            const float coef = col < 0.5f ? 0.03f + 0.97f * std::pow(col * 2.f, 2.f) : 0.02f + 0.6f * (1.f - (col - 0.5f) * 2.f);
            std::vector<Pt> pts;
            for (int i = 0; i <= 220; ++i) {
                float x = rnd();
                if (type == 1) { b0 = 0.99765f * b0 + x * 0.099f; b1 = 0.963f * b1 + x * 0.2965f; b2 = 0.57f * b2 + x * 1.0527f; x = (b0 + b1 + b2 + x * 0.1848f) * 0.25f; }
                else if (type == 2) { ck *= 0.55f; if (std::fabs(rnd()) > 0.97f) ck += rnd() * 1.6f; x = ck; }
                else if (type == 3) { if (i % 9 == 0) hold = rnd(); x = hold; }
                if (std::fabs(col - 0.5f) >= 0.01f) {
                    lp += (x - lp) * coef;
                    x = col < 0.5f ? lp : x - lp;
                }
                pts.push_back(Pt{a.x + a.w * (float)i / 220.f, a.cy() - std::clamp(x, -1.2f, 1.2f) * a.h * 0.4f * (0.25f + 0.75f * lvl)});
            }
            glowLine(pts, lvl > 0.001f ? T.waveA : T.scrMuted, 1.2f);
        }
        ui_.text(P(kNoiseLvl) > 0.001f ? std::string(noiseTypeName((int)std::lround(P(kNoiseType)))) + " \xC2\xB7 " + formatParam(kNoiseColor, P(kNoiseColor))
                                       : std::string("BRUIT COUP\xC3\x89 \xC2\xB7 monte le NIVEAU"),
                 scr.x + 8.f, scr.y + 6.f, sf, P(kNoiseLvl) > 0.001f ? T.scrText : T.scrMuted);
        static const uint32_t ids[2] = {kNoiseLvl, kNoiseColor};
        static const char* nm[2] = {"NIVEAU", "COULEUR"};
        knobs(x0, w, kR2 + 266.f, ids, nm, 2);
    }

    // ================= UNISON =================
    {
        const Rect card{433.f, kR2, 397.f, kR2H};
        const float x0 = card.x + kPadS, w = card.w - 2.f * kPadS;
        ui_.card(card);
        ui_.cardHeader(card, "UNISON", false, 14.f, 12.f);
        const int U = std::clamp((int)std::lround(P(kUnison)), 1, 16);
        ui_.tag(U > 1 ? fmt("%d copies par note \xC2\xB7 32 voix", U) : std::string("32 voix"), card.r() - 15.f, kR2 + 13.f, Ui::Tag::Line);
        ui_.text("\xC3\x89" "CART MAX", x0, kR2 + 66.f, lf, T.muted);
        static const char* ur[4] = {"1 ST", "2 ST", "QUINTE", "OCTAVE"};
        choiceRow(kUniRange, ur, 4, x0 + 92.f, kR2 + 58.f, w - 92.f, 28.f);
        // écran : éventail des copies (hauteur = désaccord, largeur = stéréo, épaisseur = blend)
        const Rect scr{x0, kR2 + 100.f, w, 150.f};
        scanScreen(scr);
        c.dotGrid(scr.inset(1.f), scr.x + 1.f, scr.y + 1.f, 12.f, 1.1f, T.scrGrid);
        c.line(Pt{scr.x + 20.f, scr.cy()}, Pt{scr.r() - 20.f, scr.cy()}, T.scrMid, 1.f);
        c.line(Pt{scr.cx(), scr.y + 22.f}, Pt{scr.cx(), scr.b() - 8.f}, T.scrMid, 1.f);
        {
            const float b = P(kUniBlend);
            const int nC = U == 1 ? 1 : (U % 2 ? 1 : 2);
            for (int u = 0; u < U; ++u) {
                const float sp = U > 1 ? 2.f * (float)u / (float)(U - 1) - 1.f : 0.f;
                const bool center = U == 1 || (U % 2 ? u == (U - 1) / 2 : (u == U / 2 - 1 || u == U / 2));
                const float g = center ? 1.f : b;
                const float x = scr.cx() + sp * P(kUniWidth) * (scr.w * 0.42f);
                const float y = scr.cy() - sp * P(kUniDetune) * (scr.h * 0.34f);
                const float hh = 6.f + 22.f * g;
                c.fillRect(Rect{x - 2.f, y - hh * 0.5f, 4.f, hh}, center ? T.acc : T.waveA.withAlpha(0.35f + 0.65f * g));
            }
            (void)nC;
        }
        ui_.text(fmt("%d COPIE%s \xC2\xB7 \xC2\xB1", U, U > 1 ? "S" : "") + fmt("%.0f ct", P(kUniDetune) * 50.f * uniRangeSemis((int)std::lround(P(kUniRange)))),
                 scr.x + 8.f, scr.y + 6.f, sf, T.scrText);
        static const uint32_t ids[4] = {kUnison, kUniDetune, kUniBlend, kUniWidth};
        static const char* nm[4] = {"VOIX", "D\xC3\x89SACCORD", "BLEND", "LARGEUR"};
        knobs(x0, w, kR2 + 266.f, ids, nm, 4);
    }

    // ================= EXPRESSION · MPE =================
    {
        const Rect card{842.f, kR2, 414.f, kR2H};
        const float x0 = card.x + kPadS, w = card.w - 2.f * kPadS;
        ui_.card(card);
        ui_.cardHeader(card, "EXPRESSION", false, 14.f, 12.f);
        static const char* mp[2] = {"MPE OFF", "MPE ON"};
        choiceRow(kMpe, mp, 2, card.r() - kPadS - 170.f, kR2 + 11.f, 170.f, 26.f);
        const auto& R = m_.readouts;
        struct Src { const char* name; const char* sub; int src; float val; };
        const Src srcs[4] = {{"PRESSION", "aftertouch / pression MPE", kRouteSrcPress, R.press.load()},
                             {"TIMBRE", "glissé MPE \xC2\xB7 CC 74", kRouteSrcTimbre, R.timbre.load()},
                             {"CLAVIER", "hauteur de la note", kRouteSrcKey, -1.f},
                             {"AL\xC3\x89" "A", "une valeur par note", kRouteSrcRnd, -1.f}};
        for (int k = 0; k < 4; ++k) {
            const float y = kR2 + 58.f + (float)k * 46.f;
            modHandle(makeId("exprMod", k), Rect{x0, y, 76.f, 22.f}, srcs[k].src);
            ui_.text(srcs[k].name, x0 + 90.f, y + 1.f, Font{Face::MonoBold, 11.f, 0.6f}, T.ink);
            ui_.text(srcs[k].sub, x0 + 90.f, y + 17.f, sf, T.muted);
            const int nr = routesOf(srcs[k].src);
            ui_.textRight(routeText(nr), card.r() - kPadS, y + 1.f, sf, nr ? T.ink : T.muted);
            if (srcs[k].val >= 0.f) {  // niveau en direct : 12 cases
                const int on = std::clamp((int)std::lround(srcs[k].val * 12.f), 0, 12);
                for (int q = 0; q < 12; ++q) c.fillRect(Rect{card.r() - kPadS - 72.f + (float)q * 6.f, y + 19.f, 4.f, 4.f}, q < on ? T.ptr : T.tickOff);
            }
            if (k < 3) c.fillRect(Rect{x0, y + 38.f, w, 1.f}, T.rule.withAlpha(0.25f));
        }
        const float nb = R.noteBend.load();
        ui_.text(P(kMpe) > 0.5f ? fmt("MPE : un canal par note \xC2\xB7 hauteur de la note %+.2f st", nb)
                                : std::string("MPE OFF : pression = aftertouch du canal, timbre = CC 74"),
                 x0, kR2 + 246.f, sf, T.hl);
        static const uint32_t ids[2] = {kBendRange, kMpeRange};
        static const char* nm[2] = {"PITCH BEND", "BEND MPE"};
        knobs(x0, w, kR2 + 266.f, ids, nm, 2);
    }
}

} // namespace raw2::ui
