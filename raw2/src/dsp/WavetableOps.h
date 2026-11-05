#pragma once
// Outils de l'éditeur WAVETABLE (§7.5) et conversion d'un fichier audio en table (§7.6).
#include "dsp/Wavetable.h"

#include <string>
#include <vector>

namespace raw2 {

void smoothFrame(Frame& a, float amount);        // LISSER : 1 + 6*amount passes [1 2 1]/4
void distortFrame(Frame& a, float amount);       // DISTORDRE : repliement harmonique
void blendBetweenFrames(Wavetable& t, const std::vector<int>& idx, float amount);  // FONDRE
void interpolateTable(Wavetable& t);             // INTERP. 01 -> dernière frame
void resizeTable(Wavetable& t, int frames);       // nouveau nombre de frames (1..256), interpolées
void applyPresetShape(Wavetable& t, Shape s, const std::vector<int>& idx, bool wholeTable);

// Outils au pinceau sur l'écran 2D (x = 0..255, v = -1..1).
void penStroke(Frame& a, int x0, float v0, int x1, float v1);
void penSmooth(Frame& a, int x);
void penDistort(Frame& a, int x, float amount);

// Audio -> table. Retourne false si le fichier est trop court.
bool singleCycleFrom(const std::vector<float>& data, std::vector<float>& tab512);  // tableFrom()
bool tableFromAudio(const std::vector<float>& data, const std::string& baseName, Wavetable& out);  // framesFrom()
// Import complet. mode : kImpAuto (table toute faite si le fichier est fait de frames de 2048
// points, sinon cycles), kImpCycles (un cycle aligné par frame, hauteur détectée), kImpSpectral
// (resynthèse : amplitudes des harmoniques de chaque frame, phases communes). frames : nombre de
// frames voulu pour un son quelconque. Renvoie la méthode employée (kImpTable, kImpCycles,
// kImpSpectral, kImpSlices : découpe régulière d'un son sans hauteur), 0 si le son est trop court.
enum ImportMode { kImpAuto = 0, kImpCycles, kImpSpectral, kImpTable, kImpSlices };
int tableFromAudioEx(const std::vector<float>& data, double sr, const std::string& baseName, int mode, int frames, Wavetable& out);
double detectPeriod(const std::vector<float>& x, double sr, size_t at);  // période en échantillons, 0 si aucune

} // namespace raw2
