#pragma once
// Lecture / écriture WAV maison (aucune bibliothèque) : PCM 8/16/24/32 bits et float32.
#include "dsp/Wavetable.h"

#include <string>
#include <vector>

namespace raw2 {

bool parseWav(const std::vector<unsigned char>& bytes, std::vector<float>& monoOut, double* sampleRate = nullptr);
// 1 ou 2 canaux entrelacés (un fichier à plus de 2 canaux garde les deux premiers)
bool parseWavMulti(const std::vector<unsigned char>& bytes, std::vector<float>& out, int& channels, double& sampleRate, size_t maxFrames);
bool readFile(const std::string& utf8Path, std::vector<unsigned char>& out);
bool writeFile(const std::string& utf8Path, const std::vector<unsigned char>& data);
// Export compatible Serum : 64 frames x 2048 points, mono 16 bits 44.1 kHz.
std::vector<unsigned char> wavetableToWav(const Wavetable& t);
std::string fileBaseName(const std::string& path);
// WAV stéréo 24 bits (export d'un son, glisser-déposer vers la DAW)
std::vector<unsigned char> stereoToWav24(const std::vector<float>& L, const std::vector<float>& R, int sampleRate);  // "C:\\x\\mon son.wav" -> "mon son"

} // namespace raw2
