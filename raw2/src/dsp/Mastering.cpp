#include "dsp/Mastering.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace raw2 {

namespace {
constexpr double kPi = 3.14159265358979323846;

int nextPow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Coefficients du SVF trapézoïdal (Simper) : y = m0 x + m1 v1 + m2 v2
struct SvfCoef {
    double g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
};
SvfCoef svfCoef(int type, double hz, double db, double q, double sr) {
    SvfCoef c;
    const double A = std::pow(10.0, db / 40.0);
    double g = std::tan(kPi * std::min(hz, 0.49 * sr) / sr);
    q = std::max(0.1, q);
    switch (type) {
        case kMixLowShelf: g /= std::sqrt(A); c.k = 1.0 / q; c.m0 = 1.0; c.m1 = c.k * (A - 1.0); c.m2 = A * A - 1.0; break;
        case kMixHighShelf: g *= std::sqrt(A); c.k = 1.0 / q; c.m0 = A * A; c.m1 = c.k * (1.0 - A) * A; c.m2 = 1.0 - A * A; break;
        default: c.k = 1.0 / (q * A); c.m0 = 1.0; c.m1 = c.k * (A * A - 1.0); c.m2 = 0.0; break;
    }
    c.g = g;
    c.a1 = 1.0 / (1.0 + g * (g + c.k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}
} // namespace

// ---- True Peak ----------------------------------------------------------------------------------

TruePeak4x::TruePeak4x() {
    // sinus cardinal fenêtré (Kaiser, beta 7), 48 coefficients ; chaque phase normalisée (gain 1)
    constexpr int N = 4 * kTaps;
    auto bessel0 = [](double x) {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
        return s;
    };
    const double beta = 7.0, c = (N - 1) / 2.0;
    double proto[N];
    for (int n = 0; n < N; ++n) {
        const double x = (n - c) / 4.0, r = (n - c) / c;
        const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        proto[n] = sinc * bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel0(beta);
    }
    for (int p = 0; p < 4; ++p) {
        double s = 0.0;
        for (int k = 0; k < kTaps; ++k) s += proto[4 * k + p];
        for (int k = 0; k < kTaps; ++k) h_[p][k] = (float)(proto[4 * k + p] / s);
    }
}

void TruePeak4x::reset() {
    std::fill(std::begin(buf_), std::end(buf_), 0.f);
    w_ = 0;
}

float TruePeak4x::push(float x) {
    w_ = (w_ + kTaps - 1) % kTaps;  // le plus récent en tête
    buf_[w_] = x;
    buf_[w_ + kTaps] = x;
    const float* b = buf_ + w_;
    float pk = std::fabs(x);
    for (int p = 0; p < 4; ++p) {
        float y = 0.f;
        for (int k = 0; k < kTaps; ++k) y += b[k] * h_[p][k];
        pk = std::max(pk, std::fabs(y));
    }
    return pk;
}

// ---- chaîne de mastering --------------------------------------------------------------------------

void MasterChain::prepare(double sr, int maxBlock) {
    sr_ = sr > 1000.0 ? sr : 48000.0;
    maxBlock_ = std::max(1, maxBlock);
    la_ = std::max(8, (int)std::lround(0.0015 * sr_));
    const int dl = nextPow2(la_ + 2);
    dlyL_.assign((size_t)dl, 0.f);
    dlyR_.assign((size_t)dl, 0.f);
    dmask_ = dl - 1;
    gq_.assign((size_t)dl, 1.f);
    dq_.assign((size_t)dl, 0);
    m1_ = std::max(1, la_ / 2);
    m2_ = std::max(1, la_ - m1_);
    ma1_.assign((size_t)m1_, 1.f);
    ma2_.assign((size_t)m2_, 1.f);
    reset();
}

void MasterChain::reset() {
    for (auto& s : slot_) {
        s.ic1[0] = s.ic1[1] = s.ic2[0] = s.ic2[1] = 0.0;
        s.hz = s.tHz;
        s.q = s.tQ;
        s.db = s.on ? s.tDb : 0.f;
        s.live = s.on;
    }
    scIc1_ = scIc2_ = 0.0;
    grFast_ = grSlow_ = grOut_ = 0.f;
    compMix_ = compOn_ ? 1.f : 0.f;
    limMix_ = limOn_ ? 1.f : 0.f;
    std::fill(dlyL_.begin(), dlyL_.end(), 0.f);
    std::fill(dlyR_.begin(), dlyR_.end(), 0.f);
    dw_ = 0;
    tpL_.reset();
    tpR_.reset();
    std::fill(gq_.begin(), gq_.end(), 1.f);
    dqHead_ = dqTail_ = gIdx_ = 0;
    rel_ = 1.f;
    std::fill(ma1_.begin(), ma1_.end(), 1.f);
    std::fill(ma2_.begin(), ma2_.end(), 1.f);
    s1_ = (double)m1_;
    s2_ = (double)m2_;
    i1_ = i2_ = 0;
}

void MasterChain::setEq(int i, bool on, int type, float hz, float db, float q) {
    if (i < 0 || i >= kMixSlots) return;
    Slot& s = slot_[(size_t)i];
    if (on && !s.live) {  // filtre qui (re)démarre : pas de glissement depuis l'ancien réglage
        s.type = type;
        s.hz = hz;
        s.q = q;
        s.db = 0.f;
    }
    if (on) s.type = type;
    s.on = on;
    s.tHz = std::clamp(hz, 16.f, 22000.f);
    s.tDb = std::clamp(db, -18.f, 18.f);
    s.tQ = std::clamp(q, 0.1f, 18.f);
}

void MasterChain::setComp(bool on, float threshDb, float ratio) {
    compOn_ = on;
    compThr_ = std::clamp(threshDb, -48.f, 0.f);
    compRatio_ = std::clamp(ratio, 1.f, 20.f);
}

void MasterChain::setLimiter(bool on, float ceilingDb) {
    limOn_ = on;
    limCeil_ = std::pow(10.f, std::clamp(ceilingDb, -12.f, 0.f) / 20.f);
}

float MasterChain::responseDb(int type, float f0, float gainDb, float q, float f, float sr) {
    const SvfCoef c = svfCoef(type, f0, gainDb, q, sr);
    // analogique équivalent : s' = j tan(pi f / fs) / g (transformation bilinéaire pré-déformée)
    const std::complex<double> s(0.0, std::tan(kPi * std::min((double)f, 0.499 * sr) / sr) / c.g);
    const std::complex<double> den = s * s + c.k * s + 1.0;
    const std::complex<double> H = c.m0 + c.m1 * (s / den) + c.m2 * (1.0 / den);
    return (float)(20.0 * std::log10(std::max(1e-9, std::abs(H))));
}

void MasterChain::process(float* L, float* R, int n) {
    constexpr int kChunk = 32;
    const double fs = sr_;
    const float aT = 1.f - std::exp(-(float)kChunk / (0.02f * (float)fs));
    const float atkF = std::exp(-1.f / (0.020f * (float)fs)), relF = std::exp(-1.f / (0.080f * (float)fs));
    const float atkS = std::exp(-1.f / (0.300f * (float)fs)), relS = std::exp(-1.f / (1.200f * (float)fs));
    const float relL = std::exp(-1.f / (0.080f * (float)fs));
    const float slope = 1.f - 1.f / compRatio_;
    constexpr float W = 6.f;
    // passe-haut de détection 100 Hz (SVF, Q 0.707)
    const double gH = std::tan(kPi * 100.0 / fs), kH = 1.41421356, a1H = 1.0 / (1.0 + gH * (gH + kH)), a2H = gH * a1H, a3H = gH * a2H;
    float grMax = 0.f;
    for (int off = 0; off < n; off += kChunk) {
        const int m = std::min(kChunk, n - off);
        float* l = L + off;
        float* r = R + off;
        // --- EQ ---
        for (Slot& s : slot_) {
            const float want = s.on ? s.tDb : 0.f;
            s.db += (want - s.db) * aT;
            if (std::fabs(want - s.db) < 0.005f) s.db = want;
            if (!s.on && s.db == 0.f) {
                if (s.live) { s.ic1[0] = s.ic1[1] = s.ic2[0] = s.ic2[1] = 0.0; s.live = false; }
                continue;
            }
            if (s.hz != s.tHz) {
                s.hz = std::exp(std::log(s.hz) + (std::log(s.tHz) - std::log(s.hz)) * aT);
                if (std::fabs(s.hz - s.tHz) < 0.01f) s.hz = s.tHz;
            }
            s.q += (s.tQ - s.q) * aT;
            s.live = true;
            const SvfCoef c = svfCoef(s.type, s.hz, s.db, s.q, fs);
            for (int ch = 0; ch < 2; ++ch) {
                float* x = ch ? r : l;
                double ic1 = s.ic1[ch], ic2 = s.ic2[ch];
                for (int i = 0; i < m; ++i) {
                    const double v0 = x[i], v3 = v0 - ic2, v1 = c.a1 * ic1 + c.a2 * v3, v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
                    ic1 = 2.0 * v1 - ic1;
                    ic2 = 2.0 * v2 - ic2;
                    x[i] = (float)(c.m0 * v0 + c.m1 * v1 + c.m2 * v2);
                }
                s.ic1[ch] = std::isfinite(ic1) ? ic1 : 0.0;
                s.ic2[ch] = std::isfinite(ic2) ? ic2 : 0.0;
            }
        }
        // --- compresseur (détection sur le signal passé au passe-haut, en dB) ---
        const float cm0 = compMix_;
        compMix_ += ((compOn_ ? 1.f : 0.f) - compMix_) * aT;
        if (std::fabs(compMix_ - (compOn_ ? 1.f : 0.f)) < 1e-4f) compMix_ = compOn_ ? 1.f : 0.f;
        if (cm0 > 0.f || compMix_ > 0.f) {
            const float dmix = (compMix_ - cm0) / (float)m;
            float mix = cm0;
            for (int i = 0; i < m; ++i) {
                mix += dmix;
                const double sc = 0.5 * ((double)l[i] + (double)r[i]);
                const double v3 = sc - scIc2_, v1 = a1H * scIc1_ + a2H * v3, v2 = scIc2_ + a2H * scIc1_ + a3H * v3;
                scIc1_ = 2.0 * v1 - scIc1_;
                scIc2_ = 2.0 * v2 - scIc2_;
                const double hp = sc - kH * v1 - v2;
                const float side = std::max((float)std::fabs(hp), 0.5f * std::fabs(l[i] - r[i]));
                const float over = 20.f * std::log10(side + 1e-9f) - compThr_;
                float want = 0.f;
                if (2.f * over > W) want = over * slope;
                else if (2.f * over > -W) { const float t = over + 0.5f * W; want = slope * t * t / (2.f * W); }
                // relâche selon le programme : la voie lente garde la réduction d'une compression longue
                grFast_ = want > grFast_ ? want + (grFast_ - want) * atkF : want + (grFast_ - want) * relF;
                grSlow_ = want > grSlow_ ? want + (grSlow_ - want) * atkS : want + (grSlow_ - want) * relS;
                const float gr = std::max(grFast_, grSlow_ * 0.8f) * mix;
                grMax = std::max(grMax, gr);
                const float g = std::pow(10.f, -gr / 20.f);
                l[i] *= g;
                r[i] *= g;
            }
            if (!std::isfinite(scIc1_) || !std::isfinite(scIc2_)) scIc1_ = scIc2_ = 0.0;
        }
        // --- limiteur True Peak à anticipation (l'audio est toujours retardé de la_ échantillons) ---
        const float lm0 = limMix_;
        limMix_ += ((limOn_ ? 1.f : 0.f) - limMix_) * aT;
        if (std::fabs(limMix_ - (limOn_ ? 1.f : 0.f)) < 1e-4f) limMix_ = limOn_ ? 1.f : 0.f;
        const float dlm = (limMix_ - lm0) / (float)m;
        float lmix = lm0;
        const int qmask = dmask_;
        if (!limOn_ && lm0 <= 0.f && limMix_ <= 0.f) {
            // limiteur coupé : seul le retard reste (latence constante), sans détection True Peak
            for (int i = 0; i < m; ++i) {
                dlyL_[(size_t)dw_] = l[i];
                dlyR_[(size_t)dw_] = r[i];
                l[i] = dlyL_[(size_t)((dw_ - la_) & dmask_)];
                r[i] = dlyR_[(size_t)((dw_ - la_) & dmask_)];
                dw_ = (dw_ + 1) & dmask_;
            }
            limIdle_ = true;
            continue;
        }
        if (limIdle_) {  // reprise : détection repartie d'un état neutre (gain 1)
            tpL_.reset();
            tpR_.reset();
            std::fill(gq_.begin(), gq_.end(), 1.f);
            dqHead_ = dqTail_ = gIdx_ = 0;
            rel_ = 1.f;
            std::fill(ma1_.begin(), ma1_.end(), 1.f);
            std::fill(ma2_.begin(), ma2_.end(), 1.f);
            s1_ = (double)m1_;
            s2_ = (double)m2_;
            i1_ = i2_ = 0;
            limIdle_ = false;
        }
        float gaMin = 1.f;
        for (int i = 0; i < m; ++i) {
            lmix += dlm;
            const float inL = l[i], inR = r[i];
            dlyL_[(size_t)dw_] = inL;
            dlyR_[(size_t)dw_] = inR;
            const float outL = dlyL_[(size_t)((dw_ - la_) & dmask_)], outR = dlyR_[(size_t)((dw_ - la_) & dmask_)];
            dw_ = (dw_ + 1) & dmask_;
            const float pk = std::max(tpL_.push(inL), tpR_.push(inR));
            const float tg = pk > limCeil_ ? limCeil_ / pk : 1.f;
            // minimum glissant sur les la_ + 1 derniers gains voulus
            const int64_t idx = gIdx_++;
            gq_[(size_t)(idx & qmask)] = tg;
            while (dqTail_ > dqHead_ && gq_[(size_t)(dq_[(size_t)((dqTail_ - 1) & qmask)] & qmask)] >= tg) --dqTail_;
            dq_[(size_t)(dqTail_++ & qmask)] = idx;
            while (dq_[(size_t)(dqHead_ & qmask)] <= idx - la_ - 1) ++dqHead_;
            const float mn = gq_[(size_t)(dq_[(size_t)(dqHead_ & qmask)] & qmask)];
            rel_ = mn < rel_ ? mn : mn + (rel_ - mn) * relL;
            // deux moyennes glissantes : le gain atteint son minimum quand la crête sort du retard
            s1_ += (double)rel_ - (double)ma1_[(size_t)i1_];
            ma1_[(size_t)i1_] = rel_;
            i1_ = (i1_ + 1) % m1_;
            const float a = (float)(s1_ / m1_);
            s2_ += (double)a - (double)ma2_[(size_t)i2_];
            ma2_[(size_t)i2_] = a;
            i2_ = (i2_ + 1) % m2_;
            const float g = std::min(1.f, (float)(s2_ / m2_));
            const float ga = 1.f + (g - 1.f) * lmix;
            gaMin = std::min(gaMin, ga);
            l[i] = outL * ga;
            r[i] = outR * ga;
        }
        grMax = std::max(grMax, -20.f * std::log10(std::max(gaMin, 1e-6f)));
    }
    grOut_ = grMax;
}

// ---- mesures --------------------------------------------------------------------------------------

double Meters::Bq::run(int c, double x) {
    const double y = b0 * x + z1[c];
    z1[c] = b1 * x - a1 * y + z2[c];
    z2[c] = b2 * x - a2 * y;
    return y;
}

void Meters::prepare(double sr) {
    sr_ = sr > 1000.0 ? sr : 48000.0;
    // pondération K (BS.1770-4) recalculée pour la fréquence d'échantillonnage
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / sr_), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        kShelf_.b0 = (Vh + Vb * K / Q + K * K) / a0;
        kShelf_.b1 = 2.0 * (K * K - Vh) / a0;
        kShelf_.b2 = (Vh - Vb * K / Q + K * K) / a0;
        kShelf_.a1 = 2.0 * (K * K - 1.0) / a0;
        kShelf_.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / sr_), a0 = 1.0 + K / Q + K * K;
        kHp_.b0 = 1.0; kHp_.b1 = -2.0; kHp_.b2 = 1.0;
        kHp_.a1 = 2.0 * (K * K - 1.0) / a0;
        kHp_.a2 = (1.0 - K / Q + K * K) / a0;
    }
    blockLen_ = std::max(1, (int)std::lround(0.1 * sr_));
    gLow_ = std::tan(kPi * 250.0 / sr_);
    gHigh_ = std::tan(kPi * 4000.0 / sr_);
    for (auto& v : pub_.v) v.store(0.f);
    reset();
}

