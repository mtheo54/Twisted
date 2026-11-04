// raw2_train : entraîne les deux réseaux de l'IA de RAW 2 et écrit leurs poids en C++.
//
//   raw2_train [--out src/ai/AiWeights.cpp] [--report docs/IA.md] [--threads N] [--quick]
//
// Les données sont produites par le moteur du plugin lui-même (rendu hors ligne) :
//  - RoleNet : les 72 presets d'usine, chacun avec de petites variations, écoutés par le protocole
//    interne (note grave, note médium, arpège). Étiquette = catégorie du preset.
//  - MixNet : des boucles jouées par ces presets, auxquelles on ajoute au hasard des défauts
//    connus (de 3 à 8 dB : bosse à 250 Hz, aigus éteints, graves creusés, dureté à 3.5 kHz, volume qui fait du
//    yo-yo, crêtes trop hautes). Le réseau apprend à retrouver le défaut et son dosage.
// La validation se fait sur des presets mis de côté, jamais vus pendant l'apprentissage.
// Tout est déterministe (graines fixes) : relancer donne les mêmes données.
#include "ai/AiWeights.h"
#include "ai/Features.h"
#include "ai/Probe.h"
#include "core/FactoryData.h"
#include "core/Model.h"
#include "dsp/Engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace raw2;
using namespace raw2::ai;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// --- hasard déterministe ------------------------------------------------------------------------
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { next(); next(); }
    uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    double uni() { return (double)(next() >> 11) / 9007199254740992.0; }
    double range(double a, double b) { return a + (b - a) * uni(); }
    int irange(int a, int b) { return a + (int)(uni() * (b - a + 1)); }  // [a, b]
    bool chance(double p) { return uni() < p; }
    double gauss() {
        const double u = std::max(1e-12, uni()), v = uni();
        return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * v);
    }
};

// catégorie d'usine -> rôle (0 BASSE, 1 LEAD, 2 PAD, 3 ARP / SEQ, 4 TEXTURE)
int roleOf(Category c) {
    switch (c) {
        case Category::Bass: return 0;
        case Category::Lead: return 1;
        case Category::Pad: return 2;
        case Category::Pluck:
        case Category::Arp: return 3;
        case Category::Fx: return 4;
    }
    return 1;
}
const char* kRoleNames[kNumRoles] = {"BASSE", "LEAD", "PAD", "ARP / SEQ", "TEXTURE"};
const char* kMixNames[kNumMixOut] = {"EQ 250 Hz", "COMP", "SHELF 8 kHz", "EQ 80 Hz", "EQ 3.5 kHz", "LIMITEUR"};

// presets mis de côté pour la validation : environ 1 sur 5 dans chaque catégorie
std::vector<bool> heldOutPresets() {
    std::vector<bool> h((size_t)kNumFactoryPresets, false);
    int rank[6] = {};
    for (int i = 0; i < kNumFactoryPresets; ++i) {
        const int c = (int)kFactoryPresets[i].category;
        if (rank[c]++ % 5 == 2) h[(size_t)i] = true;
    }
    return h;
}

// variations autour d'un preset (le son reste dans son rôle)
void loadVariant(Model& m, int preset, Rng& r, bool perturb) {
    m.params.resetDefaults();
    m.life.restart((uint32_t)(r.next() & 0xFFFFFFFF) | 1u);
    m.applyFactoryPreset(preset, nullptr);
    if (!perturb) return;
    auto nudge = [&](uint32_t id, double amt) { m.params.set(id, (float)(m.params.get(id) + r.range(-amt, amt))); };
    nudge(kAPos, 0.15); nudge(kBPos, 0.15);
    nudge(kALvl, 0.08); nudge(kBLvl, 0.08);
    nudge(kCutoff, 0.06); nudge(kReso, 0.10);
    nudge(kAttack, 0.05); nudge(kDecay, 0.06); nudge(kSustain, 0.08); nudge(kRelease, 0.06);
    nudge(kM1, 20.0); nudge(kM2, 20.0); nudge(kM3, 12.0);
    nudge(kSpeed, 0.10); nudge(kBirth, 0.10); nudge(kQty, 0.12);
    nudge(kMaster, 0.05);
    if (r.chance(0.15)) m.params.set(kRule, m.params.get(kRule) > 0.5f ? 0.f : 1.f);
    nudge(kAWarp, 0.10); nudge(kBWarp, 0.10);
    // enveloppe B et LFO (page ENV / LFO)
    nudge(kEnvBAttack, 0.05); nudge(kEnvBDecay, 0.06); nudge(kEnvBSustain, 0.08); nudge(kEnvBRelease, 0.06);
    nudge(kLfo1Rate, 0.10); nudge(kLfo2Rate, 0.10);
    // effets : réglages et dosages bougés, parfois un effet de plus ou de moins, parfois moins de traité
    for (int f = 0; f < kNumFx; ++f) {
        nudge(fxParam(f, kFxMix), 0.15);
        for (int k = 0; k < 3; ++k) nudge(fxParam(f, kFxP0 + k), 0.10);
    }
    if (r.chance(0.15)) {
        const uint32_t on = fxParam((int)(r.next() % (uint64_t)kNumFx), kFxOn);
        m.params.set(on, m.params.get(on) > 0.5f ? 0.f : 1.f);
    }
    if (r.chance(0.15)) m.params.set(kFxWet, (float)r.range(0.0, 1.0));
    // voix (enveloppes, LFO, filtre, matrice) d'un autre preset de la même catégorie : le réseau
    // apprend ce qui fait le rôle plutôt que la voix exacte de chaque preset
    if (r.chance(0.33)) {
        int j = preset;
        for (int tries = 0; tries < 20 && j == preset; ++tries) {
            const int c = (int)(r.next() % (uint64_t)kNumFactoryPresets);
            if (kFactoryPresets[c].category == kFactoryPresets[preset].category) j = c;
        }
        m.applyFactoryVoice(j, nullptr);
    }
    // chaque preset a son propre rack : un tiers des variantes reçoit le rack d'un autre preset tiré
    // au hasard (toutes catégories), pour que le réseau reconnaisse le rôle au son et pas aux effets
    if (r.chance(0.33)) {
        const float master = m.params.get(kMaster);
        m.applyFactoryFx((int)(r.next() % (uint64_t)kNumFactoryPresets), nullptr);
        m.params.set(kMaster, master);
    }
    m.life.reset(LifeEngine::Small);
}

