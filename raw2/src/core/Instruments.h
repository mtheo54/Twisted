#pragma once
// Instruments multi-échantillonnés (couche « ROMpler ») : un instrument est un fichier SFZ (sous-
// ensemble : zones de touches et de vélocité, note de référence, accord fin, volume, boucles) et
// ses WAV. Ils sont rangés dans Documents › Raw 2 › Instruments (un dossier par instrument) ; la
// banque de sons livrée à part y dépose les siens, et n'importe quel SFZ à WAV peut s'y ajouter.
// Comme pour les sons déposés : chargement en tâche de fond, lecture par pointeur atomique côté
// audio, libération seulement quand aucun bloc audio ne peut plus lire l'ancien instrument.
#include "core/Json.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace raw2 {

struct InstZone {
    int lokey = 0, hikey = 127, lovel = 1, hivel = 127, root = 60;
    float tune = 0.f;   // cents
    float gain = 1.f;   // volume (linéaire)
    bool loop = false;
    int loopStart = 0, loopEnd = 0, offset = 0;
    int ch = 1, frames = 0;
    double sr = 44100.0;
    const float* data = nullptr;  // entrelacé (ch canaux), possédé par l'instrument
};

// numéro unique de chaque instrument / son déposé chargé (jamais réutilisé, même si la mémoire l'est)
uint64_t nextSoundSerial();

struct Instrument {
    const uint64_t serial = nextSoundSerial();  // l'audio reconnaît « son » instrument par ce numéro
    std::string name, category, path, license;
    std::vector<std::vector<float>> buffers;  // un tampon par fichier WAV
    std::vector<InstZone> zones;
    size_t bytes = 0;
    // zone d'une touche et d'une vélocité (la plus proche si aucune ne couvre exactement)
    const InstZone* find(int key, int vel) const;
};

// Aperçu d'une zone d'instrument pour l'interface (forme d'onde min / max, infos)
struct InstOverview {
    uint64_t serial = 0;
    int zone = -1, zones = 0, lokey = 0, hikey = 127, root = 60;
    double seconds = 0.0;
    bool loop = false;
    std::vector<float> mn, mx;
};

struct InstEntry {
    std::string name, category, path;
};

// Lecture d'un fichier SFZ et de ses WAV (fil de chargement). nullptr et err rempli en cas d'échec.
std::unique_ptr<Instrument> loadSfzInstrument(const std::string& sfzPath, std::string* err);

class InstrumentBank {
public:
    explicit InstrumentBank(bool offline);
    ~InstrumentBank();
    InstrumentBank(const InstrumentBank&) = delete;
    InstrumentBank& operator=(const InstrumentBank&) = delete;
    void bindEpoch(const std::atomic<uint64_t>* epoch, const std::atomic<bool>* inProcess) { epoch_ = epoch; inProcess_ = inProcess; }

    // fil de l'interface
    void scan();                                   // relit Documents › Raw 2 › Instruments
    std::vector<InstEntry> catalog() const;
    void load(int osc, const std::string& sfzPath);  // en tâche de fond (immédiat hors ligne)
    bool loadByName(int osc, const std::string& name);
    void clear(int osc);
    bool has(int osc) const { return cur_[(size_t)osc].load() != nullptr; }
    bool loading(int osc) const { return pending_[(size_t)osc].load(); }
    std::string name(int osc) const;     // instrument joué (ou en chargement)
    std::string category(int osc) const;
    std::string lastError(int osc) const;
    json::Value save(int osc) const;     // {"name", "category"}
    // forme d'onde de la zone jouée par (key, vel), sur cols colonnes ; calculée sous le verrou de la
    // banque (l'instrument ne peut pas être libéré pendant ce temps), gardée tant que rien ne change
    bool overview(int osc, int key, int vel, int cols, InstOverview& out) const;
    void loadJson(int osc, const json::Value& v);

    // fil audio
    const Instrument* audio(int osc) const { return cur_[(size_t)osc].load(std::memory_order_acquire); }

private:
    void publish(int osc, Instrument* in);
    void collectLocked(bool force);
    void workerMain();

    std::array<std::atomic<Instrument*>, 2> cur_{};
    std::array<std::atomic<bool>, 2> pending_{};
    std::array<std::string, 2> wantName_, error_;
    struct Job { int osc; std::string path; uint64_t ticket; };
    std::vector<Job> jobs_;
    std::array<uint64_t, 2> ticket_{};
    struct Retired { Instrument* in; uint64_t epoch; bool busy; };
    std::vector<Retired> retired_;
    std::vector<InstEntry> catalog_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool run_ = true;
    const bool offline_;
    std::thread worker_;
    const std::atomic<uint64_t>* epoch_ = nullptr;
    const std::atomic<bool>* inProcess_ = nullptr;
};

} // namespace raw2
