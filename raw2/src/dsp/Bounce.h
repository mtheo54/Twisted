#pragma once
// Rendu hors ligne du son en cours dans un WAV (glisser-déposer vers la DAW) : le preset, la
// chaîne MIX IA et l'état de la vie sont copiés dans un modèle hors ligne, la note est tenue
// holdSeconds puis relâchée jusqu'au silence (6 s au plus). Accords et arpège compris.
#include "core/Model.h"

#include <string>

namespace raw2 {

bool bounceSound(Model& live, const std::string& path, int note, double holdSeconds);

} // namespace raw2
