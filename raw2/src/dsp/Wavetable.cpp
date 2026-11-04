#include "dsp/Wavetable.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace raw2 {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

float genShape(Shape s, float uf, float mf) {
    const double u = uf, m = mf, x = u * kPi * 2.0;
    switch (s) {
        case Shape::Sine: return (float)std::sin(x + m * 3.0 * std::sin(x));
        case Shape::Saw: {
            double v = 0.0;
            const int H = 2 + (int)std::lround(m * 30.0);
            // sin(kx) par récurrence de Tchebychev (un seul sinus, précis en double pour k <= 32)
            const double c2 = 2.0 * std::cos(x);
            double s0 = 0.0, s1 = std::sin(x);
            for (int k = 1; k <= H; ++k) {
                v += s1 / k;
                const double s2 = c2 * s1 - s0;
                s0 = s1;
                s1 = s2;
            }
            return (float)v;
        }
        case Shape::Square: return std::fmod(u, 1.0) < 0.5 - 0.42 * m ? 1.f : -1.f;
        case Shape::Tri: return (float)std::sin(kPi / 2.0 * (1.0 + 3.0 * m) * (2.0 / kPi) * std::asin(std::sin(x)));
        case Shape::Complex: {
            static const double A[7] = {1, 0.5, 0.7, 0.3, 0.45, 0.2, 0.3};
            double v = 0.0;
            for (int k = 1; k <= 7; ++k) v += A[k - 1] * std::sin(k * x + k * 1.7 + m * 2.5 * k);
            return (float)v;
        }
        case Shape::Noise: {
            const int n = 32;
            const double f = std::fmod(u, 1.0) * n;
            const int i = (int)std::floor(f);
            const double fr = f - i;
            auto h = [&](int j) {
                const double v = std::sin((double)(((j % n) + n) % n) * 12.9898 + m * 78.233) * 43758.5453;
                return (v - std::floor(v)) * 2.0 - 1.0;
            };
            const double c = (1.0 - std::cos(fr * kPi)) / 2.0;
            return (float)(h(i) * (1.0 - c) + h(i + 1) * c);
        }
        case Shape::Glass:
            return (float)(std::sin(x) + 0.5 * m * std::sin(3 * x + m * 2) + 0.45 * m * std::sin(7 * x + 1.3) +
                           0.3 * m * m * std::sin(11 * x) + 0.2 * m * std::sin(2 * x));
        case Shape::Metal: {
            double v = 0.0;
            const double c2 = 2.0 * std::cos(x);
            double s0 = 0.0, s1 = std::sin(x);
            for (int k = 1; k <= 24; ++k) {
                v += ((k % 2) ? 1.0 : 1.0 - m) * s1 / k;
                const double s2 = c2 * s1 - s0;
                s0 = s1;
                s1 = s2;
            }
            return (float)v;
        }
    }
    return 0.f;
}

void normFrame(Frame& a) {
    double mean = 0.0;
    for (float v : a) mean += v;
    mean /= kPoints;
    float pk = 1e-9f;
    for (float& v : a) {
        v -= (float)mean;
        pk = std::max(pk, std::fabs(v));
    }
    for (float& v : a) v /= pk;
}

void buildFromSpec(Wavetable& t, const ShapeSpec& spec, const std::string& name) {
    for (int k = 0; k < kFrames; ++k) {
        const float m = spec.morphStart + (spec.morphEnd - spec.morphStart) * (float)k / 63.f;
        for (int i = 0; i < kPoints; ++i) t.fr[k][i] = genShape(spec.shape, (float)i / kPoints, m);
        normFrame(t.fr[k]);
    }
    t.name = name;
    t.spec = spec;
}

void buildFallback(Wavetable& t, float hh) {
    // Reprend l'oscillateur additif de la maquette : la position règle la pente des partielles.
    for (int k = 0; k < kFrames; ++k) {
        const double p = (double)k / 63.0, dec = 1.6 - 0.9 * p;
        for (int i = 0; i < kPoints; ++i) {
            const double x = (double)i / kPoints;
            double s = 0.0;
            for (int h = 1; h <= 8; ++h) {
                const double a = h == 1 ? 1.0 : hh * 1.6 * std::pow((double)h, -dec);
                s += a * std::sin(2.0 * kPi * h * x);
            }
            t.fr[k][i] = (float)s;
        }
        normFrame(t.fr[k]);
    }
    t.name = "wavetable";
    t.spec.reset();
}

