#include "dsp/WavetableOps.h"

#include <algorithm>
#include <cmath>

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
        const Frame& a = org[std::max(0, k - 1)];
        const Frame& b = org[std::min(kFrames - 1, k + 1)];
        for (int i = 0; i < kPoints; ++i) t.fr[k][i] = org[k][i] * (1.f - al) + (a[i] + b[i]) * 0.5f * al;
    }
}

void interpolateTable(Wavetable& t) {
    const Frame A = t.fr[0], B = t.fr[kFrames - 1];
    for (int k = 1; k < kFrames - 1; ++k)
        for (int i = 0; i < kPoints; ++i) t.fr[k][i] = A[i] + (B[i] - A[i]) * (float)k / 63.f;
}

void applyPresetShape(Wavetable& t, Shape s, const std::vector<int>& idx, bool wholeTable) {
    float mb = 0.f;
    if (s == Shape::Saw) mb = 1.f;
    if (s == Shape::Complex) mb = 0.5f;
    for (int k : idx) {
        const float m = wholeTable ? (float)k / 63.f : mb;
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

bool tableFromAudio(const std::vector<float>& data, const std::string& baseName, Wavetable& out) {
    const size_t n = data.size();
    out.name = baseName.empty() ? "wavetable" : baseName;
    out.spec.reset();
    if (n >= 4096) {
        const int Fn = (int)std::min<size_t>(64, n / 2048);
        std::vector<Frame> fr((size_t)Fn);
        for (int k = 0; k < Fn; ++k) {
            const size_t st = Fn > 1 ? (size_t)((double)k * (double)(n - 2048) / (double)(Fn - 1)) : 0;
            for (int i = 0; i < kPoints; ++i) {
                double s = 0.0;
                for (int q = 0; q < 8; ++q) {
                    const size_t j = st + (size_t)i * 8 + (size_t)q;
                    s += j < n ? data[j] : 0.f;
                }
                fr[(size_t)k][(size_t)i] = (float)(s / 8.0);
            }
            normFrame(fr[(size_t)k]);
        }
        for (int k = 0; k < kFrames; ++k) {
            const double fp = Fn > 1 ? (double)k / 63.0 * (Fn - 1) : 0.0;
            const int i0 = (int)fp, i1 = std::min(Fn - 1, i0 + 1);
            const float z = (float)(fp - i0);
            for (int i = 0; i < kPoints; ++i)
                out.fr[k][i] = fr[(size_t)i0][(size_t)i] * (1.f - z) + fr[(size_t)i1][(size_t)i] * z;
        }
        return true;
    }
    std::vector<float> tab;
    if (!singleCycleFrom(data, tab)) return false;
    for (int k = 0; k < kFrames; ++k)
        for (int i = 0; i < kPoints; ++i) out.fr[k][i] = tab[(size_t)i * 2];
    return true;
}

} // namespace raw2
