// Page JEU : façon de jouer (POLY / MONO / LEGATO, glide, unison), 808 et sous-oscillateur, filtre
// 24 dB, vélocité, fondu A <-> B, 4 macros à poignée [+ MOD] et sources de jeu, arpégiateur et accords.
#include "ui/Editor.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace raw2::ui {

namespace {
const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
std::string noteName(int n) { return n < 0 ? std::string("--") : std::string(kNoteNames[n % 12]) + std::to_string(n / 12 - 1); }
} // namespace

// rangée de boutons à choix unique (valeurs 0..n-1 d'un paramètre à crans)
void Editor::choiceRow(uint32_t id, const char* const* labels, int n, float x, float y, float w, float h, int base) {
    const int cur = (int)std::lround(P(id)) - base;
    const float bw = (w - (float)(n - 1) * 6.f) / (float)n;
    for (int k = 0; k < n; ++k) {
        ButtonStyle s = ui_.miniStyle(k == cur);
        s.font = Font{Face::Mono, bw < 60.f ? 9.f : 10.f, bw < 60.f ? 0.3f : 0.7f};
        if (ui_.button(makeId("choice", (int)id * 32 + k), Rect{x + (float)k * (bw + 6.f), y, bw, h}, labels[k], s)) setParam(id, (float)(k + base));
    }
}

// Grille de la page : trois colonnes de 403 px (24, 438.5, 853), trois rangées. Dans chaque carte
// de la première rangée, les mêmes étages aux mêmes hauteurs : bandeau de choix, écran, potards.
namespace {
constexpr float kColX[3] = {24.f, 438.5f, 853.f};
constexpr float kColW = 403.f, kPad = 15.f;
constexpr float kRow1 = 73.f, kRow1H = 318.f;        // VOIX · 808 · FILTRE
constexpr float kRow2 = 403.f, kRow2H = 206.f;       // MACROS
constexpr float kRow3 = 621.f, kRow3H = 315.f;       // ARPÈGE · ACCORDS
constexpr float kStripLbl = 58.f, kStrip = 74.f, kStripH = 30.f;  // décalages dans la carte
constexpr float kScr = 118.f, kScrH = 88.f, kKnobs = 216.f, kKnob = 52.f;
} // namespace

