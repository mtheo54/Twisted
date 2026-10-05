// Runs the full chain on a drum-like test signal with every oversampling factor and
// clipper style: checks for NaN / Inf, the output ceiling, dry passthrough and speed.
#include "../src/core/Engine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

using namespace tw;

int main() {
    const double fs = 48000.0;
    const int n = 48000 * 4;
    std::vector<float> L(n), R(n);
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> noise(-1.f, 1.f);
    for (int i = 0; i < n; ++i) {
        const double t = i / fs, beat = std::fmod(t, 0.5);
        const float kick = (float)(std::sin(2 * 3.14159265358979323846 * (50 + 100 * std::exp(-beat * 30)) * beat) * std::exp(-beat * 8));
        const float hat = std::fmod(t, 0.25) < 0.02 ? noise(rng) * 0.2f : 0.f;
        const float pad = 0.15f * (float)std::sin(2 * 3.14159265358979323846 * 220 * t);
        L[i] = 0.6f * kick + hat + pad;
        R[i] = 0.6f * kick - hat * 0.5f + pad * 0.8f;
    }
    int fails = 0;
    auto eng = std::make_unique<Engine>();
    ParamStore p;
    for (int os = 0; os <= 4; ++os)
        for (int style = 0; style < 3; ++style) {
            p.set(kOversampling, (float)os);
            p.set(kClipStyle, (float)style);
            p.set(kClip, 60);
            p.set(kLimit, 60);
            p.set(kElevate, 70);
            p.set(kCharacter, (float)((os + style) % 4));
            eng->prepare(fs, 512);
            std::vector<float> oL(n), oR(n);
            const auto t0 = std::chrono::steady_clock::now();
            for (int pos = 0; pos < n; pos += 512) {
                const int len = std::min(512, n - pos);
                eng->process(&L[pos], &R[pos], &oL[pos], &oR[pos], len, p);
            }
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            float peak = 0;
            bool bad = false;
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(oL[i]) || !std::isfinite(oR[i])) bad = true;
                peak = std::max(peak, std::max(std::fabs(oL[i]), std::fabs(oR[i])));
            }
            std::printf("os %2dx style %d: peak %.3f, CPU %.2f%% of real time%s\n", 1 << os, style, peak,
                        100.0 * sec / (n / fs), bad ? "  NaN!" : "");
            if (bad || peak > 0.95f) ++fails;
        }
    // Bypass and dry: output must equal input once the crossfade is done.
    p.set(kBypass, 1);
    eng->prepare(fs, 512);
    std::vector<float> oL(n), oR(n);
    for (int pos = 0; pos < n; pos += 256) eng->process(&L[pos], &R[pos], &oL[pos], &oR[pos], 256, p);
    float err = 0;
    for (int i = 4800; i < n; ++i) err = std::max(err, std::fabs(oL[i] - L[i]));
    std::printf("bypass max error %.6f\n", err);
    if (err > 1e-5f) ++fails;
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails;
}
