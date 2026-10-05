#include "Analyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace tw {

namespace {
// Simple state-variable filter used for band splitting (not real-time critical here).
struct Band {
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1.41421356f;
    void set(float fc, float fs) {
        const float g = std::tan(3.14159265f * std::min(fc, fs * 0.45f) / fs);
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    // returns low-pass, writes band-pass and high-pass
    float tick(float x, float& bp, float& hp) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        bp = v1;
        hp = x - k * v1 - v2;
        return v2;
    }
};
} // namespace

void classifySource(float low, float mid, float high, float trans, float probs[kNumClasses]) {
    const float raw[kNumClasses] = {
        low * 1.5f + trans * 0.9f - mid * 0.6f,                    // Kick / 808
        low * 1.4f + (1.f - trans) * 0.7f - high,                  // Bass
        mid * 1.8f - low * 0.6f,                                   // Vocal
        trans * 1.1f + high * 1.2f + low * 0.5f,                   // Drum bus
        mid * 1.2f + high * 0.5f - trans * 0.3f,                   // Guitar / synth
        1.1f - std::fabs(low - 0.35f) - std::fabs(high - 0.12f) * 1.5f - std::fabs(trans - 0.3f),  // Master
    };
    float sum = 0.f;
    for (int i = 0; i < kNumClasses; ++i) { probs[i] = std::exp(raw[i] * 3.2f); sum += probs[i]; }
    for (int i = 0; i < kNumClasses; ++i) probs[i] /= sum;
}

Analyzer::Analyzer(Engine& engine) : engine_(engine) {}
Analyzer::~Analyzer() { stop(); }

void Analyzer::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void Analyzer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void Analyzer::run() {
    constexpr int kHop = 256;
    float buf[kHop];
    int fill = 0;
    float fsSeen = 0.f;
    Band lowBand, midLo, midHi, highBand;
    float msSlow = 0.f;                        // 3 s mean square for gain staging
    float lowE = 0.f, lowAvg = 0.f;            // bass hit detector
    double lastHit = -1.0, clock = 0.0;
    uint32_t seenRequest = 0;
    // recognition accumulators
    double accLow = 0, accMid = 0, accHigh = 0, accFlux = 0, accTime = 0;
    float prevLow = 0.f;
    int accN = 0;

    while (running_.load(std::memory_order_relaxed)) {
        const size_t got = engine_.analysisFifo.popMany(buf + fill, (size_t)(kHop - fill));
        fill += (int)got;
        if (fill < kHop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }
        fill = 0;

        const float fs = fs_.load();
        if (fs != fsSeen) {
            fsSeen = fs;
            lowBand.set(150.f, fs);
            midLo.set(300.f, fs);
            midHi.set(3000.f, fs);
            highBand.set(5000.f, fs);
        }
        const float hopSec = kHop / fs;
        clock += hopSec;

        double eLow = 0, eMid = 0, eHigh = 0, eAll = 0;
        for (int i = 0; i < kHop; ++i) {
            const float x = buf[i];
            float bp, hp, bp2, hp2, bp3, hp3, bp4, hp4;
            const float lo = lowBand.tick(x, bp, hp);
            const float ml = midLo.tick(x, bp2, hp2);       // hp2: above 300 Hz
            const float mh = midHi.tick(hp2, bp3, hp3);     // mh: 300 Hz .. 3 kHz
            highBand.tick(x, bp4, hp4);                     // hp4: above 5 kHz
            (void)ml;
            eLow += lo * lo;
            eMid += mh * mh;
            eHigh += hp4 * hp4;
            eAll += x * x;
        }
        const float ms = (float)(eAll / kHop);

        // Gain staging: slow mean square, target -18 dBFS RMS
        const float a = std::exp(-hopSec / 3.f);
        msSlow = a * msSlow + (1.f - a) * ms;
        const float rmsDb = 10.f * std::log10(msSlow + 1e-12f);
        inputRmsDb_.store(rmsDb, std::memory_order_relaxed);
        const bool signal = rmsDb > -60.f;
        hasSignal_.store(signal, std::memory_order_relaxed);
        if (signal) engine_.autoTrimDb.store(std::clamp(-18.f - rmsDb, -18.f, 18.f), std::memory_order_relaxed);

        // Bass hits: low-band energy jumping above its running average
        const float e = std::sqrt((float)(eLow / kHop)) * 2.5f;
        lowE = e;
        if (e > lowAvg * 1.35f + 0.02f && e > 0.08f && clock - lastHit > 0.18) {
            lastHit = clock;
            BassHit hit;
            hit.strength = std::clamp(0.35f + (e - lowAvg) * 2.0f, 0.f, 1.f);
            bassHits.push(hit);
        }
        lowAvg += (lowE - lowAvg) * 0.08f;

        // Recognition: 2 s of averaged band ratios and low-band flux
        const uint32_t req = recognizeRequest_.load();
        if (req != seenRequest) {
            seenRequest = req;
            accLow = accMid = accHigh = accFlux = accTime = 0;
            accN = 0;
            listening_.store(true);
        }
        if (listening_.load()) {
            const double tot = eLow + eMid + eHigh + 1e-12;
            accLow += eLow / tot;
            accMid += eMid / tot;
            accHigh += eHigh / tot;
            accFlux += std::max(0.f, e - prevLow);
            accTime += hopSec;
            ++accN;
            listenProgress_.store((float)std::min(1.0, accTime / 2.0));
            if (accTime >= 2.0) {
                float probs[kNumClasses];
                classifySource((float)(accLow / accN), (float)(accMid / accN), (float)(accHigh / accN),
                               std::min(1.f, (float)(accFlux / accN) * 12.f), probs);
                int best = 0;
                for (int i = 1; i < kNumClasses; ++i) if (probs[i] > probs[best]) best = i;
                recognizedClass_.store(best);
                recognizedConfidence_.store(probs[best]);
                listening_.store(false);
                recognitionCount_.fetch_add(1);
            }
        }
        prevLow = e;
    }
}

} // namespace tw
