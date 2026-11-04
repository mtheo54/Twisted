#include "ai/Features.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw2::ai {

namespace {
constexpr int kN = 4096, kHop = 2048;
constexpr double kPi = 3.14159265358979323846;
constexpr float kLoHz = 30.f, kHiHz = 16000.f;

inline float db(double p) { return (float)(10.0 * std::log10(p + 1e-14)); }

int coarseOf(float hz) { return hz < 200.f ? 0 : (hz < 1000.f ? 1 : (hz < 5000.f ? 2 : 3)); }
} // namespace

float bandLowHz(int b) { return kLoHz * std::pow(kHiHz / kLoHz, (float)b / kBands); }
float bandHighHz(int b) { return bandLowHz(b + 1); }
float bandCenterHz(int b) { return std::sqrt(bandLowHz(b) * bandHighHz(b)); }

void SegmentAnalyzer::reset(double sr) {
    sr_ = sr > 1000.0 ? sr : 48000.0;
    total_ = 0;
    ringL_.assign(kN, 0.f);
    ringR_.assign(kN, 0.f);
    fill_ = 0;
    buf_.assign(kN, {0.0, 0.0});
    win_.resize(kN);
    double wsum = 0.0;
    for (int i = 0; i < kN; ++i) {
        win_[(size_t)i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / kN);
        wsum += win_[(size_t)i] * win_[(size_t)i];
    }
    for (auto& w : win_) w /= std::sqrt(wsum * kN / 2.0);  // un sinus d'amplitude 1 donne ~1 en puissance
    binBand_.assign(kN / 2, -1);
    for (int k = 1; k < kN / 2; ++k) {
        const float hz = (float)(k * sr_ / kN);
        if (hz < kLoHz || hz >= kHiHz) continue;
        binBand_[(size_t)k] = std::clamp((int)std::floor(kBands * std::log(hz / kLoHz) / std::log(kHiHz / kLoHz)), 0, kBands - 1);
    }
    bandSum_.fill(0.0);
    coarse_.clear();
    fluxSum_ = 0.0;
    fluxN_ = frames_ = 0;
    havePrev_ = false;
    sumMid_ = sumSide_ = sumLL_ = sumRR_ = sumLR_ = 0.0;
    peak_ = 0.f;
    blk_ = env_ = 0.0;
    blkN_ = envN_ = 0;
    blocks_.clear();
    envs_.clear();
}

void SegmentAnalyzer::push(const float* L, const float* R, size_t n) {
    const size_t blkLen = (size_t)(sr_ * 0.1), envLen = (size_t)(sr_ * 0.01);
    for (size_t i = 0; i < n; ++i) {
        const float l = std::isfinite(L[i]) ? L[i] : 0.f;
        const float r = R ? (std::isfinite(R[i]) ? R[i] : 0.f) : l;
        const double m = 0.5 * ((double)l + r), s = 0.5 * ((double)l - r);
        sumMid_ += m * m;
        sumSide_ += s * s;
        sumLL_ += (double)l * l;
        sumRR_ += (double)r * r;
        sumLR_ += (double)l * r;
        peak_ = std::max(peak_, std::max(std::fabs(l), std::fabs(r)));
        const double e = 0.5 * ((double)l * l + (double)r * r);
        blk_ += e;
        env_ += e;
        if (++blkN_ >= blkLen) {
            blocks_.push_back(db(blk_ / (double)blkN_));
            blk_ = 0.0;
            blkN_ = 0;
        }
        if (++envN_ >= envLen) {
            envs_.push_back(db(env_ / (double)envN_));
            env_ = 0.0;
            envN_ = 0;
        }
        ringL_[fill_] = l;
        ringR_[fill_] = r;
        if (++fill_ == (size_t)kN) {
            frame();
            std::memmove(ringL_.data(), ringL_.data() + kHop, sizeof(float) * (kN - kHop));
            std::memmove(ringR_.data(), ringR_.data() + kHop, sizeof(float) * (kN - kHop));
            fill_ = kN - kHop;
        }
    }
    total_ += n;
}

void SegmentAnalyzer::frame() {
    for (int i = 0; i < kN; ++i) buf_[(size_t)i] = {0.5 * ((double)ringL_[(size_t)i] + ringR_[(size_t)i]) * win_[(size_t)i], 0.0};
    fft(buf_, false);
    std::array<double, kBands> p{};
    std::array<double, 4> c{};
    double tot = 0.0;
    for (int k = 1; k < kN / 2; ++k) {
        const int b = binBand_[(size_t)k];
        if (b < 0) continue;
        const double pw = std::norm(buf_[(size_t)k]);
        p[(size_t)b] += pw;
        c[(size_t)coarseOf((float)(k * sr_ / kN))] += pw;
        tot += pw;
    }
    for (int b = 0; b < kBands; ++b) bandSum_[(size_t)b] += p[(size_t)b];
    ++frames_;
    if (db(tot) < -80.f) {  // trame silencieuse : pas de statistiques de mouvement
        havePrev_ = false;
        return;
    }
    coarse_.push_back({db(c[0]), db(c[1]), db(c[2]), db(c[3])});
    std::array<float, kBands> d{};
    for (int b = 0; b < kBands; ++b) d[(size_t)b] = std::max(-100.f, db(p[(size_t)b]));
    if (havePrev_) {
        double f = 0.0;
        for (int b = 0; b < kBands; ++b) f += std::max(0.f, d[(size_t)b] - prevDb_[(size_t)b]);
        fluxSum_ += f / kBands;
        ++fluxN_;
    }
    prevDb_ = d;
    havePrev_ = true;
}

