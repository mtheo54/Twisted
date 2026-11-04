#include "ui/Analyzer.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace raw2::ui {

namespace {
constexpr double M_PI_ = 3.14159265358979323846;
}

void Analyzer::design(double sr) {
    sr_ = sr;
    // pondération K (ITU-R BS.1770) : plateau haut + passe-haut RLB
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(M_PI_ * f0 / sr), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        pre_ = Bq{(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan(M_PI_ * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        rlb_ = Bq{1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
    }
    msBlocks_.clear();
    lrBlocks_.clear(); llBlocks_.clear(); rrBlocks_.clear(); peakBlocks_.clear();
}

void Analyzer::update(const ScopeRing& ring, double sr) {
    if (!init_ || sr != sr_) {
        design(sr);
        lastPos_ = ring.position();
        init_ = true;
    }
    const size_t pos = ring.position();
    size_t fresh = pos - lastPos_;
    if (fresh > ScopeRing::kSize / 2) fresh = ScopeRing::kSize / 2;
    lastPos_ = pos;

    // sonie, crête, corrélation : traitement incrémental des nouveaux échantillons
    if (fresh > 0) {
        std::vector<float> L(fresh), R(fresh);
        ring.read(L.data(), R.data(), fresh);
        const int blockLen = std::max(1, (int)(sr * 0.1));
        for (size_t i = 0; i < fresh; ++i) {
            const double l = L[i], r = R[i];
            const double kl = rlb_.run(0, pre_.run(0, l)), kr = rlb_.run(1, pre_.run(1, r));
            blockAcc_ += kl * kl + kr * kr;
            blockLR_ += l * r;
            blockLL_ += l * l;
            blockRR_ += r * r;
            blockPeak_ = std::max(blockPeak_, std::max(std::fabs(l), std::fabs(r)));
            if (++blockN_ >= blockLen) {
                msBlocks_.push_back(blockAcc_ / blockN_);
                lrBlocks_.push_back(blockLR_);
                llBlocks_.push_back(blockLL_);
                rrBlocks_.push_back(blockRR_);
                peakBlocks_.push_back(blockPeak_);
                blockAcc_ = blockLR_ = blockLL_ = blockRR_ = blockPeak_ = 0.0;
                blockN_ = 0;
                const size_t keep = 30;  // 3 s
                for (auto* v : {&msBlocks_, &lrBlocks_, &llBlocks_, &rrBlocks_, &peakBlocks_})
                    if (v->size() > keep) v->erase(v->begin(), v->begin() + (ptrdiff_t)(v->size() - keep));
            }
        }
        // sonie momentanée : 4 derniers blocs (400 ms)
        const size_t n = std::min<size_t>(4, msBlocks_.size());
        if (n) {
            double ms = 0.0, lr = 0.0, ll = 0.0, rr = 0.0;
            for (size_t k = msBlocks_.size() - n; k < msBlocks_.size(); ++k) {
                ms += msBlocks_[k];
                lr += lrBlocks_[k];
                ll += llBlocks_[k];
                rr += rrBlocks_[k];
            }
            ms /= (double)n;
            lufs_ = (float)std::max(-90.0, -0.691 + 10.0 * std::log10(ms + 1e-12));
            corr_ = (ll > 1e-9 && rr > 1e-9) ? (float)std::clamp(lr / std::sqrt(ll * rr), -1.0, 1.0) : 1.f;
        }
        double pk = 0.0, msAll = 0.0;
        for (size_t k = 0; k < peakBlocks_.size(); ++k) {
            pk = std::max(pk, peakBlocks_[k]);
            msAll += (llBlocks_[k] + rrBlocks_[k]) * 0.5;
        }
        peakDb_ = (float)std::max(-90.0, 20.0 * std::log10(pk + 1e-9));
        const double rmsDb = 10.0 * std::log10(msAll / std::max(1.0, (double)peakBlocks_.size() * sr * 0.1) + 1e-12);
        dyn_ = std::clamp(peakDb_ - (float)rmsDb, 0.f, 40.f);
    }

    // spectre : FFT de Hann sur les 4096 derniers échantillons (mono)
    std::vector<float> L(kFft), R(kFft);
    ring.read(L.data(), R.data(), kFft);
    std::vector<std::complex<double>> X(kFft);
    for (int i = 0; i < kFft; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI_ * i / (kFft - 1));
        X[(size_t)i] = std::complex<double>((L[(size_t)i] + R[(size_t)i]) * 0.5 * w, 0.0);
    }
    fft(X, false);
    mag_.resize(kFft / 2);
    // puissance normalisée : une sinusoïde pleine échelle donne 0 dB (gain de Hann = 0.5)
    for (int k = 0; k < kFft / 2; ++k) mag_[(size_t)k] = (float)(std::norm(X[(size_t)k]) * 4.0 / (0.25 * kFft * kFft));

    // oscilloscope : 1024 échantillons, déclenchés sur un passage par zéro montant
    const int show = 1024;
    int start = kFft - show - 1;
    for (int i = kFft - show - 1; i > kFft - show - 1 - 1200 && i > 1; --i)
        if (L[(size_t)i - 1] < 0.f && L[(size_t)i] >= 0.f) { start = i; break; }
    scopeL_.assign(L.begin() + start, L.begin() + start + show);
    scopeR_.assign(R.begin() + start, R.begin() + start + show);
}

float Analyzer::bandDb(float f0, float f1) const {
    if (mag_.empty() || sr_ <= 0.0) return -120.f;
    const double bin = sr_ / kFft;
    int k0 = std::max(1, (int)std::floor(f0 / bin)), k1 = std::min((int)mag_.size() - 1, (int)std::ceil(f1 / bin));
    if (k1 < k0) k1 = k0;
    double p = 0.0;
    for (int k = k0; k <= k1; ++k) p += mag_[(size_t)k];
    return (float)(10.0 * std::log10(p + 1e-14));
}

void Analyzer::bands(int count, std::vector<float>& out) const {
    out.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        const double c = 20.0 * std::pow(1000.0, (double)i / (count - 1));
        const double half = std::pow(1000.0, 0.5 / (count - 1));
        out[(size_t)i] = bandDb((float)(c / half), (float)(c * half));
    }
}

} // namespace raw2::ui
