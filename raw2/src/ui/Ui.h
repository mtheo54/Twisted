#pragma once
// Interface en mode immédiat : chaque image redessine tout et traite la souris au passage.
// Pas d'animation douce (§4) : tout ce qui bouge le fait par paliers.
#include "core/FactoryData.h"
#include "ui/Canvas.h"

#include <cstdint>
#include <string>
#include <vector>

namespace raw2::ui {

enum class Cursor { Arrow, Hand, ResizeNS, ResizeEW, Cross, Text };

struct Input {
    Pt mouse{-1000.f, -1000.f};
    bool down = false, pressed = false, released = false;
    bool rightPressed = false;
    bool shift = false;  // touche Maj tenue
    float wheel = 0.f;  // crans, positif vers le haut
    std::string typed;  // UTF-8 tapé pendant l'image
    bool keyEsc = false, keyEnter = false, keyBack = false, keyDelete = false;
    bool keyLeft = false, keyRight = false, keyHome = false, keyEnd = false;
    // glisser-déposer de fichiers
    bool dragOver = false;
    Pt dragPos{-1000.f, -1000.f};
    std::vector<std::string> dropped;
    Pt dropPos{-1000.f, -1000.f};
    double timeMs = 0.0;
};

using Id = uint64_t;
Id makeId(const char* s, int i = 0);

struct ButtonStyle {
    Color bg, fg, border;
    Font font;
    bool shadow = true;   // ombre dure 2 px
    bool dashed = false;
    bool pressed = false; // état enfoncé permanent (bouton actif)
    float shadowSize = 2.f;
};

class Ui {
public:
    void beginFrame(Canvas& c, const Theme& t, const Input& in);
    void endFrame();

    Canvas& c() { return *c_; }
    const Theme& T() const { return *t_; }
    const Input& in() const { return in_; }
    double now() const { return in_.timeMs; }

    // calques : un bloqueur empêche les widgets des calques inférieurs de réagir
    void setLayer(int l) { layer_ = l; }
    int layer() const { return layer_; }
    void addBlocker(const Rect& r, int layer) { blockers_.push_back({r, layer}); }
    void clearBlockers() { blockers_.clear(); }
    bool hit(const Rect& r) const;

    struct Interact {
        bool hovered = false, pressed = false, held = false, clicked = false, released = false;
    };
    Interact interact(Id id, const Rect& r, Cursor cur = Cursor::Hand);
    bool isActive(Id id) const { return active_ == id; }
    Id active() const { return active_; }
    void setCursor(Cursor c) { cursor_ = c; }
    Cursor cursor() const { return cursor_; }
    void releaseActive() { active_ = 0; }

    // focus clavier (champs texte)
    Id focus = 0;
    bool wantsKeyboard() const { return focus != 0; }

    // --- dessin façon maquette ----------------------------------------------------------------
    void border(const Rect& r, Color c, float w = 1.f);
    void dashedBorder(const Rect& r, Color c, float dash = 3.f, float gap = 3.f);
    void box(const Rect& r, Color fill, Color line, float radius = 2.f);  // fond + bordure 1 px
    // texte dans une boîte CSS (top = haut de la boîte)
    float text(const std::string& s, float x, float top, const Font& f, Color c, float lineHeight = 0.f);
    void textCenter(const std::string& s, const Rect& r, const Font& f, Color c);   // centré dans r
    void textRight(const std::string& s, float right, float top, const Font& f, Color c);
    float width(const std::string& s, const Font& f) { return c_->textWidth(s, f); }
    std::string fit(const std::string& s, const Font& f, float maxW);  // ajoute « … » si trop long

    // les deux « + » d'une carte (classe .crs)
    void crosses(const Rect& card, Color c);
    // carte claire / carte écran noire (cardS / cardTerm)
    void card(const Rect& r);
    void cardTerm(const Rect& r);
    // en-tête de carte : titre en Silkscreen et filet ; renvoie le bas du filet
    void cardHeader(const Rect& card, const std::string& title, bool term, float padX = 20.f, float padTop = 16.f);
    enum class Tag { Line, Dark, Accent };
    // étiquette alignée à droite ; renvoie sa largeur
    float tag(const std::string& label, float right, float top, Tag kind, bool term = false);

    // écran noir avec grille de points (scr, bigScrMini...)
    void screen(const Rect& r, float dotSpacing = 12.f);

    bool button(Id id, const Rect& r, const std::string& label, const ButtonStyle& st, Cursor cur = Cursor::Hand);
    void drawButton(const Rect& r, const std::string& label, const ButtonStyle& st, bool pressed);
    ButtonStyle keyStyle() const;            // RESET, PANIC, < > (ombre dure, 12 px)
    ButtonStyle miniStyle(bool on) const;    // boutons « mini » des vues secondaires (11 px)
    ButtonStyle accentStyle() const;         // ENREGISTRER

    // potard (svg 84 de la maquette, mis à l'échelle sur size) ; renvoie true si la valeur a changé
    bool knob(Id id, Pt topLeft, float size, float& value, float modDepth, bool* began = nullptr, bool* ended = nullptr);
    void drawKnob(Pt topLeft, float size, float value, float modDepth, bool showMod);
    void drawKnobCap(Pt center, float radius, float value);   // disque moleté + repère
    void lcd(const std::string& s, float centerX, float top, bool large = false);
    void screw(Pt center, bool dark);
    void screws(const Rect& card, bool dark);
    // curseur horizontal 0..1 (hsTrack)
    bool hslider(Id id, const Rect& track, float& value, bool* began = nullptr, bool* ended = nullptr);
    // champ texte ; renvoie true si Entrée est pressée
    bool textField(Id id, const Rect& r, std::string& value, const std::string& placeholder, const Font& f, Color bg, Color fg, Color line, Color placeholderColor);

private:
    struct Blocker {
        Rect r;
        int layer;
    };
    Canvas* c_ = nullptr;
    const Theme* t_ = nullptr;
    Input in_;
    int layer_ = 0;
    std::vector<Blocker> blockers_;
    Id active_ = 0;
    bool activeSeen_ = false;
    Cursor cursor_ = Cursor::Arrow;
    float dragStartY_ = 0.f, dragStartX_ = 0.f, dragStartV_ = 0.f;
    size_t caret_ = 0;
    Id caretOwner_ = 0;  // champ auquel appartient caret_
    double caretT_ = 0.0;
};

// Majuscules UTF-8 (ASCII, lettres accentuées latines, œ) : text-transform: uppercase.
std::string upper(const std::string& s);
// Minuscules sans accents, pour la recherche de presets.
std::string foldForSearch(const std::string& s);
std::string fmt(const char* f, ...);

} // namespace raw2::ui