float SegmentAnalyzer::levelDb() const {
    return total_ ? std::max(-100.f, db((sumLL_ + sumRR_) / (2.0 * (double)total_))) : -100.f;
}

void SegmentAnalyzer::compute(float* out) const {
    std::fill(out, out + kSegDim, 0.f);
    const int fr = std::max(1, frames_);
    double all = 0.0;
    for (double v : bandSum_) all += v;
    const float mean = db(all / fr / kBands);
    double cen = 0.0, logSum = 0.0;
    for (int b = 0; b < kBands; ++b) {
        const double pb = bandSum_[(size_t)b] / fr;
        out[fBand0 + b] = std::clamp(db(pb) - mean, -60.f, 30.f);
        cen += pb * std::log2(bandCenterHz(b));
        logSum += std::log(pb + 1e-14);
    }
    const float level = levelDb();
    out[fLevel] = level;
    out[fPeak] = std::max(-100.f, (float)(20.0 * std::log10(peak_ + 1e-9)));
    out[fCrest] = std::clamp(out[fPeak] - level, 0.f, 40.f);
    {
        std::vector<float> act;
        for (float b : blocks_) if (b > -70.f) act.push_back(b);
        if (act.size() >= 3) {
            std::sort(act.begin(), act.end());
            auto q = [&](double f) { return act[(size_t)std::lround(f * (double)(act.size() - 1))]; };
            out[fDyn] = q(0.95) - q(0.10);
        }
    }
    out[fCorr] = (sumLL_ > 1e-12 && sumRR_ > 1e-12) ? (float)(sumLR_ / std::sqrt(sumLL_ * sumRR_)) : 1.f;
    out[fSide] = std::clamp(db(sumSide_) - db(sumMid_), -40.f, 10.f);
    out[fCentroid] = all > 1e-14 ? (float)(cen / (all / fr)) : std::log2(1000.f);
    out[fFlatness] = all > 1e-14 ? std::clamp((float)(10.0 * (logSum / kBands) / std::log(10.0)) - mean, -60.f, 0.f) : -60.f;
    out[fFlux] = fluxN_ ? (float)(fluxSum_ / fluxN_) : 0.f;
    if (coarse_.size() >= 2) {
        for (int g = 0; g < 4; ++g) {
            double s = 0.0, s2 = 0.0;
            for (const auto& c : coarse_) {
                const double v = std::max(-100.f, c[(size_t)g]);
                s += v;
                s2 += v * v;
            }
            const double n = (double)coarse_.size(), m = s / n;
            out[fMove0 + g] = (float)std::min(30.0, std::sqrt(std::max(0.0, s2 / n - m * m)));
        }
    }
    if (!envs_.empty()) {
        // attaques : montée de plus de 9 dB en 30 ms au-dessus de -60 dB, 50 ms d'intervalle minimum
        int onsets = 0, last = -100;
        for (int i = 3; i < (int)envs_.size(); ++i) {
            const float lo = std::min({envs_[(size_t)i - 1], envs_[(size_t)i - 2], envs_[(size_t)i - 3]});
            if (envs_[(size_t)i] > -60.f && envs_[(size_t)i] - lo > 9.f && i - last >= 5) {
                ++onsets;
                last = i;
            }
        }
        const double secs = std::max(0.1, (double)envs_.size() * 0.01);
        out[fOnsets] = (float)(onsets / secs);
        const float mx = *std::max_element(envs_.begin(), envs_.end());
        size_t at = envs_.size() - 1;
        for (size_t i = 0; i < envs_.size(); ++i)
            if (envs_[i] >= mx - 3.f) { at = i; break; }
        out[fAttack] = (float)std::log10(std::max(0.005, (double)at * 0.01 + 0.005));
        const size_t s70 = std::min(envs_.size() - 1, (size_t)((double)envs_.size() * 0.7));
        out[fSustain] = std::clamp(envs_[s70] - mx, -80.f, 0.f);
    } else {
        out[fAttack] = -2.3f;
        out[fSustain] = -80.f;
    }
}

} // namespace raw2::ai