// --- boucles jouées pour MixNet ------------------------------------------------------------------
struct TimedNote { double t; int note; float vel; bool on; };

std::vector<TimedNote> pattern(int kind, Rng& r, double dur) {
    std::vector<TimedNote> v;
    auto note = [&](double t, double len, int n, float vel) {
        if (t >= dur) return;
        v.push_back({t, n, vel, true});
        v.push_back({std::min(dur - 0.01, t + len), n, 0.f, false});
    };
    const double bpm = r.range(85, 140), beat = 60.0 / bpm;
    const float v0 = (float)r.range(0.6, 0.8), vr = (float)r.range(0.05, 0.25);
    auto vel = [&] { return std::clamp(v0 + (float)r.range(-vr, vr), 0.2f, 1.f); };
    static const int penta[5] = {0, 3, 5, 7, 10};
    switch (kind) {
        case 0: {  // ligne de basse en croches
            const int root = r.irange(33, 45);
            const int offs[6] = {0, 0, 7, 12, 5, 3};
            const double gate = r.range(0.5, 0.95);
            for (double t = 0; t < dur; t += beat / 2) note(t, beat / 2 * gate, root + offs[r.irange(0, 5)], vel());
            break;
        }
        case 1: {  // mélodie
            const int root = r.irange(57, 69);
            double t = 0;
            while (t < dur) {
                const double len = beat * (r.chance(0.5) ? 0.5 : 1.0);
                note(t, len * r.range(0.8, 1.0), root + penta[r.irange(0, 4)] + (r.chance(0.3) ? 12 : 0), vel());
                t += len;
            }
            break;
        }
        case 2: {  // accords tenus
            const int root = r.irange(48, 60);
            const int third = r.chance(0.5) ? 3 : 4, top = r.chance(0.5) ? 10 : (r.chance(0.5) ? 11 : 12);
            const double split = r.chance(0.5) ? dur : r.range(1.2, 1.8);
            for (int n : {root, root + third, root + 7, root + top}) note(0.0, split - 0.05, n, vel());
            if (split < dur)
                for (int n : {root + 5, root + 5 + third, root + 12}) note(split, dur - split - 0.2, n, vel());
            break;
        }
        case 3: {  // arpège en doubles croches
            const int root = r.irange(52, 64);
            const int tones[6] = {0, 4, 7, 12, 16, 19};
            const double gate = r.range(0.3, 0.7);
            int k = 0;
            for (double t = 0; t < dur; t += beat / 4, ++k) note(t, beat / 4 * gate, root + tones[(k * (r.chance(0.2) ? 2 : 1)) % 6], vel());
            break;
        }
        default: {  // notes longues isolées
            double t = 0;
            while (t < dur) {
                const double len = r.range(0.8, 2.0);
                note(t, len * 0.85, r.irange(48, 74), vel());
                t += len;
            }
            break;
        }
    }
    std::stable_sort(v.begin(), v.end(), [](const TimedNote& a, const TimedNote& b) { return a.t < b.t; });
    return v;
}

void renderClip(Model& m, const std::vector<TimedNote>& notes, double dur, std::vector<float>& L, std::vector<float>& R) {
    Engine e(m);
    e.prepare(kSr, 256);
    m.mix.clear();
    const long total = (long)(dur * kSr);
    L.assign((size_t)total, 0.f);
    R.assign((size_t)total, 0.f);
    size_t next = 0;
    for (long pos = 0; pos < total; pos += 256) {
        const int n = (int)std::min<long>(256, total - pos);
        NoteEvent ev[32];
        int nev = 0;
        while (next < notes.size() && nev < 32) {
            const long at = (long)std::lround(notes[next].t * kSr);
            if (at >= pos + n) break;
            ev[nev++] = NoteEvent{(int)std::max<long>(0, at - pos), notes[next].note, notes[next].vel, notes[next].on};
            ++next;
        }
        e.process(L.data() + pos, R.data() + pos, n, ev, nev);
    }
}

// --- filtres de dégradation (mêmes formules que la chaîne MIX IA du moteur) -----------------------
struct Bq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    void apply(std::vector<float>& x) const {
        double z1 = 0, z2 = 0;
        for (float& s : x) {
            const double y = b0 * s + z1;
            z1 = b1 * s - a1 * y + z2;
            z2 = b2 * s - a2 * y;
            s = (float)y;
        }
    }
};
Bq peaking(double f, double dbg, double q) {
    const double A = std::pow(10.0, dbg / 40.0), w = 2 * kPi * f / kSr, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al / A;
    return {(1 + al * A) / a0, -2 * c / a0, (1 - al * A) / a0, -2 * c / a0, (1 - al / A) / a0};
}
Bq highShelf(double f, double dbg) {
    const double A = std::pow(10.0, dbg / 40.0), w = 2 * kPi * f / kSr, c = std::cos(w), s = std::sin(w);
    const double al = s / 2 * std::sqrt(2.0), sq = 2 * std::sqrt(A) * al, a0 = (A + 1) - (A - 1) * c + sq;
    return {A * ((A + 1) + (A - 1) * c + sq) / a0, -2 * A * ((A - 1) + (A + 1) * c) / a0, A * ((A + 1) + (A - 1) * c - sq) / a0,
            2 * ((A - 1) - (A + 1) * c) / a0, ((A + 1) - (A - 1) * c - sq) / a0};
}
float softSafety(float x) {
    const float a = std::fabs(x);
    if (a < 0.95f) return x;
    const float y = 0.95f + 0.05f * std::tanh((a - 0.95f) / 0.05f);
    return x < 0.f ? -y : y;
}

