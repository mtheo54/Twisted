// Everything the CLAP and VST3 wrappers share: parameters, engine, analyzer, state.
#pragma once

#include "Analyzer.h"
#include "Engine.h"
#include "Params.h"

#include <atomic>
#include <memory>
#include <string>

namespace tw {

// Implemented by each wrapper: how a GUI gesture reaches the host (automation, undo).
// Called on the GUI thread only.
class ParamHost {
public:
    virtual ~ParamHost() = default;
    virtual void beginEdit(uint32_t id) = 0;
    virtual void performEdit(uint32_t id, float plain) = 0;
    virtual void endEdit(uint32_t id) = 0;
};

class PluginCore {
public:
    PluginCore();
    ~PluginCore();

    void activate(double sampleRate, int maxBlock);  // main thread
    void deactivate();                               // main thread
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);  // audio thread

    ParamStore params;
    Engine& engine() { return *engine_; }
    Analyzer& analyzer() { return *analyzer_; }

    std::string saveState() const { return params.serialize(); }
    void loadState(const std::string& s) { params.deserialize(s); }

    // Share of the real-time budget used by the last process call (0..1).
    std::atomic<float> cpuLoad{0.f};

private:
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<Analyzer> analyzer_;
    double sampleRate_ = 48000.0;
};

} // namespace tw