void Meters::reset() {
    kShelf_.z1[0] = kShelf_.z1[1] = kShelf_.z2[0] = kShelf_.z2[1] = 0.0;
    kHp_.z1[0] = kHp_.z1[1] = kHp_.z2[0] = kHp_.z2[1] = 0.0;
    blockSum_ = 0.0;
    blockN_ = 0;
    blocks_.fill(0.0);
    bIdx_ = bCount_ = 0;
    iE_.fill(0.0);
    iN_.fill(0);
    sN_.fill(0);
    tpL_.reset();
    tpR_.reset();
    tpBlockL_ = tpBlockR_ = tpHoldL_ = tpHoldR_ = spBlock_ = tpMax_ = 0.f;
    rmsAcc_ = 0.0;
    rmsBlocks_.fill(0.0);
    pkBlocks_.fill(0.f);
    for (auto& a : lowS_) a[0] = a[1] = 0.0;
    for (auto& a : highS_) a[0] = a[1] = 0.0;
    for (auto& b : cs_) b[0] = b[1] = b[2] = 0.0;
    for (auto& b : accB_) b[0] = b[1] = b[2] = 0.0;
    accMs_[0] = accMs_[1] = accBal_[0] = accBal_[1] = 0.0;
    ms_[0] = ms_[1] = bal_[0] = bal_[1] = 0.0;
}

