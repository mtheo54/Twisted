#include "core/Params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

namespace raw2 {
namespace {

const ParamDef kBase[kFxBase] = {
    {"aPos", "Osc A position", "%", 0.f, 1.f, 0.42f, 0},
    {"aLvl", "Osc A niveau", "dB", 0.f, 1.f, 0.78f, 0},
    {"aOct", "Osc A octave", "oct", -2.f, 2.f, 0.f, 4},
    {"aSt", "Osc A demi-tons", "st", -12.f, 12.f, 0.f, 24},
    {"bPos", "Osc B position", "%", 0.f, 1.f, 0.66f, 0},
    {"bLvl", "Osc B niveau", "dB", 0.f, 1.f, 0.53f, 0},
    {"bOct", "Osc B octave", "oct", -2.f, 2.f, 0.f, 4},
    {"bSt", "Osc B demi-tons", "st", -12.f, 12.f, 0.f, 24},
    {"speed", "Vie vitesse", "Hz", 0.f, 1.f, 0.39f, 0},
    {"birth", "Vie naissance", "%", 0.f, 1.f, 0.3f, 0},
    {"qty", "Vie quantité", "%", 0.f, 1.f, 0.6f, 0},
    {"m1", "Harmoniques", "%", -100.f, 100.f, 45.f, 0},
    {"m2", "Espace", "%", -100.f, 100.f, -20.f, 0},
    {"m3", "Texture", "%", 0.f, 100.f, 0.f, 0},
    {"master", "Master", "dB", 0.f, 1.f, 0.7f, 0},
    {"rule", "Vie règle", "", 0.f, 1.f, 0.f, 1},
    {"target", "Vie cible", "", 0.f, 2.f, 0.f, 2},
    {"freeze", "Vie gel", "", 0.f, 1.f, 0.f, 1},
    {"cutoff", "Filtre cutoff", "Hz", 0.f, 1.f, 0.82f, 0},
    {"reso", "Filtre réso", "%", 0.f, 1.f, 0.12f, 0},
    {"attack", "Env A attack", "ms", 0.f, 1.f, 0.1f, 0},
    {"decay", "Env A decay", "ms", 0.f, 1.f, 0.45f, 0},
    {"sustain", "Env A sustain", "%", 0.f, 1.f, 0.7f, 0},
    {"release", "Env A release", "ms", 0.f, 1.f, 0.4f, 0},
    {"aWarp", "Osc A warp", "%", 0.f, 1.f, 0.f, 0},
    {"aPan", "Osc A pan", "", 0.f, 1.f, 0.5f, 0},
    {"aMode", "Osc A mode", "", 0.f, 4.f, 0.f, 4},
    {"bWarp", "Osc B warp", "%", 0.f, 1.f, 0.f, 0},
    {"bPan", "Osc B pan", "", 0.f, 1.f, 0.5f, 0},
    {"bMode", "Osc B mode", "", 0.f, 4.f, 0.f, 4},
    {"smooth", "Lissage modulation", "ms", 0.f, 1.f, 0.1f, 0},
    {"pitchBend", "Molette pitch", "st", -1.f, 1.f, 0.f, 0},
    {"modWheel", "Molette mod", "%", 0.f, 1.f, 0.f, 0},
    {"fxWet", "FX dry/wet", "%", 0.f, 1.f, 1.f, 0},
    {"fxDistMode", "Distortion mode", "", 0.f, 3.f, 1.f, 3},
    {"fxOversample", "Distortion oversample", "", 0.f, 2.f, 1.f, 2},
    {"fxEqF0", "EQ fréquence 1", "Hz", 0.f, 1.f, 0.14f, 0},
    {"fxEqF1", "EQ fréquence 2", "Hz", 0.f, 1.f, 0.5f, 0},
    {"fxEqF2", "EQ fréquence 3", "Hz", 0.f, 1.f, 0.86f, 0},
};

// version 3 : enveloppes et LFO (page ENV / LFO), défauts de la maquette
const ParamDef kV3[kV3End - kEnvACurve] = {
    {"envACurve", "Env A courbe", "", 0.f, 1.f, 0.8f, 0},
    {"envAMode", "Env A mode", "", 0.f, 1.f, 0.f, 1},
    {"envBAttack", "Env B attack", "ms", 0.f, 1.f, 0.3f, 0},
    {"envBDecay", "Env B decay", "ms", 0.f, 1.f, 0.6f, 0},
    {"envBSustain", "Env B sustain", "%", 0.f, 1.f, 0.4f, 0},
    {"envBRelease", "Env B release", "ms", 0.f, 1.f, 0.5f, 0},
    {"envBCurve", "Env B courbe", "", 0.f, 1.f, 0.8f, 0},
    {"envBMode", "Env B mode", "", 0.f, 1.f, 0.f, 1},
    {"envLink", "Env lier A-B", "", 0.f, 1.f, 0.f, 1},
    {"lfo1Rate", "LFO 1 vitesse", "Hz", 0.f, 1.f, 0.4f, 0},
    {"lfo1Phase", "LFO 1 phase", "deg", 0.f, 1.f, 0.f, 0},
    {"lfo1Fade", "LFO 1 fade in", "ms", 0.f, 1.f, 0.f, 0},
    {"lfo1Smooth", "LFO 1 lissage", "%", 0.f, 1.f, 0.f, 0},
    {"lfo1Shape", "LFO 1 forme", "", 0.f, 4.f, 0.f, 4},
    {"lfo1Sync", "LFO 1 sync", "", 0.f, 5.f, 0.f, 5},
    {"lfo2Rate", "LFO 2 vitesse", "Hz", 0.f, 1.f, 0.25f, 0},
    {"lfo2Phase", "LFO 2 phase", "deg", 0.f, 1.f, 0.25f, 0},
    {"lfo2Fade", "LFO 2 fade in", "ms", 0.f, 1.f, 0.2f, 0},
    {"lfo2Smooth", "LFO 2 lissage", "%", 0.f, 1.f, 0.1f, 0},
    {"lfo2Shape", "LFO 2 forme", "", 0.f, 4.f, 1.f, 4},
    {"lfo2Sync", "LFO 2 sync", "", 0.f, 5.f, 0.f, 5},
};

const char* kFxKeys[kNumFx] = {"hyper", "dist", "flang", "phase", "chor", "dly", "rev", "comp", "eq", "filt", "vint", "pump"};
const char* kFxNames[kNumFx] = {"HYPER/DIM", "DISTORTION", "FLANGER", "PHASER", "CHORUS", "DELAY", "REVERB", "COMPRESSOR", "EQ", "FILTER", "VINTAGE", "PUMP"};
const char* kFxKnobs[kNumFx][3] = {{"RATE", "DETUNE", "UNISON"}, {"DRIVE", "TONE", "BIAS"}, {"RATE", "DEPTH", "FEEDBK"}, {"RATE", "DEPTH", "FREQ"},
                                    {"RATE", "DELAY", "DEPTH"}, {"TIME", "FEEDBK", "FILTER"}, {"SIZE", "DAMP", "PR\xC3\x89"}, {"THRESH", "RATIO", "GAIN"},
                                    {"BAS", "M\xC3\x89" "D", "AIGU"}, {"CUTOFF", "RESO", "DRIVE"}, {"USURE", "SOUFFLE", "LO-FI"},
                                    {"PROFOND.", "RYTHME", "COURBE"}};

// Table complète : paramètres de base puis 7 paramètres par effet (noms construits une fois).
struct Table {
    std::vector<ParamDef> defs;
    std::deque<std::string> text;  // stockage stable des clés et des noms construits
    Table() {
        defs.assign(kBase, kBase + kFxBase);
        for (int f = 0; f < kNumFxV1; ++f) addFx(f);
        defs.insert(defs.end(), kV3, kV3 + (kV3End - kEnvACurve));
        // version 4
        defs.push_back({"lfo1Draw", "LFO 1 dessin", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"lfo2Draw", "LFO 2 dessin", "", 0.f, 1.f, 0.f, 1});
        static const char* fk[kLfFieldsPer] = {"Rate", "Phase", "Fade", "Smooth", "Shape", "Sync", "Draw", "On"};
        static const char* fn[kLfFieldsPer] = {"vitesse", "phase", "fade in", "lissage", "forme", "sync", "dessin", "actif"};
        for (int r = 0; r < kNumRackLfos; ++r)
            for (int k = 0; k < kLfFieldsPer; ++k) {
                text.push_back("rk" + std::to_string(r + 1) + fk[k]);
                const char* key = text.back().c_str();
                text.push_back("LFO R" + std::to_string(r + 1) + " " + fn[k]);
                const char* name = text.back().c_str();
                switch (k) {
                    case kLfRate: defs.push_back({key, name, "Hz", 0.f, 1.f, 0.3f, 0}); break;
                    case kLfPhase: defs.push_back({key, name, "deg", 0.f, 1.f, 0.f, 0}); break;
                    case kLfFade: defs.push_back({key, name, "ms", 0.f, 1.f, 0.f, 0}); break;
                    case kLfSmooth: defs.push_back({key, name, "%", 0.f, 1.f, 0.f, 0}); break;
                    case kLfShape: defs.push_back({key, name, "", 0.f, 4.f, 0.f, 4}); break;
                    case kLfSync: defs.push_back({key, name, "", 0.f, 5.f, 0.f, 5}); break;
                    case kLfDraw: defs.push_back({key, name, "", 0.f, 1.f, 0.f, 1}); break;
                    default: defs.push_back({key, name, "", 0.f, 1.f, r == 0 ? 1.f : 0.f, 1}); break;  // R1 présent par défaut
                }
            }
        defs.push_back({"aPlay", "Osc A lecture", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"aSmpMode", "Osc A sample mode", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"aSmpRoot", "Osc A sample root", "", 24.f, 96.f, 48.f, 72});
        defs.push_back({"bPlay", "Osc B lecture", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"bSmpMode", "Osc B sample mode", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"bSmpRoot", "Osc B sample root", "", 24.f, 96.f, 48.f, 72});
        // version 5 : page JEU
        defs.push_back({"polyMode", "Mode de jeu", "", 0.f, 2.f, 0.f, 2});
        defs.push_back({"glide", "Glide", "ms", 0.f, 1.f, 0.f, 0});
        defs.push_back({"glideMode", "Glide mode", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"unison", "Unison", "", 1.f, 8.f, 1.f, 7});
        defs.push_back({"uniDetune", "Unison d\xC3\xA9" "saccord", "%", 0.f, 1.f, 0.3f, 0});
        defs.push_back({"uniWidth", "Unison largeur", "%", 0.f, 1.f, 0.7f, 0});
        defs.push_back({"punch", "808 punch", "ST", 0.f, 1.f, 0.f, 0});
        defs.push_back({"punchTime", "808 dur\xC3\xA9" "e", "ms", 0.f, 1.f, 0.3f, 0});
        defs.push_back({"subLvl", "Sub niveau", "%", 0.f, 1.f, 0.f, 0});
        defs.push_back({"subOct", "Sub octave", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"filtSlope", "Filtre pente", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"filtDrive", "Filtre drive", "%", 0.f, 1.f, 0.f, 0});
        defs.push_back({"blend", "Blend A/B", "%", 0.f, 1.f, 0.5f, 0});
        defs.push_back({"velAmt", "V\xC3\xA9locit\xC3\xA9 niveau", "%", 0.f, 1.f, 0.65f, 0});
        defs.push_back({"velCut", "V\xC3\xA9locit\xC3\xA9 filtre", "%", 0.f, 1.f, 0.f, 0});
        defs.push_back({"macro1", "Macro 1", "%", 0.f, 1.f, 0.f, 0});
        defs.push_back({"macro2", "Macro 2", "%", 0.f, 1.f, 0.f, 0});
        defs.push_back({"macro3", "Macro 3", "%", 0.f, 1.f, 0.f, 0});
        defs.push_back({"macro4", "Macro 4", "%", 0.f, 1.f, 0.f, 0});
        defs.push_back({"arpOn", "Arp", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"arpMode", "Arp mode", "", 0.f, 4.f, 0.f, 4});
        defs.push_back({"arpRate", "Arp vitesse", "", 0.f, 5.f, 3.f, 5});
        defs.push_back({"arpOct", "Arp octaves", "", 1.f, 4.f, 1.f, 3});
        defs.push_back({"arpGate", "Arp gate", "%", 0.f, 1.f, 0.6f, 0});
        defs.push_back({"arpSwing", "Arp swing", "%", 0.f, 1.f, 0.f, 0});
        defs.push_back({"arpLatch", "Arp latch", "", 0.f, 1.f, 0.f, 1});
        defs.push_back({"chord", "Accord", "", 0.f, (float)(kNumChords - 1), 0.f, kNumChords - 1});
        defs.push_back({"chordStrum", "Accord strum", "ms", 0.f, 1.f, 0.f, 0});
        for (int f = kNumFxV1; f < kNumFx; ++f) addFx(f);
    }
    void addFx(int f) {
        static const char* what[kFxParamsPer] = {"on", "p0", "p1", "p2", "mix", "modDepth", "modSrc"};
        for (int k = 0; k < kFxParamsPer; ++k) {
            text.push_back(std::string("fx_") + kFxKeys[f] + "_" + what[k]);
            const char* key = text.back().c_str();
            std::string nm = std::string(kFxNames[f]) + " ";
            if (k == kFxOn) nm += "ON";
            else if (k <= kFxP2) nm += kFxKnobs[f][k - kFxP0];
            else if (k == kFxMix) nm += "MIX";
            else if (k == kFxModDepth) nm += "MOD";
            else nm += "MOD SOURCE";
            text.push_back(nm);
            const char* name = text.back().c_str();
            switch (k) {
                case kFxOn: defs.push_back({key, name, "", 0.f, 1.f, 0.f, 1}); break;
                case kFxMix: defs.push_back({key, name, "%", 0.f, 1.f, 0.3f, 0}); break;
                case kFxModDepth: defs.push_back({key, name, "%", -100.f, 100.f, 0.f, 0}); break;
                case kFxModSrc: defs.push_back({key, name, "", 0.f, 5.f, 0.f, 5}); break;
                default: defs.push_back({key, name, "", 0.f, 1.f, 0.5f, 0}); break;
            }
        }
    }
};
const Table& table() {
    static const Table t;
    return t;
}


std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

std::string timeText(float s) {
    return s < 1.f ? fmt("%.0f ms", s * 1000.0) : fmt("%.2f s", s);
}

} // namespace

const ParamDef& paramDef(uint32_t id) { return table().defs[id < kNumParams ? id : 0]; }

float clampParam(uint32_t id, float v) {
    const ParamDef& d = paramDef(id);
    if (!(v == v)) v = d.def;  // NaN
    v = v < d.min ? d.min : (v > d.max ? d.max : v);
    if (d.steps > 0) v = std::round(v);
    return v;
}

float toNormalized(uint32_t id, float plain) {
    const ParamDef& d = paramDef(id);
    return (clampParam(id, plain) - d.min) / (d.max - d.min);
}

float fromNormalized(uint32_t id, float n) {
    const ParamDef& d = paramDef(id);
    n = n < 0.f ? 0.f : (n > 1.f ? 1.f : n);
    return clampParam(id, d.min + n * (d.max - d.min));
}

float levelToDb(float v) { return -(1.f - v) * 18.2f; }
float levelToGain(float v) { return v <= 0.0005f ? 0.f : std::pow(10.f, levelToDb(v) / 20.f); }
std::string levelText(float v) {
    if (v <= 0.0005f) return "-\xE2\x88\x9E dB";  // -∞ dB
    const float db = (1.f - v) * 18.2f;
    return db < 0.05f ? std::string("0.0 dB") : fmt("-%.1f dB", db);
}
float speedToHz(float v) { return 0.5f * std::pow(40.f, v); }
float cutoffToHz(float v) { return 20.f * std::pow(1000.f, v); }
float attackSeconds(float v) { return 0.001f * std::pow(5000.f, v); }     // 1 ms .. 5 s
float decaySeconds(float v) { return 0.005f * std::pow(1000.f, v); }      // 5 ms .. 5 s
float releaseSeconds(float v) { return 0.005f * std::pow(2000.f, v); }    // 5 ms .. 10 s
float lfoRateHz(float v) { return 0.05f * std::pow(400.f, v); }           // 0.05 .. 20 Hz
float lfoFadeSeconds(float v) { return 4.f * v; }
float glideSeconds(float v) { return 1.5f * v * v; }
float punchSeconds(float v) { return 0.005f * std::pow(80.f, v); }   // 5 ms .. 400 ms
const char* chordName(int c) {
    static const char* n[kNumChords] = {"OFF", "MAJ", "MIN", "7", "MAJ7", "MIN7", "SUS2", "SUS4", "POWER", "MIN9", "ADD9"};
    return n[c < 0 || c >= kNumChords ? 0 : c];
}
const int8_t* chordIntervals(int c) {
    static const int8_t iv[kNumChords][6] = {{0, -1, -1, -1, -1, -1}, {0, 4, 7, -1, -1, -1},  {0, 3, 7, -1, -1, -1},  {0, 4, 7, 10, -1, -1},
                                             {0, 4, 7, 11, -1, -1},   {0, 3, 7, 10, -1, -1},  {0, 2, 7, -1, -1, -1},  {0, 5, 7, -1, -1, -1},
                                             {0, 7, 12, -1, -1, -1},  {0, 3, 7, 10, 14, -1},  {0, 4, 7, 14, -1, -1}};
    return iv[c < 0 || c >= kNumChords ? 0 : c];
}
const char* arpModeName(int m) {
    static const char* n[5] = {"HAUT", "BAS", "HAUT/BAS", "HASARD", "JOU\xC3\x89"};
    return n[m < 0 || m > 4 ? 0 : m];
}
const char* arpRateName(int r) {
    static const char* n[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
    return n[r < 0 || r > 5 ? 0 : r];
}
const char* routeSrcName(int s) {
    static const char* n[kNumRouteSrcs] = {"LFO 1", "LFO 2", "LFO R1", "LFO R2", "LFO R3", "LFO R4", "LFO R5", "LFO R6",
                                            "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "V\xC3\x89LOCIT\xC3\x89", "MOLETTE"};
    return n[s < 0 || s >= kNumRouteSrcs ? 0 : s];
}
const char* lfoDisplayName(int lfo) {
    static const char* n[kNumLfos] = {"LFO 1", "LFO 2", "LFO R1", "LFO R2", "LFO R3", "LFO R4", "LFO R5", "LFO R6"};
    return n[lfo < 0 || lfo >= kNumLfos ? 0 : lfo];
}
bool isModulatable(uint32_t id) {
    if (id >= kNumParams) return false;
    const ParamDef& d = paramDef(id);
    if (d.steps > 0) return false;  // boutons à crans (modes, formes, sync…) : pas de route
    if (id >= kMacro1 && id <= kMacro4) return false;  // une macro est une source, pas une destination
    return id != kPitchBend && id != kModWheel;
}
const char* lfoShapeName(int s) {
    static const char* n[kNumLfoShapes] = {"SINE", "TRI", "SAW", "SQR", "S&H"};
    return n[s < 0 || s >= kNumLfoShapes ? 0 : s];
}
const char* lfoSyncName(int s) {
    static const char* n[6] = {"OFF", "1/1", "1/2", "1/4", "1/8", "1/16"};
    return n[s < 0 || s > 5 ? 0 : s];
}
float lfoSyncFactor(int s) {
    static const float f[6] = {0.f, 0.5f, 1.f, 2.f, 4.f, 8.f};  // Hz à 120 BPM
    return f[s < 0 || s > 5 ? 0 : s];
}
const char* envCurveName(float c) {
    if (std::fabs(c - 0.5f) < 0.04f) return "LIN";
    if (std::fabs(c - 0.8f) < 0.04f) return "EXP";
    if (std::fabs(c - 0.2f) < 0.04f) return "LOG";
    if (std::fabs(c - 1.f) < 0.04f) return "EXTR";
    return "CUSTOM";
}
float envCurveQ(float c) { return std::pow(4.f, (c - 0.5f) * 2.f); }

const char* fxKey(int f) { return kFxKeys[f < 0 || f >= kNumFx ? 0 : f]; }
const char* fxName(int f) { return kFxNames[f < 0 || f >= kNumFx ? 0 : f]; }
const char* fxKnobName(int f, int k) { return kFxKnobs[f < 0 || f >= kNumFx ? 0 : f][k < 0 || k > 2 ? 0 : k]; }
const char* modSourceName(int s) {
    static const char* n[6] = {"VIE", "LFO 1", "LFO 2", "ENV", "CLAVIER", "AL\xC3\x89" "A"};
    return n[s < 0 || s > 5 ? 0 : s];
}
const char* oscModeName(int m) {
    static const char* n[5] = {"SYNC : 0\xC2\xB0", "SYNC : 90\xC2\xB0", "SYNC : 180\xC2\xB0", "LOOP \xE2\x96\xBE", "FREE \xE2\x96\xBE"};
    return n[m < 0 || m > 4 ? 0 : m];
}
std::string panText(float v) {
    const int d = (int)std::lround((v - 0.5f) * 100.f);
    return d == 0 ? std::string("C") : (d < 0 ? fmt("L %.0f", -d) : fmt("R %.0f", d));
}
const char* pumpRateName(float v) {
    static const char* n[4] = {"1/2", "1/4", "1/8", "1/16"};
    return n[std::clamp((int)(v * 4.f), 0, 3)];
}
namespace {
std::string hzText(float f) {
    const float h = cutoffToHz(f);
    return h >= 1000.f ? fmt("%.1f kHz", h / 1000.0) : fmt("%.0f Hz", std::round(h));
}
std::string pctText(float f) { return fmt("%.0f %%", std::round(f * 100.f)); }
} // namespace

// Textes des boutons d'effet (mêmes formats que la maquette).
std::string fxValueText(int fx, int k, float f) {
    switch (fx) {
        case 0: return k == 0 ? fmt("%.1f Hz", 0.3 + f * 6.0) : (k == 1 ? pctText(f) : fmt("%.0f V", std::round(2.0 + f * 5.0)));
        case 2: return k == 0 ? fmt("%.1f Hz", 0.4 + f * 2.0) : pctText(f);
        case 3: return k == 0 ? fmt("%.1f Hz", 0.3 + f * 2.0) : pctText(f);
        case 4: return k == 0 ? fmt("%.1f Hz", 0.2 + f * 2.0) : pctText(f);
        case 5: return k == 0 ? fmt("%.0f ms", std::round(20.0 + f * 980.0)) : pctText(f);
        case 6: return k == 0 ? fmt("%.1f s", 0.3 + f * 7.0) : (k == 1 ? pctText(f) : fmt("%.0f ms", std::round(f * 100.0)));
        case 7: return k == 0 ? fmt("-%.0f dB", std::round(f * 40.0)) : (k == 1 ? fmt("%.1f:1", 1.0 + f * 19.0) : fmt("+%.1f dB", f * 12.0));
        case 8: return fmt("%.1f dB", (f - 0.5) * 24.0);
        case 9: return k == 0 ? hzText(f) : pctText(f);
        case 11: return k == 1 ? std::string(pumpRateName(f)) : pctText(f);
        default: return pctText(f);
    }
}

std::string formatParam(uint32_t id, float v) {
    v = clampParam(id, v);
    int fk = 0;
    if (const int f = fxOfParam(id, &fk); f >= 0) {  // réglages des effets seulement
        const int k = fk;
        switch (k) {
            case kFxOn: return v > 0.5f ? "ON" : "OFF";
            case kFxMix: return pctText(v);
            case kFxModDepth: return (v > 0.5f ? "+" : "") + fmt("%.0f %%", std::round(v));
            case kFxModSrc: return modSourceName((int)v);
            default: return fxValueText(f, k - kFxP0, v);
        }
    }
    if (id >= kRkBase && id < kAPlay) {
        switch ((int)(id - kRkBase) % kLfFieldsPer) {
            case kLfRate: return fmt("%.2f Hz", lfoRateHz(v));
            case kLfPhase: return fmt("%.0f\xC2\xB0", std::round(v * 360.0));
            case kLfFade: return timeText(std::max(0.001f, lfoFadeSeconds(v)));
            case kLfSmooth: return pctText(v);
            case kLfShape: return lfoShapeName((int)v);
            case kLfSync: return lfoSyncName((int)v);
            default: return v > 0.5f ? "ON" : "OFF";
        }
    }
    switch (id) {
        case kLfo1Draw: case kLfo2Draw: return v > 0.5f ? "DRAW" : "FORME";
        case kPolyMode: return (int)v == 0 ? "POLY" : ((int)v == 1 ? "MONO" : "LEGATO");
        case kGlide: return v < 0.001f ? std::string("OFF") : timeText(glideSeconds(v));
        case kGlideMode: return v > 0.5f ? "LI\xC3\x89" "ES" : "TOUJOURS";
        case kUnison: return (int)v <= 1 ? std::string("OFF") : fmt("%.0f VOIX", v);
        case kUniDetune: return fmt("%.0f ct", std::round(v * 100.0));
        case kPunch: return fmt("+%.0f ST", std::round(v * 36.0));
        case kPunchTime: return timeText(punchSeconds(v));
        case kSubOct: return v > 0.5f ? "-1 OCT" : "0 OCT";
        case kFiltSlope: return v > 0.5f ? "24 dB" : "12 dB";
        case kBlend: {
            const int d = (int)std::lround((v - 0.5f) * 200.f);
            return d == 0 ? std::string("A = B") : (d < 0 ? fmt("A %.0f", -d) : fmt("B %.0f", d));
        }
        case kUniWidth: case kSubLvl: case kFiltDrive: case kVelAmt: case kVelCut: case kMacro1: case kMacro2: case kMacro3: case kMacro4:
        case kArpGate: case kArpSwing: return pctText(v);
        case kArpOn: case kArpLatch: return v > 0.5f ? "ON" : "OFF";
        case kArpMode: return arpModeName((int)v);
        case kArpRate: return arpRateName((int)v);
        case kArpOct: return fmt("%.0f OCT", v);
        case kChord: return chordName((int)v);
        case kChordStrum: return fmt("%.0f ms", std::round(v * 80.0));
        case kAPlay: case kBPlay: return v > 0.5f ? "SAMPLE" : "WAVEFORM";
        case kASmpMode: case kBSmpMode: return v > 0.5f ? "RESAMPLE" : "STRETCH";
        case kASmpRoot: case kBSmpRoot: {
            static const char* nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            const int n = (int)v;
            return std::string(nn[n % 12]) + std::to_string(n / 12 - 1);
        }
        case kAWarp: case kBWarp: case kModWheel: case kFxWet: return pctText(v);
        case kAPan: case kBPan: return panText(v);
        case kAMode: case kBMode: return oscModeName((int)v);
        case kSmooth: return fmt("%.0f ms", std::round(v * 500.f));
        case kPitchBend: return (v >= 0 ? "+" : "") + fmt("%.1f ST", v * 2.0);
        case kFxDistMode: { static const char* m[4] = {"TAPE", "TUBE", "FOLD", "HARD CLIP"}; return m[(int)v]; }
        case kFxOversample: return (int)v == 0 ? "1x" : ((int)v == 1 ? "2x" : "4x");
        case kFxEqF0: case kFxEqF1: case kFxEqF2: return hzText(v);
        case kAPos: case kBPos: case kBirth: case kQty: case kReso: case kSustain:
            return fmt("%.0f %%", v * 100.0);
        case kALvl: case kBLvl: case kMaster: return levelText(v);
        case kAOct: case kBOct: return (v > 0 ? "+" : "") + fmt("%.0f OCT", v);
        case kASt: case kBSt: return (v > 0 ? "+" : "") + fmt("%.0f ST", v);
        case kSpeed: return fmt("%.1f Hz", speedToHz(v));
        case kM1: case kM2: case kM3: return (v > 0.5f ? "+" : "") + fmt("%.0f %%", std::round(v));
        case kRule: return v > 0.5f ? "B36/S23" : "B3/S23";
        case kTarget: return v < 0.5f ? "FILTRE" : (v < 1.5f ? "POSITION" : "NIVEAU");
        case kFreeze: return v > 0.5f ? "GEL" : "VIE";
        case kCutoff: {
            const float hz = cutoffToHz(v);
            return hz < 1000.f ? fmt("%.0f Hz", hz) : fmt("%.1f kHz", hz / 1000.0);
        }
        case kAttack: case kEnvBAttack: return timeText(attackSeconds(v));
        case kDecay: case kEnvBDecay: return timeText(decaySeconds(v));
        case kRelease: case kEnvBRelease: return timeText(releaseSeconds(v));
        case kEnvBSustain: return fmt("%.0f %%", v * 100.0);
        case kEnvACurve: case kEnvBCurve: return envCurveName(v);
        case kEnvAMode: case kEnvBMode: return v > 0.5f ? "FREE" : "RETRIG";
        case kEnvLink: return v > 0.5f ? "LI\xC3\x89" : "S\xC3\x89PAR\xC3\x89";
        case kLfo1Rate: case kLfo2Rate: return fmt("%.2f Hz", lfoRateHz(v));
        case kLfo1Phase: case kLfo2Phase: return fmt("%.0f\xC2\xB0", std::round(v * 360.0));
        case kLfo1Fade: case kLfo2Fade: return timeText(std::max(0.001f, lfoFadeSeconds(v)));
        case kLfo1Smooth: case kLfo2Smooth: return fmt("%.0f %%", v * 100.0);
        case kLfo1Shape: case kLfo2Shape: return lfoShapeName((int)v);
        case kLfo1Sync: case kLfo2Sync: return lfoSyncName((int)v);
        default: return fmt("%.2f", v);
    }
}

bool parseParam(uint32_t id, const char* text, float& out) {
    if (!text) return false;
    if (id == kRule) { out = std::strstr(text, "36") ? 1.f : 0.f; return true; }
    if (id == kTarget) {
        if (std::strstr(text, "FIL") || std::strstr(text, "fil")) { out = 0.f; return true; }
        if (std::strstr(text, "POS") || std::strstr(text, "pos")) { out = 1.f; return true; }
        if (std::strstr(text, "NIV") || std::strstr(text, "niv")) { out = 2.f; return true; }
    }
    char* end = nullptr;
    const double x = std::strtod(text, &end);
    if (end == text) return false;
    switch (id) {
        case kAPos: case kBPos: case kBirth: case kQty: case kReso: case kSustain: case kEnvBSustain: case kLfo1Smooth: case kLfo2Smooth: out = (float)(x / 100.0); break;
        case kASmpRoot: case kBSmpRoot: out = (float)x; break;
        case kALvl: case kBLvl: case kMaster: out = (float)(1.0 + (x > 0 ? -x : x) / 18.2); break;
        case kSpeed: out = x > 0 ? (float)(std::log(x / 0.5) / std::log(40.0)) : 0.f; break;
        case kCutoff: {
            double hz = x;
            if (std::strstr(text, "k")) hz *= 1000.0;
            out = hz > 20 ? (float)(std::log(hz / 20.0) / std::log(1000.0)) : 0.f;
            break;
        }
        default: out = (float)x; break;
    }
    out = clampParam(id, out);
    return true;
}

int paramIdFromKey(const char* key) {
    for (uint32_t i = 0; i < kNumParams; ++i)
        if (std::strcmp(table().defs[i].key, key) == 0) return (int)i;
    return -1;
}

ParamStore::ParamStore() { resetDefaults(); }
void ParamStore::resetDefaults() {
    for (uint32_t i = 0; i < kNumParams; ++i) values_[i].store(table().defs[i].def, std::memory_order_relaxed);
}

} // namespace raw2
