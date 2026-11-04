#pragma once
// Chaîne de mastering de la page MIX IA et mesures de sortie.
//  - MasterChain : jusqu'à 12 filtres (cloches, plateaux) en SVF trapézoïdal de Simper (réglables en
//    mouvement sans transitoire), compresseur à détection log découplée (passe-haut de détection,
//    relâche double selon le programme), limiteur True Peak à anticipation (latence constante).
//  - Meters : sonie BS.1770-4 (M, S, I avec portes), LRA (EBU Tech 3342), True Peak, crête,
//    dynamique, corrélation globale et par bande, largeur et balance stéréo.
// Tout est réservé dans prepare() : aucune allocation ni verrou dans process().
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace raw2 {

enum MixFilterType { kMixBell = 0, kMixHighShelf, kMixLowShelf };
constexpr int kMixSlots = 12;

// Suréchantillonneur 4x à phase linéaire (sinus cardinal fenêtré, 4 phases de 12 coefficients) :
// détection des crêtes entre échantillons (True Peak), comme l'annexe 2 de l'ITU-R BS.1770-4.
class TruePeak4x {
public:
    TruePeak4x();
    void reset();
    float push(float x);  // crête absolue des 4 sous-échantillons de cet instant
    static constexpr int kTaps = 12;

private:
    float h_[4][kTaps];
    float buf_[kTaps * 2] = {};
    int w_ = 0;
};

class MasterChain {
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();
    // appelés par le moteur au début de chaque bloc (fil audio)
    void setEq(int slot, bool on, int type, float hz, float gainDb, float q);
    void setComp(bool on, float threshDb, float ratio);
    void setLimiter(bool on, float ceilingDb);
    void process(float* L, float* R, int n);
    float gainReductionDb() const { return grOut_; }
    int latencySamples() const { return la_; }
    // réponse en dB d'une cloche / d'un plateau (prototype analogique pré-déformé, comme le filtre)
    static float responseDb(int type, float f0, float gainDb, float q, float f, float sampleRate);

private:
    struct Slot {
        bool on = false;
        int type = kMixBell;
        float tHz = 1000.f, tDb = 0.f, tQ = 1.f;       // cibles
        float hz = 1000.f, db = 0.f, q = 1.f;          // valeurs lissées
        double ic1[2] = {0, 0}, ic2[2] = {0, 0};
        bool live = false;                             // filtre encore audible (gain lissé non nul)
    };
    std::array<Slot, kMixSlots> slot_;
    double sr_ = 48000.0;
    int maxBlock_ = 1024;
    // compresseur
    bool compOn_ = false;
    float compThr_ = -18.f, compRatio_ = 2.f, compMix_ = 0.f;
    double scIc1_ = 0.0, scIc2_ = 0.0;              // passe-haut de détection (100 Hz)
    float grFast_ = 0.f, grSlow_ = 0.f, grOut_ = 0.f;
    // limiteur
    bool limOn_ = false;
    float limCeil_ = 0.891f, limMix_ = 0.f;
    int la_ = 72;                                    // anticipation (1,5 ms)
    std::vector<float> dlyL_, dlyR_;                 // retard de l'audio (toujours actif : latence fixe)
    int dw_ = 0, dmask_ = 0;
    TruePeak4x tpL_, tpR_;
    std::vector<float> gq_;                          // gains voulus (file circulaire)
    std::vector<int64_t> dq_;                        // minimum glissant : file monotone d'indices
    int64_t dqHead_ = 0, dqTail_ = 0, gIdx_ = 0;
    float rel_ = 1.f;                                // gain après relâche
    bool limIdle_ = false;                           // limiteur coupé : détection en sommeil
    std::vector<float> ma1_, ma2_;                   // deux moyennes glissantes (forme de l'attaque)
    double s1_ = 0.0, s2_ = 0.0;
    int m1_ = 36, m2_ = 36, i1_ = 0, i2_ = 0;
};

// Mesures lues par l'interface (copie atomique, une valeur par champ).
struct MeterReadings {
    float momentary = -70.f, shortTerm = -70.f, integrated = -70.f, lra = 0.f;
    float truePeakL = -90.f, truePeakR = -90.f, truePeakHoldL = -90.f, truePeakHoldR = -90.f;
    float samplePeak = -90.f, crest = 0.f, plr = 0.f;
    float corr = 1.f, corrLow = 1.f, corrMid = 1.f, corrHigh = 1.f;
    float width = 0.f, balance = 0.f;  // % ; balance < 0 : gauche
    float rms = -90.f;
};

class Meters {
public:
    void prepare(double sampleRate);
    void reset();
    void process(const float* L, const float* R, int n);  // fil audio
    MeterReadings read() const;                          // fil de l'interface
    std::atomic<bool> resetPeaks{false};
    std::atomic<bool> resetIntegrated{false};

private:
    void endBlock();  // toutes les 100 ms
    static constexpr int kHist = 750;  // -70 .. +5 LUFS par pas de 0,1
    struct Bq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] = {0, 0}, z2[2] = {0, 0}; double run(int c, double x); };
    double sr_ = 48000.0;
    Bq kShelf_, kHp_;
    double blockSum_ = 0.0;
    int blockN_ = 0, blockLen_ = 4800;
    std::array<double, 30> blocks_{};  // énergie des 30 derniers blocs de 100 ms (3 s)
    int bIdx_ = 0, bCount_ = 0;
    std::array<double, kHist> iE_{};   // intégrée : énergie et compte par case de sonie
    std::array<uint32_t, kHist> iN_{};
    std::array<uint32_t, kHist> sN_{};  // LRA : histogramme des valeurs à court terme
    TruePeak4x tpL_, tpR_;
    float tpBlockL_ = 0.f, tpBlockR_ = 0.f, tpHoldL_ = 0.f, tpHoldR_ = 0.f, spBlock_ = 0.f, tpMax_ = 0.f;
    double rmsAcc_ = 0.0;                 // énergie brute (crête / RMS sur 3 s)
    std::array<double, 30> rmsBlocks_{};
    std::array<float, 30> pkBlocks_{};
    // stéréo : sommes lissées (globale, graves < 250 Hz, médiums, aigus > 4 kHz)
    double lowS_[2][2] = {}, highS_[2][2] = {};  // états SVF (passe-bas 250 Hz, passe-haut 4 kHz)
    double gLow_ = 0.0, kLow_ = 1.414, gHigh_ = 0.0, kHigh_ = 1.414;
    double cs_[4][3] = {};  // pour chaque bande : Σ l·r, Σ l², Σ r² (lissés)
    double ms_[2] = {};     // énergie mid, side (lissées)
    double bal_[2] = {};
    double accB_[4][3] = {}, accMs_[2] = {}, accBal_[2] = {};  // sommes du bloc de 100 ms en cours
    double smA_ = 0.999;
    // publication
    struct Pub {
        std::atomic<float> v[18];
    } pub_;
};

} // namespace raw2
