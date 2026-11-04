#pragma once
// La Vie (§7.2) : UN monde torique de 64x58 (page VIE), règles B3/S23 ou B36/S23. La page SON en
// montre une fenêtre de 36x16 au centre, la « zone écoutée » : c'est elle qui module le son.
// NAISSANCE règle la densité du RESET et les naissances spontanées qui relancent un monde éteint
// ou figé. Le moteur tourne sur son propre thread ; l'audio ne lit que des atomiques (densité,
// naissances, centre de masse, aléa), jamais de verrou.
#include "core/Params.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace raw2 {

enum class Pattern { Glider, Pulsar, Gun, Ship, RPent };
constexpr int kNumPatterns = 5;

struct PatternInfo {
    const char* name;
    std::vector<std::pair<int, int>> cells;
    int w, h;
};
const PatternInfo& patternInfo(Pattern p);

struct LifeWorld {
    int GW = 0, GH = 0, N = 0;
    std::vector<uint8_t> g;
    std::vector<uint16_t> age;
    uint32_t gen = 0;
    int alive = 0;
    float live = 0.f;        // densité vivante normalisée : alive / (N * 0.3), bornée à 1
    float births = 0.f;      // naissances du dernier pas / (N * 0.08), bornées à 1
    float centroid = 0.f;    // centre de masse horizontal, -1 (gauche) .. +1 (droite)
    double since = 0.0;      // temps écoulé depuis la dernière génération (s)
    struct Dying { int i; double t; };  // rémanence de phosphore (horloge murale, ms)
    std::vector<Dying> dying;
};

// Copie d'un monde pour l'affichage.
struct LifeView {
    int GW = 0, GH = 0;
    std::vector<uint8_t> g;
    std::vector<uint16_t> age;
    std::vector<LifeWorld::Dying> dying;
    uint32_t gen = 0;
    int alive = 0;
};

struct LifeSnapshot {
    std::vector<uint8_t> g;
    std::vector<uint16_t> age;
    uint32_t gen = 0;
};

class LifeEngine {
public:
    enum World { Small = 0, Big = 1 };  // Small : la fenêtre de la page SON, Big : le monde entier
    static constexpr int kWorldW = 64, kWorldH = 58;
    static constexpr int kWinX = 14, kWinY = 21, kWinW = 36, kWinH = 16;  // zone écoutée

    // threaded = false : pas de thread, la vie avance dans audioTick (rendu hors ligne, IA)
    explicit LifeEngine(ParamStore& params, bool threaded = true);
    ~LifeEngine();

    // --- audio (sans verrou) ------------------------------------------------------------------
    void audioTick(int numSamples, double sampleRate);  // fait avancer l'horloge de la vie
    float soundLive() const { return soundLive_.load(std::memory_order_relaxed); }
    float soundBirths() const { return soundBirths_.load(std::memory_order_relaxed); }
    float soundCentroid() const { return soundCentroid_.load(std::memory_order_relaxed); }
    float randomValue() const { return rnd_.load(std::memory_order_relaxed); }
    uint32_t generationCounter() const { return genCounter_.load(std::memory_order_relaxed); }

    // --- interface (verrou interne, jamais pris par l'audio) ---------------------------------
    void setSoundWorld(World w);  // gardé pour les anciens états : le son vient toujours de la fenêtre
    World soundWorld() const { return (World)soundWorld_.load(); }
    void view(World w, LifeView& out);
    void setCell(World w, int i, bool alive);
    void paint(World w, int x, int y, int size, bool erase);
    void stamp(World w, Pattern p, int cx, int cy);
    void reset(World w);      // RESET : densité = NAISSANCE
    void clear(World w);      // EFFACER
    void inject(World w, float amount);  // injection au hasard, dosée par QUANTITÉ
    void seed(Pattern p);     // menu SEED du petit monde
    // snapshots (grand monde, §7.8)
    void snapCapture();
    void snapRecall(int i);
    void snapClearSelected();
    void setSnapSync(bool v);
    void setSnapAuto(bool v);
    struct SnapState {
        std::array<bool, 8> filled{};
        std::array<std::vector<uint8_t>, 8> thumbs;
        int selected = -1, pending = -1;
        bool sync = true, autoCapture = false;
    };
    void snapState(SnapState& out);

    // rendu hors ligne : horloge et hasard remis à zéro pour des rendus comparables
    void restart(uint32_t seed);

    // état sauvegardé (grilles, âges, snapshots)
    std::string saveState();
    void loadState(const std::string& s);

    static double wallMs();

private:
    void threadMain();
    void tickLocked(double dt, double nowWall);
    void step(LifeWorld& w);
    void count(LifeWorld& w);
    void syncWindowLocked();   // recopie la fenêtre 36x16 du monde (small_) et publie ses mesures
    void sustainLocked();      // naissances spontanées : relance un monde éteint ou figé
    int toWorld(int i) const { return (kWinY + i / kWinW) * kWorldW + kWinX + i % kWinW; }
    void setCellLocked(LifeWorld& w, int i, bool alive, double now);
    void placeLocked(LifeWorld& w, const PatternInfo& p, int ox, int oy, double now);
    void applySnapLocked(int i, double now);
    void publishLocked();
    LifeWorld& world(World) { return big_; }  // toute modification porte sur le monde unique

    ParamStore& params_;
    std::mutex m_;
    LifeWorld big_;            // le monde (simulé)
    LifeWorld small_;          // sa fenêtre 36x16, recopiée après chaque changement (lecture seule)
    uint64_t hash_[3] = {};    // empreintes des dernières générations (monde figé ou oscillant)
    int stale_ = 0;
    std::array<std::optional<LifeSnapshot>, 8> snaps_;
    int snapSel_ = -1, snapNext_ = 0, snapPend_ = -1;
    bool snapSync_ = true, snapAuto_ = false;
    uint32_t autoCount_ = 0;
    uint32_t rng_ = 0x2545F491u;
    float frand();

    double lifeTime_ = 0.0;
    uint64_t lastSamples_ = 0;
    double lastWall_ = 0.0;
    std::atomic<uint64_t> audioSamples_{0};
    std::atomic<double> audioRate_{48000.0};
    std::atomic<double> lastAudioWall_{-1e9};

    std::atomic<int> soundWorld_{Small};
    std::atomic<float> soundLive_{0.f}, soundBirths_{0.f}, soundCentroid_{0.f}, rnd_{0.f};
    std::atomic<uint32_t> genCounter_{0};

    const bool threaded_;
    std::atomic<bool> running_{true};
    std::condition_variable cv_;
    std::thread thread_;
};

} // namespace raw2
