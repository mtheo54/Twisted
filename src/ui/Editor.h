// The plugin window contents: header, 3D scene with the knobs laid over it, options strip,
// status bar. Platform code calls render() at ~60 Hz with a current GL context and
// forwards mouse events in logical pixels.
#pragma once

#include "../core/PluginCore.h"
#include "Draw2D.h"
#include "Scene.h"

#include <chrono>
#include <string>
#include <vector>

namespace tw {

class Editor {
public:
    static constexpr int kWidth = 1100, kHeight = 760;

    Editor(PluginCore& core, ParamHost& host);
    ~Editor();

    bool glInit(float scale);   // GL context current
    void glDestroy();           // GL context current
    void render(int fbW, int fbH);

    void mouseDown(float x, float y, bool shift);
    void mouseUp(float x, float y);
    void mouseMove(float x, float y, bool shift);
    void mouseWheel(float x, float y, float dy, bool shift);
    void mouseLeave();

    // Saisie clavier pour le panneau de licence, appelée par le code plateforme
    // (Win32 : WM_CHAR + collage ; Cocoa : keyDown: + Cmd-V ; X11 : XLookupString + collage).
    // Sans effet si aucun champ n'a le focus (ne gêne jamais les raccourcis du DAW).
    enum class KeyCmd { Backspace, Enter, Escape, Tab };
    void textInput(const std::string& utf8);  // un caractère tapé, ou le texte collé en entier
    void keyCommand(KeyCmd cmd);
    bool wantsKeyboard() const { return licFocus_ != LicField::None; }

private:
    struct Knob { uint32_t id; const char* label; const char* fx; float cx, cy, r; };
    struct Rect { float x, y, w, h; bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; } };
    enum class Menu { None, Preset, Character };

    void layout();
    void tick(float dt);
    void drawHeader();
    void drawKnobs();
    void drawOverlays();
    void drawOptions();
    void drawStatus();
    void drawMenu();
    void drawSegmented(const Rect& r, const char* const* labels, int n, int selected, std::vector<Rect>& hits);
    void drawToggle(const Rect& r, const char* label, bool on);
    void drawButton(const Rect& r, const std::string& label, Color fg, Color border, Color bg);

    void setParam(uint32_t id, float plain);  // full gesture: begin, perform, end
    void applyPreset(int sourceClass);
    void toast(const std::string& text, float seconds = 4.5f);
    int knobAt(float x, float y) const;

    // ---- licence : panneau d'activation, bandeau et écran BRUT ----
    enum class LicPanel { None, Form };
    enum class LicField { None, Key, Email, Password };
    void licenceOpen();
    void licenceClose();
    void licenceSubmit();
    std::string& focusedText();
    void drawLicencePanel();
    void drawBrut();
    void drawLicencePill();  // petit bouton dans l'en-tête, toujours accessible sauf en BRUT

    PluginCore& core_;
    ParamHost& host_;
    Scene scene_;
    Draw2D draw_;
    bool glOk_ = false;
    float scale_ = 1.f;

    std::vector<Knob> knobs_;
    Rect stage_{}, presetBox_{}, analyseBtn_{}, bypassBtn_{}, osSeg_{}, charBox_{}, clipSeg_{}, autoGainTgl_{}, cameraTgl_{};
    std::vector<Rect> osHits_, clipHits_, menuHits_;
    Menu menu_ = Menu::None;

    // interaction
    int dragKnob_ = -1, hoverKnob_ = -1;
    float dragStartY_ = 0, dragStartNorm_ = 0;
    int lastClickKnob_ = -1;
    std::chrono::steady_clock::time_point lastClickTime_{};
    float mouseX_ = -1, mouseY_ = -1;

    // preset morph
    bool morphing_ = false;
    float morphT_ = 0;
    float morphFrom_[kNumParams] = {}, morphTo_[kNumParams] = {};
    int currentPreset_ = -1;  // -1 = manual

    // feedback
    std::string toast_;
    float toastTime_ = 0;
    uint32_t seenRecognition_ = 0;
    float listenWait_ = 0;
    bool listening_ = false;

    // meters
    float inPeak_ = 0, outPeak_ = 0, lufs_ = -40;
    std::chrono::steady_clock::time_point last_;
    bool firstFrame_ = true;

    // ---- licence : état du panneau et des champs ----
    LicPanel licPanel_ = LicPanel::None;
    LicField licFocus_ = LicField::None;
    bool licAccountTab_ = false;  // false = onglet "clé", true = onglet "compte"
    std::string licKeyText_, licEmailText_, licPasswordText_;
    std::string licStatus_;    // dernier message du contrôleur (erreur ou succès) affiché dans le panneau
    uint64_t licSeenSeq_ = 0;  // numéro de la dernière réponse vue (jamais le texte : voir tick())
    bool licBusy_ = false;     // une activation est en cours (clé envoyée, en attente de réponse)
    float licBusyTimeout_ = 0; // filet de sécurité : si le serveur ne répond jamais
    float licGateTimer_ = 0;      // limite gate(Interface) à ~1 fois toutes les 2 s, pas 60×/s
    float licBrutT_ = 0;          // temps écoulé en BRUT, pour la vibration du bloc
    float licCaretT_ = 0;         // temps écoulé, pour le clignotement du curseur dans un champ
    Rect licMsgBox_{}, licPill_{}, licPanelBox_{}, licTabKey_{}, licTabAccount_{};
    Rect licFieldKey_{}, licFieldEmail_{}, licFieldPassword_{}, licSubmit_{}, licLogout_{}, licClose_{};
    Rect licBrutBottom_{};
};

} // namespace tw
