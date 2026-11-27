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

// Comment un bloc change de taille (comme un widget) : son contenu se réorganise au lieu d'être zoomé.
enum BlockResize : int {
    kRsScale = 0,  // seulement à l'échelle (proportions gardées)
    kRsWidth = 1,  // largeur (clavier : 1 à 3 octaves) ; hauteur seulement ajustée à la grille (minH..maxH)
    kRsFree = 2,   // largeur et hauteur (OSC : écran agrandi, infos en plus)
    kRsStack = 3,  // largeur ; les cases passent en colonnes ou en lignes (modules) ; hauteur au moins celle des lignes
};

struct BlockDef {
    const char* key;   // "son.oscA"
    int tab;           // onglet (0 SON, 1 VIE, 2 MODULES, 3 ENV / LFO, 4 EFX, 5 MIX IA, 6 WAVETABLE, 7 JEU, 8 SYNTH)
    const char* name;  // nom affiché dans le studio
    Rect r;            // place d'origine (fenêtre 1280 x 960)
    int resize = kRsScale;
    float minW = 0.f, minH = 0.f, maxW = 0.f, maxH = 0.f;  // bornes de taille (kRsWidth, kRsFree, kRsStack)
};

// modules en kRsStack : cases d'au moins 260 px, 56 px par ligne
constexpr float kModCellMin = 260.f, kModRowH = 56.f;
inline int modCols(float w) { const int c = (int)(w / kModCellMin); return c < 1 ? 1 : (c > 3 ? 3 : c); }
inline float modHeight(float w) { const int c = modCols(w); return (float)((3 + c - 1) / c) * kModRowH; }

extern const BlockDef kBlocks[kNumBlocks];
int blockByKey(const char* key);  // -1 si inconnu

// zone où un bloc peut être posé (sous l'en-tête, au-dessus du pied de page)
constexpr float kBlockTop = 64.f, kBlockBottom = 940.f, kBlockLeft = 8.f, kBlockRight = 1272.f;

} // namespace raw2::ui
