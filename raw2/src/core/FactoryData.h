#pragma once
// Données d'usine : les 6 thèmes et les 72 presets (générés par tools/gen_data.py).
#include "core/Color.h"

namespace raw2 {

// Jetons de couleur d'un thème (mêmes noms que themes.json).
struct Theme {
    const char* key;    // "alu", "nothing", ...
    const char* name;   // "ALU"
    const char* label;  // "Thème ALU"
    Color chassis, chDot, card, line, rule, ink, muted, scr, scrLine, scrGrid, scrMid, scrText, scrMuted, scrRule,
        acc, onAcc, hl, ledTab, ptr, tabBg, tabFg, logoBg, btnBg, btnEdge, waveA, waveB, barHi, barLo, barPk,
        tickOff, knobFace, knobLine, lifeBg, lifeLine, dead, railBg, keyNat, keyNatLine, keySharp, crs, sw,
        inputBg, div;
    Color deadFill, cellEdge, tagAcc;  // facultatifs : voir les drapeaux ci-dessous
    bool hasDeadFill, hasCellEdge, hasTagAcc;
    Rgb8 accRGB, old, mid;  // dégradé d'âge des cellules : acc -> (mid) -> old
    bool hasMid;

    Color tagAccent() const { return hasTagAcc ? tagAcc : acc; }
};

enum class Shape { Sine, Saw, Square, Tri, Complex, Noise, Glass, Metal };
enum class Category { Bass, Lead, Pad, Pluck, Arp, Fx };

struct ShapeSpec {
    Shape shape;
    float morphStart, morphEnd;
};

struct FactoryPreset {
    const char* name;
    Category category;
    const char* style;
    ShapeSpec oscA, oscB;
    float aPos, bPos, aLevel, bLevel;
    int aOct, aSemi, bOct, bSemi;
    float lifeSpeed, lifeBirth, lifeQty;
    int depthHarm, depthSpace, depthTexture;
    bool highLife;  // B36/S23
    float master;
};

constexpr int kNumThemes = 6;
constexpr int kNumFactoryPresets = 72;
extern const Theme kThemes[kNumThemes];
extern const FactoryPreset kFactoryPresets[kNumFactoryPresets];

inline const char* categoryName(Category c) {
    switch (c) {
        case Category::Bass: return "basse";
        case Category::Lead: return "lead";
        case Category::Pad: return "pad";
        case Category::Pluck: return "pluck";
        case Category::Arp: return "arp";
        case Category::Fx: return "fx";
    }
    return "";
}

} // namespace raw2
