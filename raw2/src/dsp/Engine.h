#pragma once
// Moteur audio de RAW 2 : synthé polyphonique (16 voix, unison, mono / legato avec glide),
// 2 oscillateurs wavetable par voix et un sous-oscillateur, accords et arpégiateur,
// enveloppe ADSR, filtre passe-bas 12 dB/oct, matrice de modulation 6 x 8, liaison avec la vie,
// modules HARMONIQUES / ESPACE / TEXTURE et chaîne de mixage acceptée dans MIX IA.
// Aucune allocation ni verrou dans process().
#include "core/Model.h"
#include "dsp/Effects.h"
#include "dsp/Mastering.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace raw2 {

class Engine {
public:
    explicit Engine(Model& m);
    void prepare(double sampleRate, int maxBlock);
    void reset();
    // events triés par offset croissant ; params (facultatif) : automations de l'hôte, triées par
    // offset, appliquées à l'échantillon près
    void process(float* outL, float* outR, int numSamples, const NoteEvent* events, int numEvents,
                 const ParamEvent* params = nullptr, int numParams = 0);

    static constexpr int kVoices = 16;
    int latencySamples() const { return master_.latencySamples(); }  // anticipation du limiteur

private:
    // enveloppe ADSR à courbe (maquette : attaque u^(1/q), déclin et relâche (1-u)^q)
    struct EnvShape {
        float ta = 0.01f, td = 0.1f, tr = 0.1f, sus = 1.f, q = 1.f, invq = 1.f;
    };
    struct EnvGen {
        int stage = -1;  // -1 au repos, 0 attaque, 1 déclin, 2 maintien, 3 relâche
        float t = 0.f, lv = 0.f, from = 0.f;
        void start(bool keepLevel) { stage = 0; t = 0.f; from = keepLevel ? lv : 0.f; }
        void release() { if (stage >= 0 && stage < 3) { stage = 3; t = 0.f; from = lv; } }
        bool idle() const { return stage < 0; }
        float step(float dt, const EnvShape& s);
    };
    struct Voice {
        bool active = false, released = false;
        int note = 60;
        float vel = 0.8f;
        uint64_t order = 0;
        double phA = 0.0, phB = 0.0;
        EnvGen envA, envB;  // une enveloppe par oscillateur (page ENV / LFO)
        float env = 0.f;    // niveau de l'enveloppe A (source ENV de la matrice, affichages)
        // niveau réellement appliqué : interpolé entre deux points de l'enveloppe, gardé d'un bloc
        // à l'autre (l'interpolation ne repart jamais d'un point décalé, quelle que soit la taille
        // des blocs de l'hôte)
        float eA = 0.f, eB = 0.f, deA = 0.f, deB = 0.f;
        int envCnt = 0, envSpan = 0;  // échantillons avant le prochain point ; durée du prochain segment
        // composante continue de chaque oscillateur (couplage AC, comme un synthé analogique)
        float dcA = 0.f, dcB = 0.f;
        bool dcInit = true;
        // fondu entre deux niveaux de table (le timbre suit la hauteur sans saut)
        float mixA = 0.f, mixB = 0.f;
        // lecture SAMPLE (son déposé) : position, pas, données de la version jouée
        bool smpA = false, smpB = false;
        const void* smpSetA = nullptr;   // SampleSet de départ (comparé, jamais relu s'il a changé)
        const void* smpSetB = nullptr;
        const float* smpDataA = nullptr;
        const float* smpDataB = nullptr;
        int smpLenA = 0, smpLenB = 0, smpDA = 0, smpDB = 0;  // longueur, transposition de la version
        double smpPosA = 0.0, smpPosB = 0.0;
        float smpRateA = 1.f, smpRateB = 1.f;
        // instrument multi-échantillonné : zone choisie au départ (note, vélocité), position, pas
        const InstZone* zoneA = nullptr;
        const InstZone* zoneB = nullptr;
        const void* instA = nullptr;  // instrument de départ (comparé, jamais relu s'il a changé)
        const void* instB = nullptr;
        double inPosA = 0.0, inPosB = 0.0;
        float inRateA = 1.f, inRateB = 1.f;
        // dérive analogique tirée au départ de note : hauteur (demi-tons) de chaque osc, coupure (oct)
        float driftA = 0.f, driftB = 0.f, driftCut = 0.f;
        // drive anti-crénelé (ADAA d'ordre 1) : entrée et primitive précédentes, par côté
        double adX[2] = {0.0, 0.0};  // entrée précédente du drive (ADAA)
        // page JEU : unison (rang, nombre, désaccord en demi-tons, panoramique), glide (écart restant
        // en demi-tons), punch 808 (temps depuis l'attaque), sous-oscillateur sinus
        int uni = 0, uniN = 1;
        float uniDet = 0.f, uniPan = 0.f, glide = 0.f, punchT = 0.f;
        double phS = 0.0;
        float incS = 0.f, sub = 0.f;
        float ic3 = 0.f, ic4 = 0.f, ic3r = 0.f, ic4r = 0.f;  // second étage du filtre (24 dB)
        float lin1[2] = {0.f, 0.f};
        // voix volée : fondu de sortie (échantillons restants), pendant que la nouvelle note démarre
        int ghost = 0;
        float ic1 = 0.f, ic2 = 0.f, ic1r = 0.f, ic2r = 0.f;  // filtre (gauche, droite)
        // valeurs de contrôle (début de bloc -> fin de bloc)
        float posA = 0.f, posB = 0.f, g = 0.f, k = 2.f, drive = 1.f, panL = 1.f, panR = 1.f;
        float aL = 0.f, aR = 0.f, bL = 0.f, bR = 0.f, wA = 0.f, wB = 0.f;  // niveaux x panoramique, warp
        float incA = 0.f, incB = 0.f;
        int levelA = 0, levelB = 0, modeA = 0, modeB = 0;
        bool fresh = true;
    };
    struct VoiceTargets {
        float posA = 0, posB = 0, g = 0, k = 2, panL = 1, panR = 1, aL = 0, aR = 0, bL = 0, bR = 0, wA = 0, wB = 0, drive = 1;
        float mixA = 0, mixB = 0, sub = 0;
    };
    struct Grain {
        bool on = false;
        double pos = 0.0;
        float inc = 1.f;
        int len = 0, age = 0;
        float gl = 0.f, gr = 0.f;
    };
    // couche d'entrée (clavier, hôte) : accords, strum, arpégiateur ; t : temps absolu en échantillons
    void inputOn(int note, float vel, int64_t t);
    void inputOff(int note, int64_t t);
    void schedule(int64_t t, int note, float vel, bool on);
    int nextInternal(int64_t blockStart, int done, int numSamples);  // prochain évènement interne (offset), ou -1
    void fireInternal(int64_t t);                                     // évènements internes échus à t
    void arpStep(int64_t t);
    void arpAllOff();
    double beatAt(int64_t t) const;                                   // position en noires
    // couche des voix : poly / mono / legato, unison, glide
    void noteOn(int note, float vel);
    void noteOff(int note);
    void startVoice(int note, float vel, int u, int U, float glideFrom);
    void retarget(int note, float vel, bool retrigger);
    void allOff(bool hard);
    void renderBlock(float* L, float* R, int n, int span);
    bool renderVoice(Voice& v, const VoiceTargets& t, float* L, float* R, float* mono, int n, int span, const EnvShape& esA, const EnvShape& esB);
    void startSample(Voice& v, int osc, float semis);
    float pv(uint32_t id) const;  // valeur du paramètre + routes libres
    void controlUpdate(int n);
    void spawnGrain(float amount);

