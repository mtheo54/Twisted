#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace tw {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kCeiling = 0.8912509f;  // -1 dBFS

inline float dbToLin(float db) { return std::pow(10.f, db * 0.05f); }
inline float linToDb(float lin) { return 20.f * std::log10(lin + 1e-9f); }
inline float onePoleCoef(float ms, double fs) { return (float)std::exp(-1.0 / (ms * 0.001 * fs)); }

} // namespace

Engine::Engine() { prepare(48000.0, 512); }

void Engine::prepare(double sampleRate, int /*maxBlock*/) {
    fs_ = sampleRate > 0 ? sampleRate : 48000.0;
    // TPT state-variable low-pass (Zavalishin), Butterworth Q
    const float g = std::tan(kPi * 95.f / (float)fs_), k = 1.41421356f;
    subA1_ = 1.f / (1.f + g * (g + k));
    subA2_ = g * subA1_;
    subA3_ = g * subA2_;
    const float ms[4] = {11.3f, 13.7f, 17.9f, 23.1f};
    for (int k = 0; k < 4; ++k) {
        fdnLen_[k] = std::max(8, (int)(ms[k] * 0.001 * fs_));
        fdnBuf_[k].assign((size_t)fdnLen_[k], 0.f);
    }
    reset();
}

void Engine::reset() {
    subIc1_ = subIc2_ = exLp_ = dcX_ = dcY_ = sideLp_ = V4(0.f);
    envFast_ = envSlow_ = 0.f;
    compEnvDb_ = 0.f;
    limGain_ = 1.f;
    for (int k = 0; k < 4; ++k) fdnPos_[k] = 0;
    for (int k = 0; k < 4; ++k) { std::fill(fdnBuf_[k].begin(), fdnBuf_[k].end(), 0.f); fdnLp_[k] = 0.f; }
    osElevate_.reset();
    osClip_.reset();
}

void Engine::process(const float* inL, const float* inR, float* outL, float* outR, int n, const ParamStore& p) {
    DenormalGuard guard;
    int done = 0;
    while (done < n) {
        const int len = std::min(kChunk, n - done);
        processChunk(inL + done, inR ? inR + done : nullptr, outL + done, outR ? outR + done : nullptr, len, p);
        done += len;
    }
}

