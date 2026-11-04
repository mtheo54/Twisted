#pragma once
// Rack d'effets de RAW 2 : 12 effets stéréo (dimension, distorsion, flanger, phaser, chorus,
// délai, réverbération, compresseur, égaliseur, filtre, vintage, pump) chaînés dans un ordre libre.
// Tout est réservé dans prepare() : aucune allocation ni verrou dans process().
#include "core/Params.h"  // FxId, kNumFx

#include <cstdint>
#include <vector>

namespace raw2 {

// Réglages lus au début de chaque bloc (valeurs « plain », toutes déjà bornées par l'appelant).
struct FxSettings {
    bool on[kNumFx] = {};
    float p[kNumFx][3] = {};     // les 3 boutons de chaque effet, 0..1
    float mix[kNumFx] = {};      // MIX de chaque effet, 0..1 (sec / traité)
    int order[kNumFx] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};  // ordre de la chaîne (permutation de FxId)
    int distMode = 1;            // 0 Tape, 1 Tube, 2 Fold, 3 Hard Clip
    int oversample = 2;          // 1, 2 ou 4 (distorsion seulement)
    float eqFreq[3] = {0.14f, 0.5f, 0.86f};  // fréquence des 3 bandes d'EQ : 20 Hz * 1000^f
    float wet = 1.f;             // DRY / WET global de toute la chaîne, 0..1
    double beat = 0.0;           // position musicale (noires) au début du bloc (PUMP)
    double beatPerSample = 0.0;
};

class FxRack {
public:
    void prepare(double sampleRate, int maxBlock);   // seule fonction qui alloue
    void reset();                                     // vide les mémoires (lignes à retard, filtres)
    // Traite en place un bloc stéréo. Aucun alloc / verrou. n <= maxBlock.
    void process(float* L, float* R, int n, const FxSettings& s);
    float compGainReductionDb() const;               // pour l'affichage (dernier bloc)

private:
    static constexpr int kChunk = 32;        // sous-bloc : lissage et coefficients
    static constexpr int kHyperVoices = 7;
    static constexpr int kChorVoices = 3;
    static constexpr int kRevLines = 8;
    static constexpr int kRevAp = 4;
    static constexpr int kHbA = 8;           // coefficients du demi-bande 1x <-> 2x
    static constexpr int kHbB = 4;           // coefficients du demi-bande 2x <-> 4x

    // ligne à retard circulaire (taille puissance de 2) ; at(1) = dernier échantillon écrit
    struct Delay {
        std::vector<float> buf;
        int mask = 0, w = 0;
        void init(int minLen);
        void clear();
        void push(float x) { buf[(size_t)w] = x; w = (w + 1) & mask; }
        float at(int d) const { return buf[(size_t)((w - d) & mask)]; }
        float readLinear(float d) const;
        float readHermite(float d) const;  // d >= 2
    };

    // demi-bande IIR polyphase (deux chaînes de passe-tout), sur- et sous-échantillonnage x2
    template <int N>
    struct HalfBand {
        float c[N] = {};
        float ux[N] = {}, uy[N] = {};  // état du suréchantillonneur
        float dx[N] = {}, dy[N] = {};  // état du sous-échantillonneur
        void design(double transition);
        void clear();
        void up(float in, float& o0, float& o1);
        float down(float i0, float i1);
    };

    // valeurs lissées d'un effet : début (…0) et fin du sous-bloc courant
    struct Slot {
        float p[3] = {0, 0, 0}, p0[3] = {0, 0, 0};
        float mix = 0.f, mix0 = 0.f, fade = 0.f, fade0 = 0.f;
        bool live() const { return fade > 0.f || fade0 > 0.f; }
    };

