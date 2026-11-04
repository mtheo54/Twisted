#pragma once
// Analyse du signal de sortie pour l'interface : oscilloscope, spectre, sonie (pondération K),
// crête, dynamique, corrélation. Tourne dans le thread de l'interface à partir de l'anneau audio.
#include "core/Spsc.h"

#include <array>
#include <vector>

namespace raw2::ui {

class Analyzer {
public:
    static constexpr int kFft = 4096;
    void update(const ScopeRing& ring, double sampleRate);

    // spectre en dB par bande (bandes logarithmiques 20 Hz - 20 kHz)
    void bands(int count, std::vector<float>& outDb) const;
    float bandDb(float f0, float f1) const;  // énergie dans [f0, f1]
    const std::vector<float>& scopeL() const { return scopeL_; }  // derniers échantillons (déclenchés)
    const std::vector<float>& scopeR() const { return scopeR_; }
    float lufs() const { return lufs_; }
    float peakDb() const { return peakDb_; }
    float dynamicsDb() const { return dyn_; }
    float correlation() const { return corr_; }
    bool hasSignal() const { return lufs_ > -70.f; }

private:
    struct Bq {
        double b0, b1, b2, a1, a2, z1[2] = {0, 0}, z2[2] = {0, 0};
        double run(int c, double x) {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
    };
    void design(double sr);
    size_t lastPos_ = 0;
    bool init_ = false;
    double sr_ = 0.0;
    Bq pre_{}, rlb_{};
    std::vector<double> msBlocks_;  // énergie K par bloc de 100 ms
    double blockAcc_ = 0.0, blockLR_ = 0.0, blockLL_ = 0.0, blockRR_ = 0.0, blockPeak_ = 0.0;
    int blockN_ = 0;
    std::vector<double> lrBlocks_, llBlocks_, rrBlocks_, peakBlocks_;
    std::vector<float> mag_;  // puissance par case FFT
    std::vector<float> scopeL_, scopeR_;
    float lufs_ = -90.f, peakDb_ = -90.f, dyn_ = 0.f, corr_ = 1.f;
};

} // namespace raw2::ui
