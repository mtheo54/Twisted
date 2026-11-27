#pragma once
// Fond personnalisé de l'interface (image, GIF animé ou vidéo) : décodé dans un fil à part, partagé
// entre les instances du plugin (même fichier = une seule copie en mémoire), joué en boucle.
#include "platform/Media.h"
#include "ui/Canvas.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace raw2::ui {

class Backdrop {
public:
    ~Backdrop();
    // Demande ce fichier en fond ("" : aucun). Sans effet si c'est déjà lui.
    // blur (0..1) : force du flou de la version « verre » (0 : pas de version floutée).
    void request(const std::string& path, float blur = 0.f);
    // Image à afficher à l'instant nowMs (ms) ; faux s'il n'y en a pas (encore).
    // keep garde les pixels en vie jusqu'à la fin de l'image dessinée.
    // blurOut (facultatif) reçoit la version floutée de la même image si elle existe.
    bool frame(double nowMs, ImageView& out, std::shared_ptr<const media::Frames>& keep, ImageView* blurOut = nullptr);
    bool loading() const { return busy_.load(); }
    std::string error() const;
    bool video() const;

private:
    void join();
    std::thread th_;
    std::atomic<bool> busy_{false};
    mutable std::mutex mu_;
    std::string path_, err_;
    int blurQ_ = 0;  // flou demandé (quantifié 0..10)
    std::shared_ptr<const media::Frames> cur_;
    unsigned long long serial_ = 0;  // change à chaque nouveau média (identifiants d'images uniques)
    double t0_ = -1.0;
};

} // namespace raw2::ui
