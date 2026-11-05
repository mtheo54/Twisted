#pragma once
// État complet d'une instance RAW 2, partagé par le processeur audio, le contrôleur VST3 et
// l'interface. Les parties lues par l'audio sont atomiques ; le reste est protégé par des
// verrous que le thread audio ne prend jamais.
#include "core/Json.h"
#include "core/Life.h"
#include "core/Modulation.h"
#include "core/Params.h"
#include "core/Instruments.h"
#include "core/Samples.h"
#include "dsp/Mastering.h"
#include "core/Spsc.h"
#include "dsp/Wavetable.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace raw2 {

constexpr int kNumSources = 6;  // VIE, LFO 1, LFO 2, ENV, CLAVIER, ALÉA
constexpr int kNumDests = 8;    // PITCH A, PITCH B, POS. A, POS. B, CUTOFF, RÉSO, GRAIN, MASTER

struct ModMatrix {
    std::array<std::atomic<int>, kNumSources * kNumDests> amt;  // -100..100
    std::array<std::atomic<bool>, kNumSources * kNumDests> on;
    ModMatrix() { clear(); }
    void clear() {
        for (auto& a : amt) a.store(0);
        for (auto& o : on) o.store(false);
    }
    static int idx(int s, int d) { return s * kNumDests + d; }
};

// Corrections acceptées dans MIX IA (maquette mise à jour) : une liste de filtres (cloches,
// plateaux) appliqués pour de vrai en sortie, plus le compresseur et le limiteur True Peak.
// MixEq : les quatre corrections de référence dont le réseau MixNet estime le besoin et le dosage.
enum MixEq { kEq250 = 0, kEq8k, kEq80, kEq35, kNumMixEq };
constexpr int kMixFilterSlots = 12;
struct MixFilter {
    std::atomic<bool> on{false};
    std::atomic<int> type{0};  // MixFilterType : 0 cloche, 1 plateau aigu, 2 plateau grave
    std::atomic<float> hz{1000.f}, db{0.f}, q{1.f};
};
struct MixChain {
    std::array<MixFilter, kMixFilterSlots> flt;
    std::atomic<bool> comp{false}, limit{false};
    std::atomic<float> compThreshDb{-18.f}, compRatio{2.f}, limitCeilDb{-1.f};
    // dosage par défaut des corrections de référence (maquette) et bornes
    static float defaultGain(int e) { static const float g[kNumMixEq] = {-2.5f, 1.5f, 2.f, -1.5f}; return g[e]; }
    static float minGain(int e) { static const float g[kNumMixEq] = {-6.f, 0.5f, 0.5f, -6.f}; return g[e]; }
    static float maxGain(int e) { static const float g[kNumMixEq] = {-0.5f, 6.f, 6.f, -0.5f}; return g[e]; }
    MixChain() { clear(); }
    void clear() {
        for (auto& f : flt) f.on.store(false);
        comp = false;
        limit = false;
        compThreshDb = -18.f;
        compRatio = 2.f;
        limitCeilDb = -1.f;
    }
    int addFilter(int type, float hz, float db, float q) {  // indice, -1 si les 12 sont pris
        for (int i = 0; i < kMixFilterSlots; ++i) {
            MixFilter& f = flt[(size_t)i];
            if (f.on.load()) continue;
            f.type = type;
            f.hz = hz;
            f.db = db;
            f.q = q;
            f.on.store(true, std::memory_order_release);
            return i;
        }
        return -1;
    }
    int activeFilters() const {
        int n = 0;
        for (const auto& f : flt) n += f.on.load() ? 1 : 0;
        return n;
    }
};

