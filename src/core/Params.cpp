#include "Params.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <sstream>

namespace tw {

namespace {
const ParamInfo kParams[kNumParams] = {
    {kInput,        "input",      "Input",        "dB", -24.f, 24.f,   0.f, 0, false},
    {kElevate,      "elevate",    "Elevate",      "%",    0.f, 100.f, 30.f, 0, false},
    {kComp,         "comp",       "Compression",  "%",    0.f, 100.f, 30.f, 0, false},
    {kPunch,        "punch",      "Punch",        "%",    0.f, 100.f, 40.f, 0, false},
    {kSub,          "sub",        "Sub",          "%",    0.f, 100.f, 35.f, 0, false},
    {kMix,          "mix",        "Dry/Wet",      "%",    0.f, 100.f, 80.f, 0, false},
    {kSpace,        "space",      "Space",        "%",    0.f, 100.f, 40.f, 0, false},
    {kClip,         "clip",       "Clipper",      "%",    0.f, 100.f, 20.f, 0, false},
    {kLimit,        "limit",      "Limiter",      "%",    0.f, 100.f, 40.f, 0, false},
    {kOutput,       "output",     "Output",       "dB", -24.f, 24.f,   0.f, 0, false},
    {kOversampling, "oversampling","Oversampling", "",    0.f, 4.f,    2.f, 4, false},
    {kCharacter,    "character",  "Character",    "",     0.f, 3.f,    0.f, 3, false},
    {kClipStyle,    "clipstyle",  "Clip Style",   "",     0.f, 2.f,    1.f, 2, false},
    {kAutoGain,     "autogain",   "Auto Gain",    "",     0.f, 1.f,    1.f, 1, false},
    {kCameraBass,   "camerabass", "Camera on Bass","",    0.f, 1.f,    1.f, 1, false},
    {kBypass,       "bypass",     "Bypass",       "",     0.f, 1.f,    0.f, 1, true},
};

const Preset kPresets[kNumClasses] = {
    // input elev comp punch sub space mix clip limit out   os char clipStyle
    {-1.5f, 30, 45, 75, 70, 15, 70,  40, 40, -0.5f, 2, 2, 0},  // Kick / 808
    {-2.0f, 28, 55, 35, 80, 10, 65,  20, 45,  0.0f, 2, 1, 1},  // Bass
    {-3.0f, 32, 50, 20, 10, 55, 55,   5, 35,  0.5f, 1, 1, 1},  // Vocal
    {-2.5f, 36, 40, 65, 45, 40, 80,  30, 40,  0.0f, 2, 0, 2},  // Drum bus
    {-1.0f, 32, 30, 30, 15, 65, 75,  10, 30,  0.0f, 3, 0, 2},  // Guitar / synth
    {-4.0f, 15, 25, 20, 30, 35, 100, 15, 70,  1.0f, 4, 0, 1},  // Master
};
} // namespace

const char* const kCharacterNames[4] = {"Tape", "Triode", "Transistor", "Wavefolder"};
const char* const kClipStyleNames[3] = {"Hard", "Soft", "Analog"};
const char* const kClassNames[kNumClasses] = {"Kick / 808", "Bass", "Vocal", "Drum Bus", "Guitar / Synth", "Master"};

const ParamInfo& paramInfo(uint32_t id) { return kParams[id < kNumParams ? id : 0]; }
const Preset& presetFor(int c) { return kPresets[(c >= 0 && c < kNumClasses) ? c : kDrumBus]; }

std::string formatParam(uint32_t id, float v) {
    char buf[64];
    switch (id) {
    case kInput:
    case kOutput:
        std::snprintf(buf, sizeof buf, "%+.1f dB", v);
        break;
    case kOversampling:
        std::snprintf(buf, sizeof buf, "%dx", kOversamplingFactors[v < 0 ? 0 : v > 4 ? 4 : (int)v]);
        break;
    case kCharacter:
        return kCharacterNames[(int)v & 3];
    case kClipStyle:
        return kClipStyleNames[(int)v > 2 ? 2 : (int)v];
    case kAutoGain:
    case kCameraBass:
    case kBypass:
        return v >= 0.5f ? "On" : "Off";
    default:
        std::snprintf(buf, sizeof buf, "%.0f %%", v);
        break;
    }
    return buf;
}

bool parseParam(uint32_t id, const char* text, float& out) {
    const auto& p = paramInfo(id);
    if (p.steps > 0) {
        for (int i = 0; i <= p.steps; ++i) {
            if (formatParam(id, (float)i) == text) { out = (float)i; return true; }
        }
    }
    char* end = nullptr;
    double v = std::strtod(text, &end);
    if (end == text) return false;
    out = (float)std::fmin(p.max, std::fmax(p.min, v));
    return true;
}

ParamStore::ParamStore() {
    for (uint32_t i = 0; i < kNumParams; ++i) values_[i].store(kParams[i].def);
}

void ParamStore::set(uint32_t id, float plain) {
    if (id >= kNumParams) return;
    const auto& p = kParams[id];
    plain = std::fmin(p.max, std::fmax(p.min, plain));
    if (p.steps > 0) plain = std::floor(plain + 0.5f);
    values_[id].store(plain, std::memory_order_relaxed);
    version_.fetch_add(1, std::memory_order_relaxed);
}

std::string ParamStore::serialize() const {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << "twisted-state 1\n";
    for (uint32_t i = 0; i < kNumParams; ++i) os << kParams[i].key << '=' << get(i) << '\n';
    return os.str();
}

void ParamStore::deserialize(const std::string& text) {
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        for (uint32_t i = 0; i < kNumParams; ++i)
            if (key == kParams[i].key) {
                std::istringstream vs(line.substr(eq + 1));
                vs.imbue(std::locale::classic());
                float v = 0.f;
                if (vs >> v) set(i, v);
            }
    }
}

} // namespace tw