// --- données -------------------------------------------------------------------------------------
struct Example {
    std::vector<float> x;
    int cls = 0;                       // RoleNet
    float t[kNumMixOut] = {};          // MixNet : besoin de chaque correction
    float amt[kNumMixEq] = {};         // MixNet : dosage (dB, valeur absolue)
    int preset = 0;
    bool val = false;
};

// cache des données (pour réessayer l'entraînement sans tout re-rendre)
bool saveData(const std::string& path, const std::vector<Example>& d) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t n = (uint32_t)d.size(), dim = d.empty() ? 0u : (uint32_t)d[0].x.size();
    std::fwrite(&n, 4, 1, f);
    std::fwrite(&dim, 4, 1, f);
    for (const auto& e : d) {
        std::fwrite(e.x.data(), sizeof(float), dim, f);
        std::fwrite(&e.cls, sizeof e.cls, 1, f);
        std::fwrite(e.t, sizeof e.t, 1, f);
        std::fwrite(e.amt, sizeof e.amt, 1, f);
        std::fwrite(&e.preset, sizeof e.preset, 1, f);
    }
    std::fclose(f);
    return true;
}
bool loadData(const std::string& path, std::vector<Example>& d, uint32_t wantN, uint32_t wantDim, const std::vector<bool>& held) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint32_t n = 0, dim = 0;
    bool ok = std::fread(&n, 4, 1, f) == 1 && std::fread(&dim, 4, 1, f) == 1 && n == wantN && dim == wantDim;
    if (ok) {
        d.assign(n, Example{});
        for (auto& e : d) {
            e.x.resize(dim);
            ok = ok && std::fread(e.x.data(), sizeof(float), dim, f) == dim && std::fread(&e.cls, sizeof e.cls, 1, f) == 1 && std::fread(e.t, sizeof e.t, 1, f) == 1 &&
                 std::fread(e.amt, sizeof e.amt, 1, f) == 1 && std::fread(&e.preset, sizeof e.preset, 1, f) == 1;
            if (ok) e.val = held[(size_t)e.preset];
        }
    }
    std::fclose(f);
    return ok;
}

void parallelFor(int n, int threads, const std::function<void(int, int)>& fn) {
    std::atomic<int> next{0}, done{0};
    std::vector<std::thread> th;
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < threads; ++k)
        th.emplace_back([&, k] {
            for (int i; (i = next.fetch_add(1)) < n;) {
                fn(k, i);
                const int d = done.fetch_add(1) + 1;
                if (d % 200 == 0 || d == n) {
                    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    std::fprintf(stderr, "\r  %d / %d  (%.0f s)", d, n, s);
                }
            }
        });
    for (auto& t : th) t.join();
    std::fprintf(stderr, "\n");
}

std::vector<Example> makeRoleData(int variants, int threads, const std::vector<bool>& held) {
    const int n = kNumFactoryPresets * variants;
    std::vector<Example> out((size_t)n);
    std::vector<std::unique_ptr<ProbeRenderer>> pr;
    for (int k = 0; k < threads; ++k) pr.push_back(std::make_unique<ProbeRenderer>());
    parallelFor(n, threads, [&](int th, int i) {
        const int p = i / variants, v = i % variants;
        Rng r(1000003ull * (uint64_t)p + (uint64_t)v + 17);
        loadVariant(pr[(size_t)th]->model(), p, r, v > 0);
        const ProbeResult res = pr[(size_t)th]->render((uint32_t)(r.next() | 1));
        Example& e = out[(size_t)i];
        e.x.assign(res.x.begin(), res.x.end());
        e.cls = roleOf(kFactoryPresets[p].category);
        e.preset = p;
        e.val = held[(size_t)p];
    });
    return out;
}