float sampleTable(const Wavetable& w, float pos, float u) {
    pos = pos < 0.f ? 0.f : (pos > 1.f ? 1.f : pos);
    const float fp = pos * (kFrames - 1);
    const int i0 = (int)fp, i1 = std::min(kFrames - 1, i0 + 1);
    const float fz = fp - (float)i0;
    const float f = (u - std::floor(u)) * kPoints;
    int k = (int)f;
    const float fr = f - (float)k;
    const int k1 = (k + 1) & 255;
    k &= 255;
    const Frame& A = w.fr[i0];
    const Frame& B = w.fr[i1];
    return (A[k] * (1 - fr) + A[k1] * fr) * (1 - fz) + (B[k] * (1 - fr) + B[k1] * fr) * fz;
}

static const char* kShapeKeys[] = {"sine", "saw", "square", "tri", "complex", "noise", "glass", "metal"};
const char* shapeKey(Shape s) { return kShapeKeys[(int)s]; }
bool shapeFromKey(const char* key, Shape& out) {
    for (int i = 0; i < 8; ++i)
        if (std::strcmp(kShapeKeys[i], key) == 0) { out = (Shape)i; return true; }
    return false;
}

void MipTable::buildFrame(const Wavetable& t, int f) {
    // spectre de la frame : forme calculée rendue à 4096 points (harmoniques jusqu'à 511 pour les
    // notes graves), sinon la frame éditée de 256 points (127 harmoniques au plus)
    constexpr int kHi = 4096;
    const bool hi = t.spec.has_value();
    const int n = hi ? kHi : kPoints;
    std::vector<std::complex<double>> spec((size_t)n);
    if (hi) {
        const ShapeSpec& sp = *t.spec;
        const float m = sp.morphStart + (sp.morphEnd - sp.morphStart) * (float)f / 63.f;
        double mean = 0.0, pk = 1e-9;
        std::vector<double> w((size_t)n);
        for (int i = 0; i < n; ++i) { w[(size_t)i] = genShape(sp.shape, (float)i / (float)n, m); mean += w[(size_t)i]; }
        mean /= n;
        for (double& v : w) { v -= mean; pk = std::max(pk, std::fabs(v)); }  // comme normFrame
        for (int i = 0; i < n; ++i) spec[(size_t)i] = w[(size_t)i] / pk;
    } else {
        for (int i = 0; i < kPoints; ++i) spec[(size_t)i] = t.fr[f][i];
    }
    fft(spec, false);
    const int hMax = n / 2 - 1;
    std::vector<std::complex<double>> buf(kMipLen);
    // deux niveaux par IFFT : leurs spectres sont hermitiens (signaux réels), donc
    // IFFT(A + iB) = a + ib, le niveau pair sort en partie réelle, l'impair en partie imaginaire
    const double scale = (double)kMipLen / (double)n;  // spectre recopié dans 2048 points : même forme, plus fine
    for (int level = 0; level < kMipLevels; level += 2) {
        std::fill(buf.begin(), buf.end(), std::complex<double>(0.0, 0.0));
        const std::complex<double> I(0.0, 1.0);
        for (int k = 0; k < 2 && level + k < kMipLevels; ++k) {
            const int H = std::min(kMipHarmonics[level + k], hMax);
            const std::complex<double> mul = k ? I * scale : std::complex<double>(scale, 0.0);
            for (int h = 1; h <= H; ++h) {
                buf[(size_t)h] += spec[(size_t)h] * mul;
                buf[(size_t)(kMipLen - h)] += spec[(size_t)(n - h)] * mul;
            }
        }
        fft(buf, true);
        for (int k = 0; k < 2 && level + k < kMipLevels; ++k) {
            float* out = data_.data() + ((size_t)(level + k) * kFrames + (size_t)f) * kMipStride + 1;
            for (int i = 0; i < kMipLen; ++i) out[i] = (float)(k ? buf[(size_t)i].imag() : buf[(size_t)i].real());
            out[-1] = out[kMipLen - 1];  // gardes circulaires
            out[kMipLen] = out[0];
            out[kMipLen + 1] = out[1];
            out[kMipLen + 2] = out[2];
        }
    }
}

void MipTable::build(const Wavetable& t) {
    for (int f = 0; f < kFrames; ++f) buildFrame(t, f);
}

void MipTable::rebuildFrames(const Wavetable& t, const std::array<bool, kFrames>& dirty) {
    for (int f = 0; f < kFrames; ++f)
        if (dirty[f]) buildFrame(t, f);
}

} // namespace raw2
