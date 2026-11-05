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
    t.fr.assign(kFrames, Frame{});
    t.dropHi();
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
    t.fr.assign(kFrames, Frame{});
    t.dropHi();
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
    const int nf = w.frames();
    if (nf <= 0) return 0.f;
    const float fp = pos * (float)(nf - 1);
    const int i0 = std::min(nf - 1, (int)fp), i1 = std::min(nf - 1, i0 + 1);
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

void MipTable::allocate(int frames) {
    frames_ = std::clamp(frames, 1, kMaxFrames);
    size_t o = 0;
    for (int L = 0; L < kMipLevels; ++L) {
        off_[L] = o;
        o += (size_t)frames_ * (size_t)(kMipLens[L] + 4);
    }
    data_.assign(o, 0.f);
}

void MipTable::buildFrame(const Wavetable& t, int f) {
    // spectre de la frame : forme calculée rendue à 4096 points (harmoniques jusqu'à 511 pour les
    // notes graves), frame importée de 2048 points, sinon la frame éditée de 256 points (127 harmoniques)
    constexpr int kHi = 4096;
    const bool calc = t.spec.has_value();
    const bool imp = !calc && t.hiFrame(f);
    const int n = calc ? kHi : (imp ? kHiLen : kPoints);
    std::vector<std::complex<double>> spec((size_t)n);
    if (calc) {
        const ShapeSpec& sp = *t.spec;
        const float m = sp.morphStart + (sp.morphEnd - sp.morphStart) * (float)f / (float)std::max(1, frames_ - 1);
        double mean = 0.0, pk = 1e-9;
        std::vector<double> w((size_t)n);
        for (int i = 0; i < n; ++i) { w[(size_t)i] = genShape(sp.shape, (float)i / (float)n, m); mean += w[(size_t)i]; }
        mean /= n;
        for (double& v : w) { v -= mean; pk = std::max(pk, std::fabs(v)); }  // comme normFrame
        for (int i = 0; i < n; ++i) spec[(size_t)i] = w[(size_t)i] / pk;
    } else if (imp) {
        const float* h = t.hi->data() + (size_t)f * kHiLen;
        for (int i = 0; i < kHiLen; ++i) spec[(size_t)i] = h[i];
    } else {
        for (int i = 0; i < kPoints; ++i) spec[(size_t)i] = t.fr[(size_t)f][(size_t)i];
    }
    fft(spec, false);
    const int hMax = n / 2 - 1;
    // deux niveaux par IFFT (même longueur) : leurs spectres sont hermitiens (signaux réels), donc
    // IFFT(A + iB) = a + ib, le niveau pair sort en partie réelle, l'impair en partie imaginaire
    for (int level = 0; level < kMipLevels; level += 2) {
        const int len = kMipLens[level];
        std::vector<std::complex<double>> buf((size_t)len);
        const std::complex<double> I(0.0, 1.0);
        const double scale = (double)len / (double)n;  // spectre recopié dans len points : même forme
        for (int k = 0; k < 2 && level + k < kMipLevels; ++k) {
            const int H = std::min({kMipHarmonics[level + k], hMax, len / 2 - 1});
            const std::complex<double> mul = k ? I * scale : std::complex<double>(scale, 0.0);
            for (int h = 1; h <= H; ++h) {
                buf[(size_t)h] += spec[(size_t)h] * mul;
                buf[(size_t)(len - h)] += spec[(size_t)(n - h)] * mul;
            }
        }
        fft(buf, true);
        for (int k = 0; k < 2 && level + k < kMipLevels; ++k) {
            float* out = data_.data() + off_[level + k] + (size_t)f * (size_t)(len + 4) + 1;
            for (int i = 0; i < len; ++i) out[i] = (float)(k ? buf[(size_t)i].imag() : buf[(size_t)i].real());
            out[-1] = out[len - 1];  // gardes circulaires
            out[len] = out[0];
            out[len + 1] = out[1];
            out[len + 2] = out[2];
        }
    }
}

