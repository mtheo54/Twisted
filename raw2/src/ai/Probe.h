#pragma once
// Écoute interne : rejoue le son courant hors ligne (moteur identique, sans toucher à l'instance
// qui joue) sur un petit protocole fixe, puis en extrait les caractéristiques pour le réseau des
// rôles. Sert aussi à l'entraînement. À utiliser hors du thread audio (thread de travail).
#include "ai/Features.h"
#include "core/Model.h"

#include <array>
#include <memory>
#include <string>

namespace raw2::ai {

struct ProbeResult {
    std::array<float, kProbeDim> x{};
    float peakDb = -100.f;
    bool silent = true;
};

// État du son copié depuis l'instance (à faire dans le thread de l'interface).
struct SoundSnapshot {
    std::string preset;  // JSON du preset (paramètres, tables, matrice)
    std::string life;    // grilles de la vie
    int soundWorld = 0;
    bool valid = false;
};
SoundSnapshot snapshotSound(Model& live);

class ProbeRenderer {
public:
    ProbeRenderer();
    ~ProbeRenderer();
    Model& model() { return *m_; }
    void load(const SoundSnapshot& s);      // charge le son copié
    ProbeResult render(uint32_t seed = 1);  // rend le protocole avec les paramètres actuels du modèle

    static constexpr double kRate = 48000.0;
    static constexpr double kSegSeconds = 1.2;

private:
    std::unique_ptr<Model> m_;
    std::string life_;
};

} // namespace raw2::ai