void Engine::processChunk(const float* inL, const float* inR, float* outL, float* outR, int n, const ParamStore& p) {
    const float chunkSec = (float)(n / fs_);

    // ---- parameters for this chunk -------------------------------------------------------
    const bool autoGain = p.get(kAutoGain) >= 0.5f;
    const float autoTarget = autoGain ? autoTrimDb.load(std::memory_order_relaxed) : 0.f;
    autoTrimDbSm_ += (autoTarget - autoTrimDbSm_) * std::min(1.f, chunkSec / 0.8f);
    const float autoLin = dbToLin(autoTrimDbSm_);

    const float trimEnd = dbToLin(p.get(kInput)) * autoLin;
    const float mixEnd = p.get(kMix) * 0.01f;
    const float outEnd = dbToLin(p.get(kOutput));
    const float bypassEnd = p.get(kBypass) >= 0.5f ? 1.f : 0.f;
    const float inv = 1.f / (float)n;
    const float trimInc = (trimEnd - trimLin_) * inv, mixInc = (mixEnd - mix_) * inv;
    const float outInc = (outEnd - outLin_) * inv;
    const float bypassInc = std::clamp(bypassEnd - bypassMix_, -chunkSec / 0.01f, chunkSec / 0.01f) * inv;  // 10 ms fade

    const float elevate = p.get(kElevate) * 0.01f;
    const float comp = p.get(kComp) * 0.01f;
    const float punch = p.get(kPunch) * 0.01f;
    const float sub = p.get(kSub) * 0.01f;
    const float space = p.get(kSpace) * 0.01f;
    const float clip = p.get(kClip) * 0.01f;
    const float limit = p.get(kLimit) * 0.01f;
    const int character = (int)p.get(kCharacter);
    const int clipStyle = (int)p.get(kClipStyle);
    const int osStages = (int)p.get(kOversampling);
    osElevate_.setStages(osStages);
    osClip_.setStages(osStages);

    // Sub
    const V4 sA1(subA1_), sA2(subA2_), sA3(subA3_);
    const V4 subGain(sub * 1.0f), subHarm(sub * 0.35f);
    // Punch envelopes
    const float fAtt = onePoleCoef(0.5f, fs_), fRel = onePoleCoef(25.f, fs_);
    const float sAtt = onePoleCoef(25.f, fs_), sRel = onePoleCoef(250.f, fs_);
    // Compression: threshold and ratio both follow the knob
    const float thr = -4.f - comp * 26.f, ratio = 1.4f + comp * 2.6f, knee = 6.f;
    const float slope = 1.f / ratio - 1.f;
    const float cAtt = onePoleCoef(8.f, fs_), cRel = onePoleCoef(140.f, fs_);
    const float makeup = -thr * (1.f - 1.f / ratio) * 0.35f * comp;

    float peakIn = 0.f, grMax = 0.f;
    float trim = trimLin_;
    for (int i = 0; i < n; ++i) {
        const float l = inL[i], r = inR ? inR[i] : l;
        dry_[i] = V4(l, r, 0.f, 0.f);
        mono_[i] = 0.5f * (l + r);
        peakIn = std::max(peakIn, std::max(std::fabs(l), std::fabs(r)));
        trim += trimInc;
        V4 x = dry_[i] * V4(trim);

        // Sub: band below ~95 Hz, lifted and gently saturated for harmonics
        const V4 v3 = x - subIc2_;
        const V4 v1 = sA1 * subIc1_ + sA2 * v3;
        const V4 v2 = subIc2_ + sA2 * subIc1_ + sA3 * v3;
        subIc1_ = V4(2.f) * v1 - subIc1_;
        subIc2_ = V4(2.f) * v2 - subIc2_;
        x = x + v2 * subGain + vtanh(v2 * V4(4.f)) * subHarm;

        // Punch: transient shaper on a stereo-linked envelope
        const float lvl = std::max(std::fabs(x.lane(0)), std::fabs(x.lane(1)));
        envFast_ = lvl + (lvl > envFast_ ? fAtt : fRel) * (envFast_ - lvl);
        envSlow_ = lvl + (lvl > envSlow_ ? sAtt : sRel) * (envSlow_ - lvl);
        float punchDb = 0.f;
        if (envSlow_ > 1e-4f) punchDb = std::clamp((linToDb(envFast_) - linToDb(envSlow_)) * punch * 1.2f, -4.f, 9.f);

        // Compression: feed-forward, soft knee, stereo-linked
        const float lvlDb = linToDb(lvl * dbToLin(punchDb));
        const float over = lvlDb - thr;
        float gr = 0.f;
        if (2.f * over > knee) gr = slope * over;
        else if (2.f * over > -knee) gr = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.f * knee);
        compEnvDb_ = gr + (gr < compEnvDb_ ? cAtt : cRel) * (compEnvDb_ - gr);
        grMax = std::min(grMax, compEnvDb_);

        a_[i] = x * V4(dbToLin(punchDb + compEnvDb_ * (comp > 0.001f ? 1.f : 0.f) + makeup));
    }
    trimLin_ = trimEnd;

    // ---- Elevate (oversampled): saturation + harmonic exciter ------------------------------
    const int f = osElevate_.factor();
    if (elevate > 0.001f) {
        osElevate_.up(a_, hi_, n);
        const double fsHi = fs_ * f;
        const V4 exC((float)(1.0 - std::exp(-2.0 * 3.14159265 * 3500.0 / fsHi)));
        const float drive = 1.f + elevate * 5.f;
        const V4 d(drive), invD(1.f / drive), amt(elevate), mk(1.f + 0.4f * elevate), exAmt(elevate * 0.8f);
        for (int j = 0; j < n * f; ++j) {
            const V4 y = hi_[j];
            exLp_ = exLp_ + exC * (y - exLp_);
            const V4 hb = y - exLp_;
            const V4 u = y * d;
            V4 s;
            switch (character) {
            case 0: s = vtanh(u + V4(0.1f)) - V4(0.0997f); break;                         // tape
            case 1: { const V4 t = vtanh(u); s = t - V4(0.15f) * t * t; } break;          // triode
            case 2: s = u / vsqrt(vsqrt(V4(1.f) + u * u * u * u)); break;                 // transistor
            default: {                                                                    // wavefolder
                const V4 w = u + V4(1.f);
                const V4 m = w - V4(4.f) * vfloor(w * V4(0.25f));
                s = V4(1.f) - vabs(m - V4(2.f));
            } break;
            }
            hi_[j] = y + (s * invD * mk - y) * amt + vtanh(hb * V4(4.f)) * V4(0.25f) * exAmt;
        }
        osElevate_.down(hi_, b_, n);
    } else {
        for (int i = 0; i < n; ++i) b_[i] = a_[i];
    }

    // ---- Space, DC blocker, undo auto gain, dry/wet -----------------------------------------
    const float width = 1.f + space * 0.8f;
    const float sideLpC = 1.f - std::exp(-2.f * kPi * 120.f / (float)fs_);
    const float fb = 0.45f + space * 0.3f, damp = 0.35f, ambWet = space * 0.32f;
    const float invAuto = 1.f / autoLin;
    float mix = mix_;
    for (int i = 0; i < n; ++i) {
        // DC blocker (the asymmetric saturation curves add some DC)
        const V4 x0 = b_[i];
        dcY_ = x0 - dcX_ + V4(0.9995f) * dcY_;
        dcX_ = x0;
        float l = dcY_.lane(0), r = dcY_.lane(1);

        const float mid = 0.5f * (l + r);
        float side = 0.5f * (l - r);
        // keep the low end mono: the side signal below 120 Hz is not widened (and fades with Space)
        const float sl = sideLp_.lane(0) + sideLpC * (side - sideLp_.lane(0));
        sideLp_ = V4(sl, 0, 0, 0);
        side = (side - sl) * width + sl * (1.f - space);

        // 4-line feedback delay network, Householder mixing, damped
        float y[4];
        for (int k = 0; k < 4; ++k) y[k] = fdnBuf_[k][(size_t)fdnPos_[k]];
        const float sum = 0.5f * (y[0] + y[1] + y[2] + y[3]);
        for (int k = 0; k < 4; ++k) {
            const float h = y[k] - sum;
            fdnLp_[k] += damp * (h - fdnLp_[k]);
            fdnBuf_[k][(size_t)fdnPos_[k]] = mid * 0.5f + fb * fdnLp_[k];
            if (++fdnPos_[k] == fdnLen_[k]) fdnPos_[k] = 0;
        }
        l = mid + side + ambWet * (y[0] + y[2]) * 0.5f;
        r = mid - side + ambWet * (y[1] + y[3]) * 0.5f;

        mix += mixInc;
        const V4 wet = V4(l, r, 0.f, 0.f) * V4(invAuto);
        a_[i] = dry_[i] + (wet - dry_[i]) * V4(mix);
    }
    mix_ = mixEnd;

    // ---- Clipper (oversampled) ---------------------------------------------------------------
    float clipped = 0.f;
    if (clip > 0.001f) {
        osClip_.up(a_, hi_, n);
        const float pre = dbToLin(clip * 12.f);
        const V4 P(pre), T(kCeiling), invT(1.f / kCeiling);
        for (int j = 0; j < n * f; ++j) {
            const V4 u = hi_[j] * P;
            V4 y;
            if (clipStyle == 0) y = vclamp(u, -kCeiling, kCeiling);
            else if (clipStyle == 1) y = T * vtanh(u * invT);
            else { const V4 v = vclamp(u * invT, -1.f, 1.f); y = T * (V4(1.5f) * v - V4(0.5f) * v * v * v); }
            clipped = std::max(clipped, std::fabs(u.lane(0)) - kCeiling);
            hi_[j] = y;
        }
        osClip_.down(hi_, b_, n);
    } else {
        for (int i = 0; i < n; ++i) b_[i] = a_[i];
    }

    // ---- Limiter, output gain, bypass --------------------------------------------------------
    const float limDrive = dbToLin(limit * 9.f);
    const float limRel = 1.f - onePoleCoef(80.f, fs_);
    float out = outLin_, byp = bypassMix_;
    float peakOut = 0.f, sumSq = 0.f, limMin = 1.f;
    for (int i = 0; i < n; ++i) {
        V4 x = b_[i];
        if (limit > 0.001f) {
            x = x * V4(limDrive);
            const float lvl = std::max(std::fabs(x.lane(0)), std::fabs(x.lane(1)));
            const float target = lvl > kCeiling ? kCeiling / lvl : 1.f;
            limGain_ = target < limGain_ ? target : limGain_ + (target - limGain_) * limRel;  // instant attack
            x = x * V4(limGain_);
            limMin = std::min(limMin, limGain_);
        }
        out += outInc;
        byp += bypassInc;
        x = x * V4(out);
        x = x + (dry_[i] - x) * V4(byp);
        const float l = x.lane(0), r = x.lane(1);
        if (outR) { outL[i] = l; outR[i] = r; }
        else outL[i] = 0.5f * (l + r);
        peakOut = std::max(peakOut, std::max(std::fabs(l), std::fabs(r)));
        sumSq += 0.5f * (l * l + r * r);
    }
    outLin_ = outEnd;
    bypassMix_ = std::clamp(byp, 0.f, 1.f);

    // ---- meters and analysis feed (relaxed atomics, a failed push just drops samples) -------
    auto maxStore = [](std::atomic<float>& a, float v) { if (v > a.load(std::memory_order_relaxed)) a.store(v, std::memory_order_relaxed); };
    maxStore(meters.inPeak, peakIn);
    maxStore(meters.outPeak, peakOut);
    meters.outRms.store(std::sqrt(sumSq * inv), std::memory_order_relaxed);
    meters.gainReductionDb.store(grMax, std::memory_order_relaxed);
    meters.limiterDb.store(linToDb(limMin), std::memory_order_relaxed);
    maxStore(meters.clipAmount, clipped);
    analysisFifo.pushMany(mono_, (size_t)n);
}

} // namespace tw
