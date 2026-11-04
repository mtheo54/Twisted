// Page MIX IA : mesures réelles de la sortie (BS.1770, True Peak, corrélation par bande), courbe
// cible du rôle et du profil, excès de résonance, et actions correctives réglables. Chaque action
// est dosée d'après l'écoute (écart au gabarit du rôle, crête, dynamique) et, quand ses poids sont
// entraînés, d'après le petit réseau MixNet ; rien ne s'applique sans accord, tout se restaure.
#include "dsp/Mastering.h"
#include "ui/Editor.h"

#include <algorithm>
#include <cmath>

namespace raw2::ui {

namespace {

const Color kOr = Color::rgb8(255, 122, 26);
const char* kProfNames[5] = {"NEUTRE", "AGRESSIF", "CHAUD", "A\xC3\x89RIEN", "PROPRE"};
const char* kLog[5] = {"> lecture de la boucle \xC2\xB7 4 mesures", "> analyse spectrale 20 Hz\xE2\x80\x93" "20 kHz", "> mesure cr\xC3\xAAte / dynamique", "> comparaison au r\xC3\xB4le : ",
                       "> synth\xC3\xA8se des r\xC3\xA9sultats"};
const double kLogT[5] = {0, 900, 1900, 3000, 3900};
const double kScanMs = 4500.0;
constexpr float kDbPerRow = 66.f / 32.f;  // un cran du spectre

std::string cpPrefix(const std::string& s, size_t n) {
    size_t i = 0, k = 0;
    while (i < s.size() && k < n) {
        ++i;
        while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) ++i;
        ++k;
    }
    return s.substr(0, i);
}

// gabarit spectral (en crans 0..32) d'un rôle et d'un profil, comme la maquette
float tgtRows(int role, int prof, int i) {
    struct R { float b, t, c, a, w; };
    static const R rr[6] = {{17, 7, 14, 3, 6}, {24, 15, 6, 5, 4}, {16, 3, 27, 6, 6}, {18, 8, 16, 3, 8}, {15, 6, 22, 4, 6}, {11, 0, 34, 6, 8}};
    auto G = [&](float c, float w) { return std::exp(-0.5f * std::pow(((float)i - c) / w, 2.f)); };
    const R& r = rr[std::clamp(role, 0, 5)];
    float y = r.b - r.t * ((float)i / 47.f) + r.a * G(r.c, r.w);
    if (prof == 1) y += 2.5f * G(30, 6);
    else if (prof == 2) y += 1.5f * G(8, 6) - 2.f * ((float)i / 47.f);
    else if (prof == 3) y += 3.5f * G(41, 5);
    else if (prof == 4) y -= 1.5f * G(16, 5);
    return std::clamp(y * 0.66f, 2.f, 30.f);
}

// décalage (en crans) qui aligne le gabarit sur le niveau du spectre : moyenne en énergie, pour qu'un
// spectre clairsemé (une note tenue) ne passe pas pour une suite de résonances
float levelOffset(const float* rows, int role, int prof) {
    double a = 0.0, b = 0.0;
    for (int i = 3; i < 46; ++i) {
        a += std::pow(10.0, rows[i] * kDbPerRow / 10.0);
        b += std::pow(10.0, tgtRows(role, prof, i) * kDbPerRow / 10.0);
    }
    return (float)(10.0 * std::log10(a / b)) / kDbPerRow;
}

float barOf(float f) { return 47.f * std::log10(f / 20.f) / 3.f; }
float freqOf(float bar) { return 20.f * std::pow(1000.f, bar / 47.f); }

std::string fq(float f) {
    if (f >= 1000.f) return std::fmod(f, 1000.f) != 0.f ? fmt("%.1f kHz", f / 1000.f) : fmt("%.0f kHz", f / 1000.f);
    return fmt("%.0f Hz", f);
}

std::string actTitle(const EditorState::Act& a) {
    if (a.kind == 0) {
        std::string s = (a.type == kMixBell ? "BELL " : "SHELF ") + fmt("%+.1f dB @ ", a.g) + fq(a.f);
        if (a.type == kMixBell) s += fmt(" \xC2\xB7 Q %g", a.q);
        return s;
    }
    if (a.kind == 1) return fmt("COMPRESSEUR 2:1 \xC2\xB7 seuil %.0f dB", a.g);
    return fmt("LIMITEUR \xC2\xB7 plafond %.1f dBFS", a.g);
}

// sortie de MixNet correspondant à une action (-1 : aucune)
int netOf(const EditorState::Act& a) {
    if (a.kind == 1) return 1;
    if (a.kind == 2) return 5;
    if (a.type == kMixBell && a.f >= 160.f && a.f <= 400.f && a.g < 0.f) return 0;
    if (a.type != kMixBell && a.f >= 6000.f && a.g > 0.f) return 2;
    if (a.type == kMixBell && a.f <= 110.f && a.g > 0.f) return 3;
    if (a.type == kMixBell && a.f >= 2000.f && a.f <= 5000.f && a.g < 0.f) return 4;
    return -1;
}

// barre crantée (piste + segments 5/2 px + repère)
void meterBar(Ui& ui, Canvas& c, const Theme& T, const Rect& tr, float v, float mark, Color fill) {
    ui.box(tr, T.card, T.rule);
    const float fw = std::clamp(v, 0.f, 1.f) * (tr.w - 2.f);
    for (float x = 0; x < fw; x += 7.f) c.fillRect(Rect{tr.x + 1.f + x, tr.y + 1.f, std::min(5.f, fw - x), tr.h - 2.f}, fill);
    if (mark >= 0.f) c.fillRect(Rect{tr.x + 1.f + std::clamp(mark, 0.f, 1.f) * (tr.w - 2.f) - 1.f, tr.y - 2.f, 2.f, tr.h + 4.f}, T.ink);
}

void dashedCircle(Canvas& c, Pt o, float r, Color col, float on, float off) {
    Pt p[73];
    for (int k = 0; k <= 72; ++k) {
        const float a = (float)k / 72.f * 6.2831853f;
        p[k] = Pt{o.x + r * std::cos(a), o.y + r * std::sin(a)};
    }
    c.dashedPolyline(p, 73, col, 1.f, on, off);
}

} // namespace

