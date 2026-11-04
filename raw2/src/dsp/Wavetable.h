#pragma once
// Tables d'onde : 64 frames x 256 points (format d'édition, §7.1) et leur version
// limitée en bande pour la lecture audio (mipmaps, une par octave).
#include "core/FactoryData.h"

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace raw2 {

constexpr int kFrames = 64;
constexpr int kPoints = 256;

using Frame = std::array<float, kPoints>;

struct Wavetable {
    std::string name;
    std::array<Frame, kFrames> fr{};
    // Forme d'origine si la table sort intacte d'un preset (les fichiers restent compacts).
    std::optional<ShapeSpec> spec;
};

// Formes de base de la maquette (genShape) : u dans [0, 1), m = morph dans [0, 1].
float genShape(Shape s, float u, float m);
void normFrame(Frame& a);  // retire le continu, crête à 1
void buildFromSpec(Wavetable& t, const ShapeSpec& spec, const std::string& name);
// Oscillateur additif de secours (8 partielles) quand aucune table n'est montée.
void buildFallback(Wavetable& t, float brightness);
float sampleTable(const Wavetable& t, float pos, float u);  // lecture interpolée (affichage)
const char* shapeKey(Shape s);
bool shapeFromKey(const char* key, Shape& out);

// Version audio : niveaux [level][frame][kMipLen + 4], un niveau par demi-octave.
// Le niveau L garde kMipHarmonics[L] harmoniques. Les formes calculées (preset d'usine) sont
// rendues à 4096 points pour remplir les niveaux riches (jusqu'à 511 harmoniques, notes graves) ;
// une table éditée (256 points) s'arrête à 127 harmoniques.
constexpr int kMipLevels = 18;
constexpr int kMipLen = 2048;
constexpr int kMipStride = kMipLen + 4;  // un point de garde avant, trois après (lecture Hermite)
constexpr int kMipHarmonics[kMipLevels] = {511, 362, 256, 181, 127, 90, 63, 45, 32, 22, 16, 11, 8, 6, 4, 3, 2, 1};
constexpr int kMipHermiteBelow = 4;      // niveaux 0..3 (plus de 127 harmoniques) : interpolation cubique

class MipTable {
public:
    void build(const Wavetable& t);
    // Recalcule seulement les frames marquées (copie d'une table existante).
    void rebuildFrames(const Wavetable& t, const std::array<bool, kFrames>& dirty);
    const float* frame(int level, int f) const { return data_.data() + ((size_t)level * kFrames + (size_t)f) * kMipStride + 1; }
    // Niveau le plus riche dont les harmoniques repliées restent au-dessus de 20 kHz :
    // limite 0.54 * fs (à 44.1 kHz, une harmonique à 0.54 fs se replie vers 20.3 kHz).
    static int levelFor(float phaseInc) {
        int level = 0;
        while (level < kMipLevels - 1 && (float)kMipHarmonics[level] * phaseInc > 0.54f) ++level;
        return level;
    }
    // Niveau et dosage vers le niveau suivant (moins riche) : le timbre s'appauvrit en continu
    // quand la note monte, au lieu de sauter d'une demi-octave d'harmoniques d'un coup.
    static int levelMix(float phaseInc, float& mix) {
        const int L = levelFor(phaseInc);
        mix = 0.f;
        if (L >= kMipLevels - 1 || phaseInc <= 0.f) return L;
        const float hi = 0.54f / (float)kMipHarmonics[L];                          // L n'est plus permis
        const float lo = L > 0 ? 0.54f / (float)kMipHarmonics[L - 1] : hi * 0.7071f;  // L devient permis
        const float t = std::log(phaseInc / lo) / std::log(hi / lo);
        mix = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        return L;
    }
    // Lecture : position (0..1) entre frames, phase (0..1).
    inline float read(int level, float pos, float phase) const {
        const float fp = pos * (kFrames - 1);
        int i0 = (int)fp;
        if (i0 < 0) i0 = 0;
        if (i0 > kFrames - 2) i0 = kFrames - 2;
        const float fz = fp - (float)i0;
        const float x = phase * kMipLen;
        int k = (int)x;
        if (k < 0) k = 0;
        if (k >= kMipLen) k = kMipLen - 1;
        const float fr = x - (float)k;
        const float* A = frame(level, i0);
        const float* B = A + kMipStride;
        float a, b;
        if (level < kMipHermiteBelow) {
            a = hermite(A + k, fr);
            b = hermite(B + k, fr);
        } else {
            a = A[k] + (A[k + 1] - A[k]) * fr;
            b = B[k] + (B[k + 1] - B[k]) * fr;
        }
        return a + (b - a) * fz;
    }
    // lecture fondue entre level et level + 1 (mix 0..1)
    inline float readMix(int level, float mix, float pos, float phase) const {
        const float a = read(level, pos, phase);
        if (mix <= 0.0005f || level >= kMipLevels - 1) return a;
        return a + (read(level + 1, pos, phase) - a) * mix;
    }

private:
    static float hermite(const float* p, float f) {  // p[-1..2]
        const float c1 = 0.5f * (p[1] - p[-1]);
        const float c2 = p[-1] - 2.5f * p[0] + 2.f * p[1] - 0.5f * p[2];
        const float c3 = 0.5f * (p[2] - p[-1]) + 1.5f * (p[0] - p[1]);
        return ((c3 * f + c2) * f + c1) * f + p[0];
    }
    void buildFrame(const Wavetable& t, int f);
    std::vector<float> data_ = std::vector<float>((size_t)kMipLevels * kFrames * kMipStride, 0.f);
};

} // namespace raw2
