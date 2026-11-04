#pragma once
// Assistant neuronal de RAW 2 (§9.5).
//  - MIX IA : MixNet lit le descripteur de la sortie réelle et estime, pour chacune des 6
//    corrections, si elle est utile et de combien (gain des EQ).
//  - PROPOSITION : RoleNet reconnaît le rôle d'un son (BASSE, LEAD, PAD, ARP / SEQ, TEXTURE).
//    L'assistant essaie des retouches sur une copie du son (écoute interne, hors ligne) et garde
//    celles qui le rapprochent le plus du rôle visé selon le réseau : les chiffres affichés sont
//    mesurés, pas inventés.
// Tout tourne dans le thread de l'interface ou dans un thread de travail, jamais dans l'audio.
#include "ai/AiWeights.h"
#include "ai/Probe.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace raw2::ai {

bool networksTrained();

// --- MIX IA ---------------------------------------------------------------------------------------
struct MixAdvice {
    float prob[kNumMixOut] = {};      // besoin estimé de chaque correction (0..1)
    float gainDb[kNumMixEq] = {};     // dosage proposé (signé, dans les bornes de MixChain)
    float compThreshDb = -18.f;       // seuil du compresseur, d'après le niveau mesuré
};
MixAdvice adviseMix(const float* segment);  // kSegDim valeurs

// --- rôles ----------------------------------------------------------------------------------------
struct RoleGuess {
    float p[kNumRoles] = {};
    int best = 0;
};
RoleGuess classifyRole(const ProbeResult& r);
const char* roleName(int r);  // 0..4

// --- PROPOSITION ----------------------------------------------------------------------------------
struct Proposal {
    std::vector<std::pair<uint32_t, float>> changes;  // paramètre -> nouvelle valeur
    std::string what;     // « Monter OSC A de »
    std::string value;    // « +2 dB »
    std::string done;     // « OSC A +2 dB »
    float before = 0.f, after = 0.f;  // probabilité du rôle visé, avant / après (écoute interne)
};

struct SoundAnalysis {
    uint64_t serial = 0;
    int target = 0;                   // rôle visé (0..4)
    bool autoRole = false;
    RoleGuess guess;                  // son actuel
    bool silent = false;
    int bandHz = 0;                   // plus grand écart au profil du rôle
    bool bandExcess = false;
    std::vector<Proposal> props;      // 3 au plus, meilleure d'abord
    int tried = 0;                    // retouches essayées
};

class SoundAdvisor {
public:
    SoundAdvisor();
    ~SoundAdvisor();
    // targetRole : 0..4, ou -1 pour le rôle reconnu (AUTO)
    void request(const SoundSnapshot& snap, int targetRole);
    bool poll(SoundAnalysis& out);  // vrai si un nouveau résultat est prêt
    bool busy() const { return busy_.load(); }

private:
    void threadMain();
    SoundAnalysis run(const SoundSnapshot& snap, int targetRole, uint64_t serial);

    std::unique_ptr<ProbeRenderer> probe_;
    std::mutex m_;
    std::condition_variable cv_;
    bool haveJob_ = false, haveResult_ = false, quit_ = false;
    SoundSnapshot job_;
    int jobRole_ = -1;
    uint64_t serial_ = 0;
    SoundAnalysis result_;
    std::atomic<bool> busy_{false}, cancel_{false};
    std::thread thread_;
};

} // namespace raw2::ai
