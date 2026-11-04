#pragma once
// Vue Cocoa (NSView) qui affiche l'éditeur avec CoreGraphics, à 60 images par seconde.
// Utilisée par la vue VST3 (kPlatformTypeNSView) et par la vue de l'Audio Unit. Même interface
// que Win32View, pour que l'adaptateur VST3 reste commun aux deux systèmes.
//
// Ce fichier est compilé dans chaque plugin (VST3, AU) avec son propre suffixe de classes
// Objective-C (RAW2_OBJC_SUFFIX) : deux binaires chargés par le même hôte ne se gênent jamais.
#include "core/Model.h"
#include "ui/Editor.h"

#include <memory>

namespace raw2 {

class MacView {
public:
    struct Host {
        virtual ~Host() = default;
        virtual void requestResize(int width, int height) = 0;  // points (pas pixels)
    };

    MacView(Model& model, ParamSink& sink, ui::EditorState& state, Host* host);
    ~MacView();

    bool attach(void* parentNSView);  // ajoute la vue dans parentNSView
    void detach();
    void setContentScale(float) {}    // macOS : l'échelle Retina est gérée par le système
    void preferredSize(int& w, int& h) const;
    void setSize(int w, int h);
    // touches transmises par l'hôte (IPlugView::onKeyDown, codes virtuels Windows) ; true si consommée
    bool hostKey(char16_t ch, int virtualKey);
    void* nsview() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace raw2