// Évènement de jeu. kind 0 : note (on / off). Les autres portent une expression :
// pression, glissé (timbre) et hauteur par canal (MPE) ou par note (VST3 Note Expression).
enum NoteKind : uint8_t {
    kEvNote = 0,
    kEvChanPress,   // pression du canal (MPE) : value 0..1
    kEvPolyPress,   // pression d'une note : value 0..1 (note, ou noteId si >= 0)
    kEvChanBend,    // pitch bend du canal (MPE) : value -1..1
    kEvChanTimbre,  // CC 74 du canal (MPE) : value 0..1
    kEvNoteTune,    // hauteur d'une note (Note Expression) : value en demi-tons
    kEvNoteTimbre,  // brillance d'une note (Note Expression) : value 0..1
};
struct NoteEvent {
    int offset = 0;
    int note = 60;
    float velocity = 0.8f;
    bool on = true;
    uint8_t kind = kEvNote;
    uint8_t channel = 0;  // canal MIDI 0..15
    int noteId = -1;      // identifiant VST3 de la note (-1 : aucun)
    float value = 0.f;
};

// automation de l'hôte : valeur « plain » d'un paramètre à un échantillon précis du bloc
struct ParamEvent {
    int offset = 0;
    uint32_t id = 0;
    float value = 0.f;
};

// Valeurs mesurées par l'audio, affichées par l'interface.
struct Readouts {
    std::array<std::atomic<float>, kNumSources> src;
    std::array<std::atomic<float>, kNumDests> dst;
    std::atomic<float> posA{0.f}, posB{0.f}, lifeMod{0.f}, compGr{0.f}, cpu{0.f}, hit{0.f};
    std::atomic<int> arpStep{-1}, arpNote{-1};  // page JEU : pas et note de l'arpège en cours
    std::atomic<int> heldNote{-1}, voices{0};
    // page ENV / LFO
    std::array<std::atomic<int>, 4> envStage{};       // -1 repos, 0 A, 1 D, 2 S, 3 R (ENV A, B, 3, 4)
    std::array<std::atomic<float>, 4> envT{}, envLv{};
    std::atomic<float> press{0.f}, timbre{0.f}, noteBend{0.f};  // expression de la note la plus récente
    std::array<std::atomic<float>, kNumLfos> lfoPh{}, lfoVal{};  // 8 LFO (anneaux de modulation des potards)
    std::atomic<double> sampleRate{48000.0};
    std::atomic<bool> active{false};
    Readouts() {
        for (auto& s : src) s.store(0.f);
        for (auto& d : dst) d.store(0.f);
    }
};

// Interface par laquelle un changement de paramètre est signalé (à l'hôte, s'il y en a un).
struct ParamSink {
    virtual ~ParamSink() = default;
    virtual void beginEdit(uint32_t id) = 0;
    virtual void performEdit(uint32_t id, float plain) = 0;
    virtual void endEdit(uint32_t id) = 0;
};

// Packs de presets (dossier Documents/Raw 2) : un sous-dossier de fichiers .raw = un pack ;
// un fichier .raw2pack (JSON « raw2-pack » de la maquette) = un pack ; « Mes presets » = MES PRESETS.
struct PackPreset {
    std::string name, category, style, tags;
    std::string path;           // fichier .raw (vide pour une entrée de .raw2pack)
    int entry = -1;             // indice dans le .raw2pack
    int base = -1;              // preset d'usine de départ (entrée de .raw2pack)
};
struct Pack {
    std::string name, author, version;
    std::string path;           // dossier ou fichier .raw2pack
    bool folder = true, mine = false;
    std::vector<PackPreset> presets;
};

class Model {
public:
    // Live : instance du plugin (threads de construction des tables et de la vie).
    // Offline : rendu hors ligne déterministe pour l'IA (tout est fait dans le thread appelant).
    enum class Mode { Live, Offline };
    explicit Model(Mode mode = Mode::Live);
    ~Model();
    bool offline() const { return offline_; }

