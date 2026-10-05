// Parameter table shared by the DSP engine, the GUI and the CLAP / VST3 wrappers.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace tw {

enum ParamId : uint32_t {
    kInput = 0,
    kElevate,
    kComp,
    kPunch,
    kSub,
    kMix,
    kSpace,
    kClip,
    kLimit,
    kOutput,
    kOversampling,   // choice: 1x 2x 4x 8x 16x
    kCharacter,      // choice: tape, triode, transistor, wavefolder
    kClipStyle,      // choice: hard, soft, analog
    kAutoGain,       // bool
    kCameraBass,     // bool (GUI only, kept as a parameter so it is saved with the session)
    kBypass,         // bool
    kNumParams
};

struct ParamInfo {
    ParamId id;
    const char* key;     // stable identifier used in saved state
    const char* name;    // display name
    const char* unit;
    float min, max, def;
    int steps;           // 0 = continuous, otherwise number of discrete steps (max - min)
    bool isBypass;
};

const ParamInfo& paramInfo(uint32_t id);

// Display text for a plain value ("−2.5 dB", "4×", "Soft"...)
std::string formatParam(uint32_t id, float plain);
bool parseParam(uint32_t id, const char* text, float& plainOut);

inline float toNormalized(uint32_t id, float plain) {
    const auto& p = paramInfo(id);
    return (plain - p.min) / (p.max - p.min);
}
inline float fromNormalized(uint32_t id, float norm) {
    const auto& p = paramInfo(id);
    float v = p.min + norm * (p.max - p.min);
    if (p.steps > 0) v = (float)(int)(v + 0.5f);
    return v;
}

constexpr int kOversamplingFactors[5] = {1, 2, 4, 8, 16};
extern const char* const kCharacterNames[4];
extern const char* const kClipStyleNames[3];

// Lock-free parameter values (plain units). Written by the host thread or the GUI,
// read by the audio thread. Never blocks.
class ParamStore {
public:
    ParamStore();
    float get(uint32_t id) const { return values_[id].load(std::memory_order_relaxed); }
    void set(uint32_t id, float plain);
    // Bumped on every change, lets the GUI repaint without polling each value.
    uint32_t version() const { return version_.load(std::memory_order_relaxed); }

    std::string serialize() const;
    void deserialize(const std::string& text);

private:
    std::array<std::atomic<float>, kNumParams> values_;
    std::atomic<uint32_t> version_{0};
};

// Presets chosen by the audio recognition (or picked by hand).
enum SourceClass { kKick = 0, kBass, kVocal, kDrumBus, kGuitarSynth, kMaster, kNumClasses };
extern const char* const kClassNames[kNumClasses];

struct Preset {
    float input, elevate, comp, punch, sub, space, mix, clip, limit, output;
    int oversampling, character, clipStyle;
};
const Preset& presetFor(int sourceClass);

} // namespace tw
