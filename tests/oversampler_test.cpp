// Checks the half-band design: pass-band gain, stop-band rejection and round trip.
#include "../src/core/Oversampler.h"

#include <cmath>
#include <cstdio>

using namespace tw;

static double toneGainDown(HalfbandStage& st, double freqHi) {
    // Feed a tone at the high rate, measure the level after decimation.
    st.reset();
    double peak = 0;
    for (int n = 0; n < 8000; ++n) {
        float a = (float)std::sin(2 * 3.14159265358979323846 * freqHi * (2 * n));
        float b = (float)std::sin(2 * 3.14159265358979323846 * freqHi * (2 * n + 1));
        V4 y = st.downsample(V4(a, a, 0, 0), V4(b, b, 0, 0));
        if (n > 4000) peak = std::fmax(peak, std::fabs(y.lane(0)));
    }
    return peak;
}

int main() {
    int fails = 0;
    double c[12];
    designHalfband(c, 12, 0.025);
    std::printf("coefs:");
    for (double v : c) std::printf(" %.5f", v);
    std::printf("\n");

    HalfbandStage st;
    st.init(12, 0.025);
    const double pass = toneGainDown(st, 0.20);   // 0.2 * high rate = 0.4 * low rate: pass band
    const double stop = toneGainDown(st, 0.30);   // 0.3 * high rate: must fold back, rejected
    std::printf("passband gain %.4f, stopband %.1f dB\n", pass, 20 * std::log10(stop + 1e-12));
    if (std::fabs(pass - 1.0) > 0.01) { std::printf("FAIL passband\n"); ++fails; }
    if (20 * std::log10(stop + 1e-12) > -70) { std::printf("FAIL stopband\n"); ++fails; }

    for (int stages = 1; stages <= 4; ++stages) {
        static Oversampler os;
        os.setStages(stages);
        os.reset();
        static V4 in[64], hi[64 * 16], out[64];
        double peak = 0, peakHi = 0;
        for (int blk = 0; blk < 200; ++blk) {
            for (int i = 0; i < 64; ++i) {
                float s = (float)std::sin(2 * 3.14159265358979323846 * 1000.0 / 48000.0 * (blk * 64 + i));
                in[i] = V4(s, -s, 0, 0);
            }
            os.up(in, hi, 64);
            os.down(hi, out, 64);
            if (blk > 100)
                for (int i = 0; i < 64; ++i) {
                    peak = std::fmax(peak, std::fabs(out[i].lane(0)));
                    peakHi = std::fmax(peakHi, std::fabs(hi[i].lane(1)));
                }
        }
        std::printf("%2dx round trip 1 kHz: gain %.4f (high-rate peak %.4f)\n", 1 << stages, peak, peakHi);
        if (std::fabs(peak - 1.0) > 0.01 || std::fabs(peakHi - 1.0) > 0.02) { std::printf("FAIL roundtrip\n"); ++fails; }
    }
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
