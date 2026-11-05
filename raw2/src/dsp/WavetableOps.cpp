#include "dsp/WavetableOps.h"

#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>

namespace raw2 {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

void smoothFrame(Frame& a, float amt) {
    const int passes = 1 + (int)std::lround(amt * 6.f);
    for (int p = 0; p < passes; ++p) {
        const Frame c = a;
        for (int i = 0; i < kPoints; ++i) a[i] = (c[(i + kPoints - 1) % kPoints] + 2.f * c[i] + c[(i + 1) % kPoints]) / 4.f;
    }
}

void distortFrame(Frame& a, float amt) {
    const double g = 1.0 + amt * 8.0;
    for (float& v : a) v = (float)std::sin(kPi / 2.0 * (1.0 + amt * 1.5) * std::tanh(g * v));
}

void blendBetweenFrames(Wavetable& t, const std::vector<int>& idx, float amount) {
    const float al = amount * 0.8f;
    const auto org = t.fr;
    for (int k : idx) {
        const Frame& a = org[(size_t)std::max(0, k - 1)];
        const Frame& b = org[(size_t)std::min(t.frames() - 1, k + 1)];
        for (int i = 0; i < kPoints; ++i) t.fr[k][i] = org[k][i] * (1.f - al) + (a[i] + b[i]) * 0.5f * al;
    }
}

void interpolateTable(Wavetable& t) {
    const int n = t.frames();
    if (n < 3) return;
    const Frame A = t.fr[0], B = t.fr[(size_t)n - 1];
    for (int k = 1; k < n - 1; ++k) {
        for (int i = 0; i < kPoints; ++i) t.fr[(size_t)k][(size_t)i] = A[(size_t)i] + (B[(size_t)i] - A[(size_t)i]) * (float)k / (float)(n - 1);
        t.touch(k);
    }
}

void resizeTable(Wavetable& t, int n) {
    n = std::clamp(n, 1, kMaxFrames);
    const int m = t.frames();
    if (n == m || m < 1) return;
    std::vector<Frame> fr((size_t)n);
    std::shared_ptr<std::vector<float>> hi;
    const bool anyHi = t.hi != nullptr;
    if (anyHi) hi = std::make_shared<std::vector<float>>((size_t)n * kHiLen, 0.f);
    std::vector<float> a(kHiLen), b(kHiLen);
    for (int k = 0; k < n; ++k) {
        const float fp = n > 1 ? (float)k / (float)(n - 1) * (float)(m - 1) : 0.f;
        const int i0 = std::min(m - 1, (int)fp), i1 = std::min(m - 1, i0 + 1);
        const float z = fp - (float)i0;
        for (int i = 0; i < kPoints; ++i) fr[(size_t)k][(size_t)i] = t.fr[(size_t)i0][(size_t)i] * (1.f - z) + t.fr[(size_t)i1][(size_t)i] * z;
        if (anyHi) {
            frameHi(t, i0, a.data());
            frameHi(t, i1, b.data());
            for (int i = 0; i < kHiLen; ++i) (*hi)[(size_t)k * kHiLen + (size_t)i] = a[(size_t)i] * (1.f - z) + b[(size_t)i] * z;
        }
    }
    t.fr = std::move(fr);
    if (anyHi) {
        t.hi = hi;
        t.hiOk.assign((size_t)n, true);
    } else {
        t.dropHi();
    }
}

void applyPresetShape(Wavetable& t, Shape s, const std::vector<int>& idx, bool wholeTable) {
    float mb = 0.f;
    if (s == Shape::Saw) mb = 1.f;
    if (s == Shape::Complex) mb = 0.5f;
    for (int k : idx) {
        const float m = wholeTable ? (float)k / (float)std::max(1, t.frames() - 1) : mb;
        t.touch(k);
        for (int i = 0; i < kPoints; ++i) t.fr[k][i] = genShape(s, (float)i / kPoints, m);
    }
}

void penStroke(Frame& a, int x0, float v0, int x1, float v1) {
    const int n = std::abs(x1 - x0);
    for (int s = 0; s <= n; ++s) {
        const int x = n ? (int)std::lround(x0 + (double)(x1 - x0) * s / n) : x1;
        a[std::clamp(x, 0, kPoints - 1)] = n ? v0 + (v1 - v0) * (float)s / (float)n : v1;
    }
}

void penSmooth(Frame& a, int x) {
    const Frame c = a;
    for (int d = -14; d <= 14; ++d) {
        const int j = (x + d + 256) & 255;
        const float w = 1.f - (float)std::abs(d) / 15.f;
        a[j] += ((c[(j + 255) & 255] + c[j] + c[(j + 1) & 255]) / 3.f - c[j]) * 0.7f * w;
    }
}

void penDistort(Frame& a, int x, float amount) {
    const double g = 1.0 + amount * 6.0;
    for (int d = -16; d <= 16; ++d) {
        const int j = (x + d + 256) & 255;
        const float w = 1.f - (float)std::abs(d) / 17.f;
        a[j] = a[j] * (1.f - w) + w * (float)std::sin(kPi / 2.0 * 1.3 * std::tanh(g * a[j]));
    }
}

bool singleCycleFrom(const std::vector<float>& data, std::vector<float>& tab) {
    const size_t n = data.size();
    if (n < 64) return false;
    const size_t L = std::min<size_t>(n, 4096), step = std::max<size_t>(1, L >> 1);
    size_t best = 0;
    double bestE = -1.0;
    for (size_t st = 0; st + L <= n; st += step) {
        double e = 0.0;
        for (size_t i = st; i < st + L; i += 4) e += (double)data[i] * data[i];
        if (e > bestE) { bestE = e; best = st; }
    }
    const int N = 512;
    tab.assign(N, 0.f);
    for (int i = 0; i < N; ++i) {
        const double f = (double)best + (double)i / N * (double)(L - 1);
        const size_t k = (size_t)f;
        const double fr = f - (double)k;
        const float b = k + 1 < n ? data[k + 1] : 0.f;
        tab[i] = (float)(data[k] * (1.0 - fr) + b * fr);
    }
    double mean = 0.0;
    for (float v : tab) mean += v;
    mean /= N;
    const double dj = tab[N - 1] - tab[0];
    float pk = 1e-9f;
    for (int i = 0; i < N; ++i) {
        tab[i] = (float)(tab[i] - mean - dj * ((double)i / (N - 1)));
        pk = std::max(pk, std::fabs(tab[i]));
    }
    for (float& v : tab) v /= pk;
    return true;
}

// --- audio -> table : hauteur, cycles, resynthèse ------------------------------------------
namespace {

// cycle de longueur P (fractionnaire) lu à partir de pos, sur 2048 points (interpolation cubique),
// sans saut au bouclage (dérive linéaire retirée)
void readCycle(const std::vector<float>& x, double pos, double P, float* out) {
    const size_t n = x.size();
    auto at = [&](double t) {
        const long i = (long)std::floor(t);
        const double f = t - (double)i;
        auto s = [&](long j) { return j >= 0 && (size_t)j < n ? (double)x[(size_t)j] : 0.0; };
        const double p0 = s(i - 1), p1 = s(i), p2 = s(i + 1), p3 = s(i + 2);
        return p1 + 0.5 * f * (p2 - p0 + f * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + f * (3.0 * (p1 - p2) + p3 - p0)));
    };
    const double a = at(pos), b = at(pos + P);
    for (int j = 0; j < kHiLen; ++j) {
        const double u = (double)j / kHiLen;
        out[j] = (float)(at(pos + u * P) - (b - a) * u);
    }
}

// spectre d'un cycle de 2048 points
std::vector<std::complex<double>> spectrum(const float* c) {
    std::vector<std::complex<double>> s(kHiLen);
    for (int i = 0; i < kHiLen; ++i) s[(size_t)i] = c[i];
    fft(s, false);
    return s;
}

void fromSpectrum(std::vector<std::complex<double>>& s, float* out) {
    s[0] = 0.0;
    s[kHiLen / 2] = 0.0;
    fft(s, true);
    double pk = 1e-9;
    for (int i = 0; i < kHiLen; ++i) pk = std::max(pk, std::fabs(s[(size_t)i].real()));
    for (int i = 0; i < kHiLen; ++i) out[i] = (float)(s[(size_t)i].real() / pk);
}

// période près de P (0,8 P .. 1,25 P) : minimum de la différence au point pos (0 si hors du son)
double refinePeriod(const std::vector<float>& x, size_t at, double P) {
    const size_t W = 1024;
    const int lo = std::max(2, (int)std::floor(P * 0.8)), hi = (int)std::ceil(P * 1.25);
    if (x.size() < at + W + (size_t)hi + 2) return 0.0;
    std::vector<double> d((size_t)hi + 2, 0.0);
    int best = lo;
    for (int tau = lo - 1; tau <= hi + 1; ++tau) {
        double s = 0.0;
        for (size_t i = 0; i < W; ++i) {
            const double e = (double)x[at + i] - (double)x[at + i + (size_t)tau];
            s += e * e;
        }
        d[(size_t)tau] = s;
        if (tau >= lo && tau <= hi && s < d[(size_t)best]) best = tau;
    }
    const double a = d[(size_t)best - 1], b = d[(size_t)best], c = d[(size_t)best + 1];
    const double den = a - 2.0 * b + c;
    return (double)best + (std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
}

} // namespace

double detectPeriod(const std::vector<float>& x, double sr, size_t at) {
    // YIN (différence cumulée normalisée) sur 2048 échantillons, périodes de 30 Hz à 2 kHz
    const size_t W = 2048;
    const int tmin = std::max(2, (int)(sr / 2000.0)), tmax = std::min((int)(sr / 30.0), (int)W - 2);
    if (x.size() < at + W + (size_t)tmax + 2 || tmax <= tmin) return 0.0;
    std::vector<double> d((size_t)tmax + 2, 0.0);
    for (int tau = 1; tau <= tmax + 1; ++tau) {
        double s = 0.0;
        for (size_t i = 0; i < W; ++i) {
            const double e = (double)x[at + i] - (double)x[at + i + (size_t)tau];
            s += e * e;
        }
        d[(size_t)tau] = s;
    }
    double run = 0.0;
    std::vector<double> cm((size_t)tmax + 2, 1.0);
    for (int tau = 1; tau <= tmax + 1; ++tau) {
        run += d[(size_t)tau];
        cm[(size_t)tau] = run > 0.0 ? d[(size_t)tau] * (double)tau / run : 1.0;
    }
    int best = -1;
    for (int tau = tmin; tau <= tmax; ++tau)
        if (cm[(size_t)tau] < 0.15 && cm[(size_t)tau] <= cm[(size_t)tau - 1] && cm[(size_t)tau] <= cm[(size_t)tau + 1]) { best = tau; break; }
    if (best < 0) {  // pas de minimum net : le plus profond, s'il reste crédible
        double m = 1e9;
        for (int tau = tmin; tau <= tmax; ++tau)
            if (cm[(size_t)tau] < m) { m = cm[(size_t)tau]; best = tau; }
        if (m > 0.35) return 0.0;
    }
    // interpolation parabolique du minimum
    const double a = cm[(size_t)best - 1], b = cm[(size_t)best], c = cm[(size_t)best + 1];
    const double den = a - 2.0 * b + c;
    return (double)best + (std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
}

int tableFromAudioEx(const std::vector<float>& data, double sr, const std::string& baseName, int mode, int frames, Wavetable& out) {
    const size_t n = data.size();
    out.name = baseName.empty() ? "wavetable" : baseName;
    out.spec.reset();
    frames = std::clamp(frames, 1, kMaxFrames);
    if (n < 64) return 0;
    auto finish = [&](std::shared_ptr<std::vector<float>> hi, int nf) {
        out.fr.assign((size_t)nf, Frame{});
        for (int k = 0; k < nf; ++k) {
            decimateFrame(hi->data() + (size_t)k * kHiLen, out.fr[(size_t)k]);
            normFrame(out.fr[(size_t)k]);
        }
        out.hi = hi;
        out.hiOk.assign((size_t)nf, true);
    };
    // 1. table d'onde toute faite (Serum, Vital, export RAW 2) : des frames de 2048 points bout à bout
    if (mode != kImpCycles && mode != kImpSpectral && n % kHiLen == 0 && n / kHiLen >= 1 && n / kHiLen <= (size_t)kMaxFrames) {
        const int nf = (int)(n / kHiLen);
        auto hi = std::make_shared<std::vector<float>>((size_t)nf * kHiLen);
        for (int k = 0; k < nf; ++k) {
            float* f = hi->data() + (size_t)k * kHiLen;
            double mean = 0.0, pk = 1e-9;
            for (int i = 0; i < kHiLen; ++i) mean += data[(size_t)k * kHiLen + (size_t)i];
            mean /= kHiLen;
            for (int i = 0; i < kHiLen; ++i) { f[i] = (float)(data[(size_t)k * kHiLen + (size_t)i] - mean); pk = std::max(pk, (double)std::fabs(f[i])); }
            for (int i = 0; i < kHiLen; ++i) f[i] = (float)(f[i] / pk);
        }
        finish(hi, nf);
        return kImpTable;
    }
    // 2. son quelconque : hauteur (YIN) dans la partie la plus forte, puis un cycle par frame
    size_t loud = 0;
    {
        double bestE = -1.0;
        const size_t win = 2048;
        for (size_t st = 0; st + win + 1700 < n; st += 1024) {
            double e = 0.0;
            for (size_t i = st; i < st + win; i += 4) e += (double)data[i] * data[i];
            if (e > bestE) { bestE = e; loud = st; }
        }
    }
    const double P = detectPeriod(data, sr, loud);
    if (P < 2.0) {
        // pas de hauteur : découpe régulière du son (comme avant), frames de 2048 échantillons
        if (n < (size_t)kHiLen * 2) {
            std::vector<float> tab;
            if (!singleCycleFrom(data, tab)) return 0;
            auto hi = std::make_shared<std::vector<float>>((size_t)kHiLen);
            for (int i = 0; i < kHiLen; ++i) (*hi)[(size_t)i] = tab[(size_t)(i * 512 / kHiLen)];
            out.fr.assign(1, Frame{});
            finish(hi, 1);
            resizeTable(out, std::min(frames, kFrames));
            return kImpSlices;
        }
        const int nf = std::min(frames, (int)(n / kHiLen));
        auto hi = std::make_shared<std::vector<float>>((size_t)nf * kHiLen);
        for (int k = 0; k < nf; ++k) {
            const size_t st = nf > 1 ? (size_t)((double)k * (double)(n - kHiLen) / (double)(nf - 1)) : 0;
            float* f = hi->data() + (size_t)k * kHiLen;
            for (int i = 0; i < kHiLen; ++i) f[i] = data[st + (size_t)i];
            // fenêtre douce au raccord (la découpe n'est pas un cycle)
            for (int i = 0; i < 64; ++i) {
                const float w = (float)i / 64.f;
                f[i] *= w;
                f[kHiLen - 1 - i] *= w;
            }
            double mean = 0.0, pk = 1e-9;
            for (int i = 0; i < kHiLen; ++i) mean += f[i];
            mean /= kHiLen;
            for (int i = 0; i < kHiLen; ++i) { f[i] = (float)(f[i] - mean); pk = std::max(pk, (double)std::fabs(f[i])); }
            for (int i = 0; i < kHiLen; ++i) f[i] = (float)(f[i] / pk);
        }
        finish(hi, nf);
        return kImpSlices;
    }
    // début du son (au-dessus de -40 dB de la crête) et fin (dernier cycle complet)
    float peak = 1e-9f;
    for (float v : data) peak = std::max(peak, std::fabs(v));
    size_t start = 0;
    while (start < n && std::fabs(data[start]) < peak * 0.01f) ++start;
    size_t end = n;
    while (end > start && std::fabs(data[end - 1]) < peak * 0.01f) --end;
    if (end <= start + (size_t)(3.0 * P)) { start = 0; end = n; }
    const double span = (double)(end - start) - 2.0 * P;
    const int nf = std::clamp(frames, 1, std::max(1, (int)(span / P)));
    auto hi = std::make_shared<std::vector<float>>((size_t)nf * kHiLen);
    std::vector<std::vector<std::complex<double>>> specs((size_t)nf);
    double localP = P;
    for (int k = 0; k < nf; ++k) {
        const double pos = (double)start + (nf > 1 ? (double)k / (double)(nf - 1) * std::max(0.0, span) : 0.0);
        // la hauteur peut glisser (vibrato, glissando) : période réestimée sur place, près de P
        const double lp = refinePeriod(data, (size_t)pos, P);
        if (lp > 0.8 * P && lp < 1.25 * P) localP = lp;
        float* f = hi->data() + (size_t)k * kHiLen;
        readCycle(data, pos, localP, f);
        specs[(size_t)k] = spectrum(f);
    }
    // phases : CYCLES aligne chaque cycle sur sa fondamentale (le morph entre frames ne se creuse
    // pas) ; RESYNTHÈSE garde les amplitudes de chaque frame et les phases de la frame du milieu
    const auto& mid = specs[(size_t)nf / 2];
    for (int k = 0; k < nf; ++k) {
        auto& S = specs[(size_t)k];
        float* f = hi->data() + (size_t)k * kHiLen;
        if (mode == kImpSpectral) {
            for (int h = 1; h < kHiLen / 2; ++h) {
                const double mag = std::abs(S[(size_t)h]);
                const double ph = std::arg(mid[(size_t)h]);
                S[(size_t)h] = std::polar(mag, ph);
                S[(size_t)(kHiLen - h)] = std::conj(S[(size_t)h]);
            }
        } else {
            const double phi = std::arg(S[1]) - std::arg(mid[1]);
            for (int h = 1; h < kHiLen / 2; ++h) {
                S[(size_t)h] *= std::polar(1.0, -phi * (double)h);
                S[(size_t)(kHiLen - h)] = std::conj(S[(size_t)h]);
            }
        }
        fromSpectrum(S, f);
    }
    finish(hi, nf);
    return mode == kImpSpectral ? kImpSpectral : kImpCycles;
}

bool tableFromAudio(const std::vector<float>& data, const std::string& baseName, Wavetable& out) {
    return tableFromAudioEx(data, 44100.0, baseName, kImpAuto, kFrames, out) != 0;
}

} // namespace raw2