    ParamStore params;
    LifeEngine life;
    ModMatrix matrix;
    FreeRoutes routes;       // routes libres « LFO → potard » (glisser-déposer)
    LfoShapes lfoShapes;     // formes dessinées des 8 LFO
    SampleBank samples;      // sons déposés joués en SAMPLE
    InstrumentBank instruments;  // instruments multi-échantillonnés (SFZ) joués en SAMPLE
    MixChain mix;
    Meters meters;           // mesures de sortie (page MIX IA), écrites par l'audio
    Readouts readouts;
    ScopeRing scope;
    ScopeRing dryScope;  // signal avant les effets (page EFX : sec / traité)
    SpscQueue<NoteEvent, 256> guiNotes;
    std::atomic<bool> panic{false};
    // ordre de la chaîne d'effets (page EFX) : permutation des kNumFx effets
    std::array<std::atomic<int>, kNumFx> fxOrder;
    void resetFxOrder() { for (int k = 0; k < kNumFx; ++k) fxOrder[(size_t)k].store(k); }
    // clavier de la page SON : octave, gamme, maintien (sauvés avec l'instance)
    std::atomic<int> kbOct{0}, kbScale{0};
    std::atomic<bool> kbHold{false};
    std::atomic<int> controlBlock{64};  // « SAMPLES » : bloc de calcul des modulations (32..256)
    std::atomic<float> hostBpm{120.f};  // tempo de l'hôte (synchro des LFO), 120 sans hôte
    std::atomic<double> hostPpq{0.0};   // position de l'hôte en noires au début du bloc
    std::atomic<bool> hostPlaying{false};  // transport de l'hôte en lecture (arpégiateur, PUMP calés)
    std::array<std::atomic<bool>, kNumLfos> lfoBpm{};  // affichage de la vitesse des LFO en BPM

    // --- tables d'onde ---------------------------------------------------------------------
    // edit[i] : table de l'éditeur (wtd de la maquette) ; mounted[i] : table jouée par l'osc.
    std::mutex tableMutex;
    std::array<Wavetable, 2> edit;
    std::array<bool, 2> mounted{{false, false}};
    std::array<uint64_t, 2> tableVersion{{1, 1}};
    void tablesChanged(int osc, int frame = -1);  // à appeler après une modification (verrou relâché)
    void setMounted(int osc, bool m);
    const MipTable* audioTable(int osc) const { return mip_[(size_t)osc].load(std::memory_order_acquire); }
    void audioBlockBegin() { inProcess_.store(true, std::memory_order_seq_cst); }
    void audioBlockEnd() { inProcess_.store(false, std::memory_order_seq_cst); audioEpoch_.fetch_add(1, std::memory_order_seq_cst); }