    Model& m_;
    double sr_ = 48000.0;
    int maxBlock_ = 1024;
    std::array<Voice, kVoices> voices_;
    std::array<VoiceTargets, kVoices> voiceTargets_{};
    static constexpr int kGhosts = 4;  // voix volées en cours de fondu (3 ms)
    std::array<Voice, kGhosts> ghosts_;
    std::array<VoiceTargets, kGhosts> ghostTargets_{};
    int stealLen_ = 144;
    uint64_t order_ = 0;
    int ctlLeft_ = 0, ctlAge_ = 0;
    int ctlDebt_ = 0;  // échantillons déjà comptés par un bloc de contrôle interrompu (note, automation)
    void forceControl() { ctlDebt_ = std::min(ctlDebt_ + std::max(ctlLeft_, 0), 1 << 16); ctlLeft_ = 0; }
    // paramètres lissés (gains en linéaire, le reste en valeur de bouton ; passe-bas d'environ 12 ms, puis rampes linéaires par bloc de contrôle) :
    // une automation brusque ou un saut de l'hôte ne crée pas de « zipper »
    enum SmoothId { kSmMaster, kSmALvl, kSmBLvl, kSmAPan, kSmBPan, kSmCutoff, kSmReso, kSmAWarp, kSmBWarp, kSmAPos, kSmBPos, kSmCount };
    float sm_[kSmCount] = {};
    bool smSnap_ = true;
    float smoothed(SmoothId k, float target, float a);  // passe-bas d'un pas de contrôle
    // anti-clic de sortie : fondu de 5 ms avant un PANIC (puis remise à zéro), fondu d'entrée après
    float outGain_ = 0.f;
    int panicFade_ = -1;
    uint64_t panicOrder_ = 0;  // les notes jouées pendant le fondu du PANIC sont gardées
    // coupe-continu final (5 Hz, en double : aucun bruit de quantification dans le grave)
    double dcX_[2] = {0, 0}, dcY_[2] = {0, 0}, dcR_ = 0.9993;
    float dcK_ = 0.0004f;  // suivi de la composante continue des oscillateurs (environ 3 Hz)
    int fadeLen_ = 240;

