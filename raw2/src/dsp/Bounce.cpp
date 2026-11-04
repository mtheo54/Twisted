#include "dsp/Bounce.h"

#include "core/Wav.h"
#include "dsp/Engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace raw2 {

bool bounceSound(Model& live, const std::string& path, int note, double holdSeconds) {
    Model m(Model::Mode::Offline);
    if (!m.loadState(live.saveState(), nullptr)) return false;
    m.hostBpm.store(live.hostBpm.load());
    m.hostPlaying.store(false);
    m.mix.limit = live.mix.limit.load();
    constexpr int kSr = 48000, kBlock = 512;
    Engine e(m);
    e.prepare(kSr, kBlock);
    std::vector<float> L, R;
    float l[kBlock], r[kBlock];
    const long hold = (long)(std::clamp(holdSeconds, 0.05, 30.0) * kSr), maxLen = hold + 6L * kSr;
    double tail = 0.0;
    for (long pos = 0; pos < maxLen; pos += kBlock) {
        NoteEvent ev[1];
        int ne = 0;
        if (pos == 0) ev[ne++] = NoteEvent{0, note, 0.8f, true};
        if (pos <= hold && hold < pos + kBlock) ev[ne++] = NoteEvent{(int)(hold - pos), note, 0.f, false};
        e.process(l, r, kBlock, ev, ne);
        L.insert(L.end(), l, l + kBlock);
        R.insert(R.end(), r, r + kBlock);
        if (pos > hold) {  // fin de queue : 200 ms sous -80 dBFS
            float pk = 0.f;
            for (int i = 0; i < kBlock; ++i) pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
            tail = pk < 1e-4f ? tail + (double)kBlock / kSr : 0.0;
            if (tail >= 0.2) break;
        }
    }
    // latence du limiteur MIX IA : l'attaque commence au début du fichier
    const size_t lat = (size_t)std::max(0, e.latencySamples());
    if (lat < L.size()) {
        L.erase(L.begin(), L.begin() + (long)lat);
        R.erase(R.begin(), R.begin() + (long)lat);
    }
    // fondu de 5 ms à la fin
    const size_t fade = std::min<size_t>(L.size(), 240);
    for (size_t i = 0; i < fade; ++i) {
        const float g = (float)i / (float)fade;
        L[L.size() - 1 - i] *= g;
        R[R.size() - 1 - i] *= g;
    }
    return writeFile(path, stereoToWav24(L, R, kSr));
}

} // namespace raw2