std::vector<Example> makeMixData(int bases, int perBase, int threads, const std::vector<bool>& held) {
    std::vector<Example> out((size_t)(bases * perBase));
    std::vector<std::unique_ptr<Model>> models;
    for (int k = 0; k < threads; ++k) models.push_back(std::make_unique<Model>(Model::Mode::Offline));
    const double dur = 3.0;
    parallelFor(bases, threads, [&](int th, int b) {
        Rng r(7777777ull * (uint64_t)b + 5);
        const int p = r.irange(0, kNumFactoryPresets - 1);
        Model& m = *models[(size_t)th];
        loadVariant(m, p, r, true);
        int kind;
        switch (kFactoryPresets[p].category) {
            case Category::Bass: kind = 0; break;
            case Category::Lead: kind = 1; break;
            case Category::Pad: kind = 2; break;
            case Category::Pluck:
            case Category::Arp: kind = 3; break;
            default: kind = 4; break;
        }
        if (r.chance(0.3)) kind = r.irange(0, 4);
        std::vector<float> L0, R0;
        renderClip(m, pattern(kind, r, dur), dur, L0, R0);
        // un défaut n'a de sens que là où le son a de l'énergie (sinon il est inaudible)
        float base[kSegDim];
        {
            SegmentAnalyzer an;
            an.reset(kSr);
            an.push(L0.data(), R0.data(), L0.size());
            an.compute(base);
        }
        auto regionDb = [&](float f0, float f1) {
            float mx = -60.f;
            for (int q = 0; q < kBands; ++q)
                if (bandCenterHz(q) >= f0 && bandCenterHz(q) <= f1) mx = std::max(mx, base[fBand0 + q]);
            return mx;
        };
        const bool audible[kNumMixEq] = {regionDb(150, 420) > -30.f, regionDb(6000, 16000) > -30.f, regionDb(45, 130) > -30.f, regionDb(2500, 5000) > -30.f};
        for (int k = 0; k < perBase; ++k) {
            Example& e = out[(size_t)(b * perBase + k)];
            e.preset = p;
            e.val = held[(size_t)p];
            std::vector<float> L = L0, R = R0;
            // défauts d'égalisation : l'inverse de la correction que l'IA doit proposer
            struct D { int pool, eq; };
            const D defs[4] = {{0, kEq250}, {2, kEq8k}, {3, kEq80}, {4, kEq35}};
            const bool clean = k == 0 && r.chance(0.5);
            for (const D& d : defs) {
                if (clean || !r.chance(0.35) || !audible[d.eq]) continue;
                const double mag = r.range(3.0, 8.0);
                Bq f;
                switch (d.eq) {
                    case kEq250: f = peaking(250, +mag, 1.2); break;
                    case kEq8k: f = highShelf(8000, -mag); break;
                    case kEq80: f = peaking(80, -mag, 1.0); break;
                    default: f = peaking(3500, +mag, 2.0); break;
                }
                f.apply(L);
                f.apply(R);
                e.t[d.pool] = 1.f;
                e.amt[d.eq] = (float)mag;
            }
            // volume qui fait du yo-yo : besoin de compression
            if (!clean && r.chance(0.25)) {
                const double depth = r.range(5.0, 12.0), seg = r.range(0.2, 0.45);
                const size_t segN = (size_t)(seg * kSr), fade = (size_t)(0.03 * kSr);
                double prev = 0.0, cur = r.range(-depth / 2, depth / 2);
                for (size_t i = 0; i < L.size(); ++i) {
                    if (i % segN == 0) { prev = cur; cur = r.range(-depth / 2, depth / 2); }
                    const double fr = std::min(1.0, (double)(i % segN) / (double)fade);
                    const float g = (float)std::pow(10.0, (prev + (cur - prev) * fr) / 20.0);
                    L[i] *= g;
                    R[i] *= g;
                }
                e.t[1] = 1.f;
            }
            // niveau : crêtes au hasard, parfois trop près de 0 dBFS
            float pk = 1e-9f;
            for (size_t i = 0; i < L.size(); ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
            const double want = (!clean && r.chance(0.3)) ? r.range(-1.3, 3.0) : r.range(-22.0, -3.0);
            const float g = (float)(std::pow(10.0, want / 20.0) / pk);
            float pk2 = 0.f;
            for (size_t i = 0; i < L.size(); ++i) {
                L[i] = softSafety(L[i] * g);
                R[i] = softSafety(R[i] * g);
                pk2 = std::max(pk2, std::max(std::fabs(L[i]), std::fabs(R[i])));
            }
            e.t[5] = 20.f * std::log10(pk2 + 1e-9f) > -1.5f ? 1.f : 0.f;
            SegmentAnalyzer an;
            an.reset(kSr);
            an.push(L.data(), R.data(), L.size());
            e.x.assign((size_t)kSegDim, 0.f);
            an.compute(e.x.data());
        }
    });
    return out;
}

// --- perceptron multicouche et Adam ---------------------------------------------------------------
float gNoise = 0.15f, gWd = 5e-4f;  // bruit ajouté aux entrées normalisées, décroissance des poids
struct Net {
    std::vector<int> sz;
    std::vector<std::vector<float>> W, b;
    std::vector<float> mean, scale;
};

struct Grad {
    std::vector<std::vector<float>> W, b;
    explicit Grad(const Net& n) {
        for (size_t l = 0; l + 1 < n.sz.size(); ++l) {
            W.emplace_back(n.W[l].size(), 0.f);
            b.emplace_back(n.b[l].size(), 0.f);
        }
    }
    void zero() {
        for (auto& w : W) std::fill(w.begin(), w.end(), 0.f);
        for (auto& v : b) std::fill(v.begin(), v.end(), 0.f);
    }
};

void initNet(Net& n, const std::vector<int>& sz, Rng& r) {
    n.sz = sz;
    n.W.clear();
    n.b.clear();
    for (size_t l = 0; l + 1 < sz.size(); ++l) {
        const double sd = std::sqrt(2.0 / sz[l]);
        std::vector<float> w((size_t)sz[l] * sz[l + 1]);
        for (float& x : w) x = (float)(r.gauss() * sd);
        n.W.push_back(w);
        n.b.emplace_back((size_t)sz[l + 1], 0.f);
    }
}

void normalizeFrom(Net& n, const std::vector<Example>& d) {
    const int D = n.sz[0];
    n.mean.assign((size_t)D, 0.f);
    n.scale.assign((size_t)D, 1.f);
    std::vector<double> s((size_t)D, 0.0), s2((size_t)D, 0.0);
    int cnt = 0;
    for (const auto& e : d) {
        if (e.val) continue;
        ++cnt;
        for (int i = 0; i < D; ++i) { s[(size_t)i] += e.x[(size_t)i]; s2[(size_t)i] += (double)e.x[(size_t)i] * e.x[(size_t)i]; }
    }
    if (!cnt) return;
    for (int i = 0; i < D; ++i) {
        const double m = s[(size_t)i] / cnt, v = std::max(0.0, s2[(size_t)i] / cnt - m * m);
        n.mean[(size_t)i] = (float)m;
        n.scale[(size_t)i] = (float)(1.0 / std::max(1e-3, std::sqrt(v)));
    }
}

// propagation avant avec mémoire des activations (acts[0] = entrée normalisée)
void fwd(const Net& n, const std::vector<float>& x, std::vector<std::vector<float>>& acts, Rng* noise) {
    const size_t L = n.sz.size();
    acts.resize(L);
    acts[0].resize((size_t)n.sz[0]);
    for (int i = 0; i < n.sz[0]; ++i) {
        float v = std::clamp((x[(size_t)i] - n.mean[(size_t)i]) * n.scale[(size_t)i], -8.f, 8.f);
        if (noise) v += (float)(noise->gauss() * gNoise);
        acts[0][(size_t)i] = v;
    }
    for (size_t l = 0; l + 1 < L; ++l) {
        const int in = n.sz[l], out = n.sz[l + 1];
        acts[l + 1].resize((size_t)out);
        const bool last = l + 2 == L;
        for (int o = 0; o < out; ++o) {
            const float* w = n.W[l].data() + (size_t)o * in;
            float s = n.b[l][(size_t)o];
            for (int i = 0; i < in; ++i) s += w[i] * acts[l][(size_t)i];
            acts[l + 1][(size_t)o] = last ? s : std::max(0.f, s);
        }
    }
}

void bwd(const Net& n, const std::vector<std::vector<float>>& acts, std::vector<float> dy, Grad& g) {
    const size_t L = n.sz.size();
    for (size_t l = L - 1; l >= 1; --l) {
        const int in = n.sz[l - 1], out = n.sz[l];
        std::vector<float> dx((size_t)in, 0.f);
        for (int o = 0; o < out; ++o) {
            const float d = dy[(size_t)o];
            if (d == 0.f) continue;
            g.b[l - 1][(size_t)o] += d;
            float* gw = g.W[l - 1].data() + (size_t)o * in;
            const float* w = n.W[l - 1].data() + (size_t)o * in;
            for (int i = 0; i < in; ++i) {
                gw[i] += d * acts[l - 1][(size_t)i];
                dx[(size_t)i] += d * w[i];
            }
        }
        if (l - 1 >= 1)
            for (int i = 0; i < in; ++i)
                if (acts[l - 1][(size_t)i] <= 0.f) dx[(size_t)i] = 0.f;
        dy.swap(dx);
    }
}

// perte d'un exemple : remplit dy (gradient par rapport aux sorties), renvoie la perte
using LossFn = std::function<float(const std::vector<float>& y, const Example& e, std::vector<float>& dy)>;

float roleLoss(const std::vector<float>& y, const Example& e, std::vector<float>& dy) {
    float p[kNumRoles];
    for (int k = 0; k < kNumRoles; ++k) p[k] = y[(size_t)k];
    softmax(p, kNumRoles);
    dy.assign(y.size(), 0.f);
    for (int k = 0; k < kNumRoles; ++k) dy[(size_t)k] = p[k] - (k == e.cls ? 1.f : 0.f);
    return -std::log(std::max(1e-7f, p[e.cls]));
}

float mixLoss(const std::vector<float>& y, const Example& e, std::vector<float>& dy) {
    dy.assign(y.size(), 0.f);
    float loss = 0.f;
    for (int k = 0; k < kNumMixOut; ++k) {
        const float p = sigmoid(y[(size_t)k]);
        dy[(size_t)k] = p - e.t[k];
        loss += -(e.t[k] * std::log(std::max(1e-7f, p)) + (1.f - e.t[k]) * std::log(std::max(1e-7f, 1.f - p)));
    }
    const int pool[kNumMixEq] = {0, 2, 3, 4};  // MixEq -> sortie de décision
    for (int q = 0; q < kNumMixEq; ++q) {
        if (e.t[pool[q]] < 0.5f) continue;
        const float d = y[(size_t)(kNumMixOut + q)] - e.amt[q];
        dy[(size_t)(kNumMixOut + q)] = 0.1f * d;
        loss += 0.05f * d * d;
    }
    return loss;
}

float evalLoss(const Net& n, const std::vector<Example>& d, bool val, const LossFn& lf) {
    std::vector<std::vector<float>> acts;
    std::vector<float> dy;
    double s = 0.0;
    int c = 0;
    for (const auto& e : d) {
        if (e.val != val) continue;
        fwd(n, e.x, acts, nullptr);
        s += lf(acts.back(), e, dy);
        ++c;
    }
    return c ? (float)(s / c) : 0.f;
}

// Adam, mini-lots de 32, arrêt quand la validation ne progresse plus (si useVal)
int train(Net& n, const std::vector<Example>& d, const LossFn& lf, int maxEpochs, bool useVal, uint64_t seed, const char* tag) {
    Rng r(seed);
    Grad g(n), m(n), v(n);
    m.zero();
    v.zero();
    std::vector<int> idx;
    for (int i = 0; i < (int)d.size(); ++i)
        if (!d[(size_t)i].val || !useVal) idx.push_back(i);
    const float lr0 = 1e-3f, b1 = 0.9f, b2 = 0.999f, wd = gWd;
    int step = 0, best = 0, since = 0;
    float bestLoss = 1e30f;
    Net bestNet = n;
    std::vector<std::vector<float>> acts;
    std::vector<float> dy;
    for (int ep = 1; ep <= maxEpochs; ++ep) {
        for (size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[(size_t)(r.uni() * (double)i)]);
        const float lr = lr0 * (ep > maxEpochs * 0.7 ? 0.3f : 1.f);
        double tl = 0.0;
        for (size_t s0 = 0; s0 < idx.size(); s0 += 32) {
            g.zero();
            const size_t s1 = std::min(idx.size(), s0 + 32);
            for (size_t s = s0; s < s1; ++s) {
                const Example& e = d[(size_t)idx[s]];
                fwd(n, e.x, acts, &r);
                tl += lf(acts.back(), e, dy);
                bwd(n, acts, dy, g);
            }
            ++step;
            const float bs = (float)(s1 - s0);
            const float c1 = 1.f - std::pow(b1, (float)step), c2 = 1.f - std::pow(b2, (float)step);
            for (size_t l = 0; l < n.W.size(); ++l) {
                auto upd = [&](std::vector<float>& w, std::vector<float>& gw, std::vector<float>& mw, std::vector<float>& vw, bool decay) {
                    for (size_t i = 0; i < w.size(); ++i) {
                        const float gr = gw[i] / bs + (decay ? wd * w[i] : 0.f);
                        mw[i] = b1 * mw[i] + (1 - b1) * gr;
                        vw[i] = b2 * vw[i] + (1 - b2) * gr * gr;
                        w[i] -= lr * (mw[i] / c1) / (std::sqrt(vw[i] / c2) + 1e-8f);
                    }
                };
                upd(n.W[l], g.W[l], m.W[l], v.W[l], true);
                upd(n.b[l], g.b[l], m.b[l], v.b[l], false);
            }
        }
        if (useVal) {
            const float vl = evalLoss(n, d, true, lf);
            if (vl < bestLoss - 1e-4f) { bestLoss = vl; best = ep; bestNet = n; since = 0; }
            else ++since;
            if (ep % 10 == 0) std::fprintf(stderr, "  [%s] époque %d  perte entraînement %.4f  validation %.4f\n", tag, ep, tl / (double)idx.size(), vl);
            if (since >= 40) break;
        } else if (ep % 20 == 0) {
            std::fprintf(stderr, "  [%s] époque %d  perte %.4f\n", tag, ep, tl / (double)idx.size());
        }
    }
    if (useVal) n = bestNet;
    return useVal ? best : maxEpochs;
}

void predict(const Net& n, const std::vector<float>& x, std::vector<float>& y) {
    std::vector<std::vector<float>> acts;
    fwd(n, x, acts, nullptr);
    y = acts.back();
}

// --- écriture des poids ---------------------------------------------------------------------------
// littéral float valide en C++ (« 3 » -> « 3.0f »)
std::string lit(double v, const char* f = "%.8g") {
    if (!std::isfinite(v)) v = 0.0;
    char buf[40];
    std::snprintf(buf, sizeof buf, f, v);
    std::string s = buf;
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    return s + "f";
}

void writeArray(std::string& s, const char* name, const std::vector<float>& v) {
    s += "const float ";
    s += name;
    s += "[" + std::to_string(v.size()) + "] = {";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i % 8 == 0) s += "\n    ";
        s += lit(v[i]) + ", ";
    }
    s += "};\n";
}

