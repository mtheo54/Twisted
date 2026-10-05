// Zero-latency oversampling: cascaded 2x polyphase IIR half-band filters
// (two parallel all-pass chains, minimum phase, no look-ahead, no FIR delay).
// Stereo is processed in one SIMD register: lanes = [L path0, R path0, L path1, R path1].
#pragma once

#include "Simd.h"

namespace tw {

// Designs the all-pass coefficients of a polyphase IIR half-band filter
// (elliptic design, same method as Laurent de Soras' HIIR).
// transition: normalised transition bandwidth (0 < t < 0.5).
void designHalfband(double* coefs, int numCoefs, double transition);

class HalfbandStage {
public:
    static constexpr int kMaxCoefs = 12;
    void init(int numCoefs, double transition);
    void reset();
    // in: stereo sample (lanes 0/1). out[0], out[1]: two stereo samples at twice the rate.
    void upsample(V4 in, V4& out0, V4& out1);
    // in0, in1: two consecutive stereo samples at the high rate. Returns one stereo sample.
    V4 downsample(V4 in0, V4 in1);

private:
    int numSections_ = 0;          // all-pass sections per path
    V4 coef_[kMaxCoefs / 2];       // [c_even, c_even, c_odd, c_odd]
    V4 upX_[kMaxCoefs / 2], upY_[kMaxCoefs / 2];
    V4 dnX_[kMaxCoefs / 2], dnY_[kMaxCoefs / 2];
};

class Oversampler {
public:
    static constexpr int kMaxStages = 4;  // 16x
    Oversampler();
    void reset();
    // stages: 0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x, 4 = 16x
    void setStages(int stages);
    int stages() const { return stages_; }
    int factor() const { return 1 << stages_; }
    // Upsamples n stereo samples into hi (n * factor samples); hi must hold n * 16.
    void up(const V4* in, V4* hi, int n);
    // Downsamples hi (n * factor samples) back into out (n samples).
    void down(const V4* hi, V4* out, int n);

private:
    HalfbandStage up_[kMaxStages], down_[kMaxStages];
    int stages_ = 0;
    V4 scratch_[2][64 * 16];
};

} // namespace tw