// --- actions correctives ---------------------------------------------------------------------------
void Editor::genActs(bool measured) {
    const int role = st_.role == 0 ? (shownRole() == 3 ? 0 : shownRole()) : st_.role;
    const int prof = std::clamp(st_.mixProf, 0, 4);
    struct Tpl { int type; float f, g, q; const char* why; };
    static const Tpl TPL[6][3] = {
        {{kMixBell, 250, -2, 1.2f, "Accumulation dans les bas-m\xC3\xA9" "diums"}, {kMixHighShelf, 9000, 1.5f, 1, "Manque d'air"}, {kMixBell, 2800, -1.5f, 2, "Duret\xC3\xA9 de pr\xC3\xA9sence"}},
        {{kMixBell, 250, -2.5f, 1.2f, "Boue dans les bas-m\xC3\xA9" "diums"}, {kMixBell, 80, 2, 1, "Fondamentale en retrait"}, {kMixBell, 140, -1.5f, 3, "R\xC3\xA9sonance \xC3\xA9troite \xC3\xA0 140 Hz"}},
        {{kMixBell, 3500, -1.5f, 2, "Agressivit\xC3\xA9 du haut-m\xC3\xA9" "dium"}, {kMixHighShelf, 8000, 1.5f, 1, "Manque de brillance"}, {kMixBell, 400, -2, 1.5f, "Nasalit\xC3\xA9"}},
        {{kMixBell, 200, -2, 1, "Encombrement des bas-m\xC3\xA9" "diums"}, {kMixHighShelf, 10000, 2, 1, "Manque d'air"}, {kMixBell, 2000, -1, 1.5f, "M\xC3\xA9" "diums charg\xC3\xA9s"}},
        {{kMixBell, 500, -1.5f, 1.5f, "Creux boxy"}, {kMixBell, 5000, 1.5f, 1.5f, "Attaque en retrait"}, {kMixHighShelf, 12000, 1, 1, "Air manquant"}},
        {{kMixBell, 250, -2, 1, "Boue de texture"}, {kMixHighShelf, 6000, 2, 1, "Grain en retrait"}, {kMixBell, 1000, -2, 1, "M\xC3\xA9" "diums satur\xC3\xA9s"}}};
    static const Tpl EX[5] = {{-1, 0, 0, 0, ""},
                              {kMixBell, 3000, 1.5f, 1.2f, "Mordant du profil agressif"},
                              {kMixLowShelf, 120, 1.5f, 0.8f, "Chaleur dans le bas"},
                              {kMixHighShelf, 12000, 2.5f, 0.8f, "Air \xC3\xA9tendu"},
                              {kMixBell, 300, -2, 1.2f, "Nettoyage du profil propre"}};
    const float fk[5] = {0.8f, 1.3f, 1.f, 1.f, 1.1f};

    std::vector<EditorState::Act> list;
    auto push = [&](int kind, int type, float f, float g, float q, const std::string& why) {
        EditorState::Act a;
        a.kind = kind;
        a.type = type;
        a.f = f;
        a.g = g;
        a.q = q;
        a.why = why;
        a.uid = ++st_.actUid;
        list.push_back(a);
    };
    for (const Tpl& t : TPL[role]) push(0, t.type, t.f, std::round(t.g * fk[prof] * 2.f) / 2.f, t.q, t.why);
    if (EX[prof].type >= 0) push(0, EX[prof].type, EX[prof].f, EX[prof].g, EX[prof].q, EX[prof].why);

    const bool useMeas = measured && st_.scanFrames > 0 && !st_.scanLow && st_.scanAcc.size() == 48;
    if (useMeas) {
        // écart mesuré au gabarit, en dB, à niveau moyen égal (seule la forme du spectre compte)
        float dev[48], mean = 0.f;
        for (int i = 0; i < 48; ++i) {
            const float rows = std::clamp((float)(st_.scanAcc[(size_t)i] / st_.scanFrames + 72.0) / 66.f * 32.f, 1.f, 32.f);
            dev[i] = (rows - tgtRows(role, prof, i)) * kDbPerRow;
        }
        {
            float rows[48];
            for (int i = 0; i < 48; ++i) rows[i] = std::clamp((float)(st_.scanAcc[(size_t)i] / st_.scanFrames + 72.0) / 66.f * 32.f, 1.f, 32.f);
            mean = levelOffset(rows, role, prof) * kDbPerRow;
        }
        for (float& d : dev) d -= mean;
        auto around = [&](const EditorState::Act& a) {
            const float c = barOf(a.f);
            int i0, i1;
            if (a.type == kMixBell) {
                const float w = std::max(1.f, 4.71f * 0.9f / a.q);
                i0 = (int)std::floor(c - w);
                i1 = (int)std::ceil(c + w);
            } else if (a.type == kMixHighShelf) {
                i0 = (int)std::round(c);
                i1 = 46;
            } else {
                i0 = 2;
                i1 = (int)std::round(c);
            }
            i0 = std::clamp(i0, 0, 47);
            i1 = std::clamp(i1, i0, 47);
            float s = 0.f;
            for (int i = i0; i <= i1; ++i) s += dev[i];
            return s / (float)(i1 - i0 + 1);
        };
        for (auto& a : list) {
            const float d = around(a);
            const float need = std::clamp(-0.6f * d, -6.f, 6.f);  // on corrige un peu plus de la moitié de l'écart
            if (need * a.g <= 0.f || std::fabs(need) < 0.5f) {
                a.on = false;  // la mesure ne confirme pas ce besoin : proposée mais désactivée
                a.why += fmt(" \xC2\xB7 mesure %+.1f dB : d\xC3\xA9j\xC3\xA0 \xC3\xA9quilibr\xC3\xA9", d);
            } else {
                a.g = std::clamp(std::round((0.35f * a.g + 0.65f * need) * 2.f) / 2.f, -9.f, 9.f);
                a.why += fmt(" \xC2\xB7 mesure %+.1f dB", d);
            }
        }
        // excès de résonance : bande étroite nettement au-dessus de ses voisines et du gabarit
        int exI = -1;
        float exMax = 0.f;
        for (int i = 2; i < 46; ++i) {
            const float loc = dev[i] - 0.5f * (dev[i - 2] + dev[i + 2]);
            if (dev[i] > 4.f && loc > 3.f && dev[i] > exMax) { exMax = dev[i]; exI = i; }
        }
        if (exI >= 0) {
            const float f = std::round(freqOf((float)exI) / 10.f) * 10.f;
            push(0, kMixBell, f, -std::min(6.f, std::round(exMax * 0.5f * 2.f) / 2.f), 4.f, "R\xC3\xA9sonance mesur\xC3\xA9" "e \xC3\xA0 " + fq(f));
        }
    }
    // dynamique : le profil choisit, la mesure confirme et dose
    const bool wantComp = prof == 1 || prof == 2 || (useMeas && st_.scanDyn > 14.f);
    const bool wantLim = !wantComp || (useMeas && st_.scanPeak > -1.5f);
    if (wantComp && !m_.mix.comp.load()) {
        float thr = -18.f;
        std::string why = "Dynamique trop large sur le bus, attaque 20 ms";
        if (useMeas) {
            thr = std::clamp(std::round(st_.scanLufs + 4.f), -40.f, -6.f);
            why = fmt("Cr\xC3\xAAte / RMS %.1f dB \xC2\xB7 attaque 20 ms", st_.scanDyn);
        }
        push(1, 0, 0.f, thr, 0.f, why);
    }
    if (wantLim && !m_.mix.limit.load()) {
        std::string why = "Marge de cr\xC3\xAAte insuffisante avant export";
        if (useMeas) why = fmt("Vraie cr\xC3\xAAte mesur\xC3\xA9" "e %+.1f dBTP", st_.scanPeak);
        push(2, 0, 0.f, -1.f, 0.f, why);
    }
    // MixNet : confiance du réseau sur les corrections qu'il connaît
    if (useMeas && st_.mixScored)
        for (auto& a : list) {
            const int k = netOf(a);
            if (k < 0) continue;
            const float p = st_.mixProb[(size_t)k];
            a.why += fmt(" \xC2\xB7 r\xC3\xA9seau %d %%", (int)std::lround(p * 100.f));
            if (p < 0.2f) a.on = false;
        }
    st_.acts = std::move(list);
    actScroll_ = 0.f;
}

