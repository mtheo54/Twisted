#pragma once
// Poids des réseaux de l'IA, compilés dans la DLL. AiWeights.cpp est produit par
// tools/train_ai.cpp (raw2_train) : ne pas le modifier à la main.
#include "ai/Features.h"
#include "ai/Mlp.h"

namespace raw2::ai {

constexpr int kNumRoles = 5;  // BASSE, LEAD, PAD, ARP / SEQ, TEXTURE (rôles 1..5 de la maquette)
constexpr int kNumMixOut = 6; // EQ 250 Hz, COMP, SHELF 8 kHz, EQ 80 Hz, EQ 3.5 kHz, LIMITEUR

// MixNet : descripteur de la sortie réelle (kSegDim) -> 6 logits (besoin de chaque correction)
//          + 4 dosages en dB (EQ 250 Hz, 8 kHz, 80 Hz, 3.5 kHz, ordre de MixEq), en valeur absolue.
extern const Network kMixNet;
// RoleNet : écoute interne (kProbeDim) -> 5 logits de rôle.
extern const Network kRoleNet;
// Profil spectral moyen de chaque rôle (bandes relatives, dB), pour expliquer les écarts.
extern const float kRoleProfile[kNumRoles][kBands];

struct TrainingInfo {
    int roleExamples, rolePresets, roleHeldOut;
    float roleValAcc;      // précision sur des presets jamais vus pendant l'entraînement
    int mixExamples;
    float mixValAcc;       // décisions correctes (6 corrections) sur des presets jamais vus
    float mixAmountMae;    // erreur moyenne du dosage (dB)
    const char* date;
};
extern const TrainingInfo kTrainingInfo;

} // namespace raw2::ai
