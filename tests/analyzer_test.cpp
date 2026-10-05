// Feeds a 120 BPM kick pattern through the engine and checks that the analyzer
// reports bass hits, an auto gain target and finishes a recognition.
#include "../src/core/Analyzer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

using namespace tw;

int main() {
    auto eng = std::make_unique<Engine>();
    ParamStore p;
    eng->prepare(48000, 256);
    Analyzer an(*eng);
    an.setSampleRate(48000);
    an.start();
    an.requestRecognition();
    float L[256], R[256], oL[256], oR[256];
    int hits = 0;
    long t = 0;
    for (int blk = 0; blk < 48000 * 4 / 256; ++blk) {
        for (int i = 0; i < 256; ++i, ++t) {
            const double s = t / 48000.0, beat = std::fmod(s, 0.5);
            L[i] = R[i] = (float)(0.3 * std::sin(2 * 3.14159265358979323846 * (45 + 120 * std::exp(-beat * 30)) * beat) * std::exp(-beat * 7));
        }
        eng->process(L, R, oL, oR, 256, p);
        std::this_thread::sleep_for(std::chrono::microseconds(5333));  // real time
        BassHit h;
        while (an.bassHits.pop(h)) ++hits;
    }
    an.stop();
    std::printf("bass hits in 4 s at 120 BPM: %d (expected ~8)\n", hits);
    std::printf("input RMS %.1f dB -> auto trim %.1f dB\n", an.inputRmsDb(), eng->autoTrimDb.load());
    std::printf("recognitions %u, class %s (%.0f %%)\n", an.recognitionCount(), kClassNames[an.recognizedClass()],
                an.recognizedConfidence() * 100);
    const bool ok = hits >= 6 && hits <= 10 && an.recognitionCount() == 1;
    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
