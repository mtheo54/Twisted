#pragma once
// Blocs de l'interface : les cartes de chaque page, que les skins peuvent placer ailleurs.
// Clés stables (fichiers .raw2skin, ABRASION SKIN STUDIO) : ne jamais renommer, seulement ajouter.
#include "ui/Canvas.h"

namespace raw2::ui {

enum BlockId : int {
    kBSonOscA, kBSonVie, kBSonOscB, kBSonModules, kBSonSortie,
    kBVieAutomate, kBVieControles,
    kBModMatrice, kBModLiaison, kBModPatchs, kBModRack,
    kBEnvEnv1, kBEnvLfo1, kBEnvEnv2, kBEnvLfo2,
    kBEfxRack, kBEfxEditeur, kBEfxSortie,
    kBMixAnalyseur, kBMixAssistant, kBMixHistorique,
    kBWtTable, kBWtEditeur, kBWtImport, kBWtTraitement, kBWtPrereglages,
    kBJeuVoix, kBJeu808, kBJeuFiltre, kBJeuMacros, kBJeuArpege, kBJeuAccords,
    kBSynthFiltre, kBSynthOsc, kBSynthBruit, kBSynthUnison, kBSynthMpe,
    kNumBlocks
};

struct BlockDef {
    const char* key;   // "son.oscA"
    int tab;           // onglet (0 SON, 1 VIE, 2 MODULES, 3 ENV / LFO, 4 EFX, 5 MIX IA, 6 WAVETABLE, 7 JEU, 8 SYNTH)
    const char* name;  // nom affiché dans le studio
    Rect r;            // place d'origine (fenêtre 1280 x 960)
};

extern const BlockDef kBlocks[kNumBlocks];
int blockByKey(const char* key);  // -1 si inconnu

// zone où un bloc peut être posé (sous l'en-tête, au-dessus du pied de page)
constexpr float kBlockTop = 64.f, kBlockBottom = 940.f, kBlockLeft = 8.f, kBlockRight = 1272.f;

} // namespace raw2::ui