void Editor::drawJeu() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const Font lf{Face::Mono, 10.f, 1.2f};
    const Font sf{Face::Mono, 9.f, 0.f};
    auto knobRow = [&](float x0, float top, const uint32_t* ids, int n) {
        const float w = kColW - 2.f * kPad;
        for (int k = 0; k < n; ++k) {
            const float cx = x0 + w * ((float)k + 0.5f) / (float)n;
            std::string label;
            switch (ids[k]) {
                case kGlide: label = "GLIDE"; break;
                case kUnison: label = "UNISON"; break;
                case kUniDetune: label = "D\xC3\x89SACCORD"; break;
                case kUniWidth: label = "LARGEUR"; break;
                case kPunch: label = "PUNCH"; break;
                case kPunchTime: label = "DUR\xC3\x89" "E"; break;
                case kSubLvl: label = "SUB"; break;
                case kFiltDrive: label = "DRIVE"; break;
                case kVelAmt: label = "V\xC3\x89L \xE2\x86\x92 NIVEAU"; break;
                case kVelCut: label = "V\xC3\x89L \xE2\x86\x92 FILTRE"; break;
                case kBlend: label = "BLEND A\xE2\x86\x94" "B"; break;
                default: break;
            }
            knobCol(ids[k], cx, top, kKnob, label, formatParam(ids[k], P(ids[k])), 0.f, false);
        }
    };
    auto stripLabel = [&](const char* s, float x, float cardY) { ui_.text(s, x, cardY + kStripLbl, lf, T.muted); };

    // ================= VOIX =================
    {
        const float X = kColX[0], x0 = X + kPad, w = kColW - 2.f * kPad;
        const Rect card{X, kRow1, kColW, kRow1H};
        ui_.card(card);
        ui_.cardHeader(card, "VOIX", false, 14.f, 12.f);
        const int U = std::clamp((int)std::lround(P(kUnison)), 1, 16);
        ui_.tag(U > 1 ? fmt("32 VOIX \xC2\xB7 UNISON \xC3\x97%d", U) : std::string("32 VOIX"), card.r() - 15.f, kRow1 + 13.f, Ui::Tag::Line);
        stripLabel("MODE", x0, kRow1);
        static const char* modes[3] = {"POLY", "MONO", "LEGATO"};
        choiceRow(kPolyMode, modes, 3, x0, kRow1 + kStrip, 226.f, kStripH);
        stripLabel("GLIDE", x0 + 238.f, kRow1);
        static const char* gm[2] = {"TOUJ.", "LI\xC3\x89" "ES"};
        choiceRow(kGlideMode, gm, 2, x0 + 238.f, kRow1 + kStrip, w - 238.f, kStripH);
        // écran : éventail de l'unison (une barre par copie : désaccord en hauteur, largeur en stéréo)
        const Rect scr{x0, kRow1 + kScr, w, kScrH};
        scanScreen(scr);
        c.dotGrid(scr.inset(1.f), scr.x + 1.f, scr.y + 1.f, 12.f, 1.1f, T.scrGrid);
        c.line(Pt{scr.x + 22.f, scr.cy()}, Pt{scr.r() - 22.f, scr.cy()}, T.scrMid, 1.f);
        c.line(Pt{scr.cx(), scr.y + 8.f}, Pt{scr.cx(), scr.b() - 8.f}, T.scrMid, 1.f);
        for (int u = 0; u < U; ++u) {
            const float sp = U > 1 ? 2.f * (float)u / (float)(U - 1) - 1.f : 0.f;
            const float x = scr.cx() + sp * P(kUniWidth) * (scr.w * 0.40f);
            const float y = scr.cy() - sp * P(kUniDetune) * (scr.h * 0.32f);
            c.fillRect(Rect{x - 2.f, y - 11.f, 4.f, 22.f}, U > 1 ? T.waveA : T.acc);
        }
        ui_.text("L", scr.x + 8.f, scr.cy() - 6.f, sf, T.scrMuted);
        ui_.textRight("R", scr.r() - 8.f, scr.cy() - 6.f, sf, T.scrMuted);
        const int pm = (int)std::lround(P(kPolyMode));
        ui_.text(pm == 0 ? "POLY \xC2\xB7 32 VOIX" : pm == 1 ? "MONO \xC2\xB7 DERNI\xC3\x88RE NOTE" : "LEGATO \xC2\xB7 ENVELOPPE LI\xC3\x89" "E",
                 scr.x + 8.f, scr.y + 6.f, sf, T.scrText);
        if (P(kGlide) > 0.001f)
            ui_.textRight("GLIDE " + formatParam(kGlide, P(kGlide)), scr.r() - 8.f, scr.y + 6.f, sf, T.hl);
        const uint32_t ids[4] = {kGlide, kUnison, kUniDetune, kUniWidth};
        knobRow(x0, kRow1 + kKnobs, ids, 4);
    }

    // ================= 808 · SUB =================
    {
        const float X = kColX[1], x0 = X + kPad, w = kColW - 2.f * kPad;
        const Rect card{X, kRow1, kColW, kRow1H};
        ui_.card(card);
        ui_.cardHeader(card, "808 \xC2\xB7 SUB", false, 14.f, 12.f);
        ui_.tag("punch + sous-oscillateur", card.r() - 15.f, kRow1 + 13.f, Ui::Tag::Line);
        stripLabel("SUB OCTAVE", x0, kRow1);
        static const char* so[2] = {"0 OCT", "-1 OCT"};
        choiceRow(kSubOct, so, 2, x0, kRow1 + kStrip, 170.f, kStripH);
        stripLabel("RACCOURCI", x0 + 182.f, kRow1);
        {
            ButtonStyle s = ui_.keyStyle();
            s.font = Font{Face::Mono, 10.f, 0.7f};
            // réglage de départ d'une basse 808 : jeu lié avec glide, punch, sub une octave dessous
            if (ui_.button(makeId("jeu808"), Rect{x0 + 182.f, kRow1 + kStrip, w - 184.f, kStripH}, "\xE2\x86\x92 BASSE 808", s)) {
                setParam(kPolyMode, 2.f);
                setParam(kGlideMode, 1.f);
                if (P(kGlide) < 0.05f) setParam(kGlide, 0.3f);
                if (P(kPunch) < 0.05f) setParam(kPunch, 0.25f);
                setParam(kSubOct, 1.f);
                if (P(kSubLvl) < 0.05f) setParam(kSubLvl, 0.35f);
            }
        }
        // écran : chute de hauteur du punch (demi-tons au-dessus de la note) sur 500 ms
        const Rect scr{x0, kRow1 + kScr, w, kScrH};
        scanScreen(scr);
        c.dotGrid(scr.inset(1.f), scr.x + 1.f, scr.y + 1.f, 12.f, 1.1f, T.scrGrid);
        const float pst = P(kPunch) * 36.f, tau = punchSeconds(P(kPunchTime));
        std::vector<Pt> pts;
        const Rect plot{scr.x + 10.f, scr.y + 22.f, scr.w - 20.f, scr.h - 34.f};
        for (int i = 0; i <= 80; ++i) {
            const float t = (float)i / 80.f * 0.5f;
            const float st = pst * std::exp(-t / tau);
            pts.push_back(Pt{plot.x + plot.w * (float)i / 80.f, plot.b() - plot.h * std::min(1.f, st / 36.f)});
        }
        c.line(Pt{plot.x, plot.b()}, Pt{plot.r(), plot.b()}, T.scrMid, 1.f);
        glowLine(pts, T.waveA, 1.8f);
        ui_.text(fmt("HAUTEUR +%.0f ST", pst), scr.x + 8.f, scr.y + 6.f, sf, T.scrText);
        ui_.textRight("0 \xE2\x86\x92 500 ms", scr.r() - 8.f, scr.y + 6.f, sf, T.scrMuted);
        const uint32_t ids[3] = {kPunch, kPunchTime, kSubLvl};
        knobRow(x0, kRow1 + kKnobs, ids, 3);
    }

    // ================= FILTRE · VÉLOCITÉ =================
    {
        const float X = kColX[2], x0 = X + kPad, w = kColW - 2.f * kPad;
        const Rect card{X, kRow1, kColW, kRow1H};
        ui_.card(card);
        ui_.cardHeader(card, "FILTRE \xC2\xB7 V\xC3\x89LOCIT\xC3\x89", false, 14.f, 12.f);
        const float fc = cutoffToHz(P(kCutoff));
        ui_.tag(fc >= 1000.f ? fmt("coupure %.1f kHz", fc / 1000.f) : fmt("coupure %.0f Hz", fc), card.r() - 15.f, kRow1 + 13.f, Ui::Tag::Line);
        stripLabel("PENTE", x0, kRow1);
        static const char* sl[2] = {"12 dB", "24 dB"};
        choiceRow(kFiltSlope, sl, 2, x0, kRow1 + kStrip, 170.f, kStripH);
        ui_.text("filtre principal", x0 + 184.f, kRow1 + kStrip + 4.f, sf, T.muted);
        ui_.text("(coupure : page SON)", x0 + 184.f, kRow1 + kStrip + 17.f, sf, T.muted);
        // écran : réponse du filtre (20 Hz à 20 kHz, -48 à +18 dB)
        const Rect scr{x0, kRow1 + kScr, w, kScrH};
        scanScreen(scr);
        c.dotGrid(scr.inset(1.f), scr.x + 1.f, scr.y + 1.f, 12.f, 1.1f, T.scrGrid);
        const Rect plot{scr.x + 8.f, scr.y + 20.f, scr.w - 16.f, scr.h - 26.f};
        const bool s24 = P(kFiltSlope) > 0.5f;
        const float q = 0.707f + P(kReso) * 6.f;
        auto mag2 = [](float x, float qq) { const float a = 1.f - x * x, b = x / qq; return 1.f / (a * a + b * b); };
        std::vector<Pt> rp;
        for (int i = 0; i <= 120; ++i) {
            const float f = 20.f * std::pow(1000.f, (float)i / 120.f), x = f / fc;
            const float m2 = s24 ? mag2(x, 0.541f) * mag2(x, std::max(1.307f, q)) : mag2(x, q);
            const float db = std::clamp(10.f * std::log10(m2 + 1e-12f), -48.f, 18.f);
            rp.push_back(Pt{plot.x + plot.w * (float)i / 120.f, plot.y + plot.h * (18.f - db) / 66.f});
        }
        c.line(Pt{plot.x, plot.y + plot.h * 18.f / 66.f}, Pt{plot.r(), plot.y + plot.h * 18.f / 66.f}, T.scrMid, 1.f);
        glowLine(rp, T.waveA, 1.8f);
        ui_.text(s24 ? "24 dB / OCT" : "12 dB / OCT", scr.x + 8.f, scr.y + 6.f, sf, T.scrText);
        ui_.textRight(fmt("R\xC3\x89SO %.0f %%", P(kReso) * 100.f), scr.r() - 8.f, scr.y + 6.f, sf, T.scrMuted);
        const uint32_t ids[4] = {kFiltDrive, kVelAmt, kVelCut, kBlend};
        knobRow(x0, kRow1 + kKnobs, ids, 4);
    }

    // ================= MACROS =================
    {
        const Rect card{24.f, kRow2, 1232.f, kRow2H};
        ui_.card(card);
        ui_.cardHeader(card, "MACROS", false, 14.f, 12.f);
        ui_.tag("glisser une poign\xC3\xA9" "e [+ MOD] sur un potard", card.r() - 15.f, kRow2 + 13.f, Ui::Tag::Line);
        auto routesOf = [&](int src) {
            int nr = 0;
            for (int i = 0; i < m_.routes.count(); ++i) nr += m_.routes.r[(size_t)i].src.load() == src;
            return nr;
        };
        auto routeText = [](int nr) { return nr ? fmt("%d route%s", nr, nr > 1 ? "s" : "") : std::string("aucune route"); };
        const float zoneW = 860.f;  // 4 colonnes de 215 px, puis les sources de jeu
        for (int k = 0; k < kNumMacros; ++k) {
            const float cx = 39.f + zoneW * ((float)k + 0.5f) / 4.f;
            const uint32_t id = kMacro1 + (uint32_t)k;
            knobCol(id, cx, kRow2 + 50.f, 68.f, fmt("MACRO %d", k + 1), fmt("%.0f %%", P(id) * 100.f), 0.f, false);
            modHandle(makeId("macroMod", k), Rect{cx - 38.f, kRow2 + 158.f, 76.f, 22.f}, kRouteSrcMacro + k);
            const int nr = routesOf(kRouteSrcMacro + k);
            ui_.textCenter(routeText(nr), Rect{cx - 70.f, kRow2 + 184.f, 140.f, 12.f}, sf, nr ? T.ink : T.muted);
            if (k) c.fillRect(Rect{39.f + zoneW * (float)k / 4.f, kRow2 + 62.f, 1.f, 120.f}, T.rule.withAlpha(0.25f));
        }
        // sources de jeu : vélocité et molette, glissables comme les macros
        const float sx = 39.f + zoneW + 15.f;
        c.fillRect(Rect{sx, kRow2 + 52.f, 1.f, kRow2H - 67.f}, T.rule);
        ui_.text("SOURCES DE JEU", sx + 16.f, kRow2 + 58.f, lf, T.muted);
        struct Src { const char* name; const char* sub; int src; float val; };
        const Src srcs[2] = {{"V\xC3\x89LOCIT\xC3\x89", "force de frappe", kRouteSrcVel, -1.f},
                             {"MOLETTE MOD", "molette du clavier (CC 1)", kRouteSrcWheel, P(kModWheel)}};
        for (int k = 0; k < 2; ++k) {
            const float y = kRow2 + 82.f + (float)k * 58.f;
            modHandle(makeId(k ? "wheelMod" : "velMod", 0), Rect{sx + 16.f, y, 76.f, 22.f}, srcs[k].src);
            ui_.text(srcs[k].name, sx + 106.f, y + 1.f, Font{Face::MonoBold, 11.f, 0.6f}, T.ink);
            ui_.text(srcs[k].sub, sx + 106.f, y + 17.f, sf, T.muted);
            const int nr = routesOf(srcs[k].src);
            ui_.textRight(routeText(nr), card.r() - 15.f, y + 1.f, sf, nr ? T.ink : T.muted);
            if (srcs[k].val >= 0.f) {  // niveau de la molette : 12 cases
                const int on = std::clamp((int)std::lround(srcs[k].val * 12.f), 0, 12);
                for (int q = 0; q < 12; ++q) c.fillRect(Rect{card.r() - 15.f - 72.f + (float)q * 6.f, y + 19.f, 4.f, 4.f}, q < on ? T.ptr : T.tickOff);
            }
        }
    }

    // ================= ARPÈGE =================
    {
        const Rect card{24.f, kRow3, kColX[1] + kColW - 24.f, kRow3H};
        const float x0 = card.x + kPad;
        ui_.card(card);
        ui_.cardHeader(card, "ARP\xC3\x88" "GE", false, 14.f, 12.f);
        const bool on = P(kArpOn) > 0.5f;
        {
            ButtonStyle s = ui_.keyStyle();
            s.font = Font{Face::MonoBold, 11.f, 0.9f};
            if (on) { s.bg = T.acc; s.fg = T.onAcc; }
            s.pressed = on;
            if (ui_.button(makeId("arpOn"), Rect{card.r() - 15.f - 196.f, kRow3 + 9.f, 100.f, 28.f}, on ? "ARP ON" : "ARP OFF", s)) setParam(kArpOn, on ? 0.f : 1.f);
            ButtonStyle ls = ui_.miniStyle(P(kArpLatch) > 0.5f);
            if (ui_.button(makeId("arpLatch"), Rect{card.r() - 15.f - 88.f, kRow3 + 9.f, 88.f, 28.f}, "LATCH", ls)) setParam(kArpLatch, P(kArpLatch) > 0.5f ? 0.f : 1.f);
        }
        // trois rangées de choix, étiquettes à gauche
        const float lx = x0, bx = x0 + 86.f, bw = 430.f;
        static const char* am[5] = {"HAUT", "BAS", "HAUT/BAS", "HASARD", "JOU\xC3\x89"};
        static const char* ar[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
        static const char* ao[4] = {"1", "2", "3", "4"};
        const float ry[3] = {kRow3 + 60.f, kRow3 + 98.f, kRow3 + 136.f};
        ui_.text("MODE", lx, ry[0] + 8.f, lf, T.muted);
        choiceRow(kArpMode, am, 5, bx, ry[0], bw, 28.f);
        ui_.text("VITESSE", lx, ry[1] + 8.f, lf, T.muted);
        choiceRow(kArpRate, ar, 6, bx, ry[1], bw, 28.f);
        ui_.text("OCTAVES", lx, ry[2] + 8.f, lf, T.muted);
        choiceRow(kArpOct, ao, 4, bx, ry[2], 4.f * 54.f, 28.f, 1);
        // gate et swing, alignés sur les rangées
        const float kx = bx + bw + 24.f, kw = card.r() - kPad - kx;
        knobCol(kArpGate, kx + kw * 0.25f, kRow3 + 60.f, kKnob, "GATE", formatParam(kArpGate, P(kArpGate)), 0.f, false);
        knobCol(kArpSwing, kx + kw * 0.75f, kRow3 + 60.f, kKnob, "SWING", formatParam(kArpSwing, P(kArpSwing)), 0.f, false);
        // écran : 16 pas, pas joué allumé, résumé et note jouée
        const Rect scr{x0, kRow3 + 182.f, card.w - 2.f * kPad, kRow3H - 182.f - kPad};
        scanScreen(scr);
        const int stp = m_.readouts.arpStep.load(), nt = m_.readouts.arpNote.load();
        const std::string sum = std::string(arpModeName((int)std::lround(P(kArpMode)))) + " \xC2\xB7 " + arpRateName((int)std::lround(P(kArpRate))) +
                                " \xC2\xB7 " + fmt("%d OCT", (int)std::lround(P(kArpOct))) + " \xC2\xB7 GATE " + formatParam(kArpGate, P(kArpGate));
        ui_.text(on ? sum : std::string("ARP OFF \xC2\xB7 joue un accord : l'arp\xC3\xA8ge suit le tempo de l'h\xC3\xB4te"), scr.x + 10.f, scr.y + 8.f, sf,
                 on ? T.scrText : T.scrMuted);
        ui_.textRight(on && stp >= 0 ? "NOTE " + noteName(nt) : std::string("--"), scr.r() - 10.f, scr.y + 8.f, Font{Face::MonoBold, 10.f, 0.4f},
                      on ? T.hl : T.scrMuted);
        const float cw = (scr.w - 20.f - 15.f * 6.f) / 16.f;
        for (int k = 0; k < 16; ++k) {
            const Rect cell{scr.x + 10.f + (float)k * (cw + 6.f), scr.y + 40.f, cw, 34.f};
            const bool lit = on && k == stp % 16;
            if (lit) c.fillRect(cell.inset(-3.f), T.acc.withAlpha(0.25f));
            c.fillRect(cell, lit ? T.acc : on ? T.scrMid : T.scrGrid);
            if (k % 4 == 0) c.fillRect(Rect{cell.x, cell.y - 6.f, cell.w, 2.f}, on ? T.scrMuted : T.scrGrid);  // temps forts
            ui_.textCenter(std::to_string(k + 1), Rect{cell.x, cell.b() + 6.f, cell.w, 12.f}, Font{Face::Mono, 9.f, 0.f}, lit ? T.hl : T.scrMuted);
        }
    }

    // ================= ACCORDS =================
    {
        const float X = kColX[2], x0 = X + kPad, w = kColW - 2.f * kPad;
        const Rect card{X, kRow3, kColW, kRow3H};
        ui_.card(card);
        ui_.cardHeader(card, "ACCORDS", false, 14.f, 12.f);
        ui_.tag("une touche = un accord", card.r() - 15.f, kRow3 + 13.f, Ui::Tag::Line);
        const int cur = std::clamp((int)std::lround(P(kChord)), 0, kNumChords - 1);
        // écran : l'accord sur deux octaves à partir de C
        const Rect scr{x0, kRow3 + 58.f, w, 86.f};
        scanScreen(scr);
        const int8_t* iv = chordIntervals(cur);
        bool inChord[25] = {};
        std::string names;
        for (int i = 0; i < 6 && iv[i] >= 0; ++i) {
            if (iv[i] <= 24) inChord[iv[i]] = true;
            names += (names.empty() ? "" : " ") + std::string(kNoteNames[iv[i] % 12]);
        }
        ui_.text(cur ? std::string("C ") + chordName(cur) : std::string("ACCORD OFF \xC2\xB7 une note"), scr.x + 8.f, scr.y + 6.f, sf, T.scrText);
        ui_.textRight(cur ? names : std::string("C"), scr.r() - 8.f, scr.y + 6.f, sf, T.hl);
        {
            const Rect kb{scr.x + 8.f, scr.y + 24.f, scr.w - 16.f, scr.h - 32.f};
            const int WK[7] = {0, 2, 4, 5, 7, 9, 11};
            const float kw = kb.w / 15.f;
            for (int wk = 0; wk < 15; ++wk) {
                const int n = (wk / 7) * 12 + WK[wk % 7];
                const Rect r{kb.x + (float)wk * kw + 1.f, kb.y, kw - 2.f, kb.h};
                const bool lit = n <= 24 && inChord[n];
                c.fillRect(r, lit ? T.acc : T.scrText.withAlpha(0.16f));
                if (lit) c.fillRect(Rect{r.x, r.b() - 4.f, r.w, 4.f}, T.hl);
            }
            const int BS[5] = {0, 1, 3, 4, 5};
            for (int o = 0; o < 2; ++o)
                for (int b = 0; b < 5; ++b) {
                    const int slot = o * 7 + BS[b];
                    const int n = o * 12 + WK[BS[b]] + 1;
                    const Rect r{kb.x + (float)(slot + 1) * kw - kw * 0.3f, kb.y, kw * 0.6f, kb.h * 0.58f};
                    c.fillRect(r.inset(-2.f), T.scr);
                    c.fillRect(r, inChord[n] ? T.acc : T.scrText.withAlpha(0.05f));
                }
        }
        // 11 boutons : deux rangées de 4, une rangée de 3
        const float gy = kRow3 + 156.f, gh = 28.f, gg = 8.f;
        for (int k = 0; k < kNumChords; ++k) {
            const int row = k / 4, col = k % 4, nRow = row < 2 ? 4 : 3;
            const float bw = (w - (float)(nRow - 1) * gg) / (float)nRow;
            const Rect r{x0 + (float)col * (bw + gg), gy + (float)row * (gh + gg), bw, gh};
            ButtonStyle s = ui_.miniStyle(k == cur);
            s.font = Font{Face::Mono, 10.f, 0.6f};
            if (ui_.button(makeId("chord", k), r, chordName(k), s)) setParam(kChord, (float)k);
        }
        // strum : glissière horizontale
        {
            const float y = gy + 3.f * (gh + gg) + 4.f;
            ui_.text("STRUM", x0, y + 3.f, lf, T.muted);
            const Rect tr{x0 + 60.f, y, w - 60.f - 66.f, 18.f};
            const Ui::Interact it = ui_.interact(makeId("strum"), tr, Cursor::ResizeEW);
            if (it.pressed) beginDrag(kChordStrum);
            if (it.held) dragParam(kChordStrum, std::clamp((ui_.in().mouse.x - tr.x) / tr.w, 0.f, 1.f));
            if (it.released) endDrag(kChordStrum);
            ui_.box(tr, T.railBg, T.rule);
            c.fillRect(Rect{tr.x + 1.f, tr.y + 1.f, (tr.w - 2.f) * std::clamp(P(kChordStrum), 0.f, 1.f), tr.h - 2.f}, T.acc);
            ui_.lcd(formatParam(kChordStrum, P(kChordStrum)), card.r() - kPad - 28.f, y);
        }
    }
}

} // namespace raw2::ui