    // --- presets ---------------------------------------------------------------------------
    std::mutex presetMutex;
    // 0..71 usine, kPresetInit : INIT (preset vierge), kPresetPack : preset d'un pack (presetPack)
    static constexpr int kPresetInit = -1, kPresetPack = -2;
    int presetIndex = 0;
    std::string presetName = "verre givré";
    std::string presetCategory = "lead", presetStyle = "dream pop";
    std::string presetPack;                       // pack du preset chargé (kPresetPack)
    std::vector<Pack> packs;                      // fil de l'interface uniquement
    void scanPacks();                             // relit Documents/Raw 2 (crée les dossiers au besoin)
    void applyFactoryPreset(int i, ParamSink* sink);
    void applyInit(ParamSink* sink);              // INIT : sinus, FX coupés, matrice vide
    bool applyPackPreset(int pack, int preset, ParamSink* sink);
    bool applyPackEntry(const json::Value& e, ParamSink* sink);  // entrée raw2-pack ou preset complet
    bool saveUserPreset(const std::string& name, std::string& outPath);  // dans Mes presets
    std::string nextUserName();                   // « USER 01 », « USER 02 »…
    int installPackFile(const std::string& path, std::string& msg);  // copie un pack déposé ; presets ajoutés
    int installDemoPacks(std::string& msg);       // deux packs d'exemple (dossier + .raw2pack)
    bool removePack(int pack);                    // vers la corbeille
    // --- édition des packs par l'utilisateur (fil de l'interface) ---------------------------------
    // .raw2pack (un fichier : chaque preset est une entrée) ou pack dossier / MES PRESETS (un .raw
    // par preset). Chaque opération réécrit le fichier puis relit Documents/Raw 2 ; msg dit ce qui
    // s'est passé (affiché dans la fenêtre des presets).
    int createPack(const std::string& name, std::string& msg);       // nouveau .raw2pack vide -> indice
    bool addCurrentToPack(int pack, std::string& msg);                // ajoute le son actuel (nom du preset)
    bool replacePackPreset(int pack, int preset, std::string& msg);   // écrase avec le son actuel
    bool renamePackPreset(int pack, int preset, const std::string& name, std::string& msg);
    bool deletePackPreset(int pack, int preset, std::string& msg);    // entrée retirée / .raw à la corbeille
    bool movePackPreset(int pack, int preset, int delta);             // ordre (.raw2pack seulement)
    bool setPackInfo(int pack, const std::string& name, const std::string& author, const std::string& version, std::string& msg);
    int duplicatePack(int pack, std::string& msg);                    // copie éditable en .raw2pack -> indice
    std::string exportPack(int pack, std::string& msg);               // .raw2pack autonome dans Raw 2/Exports
    int findPack(const std::string& path) const;
    json::Value presetJson(bool withSamples = false);  // withSamples : sons déposés inclus (état)
    bool applyPresetJson(const json::Value& v, ParamSink* sink);
    void resetLifeAfterPreset();
    void applyFactoryVoice(int presetIndex, ParamSink* sink);  // enveloppes, LFO, filtre, matrice
    void applyRack(const char* spec, ParamSink* sink);         // rack modulaire d'un preset d'usine
    void applyFactoryFx(int presetIndex, ParamSink* sink);  // effets d'un preset d'usine (une recette par preset)

    // --- réglages d'affichage (sauvés avec l'instance, et comme défaut global) --------------
    std::atomic<int> theme{0};
    std::atomic<int> zoomPct{100};
    std::atomic<int> tab{0};  // 0 SON, 1 VIE, 2 MODULES, 3 ENV / LFO, 4 EFX, 5 MIX IA, 6 WAVETABLE, 7 JEU, 8 SYNTH
    std::atomic<bool> bpmDisplay{false};
    void setTab(int t);
    std::set<std::string> favorites;  // presets favoris (« f:12 » usine, « p:pack/nom » pack), fil de l'interface
    void loadGlobalSettings();
    void saveGlobalSettings();

    // --- état complet de l'instance (getState / setState VST3) -----------------------------
    std::string saveState();
    bool loadState(const std::string& s, ParamSink* sink);

private:
    void builderMain();
    void buildNow();
    void buildPendingLocked(std::unique_lock<std::mutex>& lk);
    void swapPlaying(int osc, const MipTable* playing);  // appelé verrou buildMutex_ pris
    void collectGarbage(bool force);
    void setParam(ParamSink* sink, uint32_t id, float v);

    std::array<std::atomic<const MipTable*>, 2> mip_{};
    const MipTable* fallback_ = nullptr;  // partagée par toutes les instances
    struct Retired { const MipTable* t; uint64_t epoch; bool wasInProcess; };
    std::vector<Retired> retired_;  // à supprimer dès que l'audio ne peut plus les lire
    std::vector<Retired> left_;     // sorties de mip_ mais encore possédées (osc démonté)
    std::array<const MipTable*, 2> owned_{{nullptr, nullptr}};  // tables construites par ce modèle
    const bool offline_ = false;
    std::atomic<uint64_t> audioEpoch_{0};
    std::atomic<bool> inProcess_{false};

    std::mutex buildMutex_;
    std::condition_variable buildCv_;
    std::array<std::vector<bool>, 2> dirty_{{std::vector<bool>(kMaxFrames, false), std::vector<bool>(kMaxFrames, false)}};
    std::array<bool, 2> fullDirty_{{true, true}};
    std::array<bool, 2> pending_{{true, true}};
    bool buildRun_ = true;
    std::thread builder_;
};

} // namespace raw2
