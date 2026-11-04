#pragma once
// Fenêtre de RAW 2 : en-tête, cinq pages (SON, VIE, MODULES, MIX IA, WAVETABLE), fenêtre de
// choix des presets, enregistrement, thèmes, mode X-Y. Dessine une plaque logique de 1280 x 960.
#include "ai/Assistant.h"
#include "core/Model.h"
#include "ui/Analyzer.h"
#include "ui/Ui.h"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace raw2::ui {

// État de l'interface qui survit à la fermeture de la fenêtre (une instance par plugin).
struct EditorState {
    // SON : terminal PROPOSITION
    int role = 1;
    int rec = 0;
    double typeT = -1e9;
    std::string status;
    bool statusOn = false;
    ai::SoundAnalysis sound;   // dernière écoute interne (RoleNet + essais)
    bool soundValid = false;
    uint64_t soundSig = 0;     // signature du son de la dernière analyse terminée
    std::string recShown;      // texte affiché, pour ne pas le retaper s'il n'a pas changé
    // EFX
    int fxSel = 1;  // DISTORTION
    bool fxBypass = false;
    std::array<bool, kNumFx> fxSaved{};
    // VIE
    int brushMode = 0;  // 0 dessiner, 1 gomme, 2 motif
    int brushSize = 1;
    Pattern brushPat = Pattern::Glider;
    // MODULES
    int mxSel = ModMatrix::idx(0, 6);
    int modSide = 0;  // 0 SIDE A (matrice), 1 SIDE B (rack modulaire)
    // MIX IA
    int aiPhase = 0;  // 0 attente, 1 écoute, 2 terminé
    double aiT0 = 0.0;
    int mixProf = 0;  // profil : NEUTRE, AGRESSIF, CHAUD, AÉRIEN, PROPRE
    bool mixRef = true, mixEx = true;  // courbe cible, marques d'excès
    // action corrective : réglable (dosage), activable, appliquée seulement sur accord
    struct Act {
        int kind = 0;     // 0 EQ, 1 compresseur, 2 limiteur
        int type = 0;     // MixFilterType
        float f = 1000.f, g = 0.f, q = 1.f;
        std::string why;
        bool on = true, applied = false;
        int slot = -1;    // filtre de la chaîne occupé une fois appliquée
        uint32_t uid = 0;
    };
    std::vector<Act> acts;
    uint32_t actUid = 0;
    std::array<float, 6> mixProb{};  // besoin estimé par MixNet à la dernière écoute
    bool mixScored = false;
    struct Hist {
        std::string time, text, status, src;
        int undoKind = -1;  // 0 filtre, 1 compresseur, 2 limiteur ; -1 : rien à restaurer
        int undoSlot = -1;
        uint32_t actUid = 0;
    };
    std::vector<Hist> hist;
    double sessionT0 = -1.0;
    std::vector<double> scanAcc;  // niveau moyen des 48 bandes pendant l'écoute
    int scanFrames = 0;
    float scanPeak = -90.f, scanDyn = 0.f, scanLufs = -90.f;
    bool scanLow = false;
    // WAVETABLE
    int wtTgt = 0;
    bool wtGrid = false;
    int wtTool = 0;          // 0 crayon, 1 lissage, 2 distorsion
    bool wtScopeTable = false;
    bool wtImpTable = false;
    float pSm = 0.5f, pDist = 0.35f, pTsm = 0.4f;
    bool autoNorm = false;
    struct Undo {
        int osc;
        std::array<Frame, kFrames> fr;
    };
    std::deque<Undo> undo;
    std::optional<Frame> clip;
    std::string wtMsg;
    double wtMsgT = -1e9;
    // X-Y
    bool xy = false;
    // valeurs envoyées par l'assistant : déplacement cranté (un cran toutes les 40 ms)
    std::array<std::optional<float>, kNumParams> tgt{};
    double stepT = 0.0;
};

class Editor {
public:
    // glisser-déposer sortant : fichier à glisser vers la DAW (la fenêtre lance le glisser OLE)
    std::string takeDragOut() { std::string p; p.swap(dragOut_); return p; }
    static constexpr int kWidth = 1280, kHeight = 960;

