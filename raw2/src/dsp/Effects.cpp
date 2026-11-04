#include "dsp/Effects.h"

#include <algorithm>
#include <cmath>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <xmmintrin.h>
#define RAW2_HAS_SSE 1
#endif

namespace raw2 {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr double kPiD = 3.14159265358979323846;
constexpr float kTau = 0.02f;  // lissage des boutons et fondus : 20 ms

// Padé (3,2) de tanh : vaut exactement ±1 avec une pente nulle en ±3, donc borner à ±3 donne une
// courbe continue et dérivable (à ±4, elle dépassait 1 puis retombait d'un coup : petit crénelage)
inline float fastTanh(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

// linéaire jusqu'à ±2, plafonne en douceur à ±4 (sécurité des boucles)
inline float softClip(float x) {
    const float a = std::fabs(x);
    if (a < 2.f) return x;
    const float y = 2.f + 2.f * fastTanh((a - 2.f) * 0.5f);
    return x < 0.f ? -y : y;
}

// pôle simple vers la cible, accroché à la cible quand l'écart devient négligeable
inline float smooth(float v, float t, float a) {
    v += (t - v) * a;
    return std::fabs(t - v) < 1e-4f ? t : v;
}

inline float clampP(float x) { return std::isfinite(x) ? std::clamp(x, 0.f, 1.f) : 0.f; }

// passe-bas 1 pôle (trapèzes) : G = g / (1 + g)
inline float onePoleG(float fc, float fs) {
    const float g = std::tan(kPi * std::clamp(fc, 5.f, 0.45f * fs) / fs);
    return g / (1.f + g);
}
inline float lp1(float& s, float x, float G) {
    const float v = (x - s) * G, y = v + s;
    s = y + v;
    return y;
}

inline int nextPow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

struct DenormalGuard {
#ifdef RAW2_HAS_SSE
    unsigned int csr;
    DenormalGuard() : csr(_mm_getcsr()) { _mm_setcsr(csr | 0x8040); }
    ~DenormalGuard() { _mm_setcsr(csr); }
#endif
};


void buildOrder(const FxSettings& s, int* out) {
    bool used[kNumFx] = {};
    int n = 0;
    for (int i = 0; i < kNumFx; ++i) {
        const int k = s.order[i];
        if (k >= 0 && k < kNumFx && !used[k]) { used[k] = true; out[n++] = k; }
    }
    for (int k = 0; k < kNumFx; ++k)
        if (!used[k]) out[n++] = k;
}

// HYPER : réglages propres à chaque voix (vitesse, profondeur, décalage en ms)
const float kHyRate[7] = {1.f, 1.13f, 0.87f, 1.29f, 0.79f, 1.41f, 0.93f};
const float kHyDepth[7] = {1.f, 0.55f, 0.85f, 0.4f, 0.95f, 0.7f, 0.3f};
const float kHyOff[7] = {0.f, 1.3f, 0.6f, 2.1f, 1.f, 2.7f, 1.7f};

inline float hyRate(const float* p, int v) { return (0.3f + 6.f * p[0]) * kHyRate[v]; }
// retard d'une voix : modulation sinus dont l'amplitude donne au plus ±25 cents
inline float hyDelay(const float* p, double ph, int v, float fs) {
    const float A = p[1] * (0.3f + 0.7f * kHyDepth[v]) * 0.01454f / (2.f * kPi * hyRate(p, v));
    return (0.001f + kHyOff[v] * 0.001f * p[1] + A * (1.f + (float)std::sin(2.0 * kPiD * ph))) * fs;
}

// FLANGER : 0,6 ms .. 8 ms
inline float flDelay(const float* p, double ph, float fs) {
    return (0.6f + (0.4f + 7.f * p[1]) * (0.5f + 0.5f * (float)std::sin(2.0 * kPiD * ph))) * 0.001f * fs;
}

// PHASER : coefficient des passe-tout, centre 200 Hz .. 4 kHz, ±2 octaves
inline float phG(const float* p, double ph, float fs) {
    const float fc = 200.f * std::pow(20.f, p[2]) * std::exp2(p[1] * 2.f * (float)std::sin(2.0 * kPiD * ph));
    return onePoleG(fc, fs);
}

// CHORUS : retard de base 5..30 ms, trois voix décalées
const float kChMul[3] = {0.85f, 1.f, 1.17f};
inline float chDelay(const float* p, double ph, int v, int ch, float fs) {
    const float base = 0.005f + 0.025f * p[1];
    const float A = p[2] * std::min(0.3f * base, 0.003f);
    return (base * kChMul[v] + A * (float)std::sin(2.0 * kPiD * (ph + v / 3.0 + ch / 6.0))) * fs;
}

// RÉVERBÉRATION : longueurs des lignes (ms), diffuseurs, vitesses de modulation
const float kRevMs[8] = {31.71f, 37.11f, 41.13f, 43.73f, 53.03f, 59.29f, 67.07f, 73.39f};
const float kRevApMs[2][4] = {{4.77f, 3.59f, 12.73f, 9.29f}, {4.93f, 3.71f, 12.29f, 9.67f}};
const float kRevApG[4] = {0.7f, 0.7f, 0.6f, 0.6f};
const float kRevRate[8] = {0.31f, 0.47f, 0.23f, 0.61f, 0.37f, 0.53f, 0.29f, 0.71f};
const float kRevSign[8] = {1.f, -1.f, 1.f, 1.f, -1.f, 1.f, -1.f, -1.f};
constexpr float kRevModMs = 0.25f;

// coefficients d'un demi-bande polyphase (méthode elliptique de Valenzuela et Constantinides)
double accNum(double q, int order, int c) {
    double acc = 0.0, term = 0.0;
    int i = 0, j = 1;
    do {
        term = std::pow(q, (double)(i * (i + 1))) * std::sin((double)((i * 2 + 1) * c) * kPiD / (double)order) * j;
        acc += term;
        j = -j;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 100);
    return acc;
}
double accDen(double q, int order, int c) {
    double acc = 0.0, term = 0.0;
    int i = 1, j = -1;
    do {
        term = std::pow(q, (double)(i * i)) * std::cos((double)(i * 2 * c) * kPiD / (double)order) * j;
        acc += term;
        j = -j;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 100);
    return acc;
}
} // namespace

// ---- briques ------------------------------------------------------------------------------

void FxRack::Delay::init(int minLen) {
    const int n = nextPow2(std::max(minLen, 8));
    buf.assign((size_t)n, 0.f);
    mask = n - 1;
    w = 0;
}

void FxRack::Delay::clear() {
    std::fill(buf.begin(), buf.end(), 0.f);
    w = 0;
}

float FxRack::Delay::readLinear(float d) const {
    const int i = (int)d;
    const float f = d - (float)i, a = at(i), b = at(i + 1);
    return a + (b - a) * f;
}

float FxRack::Delay::readHermite(float d) const {
    const int i = (int)d;
    const float f = d - (float)i;
    const float ym1 = at(i - 1), y0 = at(i), y1 = at(i + 1), y2 = at(i + 2);
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

template <int N>
void FxRack::HalfBand<N>::design(double transition) {
    double k = std::tan((1.0 - transition * 2.0) * kPiD / 4.0);
    k *= k;
    const double kk = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kk) / (1.0 + kk), e4 = e * e * e * e;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    const int order = N * 2 + 1;
    for (int i = 0; i < N; ++i) {
        const int cc = i + 1;
        const double num = accNum(q, order, cc) * std::pow(q, 0.25);
        const double den = accDen(q, order, cc) + 0.5;
        const double ww = num / den, wwsq = ww * ww;
        const double x = std::sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        c[i] = (float)((1.0 - x) / (1.0 + x));
    }
    clear();
}

template <int N>
void FxRack::HalfBand<N>::clear() {
    for (int i = 0; i < N; ++i) ux[i] = uy[i] = dx[i] = dy[i] = 0.f;
}

// une entrée -> deux sorties (chemins pair et impair)
template <int N>
void FxRack::HalfBand<N>::up(float in, float& o0, float& o1) {
    float s0 = in, s1 = in;
    for (int i = 0; i < N; i += 2) {
        const float t0 = (s0 - uy[i]) * c[i] + ux[i];
        const float t1 = (s1 - uy[i + 1]) * c[i + 1] + ux[i + 1];
        ux[i] = s0; ux[i + 1] = s1;
        uy[i] = t0; uy[i + 1] = t1;
        s0 = t0; s1 = t1;
    }
    o0 = s0;
    o1 = s1;
}

// deux entrées -> une sortie
template <int N>
float FxRack::HalfBand<N>::down(float i0, float i1) {
    float s0 = i1, s1 = i0;
    for (int i = 0; i < N; i += 2) {
        const float t0 = (s0 - dy[i]) * c[i] + dx[i];
        const float t1 = (s1 - dy[i + 1]) * c[i + 1] + dx[i + 1];
        dx[i] = s0; dx[i + 1] = s1;
        dy[i] = t0; dy[i + 1] = t1;
        s0 = t0; s1 = t1;
    }
    return 0.5f * (s0 + s1);
}

// ---- préparation --------------------------------------------------------------------------

void FxRack::prepare(double sampleRate, int maxBlock) {
    sr_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max(maxBlock, 1);
    const auto len = [&](double sec, int extra) { return (int)std::ceil(sec * sr_) + extra; };
    for (auto& d : hyDl_) d.init(len(0.030, kChunk + 8));
    for (auto& d : flDl_) d.init(len(0.0095, 8));
    for (auto& d : chDl_) d.init(len(0.045, kChunk + 8));
    for (auto& d : dlyDl_) d.init(len(1.01, 8));
    for (auto& d : revPre_) d.init(len(0.101, 8));
    for (int c = 0; c < 2; ++c)
        for (int a = 0; a < kRevAp; ++a) {
            revApLen_[c][a] = std::max(2, (int)std::lround(kRevApMs[c][a] * 0.001 * sr_));
            revAp_[c][a].init(revApLen_[c][a] + 4);
        }
    for (int j = 0; j < kRevLines; ++j) {
        revLen_[j] = (float)(kRevMs[j] * 0.001 * sr_);
        revLine_[j].init(len((kRevMs[j] + 2.f * kRevModMs) * 0.001, 8));
    }
    for (auto& h : hbA_) h.design(0.035);
    for (auto& h : hbB_) h.design(0.2);
    for (auto& d : vtDl_) d.init(len(0.012, kChunk + 8));
    prepared_ = true;
    reset();
}

void FxRack::reset() {
    for (int k = 0; k < kNumFx; ++k) clearFx(k);
    for (auto& g : hyGL_) g = 0.f;
    for (auto& g : hyGR_) g = 0.f;
    compGrMax_ = compGrOut_ = 0.f;
    orderPending_ = false;
    orderGate_ = 1.f;
    distGate_ = distGate0_ = 1.f;
    snap_ = true;  // le prochain bloc prend directement les réglages
}

void FxRack::clearFx(int k) {
    if (!prepared_) return;
    switch (k) {
        case kFxHyper:
            for (auto& d : hyDl_) d.clear();
            break;
        case kFxDist:
            for (auto& h : hbA_) h.clear();
            for (auto& h : hbB_) h.clear();
            for (int c = 0; c < 2; ++c) {
                distDcX_[c] = distDcY_[c] = distTone_[c] = 0.f;
                adX_[c] = adF_[c] = 0.0;
                adDr_[c] = -1.0;  // primitive recalculée au prochain échantillon
            }
            break;
        case kFxFlang:
            for (auto& d : flDl_) d.clear();
            break;
        case kFxPhase:
            for (int c = 0; c < 2; ++c) {
                for (auto& v : phS_[c]) v = 0.f;
                phLast_[c] = 0.f;
            }
            break;
        case kFxChor:
            for (auto& d : chDl_) d.clear();
            break;
        case kFxDly:
            for (auto& d : dlyDl_) d.clear();
            dlyLp_[0] = dlyLp_[1] = dlyHp_[0] = dlyHp_[1] = 0.f;
            break;
        case kFxRev:
            for (auto& d : revPre_) d.clear();
            for (auto& c : revAp_)
                for (auto& d : c) d.clear();
            for (auto& d : revLine_) d.clear();
            for (auto& v : revLp_) v = 0.f;
            for (int j = 0; j < kRevLines; ++j) {
                revRw_[j] = 0.f;
                revM_[j] = revLen_[j] + kRevModMs * 0.001f * (float)sr_;
            }
            break;
        case kFxComp:
            compGr_ = compSlow_ = 0.f;
            compSc_[0] = compSc_[1] = 0.f;
            break;
        case kFxEq:
            for (auto& b : eqIc_)
                for (auto& c : b) c[0] = c[1] = 0.f;
            break;
        case kFxVintage:
            for (auto& d : vtDl_) d.clear();
            for (int c = 0; c < 2; ++c) vtLp_[c] = vtHold_[c] = vtNoiseLp_[c] = 0.f;
            vtHoldPh_ = vtEnv_ = vtCrk_ = 0.f;
            break;
        case kFxPump:
            pumpG_ = 1.f;
            break;
        default:
            svf1_[0] = svf1_[1] = svf2_[0] = svf2_[1] = 0.f;
            break;
    }
}

float FxRack::compGainReductionDb() const { return compGrOut_; }

// ---- boucle principale --------------------------------------------------------------------

void FxRack::process(float* L, float* R, int n, const FxSettings& s) {
    if (!prepared_ || !L || !R || n <= 0) return;
    DenormalGuard dg;
    const float fs = (float)sr_;
    for (int i = 0; i < n; ++i) {  // entrée non finie : silence
        if (!std::isfinite(L[i])) L[i] = 0.f;
        if (!std::isfinite(R[i])) R[i] = 0.f;
    }

    // ordre voulu ; s'il change pour des effets actifs, creux de 10 ms puis permutation
    int want[kNumFx];
    buildOrder(s, want);
    {
        int a[kNumFx], b[kNumFx], na = 0, nb = 0;
        for (int i = 0; i < kNumFx; ++i) {
            if (slot_[order_[i]].live() || s.on[order_[i]]) a[na++] = order_[i];
            if (slot_[want[i]].live() || s.on[want[i]]) b[nb++] = want[i];
        }
        const bool same = na == nb && std::equal(a, a + na, b);
        if (snap_ || same) {
            std::copy(want, want + kNumFx, order_);
            orderPending_ = false;
        } else {
            std::copy(want, want + kNumFx, nextOrder_);
            orderPending_ = true;
        }
    }
    // distorsion : changement de forme ou de suréchantillonnage, même principe
    distModeWant_ = std::clamp(s.distMode, 0, 3);
    distOsWant_ = s.oversample >= 4 ? 4 : (s.oversample >= 2 ? 2 : 1);
    if (snap_ || (!slot_[kFxDist].live() && !s.on[kFxDist])) {
        if (distOsWant_ != distOs_) { for (auto& h : hbA_) h.clear(); for (auto& h : hbB_) h.clear(); }
        distMode_ = distModeWant_;
        distOs_ = distOsWant_;
    }

    compGrMax_ = 0.f;
    float inL[kChunk], inR[kChunk], dL[kChunk], dR[kChunk];
    beatPerSample_ = s.beatPerSample;
    for (int off = 0; off < n; off += kChunk) {
        const int m = std::min(kChunk, n - off);
        chunkBeat_ = s.beat + (double)off * s.beatPerSample;
        float* l = L + off;
        float* r = R + off;
        const float a = 1.f - std::exp(-(float)m / (kTau * fs));
        const float aT = 1.f - std::exp(-(float)m / (0.08f * fs));
        const float step = (float)m / (kTau * fs), gstep = (float)m / (0.01f * fs);

        // creux de permutation et de changement de distorsion
        if (orderPending_ && orderGate_ <= 0.f) {
            std::copy(nextOrder_, nextOrder_ + kNumFx, order_);
            orderPending_ = false;
        }
        const float og0 = orderGate_;
        orderGate_ = orderPending_ ? std::max(0.f, orderGate_ - gstep) : std::min(1.f, orderGate_ + gstep);
        const bool distChange = distModeWant_ != distMode_ || distOsWant_ != distOs_;
        if (distChange && distGate_ <= 0.f) {
            if (distOsWant_ != distOs_) { for (auto& h : hbA_) h.clear(); for (auto& h : hbB_) h.clear(); }
            distMode_ = distModeWant_;
            distOs_ = distOsWant_;
        }
        distGate0_ = distGate_;
        distGate_ = (distModeWant_ != distMode_ || distOsWant_ != distOs_) ? std::max(0.f, distGate_ - gstep)
                                                                           : std::min(1.f, distGate_ + gstep);

        // lissage des réglages et fondus marche / arrêt
        for (int k = 0; k < kNumFx; ++k) {
            Slot& sl = slot_[k];
            const bool on = s.on[k];
            if (on && sl.fade <= 0.f) clearFx(k);  // repart d'une mémoire vide
            if (snap_) sl.fade = on ? 1.f : 0.f;
            sl.fade0 = sl.fade;
            if (!snap_) sl.fade = on ? std::min(1.f, sl.fade + step) : std::max(0.f, sl.fade - step);
            const bool glide = sl.live() && !snap_;
            for (int j = 0; j < 3; ++j) {
                sl.p0[j] = sl.p[j];
                const float t = clampP(s.p[k][j]);
                sl.p[j] = glide ? smooth(sl.p[j], t, a) : t;
                if (!glide) sl.p0[j] = t;
            }
            sl.mix0 = sl.mix;
            sl.mix = glide ? smooth(sl.mix, clampP(s.mix[k]), a) : clampP(s.mix[k]);
            if (!glide) sl.mix0 = sl.mix;
        }
        {
            const bool glide = slot_[kFxDly].live() && !snap_;
            const float t = (0.02f + 0.98f * clampP(s.p[kFxDly][0])) * fs;
            dlyT0_ = glide ? dlyT_ : t;
            dlyT_ = glide ? smooth(dlyT_, t, aT) : t;
            const bool eqGlide = slot_[kFxEq].live() && !snap_;
            for (int b = 0; b < 3; ++b) {
                const float t2 = clampP(s.eqFreq[b]);
                eqF0_[b] = eqGlide ? eqF_[b] : t2;
                eqF_[b] = eqGlide ? smooth(eqF_[b], t2, a) : t2;
            }
            wet0_ = snap_ ? clampP(s.wet) : wet_;
            wet_ = snap_ ? clampP(s.wet) : smooth(wet_, clampP(s.wet), a);
        }
        if (snap_) {
            distGate0_ = distGate_ = 1.f;
            snap_ = false;
        }

        std::copy(l, l + m, inL);
        std::copy(r, r + m, inR);
        for (int idx = 0; idx < kNumFx; ++idx) {
            const int k = order_[idx];
            const Slot& sl = slot_[k];
            if (!sl.live()) continue;
            float g0 = og0, g1 = orderGate_;
            if (k == kFxDist) { g0 *= distGate0_; g1 *= distGate_; }
            // l'entrée de l'effet suit aussi les fondus : ses mémoires n'enregistrent aucune marche
            const float i0 = sl.fade0 * g0, i1 = sl.fade * g1;
            const float a0 = sl.mix0 * i0, a1 = sl.mix * i1;
            std::copy(l, l + m, dL);
            std::copy(r, r + m, dR);
            if (i0 < 1.f || i1 < 1.f) {
                const float di = (i1 - i0) / (float)m;
                float gi = i0;
                for (int i = 0; i < m; ++i) {
                    gi += di;
                    l[i] *= gi;
                    r[i] *= gi;
                }
            }
            runFx(k, l, r, m, a);
            if (a0 >= 1.f && a1 >= 1.f) continue;
            const float da = (a1 - a0) / (float)m;
            float am = a0;
            // effets d'espace et de modulation : loi « d'envoi » (le sec reste plein jusqu'à 50 %,
            // le traité s'ajoute), sinon un simple fondu vide le niveau sur une note tenue ;
            // distorsion, compresseur, EQ et filtre restent en fondu sec / traité
            const bool send = k == kFxHyper || k == kFxFlang || k == kFxPhase || k == kFxChor || k == kFxDly || k == kFxRev;
            if (send) {
                // le fondu marche / arrêt (f) reste un fondu sec / traité, sans bosse de niveau
                const float dm = (sl.mix - sl.mix0) / (float)m, df = (i1 - i0) / (float)m;
                float mx = sl.mix0, f = i0;
                for (int i = 0; i < m; ++i) {
                    mx += dm;
                    f += df;
                    const float gd = 1.f - f + f * std::min(1.f, 2.f * (1.f - mx)), gw = f * std::min(1.f, 2.f * mx);
                    l[i] = dL[i] * gd + l[i] * gw;
                    r[i] = dR[i] * gd + r[i] * gw;
                }
                continue;
            }
            for (int i = 0; i < m; ++i) {
                am += da;
                l[i] = dL[i] + (l[i] - dL[i]) * am;
                r[i] = dR[i] + (r[i] - dR[i]) * am;
            }
        }

        // DRY / WET global
        if (wet0_ <= 0.f && wet_ <= 0.f) {
            std::copy(inL, inL + m, l);
            std::copy(inR, inR + m, r);
        } else if (wet0_ < 1.f || wet_ < 1.f) {
            const float dw = (wet_ - wet0_) / (float)m;
            float w = wet0_;
            for (int i = 0; i < m; ++i) {
                w += dw;
                l[i] = inL[i] + (l[i] - inL[i]) * w;
                r[i] = inR[i] + (r[i] - inR[i]) * w;
            }
        }
    }
    compGrOut_ = slot_[kFxComp].live() ? compGrMax_ : 0.f;
}

void FxRack::runFx(int k, float* l, float* r, int m, float a) {
    switch (k) {
        case kFxHyper: runHyper(l, r, m, a); break;
        case kFxDist: runDist(l, r, m); break;
        case kFxFlang: runFlang(l, r, m); break;
        case kFxPhase: runPhase(l, r, m); break;
        case kFxChor: runChor(l, r, m); break;
        case kFxDly: runDly(l, r, m); break;
        case kFxRev: runRev(l, r, m); break;
        case kFxComp: runComp(l, r, m); break;
        case kFxEq: runEq(l, r, m); break;
        case kFxVintage: runVintage(l, r, m); break;
        case kFxPump: runPump(l, r, m); break;
        default: runFilt(l, r, m); break;
    }
}

// ---- HYPER/DIM : N copies retardées et modulées, réparties dans le champ stéréo ------------

void FxRack::runHyper(float* l, float* r, int m, float a) {
    const Slot& s = slot_[kFxHyper];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const int nv = std::clamp((int)std::lround(2.f + s.p[2] * 5.f), 2, kHyperVoices);
    const float norm = std::pow((float)nv, -0.8f);
    // le sous-bloc est écrit d'abord : la voix lit à d + (m - i) de la tête d'écriture
    for (int i = 0; i < m; ++i) { hyDl_[0].push(l[i]); hyDl_[1].push(r[i]); }
    float oL[kChunk] = {}, oR[kChunk] = {};
    for (int v = 0; v < kHyperVoices; ++v) {
        float tl = 0.f, tr = 0.f;
        if (v < nv) {
            const float pos = -1.f + 2.f * (float)v / (float)(nv - 1);
            const float th = (pos * 0.9f + 1.f) * kPi * 0.25f;
            tl = std::cos(th) * 1.41421f * norm;
            tr = std::sin(th) * 1.41421f * norm;
        }
        const float gl0 = hyGL_[v], gr0 = hyGR_[v];
        const float gl1 = smooth(gl0, tl, a), gr1 = smooth(gr0, tr, a);
        hyGL_[v] = gl1;
        hyGR_[v] = gr1;
        const double ph0 = hyPh_[v];
        double ph1 = ph0 + 0.5 * (double)(hyRate(s.p0, v) + hyRate(s.p, v)) * (double)m / sr_;
        ph1 -= std::floor(ph1);
        hyPh_[v] = ph1;
        if (gl0 == 0.f && gr0 == 0.f && gl1 == 0.f && gr1 == 0.f) continue;
        const float d0 = hyDelay(s.p0, ph0, v, fs), d1 = hyDelay(s.p, ph1, v, fs);
        float d = d0 + (float)m;
        const float dd = (d1 - d0) * invM - 1.f;
        float gl = gl0, gr = gr0;
        const float dgl = (gl1 - gl0) * invM, dgr = (gr1 - gr0) * invM;
        for (int i = 0; i < m; ++i) {
            gl += dgl;
            gr += dgr;
            d += dd;
            oL[i] += gl * hyDl_[0].readHermite(d);
            oR[i] += gr * hyDl_[1].readHermite(d);
        }
    }
    std::copy(oL, oL + m, l);
    std::copy(oR, oR + m, r);
}

// ---- DISTORSION : façonneur suréchantillonné 1x / 2x / 4x ------------------------------------

float FxRack::distShape(float x, float dr, float norm) const {
    switch (distMode_) {
        case 0: return std::atan(x * dr) * norm;
        case 1: return (x >= 0.f ? std::tanh(x * dr) : std::tanh(x * dr * 0.6f) * 0.8f) * norm;
        case 2: return std::sin(x * dr * 1.2f);
        default: return std::clamp(x * dr, -1.f, 1.f);
    }
}

// primitive de chaque forme (nulle en 0 sauf FOLD, la constante s'annule dans les différences)
double FxRack::distF(double x, double dr, double norm) const {
    const auto logCosh = [](double y) { y = std::fabs(y); return y + std::log1p(std::exp(-2.0 * y)) - 0.69314718055994531; };
    switch (distMode_) {
        case 0: {
            const double u = dr * x;
            return norm * (x * std::atan(u) - 0.5 * std::log1p(u * u) / dr);
        }
        case 1:
            return x >= 0.0 ? norm * logCosh(dr * x) / dr : norm * 0.8 * logCosh(0.6 * dr * x) / (0.6 * dr);
        case 2:
            return -std::cos(1.2 * dr * x) / (1.2 * dr);
        default: {
            const double u = dr * x;
            return std::fabs(u) <= 1.0 ? 0.5 * dr * x * x : std::fabs(x) - 0.5 / dr;
        }
    }
}

// ADAA d'ordre 1 : moyenne exacte de la forme entre deux échantillons, (F(x) - F(x1)) / (x - x1).
// Le repliement des harmoniques au-delà de Nyquist chute d'environ 6 dB par octave de plus,
// en plus du suréchantillonnage. Quand la commande bouge, F(x1) est recalculée avec la nouvelle.
float FxRack::distAdaa(int ch, float xf, float dr, float norm) {
    const double x = xf;
    double& x1 = adX_[ch];
    double& F1 = adF_[ch];
    if ((double)dr != adDr_[ch] || (double)norm != adNr_[ch]) {
        F1 = distF(x1, dr, norm);
        adDr_[ch] = dr;
        adNr_[ch] = norm;
    }
    const double F = distF(x, dr, norm), dx = x - x1;
    const float y = std::fabs(dx) > 1e-5 ? (float)((F - F1) / dx) : distShape((float)(0.5 * (x + x1)), dr, norm);
    x1 = x;
    F1 = F;
    return y;
}

void FxRack::runDist(float* l, float* r, int m) {
    const Slot& s = slot_[kFxDist];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const auto normOf = [&](float dr) {
        if (distMode_ == 0) return 1.f / std::atan(dr);
        if (distMode_ == 1) return 1.f / std::tanh(dr);
        return 1.f;
    };
    const float dr0 = 1.f + 7.f * s.p0[0], dr1 = 1.f + 7.f * s.p[0];
    const float n0 = normOf(dr0), n1 = normOf(dr1);
    const float c0 = std::pow(dr0, -0.4f), c1 = std::pow(dr1, -0.4f);  // compensation de niveau
    const float b0 = (s.p0[2] - 0.5f) * 0.5f, b1 = (s.p[2] - 0.5f) * 0.5f;
    const float G = onePoleG(1500.f * std::pow(20000.f / 1500.f, s.p[1]), fs);
    const float dcR = 1.f - 2.f * kPi * 10.f / fs;
    for (int ch = 0; ch < 2; ++ch) {
        float* x = ch ? r : l;
        HalfBand<kHbA>& ha = hbA_[ch];
        HalfBand<kHbB>& hb = hbB_[ch];
        float dcX = distDcX_[ch], dcY = distDcY_[ch], tone = distTone_[ch];
        for (int i = 0; i < m; ++i) {
            const float t = (float)(i + 1) * invM;
            const float dr = dr0 + (dr1 - dr0) * t, nr = n0 + (n1 - n0) * t, b = b0 + (b1 - b0) * t;
            float y;
            if (distOs_ == 1) {
                y = distAdaa(ch, x[i] + b, dr, nr);
            } else if (distOs_ == 2) {
                float u0, u1;
                ha.up(x[i], u0, u1);
                const float s0 = distAdaa(ch, u0 + b, dr, nr);
                y = ha.down(s0, distAdaa(ch, u1 + b, dr, nr));
            } else {
                float u0, u1, v0, v1, v2, v3;
                ha.up(x[i], u0, u1);
                hb.up(u0, v0, v1);
                hb.up(u1, v2, v3);
                const float s0 = distAdaa(ch, v0 + b, dr, nr), s1 = distAdaa(ch, v1 + b, dr, nr);
                const float s2 = distAdaa(ch, v2 + b, dr, nr), s3 = distAdaa(ch, v3 + b, dr, nr);
                const float w0 = hb.down(s0, s1);
                const float w1 = hb.down(s2, s3);
                y = ha.down(w0, w1);
            }
            const float yd = y - dcX + dcR * dcY;  // retire le continu dû au BIAS
            dcX = y;
            dcY = yd;
            x[i] = lp1(tone, yd, G) * (c0 + (c1 - c0) * t);
        }
        distDcX_[ch] = dcX;
        distDcY_[ch] = dcY;
        distTone_[ch] = tone;
    }
}

// ---- FLANGER : retard court modulé avec réinjection -----------------------------------------

void FxRack::runFlang(float* l, float* r, int m) {
    const Slot& s = slot_[kFxFlang];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const double ph0 = flPh_;
    double ph1 = ph0 + 0.5 * (double)(0.8f + 2.f * (s.p0[0] + s.p[0])) * (double)m / sr_;
    ph1 -= std::floor(ph1);
    flPh_ = ph1;
    const float fb0 = 0.9f * s.p0[2], fb1 = 0.9f * s.p[2];
    for (int ch = 0; ch < 2; ++ch) {
        float* x = ch ? r : l;
        Delay& dl = flDl_[ch];
        const double off = ch ? 0.25 : 0.0;  // LFO décalé d'un quart de tour à droite
        const float d0 = flDelay(s.p0, ph0 + off, fs), d1 = flDelay(s.p, ph1 + off, fs);
        const float dd = (d1 - d0) * invM, dfb = (fb1 - fb0) * invM;
        float d = d0, fb = fb0;
        for (int i = 0; i < m; ++i) {
            d += dd;
            fb += dfb;
            const float y = dl.readHermite(d);
            dl.push(softClip(x[i] + fb * y));
            x[i] = 0.5f * (x[i] + y * (1.f - 0.6f * fb));
        }
    }
}

// ---- PHASER : 6 passe-tout du premier ordre, réinjection légère -----------------------------

void FxRack::runPhase(float* l, float* r, int m) {
    const Slot& s = slot_[kFxPhase];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const double ph0 = phPh_;
    double ph1 = ph0 + 0.5 * (double)(0.6f + 2.f * (s.p0[0] + s.p[0])) * (double)m / sr_;
    ph1 -= std::floor(ph1);
    phPh_ = ph1;
    for (int ch = 0; ch < 2; ++ch) {
        float* x = ch ? r : l;
        const double off = ch ? 0.25 : 0.0;
        const float G0 = phG(s.p0, ph0 + off, fs), G1 = phG(s.p, ph1 + off, fs);
        const float dG = (G1 - G0) * invM;
        float G = G0, last = phLast_[ch];
        float* st = phS_[ch];
        for (int i = 0; i < m; ++i) {
            G += dG;
            float u = x[i] + 0.3f * last;
            for (int k = 0; k < 6; ++k) {
                const float v = (u - st[k]) * G, lp = v + st[k];
                st[k] = lp + v;
                u = 2.f * lp - u;
            }
            last = u;
            x[i] = 0.6f * (x[i] + u);
        }
        phLast_[ch] = last;
    }
}

// ---- CHORUS : trois voix par côté ------------------------------------------------------------

void FxRack::runChor(float* l, float* r, int m) {
    const Slot& s = slot_[kFxChor];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const double ph0 = chPh_;
    double ph1 = ph0 + 0.5 * (double)(0.4f + 2.f * (s.p0[0] + s.p[0])) * (double)m / sr_;
    ph1 -= std::floor(ph1);
    chPh_ = ph1;
    for (int i = 0; i < m; ++i) { chDl_[0].push(l[i]); chDl_[1].push(r[i]); }
    for (int ch = 0; ch < 2; ++ch) {
        float* x = ch ? r : l;
        const Delay& dl = chDl_[ch];
        float o[kChunk] = {};
        for (int v = 0; v < kChorVoices; ++v) {
            const float d0 = chDelay(s.p0, ph0, v, ch, fs), d1 = chDelay(s.p, ph1, v, ch, fs);
            float d = d0 + (float)m;
            const float dd = (d1 - d0) * invM - 1.f;
            for (int i = 0; i < m; ++i) {
                d += dd;
                o[i] += dl.readHermite(d);
            }
        }
        for (int i = 0; i < m; ++i) x[i] = o[i] * 0.5f;
    }
}

// ---- DÉLAI : 20 ms .. 1 s, amorti dans la boucle, léger ping-pong ----------------------------

void FxRack::runDly(float* l, float* r, int m) {
    const Slot& s = slot_[kFxDly];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const float fb0 = 0.9f * s.p0[1], fb1 = 0.9f * s.p[1];
    const float G = onePoleG(20000.f * std::pow(0.05f, s.p[2]), fs);
    const float Gh = onePoleG(60.f, fs);  // passe-haut 60 Hz dans la boucle : les graves ne s'accumulent pas
    constexpr float pp = 0.35f;  // part croisée gauche <-> droite
    const float dd = (dlyT_ - dlyT0_) * invM, dfb = (fb1 - fb0) * invM;
    float d = dlyT0_, fb = fb0, lpL = dlyLp_[0], lpR = dlyLp_[1], hpL = dlyHp_[0], hpR = dlyHp_[1];
    Delay& DL = dlyDl_[0];
    Delay& DR = dlyDl_[1];
    for (int i = 0; i < m; ++i) {
        d += dd;
        fb += dfb;
        const float yL = DL.readHermite(d), yR = DR.readHermite(d);
        float fL = lp1(lpL, yL, G), fR = lp1(lpR, yR, G);
        fL -= lp1(hpL, fL, Gh);
        fR -= lp1(hpR, fR, Gh);
        DL.push(softClip(l[i] + fb * ((1.f - pp) * fL + pp * fR)));
        DR.push(softClip(r[i] * (1.f - pp) + fb * ((1.f - pp) * fR + pp * fL)));
        l[i] = yL;
        r[i] = yR;
    }
    dlyLp_[0] = lpL;
    dlyLp_[1] = lpR;
    dlyHp_[0] = hpL;
    dlyHp_[1] = hpR;
}

// ---- RÉVERBÉRATION : pré-délai, 4 diffuseurs par côté, FDN 8 lignes modulées ----------------

void FxRack::runRev(float* l, float* r, int m) {
    const Slot& s = slot_[kFxRev];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const float rt = (0.3f + 7.f * s.p[0]) * 1.12f;  // compense l'amorti propre au filtre de boucle
    const float Gd = onePoleG(18000.f * std::pow(1500.f / 18000.f, s.p[1]), fs);
    float g[kRevLines], d[kRevLines], dd[kRevLines];
    const float depth = kRevModMs * 0.001f * fs;
    // modulation : sinus lent + marche aléatoire filtrée (pas de motif périodique audible,
    // les modes de la FDN se brouillent sans effet de chorus)
    const float rwA = 1.f - std::exp(-(float)m / (0.12f * fs));
    for (int j = 0; j < kRevLines; ++j) {
        g[j] = std::exp(-6.9078f * revLen_[j] / (rt * fs));
        double ph1 = revPh_[j] + (double)kRevRate[j] * (double)m / sr_;
        ph1 -= std::floor(ph1);
        revPh_[j] = ph1;
        revRng_ = revRng_ * 1664525u + 1013904223u;
        const float nz = (float)(revRng_ >> 8) * (2.f / 16777216.f) - 1.f;
        revRw_[j] += (nz - revRw_[j]) * rwA;
        const float rw = std::clamp(revRw_[j] * 4.f, -1.f, 1.f);
        const float m0 = revM_[j];
        const float m1 = revLen_[j] + depth * (1.f + 0.7f * (float)std::sin(2.0 * kPiD * ph1) + 0.3f * rw);
        revM_[j] = m1;
        d[j] = m0;
        dd[j] = (m1 - m0) * invM;
    }
    const float pre0 = std::max(1.f, s.p0[2] * 0.1f * fs), pre1 = std::max(1.f, s.p[2] * 0.1f * fs);
    const float dpre = (pre1 - pre0) * invM;
    float pre = pre0;
    constexpr float kOut = 0.2f;
    for (int i = 0; i < m; ++i) {
        pre += dpre;
        float x[2];
        for (int c = 0; c < 2; ++c) {
            x[c] = revPre_[c].readLinear(pre);
            revPre_[c].push(c ? r[i] : l[i]);
            for (int k = 0; k < kRevAp; ++k) {  // diffusion : passe-tout de Schroeder
                Delay& ap = revAp_[c][k];
                const float wd = ap.at(revApLen_[c][k]);
                const float w = x[c] + kRevApG[k] * wd;
                ap.push(w);
                x[c] = wd - kRevApG[k] * w;
            }
        }
        float z[kRevLines], sum = 0.f;
        for (int j = 0; j < kRevLines; ++j) {
            d[j] += dd[j];
            z[j] = lp1(revLp_[j], revLine_[j].readHermite(d[j]), Gd);
            sum += z[j];
        }
        sum *= 2.f / (float)kRevLines;  // Householder : I - 2/N 11'
        const float oL = z[0] - z[1] + z[2] - z[3] + z[4] - z[5] + z[6] - z[7];
        const float oR = z[0] + z[1] - z[2] - z[3] + z[4] + z[5] - z[6] - z[7];
        for (int j = 0; j < kRevLines; ++j)
            revLine_[j].push((z[j] - sum) * g[j] + x[j & 1] * kRevSign[j]);
        l[i] = oL * kOut;
        r[i] = oR * kOut;
    }
}

// ---- COMPRESSEUR : stéréo lié, détection passe-haut 90 Hz, genou doux 6 dB, attaque adaptative,
// relâche selon le programme (rapide après un transitoire, lente sur une compression tenue) ----

void FxRack::runComp(float* l, float* r, int m) {
    const Slot& s = slot_[kFxComp];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const float thr = -40.f * s.p[0], ratio = 1.f + 19.f * s.p[1], slope = 1.f - 1.f / ratio;
    constexpr float W = 6.f;
    // attaque adaptative : 10 ms, mais 1 ms quand le signal dépasse de plus de 6 dB la réduction
    // en cours (premier coup après un silence), pour que la compensation de gain ne fasse pas crêter
    const float atk = std::exp(-1.f / (0.010f * fs)), atkFast = std::exp(-1.f / (0.001f * fs)), rel = std::exp(-1.f / (0.080f * fs));
    const float atkSlow = std::exp(-1.f / (0.150f * fs)), relSlow = std::exp(-1.f / (0.900f * fs));
    const float Gsc = onePoleG(90.f, fs);
    const float mk0 = 12.f * s.p0[2], mk1 = 12.f * s.p[2], dmk = (mk1 - mk0) * invM;
    float mk = mk0, gr = compGr_, grS = compSlow_, grMax = compGrMax_, scL = compSc_[0], scR = compSc_[1];
    for (int i = 0; i < m; ++i) {
        mk += dmk;
        // détection sans le grave : une basse ne pompe plus tout le reste
        const float hl = l[i] - lp1(scL, l[i], Gsc), hr = r[i] - lp1(scR, r[i], Gsc);
        const float pk = std::max(std::fabs(hl), std::fabs(hr));
        const float over = 8.6858896f * std::log(pk + 1e-9f) - thr;
        float want = 0.f;
        if (2.f * over > W) want = over * slope;
        else if (2.f * over > -W) { const float t = over + 0.5f * W; want = slope * t * t / (2.f * W); }
        // deux enveloppes : rapide (transitoires) et lente (niveau moyen) ; la réduction suit la
        // plus forte des deux, donc elle relâche vite après un coup isolé mais lentement sur un tenu
        gr = want > gr ? want + (gr - want) * (want - gr > 6.f ? atkFast : atk) : want + (gr - want) * rel;
        grS = want > grS ? want + (grS - want) * atkSlow : want + (grS - want) * relSlow;
        const float g = std::max(gr, 0.8f * grS);
        grMax = std::max(grMax, g);
        const float gain = std::exp((mk - g) * 0.115129255f);
        l[i] *= gain;
        r[i] *= gain;
    }
    compGr_ = gr;
    compSlow_ = grS;
    compSc_[0] = std::isfinite(scL) ? scL : 0.f;
    compSc_[1] = std::isfinite(scR) ? scR : 0.f;
    compGrMax_ = grMax;
}

// ---- EQ : trois cloches en SVF trapézoïdal (Simper), Q = 1. Les coefficients glissent dans le
// sous-bloc : gain et fréquence se règlent ou s'automatisent sans transitoire ----------------------

void FxRack::runEq(float* l, float* r, int m) {
    const Slot& s = slot_[kFxEq];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    struct C { float a1, a2, a3, m1; };
    const auto coef = [&](float p, float fp) {
        const float A = std::pow(10.f, (p - 0.5f) * 24.f / 40.f);
        const float g = std::tan(kPi * std::min(20.f * std::pow(1000.f, fp), 0.45f * fs) / fs);
        const float k = 1.f / A;  // Q = 1 : k = 1 / (Q A)
        C c;
        c.a1 = 1.f / (1.f + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        c.m1 = k * (A * A - 1.f);
        return c;
    };
    for (int b = 0; b < 3; ++b) {
        if (s.p0[b] == 0.5f && s.p[b] == 0.5f) {  // bande à plat : rien à calculer, mémoire vidée
            for (auto& c : eqIc_[b]) c[0] = c[1] = 0.f;
            continue;
        }
        const C c0 = coef(s.p0[b], eqF0_[b]), c1 = coef(s.p[b], eqF_[b]);
        const C dc{(c1.a1 - c0.a1) * invM, (c1.a2 - c0.a2) * invM, (c1.a3 - c0.a3) * invM, (c1.m1 - c0.m1) * invM};
        for (int ch = 0; ch < 2; ++ch) {
            float* x = ch ? r : l;
            float ic1 = eqIc_[b][ch][0], ic2 = eqIc_[b][ch][1];
            C c = c0;
            for (int i = 0; i < m; ++i) {
                c.a1 += dc.a1;
                c.a2 += dc.a2;
                c.a3 += dc.a3;
                c.m1 += dc.m1;
                const float v0 = x[i], v3 = v0 - ic2;
                const float v1 = c.a1 * ic1 + c.a2 * v3, v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
                ic1 = 2.f * v1 - ic1;
                ic2 = 2.f * v2 - ic2;
                x[i] = v0 + c.m1 * v1;
            }
            eqIc_[b][ch][0] = std::isfinite(ic1) ? ic1 : 0.f;
            eqIc_[b][ch][1] = std::isfinite(ic2) ? ic2 : 0.f;
        }
    }
}

// ---- FILTRE : SVF passe-bas 12 dB/oct (Cytomic, trapèzes), saturation d'entrée --------------

void FxRack::runFilt(float* l, float* r, int m) {
    const Slot& s = slot_[kFxFilt];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const auto gOf = [&](float p) { return std::tan(kPi * std::min(20.f * std::pow(1000.f, p), 0.45f * fs) / fs); };
    const float g0 = gOf(s.p0[0]), g1 = gOf(s.p[0]);
    const float k0 = 2.f - 1.99f * s.p0[1], k1 = 2.f - 1.99f * s.p[1];
    const float dg0 = 1.f + 7.f * s.p0[2], dg1 = 1.f + 7.f * s.p[2];
    const float am0 = std::min(1.f, 4.f * s.p0[2]), am1 = std::min(1.f, 4.f * s.p[2]);
    const float c0 = std::pow(dg0, -0.5f), c1 = std::pow(dg1, -0.5f);
    for (int ch = 0; ch < 2; ++ch) {
        float* x = ch ? r : l;
        float ic1 = svf1_[ch], ic2 = svf2_[ch];
        for (int i = 0; i < m; ++i) {
            const float t = (float)(i + 1) * invM;
            const float g = g0 + (g1 - g0) * t, k = k0 + (k1 - k0) * t;
            float in = x[i];
            const float am = am0 + (am1 - am0) * t;
            if (am > 0.f) {
                const float dgv = dg0 + (dg1 - dg0) * t;
                in += am * (fastTanh(in * dgv) * (c0 + (c1 - c0) * t) - in);
            }
            const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = in - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.f * v1 - ic1;
            ic2 = 2.f * v2 - ic2;
            // auto-oscillation bornée : l'état passe-bande sature en douceur
            const float aic = std::fabs(ic1);
            if (aic > 4.f) ic1 = (ic1 < 0.f ? -1.f : 1.f) * (4.f + 2.f * fastTanh((aic - 4.f) * 0.5f));
            x[i] = softClip(v2);
        }
        svf1_[ch] = std::isfinite(ic1) ? ic1 : 0.f;
        svf2_[ch] = std::isfinite(ic2) ? ic2 : 0.f;
    }
}

// ---- VINTAGE : usure de bande, souffle, lo-fi ------------------------------------------------

void FxRack::runVintage(float* l, float* r, int m) {
    const Slot& s = slot_[kFxVintage];
    const float fs = (float)sr_, invM = 1.f / (float)m;
    const float u0 = s.p0[0], u1 = s.p[0], n1 = s.p[1], lo1 = s.p[2];
    // pleurage 0,55 Hz (jusqu'à 1,6 ms) et scintillement 7,5 Hz (0,12 ms) : la hauteur ondule
    const double w0 = vtWow_, f0 = vtFlut_;
    vtWow_ += 0.55 * (double)m / sr_;
    vtFlut_ += 7.5 * (double)m / sr_;
    vtWow_ -= std::floor(vtWow_);
    vtFlut_ -= std::floor(vtFlut_);
    auto dly = [&](double w, double f, float u) {
        return 2.f + (0.0016f * (0.5f + 0.5f * (float)std::sin(2.0 * kPiD * w)) + 0.00012f * (0.5f + 0.5f * (float)std::sin(2.0 * kPiD * f))) * fs * u * 1.2f;
    };
    const float d0 = dly(w0, f0, u0), d1 = dly(vtWow_, vtFlut_, u1);
    // bande : saturation douce et aigus qui s'éteignent (20 kHz -> 5 kHz)
    const float sat = 1.f + 3.f * u1, satN = 1.f / fastTanh(sat * 0.5f) * 0.5f;
    const float G = onePoleG(20000.f * std::pow(0.25f, u1), fs);
    // lo-fi : fréquence de 48 kHz vers 6 kHz (maintien), résolution de 16 vers 6 bits
    const float hold = 1.f - 0.87f * lo1;
    const float q = std::pow(2.f, 15.f - 9.f * lo1);
    const float nAmt = n1 * n1 * 0.05f;
    // le souffle suit le signal (pas de bruit sans son)
    float env = vtEnv_;
    const float envA = 1.f - std::exp(-1.f / (0.02f * fs)), envR = 1.f - std::exp(-1.f / (0.6f * fs));
    const float Gn = onePoleG(5000.f, fs);
    for (int i = 0; i < m; ++i) {
        const float t = (float)(i + 1) * invM;
        const float d = d0 + (d1 - d0) * t;
        const float a = std::max(std::fabs(l[i]), std::fabs(r[i]));
        env += (a - env) * (a > env ? envA : envR);
        vtRng_ = vtRng_ * 1664525u + 1013904223u;
        const float wn = (float)(vtRng_ >> 8) * (2.f / 16777216.f) - 1.f;
        // craquement : impulsion rare, plus fréquente avec SOUFFLE
        vtRng_ = vtRng_ * 1664525u + 1013904223u;
        if ((float)(vtRng_ >> 8) * (1.f / 16777216.f) < n1 * 0.0004f) vtCrk_ = ((vtRng_ & 1) ? 1.f : -1.f) * (0.3f + 0.7f * n1);
        const float crk = vtCrk_;
        vtCrk_ *= 0.7f;
        vtHoldPh_ += hold;
        const bool take = vtHoldPh_ >= 1.f;
        if (take) vtHoldPh_ -= 1.f;
        for (int c = 0; c < 2; ++c) {
            float* x = c ? r : l;
            Delay& dl = vtDl_[c];
            dl.push(x[i]);
            float y = u1 > 0.0005f || u0 > 0.0005f ? dl.readHermite(d) : x[i];
            y = fastTanh(y * sat * 0.5f) * satN;
            y = lp1(vtLp_[c], y, G);
            const float nz = lp1(vtNoiseLp_[c], wn * (c ? -1.f : 1.f), Gn);
            y += (nz * nAmt + crk * nAmt * 3.f) * std::min(1.f, env * 6.f);
            if (lo1 > 0.001f) {
                if (take) vtHold_[c] = std::round(y * q) / q;
                y = vtHold_[c];
            }
            x[i] = y;
        }
    }
    vtEnv_ = env < 1e-6f ? 0.f : env;
}

// ---- PUMP : effet de sidechain calé sur le tempo ------------------------------------------------

void FxRack::runPump(float* l, float* r, int m) {
    const Slot& s = slot_[kFxPump];
    static const double kPeriod[4] = {2.0, 1.0, 0.5, 0.25};  // noires : 1/2, 1/4, 1/8, 1/16
    const double per = kPeriod[std::clamp((int)(s.p[1] * 4.f), 0, 3)];
    const float depth = s.p[0], expo = 1.f + 5.f * s.p[2];
    const float sm = 1.f - std::exp(-1.f / (0.0015f * (float)sr_));  // 1,5 ms : pas de clic sur le temps
    float g = pumpG_;
    for (int i = 0; i < m; ++i) {
        double ph = (chunkBeat_ + (double)i * beatPerSample_) / per;
        ph -= std::floor(ph);
        // creux juste après le temps, puis remontée (COURBE : plus ou moins tardive)
        const float p = (float)ph;
        const float duck = p < 0.03f ? p / 0.03f : std::pow(1.f - (p - 0.03f) / 0.97f, expo);
        const float want = 1.f - depth * duck;
        g += (want - g) * sm;
        l[i] *= g;
        r[i] *= g;
    }
    pumpG_ = g;
}

} // namespace raw2