namespace {
// SVF (k = 1.414) : renvoie passe-bas ou passe-haut, états s[0], s[1]
inline double svfLp(double* s, double x, double g) {
    const double k = 1.41421356, a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = x - s[1], v1 = a1 * s[0] + a2 * v3, v2 = s[1] + a2 * s[0] + a3 * v3;
    s[0] = 2.0 * v1 - s[0];
    s[1] = 2.0 * v2 - s[1];
    return v2;
}
inline double svfHp(double* s, double x, double g) {
    const double k = 1.41421356, a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = x - s[1], v1 = a1 * s[0] + a2 * v3, v2 = s[1] + a2 * s[0] + a3 * v3;
    s[0] = 2.0 * v1 - s[0];
    s[1] = 2.0 * v2 - s[1];
    return x - k * v1 - v2;
}
double toLufs(double ms) { return ms > 1e-12 ? -0.691 + 10.0 * std::log10(ms) : -70.0; }
} // namespace

void Meters::process(const float* L, const float* R, int n) {
    if (resetPeaks.exchange(false)) { tpHoldL_ = tpHoldR_ = 0.f; tpMax_ = 0.f; }
    if (resetIntegrated.exchange(false)) { iE_.fill(0.0); iN_.fill(0); sN_.fill(0); tpMax_ = 0.f; }
    auto& bs = accB_;
    auto& msb = accMs_;
    auto& bb = accBal_;
    for (int i = 0; i < n; ++i) {
        const double l = L[i], r = R[i];
        const double kl = kHp_.run(0, kShelf_.run(0, l)), kr = kHp_.run(1, kShelf_.run(1, r));
        blockSum_ += kl * kl + kr * kr;
        rmsAcc_ += 0.5 * (l * l + r * r);
        const float pl = tpL_.push(L[i]), pr = tpR_.push(R[i]);
        tpBlockL_ = std::max(tpBlockL_, pl);
        tpBlockR_ = std::max(tpBlockR_, pr);
        spBlock_ = std::max(spBlock_, std::max(std::fabs(L[i]), std::fabs(R[i])));
        // stéréo par bande
        const double lo0 = svfLp(lowS_[0], l, gLow_), lo1 = svfLp(lowS_[1], r, gLow_);
        const double hi0 = svfHp(highS_[0], l, gHigh_), hi1 = svfHp(highS_[1], r, gHigh_);
        const double mi0 = l - lo0 - hi0, mi1 = r - lo1 - hi1;
        const double xs[4][2] = {{l, r}, {lo0, lo1}, {mi0, mi1}, {hi0, hi1}};
        for (int b = 0; b < 4; ++b) {
            bs[b][0] += xs[b][0] * xs[b][1];
            bs[b][1] += xs[b][0] * xs[b][0];
            bs[b][2] += xs[b][1] * xs[b][1];
        }
        const double mid = 0.5 * (l + r), sd = 0.5 * (l - r);
        msb[0] += mid * mid;
        msb[1] += sd * sd;
        bb[0] += l * l;
        bb[1] += r * r;
        if (++blockN_ >= blockLen_) {
            for (int b = 0; b < 4; ++b)
                for (int k = 0; k < 3; ++k) { cs_[b][k] = cs_[b][k] * 0.65 + bs[b][k]; bs[b][k] = 0.0; }
            for (int k = 0; k < 2; ++k) { ms_[k] = ms_[k] * 0.65 + msb[k]; bal_[k] = bal_[k] * 0.65 + bb[k]; msb[k] = bb[k] = 0.0; }
            endBlock();
        }
    }
    for (auto* s : {&lowS_[0][0], &lowS_[0][1], &lowS_[1][0], &lowS_[1][1], &highS_[0][0], &highS_[0][1], &highS_[1][0], &highS_[1][1]})
        if (!std::isfinite(*s)) *s = 0.0;
}

