// Fil audio + fil « interface » en même temps, comme dans une DAW : le son tourne pendant qu'on
// change de preset d'usine, de preset de pack, d'état complet, de paramètres et de routes.
// À compiler avec -fsanitize=thread.
#include "core/FactoryData.h"
#include "core/Model.h"
#include "dsp/Engine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace raw2;

struct Sink : ParamSink {
    Model& m;
    explicit Sink(Model& mm) : m(mm) {}
    void beginEdit(uint32_t) override {}
    void performEdit(uint32_t id, float v) override { m.params.set(id, v); }
    void endEdit(uint32_t) override {}
};

int main(int argc, char** argv) {
    if (argc > 1) setenv("HOME", argv[1], 1);
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 20;
    Model m(Model::Mode::Live);
    m.instruments.scan();
    m.scanPacks();
    Engine e(m);
    e.prepare(48000.0, 256);
    std::atomic<bool> run{true};
    std::atomic<long> blocks{0};
    bool bad = false;
    std::thread audio([&] {
        std::vector<float> L(256), R(256);
        int b = 0;
        while (run.load()) {
            NoteEvent ev[2];
            int n = 0;
            if (b % 40 == 0) ev[n++] = NoteEvent{0, 48 + (b / 40) % 24, 0.9f, true};
            if (b % 40 == 30) ev[n++] = NoteEvent{0, 48 + (b / 40) % 24, 0.f, false};
            m.hostBpm.store(120.f);
            m.hostPpq.store(b * 256.0 / 48000.0 * 2.0);
            m.hostPlaying.store(true);
            e.process(L.data(), R.data(), 256, ev, n);
            for (float x : L)
                if (!std::isfinite(x)) bad = true;
            ++b;
            blocks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    Sink sink(m);
    const auto t0 = std::chrono::steady_clock::now();
    int k = 0;
    const std::string state = m.saveState();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(seconds)) {
        switch (k % 6) {
            case 0: m.applyFactoryPreset(k % kNumFactoryPresets, &sink); break;
            case 1:
                if (!m.packs.empty() && !m.packs[0].presets.empty()) m.applyPackPreset(0, k % (int)m.packs[0].presets.size(), &sink);
                break;
            case 2: m.loadState(state, &sink); break;
            case 3:
                for (uint32_t id = 0; id < kNumParams; id += 7) sink.performEdit(id, paramDef(id).min + (paramDef(id).max - paramDef(id).min) * 0.37f);
                break;
            case 4: m.applyInit(&sink); break;
            default: (void)m.saveState(); break;
        }
        ++k;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    run = false;
    audio.join();
    std::printf("course : %d changements, %ld blocs audio, %s\n", k, blocks.load(), bad ? "NaN dans la sortie !" : "sortie saine");
    return bad ? 1 : 0;
}
