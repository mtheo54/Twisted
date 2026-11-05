// Banc de torture du moteur :
//  1. temps réel : aucune allocation pendant Engine::process (après le premier bloc de chaque son) ;
//  2. paramètres extrêmes, fréquences de 22 à 192 kHz, blocs de 1 à 4096 : sortie finie et bornée ;
//  3. queue de silence : pas d'explosion du coût (dénormaux) après relâche.
#include "core/FactoryData.h"
#include "core/Model.h"
#include "dsp/Engine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

using namespace raw2;

static std::atomic<bool> gWatch{false};
static thread_local bool tAudio = false;  // seules les allocations du fil audio comptent
static std::atomic<long> gAllocs{0};
void* operator new(size_t n) {
    if (tAudio && gWatch.load(std::memory_order_relaxed)) gAllocs.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

int main(int argc, char** argv) {
    tAudio = true;
    const int iters = argc > 1 ? std::atoi(argv[1]) : 150;
    std::mt19937 rng(77);
    auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    int fails = 0;

    // --- 1. allocations dans le fil audio ---------------------------------------------------------
    long worstAlloc = 0;
    int worstPreset = -1;
    for (int p = 0; p < kNumFactoryPresets; ++p) {
        Model m(Model::Mode::Live);  // comme dans le plugin : la vie et les tables ont leurs fils
        m.applyFactoryPreset(p, nullptr);
        m.params.set(kArpOn, p % 3 == 0 ? 1.f : 0.f);
        m.params.set(kChord, (float)(p % kNumChords));
        m.params.set(kUnison, (float)(1 + p % 8));
        m.params.set(kPolyMode, (float)(p % 3));
        Engine e(m);
        e.prepare(48000.0, 512);
        std::vector<float> L(512), R(512);
        long allocs = 0;
        for (int b = 0; b < 200; ++b) {
            NoteEvent ev[8];
            int n = 0;
            if (b % 20 == 0) for (int k = 0; k < 4; ++k) ev[n++] = NoteEvent{k * 7, 48 + k * 5, 0.9f, true};
            if (b % 20 == 12) for (int k = 0; k < 4; ++k) ev[n++] = NoteEvent{k, 48 + k * 5, 0.f, false};
            ParamEvent pe[3] = {{5, kCutoff, uni(0.f, 1.f)}, {100, kGlide, uni(0.f, 1.f)}, {300, kMaster, uni(0.2f, 1.f)}};
            if (b > 0) gWatch = true;
            gAllocs = 0;
            e.process(L.data(), R.data(), 512, ev, n, pe, 3);
            gWatch = false;
            allocs += gAllocs.load();
        }
        if (allocs > worstAlloc) { worstAlloc = allocs; worstPreset = p; }
    }
    std::printf("allocations dans process() : %ld au pire (preset %d)\n", worstAlloc, worstPreset + 1);
    if (worstAlloc > 0) ++fails;

    // --- 2. torture des paramètres ----------------------------------------------------------------
    const double rates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    const int blocks[] = {1, 17, 64, 512, 4096};
    float worstPeak = 0.f;
    int nanCount = 0;
    for (int it = 0; it < iters; ++it) {
        Model m(Model::Mode::Offline);
        m.applyFactoryPreset((int)(rng() % kNumFactoryPresets), nullptr);
        for (int k = 0; k < 40; ++k) {  // 40 paramètres au hasard, souvent à leurs bornes
            const uint32_t id = (uint32_t)(rng() % kNumParams);
            const ParamDef& d = paramDef(id);
            const int mode = (int)(rng() % 3);
            const float v = mode == 0 ? d.min : mode == 1 ? d.max : uni(d.min, d.max);
            m.params.set(id, d.steps > 0 ? std::round(v) : v);
        }
        const double sr = rates[rng() % 5];
        const int bs = blocks[rng() % 5];
        Engine e(m);
        e.prepare(sr, bs);
        std::vector<float> L((size_t)bs), R((size_t)bs);
        const int total = (int)(sr * 1.5);
        float peak = 0.f;
        bool nan = false;
        int next = 0;
        for (int done = 0; done < total; done += bs) {
            NoteEvent ev[4];
            int n = 0;
            if (done >= next) {
                ev[n++] = NoteEvent{0, (int)(12 + rng() % 100), uni(0.01f, 1.f), true};
                ev[n++] = NoteEvent{0, (int)(12 + rng() % 100), 0.f, false};
                next = done + (int)(sr * 0.13);
            }
            e.process(L.data(), R.data(), bs, ev, n);
            for (int i = 0; i < bs; ++i) {
                if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) nan = true;
                else peak = std::max(peak, std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
            }
        }
        if (nan) ++nanCount;
        worstPeak = std::max(worstPeak, peak);
        if (nan || peak > 2.f) {
            std::printf("  essai %d : %s crête %.2f (sr %.0f, bloc %d)\n", it, nan ? "NaN/Inf" : "", peak, sr, bs);
            ++fails;
        }
    }
    std::printf("paramètres extrêmes : %d essais, %d avec NaN/Inf, crête max %.3f\n", iters, nanCount, worstPeak);

    // --- 3. coût de la queue silencieuse (dénormaux) ----------------------------------------------
    {
        Model m(Model::Mode::Offline);
        m.applyFactoryPreset(38, nullptr);  // pad à réverbe
        Engine e(m);
        e.prepare(48000.0, 256);
        std::vector<float> L(256), R(256);
        auto blockCost = [&](int nblocks, bool note) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < nblocks; ++b) {
                NoteEvent ev{0, 60, 0.8f, note && b == 0};
                e.process(L.data(), R.data(), 256, &ev, note && b == 0 ? 1 : 0);
            }
            return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / nblocks;
        };
        const double play = blockCost(400, true);
        NoteEvent off{0, 60, 0.f, false};
        e.process(L.data(), R.data(), 256, &off, 1);
        (void)blockCost(1800, false);  // ~10 s de queue
        const double tail = blockCost(400, false);
        std::printf("coût d'un bloc : %.1f µs en jeu, %.1f µs après 10 s de silence\n", play, tail);
        if (tail > play * 3.0) ++fails;
    }
    std::printf(fails ? "torture : %d problème(s)\n" : "torture : rien à signaler\n", fails);
    return fails ? 1 : 0;
}
