// Tests de l'IA : caractéristiques audio, inférence, réseaux livrés (rôles, corrections), assistant.
#include "ai/Assistant.h"
#include "ai/Features.h"
#include "ai/Mlp.h"
#include "ai/Probe.h"
#include "core/FactoryData.h"
#include "core/Model.h"
#include "dsp/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace raw2;
using namespace raw2::ai;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("ÉCHEC %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

namespace {
constexpr double kSr = 48000.0, kPi = 3.14159265358979323846;

// rend une petite boucle (accord puis notes) avec le moteur, chaîne MIX IA comprise
void renderLoop(Model& m, std::vector<float>& L, std::vector<float>& R, double seconds) {
    Engine e(m);
    e.prepare(kSr, 256);
    const long total = (long)(seconds * kSr);
    L.assign((size_t)total, 0.f);
    R.assign((size_t)total, 0.f);
    const int notes[6] = {48, 55, 60, 64, 67, 72};
    for (long pos = 0, b = 0; pos < total; pos += 256, ++b) {
        NoteEvent ev[2];
        int nev = 0;
        if (b % 24 == 0) ev[nev++] = NoteEvent{0, notes[(b / 24) % 6], 0.8f, true};
        if (b % 24 == 18) ev[nev++] = NoteEvent{0, notes[(b / 24) % 6], 0.f, false};
        e.process(L.data() + pos, R.data() + pos, (int)std::min<long>(256, total - pos), ev, nev);
    }
}

void features(const std::vector<float>& L, const std::vector<float>& R, float* x) {
    SegmentAnalyzer an;
    an.reset(kSr);
    an.push(L.data(), R.data(), L.size());
    an.compute(x);
}

// filtre en cloche RBJ (pour fabriquer un défaut connu)
void bell(std::vector<float>& x, double f, double g, double q) {
    const double A = std::pow(10.0, g / 40.0), w = 2 * kPi * f / kSr, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al / A;
    const double b0 = (1 + al * A) / a0, b1 = -2 * c / a0, b2 = (1 - al * A) / a0, a1 = -2 * c / a0, a2 = (1 - al / A) / a0;
    double z1 = 0, z2 = 0;
    for (float& s : x) {
        const double y = b0 * s + z1;
        z1 = b1 * s - a1 * y + z2;
        z2 = b2 * s - a2 * y;
        s = (float)y;
    }
}
} // namespace

int main() {
    // --- caractéristiques : sinus de 1 kHz, amplitude 0.5, centré ---
    {
        std::vector<float> L((size_t)kSr), R;
        for (size_t i = 0; i < L.size(); ++i) L[i] = 0.5f * (float)std::sin(2 * kPi * 1000.0 * (double)i / kSr);
        R = L;
        float x[kSegDim];
        features(L, R, x);
        int best = 0;
        for (int b = 1; b < kBands; ++b) if (x[fBand0 + b] > x[fBand0 + best]) best = b;
        CHECK(bandLowHz(best) <= 1000.f && bandHighHz(best) > 1000.f);
        CHECK(std::fabs(x[fLevel] - (-9.03f)) < 0.3f);   // RMS d'un sinus de 0.5 : -9 dBFS
        CHECK(std::fabs(x[fPeak] - (-6.02f)) < 0.1f);
        CHECK(x[fCorr] > 0.99f);
        CHECK(x[fSide] <= -39.f);
        CHECK(x[fDyn] < 1.f);
        for (float v : x) CHECK(std::isfinite(v));
    }
    // --- inférence : petit réseau calculé à la main ---
    {
        const float W0[2 * 2] = {1.f, -1.f, 0.5f, 0.5f}, b0[2] = {0.f, -1.f};
        const float W1[1 * 2] = {2.f, 3.f}, b1[1] = {0.25f};
        const DenseLayer layers[2] = {{2, 2, W0, b0}, {2, 1, W1, b1}};
        const float mean[2] = {1.f, 0.f}, scale[2] = {2.f, 1.f};
        const Network n{"test", 2, 1, 2, layers, mean, scale, true};
        const float x[2] = {2.f, 3.f};  // normalisé : (2, 3) ; couche 1 : relu(-1) = 0, relu(1.5) = 1.5
        float y = 0.f;
        forward(n, x, &y);
        CHECK(std::fabs(y - (0.25f + 3.f * 1.5f)) < 1e-5f);
        float v[3] = {1.f, 2.f, 3.f};
        softmax(v, 3);
        CHECK(std::fabs(v[0] + v[1] + v[2] - 1.f) < 1e-5f && v[2] > v[1] && v[1] > v[0]);
    }

    if (!networksTrained()) {
        std::printf("réseaux non entraînés : tests des modèles sautés\n");
        std::printf(failures ? "%d échec(s)\n" : "ai_test : OK\n", failures);
        return failures ? 1 : 0;
    }
    std::printf("réseaux : RoleNet %.1f %%, MixNet %.1f %% (validation, %s)\n", kTrainingInfo.roleValAcc * 100.f, kTrainingInfo.mixValAcc * 100.f, kTrainingInfo.date);

    // --- RoleNet : les presets d'usine reconnus (écoute interne) ---
    {
        ProbeRenderer pr;
        int ok = 0, n = 0;
        for (int i = 0; i < kNumFactoryPresets; i += 3) {
            pr.model().params.resetDefaults();
            pr.model().applyFactoryPreset(i, nullptr);
            const ProbeResult r = pr.render(1);
            CHECK(!r.silent);
            const RoleGuess g = classifyRole(r);
            int want = 1;
            switch (kFactoryPresets[i].category) {
                case Category::Bass: want = 0; break;
                case Category::Lead: want = 1; break;
                case Category::Pad: want = 2; break;
                case Category::Pluck:
                case Category::Arp: want = 3; break;
                case Category::Fx: want = 4; break;
            }
            ok += g.best == want;
            ++n;
            float s = 0.f;
            for (float p : g.p) { CHECK(p >= 0.f && p <= 1.f); s += p; }
            CHECK(std::fabs(s - 1.f) < 1e-3f);
        }
        std::printf("RoleNet : %d / %d presets reconnus\n", ok, n);
        CHECK(ok >= n * 8 / 10);
    }

    // --- MixNet : un défaut ajouté fait monter la bonne correction ---
    {
        Model m(Model::Mode::Offline);
        float up250 = 0.f, up35 = 0.f, lim = 0.f, cleanLim = 0.f;
        const int presets[6] = {1, 13, 25, 37, 49, 61};
        for (int p : presets) {
            m.params.resetDefaults();
            m.applyFactoryPreset(p, nullptr);
            std::vector<float> L, R;
            renderLoop(m, L, R, 3.0);
            float x[kSegDim];
            features(L, R, x);
            const MixAdvice clean = adviseMix(x);
            for (int k = 0; k < kNumMixOut; ++k) CHECK(clean.prob[k] >= 0.f && clean.prob[k] <= 1.f);
            std::vector<float> L2 = L, R2 = R;
            bell(L2, 250, 7, 1.2);
            bell(R2, 250, 7, 1.2);
            features(L2, R2, x);
            const MixAdvice boom = adviseMix(x);
            up250 += boom.prob[0] - clean.prob[0];
            CHECK(boom.gainDb[kEq250] < 0.f);  // la correction creuse
            L2 = L; R2 = R;
            bell(L2, 3500, 7, 2.0);
            bell(R2, 3500, 7, 2.0);
            features(L2, R2, x);
            up35 += adviseMix(x).prob[4] - clean.prob[4];
            // crêtes à 0 dBFS
            float pk = 1e-9f;
            for (size_t i = 0; i < L.size(); ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
            L2 = L; R2 = R;
            for (size_t i = 0; i < L2.size(); ++i) { L2[i] *= 1.f / pk; R2[i] *= 1.f / pk; }
            features(L2, R2, x);
            lim += adviseMix(x).prob[5];
            cleanLim += clean.prob[5];
        }
        up250 /= 6.f; up35 /= 6.f; lim /= 6.f; cleanLim /= 6.f;
        std::printf("MixNet : +7 dB @ 250 Hz -> besoin %+.2f, +7 dB @ 3.5 kHz -> %+.2f, crêtes 0 dBFS -> limiteur %.2f (propre %.2f)\n", up250, up35, lim, cleanLim);
        CHECK(up250 > 0.15f);
        CHECK(up35 > 0.15f);
        CHECK(lim > 0.9f && cleanLim < 0.2f);
    }

    // --- la chaîne MIX IA applique bien le dosage : -6 dB à 250 Hz creuse cette zone ---
    {
        Model m(Model::Mode::Offline);
        m.applyFactoryPreset(12, nullptr);  // une basse
        std::vector<float> L, R;
        renderLoop(m, L, R, 2.0);
        float a[kSegDim], b[kSegDim];
        features(L, R, a);
        m.applyFactoryPreset(12, nullptr);
        m.mix.addFilter(0, 250.f, -6.f, 1.2f);
        renderLoop(m, L, R, 2.0);
        bool finite = true;
        for (float v : L) finite = finite && std::isfinite(v);
        CHECK(finite);
        features(L, R, b);
        int k250 = 0;
        for (int q = 0; q < kBands; ++q) if (bandLowHz(q) <= 250.f && bandHighHz(q) > 250.f) k250 = q;
        const float drop = (a[fBand0 + k250] - a[fLevel]) - (b[fBand0 + k250] - b[fLevel]);
        std::printf("chaîne : -6 dB @ 250 Hz -> bande 250 Hz %.1f dB plus bas\n", drop);
        CHECK(drop > 2.f);
    }

    // --- assistant PROPOSITION : un lead visé comme BASSE ---
    {
        Model live(Model::Mode::Offline);
        live.applyFactoryPreset(0, nullptr);  // « verre givré », lead
        SoundAdvisor adv;
        adv.request(snapshotSound(live), 0);
        SoundAnalysis res;
        bool got = false;
        const auto t0 = std::chrono::steady_clock::now();
        while (!got && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) {
            got = adv.poll(res);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(got);
        if (got) {
            std::printf("assistant : %d retouches essayées en %.0f ms, entendu %s (%.0f %%), %zu proposition(s)\n", res.tried, ms, roleName(res.guess.best),
                        res.guess.p[res.guess.best] * 100.f, res.props.size());
            CHECK(res.target == 0);
            CHECK(!res.silent);
            CHECK(res.tried > 10);
            for (const auto& p : res.props) {
                std::printf("  %s %s : BASSE %.0f %% -> %.0f %%\n", p.what.c_str(), p.value.c_str(), p.before * 100.f, p.after * 100.f);
                CHECK(p.after > p.before);
                CHECK(!p.changes.empty());
            }
        }
        // une nouvelle demande remplace l'ancienne sans bloquer
        adv.request(snapshotSound(live), -1);
        adv.request(snapshotSound(live), 2);
        got = false;
        const auto t1 = std::chrono::steady_clock::now();
        while (!got && std::chrono::steady_clock::now() - t1 < std::chrono::seconds(60)) {
            got = adv.poll(res);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(got && res.target == 2);
    }

    std::printf(failures ? "%d échec(s)\n" : "ai_test : OK\n", failures);
    return failures ? 1 : 0;
}
