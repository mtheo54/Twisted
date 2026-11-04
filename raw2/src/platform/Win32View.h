#pragma once
// Fenêtre enfant Win32 qui affiche l'éditeur avec Direct2D, à 60 images par seconde.
// Utilisée par la vue VST3 et par l'appli de démonstration RAW2Preview.
#include "core/Model.h"
#include "ui/Editor.h"

#include <memory>
#include <string>

namespace raw2 {

class Win32View {
public:
    struct Host {
        virtual ~Host() = default;
        virtual void requestResize(int width, int height) = 0;  // pixels physiques
    };

    Win32View(Model& model, ParamSink& sink, ui::EditorState& state, Host* host);
    ~Win32View();

    bool attach(void* parentHwnd);
    void detach();
    void setContentScale(float s);  // facteur fourni par l'hôte (DPI)
    void preferredSize(int& w, int& h) const;
    void setSize(int w, int h);
    // touches transmises par l'hôte (IPlugView::onKeyDown) ; true si consommée
    bool hostKey(char16_t ch, int virtualKey);
    void* hwnd() const { return hwnd_; }

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    void* hwnd_ = nullptr;
};

} // namespace raw2
