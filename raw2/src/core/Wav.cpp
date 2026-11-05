#include "core/Wav.h"
#include "core/Files.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace raw2 {
namespace {
uint32_t u32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t u16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
void put32(std::vector<unsigned char>& v, size_t o, uint32_t x) { for (int i = 0; i < 4; ++i) v[o + i] = (unsigned char)(x >> (8 * i)); }
void put16(std::vector<unsigned char>& v, size_t o, uint16_t x) { v[o] = (unsigned char)x; v[o + 1] = (unsigned char)(x >> 8); }
} // namespace

// Lecture commune : échantillons entrelacés (1 ou 2 canaux gardés), fréquence d'échantillonnage.
bool parseWavMulti(const std::vector<unsigned char>& b, std::vector<float>& out, int& channels, double& sampleRate, size_t maxFrames) {
    out.clear();
    channels = 0;
    const size_t size = b.size();
    if (size < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return false;
    size_t p = 12, off = 0, len = 0;
    int tag = 0, ch = 0, bits = 0;
    uint32_t sr = 44100;
    bool haveFmt = false, haveData = false;
    while (p + 8 <= size) {
        const uint32_t sz = u32(b.data() + p + 4);
        if (std::memcmp(b.data() + p, "fmt ", 4) == 0 && p + 24 <= size) {
            tag = u16(b.data() + p + 8);
            ch = u16(b.data() + p + 10);
            bits = u16(b.data() + p + 22);
            sr = u32(b.data() + p + 12);
            if (tag == 0xFFFE && p + 34 <= size) tag = u16(b.data() + p + 32);  // WAVE_FORMAT_EXTENSIBLE
            haveFmt = true;
        } else if (std::memcmp(b.data() + p, "data", 4) == 0) {
            off = p + 8;
            len = std::min<size_t>(sz, size - off);
            haveData = true;
            break;
        }
        p += 8 + (size_t)sz + (sz & 1);
    }
    if (!haveFmt || !haveData || ch <= 0 || (bits != 8 && bits != 16 && bits != 24 && bits != 32)) return false;
    sampleRate = sr >= 1000 && sr <= 768000 ? (double)sr : 44100.0;
    const size_t bps = (size_t)bits / 8;
    const size_t n = std::min<size_t>(maxFrames, len / (bps * (size_t)ch));
    const int keep = std::min(ch, 2);
    out.resize(n * (size_t)keep);
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < keep; ++c) {
            const unsigned char* q = b.data() + off + (i * (size_t)ch + (size_t)c) * bps;
            double v = 0.0;
            if (tag == 3 && bits == 32) { float f; std::memcpy(&f, q, 4); v = f; }
            else if (bits == 8) v = ((int)q[0] - 128) / 128.0;
            else if (bits == 16) v = (int16_t)u16(q) / 32768.0;
            else if (bits == 24) { int32_t x = q[0] | (q[1] << 8) | (q[2] << 16); if (x & 0x800000) x -= 0x1000000; v = x / 8388608.0; }
            else if (bits == 32) v = (int32_t)u32(q) / 2147483648.0;
            out[i * (size_t)keep + (size_t)c] = std::isfinite(v) ? (float)std::clamp(v, -4.0, 4.0) : 0.f;  // float hors norme borné
        }
    channels = keep;
    return n > 0;
}

bool parseWav(const std::vector<unsigned char>& b, std::vector<float>& out, double* sampleRate) {
    std::vector<float> inter;
    int ch = 0;
    double sr = 44100.0;
    out.clear();
    if (!parseWavMulti(b, inter, ch, sr, 4000000)) return false;
    if (sampleRate) *sampleRate = sr;
    const size_t n = inter.size() / (size_t)ch;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = ch == 2 ? 0.5f * (inter[2 * i] + inter[2 * i + 1]) : inter[i];
    return n > 0;
}

std::vector<unsigned char> wavetableToWav(const Wavetable& t) {
    const size_t L = 2048, n = (size_t)kFrames * L;
    std::vector<unsigned char> v(44 + n * 2, 0);
    std::memcpy(&v[0], "RIFF", 4); put32(v, 4, (uint32_t)(36 + n * 2));
    std::memcpy(&v[8], "WAVEfmt ", 8); put32(v, 16, 16); put16(v, 20, 1); put16(v, 22, 1);
    put32(v, 24, 44100); put32(v, 28, 88200); put16(v, 32, 2); put16(v, 34, 16);
    std::memcpy(&v[36], "data", 4); put32(v, 40, (uint32_t)(n * 2));
    for (int k = 0; k < kFrames; ++k)
        for (size_t j = 0; j < L; ++j) {
            const double f = (double)j / L * 256.0;
            const int i = (int)f;
            const double fr = f - i;
            const double s = t.fr[k][i & 255] * (1.0 - fr) + t.fr[k][(i + 1) & 255] * fr;
            const long q = std::clamp(std::lround(s * 32767.0), -32767L, 32767L);
            put16(v, 44 + ((size_t)k * L + j) * 2, (uint16_t)(int16_t)q);
        }
    return v;
}

std::vector<unsigned char> stereoToWav24(const std::vector<float>& L, const std::vector<float>& R, int sr) {
    const size_t n = std::min(L.size(), R.size()), bytes = n * 6;
    std::vector<unsigned char> v(44 + bytes, 0);
    std::memcpy(&v[0], "RIFF", 4); put32(v, 4, (uint32_t)(36 + bytes));
    std::memcpy(&v[8], "WAVEfmt ", 8); put32(v, 16, 16); put16(v, 20, 1); put16(v, 22, 2);
    put32(v, 24, (uint32_t)sr); put32(v, 28, (uint32_t)sr * 6u); put16(v, 32, 6); put16(v, 34, 24);
    std::memcpy(&v[36], "data", 4); put32(v, 40, (uint32_t)bytes);
    size_t o = 44;
    for (size_t i = 0; i < n; ++i)
        for (float x : {L[i], R[i]}) {
            const long q = std::clamp(std::lround((double)x * 8388607.0), -8388607L, 8388607L);
            const uint32_t u = (uint32_t)q;
            v[o++] = (unsigned char)(u & 0xFF);
            v[o++] = (unsigned char)((u >> 8) & 0xFF);
            v[o++] = (unsigned char)((u >> 16) & 0xFF);
        }
    return v;
}

std::string fileBaseName(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    std::string name = s == std::string::npos ? path : path.substr(s + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

bool readFile(const std::string& path, std::vector<unsigned char>& out) { return files::read(path, out); }
bool writeFile(const std::string& path, const std::vector<unsigned char>& data) { return files::write(path, data); }

} // namespace raw2
