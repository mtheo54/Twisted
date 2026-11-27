#include "ui/Blocks.h"

#include <cstring>

namespace raw2::ui {

const BlockDef kBlocks[kNumBlocks] = {
    {"son.oscA", 0, "OSC A", {24.f, 73.f, 361.8f, 499.2f}},
    {"son.vie", 0, "VIE", {395.8f, 73.f, 488.4f, 499.2f}},
    {"son.oscB", 0, "OSC B", {894.2f, 73.f, 361.8f, 499.2f}},
    {"son.modules", 0, "HARMONIQUES \xC2\xB7 ESPACE \xC2\xB7 TEXTURE", {24.f, 582.2f, 1232.f, 56.f}},
    {"son.sortie", 0, "SORTIE (CLAVIER)", {24.f, 648.2f, 1232.f, 287.8f}},
    {"vie.automate", 1, "AUTOMATE", {24.f, 73.f, 880.f, 863.f}},
    {"vie.controles", 1, "CONTR\xC3\x94LES", {916.f, 73.f, 340.f, 863.f}},
    {"mod.matrice", 2, "MATRICE", {24.f, 115.f, 880.f, 821.f}},
    {"mod.liaison", 2, "LIAISON", {916.f, 115.f, 340.f, 191.f}},
    {"mod.patchs", 2, "PATCHS \xC2\xB7 SORTIE", {916.f, 318.f, 340.f, 618.f}},
    {"mod.rack", 2, "RACK DE LFO", {24.f, 115.f, 1232.f, 821.f}},
    {"env.env1", 3, "ENV 1", {24.f, 73.f, 610.f, 425.5f}},
    {"env.lfo1", 3, "LFO 1", {646.f, 73.f, 610.f, 425.5f}},
    {"env.env2", 3, "ENV 2", {24.f, 510.5f, 610.f, 425.5f}},
    {"env.lfo2", 3, "LFO 2", {646.f, 510.5f, 610.f, 425.5f}},
    {"efx.rack", 4, "RACK", {24.f, 73.f, 369.6f, 693.f}},
    {"efx.editeur", 4, "EFFET", {403.6f, 73.f, 852.4f, 693.f}},
    {"efx.sortie", 4, "SORTIE FX", {24.f, 776.f, 1232.f, 160.f}},
    {"mix.analyseur", 5, "ANALYSEUR", {24.f, 73.f, 830.f, 863.f}},
    {"mix.assistant", 5, "ASSISTANT", {866.f, 73.f, 390.f, 623.f}},
    {"mix.historique", 5, "HISTORIQUE", {866.f, 708.f, 390.f, 228.f}},
    {"wt.table", 6, "TABLE", {24.f, 73.f, 330.f, 863.f}},
    {"wt.editeur", 6, "\xC3\x89" "DITEUR DE FRAME", {366.f, 73.f, 586.f, 515.7f}},
    {"wt.import", 6, "IMPORT / EXPORT", {366.f, 600.7f, 586.f, 335.3f}},
    {"wt.traitement", 6, "TRAITEMENT", {964.f, 73.f, 292.f, 299.f}},
    {"wt.prereglages", 6, "PR\xC3\x89R\xC3\x89GLAGES", {964.f, 384.f, 292.f, 552.f}},
    {"jeu.voix", 7, "VOIX", {24.f, 73.f, 403.f, 318.f}},
    {"jeu.808", 7, "808 \xC2\xB7 SUB", {438.5f, 73.f, 403.f, 318.f}},
    {"jeu.filtre", 7, "FILTRE \xC2\xB7 V\xC3\x89LOCIT\xC3\x89", {853.f, 73.f, 403.f, 318.f}},
    {"jeu.macros", 7, "MACROS", {24.f, 403.f, 1232.f, 206.f}},
    {"jeu.arpege", 7, "ARP\xC3\x88GE", {24.f, 621.f, 817.5f, 315.f}},
    {"jeu.accords", 7, "ACCORDS", {853.f, 621.f, 403.f, 315.f}},
    {"synth.filtre", 8, "FILTRE", {24.f, 73.f, 610.f, 440.f}},
    {"synth.osc", 8, "OSCILLATEURS \xC2\xB7 WARP", {646.f, 73.f, 610.f, 440.f}},
    {"synth.bruit", 8, "BRUIT", {24.f, 525.f, 397.f, 411.f}},
    {"synth.unison", 8, "UNISON", {433.f, 525.f, 397.f, 411.f}},
    {"synth.mpe", 8, "EXPRESSION \xC2\xB7 MPE", {842.f, 525.f, 414.f, 411.f}},
};

int blockByKey(const char* key) {
    for (int i = 0; i < kNumBlocks; ++i)
        if (std::strcmp(kBlocks[i].key, key) == 0) return i;
    return -1;
}

} // namespace raw2::ui
