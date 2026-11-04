#pragma once
// Rack modulaire (maquette : MODULES / SIDE B) : routes libres « LFO → potard » posées par
// glisser-déposer de la poignée [✛ MOD], et formes de LFO dessinées (ÉDITION / DRAW).
// L'interface écrit, l'audio lit sans verrou.
#include "core/Json.h"
#include "core/Params.h"

#include <array>
#include <atomic>
#include <vector>

namespace raw2 {

// --- routes libres -------------------------------------------------------------------------
// Une route ajoute amt % de la course du potard × la valeur du LFO (-1..1) au paramètre.
constexpr int kMaxPatches = 48;      // liaisons de matrice + routes libres (compteur « n / 48 »)
constexpr int kMaxFreeRoutes = 40;
struct FreeRoute {
    std::atomic<int> src{-1};        // LFO 0..7 (LFO 1, LFO 2, R1..R6), -1 : libre
    std::atomic<int> id{0};          // paramètre (isModulatable)
    std::atomic<int> amt{50};        // -100..100
};
class FreeRoutes {
public:
    std::array<FreeRoute, kMaxFreeRoutes> r;
    // les routes valides sont contiguës, de 0 à count() - 1
    int count() const;
    int find(int src, uint32_t id) const;
    int add(int src, uint32_t id, int amt);  // indice (existant ou nouveau), -1 si plein ou refusé
    void remove(int i);
    void removeSource(int src);              // LFO du rack retiré
    void removeTarget(uint32_t first, uint32_t last);  // routes vers les paramètres [first, last)
    void clear();
    json::Value save() const;
    void load(const json::Value& v);         // tableau [{src, param (clé), amt}]
};

// --- formes dessinées ----------------------------------------------------------------------
struct LfoNode {
    float x = 0.f, y = 0.f;  // x 0..1 (un cycle), y -1..1
    float c = 0.f, s = 0.f;  // courbure du segment suivant (-1..1), adoucissement (0..1)
};
constexpr int kLfoTab = 1024;
class LfoShapes {
public:
    LfoShapes();
    // fil de l'interface
    std::array<std::vector<LfoNode>, kNumLfos> pts;   // nœuds (vides : jamais dessiné)
    std::array<int, kNumLfos> snap{};                 // 0 : 1/16, 1 : 1/8, 2 : libre
    std::array<bool, kNumLfos> edited{};
    std::array<int, kNumRackLfos> grp{};              // [ GRP : G1..G3 ] du rack (0 : aucun)
    void set(int lfo, const std::vector<LfoNode>& p); // reconstruit la table jouée et la publie
    void reset(int lfo);                              // pas de dessin
    static std::vector<LfoNode> seed(int shape);      // départ d'un dessin depuis une forme
    static float segG(float t, float c, float s);
    static float eval(const std::vector<LfoNode>& p, float u);  // u : phase 0..1
    json::Value save() const;
    void load(const json::Value& v);
    // fil audio : table d'un cycle (kLfoTab + 1 valeurs)
    const float* table(int lfo) const { return tab_[(size_t)lfo][(size_t)cur_[(size_t)lfo].load(std::memory_order_acquire)].data(); }

private:
    std::array<std::array<std::array<float, kLfoTab + 1>, 2>, kNumLfos> tab_{};
    std::array<std::atomic<int>, kNumLfos> cur_{};
};

} // namespace raw2