void MipTable::build(const Wavetable& t) {
    allocate(t.frames());
    for (int f = 0; f < frames_; ++f) buildFrame(t, f);
}

void MipTable::rebuildFrames(const Wavetable& t, const std::vector<bool>& dirty) {
    if (t.frames() != frames_) { build(t); return; }
    for (int f = 0; f < frames_ && f < (int)dirty.size(); ++f)
        if (dirty[(size_t)f]) buildFrame(t, f);
}

float warpPhasePreview(float u, int wm, float w, float other) {
    if (w <= 0.0005f) return u;
    auto wrap = [](float x) { x -= std::floor(x); return x >= 0.f && x < 1.f ? x : 0.f; };
    switch (wm) {
        case 0: return wrap(u + w * 0.22f * std::sin(6.2831853f * u));
        case 1: return wrap(u * (1.f + 7.f * w));
        case 2: case 3: {
            const float x = 2.f * u - 1.f, e = wm == 2 ? 1.f / (1.f + 4.f * w) : 1.f + 4.f * w;
            const float y = std::pow(std::fabs(x), e);
            return std::min(0.99999f, 0.5f * ((x < 0.f ? -y : y) + 1.f));
        }
        case 4: { const float d = 1.f - 0.95f * w; return u < d ? u / d : 0.f; }
        case 5: { const float k = 0.5f - 0.45f * w; return u < k ? 0.5f * u / k : std::min(0.99999f, 0.5f + 0.5f * (u - k) / (1.f - k)); }
        case 7: { const float p = 1.f - 0.5f * w; return u < p ? u / p : std::min(0.99999f, (1.f - u) / std::max(1e-4f, 1.f - p)); }
        case 9: return wrap(u + w * w * 3.f * other);
        default: return u;
    }
}

float warpAmpPreview(float s, float u, int wm, float w, float other) {
    if (w <= 0.0005f) return s;
    switch (wm) {
        case 6: return u >= 1.f - w ? -s : s;
        case 8: { const float q = std::exp2(1.f + 9.f * (1.f - w)); return std::round(s * q) / q; }
        case 10: return s * (1.f + w * other) / (1.f + w);
        case 11: return s + w * (s * other - s);
        default: return s;
    }
}

void frameHi(const Wavetable& t, int f, float* out) {
    if (t.hiFrame(f)) {
        std::copy(t.hi->data() + (size_t)f * kHiLen, t.hi->data() + (size_t)(f + 1) * kHiLen, out);
        return;
    }
    std::vector<std::complex<double>> a(kPoints), b(kHiLen);
    for (int i = 0; i < kPoints; ++i) a[(size_t)i] = t.fr[(size_t)f][(size_t)i];
    fft(a, false);
    const double sc = (double)kHiLen / (double)kPoints;
    for (int h = 1; h < kPoints / 2; ++h) {
        b[(size_t)h] = a[(size_t)h] * sc;
        b[(size_t)(kHiLen - h)] = a[(size_t)(kPoints - h)] * sc;
    }
    fft(b, true);
    for (int i = 0; i < kHiLen; ++i) out[i] = (float)b[(size_t)i].real();
}

void decimateFrame(const float* hi, Frame& out) {
    std::vector<std::complex<double>> a(kHiLen), b(kPoints);
    for (int i = 0; i < kHiLen; ++i) a[(size_t)i] = hi[i];
    fft(a, false);
    const double sc = (double)kPoints / (double)kHiLen;
    for (int h = 1; h < kPoints / 2; ++h) {
        b[(size_t)h] = a[(size_t)h] * sc;
        b[(size_t)(kPoints - h)] = a[(size_t)(kHiLen - h)] * sc;
    }
    fft(b, true);
    for (int i = 0; i < kPoints; ++i) out[(size_t)i] = (float)b[(size_t)i].real();
}

} // namespace raw2