void Editor::applyAct(int i) {
    if (i < 0 || i >= (int)st_.acts.size()) return;
    EditorState::Act& a = st_.acts[(size_t)i];
    if (a.applied || !a.on) return;
    int kind = a.kind, slot = -1;
    if (a.kind == 0) {
        slot = m_.mix.addFilter(a.type, a.f, a.g, a.type == kMixBell ? a.q : 0.7f);
        if (slot < 0) {
            pushHist("12 filtres d\xC3\xA9j\xC3\xA0 actifs", "PLEIN", "MIX");
            return;
        }
    } else if (a.kind == 1) {
        m_.mix.compThreshDb = a.g;
        m_.mix.comp = true;
    } else {
        m_.mix.limitCeilDb = a.g;
        m_.mix.limit = true;
    }
    a.applied = true;
    a.slot = slot;
    pushHist(actTitle(a), "APPLIQU\xC3\x89", "IA", kind, slot, a.uid);
}

void Editor::restoreHist(int i) {
    if (i < 0 || i >= (int)st_.hist.size()) return;
    EditorState::Hist& h = st_.hist[(size_t)i];
    if (h.undoKind < 0) return;
    if (h.undoKind == 0 && h.undoSlot >= 0 && h.undoSlot < kMixFilterSlots) m_.mix.flt[(size_t)h.undoSlot].on = false;
    else if (h.undoKind == 1) m_.mix.comp = false;
    else if (h.undoKind == 2) m_.mix.limit = false;
    for (auto& a : st_.acts)
        if (a.uid == h.actUid) {
            a.applied = false;
            a.slot = -1;
        }
    h.undoKind = -1;
    h.status = "RESTAUR\xC3\x89";
}

// --- écoute ----------------------------------------------------------------------------------------
void Editor::startAnalysis() {
    if (st_.aiPhase == 1) return;
    st_.aiPhase = 1;
    st_.aiT0 = now_;
    st_.scanAcc.assign(48, 0.0);
    st_.scanFrames = 0;
    st_.scanPeak = -90.f;
    st_.scanDyn = 0.f;
    st_.scanLufs = -90.f;
    st_.scanLow = false;
    m_.meters.resetIntegrated = true;  // I et LRA décrivent le passage écouté
    mixSeg_.reset(m_.readouts.sampleRate.load());
    mixPos_ = m_.scope.position();
}

void Editor::finishAnalysis() {
    st_.aiPhase = 2;
    if (st_.scanFrames > 0) st_.scanDyn /= (float)st_.scanFrames;
    st_.scanLow = st_.scanLufs < -60.f;
    st_.mixScored = false;
    if (!st_.scanLow && ai::networksTrained()) {
        float x[ai::kSegDim];
        mixSeg_.compute(x);
        const ai::MixAdvice adv = ai::adviseMix(x);
        for (int i = 0; i < 6; ++i) st_.mixProb[(size_t)i] = adv.prob[i];
        st_.mixScored = true;
    }
    genActs(true);
}

