#pragma once
// Liste centrale des paramètres (§8.2 du brief) : identifiant stable, plage, défaut, texte.
// La même liste sert au VST3 (automation), aux presets .raw et à l'interface.
#include <atomic>
#include <cstdint>
#include <string>

namespace raw2 {

// Ne jamais renuméroter : les projets des hôtes mémorisent ces identifiants.
enum ParamId : uint32_t {
    kAPos = 0, kALvl, kAOct, kASt,
    kBPos, kBLvl, kBOct, kBSt,
    kSpeed, kBirth, kQty,
    kM1, kM2, kM3,
    kMaster,
    kRule,     // 0 = B3/S23 (Conway), 1 = B36/S23 (HighLife)
    kTarget,   // cible de la vie : 0 FILTRE, 1 POSITION, 2 NIVEAU
    kFreeze,   // GEL
    kCutoff, kReso,
    kAttack, kDecay, kSustain, kRelease,
    // --- version 2 (maquette mise à jour) : toujours ajouter à la fin ---
    kAWarp, kAPan, kAMode,     // mode : 0 SYNC 0°, 1 SYNC 90°, 2 SYNC 180°, 3 LOOP, 4 FREE
    kBWarp, kBPan, kBMode,
    kSmooth,                   // lissage des sources de modulation, 0..1 -> 0..500 ms
    kPitchBend, kModWheel,     // molettes (MIDI : pitch bend, CC 1)
    kFxWet,                    // DRY / WET global des effets
    kFxDistMode,               // 0 Tape, 1 Tube, 2 Fold, 3 Hard Clip
    kFxOversample,             // 0 1x, 1 2x, 2 4x
    kFxEqF0, kFxEqF1, kFxEqF2, // fréquences des 3 bandes de l'EQ
    kFxBase                    // puis kNumFx blocs de kFxParamsPer paramètres (voir fxParam)
};

// Effets (page EFX), dans l'ordre de la maquette.
// VINTAGE et PUMP (version 5) ont leurs réglages à la fin de la liste (kFx2Base) : les numéros des
// paramètres déjà publiés ne bougent pas.
enum FxId { kFxHyper = 0, kFxDist, kFxFlang, kFxPhase, kFxChor, kFxDly, kFxRev, kFxComp, kFxEq, kFxFilt, kFxVintage, kFxPump, kNumFx };
constexpr int kNumFxV1 = 10;  // effets de la version 2, réglages contigus à partir de kFxBase
enum FxParam { kFxOn = 0, kFxP0, kFxP1, kFxP2, kFxMix, kFxModDepth, kFxModSrc, kFxParamsPer };

// --- version 3 (page ENV / LFO) : à la suite des effets, toujours ajouter à la fin ---
// L'enveloppe de l'OSC A reste kAttack / kDecay / kSustain / kRelease.
enum ParamIdV3 : uint32_t {
    kEnvACurve = (uint32_t)kFxBase + (uint32_t)(kNumFxV1 * (int)kFxParamsPer),
    kEnvAMode,                 // 0 RETRIG, 1 FREE
    kEnvBAttack, kEnvBDecay, kEnvBSustain, kEnvBRelease, kEnvBCurve, kEnvBMode,
    kEnvLink,                  // LIER A<->B
    kLfo1Rate, kLfo1Phase, kLfo1Fade, kLfo1Smooth, kLfo1Shape, kLfo1Sync,
    kLfo2Rate, kLfo2Phase, kLfo2Fade, kLfo2Smooth, kLfo2Shape, kLfo2Sync,
    kV3End
};

// --- version 4 (rack modulaire, formes dessinées, lecture SAMPLE) : toujours ajouter à la fin ---
constexpr int kNumRackLfos = 6;   // LFO R1..R6 (MODULES, SIDE B)
constexpr int kNumLfos = 2 + kNumRackLfos;
enum LfoField { kLfRate = 0, kLfPhase, kLfFade, kLfSmooth, kLfShape, kLfSync, kLfDraw, kLfOn, kLfFieldsPer };
enum ParamIdV4 : uint32_t {
    kLfo1Draw = (uint32_t)kV3End, kLfo2Draw,   // ÉDITION / DRAW : la forme dessinée remplace FORME
    kRkBase,                                   // puis kNumRackLfos blocs de kLfFieldsPer (voir lfoP)
    kAPlay = kRkBase + (uint32_t)(kNumRackLfos * (int)kLfFieldsPer),  // 0 WAVEFORM, 1 SAMPLE
    kASmpMode,                                 // 0 STRETCH, 1 RESAMPLE
    kASmpRoot,                                 // note MIDI d'origine du son déposé
    kBPlay, kBSmpMode, kBSmpRoot,
    kV4End
};

// --- version 5 (page JEU, VINTAGE, PUMP) : toujours ajouter à la fin ---
enum ParamIdV5 : uint32_t {
    kPolyMode = (uint32_t)kV4End,  // 0 POLY, 1 MONO, 2 LEGATO
    kGlide,                        // 0..1 -> 0..1,5 s
    kGlideMode,                    // 0 TOUJOURS, 1 NOTES LIÉES
    kUnison,                       // 1..8 voix par note
    kUniDetune,                    // 0..1 -> écart total 0..100 cents
    kUniWidth,                     // 0..1 étalement stéréo des voix d'unison
    kPunch,                        // 808 : chute de hauteur à l'attaque, 0..1 -> 0..+36 demi-tons
    kPunchTime,                    // 0..1 -> 5..400 ms
    kSubLvl,                       // sous-oscillateur sinus
    kSubOct,                       // 0 : même octave, 1 : une octave plus bas
    kFiltSlope,                    // 0 : 12 dB/oct, 1 : 24 dB/oct
    kFiltDrive,                    // saturation dans la boucle du filtre
    kBlend,                        // fondu OSC A <-> OSC B (0,5 : les deux pleins)
    kVelAmt,                       // vélocité -> niveau
    kVelCut,                       // vélocité -> filtre
    kMacro1, kMacro2, kMacro3, kMacro4,
    kArpOn, kArpMode,              // mode : 0 HAUT, 1 BAS, 2 HAUT/BAS, 3 HASARD, 4 JOUÉ
    kArpRate,                      // 0 1/4, 1 1/8, 2 1/8T, 3 1/16, 4 1/16T, 5 1/32
    kArpOct,                       // 1..4 octaves
    kArpGate, kArpSwing, kArpLatch,
    kChord,                        // 0 OFF, puis types d'accords (chordName)
    kChordStrum,                   // 0..1 -> 0..80 ms entre les notes de l'accord
    kFx2Base,                      // puis (kNumFx - kNumFxV1) blocs de kFxParamsPer (VINTAGE, PUMP)
    kV5End = kFx2Base + (uint32_t)((kNumFx - kNumFxV1) * (int)kFxParamsPer)
};
constexpr uint32_t kNumParams = (uint32_t)kV5End;
constexpr uint32_t fxParam(int fx, int what) {
    return fx < kNumFxV1 ? (uint32_t)kFxBase + (uint32_t)(fx * (int)kFxParamsPer + what)
                         : (uint32_t)kFx2Base + (uint32_t)((fx - kNumFxV1) * (int)kFxParamsPer + what);
}
// effet (ou -1) et réglage d'un identifiant de paramètre d'effet
inline int fxOfParam(uint32_t id, int* what = nullptr) {
    int f = -1, k = 0;
    if (id >= (uint32_t)kFxBase && id < (uint32_t)kEnvACurve) { f = (int)(id - kFxBase) / kFxParamsPer; k = (int)(id - kFxBase) % kFxParamsPer; }
    else if (id >= (uint32_t)kFx2Base && id < (uint32_t)kV5End) { f = kNumFxV1 + (int)(id - kFx2Base) / kFxParamsPer; k = (int)(id - kFx2Base) % kFxParamsPer; }
    if (what) *what = k;
    return f;
}
constexpr int kNumMacros = 4;
constexpr int kNumChords = 11;  // OFF + 10 accords
const char* chordName(int c);   // OFF, MAJ, MIN, 7, MAJ7, MIN7, SUS2, SUS4, POWER, MIN9, ADD9
const int8_t* chordIntervals(int c);  // demi-tons au-dessus de la note jouée (6 cases, -1 = fin)
const char* arpModeName(int m);
const char* arpRateName(int r);
// sources des routes libres (poignées [✛ MOD]) : 8 LFO, 4 macros, vélocité, molette MOD
constexpr int kRouteSrcMacro = kNumLfos, kRouteSrcVel = kNumLfos + kNumMacros, kRouteSrcWheel = kRouteSrcVel + 1;
constexpr int kNumRouteSrcs = kRouteSrcWheel + 1;
const char* routeSrcName(int s);  // LFO 1 … LFO R6, MACRO 1..4, VÉLOCITÉ, MOLETTE
// Paramètre d'un des 8 LFO (0 et 1 : page ENV / LFO, 2..7 : rack R1..R6). kLfOn n'existe que pour
// le rack (renvoie kNumParams pour LFO 1 et 2, toujours actifs).
constexpr uint32_t lfoP(int lfo, int field) {
    return lfo < 2 ? (field == kLfDraw ? (uint32_t)kLfo1Draw + (uint32_t)lfo
                                       : (field == kLfOn ? kNumParams : (uint32_t)kLfo1Rate + (uint32_t)(lfo * 6 + field)))
                   : (uint32_t)kRkBase + (uint32_t)((lfo - 2) * (int)kLfFieldsPer + field);
}
const char* lfoDisplayName(int lfo);  // « LFO 1 », « LFO 2 », « LFO R1 »…
constexpr int kLfoParamsPer = 6;  // RATE, PHASE, FADE, LISSAGE, FORME, SYNC
constexpr uint32_t lfoParam(int lfo, int k) { return (uint32_t)kLfo1Rate + (uint32_t)(lfo * kLfoParamsPer + k); }
enum LfoShape { kLfoSine = 0, kLfoTri, kLfoSaw, kLfoSqr, kLfoSH, kNumLfoShapes };
const char* lfoShapeName(int s);   // SINE, TRI, SAW, SQR, S&H
const char* lfoSyncName(int s);    // OFF, 1/1, 1/2, 1/4, 1/8, 1/16
float lfoSyncFactor(int s);        // cycles par noire x 2 (0 : libre) ; Hz = facteur x BPM / 120
float lfoRateHz(float v);          // 0.05 * 400^v
float lfoFadeSeconds(float v);     // 4 s x v
const char* envCurveName(float c); // LIN, EXP, LOG, EXTR, CUSTOM
float envCurveQ(float c);          // 4^((c - 0.5) x 2)
const char* fxKey(int fx);       // « hyper », « dist »… (fichiers .raw)
const char* fxName(int fx);      // « HYPER/DIM »…
const char* fxKnobName(int fx, int k);  // « RATE »…
std::string fxValueText(int fx, int k, float v);  // texte d'un des 3 boutons
const char* modSourceName(int s);       // VIE, LFO 1, LFO 2, ENV, CLAVIER, ALÉA
const char* oscModeName(int m);         // « SYNC : 0° »…

struct ParamDef {
    const char* key;    // clé dans les fichiers .raw
    const char* name;   // nom affiché par l'hôte
    const char* unit;
    float min, max, def;
    int steps;          // 0 = continu, sinon nombre de crans - 1 (convention VST3)
};

const ParamDef& paramDef(uint32_t id);
float clampParam(uint32_t id, float plain);
float toNormalized(uint32_t id, float plain);
float fromNormalized(uint32_t id, float normalized);
std::string formatParam(uint32_t id, float plain);
std::string panText(float v);         // « C », « L 12 », « R 8 »
bool parseParam(uint32_t id, const char* text, float& plain);
int paramIdFromKey(const char* key);  // -1 si inconnu

// --- conversions partagées par le moteur et l'interface ---------------------------------------
// Niveau : dB = -(1 - v) * 18.2, 0 dB en haut ; v = 0 coupe (choix d'ergonomie, voir README).
float levelToDb(float v);
float levelToGain(float v);
std::string levelText(float v);       // "-4.0 dB"
float speedToHz(float v);             // 0.5 * 40^v
float cutoffToHz(float v);            // 20 Hz * 1000^v
float glideSeconds(float v);          // 1,5 s * v²
float punchSeconds(float v);          // 5 ms * 80^v
const char* pumpRateName(float v);    // 1/2, 1/4, 1/8, 1/16
float attackSeconds(float v);         // 1 ms .. 5 s (maquette : 0.001 x 5000^v)
float decaySeconds(float v);          // 5 ms .. 5 s
float releaseSeconds(float v);        // 5 ms .. 10 s
// paramètre « bouton » continu (destination possible d'une route libre glissée sur un potard)
bool isModulatable(uint32_t id);

// Valeurs courantes, lues par le thread audio et l'interface sans verrou.
class ParamStore {
public:
    ParamStore();
    ParamStore(const ParamStore&) = delete;
    ParamStore& operator=(const ParamStore&) = delete;
    float get(uint32_t id) const { return values_[id].load(std::memory_order_relaxed); }
    void set(uint32_t id, float plain) { values_[id].store(clampParam(id, plain), std::memory_order_relaxed); }
    int geti(uint32_t id) const { return (int)(get(id) + (get(id) >= 0.f ? 0.5f : -0.5f)); }
    void resetDefaults();

private:
    std::atomic<float> values_[kNumParams];
};

} // namespace raw2