void Meters::endBlock() {
    const double ms = blockSum_ / (double)blockN_;
    blockSum_ = 0.0;
    blocks_[(size_t)bIdx_] = ms;
    rmsBlocks_[(size_t)bIdx_] = rmsAcc_ / (double)blockN_;
    pkBlocks_[(size_t)bIdx_] = spBlock_;
    rmsAcc_ = 0.0;
    blockN_ = 0;
    bIdx_ = (bIdx_ + 1) % 30;
    bCount_ = std::min(bCount_ + 1, 1 << 30);
    auto meanLast = [&](int k) {
        const int c = std::min(k, std::min(bCount_, 30));
        double s = 0.0;
        for (int j = 1; j <= c; ++j) s += blocks_[(size_t)((bIdx_ - j + 30) % 30)];
        return c > 0 ? s / c : 0.0;
    };
    const double mMs = meanLast(4), sMs = meanLast(30);
    const double M = toLufs(mMs), S = toLufs(sMs);
    // intégrée : blocs de 400 ms (recouvrement 75 %) au-dessus de la porte absolue
    if (bCount_ >= 4 && M > -70.0) {
        const int bin = std::clamp((int)((M + 70.0) * 10.0), 0, kHist - 1);
        iE_[(size_t)bin] += mMs;
        iN_[(size_t)bin] += 1;
    }
    if (bCount_ >= 30 && S > -70.0) sN_[(size_t)std::clamp((int)((S + 70.0) * 10.0), 0, kHist - 1)] += 1;
    double E = 0.0, N = 0.0;
    for (int b = 0; b < kHist; ++b) { E += iE_[(size_t)b]; N += iN_[(size_t)b]; }
    double I = -70.0;
    if (N > 0) {
        const double gate = toLufs(E / N) - 10.0;
        double E2 = 0.0, N2 = 0.0;
        for (int b = std::max(0, (int)((gate + 70.0) * 10.0)); b < kHist; ++b) { E2 += iE_[(size_t)b]; N2 += iN_[(size_t)b]; }
        if (N2 > 0) I = toLufs(E2 / N2);
    }
    // LRA : écart entre les 10e et 95e centiles des sonies à court terme (porte relative -20 LU)
    double lra = 0.0;
    {
        double se = 0.0, sn = 0.0;
        for (int b = 0; b < kHist; ++b) { const double lu = -70.0 + (b + 0.5) * 0.1; se += sN_[(size_t)b] * std::pow(10.0, (lu + 0.691) / 10.0); sn += sN_[(size_t)b]; }
        if (sn > 0) {
            const int g0 = std::max(0, (int)((toLufs(se / sn) - 20.0 + 70.0) * 10.0));
            double tot = 0.0;
            for (int b = g0; b < kHist; ++b) tot += sN_[(size_t)b];
            double acc = 0.0, p10 = -1.0, p95 = -1.0;
            for (int b = g0; b < kHist && tot > 0; ++b) {
                acc += sN_[(size_t)b];
                const double lu = -70.0 + (b + 0.5) * 0.1;
                if (p10 < -0.5 && acc >= 0.10 * tot) p10 = lu + 70.0;
                if (p95 < -0.5 && acc >= 0.95 * tot) p95 = lu + 70.0;
            }
            if (p10 >= 0 && p95 >= 0) lra = p95 - p10;
        }
    }
    tpHoldL_ = std::max(tpHoldL_, tpBlockL_);
    tpHoldR_ = std::max(tpHoldR_, tpBlockR_);
    tpMax_ = std::max(tpMax_, std::max(tpBlockL_, tpBlockR_));
    auto db = [](double x) { return (float)(x > 1e-9 ? 20.0 * std::log10(x) : -90.0); };
    double rmsE = 0.0;
    float pk = 0.f;
    for (int j = 0; j < 30; ++j) { rmsE += rmsBlocks_[(size_t)j]; pk = std::max(pk, pkBlocks_[(size_t)j]); }
    rmsE /= 30.0;
    auto corr = [&](int b) {
        const double d = std::sqrt(cs_[b][1] * cs_[b][2]);
        return d > 1e-12 ? (float)std::clamp(cs_[b][0] / d, -1.0, 1.0) : 1.f;
    };
    const float width = ms_[0] > 1e-12 ? (float)std::min(200.0, 100.0 * std::sqrt(ms_[1] / ms_[0])) : 0.f;
    const float bal = (bal_[0] + bal_[1]) > 1e-12 ? (float)(100.0 * (bal_[1] - bal_[0]) / (bal_[0] + bal_[1])) : 0.f;
    const float vals[18] = {(float)M, (float)S, (float)I, (float)lra, db(tpBlockL_), db(tpBlockR_), db(tpHoldL_), db(tpHoldR_), db(pk),
                            db(pk) - (rmsE > 1e-12 ? (float)(10.0 * std::log10(rmsE)) : -90.f), db(tpMax_) - (float)I, corr(0), corr(1), corr(2), corr(3),
                            width, bal, rmsE > 1e-12 ? (float)(10.0 * std::log10(rmsE)) : -90.f};
    for (int k = 0; k < 18; ++k) pub_.v[k].store(vals[k], std::memory_order_relaxed);
    tpBlockL_ = tpBlockR_ = spBlock_ = 0.f;
}

MeterReadings Meters::read() const {
    MeterReadings r;
    float v[18];
    for (int k = 0; k < 18; ++k) v[k] = pub_.v[k].load(std::memory_order_relaxed);
    r.momentary = v[0]; r.shortTerm = v[1]; r.integrated = v[2]; r.lra = v[3];
    r.truePeakL = v[4]; r.truePeakR = v[5]; r.truePeakHoldL = v[6]; r.truePeakHoldR = v[7];
    r.samplePeak = v[8]; r.crest = v[9]; r.plr = v[10];
    r.corr = v[11]; r.corrLow = v[12]; r.corrMid = v[13]; r.corrHigh = v[14];
    r.width = v[15]; r.balance = v[16]; r.rms = v[17];
    return r;
}

} // namespace raw2