    void clearFx(int k);
    void runFx(int k, float* l, float* r, int m, float a);
    void runHyper(float* l, float* r, int m, float a);
    void runDist(float* l, float* r, int m);
    void runFlang(float* l, float* r, int m);
    void runPhase(float* l, float* r, int m);
    void runChor(float* l, float* r, int m);
    void runDly(float* l, float* r, int m);
    void runRev(float* l, float* r, int m);
    void runComp(float* l, float* r, int m);
    void runEq(float* l, float* r, int m);
    void runFilt(float* l, float* r, int m);
    void runVintage(float* l, float* r, int m);
    void runPump(float* l, float* r, int m);
    float distShape(float x, float dr, float norm) const;
    double distF(double x, double dr, double norm) const;  // primitive du façonneur (ADAA)
    float distAdaa(int ch, float x, float dr, float norm);

    bool prepared_ = false, snap_ = true;
    double sr_ = 48000.0;
    int maxBlock_ = 1024;
    Slot slot_[kNumFx];
    float wet_ = 1.f, wet0_ = 1.f;

    // ordre de la chaîne : changement par un court creux (pas de clic)
    int order_[kNumFx] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, nextOrder_[kNumFx] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    bool orderPending_ = false;
    float orderGate_ = 1.f;

    // HYPER/DIM
    Delay hyDl_[2];
    double hyPh_[kHyperVoices] = {};
    float hyGL_[kHyperVoices] = {}, hyGR_[kHyperVoices] = {};

    // DISTORSION
    HalfBand<kHbA> hbA_[2];
    HalfBand<kHbB> hbB_[2];
    int distMode_ = 1, distOs_ = 2, distModeWant_ = 1, distOsWant_ = 2;
    float distGate_ = 1.f, distGate0_ = 1.f;
    float distDcX_[2] = {}, distDcY_[2] = {}, distTone_[2] = {};
    // anti-repliement par primitive (ADAA d'ordre 1), au rythme suréchantillonné
    double adX_[2] = {}, adF_[2] = {}, adDr_[2] = {-1.0, -1.0}, adNr_[2] = {};

    // FLANGER, PHASER, CHORUS
    Delay flDl_[2];
    double flPh_ = 0.0;
    double phPh_ = 0.0;
    float phS_[2][6] = {}, phLast_[2] = {};
    Delay chDl_[2];
    double chPh_ = 0.0;

    // DÉLAI
    Delay dlyDl_[2];
    float dlyT_ = 0.f, dlyT0_ = 0.f, dlyLp_[2] = {}, dlyHp_[2] = {};

    // RÉVERBÉRATION : pré-délai, diffusion, FDN 8 lignes (Householder)
    Delay revPre_[2];
    Delay revAp_[2][kRevAp];
    Delay revLine_[kRevLines];
    float revLp_[kRevLines] = {};
    double revPh_[kRevLines] = {};
    float revRw_[kRevLines] = {}, revM_[kRevLines] = {};  // marche aléatoire, dernière position modulée
    uint32_t revRng_ = 0x9E3779B9u;
    int revApLen_[2][kRevAp] = {};
    float revLen_[kRevLines] = {};

    // COMPRESSEUR
    float compGr_ = 0.f, compGrMax_ = 0.f, compGrOut_ = 0.f, compSlow_ = 0.f;
    float compSc_[2] = {};  // passe-haut de détection (état du passe-bas complémentaire)

    // EQ et FILTRE
    float eqIc_[3][2][2] = {};  // SVF en cloche : bande, canal, deux intégrateurs
    float eqF_[3] = {0.14f, 0.5f, 0.86f}, eqF0_[3] = {0.14f, 0.5f, 0.86f};
    float svf1_[2] = {}, svf2_[2] = {};

    // VINTAGE : pleurage / scintillement (retard modulé), saturation de bande, souffle et
    // craquements suivant le signal, réduction de fréquence et de résolution (sampler 12 bits)
    Delay vtDl_[2];
    double vtWow_ = 0.0, vtFlut_ = 0.0;
    float vtLp_[2] = {}, vtHold_[2] = {}, vtHoldPh_ = 0.f, vtEnv_ = 0.f, vtNoiseLp_[2] = {}, vtCrk_ = 0.f;
    uint32_t vtRng_ = 0x1234567u;
    // PUMP : gain calé sur la position musicale du bloc
    double chunkBeat_ = 0.0, beatPerSample_ = 0.0;
    float pumpG_ = 1.f;
};

} // namespace raw2
