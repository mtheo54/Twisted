#include "PluginCore.h"

#include <chrono>

namespace tw {

PluginCore::PluginCore() : engine_(std::make_unique<Engine>()), analyzer_(std::make_unique<Analyzer>(*engine_)) {
    // Vérification de licence immédiate, puis en fond (thread séparé, jamais l'audio).
    license.start();
}

PluginCore::~PluginCore() { analyzer_->stop(); }

void PluginCore::activate(double sampleRate, int maxBlock) {
    sampleRate_ = sampleRate;
    engine_->prepare(sampleRate, maxBlock);
    analyzer_->setSampleRate(sampleRate);
    analyzer_->start();
}

void PluginCore::deactivate() { analyzer_->stop(); }

void PluginCore::process(const float* inL, const float* inR, float* outL, float* outR, int n) {
    const auto t0 = std::chrono::steady_clock::now();
    engine_->process(inL, inR, outL, outR, n, params);
    const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double budget = n / sampleRate_;
    const float load = (float)(used / budget);
    cpuLoad.store(cpuLoad.load(std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
}

} // namespace tw