void writeNet(std::string& s, const Net& n, const char* sym, const char* label) {
    const std::string p = std::string(sym) + "_";
    for (size_t l = 0; l < n.W.size(); ++l) {
        writeArray(s, (p + "W" + std::to_string(l)).c_str(), n.W[l]);
        writeArray(s, (p + "b" + std::to_string(l)).c_str(), n.b[l]);
    }
    writeArray(s, (p + "mean").c_str(), n.mean);
    writeArray(s, (p + "scale").c_str(), n.scale);
    s += "const DenseLayer " + p + "layers[" + std::to_string(n.W.size()) + "] = {\n";
    for (size_t l = 0; l < n.W.size(); ++l)
        s += "    {" + std::to_string(n.sz[l]) + ", " + std::to_string(n.sz[l + 1]) + ", " + p + "W" + std::to_string(l) + ", " + p + "b" + std::to_string(l) + "},\n";
    s += "};\n";
    s += "} // namespace\nconst Network " + std::string(sym) + " = {\"" + label + "\", " + std::to_string(n.sz.front()) + ", " + std::to_string(n.sz.back()) + ", " +
         std::to_string(n.W.size()) + ", " + p + "layers, " + p + "mean, " + p + "scale, true};\nnamespace {\n";
}

bool writeFile(const std::string& path, const std::string& s) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
    return true;
}

