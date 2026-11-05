#pragma once
// Lecture SAMPLE (maquette, page WAVETABLE : « LECTURE AU CLAVIER · SON DÉPOSÉ ») : un son déposé
// sur un oscillateur peut être joué tel quel, en RESAMPLE (la durée suit la hauteur) ou en
// STRETCH (durée conservée : une version transposée par demi-ton, calculée en tâche de fond par
// WSOLA, comme la maquette). L'audio lit les sons par pointeur atomique ; un son remplacé n'est
// libéré qu'une fois qu'aucun bloc audio ne peut plus le lire (même principe que les tables).
#include "core/Json.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace raw2 {

uint64_t nextSoundSerial();  // Instruments.cpp

struct SampleSet {
    static constexpr int kMinD = -12, kMaxD = 24;  // transpositions calculées d'avance (STRETCH)
    static constexpr int kNumD = kMaxD - kMinD + 1;
    const uint64_t serial = nextSoundSerial();     // numéro unique (voir Instruments.h)
    std::string name;
    double sr = 48000.0;
    std::vector<float> base;                         // son nettoyé (début de silence retiré, normalisé)
    std::array<std::vector<float>, kNumD> st;        // écrites par la tâche de fond
    std::array<std::atomic<const float*>, kNumD> ver{};
    std::array<std::atomic<int>, kNumD> len{};
    std::atomic<int> ready{0};
    std::atomic<bool> cancel{false};
    // version STRETCH transposée de d demi-tons (nullptr si pas encore prête)
    const float* version(int d, int& n) const {
        if (d == 0) { n = (int)base.size(); return base.data(); }
        if (d < kMinD || d > kMaxD) return nullptr;
        const float* p = ver[(size_t)(d - kMinD)].load(std::memory_order_acquire);
        n = p ? len[(size_t)(d - kMinD)].load(std::memory_order_relaxed) : 0;
        return p;
    }
};

class SampleBank {
public:
    explicit SampleBank(bool offline);
    ~SampleBank();
    SampleBank(const SampleBank&) = delete;
    SampleBank& operator=(const SampleBank&) = delete;
    // époque audio du modèle (pour libérer sans risque)
    void bindEpoch(const std::atomic<uint64_t>* epoch, const std::atomic<bool>* inProcess) { epoch_ = epoch; inProcess_ = inProcess; }

    // fil de l'interface
    bool load(int osc, const std::string& name, const std::vector<float>& mono, double sr);
    void clear(int osc);
    bool has(int osc) const { return set_[(size_t)osc].load() != nullptr; }
    std::string name(int osc) const;
    double seconds(int osc) const;
    int ready(int osc) const;  // versions STRETCH prêtes
    static int total() { return SampleSet::kNumD - 1; }
    json::Value save(int osc) const;
    void loadJson(int osc, const json::Value& v);

    // fil audio
    const SampleSet* audio(int osc) const { return set_[(size_t)osc].load(std::memory_order_acquire); }

    // transposition par WSOLA + rééchantillonnage (même méthode que la maquette), publique pour les tests
    static std::vector<float> pitchStretch(const std::vector<float>& x, double sr, int d);

private:
    void publish(int osc, SampleSet* s);
    void collect(bool force);
    void collectLocked(bool force);  // mu_ déjà pris
    void workerMain();
    void computeAll(SampleSet* s);

    std::array<std::atomic<SampleSet*>, 2> set_{};
    struct Retired { SampleSet* s; uint64_t epoch; bool busy; };
    std::vector<Retired> retired_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<SampleSet*> todo_;
    bool run_ = true;
    const bool offline_;
    std::thread worker_;
    const std::atomic<uint64_t>* epoch_ = nullptr;
    const std::atomic<bool>* inProcess_ = nullptr;
};

} // namespace raw2
