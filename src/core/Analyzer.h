// Analysis worker thread. Consumes the input signal from the engine's lock-free FIFO and
// computes everything that is too heavy or too slow for the audio thread:
//  - auto gain staging target (3 s RMS, aims at -18 dBFS RMS)
//  - bass hit detection (20-150 Hz transients) for the waves and the camera
//  - short-term level
//  - source recognition on request (2 s of listening) for the presets
// Results go to the engine and the GUI through atomics and an SPSC event queue.
#pragma once

#include "Engine.h"
#include "SpscQueue.h"

#include <atomic>
#include <thread>

namespace tw {

struct BassHit {
    float strength = 0.f;  // 0..1
};

class Analyzer {
public:
    explicit Analyzer(Engine& engine);
    ~Analyzer();
    void start();
    void stop();
    void setSampleRate(double fs) { fs_.store((float)fs); }

    // GUI side
    SpscQueue<BassHit> bassHits{256};
    void requestRecognition() { recognizeRequest_.fetch_add(1); }
    bool isListening() const { return listening_.load(); }
    float listenProgress() const { return listenProgress_.load(); }
    // Bumped each time a recognition finishes; read class and confidence after it changes.
    uint32_t recognitionCount() const { return recognitionCount_.load(); }
    int recognizedClass() const { return recognizedClass_.load(); }
    float recognizedConfidence() const { return recognizedConfidence_.load(); }
    float inputRmsDb() const { return inputRmsDb_.load(); }
    bool hasSignal() const { return hasSignal_.load(); }

private:
    void run();

    Engine& engine_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<float> fs_{48000.f};

    std::atomic<uint32_t> recognizeRequest_{0};
    std::atomic<bool> listening_{false};
    std::atomic<float> listenProgress_{0.f};
    std::atomic<uint32_t> recognitionCount_{0};
    std::atomic<int> recognizedClass_{kDrumBus};
    std::atomic<float> recognizedConfidence_{0.f};
    std::atomic<float> inputRmsDb_{-100.f};
    std::atomic<bool> hasSignal_{false};
};

// Pure function, unit-testable: class probabilities from averaged band features.
void classifySource(float low, float mid, float high, float transient, float probs[kNumClasses]);

} // namespace tw