// règles de l'ancienne version (approximées sur les mêmes bandes), pour comparaison honnête
void ruleDecisions(const std::vector<float>& x, bool out[kNumMixOut]) {
    auto dens = [&](float f0, float f1) {
        double p = 0.0;
        for (int b = 0; b < kBands; ++b) {
            const float c = bandCenterHz(b);
            if (c >= f0 && c <= f1) p += std::pow(10.0, x[(size_t)(fBand0 + b)] / 10.0);
        }
        return 10.0 * std::log10(p + 1e-12) - 10.0 * std::log10(std::log2(f1 / f0));
    };
    const double d0 = dens(80, 160), d1 = dens(180, 350), d2 = dens(400, 900), d3 = dens(1000, 4000), d4 = dens(6000, 14000), d5 = dens(2500, 5000), d6 = dens(50, 110);
    out[0] = d1 - 0.5 * (d0 + d2) - 4 > 0;
    out[1] = x[fDyn] - 13 > 0;
    out[2] = -(d4 - d3) - 14 > 0;
    out[3] = d1 - d6 - 8 > 0;
    out[4] = d5 - d2 - 2 > 0;
    out[5] = x[fPeak] + 1.5 > 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string outPath = "src/ai/AiWeights.cpp", reportPath = "docs/IA.md";
    int threads = (int)std::max(1u, std::thread::hardware_concurrency());
    bool quick = false;
    std::string cache;
    int mixHidden = 64;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) outPath = argv[++i];
        else if (a == "--report" && i + 1 < argc) reportPath = argv[++i];
        else if (a == "--threads" && i + 1 < argc) threads = std::max(1, std::atoi(argv[++i]));
        else if (a == "--quick") quick = true;
        else if (a == "--cache" && i + 1 < argc) cache = argv[++i];
        else if (a == "--noise" && i + 1 < argc) gNoise = (float)std::atof(argv[++i]);
        else if (a == "--wd" && i + 1 < argc) gWd = (float)std::atof(argv[++i]);
        else if (a == "--mix-hidden" && i + 1 < argc) mixHidden = std::max(8, std::atoi(argv[++i]));
        else {
            std::printf("usage : raw2_train [--out AiWeights.cpp] [--report IA.md] [--threads N] [--quick] [--cache dossier] [--noise 0.15] [--wd 5e-4] [--mix-hidden 64]\n");
            return 1;
        }
    }
    const auto held = heldOutPresets();
    int nHeld = 0;
    for (bool h : held) nHeld += h;
    const int roleVariants = quick ? 6 : 48;
    const int mixBases = quick ? 200 : 5000, perBase = 4;

    std::fprintf(stderr, "RoleNet : %d écoutes (%d presets, %d mis de côté)\n", kNumFactoryPresets * roleVariants, kNumFactoryPresets, nHeld);
    std::vector<Example> roleData, mixData;
    const std::string roleCache = cache.empty() ? "" : cache + (quick ? "/role-quick.bin" : "/role.bin");
    const std::string mixCache = cache.empty() ? "" : cache + (quick ? "/mix-quick.bin" : "/mix.bin");
    if (roleCache.empty() || !loadData(roleCache, roleData, (uint32_t)(kNumFactoryPresets * roleVariants), kProbeDim, held)) {
        roleData = makeRoleData(roleVariants, threads, held);
        if (!roleCache.empty()) saveData(roleCache, roleData);
    }
    std::fprintf(stderr, "MixNet : %d boucles x %d variantes\n", mixBases, perBase);
    if (mixCache.empty() || !loadData(mixCache, mixData, (uint32_t)(mixBases * perBase), kSegDim, held)) {
        mixData = makeMixData(mixBases, perBase, threads, held);
        if (!mixCache.empty()) saveData(mixCache, mixData);
    }

    // --- RoleNet : validation sur presets inconnus, puis modèle final sur tout ---
    Rng ir(42);
    Net role;
    initNet(role, {kProbeDim, 64, 32, kNumRoles}, ir);
    normalizeFrom(role, roleData);
    const int roleEpochs = train(role, roleData, roleLoss, quick ? 40 : 400, true, 1, "RoleNet");
    int conf[kNumRoles][kNumRoles] = {};
    int ok = 0, tot = 0;
    {
        std::vector<float> y;
        for (const auto& e : roleData) {
            if (!e.val) continue;
            predict(role, e.x, y);
            const int k = (int)(std::max_element(y.begin(), y.end()) - y.begin());
            ++conf[e.cls][k];
            ok += k == e.cls;
            ++tot;
        }
    }
    const float roleAcc = tot ? (float)ok / (float)tot : 0.f;
    std::fprintf(stderr, "RoleNet : %.1f %% sur presets inconnus (époque %d)\n", roleAcc * 100.f, roleEpochs);

    // --- MixNet ---
    Net mix;
    initNet(mix, {kSegDim, mixHidden, mixHidden, kNumMixOut + kNumMixEq}, ir);
    normalizeFrom(mix, mixData);
    const int mixEpochs = train(mix, mixData, mixLoss, quick ? 40 : 300, true, 2, "MixNet");
    int mOk[kNumMixOut] = {}, mTp[kNumMixOut] = {}, mFp[kNumMixOut] = {}, mFn[kNumMixOut] = {}, rOk[kNumMixOut] = {}, mTot = 0;
    double maeSum = 0.0;
    int maeN = 0;
    {
        std::vector<float> y;
        const int pool[kNumMixEq] = {0, 2, 3, 4};
        for (const auto& e : mixData) {
            if (!e.val) continue;
            predict(mix, e.x, y);
            bool rules[kNumMixOut];
            ruleDecisions(e.x, rules);
            ++mTot;
            for (int k = 0; k < kNumMixOut; ++k) {
                const bool p = sigmoid(y[(size_t)k]) > 0.5f, t = e.t[k] > 0.5f;
                mOk[k] += p == t;
                rOk[k] += rules[k] == t;
                mTp[k] += p && t;
                mFp[k] += p && !t;
                mFn[k] += !p && t;
            }
            for (int q = 0; q < kNumMixEq; ++q)
                if (e.t[pool[q]] > 0.5f) { maeSum += std::fabs(y[(size_t)(kNumMixOut + q)] - e.amt[q]); ++maeN; }
        }
    }
    int allOk = 0, ruleAll = 0;
    for (int k = 0; k < kNumMixOut; ++k) { allOk += mOk[k]; ruleAll += rOk[k]; }
    const float mixAcc = mTot ? (float)allOk / (float)(mTot * kNumMixOut) : 0.f;
    const float ruleAcc = mTot ? (float)ruleAll / (float)(mTot * kNumMixOut) : 0.f;
    const float mae = maeN ? (float)(maeSum / maeN) : 0.f;
    std::fprintf(stderr, "MixNet : %.1f %% de bonnes décisions (règles : %.1f %%), dosage à %.2f dB près\n", mixAcc * 100.f, ruleAcc * 100.f, mae);

    // --- modèles livrés : même recette, entraînés sur tous les presets ---
    for (auto& e : roleData) e.val = false;
    for (auto& e : mixData) e.val = false;
    Rng fr(4242);
    Net roleF, mixF;
    initNet(roleF, role.sz, fr);
    normalizeFrom(roleF, roleData);
    train(roleF, roleData, roleLoss, std::max(10, roleEpochs + roleEpochs / 10), false, 3, "RoleNet final");
    initNet(mixF, mix.sz, fr);
    normalizeFrom(mixF, mixData);
    train(mixF, mixData, mixLoss, std::max(10, mixEpochs + mixEpochs / 10), false, 4, "MixNet final");

    // profils spectraux moyens par rôle (moyenne des 3 segments de l'écoute)
    float prof[kNumRoles][kBands] = {};
    int pc[kNumRoles] = {};
    for (const auto& e : roleData) {
        ++pc[e.cls];
        for (int b = 0; b < kBands; ++b)
            for (int s = 0; s < kProbeSegs; ++s) prof[e.cls][b] += e.x[(size_t)(s * kSegDim + fBand0 + b)] / kProbeSegs;
    }

    char date[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(date, sizeof date, "%Y-%m-%d", std::gmtime(&now));
    std::string s;
    s += "// Produit par tools/train_ai.cpp (raw2_train) le ";
    s += date;
    s += ". Ne pas modifier à la main.\n";
    s += "#include \"ai/AiWeights.h\"\n\nnamespace raw2::ai {\nnamespace {\n";
    writeNet(s, mixF, "kMixNet", "MixNet");
    writeNet(s, roleF, "kRoleNet", "RoleNet");
    s += "} // namespace\n\nconst float kRoleProfile[kNumRoles][kBands] = {\n";
    char buf[64];
    for (int k = 0; k < kNumRoles; ++k) {
        s += "    {";
        for (int b = 0; b < kBands; ++b) {
            s += lit(pc[k] ? prof[k][b] / pc[k] : 0.f, "%.3g") + (b + 1 < kBands ? ", " : "");
        }
        s += "},\n";
    }
    s += "};\n\n";
    s += "const TrainingInfo kTrainingInfo = {" + std::to_string(roleData.size()) + ", " + std::to_string(kNumFactoryPresets) + ", " + std::to_string(nHeld) + ", " + lit(roleAcc, "%.4g") + ", ";
    s += std::to_string(mixData.size()) + ", " + lit(mixAcc, "%.4g") + ", " + lit(mae, "%.3g") + ", \"" + date + "\"};\n\n} // namespace raw2::ai\n";
    if (!writeFile(outPath, s)) {
        std::fprintf(stderr, "impossible d'écrire %s\n", outPath.c_str());
        return 1;
    }

    // rapport
    std::string r;
    r += "# IA de RAW 2 : rapport d'entraînement\n\n";
    r += std::string("Produit par `raw2_train` le ") + date + (quick ? " (mode rapide, chiffres indicatifs)" : "") + ".\n\n";
    r += "Validation : les réseaux sont d'abord entraînés **sans** " + std::to_string(nHeld) +
         " presets d'usine mis de côté, puis mesurés sur ces presets inconnus. Les poids livrés sont ensuite ré-entraînés sur les 72 presets avec la même recette.\n\n";
    r += "## RoleNet (carte PROPOSITION)\n\n";
    std::snprintf(buf, sizeof buf, "%.1f %%", roleAcc * 100.f);
    r += "- " + std::to_string(roleData.size()) + " écoutes (72 presets x " + std::to_string(roleVariants) + " variantes), réseau " + std::to_string(kProbeDim) + " -> 64 -> 32 -> 5.\n";
    r += std::string("- Rôle reconnu sur des presets inconnus : **") + buf + "** (hasard : 20 %).\n\n";
    r += "Matrice de confusion (lignes : rôle réel, colonnes : rôle prédit) :\n\n| | ";
    for (auto* nm : kRoleNames) r += std::string(nm) + " | ";
    r += "\n|---|---|---|---|---|---|\n";
    for (int a = 0; a < kNumRoles; ++a) {
        r += std::string("| ") + kRoleNames[a] + " | ";
        for (int b = 0; b < kNumRoles; ++b) r += std::to_string(conf[a][b]) + " | ";
        r += "\n";
    }
    r += "\n## MixNet (page MIX IA)\n\n";
    r += "- " + std::to_string(mixData.size()) + " boucles de 3 s avec défauts ajoutés au hasard, réseau " + std::to_string(kSegDim) + " -> " + std::to_string(mixHidden) + " -> " + std::to_string(mixHidden) + " -> 10.\n";
    std::snprintf(buf, sizeof buf, "%.1f %% (anciennes règles : %.1f %%)", mixAcc * 100.f, ruleAcc * 100.f);
    r += std::string("- Décisions correctes sur des presets inconnus : **") + buf + "**.\n";
    std::snprintf(buf, sizeof buf, "%.2f dB", mae);
    r += std::string("- Erreur moyenne sur le dosage des EQ : **") + buf + "**.\n\n";
    r += "| Correction | Réseau : bonnes décisions | précision | rappel | Règles |\n|---|---|---|---|---|\n";
    for (int k = 0; k < kNumMixOut; ++k) {
        const float pr = mTp[k] + mFp[k] ? (float)mTp[k] / (float)(mTp[k] + mFp[k]) : 0.f;
        const float rc = mTp[k] + mFn[k] ? (float)mTp[k] / (float)(mTp[k] + mFn[k]) : 0.f;
        std::snprintf(buf, sizeof buf, "%.1f %% | %.0f %% | %.0f %% | %.1f %%", 100.0 * mOk[k] / std::max(1, mTot), pr * 100.0, rc * 100.0, 100.0 * rOk[k] / std::max(1, mTot));
        r += std::string("| ") + kMixNames[k] + " | " + buf + " |\n";
    }
    r += "\nLes « règles » sont celles de la version précédente, recalculées sur les mêmes bandes (approximation).\n";
    if (!writeFile(reportPath, r)) std::fprintf(stderr, "impossible d'écrire %s\n", reportPath.c_str());
    std::fprintf(stderr, "écrit : %s, %s\n", outPath.c_str(), reportPath.c_str());
    return 0;
}