void Editor::drawMix() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const MeterReadings mr = m_.meters.read();
    const bool sig = mr.shortTerm > -69.f;
    const int role = st_.role == 0 ? (shownRole() == 3 ? 0 : shownRole()) : st_.role;
    const int prof = std::clamp(st_.mixProf, 0, 4);
    if (st_.acts.empty() && st_.aiPhase != 1) genActs(st_.aiPhase == 2);
    if (mixMenu_ != 0 && ui_.in().pressed) {  // un clic hors du menu et de son bouton le referme
        const Pt p = ui_.in().mouse;
        const Rect menu = mixMenu_ == 1 ? Rect{889.f, 164.f, 150.f, 182.f} : Rect{1065.8f, 164.f, 150.f, 152.f};
        if (!menu.contains(p) && !Rect{889.f, 136.f, 326.8f, 25.f}.contains(p)) mixMenu_ = 0;
    }

    // --- écoute en cours ---
    const bool scanning = st_.aiPhase == 1;
    const double el = scanning ? now_ - st_.aiT0 : 1e9;
    mix_.resize(48, now_);
    an_.bands(48, bandTmp_);
    if (scanning) {
        if (st_.scanAcc.size() != 48) st_.scanAcc.assign(48, 0.0);
        for (int k = 0; k < 48; ++k) st_.scanAcc[(size_t)k] += bandTmp_[(size_t)k];
        ++st_.scanFrames;
        st_.scanPeak = std::max(st_.scanPeak, std::max(mr.truePeakL, mr.truePeakR));
        st_.scanDyn += sig ? mr.crest : 0.f;
        st_.scanLufs = std::max(st_.scanLufs, mr.shortTerm);
        {
            // échantillons arrivés depuis la dernière image -> descripteur du réseau
            const size_t pos = m_.scope.position();
            const size_t fresh = std::min(pos - mixPos_, ScopeRing::kSize / 2);
            mixPos_ = pos;
            if (fresh > 0) {
                std::vector<float> L(fresh), R(fresh);
                m_.scope.read(L.data(), R.data(), fresh);
                mixSeg_.push(L.data(), R.data(), fresh);
            }
        }
        if (el >= kScanMs) finishAnalysis();
    }
    const float prog = st_.aiPhase == 1 ? (float)std::min(1.0, el / kScanMs) : 0.f;

    // --- ANALYSEUR GLOBAL ---
    const Rect card{24.f, 73.f, 830.f, 863.f};
    ui_.card(card);
    ui_.cardHeader(card, "ANALYSEUR GLOBAL", false, 14.f, 12.f);
    ui_.tag("20 Hz \xE2\x80\x93 20 kHz", card.r() - 15.f, 86.f, Ui::Tag::Line);
    {
        const std::string vals[4] = {sig ? fmt("%.1f LUFS", mr.shortTerm) : std::string("-- LUFS"), sig ? fmt("%.1f dBFS", mr.samplePeak) : std::string("-- dBFS"),
                                     sig ? fmt("%.1f dB", mr.crest) : std::string("-- dB"), sig ? fmt("%+.2f", mr.corr) : std::string("--")};
        const char* labels[4] = {"NIVEAU", "CR\xC3\x8ATE", "DYNAMIQUE", "CORR\xC3\x89LATION"};
        for (int k = 0; k < 4; ++k) {
            const Rect r{39.f + (float)k * 202.5f, 132.f, 192.5f, 55.f};
            ui_.box(r, T.railBg, T.rule);
            ui_.text(labels[k], r.x + 13.f, r.y + 9.f, Font{Face::Mono, 10.f, 1.2f}, T.muted);
            ui_.text(vals[k], r.x + 13.f, r.y + 26.f, Font{Face::MonoBold, 15.f, 0.f}, T.ink);
        }
    }
    // spectre 48 bandes, courbe d'EQ, courbe cible, excès
    const Rect big{39.f, 201.f, 800.f, 403.3f};
    ui_.screen(big);
    float tgtOff = 0.f;  // le gabarit suit le niveau moyen du spectre (seule sa forme compte)
    {
        const float s = 770.f / 840.f, ox = 54.f, oy = 214.f;
        auto X = [&](float i) { return ox + (10.f + i * 17.2f + 6.5f) * s; };
        bool hlb[48] = {};
        for (const auto& a : st_.acts)
            if (a.kind == 0 && a.on && !a.applied) {
                const int cc = (int)std::lround(barOf(a.f));
                for (int k = -2; k <= 2; ++k)
                    if (cc + k >= 0 && cc + k < 48) hlb[cc + k] = true;
            }
        for (int i = 0; i < 48; ++i) {
            int nT = std::clamp((int)std::lround((bandTmp_[(size_t)i] + 72.f) / 66.f * 32.f), 1, 32);
            if (scanning) nT = std::min(32, nT + (int)std::lround(0.12f * 32.f * std::max(0.f, 1.f - std::fabs((float)i / 47.f - prog) * 8.f)));
            mix_.push(i, nT, now_, 30.0, 50.0);
            const int n = mix_.sh[(size_t)i], p = mix_.pk[(size_t)i];
            const float x = ox + (10.f + (float)i * 17.2f) * s;
            for (int sg = 0; sg < n; ++sg) {
                const Color col = hlb[i] ? T.barPk : (sg >= 24 ? T.barPk : (sg >= 12 ? T.barHi : T.barLo));
                c.fillRect(Rect{x, oy + (388.f - (float)(sg + 1) * 12.f) * s, 13.f * s, 10.f * s}, col);
            }
            if (p > n) c.fillRect(Rect{x, oy + (388.f - (float)p * 12.f) * s, 13.f * s, 10.f * s}, T.barPk);
        }
        if (an_.hasSignal()) {
            float rows[48];
            for (int i = 0; i < 48; ++i) rows[i] = (float)mix_.sh[(size_t)i];
            tgtOff = std::clamp(levelOffset(rows, role, prof), -10.f, 10.f);
        }
        // courbe d'EQ : filtres actifs (réponse exacte) + actions en attente (pointillés)
        std::vector<Pt> eq;
        bool pending = false;
        for (const auto& a : st_.acts) pending = pending || (a.kind == 0 && a.on && !a.applied);
        const int nAct = m_.mix.activeFilters();
        const float sr = std::max(22050.f, (float)m_.readouts.sampleRate.load());
        for (int k = 0; k <= 94; ++k) {
            const float i = (float)k / 2.f, f = freqOf(i);
            float d = 0.f;
            for (const auto& fl : m_.mix.flt)
                if (fl.on.load()) d += MasterChain::responseDb(fl.type.load(), fl.hz.load(), fl.db.load(), fl.q.load(), f, sr);
            for (const auto& a : st_.acts)
                if (a.kind == 0 && a.on && !a.applied) d += MasterChain::responseDb(a.type, a.f, a.g, a.type == kMixBell ? a.q : 0.7f, f, sr);
            eq.push_back(Pt{X(i), oy + std::clamp(196.f - d * 18.f, 8.f, 384.f) * s});
        }
        if (pending || nAct == 0) c.dashedPolyline(eq.data(), (int)eq.size(), T.waveA, 1.5f, 4.f, 4.f);
        else c.polyline(eq.data(), (int)eq.size(), T.waveA, 1.5f);
        // courbe cible du rôle / profil
        if (st_.mixRef) {
            std::vector<Pt> tg;
            for (int i = 0; i < 48; ++i) tg.push_back(Pt{X((float)i), oy + (388.f - (tgtRows(role, prof, i) + tgtOff) * 12.f - 5.f) * s});
            c.polyline(tg.data(), (int)tg.size(), kOr.withAlpha(0.18f), 5.f);
            c.dashedPolyline(tg.data(), (int)tg.size(), kOr, 1.8f, 7.f, 4.f);
        }
        ui_.textRight("- - COURBE CIBLE \xC2\xB7 R\xC3\x94LE / PROFIL", ox + 826.f * s, oy + 11.f * s, Font{Face::Mono, 10.f, 0.f}, T.scrMuted);
        // excès : bandes à 3 crans ou plus au-dessus du gabarit
        if (st_.mixEx) {
            int exI = -1;
            float exMax = 0.f;
            for (int i = 0; i < 48; ++i) {
                const float n = (float)mix_.sh[(size_t)i], over = n - (tgtRows(role, prof, i) + tgtOff);
                if (!an_.hasSignal() || over < 3.f) continue;
                const float x = X((float)i), y = oy + (388.f - n * 12.f - 3.f) * s;
                const Pt tri[3] = {Pt{x, y}, Pt{x + 6.f * s, y - 10.f * s}, Pt{x - 6.f * s, y - 10.f * s}};
                c.fillPolygon(tri, 3, kOr);
                if (over > exMax) { exMax = over; exI = i; }
            }
            const std::string ex = exI >= 0 ? fmt("EXC\xC3\x88S +%.1f dB @ ", exMax * kDbPerRow) + fq(std::round(freqOf((float)exI))) : std::string("AUCUN EXC\xC3\x88S DE R\xC3\x89SONANCE");
            ui_.text(ex, ox + 14.f * s, oy + 10.f * s, Font{Face::Mono, 11.f, 1.f}, kOr);
        }
        if (scanning) c.line(Pt{ox + (10.f + prog * 820.f) * s, oy}, Pt{ox + (10.f + prog * 820.f) * s, oy + 392.f * s}, T.waveA, 2.f);
        const float freqs[10] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
        const char* lbl[10] = {"20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"};
        for (int k = 0; k < 10; ++k) {
            const float x = X(barOf(freqs[k]));
            const Font f{Face::Mono, 10.f, 0.f};
            ui_.text(lbl[k], x - ui_.width(lbl[k], f) / 2.f, 579.3f, f, T.scrMuted);
        }
    }

    // barre de commandes
    std::string actSum;
    {
        const int ne = m_.mix.activeFilters();
        if (ne) actSum = fmt("EQ \xC3\x97%d", ne);
        if (m_.mix.comp.load()) actSum += std::string(actSum.empty() ? "" : " \xC2\xB7 ") + "COMP";
        if (m_.mix.limit.load()) actSum += std::string(actSum.empty() ? "" : " \xC2\xB7 ") + "LIM";
        if (actSum.empty()) actSum = "aucune";
        actSum = "corrections actives : " + actSum;
    }
    {
        ui_.box(Rect{39.f, 614.3f, 800.f, 50.f}, T.railBg, T.rule);
        ButtonStyle s = ui_.keyStyle();
        s.bg = T.acc;
        s.fg = T.onAcc;
        s.font = Font{Face::MonoBold, 12.f, 1.2f};
        s.pressed = scanning;
        const std::string lbl = scanning ? fmt("[ \xC3\x89" "COUTE\xE2\x80\xA6 %d %% ]", (int)std::lround(prog * 100.f)) : std::string("[ ANALYSER LE MIX ]");
        if (ui_.button(makeId("analyse"), Rect{48.f, 621.3f, 193.6f, 36.f}, lbl, s)) startAnalysis();
        auto tg2 = [&](const char* id, float x, float w, bool on, const char* label) {
            ButtonStyle b;
            b.bg = on ? T.ink : T.btnBg;
            b.fg = on ? T.card : T.ink;
            b.border = T.ink;
            b.shadow = false;
            b.font = Font{Face::Mono, 10.f, 0.6f};
            return ui_.button(makeId(id), Rect{x, 625.8f, w, 27.f}, label, b);
        };
        if (tg2("mixRef", 249.6f, 79.4f, st_.mixRef, "[ CIBLE ]")) st_.mixRef = !st_.mixRef;
        if (tg2("mixEx", 337.f, 79.4f, st_.mixEx, "[ EXC\xC3\x88S ]")) st_.mixEx = !st_.mixEx;
        if (tg2("mixRaz", 424.4f, 99.2f, false, "[ RAZ PEAK ]")) {
            m_.meters.resetPeaks = true;
            tpShowL_ = tpShowR_ = -90.f;
        }
        const Font f{Face::Mono, 10.f, 0.8f};
        ui_.textRight(ui_.fit(actSum, f, 290.f), 830.f, 633.8f, f, T.muted);
    }

    // bandes, vectorscope / True Peak, dynamique
    const Font lf{Face::Mono, 10.f, 1.2f};
    const Font rf{Face::Mono, 10.f, 0.8f};
    const Font vf{Face::MonoBold, 11.f, 0.f};
    for (const Rect& r : {Rect{39.f, 672.3f, 211.9f, 250.7f}, Rect{258.9f, 672.3f, 360.2f, 250.7f}, Rect{627.1f, 672.3f, 211.9f, 250.7f}}) ui_.box(r, T.railBg, T.rule);
    {
        ui_.text("BANDES \xC2\xB7 CIBLE DU R\xC3\x94LE", 52.f, 681.3f, lf, T.muted);
        const int rng[3][2] = {{0, 16}, {17, 35}, {36, 47}};
        const char* names[3] = {"GRAVES", "M\xC3\x89" "DIUMS", "AIGUS"};
        for (int k = 0; k < 3; ++k) {
            float v = 0.f, t = 0.f;
            for (int i = rng[k][0]; i <= rng[k][1]; ++i) {
                v += (float)mix_.sh[(size_t)i] / 32.f;
                t += (tgtRows(role, prof, i) + tgtOff) / 32.f;
            }
            v /= (float)(rng[k][1] - rng[k][0] + 1);
            t /= (float)(rng[k][1] - rng[k][0] + 1);
            const float y = 707.3f + (float)k * 19.f;
            ui_.text(names[k], 52.f, y, rf, T.muted);
            meterBar(ui_, c, T, Rect{108.f, y + 1.f, 91.9f, 12.f}, v, t, T.acc);
            ui_.textRight(fmt("%d%%", (int)std::lround(v * 100.f)), 238.f, y, vf, T.ink);
        }
        ui_.text("CORR\xC3\x89LATION PAR BANDE", 52.f, 767.3f, lf, T.muted);
        const float cb[3] = {mr.corrLow, mr.corrMid, mr.corrHigh};
        for (int k = 0; k < 3; ++k) {
            const float y = 785.3f + (float)k * 19.f, v = sig ? cb[k] : 0.f;
            ui_.text(names[k], 52.f, y, rf, T.muted);
            meterBar(ui_, c, T, Rect{108.f, y + 1.f, 87.9f, 12.f}, v, 0.3f, v < 0.3f ? kOr : T.acc);
            ui_.textRight(sig ? fmt("%.2f", v) : std::string("--"), 238.f, y, vf, T.ink);
        }
        c.fillRect(Rect{52.f, 845.3f, 186.f, 1.f}, T.rule);
        ui_.text("LARGEUR", 52.f, 854.3f, rf, T.muted);
        ui_.textRight(sig ? fmt("%.0f %%", mr.width) : std::string("-- %"), 238.f, 854.3f, vf, T.ink);
        ui_.text("BALANCE L/R", 52.f, 872.3f, rf, T.muted);
        const float b = mr.balance;
        ui_.textRight(!sig ? std::string("-- %") : std::fabs(b) < 1.f ? std::string("C 0 %") : fmt("%s %.0f %%", b < 0.f ? "L" : "R", std::fabs(b)), 238.f, 872.3f, vf, T.ink);
    }
    {
        ui_.text("VECTEURSCOPE \xC2\xB7 TRUE PEAK", 271.9f, 681.3f, lf, T.muted);
        ui_.textRight(sig ? fmt("corr\xC3\xA9lation %+.2f", mr.corr) : std::string("corr\xC3\xA9lation --"), 606.1f, 681.3f, Font{Face::Mono, 10.f, 0.6f}, T.muted);
        // vectorscope : M vertical, S horizontal, L et R sur les diagonales
        const Rect vs{271.9f, 700.3f, 226.2f, 196.7f};
        ui_.box(vs, T.scr, T.scrLine);
        c.dotGrid(vs.inset(1.f), vs.x + 1.f, vs.y + 1.f, 12.f, 1.1f, T.scrGrid);
        const float k = std::min(vs.w / 150.f, vs.h / 96.f);
        const Pt o{vs.x + vs.w / 2.f, vs.y + vs.h / 2.f};
        c.strokeEllipse(o, 34.f * k, 34.f * k, T.scrMid, 1.f);
        dashedCircle(c, o, 17.f * k, T.scrMid, 2.f, 3.f);
        c.line(Pt{o.x - 72.f * k, o.y}, Pt{o.x + 72.f * k, o.y}, T.scrMid, 1.f);
        c.line(Pt{o.x, o.y - 38.f * k}, Pt{o.x, o.y + 38.f * k}, T.scrMid, 1.f);
        {
            const Pt d1[2] = {Pt{o.x - 30.f * k, o.y - 30.f * k}, Pt{o.x + 30.f * k, o.y + 30.f * k}};
            const Pt d2[2] = {Pt{o.x + 30.f * k, o.y - 30.f * k}, Pt{o.x - 30.f * k, o.y + 30.f * k}};
            c.dashedPolyline(d1, 2, T.scrMid, 1.f, 2.f, 3.f);
            c.dashedPolyline(d2, 2, T.scrMid, 1.f, 2.f, 3.f);
        }
        const Font sf{Face::Mono, 7.5f, 0.f};
        ui_.text("L", o.x - 71.f * k, o.y - 43.f * k, sf, T.scrMuted);
        ui_.text("R", o.x + 66.f * k, o.y - 43.f * k, sf, T.scrMuted);
        ui_.text("M", o.x + 2.f * k, o.y - 49.f * k, sf, T.scrMuted);
        ui_.text("S", o.x + 2.f * k, o.y + 41.f * k, sf, T.scrMuted);
        const auto& L = an_.scopeL();
        const auto& R = an_.scopeR();
        float pk = 0.f;
        for (size_t i = 0; i < L.size(); ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
        scopeGain_ = std::max(0.05f, std::max(pk, scopeGain_ * 0.97f));  // gain automatique, retombée douce
        if (an_.hasSignal()) {
            std::vector<Pt> pts;
            const float g = 40.f * k / scopeGain_;
            for (size_t i = 0; i < L.size() && i < 800; ++i) {
                const float side = (R[i] - L[i]) * 0.7071f, mid = (L[i] + R[i]) * 0.7071f;
                pts.push_back(Pt{o.x + std::clamp(side * g, -70.f * k, 70.f * k), o.y - std::clamp(mid * g, -46.f * k, 46.f * k)});
            }
            c.save();
            c.clip(vs.inset(1.f));
            c.polyline(pts.data(), (int)pts.size(), T.waveA, 1.3f);
            c.restore();
        }
        // crête-mètres True Peak : 25 segments de -46 à +2 dB, maintien des crêtes
        const Rect ts{506.1f, 700.3f, 100.f, 196.7f};
        ui_.box(ts, T.scr, T.scrLine);
        const double dt = std::clamp((now_ - tpT_) / 1000.0, 0.0, 0.1);
        tpT_ = now_;
        tpShowL_ = std::max(mr.truePeakL, tpShowL_ - (float)dt * 22.f);
        tpShowR_ = std::max(mr.truePeakR, tpShowR_ - (float)dt * 22.f);
        const float sx = ts.x + 7.f, sy = ts.y + (ts.h - 124.f) / 2.f;
        auto meter = [&](float v, float hold, float x0) {
            for (int s2 = 0; s2 < 25; ++s2) {
                const float thr = -46.f + 2.f * (float)s2, y = sy + 118.f - (float)(s2 + 1) * 4.5f;
                const Rect r{sx + x0, y, 14.f, 3.6f};
                if (v >= thr) c.fillRect(r, thr >= -6.f ? kOr : T.waveA);
                else c.fillRect(r, T.scrMuted.withAlpha(0.35f));
            }
            if (hold > -60.f) {
                const int s2 = std::clamp((int)std::lround((hold + 46.f) / 2.f), 0, 24);
                c.fillRect(Rect{sx + x0, sy + 118.f - (float)(s2 + 1) * 4.5f, 14.f, 1.6f}, T.hl);
            }
        };
        meter(tpShowL_, mr.truePeakHoldL, 4.f);
        meter(tpShowR_, mr.truePeakHoldR, 22.f);
        const struct { const char* t; float y, tick; } sc[6] = {{"0", 15, 12}, {"-6", 29, 25.5f}, {"-12", 42, 39}, {"-24", 69, 66}, {"-36", 96, 93}, {"-40", 105, 102}};
        for (const auto& q : sc) {
            ui_.text(q.t, sx + 42.f, sy + q.y - 6.f, Font{Face::Mono, 7.f, 0.f}, T.scrMuted);
            c.line(Pt{sx + 38.f, sy + q.tick}, Pt{sx + 41.f, sy + q.tick}, T.scrMuted, 1.f);
        }
        const bool over = std::max(mr.truePeakHoldL, mr.truePeakHoldR) > 0.f;
        auto tpTxt = [](const char* ch, float v) { return v > -60.f ? fmt("%s %+.1f", ch, v) : std::string(ch) + " --"; };
        const Font bf{Face::MonoBold, 10.f, 0.4f};
        ui_.text(tpTxt("L", mr.truePeakHoldL), 271.9f, 903.f, bf, mr.truePeakHoldL > 0.f ? kOr : T.ink);
        ui_.textCenter("dBTP", Rect{388.f, 903.f, 100.f, 12.f}, Font{Face::Mono, 10.f, 0.4f}, over ? kOr : T.muted);
        ui_.textRight(tpTxt("R", mr.truePeakHoldR), 606.1f, 903.f, bf, mr.truePeakHoldR > 0.f ? kOr : T.ink);
    }
    {
        ui_.text("DYNAMIQUE", 640.1f, 681.3f, lf, T.muted);
        const float gr = m_.readouts.compGr.load();
        ui_.text("R\xC3\x89" "D.", 640.1f, 708.3f, rf, T.muted);
        meterBar(ui_, c, T, Rect{680.f, 709.3f, 90.f, 12.f}, gr / 12.f, -1.f, T.acc);
        ui_.textRight(fmt("-%.1f dB", gr), 826.f, 708.3f, vf, T.ink);
        auto ledRow = [&](float y, const char* name, bool on, const std::string& onTxt, int which) {
            const Rect row{640.1f, y, 186.f, 14.f};
            if (ui_.interact(makeId("dynRow", which), row).clicked) {
                if (which == 0) m_.mix.comp = !on;
                else m_.mix.limit = !on;
                pushHist(std::string(which == 0 ? "Compresseur" : "Limiteur") + (on ? " coup\xC3\xA9" : " activ\xC3\xA9"), on ? "BYPASS" : "ACTIF", "MIX");
            }
            ui_.box(Rect{640.1f, y + 2.f, 10.f, 10.f}, on ? T.acc : Color{0, 0, 0, 0}, T.rule, 0.f);
            const float w = ui_.text(name, 658.1f, y, Font{Face::Mono, 11.f, 0.f}, T.ink);
            ui_.text(on ? onTxt : std::string("bypass"), 658.1f + w + 7.f, y, Font{Face::Mono, 11.f, 0.f}, T.muted);
        };
        ledRow(731.3f, "COMP", m_.mix.comp.load(), fmt("2:1 \xC2\xB7 %.0f dB", m_.mix.compThreshDb.load()), 0);
        ledRow(751.3f, "LIMIT", m_.mix.limit.load(), fmt("%.1f dBFS", m_.mix.limitCeilDb.load()), 1);
        ui_.text("LOUDNESS \xC2\xB7 M / S / I", 640.1f, 773.3f, lf, T.muted);
        const float lv[3] = {mr.momentary, mr.shortTerm, mr.integrated};
        const char* ln[3] = {"M", "S", "I"};
        for (int k = 0; k < 3; ++k) {
            const float y = 791.3f + (float)k * 19.f;
            ui_.text(ln[k], 640.1f, y, rf, T.muted);
            meterBar(ui_, c, T, Rect{696.f, y + 1.f, 86.f, 12.f}, (lv[k] + 40.f) / 40.f, 26.f / 40.f, T.acc);
            ui_.textRight(lv[k] > -69.f ? fmt("%.1f", lv[k]) : std::string("--"), 826.f, y, vf, T.ink);
        }
        c.fillRect(Rect{640.1f, 851.3f, 186.f, 1.f}, T.rule);
        ui_.text("PLR", 640.1f, 860.3f, rf, T.muted);
        ui_.textRight(sig ? fmt("%.1f dB", mr.plr) : std::string("-- dB"), 826.f, 860.3f, vf, T.ink);
        ui_.text("LRA", 640.1f, 878.3f, rf, T.muted);
        ui_.textRight(mr.integrated > -69.f ? fmt("%.1f LU", mr.lra) : std::string("-- LU"), 826.f, 878.3f, vf, T.ink);
    }

    // --- ASSISTANT ---
    const Rect ac{866.f, 73.f, 390.f, 623.f};
    ui_.cardTerm(ac);
    ui_.cardHeader(ac, "ASSISTANT", true, 22.f, 20.f);
    ui_.tag("mix \xC2\xB7 ia", ac.r() - 23.f, 94.f, Ui::Tag::Accent);
    std::string roleShort = roleLabel(st_.role);
    if (st_.role == 0) roleShort = "AUTO";
    else if (roleShort.find(" / ") != std::string::npos) roleShort = roleShort.substr(0, roleShort.find(" / "));
    {
        auto dd = [&](const char* id, const Rect& r, bool open, const std::string& label) {
            ButtonStyle b;
            b.bg = Color{0, 0, 0, 0};
            b.fg = T.scrText;
            b.border = open ? T.hl : T.scrMuted;
            b.shadow = false;
            b.font = Font{Face::Mono, 10.f, 0.8f};
            return ui_.button(makeId(id), r, label, b);
        };
        if (dd("mixRole", Rect{889.f, 136.f, 142.4f, 25.f}, mixMenu_ == 1, "[ R\xC3\x94LE : " + roleShort + " \xE2\x96\xBE ]")) mixMenu_ = mixMenu_ == 1 ? 0 : 1;
        if (dd("mixProf", Rect{1039.4f, 136.f, 176.4f, 25.f}, mixMenu_ == 2, std::string("[ PROFIL : ") + kProfNames[prof] + " \xE2\x96\xBE ]")) mixMenu_ = mixMenu_ == 2 ? 0 : 2;
    }
    // journal (aligné en bas de sa zone)
    {
        const Font f{Face::Mono, 11.f, 0.f};
        const float lh = 16.5f;
        std::vector<std::pair<std::string, Color>> lines;
        if (st_.aiPhase == 0) lines.push_back({"> en attente. Lance une analyse globale.", T.scrMuted});
        else {
            for (int k = 0; k < 5 && scanning; ++k) {
                if (el < kLogT[k]) break;
                std::string line = kLog[k];
                if (k == 3) line += roleLabel(st_.role);
                lines.push_back({cpPrefix(line, (size_t)std::max(0.0, (el - kLogT[k]) / 20.0)), T.scrText});
            }
            if (!scanning) {
                if (st_.scanLow) lines.push_back({"> signal trop faible : joue quelques notes pendant l'\xC3\xA9" "coute.", T.hl});
                else {
                    lines.push_back({fmt("> %.1f LUFS \xC2\xB7 cr\xC3\xAAte %+.1f dBTP \xC2\xB7 cr\xC3\xAAte/RMS %.1f dB", st_.scanLufs, st_.scanPeak, st_.scanDyn), T.scrText});
                    if (st_.mixScored) lines.push_back({"> r\xC3\xA9seau MixNet \xC2\xB7 6 corrections \xC3\xA9valu\xC3\xA9" "es", T.scrText});
                }
                int nOn = 0;
                for (const auto& a : st_.acts) nOn += a.on && !a.applied;
                lines.push_back({fmt("> %d correction%s \xC2\xB7 r\xC3\xB4le ", nOn, nOn > 1 ? "s" : "") + roleShort + " \xC2\xB7 profil " + kProfNames[prof], T.hl});
            } else {
                const int cells = 20, filled = (int)std::lround(prog * cells);
                std::string bar = "[";
                for (int k = 0; k < cells; ++k) bar += k < filled ? "\xE2\x96\x88" : "\xE2\x96\x91";
                bar += fmt("] %d %%", (int)std::lround(prog * 100.f));
                lines.push_back({bar, T.hl});
            }
        }
        const int maxL = 4;
        const int first = std::max(0, (int)lines.size() - maxL);
        float y = 171.f + 66.f - (float)((int)lines.size() - first) * lh;
        for (int i = first; i < (int)lines.size(); ++i, y += lh) ui_.text(ui_.fit(lines[(size_t)i].first, f, 344.f), 889.f, y + 2.f, f, lines[(size_t)i].second);
    }
    c.fillRect(Rect{889.f, 247.f, 344.f, 1.f}, T.scrRule);
    int nOn = 0;
    for (const auto& a : st_.acts) nOn += a.on && !a.applied;
    ui_.text("ACTIONS CORRECTIVES", 889.f, 256.f, Font{Face::Mono, 10.f, 1.2f}, T.scrMuted);
    ui_.textRight(fmt("%d en attente", nOn), 1233.f, 256.f, Font{Face::Mono, 10.f, 0.6f}, T.hl);
    {
        const Rect view{889.f, 277.f, 344.f, 372.f};
        const float content = (float)st_.acts.size() * 67.f - 5.f;
        if (ui_.hit(view) && ui_.in().wheel != 0.f && mixMenu_ == 0) actScroll_ -= ui_.in().wheel * 67.f;
        actScroll_ = std::clamp(actScroll_, 0.f, std::max(0.f, content - view.h));
        c.save();
        c.clip(view);
        for (size_t i = 0; i < st_.acts.size(); ++i) {
            EditorState::Act& a = st_.acts[i];
            const Rect box{889.f, 277.f + (float)i * 67.f - actScroll_, 344.f, 62.f};
            if (box.b() < view.y || box.y > view.b()) continue;
            const float al = (a.on || a.applied) ? 1.f : 0.55f;
            if (a.applied) c.fillRect(box, kOr.withAlpha(0.07f));
            ui_.box(box, Color{0, 0, 0, 0}, a.applied ? T.hl : (a.on ? T.scrMuted : T.scrRule));
            // interrupteur
            const Rect sw{box.x + 9.f, box.y + 7.f, 38.f, 20.f};
            if (ui_.interact(makeId("actSw", (int)i), sw).clicked && !a.applied) a.on = !a.on;
            ui_.box(sw, a.on ? kOr.withAlpha(0.18f) : Color{0, 0, 0, 0}, T.scrMuted);
            c.fillRect(Rect{sw.x + (a.on ? 21.f : 2.f), sw.y + 2.f, 13.f, 14.f}, a.on ? kOr : T.scrMuted);
            const char* tag = a.kind == 0 ? "EQ" : "DYN";
            const Font tf{Face::Mono, 9.f, 0.9f};
            const float tw = ui_.width(tag, tf) + 10.f;
            ui_.box(Rect{sw.r() + 8.f, box.y + 10.f, tw, 14.f}, Color{0, 0, 0, 0}, T.hl.withAlpha(al));
            ui_.text(tag, sw.r() + 13.f, box.y + 11.5f, tf, T.hl.withAlpha(al));
            const Font hf{Face::MonoBold, 11.f, 0.f};
            const float tx = sw.r() + 8.f + tw + 8.f;
            ui_.text(ui_.fit(actTitle(a), hf, box.r() - 9.f - tx), tx, box.y + 10.f, hf, T.scrText.withAlpha(al));
            // dosage
            const float ry = box.y + 34.f;
            ButtonStyle st;
            st.bg = Color{0, 0, 0, 0};
            st.fg = T.scrText;
            st.border = T.scrMuted;
            st.shadow = false;
            st.font = Font{Face::Mono, 12.f, 0.f};
            auto step = [&](int d) {
                if (a.applied) return;
                if (a.kind == 0) a.g = std::clamp(std::round((a.g + (float)d * 0.5f) * 2.f) / 2.f, -9.f, 9.f);
                else if (a.kind == 1) a.g = std::clamp(a.g + (float)d, -40.f, -6.f);
                else a.g = std::clamp(std::round((a.g + (float)d * 0.1f) * 10.f) / 10.f, -3.f, 0.f);
            };
            if (ui_.button(makeId("actDn", (int)i), Rect{box.x + 9.f, ry, 20.f, 20.f}, "\xE2\x88\x92", st)) step(-1);
            const std::string gv = (a.kind == 0 ? fmt("%+.1f", a.g) : fmt(a.kind == 2 ? "%.1f" : "%.0f", a.g)) + " dB";
            ui_.textCenter(gv, Rect{box.x + 33.f, ry + 3.f, 58.f, 14.f}, vf, T.hl.withAlpha(al));
            if (ui_.button(makeId("actUp", (int)i), Rect{box.x + 95.f, ry, 20.f, 20.f}, "+", st)) step(1);
            const Font wf{Face::Mono, 10.f, 0.f};
            ui_.text(ui_.fit(a.why, wf, 1119.2f - 6.f - (box.x + 121.f)), box.x + 121.f, ry + 4.f, wf, T.scrMuted.withAlpha(al));
            ButtonStyle ap;
            const bool can = a.on && !a.applied;
            ap.bg = can ? T.acc : Color{0, 0, 0, 0};
            ap.fg = can ? T.onAcc : T.scrMuted;
            ap.border = can ? T.acc : T.scrMuted;
            ap.shadow = false;
            ap.font = Font{Face::MonoBold, 10.f, 0.6f};
            if (ui_.button(makeId("actAp", (int)i), Rect{1119.2f, ry - 1.5f, 103.8f, 23.f}, a.applied ? "[ APPLIQU\xC3\x89 \xE2\x9C\x93 ]" : "[ APPLIQUER ]", ap, can ? Cursor::Hand : Cursor::Arrow) && can)
                applyAct((int)i);
        }
        c.restore();
    }
    if (std::fmod(now_ / 1000.0, 1.0) < 0.5) ui_.text("\xE2\x96\x88", 889.f, 660.f, Font{Face::Mono, 10.f, 0.f}, T.hl);
    ui_.text(" rien ne change sans ton accord", 895.f, 660.f, Font{Face::Mono, 10.f, 0.f}, T.scrMuted);

    // --- HISTORIQUE ---
    const Rect hc{866.f, 708.f, 390.f, 228.f};
    ui_.card(hc);
    ui_.cardHeader(hc, "HISTORIQUE", false, 14.f, 12.f);
    ui_.tag("session", hc.r() - 15.f, 721.f, Ui::Tag::Line);
    {
        const int applied = m_.mix.activeFilters() + (int)m_.mix.comp.load() + (int)m_.mix.limit.load();
        int score = std::min(98, 64 + applied * 7);
        if (st_.mixScored) {  // estimation du réseau : besoins restants, corrections appliquées exclues
            bool done[6] = {};
            for (const auto& a : st_.acts) {
                const int k = netOf(a);
                if (k >= 0 && a.applied) done[k] = true;
            }
            float need = 0.f;
            for (int i = 0; i < 6; ++i)
                if (!done[i]) need += st_.mixProb[(size_t)i];
            score = std::clamp((int)std::lround(100.f - 22.f * need), 30, 99);
        }
        ui_.text("SCORE DU MIX", 881.f, 766.5f, lf, T.muted);
        meterBar(ui_, c, T, Rect{977.4f, 767.f, 210.4f, 12.f}, (float)score / 100.f, -1.f, T.acc);
        ui_.textRight(fmt("%d/100", score), 1241.f, 765.f, Font{Face::MonoBold, 12.f, 0.f}, T.ink);
        for (int k = 0; k < 3; ++k) {
            const EditorState::Hist* h = k < (int)st_.hist.size() ? &st_.hist[(size_t)k] : nullptr;
            const float yy = 787.f + (float)k * 35.f;
            const Font f{Face::Mono, 11.f, 0.f};
            float x = 881.f;
            x += ui_.text(h ? h->time : std::string("--:--:--"), x, yy + 11.f, f, T.muted) + 8.f;
            const Font tf{Face::Mono, 9.f, 0.9f};
            const std::string src = h ? h->src : std::string("\xE2\x80\x94");
            const float tw = ui_.width(src, tf) + 12.f;
            ui_.box(Rect{x, yy + 11.5f, tw, 15.f}, Color{0, 0, 0, 0}, T.rule);
            ui_.text(src, x + 6.f, yy + 13.f, tf, T.ink);
            x += tw + 8.f;
            const Font sf{Face::MonoBold, 10.f, 0.6f};
            const std::string status = h ? h->status : std::string();
            const float sw = status.empty() ? 0.f : ui_.width(status, sf) + 8.f;
            ui_.text(ui_.fit(h ? h->text : std::string("emplacement libre"), f, 1149.8f - 8.f - sw - x), x, yy + 11.f, f, h ? T.ink : T.muted);
            if (!status.empty()) ui_.textRight(status, 1149.8f - 8.f, yy + 12.f, sf, status == "APPLIQU\xC3\x89" ? T.hl : T.muted);
            const bool can = h && h->undoKind >= 0;
            ButtonStyle rs = ui_.miniStyle(false);
            rs.font = Font{Face::Mono, 9.f, 0.5f};
            rs.shadow = can;
            if (!can) {
                rs.bg = Color{0, 0, 0, 0};
                rs.fg = T.muted.withAlpha(0.5f);
                rs.border = T.rule.withAlpha(0.5f);
            }
            if (ui_.button(makeId("histRs", k), Rect{1149.8f, yy + 7.f, 91.2f, 20.f}, "[ RESTAURER ]", rs, can ? Cursor::Hand : Cursor::Arrow) && can) restoreHist(k);
            c.fillRect(Rect{881.f, yy + 34.f, 360.f, 1.f}, T.rule);
        }
        const Font f{Face::Mono, 10.f, 0.8f};
        ui_.text(ui_.fit(actSum, f, 360.f), 881.f, 900.f, f, T.muted);
    }

    // menus RÔLE / PROFIL (au-dessus de tout)
    if (mixMenu_ != 0) {
        ui_.setLayer(3);
        const int kind = mixMenu_;
        const int n = kind == 1 ? 6 : 5;
        const Rect menu{kind == 1 ? 889.f : 1215.8f - 150.f, 164.f, 150.f, (float)n * 30.f + 2.f};
        c.fillRect(menu.moved(3.f, 3.f), Color{0, 0, 0, 0.45f});
        ui_.box(menu, T.scr, T.hl);
        for (int k = 0; k < n; ++k) {
            const Rect r{menu.x + 1.f, menu.y + 1.f + (float)k * 30.f, menu.w - 2.f, 29.f};
            const bool on = kind == 1 ? k == st_.role : k == prof;
            if (on) c.fillRect(r, T.hl);
            c.fillRect(Rect{r.x, r.b(), r.w, 1.f}, T.scrRule);
            if (ui_.interact(makeId("mixIt", kind * 10 + k), r).clicked) {
                if (kind == 1) {
                    if (k != st_.role) forceSound_ = true;
                    st_.role = k;
                    st_.rec = 0;
                } else st_.mixProf = k;
                mixMenu_ = 0;
                genActs(st_.aiPhase == 2);
            }
            ui_.text(kind == 1 ? roleLabel(k) : std::string(kProfNames[k]), r.x + 10.f, r.y + 8.f, Font{Face::Mono, 10.f, 0.8f}, on ? T.scr : T.scrText);
        }
        ui_.setLayer(0);
    }
}

} // namespace raw2::ui