    // modulation globale
    double chorusPh_ = 0.0;
    struct Lfo {
        double ph = 0.0;       // phase en cycles
        float age = 1e9f;      // secondes depuis le dernier départ (FADE IN)
        float sh = 0.f;        // valeur tenue (S&H)
        int shStep = -1;
        float sm = 0.f;        // sortie lissée (LISSAGE)
    };
    Lfo lfo_[kNumLfos];               // LFO 1, LFO 2, rack R1..R6
    float lfoNow_[kNumLfos] = {};
    float lfoValue(int n, float dt);
    // routes libres : décalage (en course normalisée du potard) de chaque paramètre routé
    std::array<float, kNumParams> modOff_{};
    std::array<uint32_t, kMaxFreeRoutes> routed_{};
    int nRouted_ = 0;
    void applyRoutes();
    const SampleSet* smpSet_[2] = {nullptr, nullptr};
    const Instrument* inst_[2] = {nullptr, nullptr};
    EnvShape envShape(int osc) const;
    float liveSm_ = 0.f, pulse_ = 0.f, centroidSm_ = 0.f;
    uint32_t lastGen_ = 0;
    float lifeMod_ = 0.f, grainAmt_ = 0.f, masterGain_ = 0.f, masterTarget_ = 0.f, space_ = 0.f, liveNow_ = 0.f;
    bool snapMaster_ = true;
    std::array<float, kNumDests> dstGlobal_{};
    std::array<float, kNumSources> srcNow_{};
    const MipTable* tab_[2] = {nullptr, nullptr};

    // page JEU
    bool slope24_ = false;
    float fdrive_ = 0.f;
    float lastPitch_ = -1.f, lastVel_ = 0.8f;
    std::array<int, 32> monoStack_{};
    int nMono_ = 0;
    struct Pending { int64_t t; int note; float vel; bool on; };
    std::array<Pending, 256> pend_{};
    int nPend_ = 0;
    int64_t time_ = 0;                        // temps absolu (échantillons) au début du bloc de l'hôte
    std::array<uint8_t, 128> keyDown_{};      // touches tenues (entrée)
    std::array<std::array<int8_t, 6>, 128> chordOut_{};  // notes produites par chaque touche (accord)
    std::array<uint8_t, 128> chordN_{};
    int keysHeld_ = 0;
    // arpégiateur : réserve de notes (ordre de jeu), position, horloge en noires
    std::array<int, 64> pool_{};
    std::array<float, 64> poolVel_{};
    int nPool_ = 0, arpPos_ = 0, arpDir_ = 1, arpNote_ = -1;
    bool arpWasOn_ = false;
    int64_t arpIdx_ = 0;
    double arpNext_ = 0.0;                    // position (noires, sans swing) du prochain pas
    double freeBeat_ = 0.0;                   // horloge libre (hôte à l'arrêt)
    double blockBeat_ = 0.0, beatPerSmp_ = 0.0;
    int64_t blockT0_ = 0;

    // texture granulaire
    std::vector<float> gbuf_;
    int gw_ = 0;
    std::array<Grain, 24> grains_;
    float spawnAcc_ = 0.f;
    uint32_t rng_ = 22222;
    float frand();
    std::array<float, 513> hann_{};
    std::array<float, 1025> sin_{};  // sinus sur un cycle (warp)
    float srcSm_[kNumSources] = {};   // sources globales lissées (LISSAGE MODULATION)

    // espace : deux lignes à retard modulées
    std::vector<float> dl_[2];
    int dw_ = 0;

    // rack d'effets (page EFX)
    FxRack fx_;
    FxSettings fxs_;
    void updateFxSettings();

    // chaîne MIX IA
    MasterChain master_;

    std::vector<float> tmpL_, tmpR_, mono_;
    float cpu_ = 0.f;
};

} // namespace raw2
