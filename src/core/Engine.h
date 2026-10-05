// Audio engine. Runs on the host's audio thread: no locks, no allocation, no system
// calls inside process(). Everything is allocated in prepare().
//
// Signal flow (stereo, processed in SIMD lanes):
//   input trim (manual + auto gain staging)
//   -> Sub -> Punch -> Compression                       (base rate)
//   -> Elevate: saturation + exciter                     (oversampled)
//   -> Space: width (bass kept mono) + ambience          (base rate)
//   -> Dry/Wet mix
//   -> Clipper (hard / soft / analog)                    (oversampled)
//   -> Limiter (no look-ahead, -1 dBFS ceiling) -> Output gain -> click-free bypass
// No stage adds latency: the reported latency is 0 samples.
#pragma once

#include "Oversampler.h"
#include "Params.h"
#include "Simd.h"
#include "SpscQueue.h"

#include <atomic>
#include <vector>

namespace tw {

struct Meters {
    std::atomic<float> inPeak{0}, outPeak{0}, outRms{0}, gainReductionDb{0}, limiterDb{0}, clipAmount{0};
};

class Engine {
public:
    static constexpr int kChunk = 64;

    Engine();
    void prepare(double sampleRate, int maxBlock);  // not real-time safe
    void reset();
    // in/out may alias. inR/outR may be null for mono.
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, const ParamStore& params);

    Meters meters;
    // Mono input samples for the analysis thread (pushed by the audio thread, popped by the analyzer).
    SpscQueue<float> analysisFifo{1 << 16};
    // Auto gain staging target, written by the analyzer.
    std::atomic<float> autoTrimDb{0.f};
    double sampleRate() const { return fs_; }

private:
    void processChunk(const float* inL, const float* inR, float* outL, float* outR, int n, const ParamStore& p);

    double fs_ = 48000.0;

    // smoothed controls
    float trimLin_ = 1.f, mix_ = 0.8f, outLin_ = 1.f, bypassMix_ = 0.f, autoTrimDbSm_ = 0.f;

    // Sub: TPT state-variable low-pass (stereo in lanes 0/1)
    float subA1_ = 0, subA2_ = 0, subA3_ = 0;
    V4 subIc1_, subIc2_;
    // Punch: fast / slow envelopes (linked)
    float envFast_ = 0.f, envSlow_ = 0.f;
    // Compression
    float compEnvDb_ = 0.f;
    // Elevate (oversampled): exciter one-pole low-pass, DC blocker
    V4 exLp_, dcX_, dcY_;
    Oversampler osElevate_, osClip_;
    // Space: side low-pass for mono bass, 4-line FDN ambience
    V4 sideLp_;
    std::vector<float> fdnBuf_[4];
    int fdnLen_[4] = {0, 0, 0, 0};
    int fdnPos_[4] = {0, 0, 0, 0};
    float fdnLp_[4] = {0, 0, 0, 0};
    // Limiter
    float limGain_ = 1.f;

    // work buffers
    V4 dry_[kChunk], a_[kChunk], b_[kChunk], hi_[kChunk * 16];
    float mono_[kChunk];
};

} // namespace tw
