#pragma once
// Caractéristiques audio données aux réseaux de l'IA (§9.5). Le même code sert à l'entraînement
// (rendus hors ligne) et dans le plugin (sortie réelle ou écoute interne), pour que le réseau voie
// exactement ce qu'il a appris à lire. Jamais appelé depuis le thread audio.
#include <array>
#include <complex>
#include <cstddef>
#include <vector>

namespace raw2::ai {

constexpr int kBands = 24;     // bandes logarithmiques 30 Hz - 16 kHz
constexpr int kSegDim = 40;    // taille d'un descripteur de segment
constexpr int kProbeSegs = 3;  // écoute interne : note grave, note médium, notes courtes
constexpr int kProbeDim = kSegDim * kProbeSegs;

// Indices dans un descripteur de segment.
enum Feat {
    fBand0 = 0,                 // 24 niveaux de bande en dB, relatifs au niveau moyen
    fLevel = kBands,            // niveau RMS (dBFS)
    fPeak,                      // crête (dBFS)
    fCrest,                     // crête - RMS (dB)
    fDyn,                       // dynamique : écart p95 - p10 des blocs de 100 ms (dB)
    fCorr,                      // corrélation L/R
    fSide,                      // énergie côté / milieu (dB)
    fCentroid,                  // centre de gravité spectral (log2 Hz)
    fFlatness,                  // platitude spectrale (dB)
    fFlux,                      // variation spectrale moyenne (dB par trame)
    fMove0,                     // 4 écarts types temporels (graves, bas-médiums, médiums, aigus)
    fOnsets = fMove0 + 4,       // attaques par seconde
    fAttack,                    // temps de montée (log10 s)
    fSustain,                   // niveau final / maximum (dB)
};
static_assert(fSustain + 1 == kSegDim, "descripteur incomplet");

float bandLowHz(int b);   // bornes des bandes
float bandHighHz(int b);
float bandCenterHz(int b);

// Accumule un signal stéréo puis calcule son descripteur.
class SegmentAnalyzer {
public:
    void reset(double sampleRate);
    void push(const float* L, const float* R, size_t n);
    void compute(float* out) const;  // kSegDim valeurs
    double seconds() const { return sr_ > 0.0 ? (double)total_ / sr_ : 0.0; }
    float levelDb() const;           // niveau RMS de ce qui a été accumulé

private:
    void frame();
    double sr_ = 48000.0;
    size_t total_ = 0;
    // trames FFT
    std::vector<float> ringL_, ringR_;
    size_t fill_ = 0;
    std::vector<std::complex<double>> buf_;
    std::vector<double> win_;
    std::vector<int> binBand_;
    std::array<double, kBands> bandSum_{};
    std::vector<std::array<float, 4>> coarse_;  // dB par trame, 4 groupes
    double fluxSum_ = 0.0;
    int fluxN_ = 0, frames_ = 0;
    std::array<float, kBands> prevDb_{};
    bool havePrev_ = false;
    // niveau, crête, stéréo
    double sumMid_ = 0.0, sumSide_ = 0.0, sumLL_ = 0.0, sumRR_ = 0.0, sumLR_ = 0.0;
    float peak_ = 0.f;
    // enveloppes : blocs de 100 ms et de 10 ms
    double blk_ = 0.0, env_ = 0.0;
    size_t blkN_ = 0, envN_ = 0;
    std::vector<float> blocks_, envs_;
};

} // namespace raw2::ai
