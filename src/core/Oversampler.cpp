#include "Oversampler.h"

#include <cmath>

namespace tw {

namespace {
constexpr double kPi = 3.14159265358979323846;

void transitionParams(double transition, double& k, double& q) {
    k = std::tan((1.0 - transition * 2.0) * kPi / 4.0);
    k *= k;
    const double kksqrt = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    const double e2 = e * e;
    const double e4 = e2 * e2;
    q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
}

double accNum(double q, int order, int c) {
    double acc = 0.0, term;
    int i = 0, sign = 1;
    do {
        term = std::pow(q, (double)(i * (i + 1))) * std::sin((i * 2 + 1) * c * kPi / order) * sign;
        acc += term;
        sign = -sign;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 100);
    return acc;
}

double accDen(double q, int order, int c) {
    double acc = 0.0, term;
    int i = 1, sign = -1;
    do {
        term = std::pow(q, (double)(i * i)) * std::cos(i * 2 * c * kPi / order) * sign;
        acc += term;
        sign = -sign;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 100);
    return acc;
}
} // namespace

void designHalfband(double* coefs, int numCoefs, double transition) {
    double k, q;
    transitionParams(transition, k, q);
    const int order = numCoefs * 2 + 1;
    for (int index = 0; index < numCoefs; ++index) {
        const int c = index + 1;
        const double num = accNum(q, order, c) * std::pow(q, 0.25);
        const double den = accDen(q, order, c) + 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coefs[index] = (1.0 - x) / (1.0 + x);
    }
}

void HalfbandStage::init(int numCoefs, double transition) {
    if (numCoefs > kMaxCoefs) numCoefs = kMaxCoefs;
    numCoefs &= ~1;  // even: both paths get the same number of sections
    double c[kMaxCoefs];
    designHalfband(c, numCoefs, transition);
    numSections_ = numCoefs / 2;
    for (int s = 0; s < numSections_; ++s)
        coef_[s] = V4((float)c[2 * s], (float)c[2 * s], (float)c[2 * s + 1], (float)c[2 * s + 1]);
    reset();
}

void HalfbandStage::reset() {
    for (int s = 0; s < kMaxCoefs / 2; ++s) upX_[s] = upY_[s] = dnX_[s] = dnY_[s] = V4(0.f);
}

void HalfbandStage::upsample(V4 in, V4& out0, V4& out1) {
    V4 x = dupLo(in);
    for (int s = 0; s < numSections_; ++s) {
        V4 y = coef_[s] * (x - upY_[s]) + upX_[s];
        upX_[s] = x;
        upY_[s] = y;
        x = y;
    }
    out0 = x;          // lanes 0/1: even output sample
    out1 = hiToLo(x);  // lanes 2/3: odd output sample
}

V4 HalfbandStage::downsample(V4 in0, V4 in1) {
    V4 x = packLoLo(in1, in0);  // newest sample to path 0, previous one to path 1
    for (int s = 0; s < numSections_; ++s) {
        V4 y = coef_[s] * (x - dnY_[s]) + dnX_[s];
        dnX_[s] = x;
        dnY_[s] = y;
        x = y;
    }
    return V4(0.5f) * (x + hiToLo(x));
}

Oversampler::Oversampler() {
    // First stage carries the full audio band: steep and long. Later stages only see
    // content below a quarter of their rate, so short filters with wide transitions do.
    const int coefs[kMaxStages] = {12, 8, 6, 4};
    const double tbw[kMaxStages] = {0.025, 0.12, 0.2, 0.25};
    for (int i = 0; i < kMaxStages; ++i) {
        up_[i].init(coefs[i], tbw[i]);
        down_[i].init(coefs[i], tbw[i]);
    }
}

void Oversampler::reset() {
    for (int i = 0; i < kMaxStages; ++i) { up_[i].reset(); down_[i].reset(); }
}

void Oversampler::setStages(int stages) {
    stages = stages < 0 ? 0 : (stages > kMaxStages ? kMaxStages : stages);
    if (stages != stages_) { stages_ = stages; reset(); }
}

void Oversampler::up(const V4* in, V4* hi, int n) {
    if (stages_ == 0) { for (int i = 0; i < n; ++i) hi[i] = in[i]; return; }
    const V4* src = in;
    int len = n;
    for (int s = 0; s < stages_; ++s) {
        V4* dst = (s == stages_ - 1) ? hi : scratch_[s & 1];
        for (int i = 0; i < len; ++i) up_[s].upsample(src[i], dst[2 * i], dst[2 * i + 1]);
        src = dst;
        len *= 2;
    }
}

void Oversampler::down(const V4* hi, V4* out, int n) {
    if (stages_ == 0) { for (int i = 0; i < n; ++i) out[i] = hi[i]; return; }
    const V4* src = hi;
    int len = n << stages_;
    for (int s = stages_ - 1; s >= 0; --s) {
        len /= 2;
        V4* dst = (s == 0) ? out : scratch_[s & 1];
        for (int i = 0; i < len; ++i) dst[i] = down_[s].downsample(src[2 * i], src[2 * i + 1]);
        src = dst;
    }
}

} // namespace tw