    Editor(Model& model, ParamSink& sink, EditorState& state);
    void frame(Canvas& c, const Input& in);
    Cursor cursor() const { return ui_.cursor(); }
    bool wantsKeyboard() const { return ui_.wantsKeyboard(); }
    bool anyOverlayOpen() const { return pbOpen_ || thOpen_ || saveOpen_ || instOpen_; }

private:
    // pages
    void drawHeader();
    void drawSon();
    void drawVie();
    void drawMod();
    void drawEfx();
    void drawEnv();
    void drawJeu();
    void drawStar(Pt center, float r, Color col, bool filled);
    std::string exportWav();  // son en cours -> WAV (chemin, vide si échec)                                  // page JEU (voix, 808, macros, arpège, accords)
    void choiceRow(uint32_t id, const char* const* labels, int n, float x, float y, float w, float h, int base = 0);
    void envCard(int osc, const Rect& card);
    void lfoCard(int lfo, const Rect& card, bool rack);
    void lfoScreen(int lfo, const Rect& scr, bool rack);
    bool modHandle(Id id, const Rect& r, int lfo);  // poignée [✛ MOD] : glisser vers un potard
    void rackKnob(uint32_t id, float cx, float top, const std::string& label, const std::string& value);
    void drawRack();                                 // MODULES, SIDE B
    void drawModMatrix();                            // MODULES, SIDE A
    void addRackLfo();
    void removeRackLfo(int lfo);
    bool ddAllowed() const { return !(tab_ == 5 || (tab_ == 2 && st_.modSide == 0)); }
    void dragOverlay();
    void knobRings(uint32_t id, Pt p, float size);
    bool envBtn(Id id, float right, float top, const std::string& label, bool on, float* outW);
    void envKnob(uint32_t id, float cx, float top, const std::string& label, const std::string& value);
    void drawMix();
    void drawWt();
    void drawPresetBrowser();
    void drawInstBrowser();  // choix d'un instrument multi-échantillonné pour un oscillateur
    void drawSaveDialog();
    void drawThemePanel();
    void drawFx();
    void drawFooter();

    // éléments partagés
    void lifeGrid(LifeEngine::World w, const Rect& area, float pitch, float rAlive, float rEdge, float rRing, Id id, bool big);
    Color stageColor(int k) const;
    void legend(float x, float y);
    void oscCard(int osc, const Rect& r);
    void spectrumBars(const Rect& r);
    void keyboard(const Rect& area);
    void keyboardCard(const Rect& card);
    void knobCol(uint32_t id, float cx, float top, float size, const std::string& label, const std::string& value, float mod, bool large);
    void scanScreen(const Rect& r);
    void glowLine(const std::vector<Pt>& pts, Color col, float width);
    void proposition(const Rect& r);
    void depthModule(int i, const Rect& r);
    // page EFX
    float fxModValue(int src) const;
    void fxSwitch(Id id, const Rect& r, bool on, float fontSize, uint32_t param);
    void fxScreen(int fx, const Rect& area, float a, float b, float c, float mix, bool on);
    void fxBars(const Rect& box);

    // paramètres
    float P(uint32_t id) const { return m_.params.get(id); }
    void setParam(uint32_t id, float v);
    void beginDrag(uint32_t id);
    void dragParam(uint32_t id, float v);
    void endDrag(uint32_t id);
    void knobParam(uint32_t id, Pt p, float size, float mod);
    void cancelTarget(uint32_t id) { st_.tgt[id].reset(); }
    void stepTargets();

    // actions
    void loadPreset(int index);  // usine (0..71), -1 : INIT
    void loadPack(int pack, int preset);
    void panicNow();
    void setTab(int t);
    void pushHist(const std::string& text, const std::string& status, const std::string& src, int undoKind = -1, int undoSlot = -1, uint32_t actUid = 0);
    void startAnalysis();
    void finishAnalysis();
    void genActs(bool measured);  // actions correctives du rôle / profil (dosées sur la mesure après une écoute)
    void applyAct(int i);
    void restoreHist(int i);
    void acceptRec();
    void ignoreRec();
    void soundAdvisorFrame();
    int shownRole() const;            // rôle affiché (AUTO remplacé par le rôle reconnu)
    std::string roleLabel(int r) const;
    void noteOn(int n);
    void noteOff(int n, bool force = false);

    // wavetable
    Wavetable& wtCur();      // à appeler avec tableMutex pris
    int wtSel() const;
    void wtSetSel(int i);
    void wtPushUndo();
    void wtMount();
    void wtChanged(int frame = -1);
    void wtSay(const std::string& m);
    void wtOp(const std::string& label, int kind, bool forceNorm);
    void wtAction(int id);
    void wtImport(const std::string& path, bool toOsc, int osc);
    void wtExport();
    void handleDrops();

    Model& m_;
    ParamSink& sink_;
    EditorState& st_;
    Ui ui_;
    Analyzer an_;
    const Theme* T_ = nullptr;
    double now_ = 0.0;
    int tab_ = 0;

