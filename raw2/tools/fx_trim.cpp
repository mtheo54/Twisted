// Calibre le volume des presets d'usine avec leurs effets : chaque preset est joué deux fois
// (effets actifs, puis DRY/WET à 0) et la correction de MASTER est choisie pour que la version
// avec effets sonne 1.5 dB plus présente que la version sèche, crêtes sous -3 dBFS (sous -2 dBFS
// pour le tout premier départ de note), sur 4 départs différents de l'automate de VIE.
// Écrit src/core/FxTrim.inc (à relancer quand les presets ou les effets changent).
//   raw2_fx_trim <chemin de src/core/FxTrim.inc>
#include "core/FactoryData.h"
#include "core/Model.h"
#include "dsp/Engine.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>

using namespace raw2;

namespace {
struct Level { float peakDb, rmsDb, onsetPeakDb; };  // crête après 1 s, RMS après 1 s, crête dès la 1re note

// une petite boucle de quatre notes, 6 s (mesure après la première seconde) ; un modèle neuf à
// chaque fois, l'automate de VIE partant de la graine donnée (sinon les écoutes seraient incomparables)
Level play1(int preset, float master, bool fx, uint32_t seed) {
    Model m(Model::Mode::Offline);
    m.applyFactoryPreset(preset, nullptr);
    m.life.restart(seed);
    m.params.set(kMaster, master);  // mesure sans correction
    if (!fx) m.params.set(kFxWet, 0.f);
    Engine e(m);
    e.prepare(48000.0, 256);
    float L[256], R[256], peak = 0.f, peakAll = 0.f;
    double sum = 0.0;
    long n = 0;
    // les basses sont jouées une octave plus bas (comme dans les tests du moteur), à pleine vélocité
    const bool bass = kFactoryPresets[preset].category == Category::Bass;
    const int notes[4] = {bass ? 36 : 48, bass ? 43 : 55, bass ? 48 : 60, bass ? 40 : 64};
    for (int b = 0; b < 48000 * 6 / 256; ++b) {
        NoteEvent ev[2];
        int ne = 0;
        // une note par seconde, tenue 0.75 s : les enveloppes et les LFO ont le temps de jouer
        if (b % 188 == 0) ev[ne++] = NoteEvent{0, notes[(b / 188) % 4], 1.f, true};
        if (b % 188 == 141) ev[ne++] = NoteEvent{0, notes[(b / 188) % 4], 0.f, false};
        e.process(L, R, 256, ev, ne);
        for (int i = 0; i < 256; ++i) peakAll = std::max(peakAll, std::max(std::fabs(L[i]), std::fabs(R[i])));
        if (b < 48000 / 256) continue;
        for (int i = 0; i < 256; ++i) {
            peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
            sum += 0.5 * ((double)L[i] * L[i] + (double)R[i] * R[i]);
            ++n;
        }
    }
    return {20.f * std::log10(peak + 1e-9f), (float)(10.0 * std::log10(sum / (double)n + 1e-12)), 20.f * std::log10(peakAll + 1e-9f)};
}

// la vie module le son : 4 départs différents, RMS moyen et pire crête
Level play(int preset, float master, bool fx) {
    Level r{-200.f, 0.f, -200.f};
    double rms = 0.0;
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        const Level l = play1(preset, master, fx, seed * 7919u);
        r.peakDb = std::max(r.peakDb, l.peakDb);
        r.onsetPeakDb = std::max(r.onsetPeakDb, l.onsetPeakDb);
        rms += std::pow(10.0, l.rmsDb / 10.0) / 4.0;
    }
    r.rmsDb = (float)(10.0 * std::log10(rms + 1e-12));
    return r;
}
} // namespace

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "FxTrim.inc";
    float trim[kNumFactoryPresets];
    for (int p = 0; p < kNumFactoryPresets; ++p) {
        const float master = kFactoryPresets[p].master;
        const Level fx = play(p, master, true), dry = play(p, master, false);
        // le tout premier départ de note (attaque des compresseurs) reste sous -2 dBFS
        float t = std::min({dry.rmsDb + 1.5f - fx.rmsDb, -3.f - fx.peakDb, -2.f - fx.onsetPeakDb});
        // le MASTER est borné : on ne peut pas monter au-delà de 0 dB
        t = std::clamp(t, (0.05f - master) * 18.2f, (1.f - master) * 18.2f);
        trim[p] = std::round(t * 10.f) / 10.f;
        std::printf("%2d %-20s sec %6.1f dB  effets %6.1f dB (crête %5.1f)  correction %+5.1f dB\n", p + 1, kFactoryPresets[p].name, dry.rmsDb,
                    fx.rmsDb, fx.peakDb, trim[p]);
    }
    FILE* f = std::fopen(out, "wb");
    if (!f) { std::printf("impossible d'écrire %s\n", out); return 1; }
    std::fprintf(f, "// g\xC3\xA9n\xC3\xA9r\xC3\xA9 par tools/fx_trim.cpp : correction de MASTER (dB) des presets d'usine avec effets\n");
    std::fprintf(f, "const float kFxTrimDb[%d] = {", kNumFactoryPresets);
    for (int p = 0; p < kNumFactoryPresets; ++p) std::fprintf(f, "%s%s%.1ff", p ? "," : "", p % 12 ? " " : "\n    ", trim[p]);
    std::fprintf(f, "\n};\n");
    std::fclose(f);
    std::printf("\xC3\xA9" "crit : %s\n", out);
    return 0;
}
