// Effets des 72 presets d'usine : une recette écrite à la main pour chaque preset (jeu d'effets,
// ordre du rack, réglages, dosage, modulation rapide), pour que deux presets d'une même
// catégorie ne sonnent jamais pareil.
#pragma once

#include "core/Params.h"

#include <string>

namespace raw2 {

struct FactoryFxSetup {
    bool on[kNumFx] = {};
    float p[kNumFx][3] = {};
    float mix[kNumFx] = {};
    int modSrc[kNumFx] = {};
    float modDepth[kNumFx] = {};
    int order[kNumFx] = {};
    int distMode = 1;    // 0 TAPE, 1 TUBE, 2 FOLD, 3 HARD CLIP
    int oversample = 1;  // 0 1x, 1 2x, 2 4x
    float eqFreq[3] = {0.14f, 0.5f, 0.86f};
    float wet = 1.f;
};

// false (et un message dans err) si la recette du preset est mal écrite
bool factoryFxSetup(int preset, FactoryFxSetup& out, std::string* err = nullptr);
// texte brut de la recette (pour les tests)
const char* factoryFxRecipe(int preset);

} // namespace raw2