    // fenêtres
    bool thOpen_ = false;
    double thT_ = 0.0;
    bool pbOpen_ = false;
    std::string pbQuery_;
    int pbCat_ = 0;
    float pbScroll_ = 0.f;
    int pbView_ = 0;                  // 0 FACTORY, 1 USER (packs)
    std::string pbGenre_, pbMsg_, pbRmPath_;
    bool pbGenreOpen_ = false;
    bool pbFav_ = false, pbDice_ = false;  // filtre FAVORIS, tirage HASARD demandé
    bool instOpen_ = false;
    int instOsc_ = 0;
    std::string instCat_;
    float instScroll_ = 0.f;
    std::string dragOut_, wavMsg_;
    double wavMsgT_ = -1e9;
    Pt wavPress_{};
    bool wavDragged_ = false;
    int lastNote_ = -1;  // dernière note jouée (export WAV)
    float pbGenreScroll_ = 0.f;
    Rect pbGenreRect_{};
    double pbRmT_ = 0.0;
    std::set<std::string> pbExp_;     // packs déroulés
    // édition d'un pack (chemin du pack en cours d'édition, champs, renommage, suppression)
    std::string pbEdit_, pbEdName_, pbEdAuthor_, pbEdVer_;
    std::string pbRenKey_, pbRenText_, pbDelKey_;
    double pbDelT_ = 0.0;
    void startPackEdit(int pack);
    bool saveOpen_ = false;
    std::string saveName_;
    std::string saveMsg_;
    bool seedOpen_ = false;
    int fxDragRow_ = -1, fxMenu_ = 0;  // menu EFX : 1 mode, 2 suréchantillonnage, 3 source
    int fxSelPreset_ = -2;
    int envHandle_ = 0, envDragging_ = -1, lfoHandle_ = 0, lfoDragging_ = -1;  // page ENV / LFO
    // glisser-déposer de modulation : [✛ MOD] d'un LFO vers un potard
    struct DragMod {
        bool on = false;
        int src = -1;
        uint32_t hov = 0xFFFFFFFFu;  // potard survolé (destination possible)
        Rect hovRect{};
        int tabHov = -1;
        double tabT = 0.0;
    } dd_;
    // anneau d'une route libre : glisser verticalement règle son dosage
    struct RingDrag {
        int route = -1;
        uint32_t id = 0;
        float y0 = 0.f;
        int a0 = 0;
    } rd_;
    // éditeur de forme dessinée (ÉDITION / DRAW)
    struct LfoEdit {
        int lfo = -1, mode = 0, i = -1;  // mode 1 : nœud, 2 : segment
        float vx0 = 0.f, vy0 = 0.f, c0 = 0.f, s0 = 0.f;
    } le_;
    double dblT_ = -1e9;
    Pt dblP_{};
    int dblLfo_ = -1;
    float rackScroll_ = 0.f;
    float envLast_[5] = {};  // preset pour lequel l'effet affiché a été choisi
    float fxMenuX_[2] = {0.f, 0.f};
    Rect fxMenuRect_{};
    std::vector<float> dryScope_, wetScope_;
    std::string tele_[2];
    double teleT_[2] = {-1e9, -1e9};
    bool roleOpen_ = false;
    int mixMenu_ = 0;                          // MIX IA : 1 menu RÔLE, 2 menu PROFIL
    float tpShowL_ = -90.f, tpShowR_ = -90.f;  // crête-mètres True Peak (retombée de l'affichage)
    double tpT_ = 0.0;
    float actScroll_ = 0.f, scopeGain_ = 0.05f;

    // interactions en cours
    int paintMode_ = -1, paintLast_ = -1;
    int bigLast_ = -1;
    int mouseNote_ = -1;
    int hovR_ = -2, hovC_ = -2;
    bool wtDrawing_ = false;
    int wtLastX_ = 0;
    float wtLastV_ = 0.f;
    int dragParam_ = -1;
    bool dropA_ = false, dropB_ = false, dropWt_ = false;

    // logo, X-Y, effets
    bool logoDown_ = false;
    double logoT0_ = 0.0, xyFlashT_ = -1e9, fxT_ = 0.0;
    std::array<float, 16> fxR_{};
    uint32_t rng_ = 99991;
    float rnd();

    // en-tête
    float cpuShown_ = 1.2f;
    double cpuT_ = 0.0, panicT_ = -1e9;

    // spectres crantés
    struct Bars {
        std::vector<int> sh, pk;
        std::vector<double> pkT, pkD, dropT;
        void resize(int n, double now);
        void push(int i, int target, double now, double fall, double peakFall);
    };
    Bars son_, mix_, fxb_;
    // IA : écoute de la sortie (MIX IA) et écoute interne (PROPOSITION)
    ai::SegmentAnalyzer mixSeg_;
    size_t mixPos_ = 0;
    std::unique_ptr<ai::SoundAdvisor> advisor_;
    uint64_t sigSeen_ = 0, askedSig_ = 0;
    double sigT_ = 0.0;
    bool forceSound_ = false, awaitSound_ = false;
    std::vector<float> bandTmp_;
    float hit_ = 0.f;
    int lastHeld_ = -1;
    double lastFrame_ = 0.0;
};

} // namespace raw2::ui
