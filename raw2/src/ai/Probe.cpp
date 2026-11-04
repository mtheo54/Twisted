#include "ai/Probe.h"
#include "dsp/Engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace raw2::ai {

namespace {
struct TimedNote { double t; int note; bool on; };

// Protocole : note tenue grave (C2), note tenue médium (C4), huit notes courtes (arpège).
std::vector<TimedNote> segmentNotes(int seg) {
    std::vector<TimedNote> v;
    if (seg < 2) {
        const int n = seg == 0 ? 36 : 60;
        v.push_back({0.0, n, true});
        v.push_back({0.9, n, false});
    } else {
        const int arp[8] = {60, 64, 67, 72, 67, 64, 60, 55};
        for (int k = 0; k < 8; ++k) {
            v.push_back({k * 0.125, arp[k], true});
            v.push_back({k * 0.125 + 0.075, arp[k], false});
        }
    }
    return v;
}
} // namespace

SoundSnapshot snapshotSound(Model& live) {
    SoundSnapshot s;
    s.preset = live.presetJson().dump();
    s.life = live.life.saveState();
    s.soundWorld = (int)live.life.soundWorld();
    s.valid = true;
    return s;
}

ProbeRenderer::ProbeRenderer() : m_(std::make_unique<Model>(Model::Mode::Offline)) {}
ProbeRenderer::~ProbeRenderer() = default;

void ProbeRenderer::load(const SoundSnapshot& s) {
    if (!s.valid) return;
    json::Value v;
    if (json::Value::parse(s.preset, v)) m_->applyPresetJson(v, nullptr);
    life_ = s.life;
    m_->life.setSoundWorld(s.soundWorld == 1 ? LifeEngine::Big : LifeEngine::Small);
}

ProbeResult ProbeRenderer::render(uint32_t seed) {
    ProbeResult r;
    if (!life_.empty()) m_->life.loadState(life_);
    m_->life.restart(seed);
    m_->mix.clear();  // l'écoute porte sur le son, pas sur les corrections de mixage
    const int block = 256;
    std::vector<float> L(block), R(block);
    float peak = 0.f;
    for (int seg = 0; seg < kProbeSegs; ++seg) {
        Engine e(*m_);
        e.prepare(kRate, block);
        SegmentAnalyzer an;
        an.reset(kRate);
        const auto notes = segmentNotes(seg);
        size_t next = 0;
        const long total = (long)(kSegSeconds * kRate);
        for (long pos = 0; pos < total; pos += block) {
            const int n = (int)std::min<long>(block, total - pos);
            NoteEvent ev[16];
            int nev = 0;
            while (next < notes.size() && nev < 16) {
                const long at = (long)std::lround(notes[next].t * kRate);
                if (at >= pos + n) break;
                ev[nev++] = NoteEvent{(int)std::max<long>(0, at - pos), notes[next].note, notes[next].on ? 0.8f : 0.f, notes[next].on};
                ++next;
            }
            e.process(L.data(), R.data(), n, ev, nev);
            an.push(L.data(), R.data(), (size_t)n);
            for (int i = 0; i < n; ++i) peak = std::max(peak, std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
        }
        an.compute(r.x.data() + seg * kSegDim);
        if (an.levelDb() > -75.f) r.silent = false;
    }
    r.peakDb = (float)(20.0 * std::log10(peak + 1e-9));
    return r;
}

} // namespace raw2::ai
