// Tests de la chaîne de mastering (MIX IA) et des mesures : sonie BS.1770, True Peak, limiteur
// (plafond, latence), EQ, passe-haut de détection du compresseur, corrélation et largeur.
#include "dsp/Mastering.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace raw2;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("ÉCHEC %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const double kPi = 3.14159265358979323846;

struct Rng {
    uint32_t s = 777;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(s & 0xFFFFFF) / 16777216.f * 2.f - 1.f; }
};

// sonie d'un signal stéréo après `seconds`
static MeterReadings measure(double sr, double seconds, const std::vector<float>& L, const std::vector<float>& R) {
    Meters m;
    m.prepare(sr);
    const int n = (int)L.size();
    for (int i = 0; i < n; i += 256) m.process(&L[(size_t)i], &R[(size_t)i], std::min(256, n - i));
    (void)seconds;
    return m.read();
}

int main() {
    const double sr = 48000.0;
    // --- sonie : sinus de 997 Hz à -20 dBFS dans les deux canaux ---
    // Référence BS.1770-4 : un sinus de 1 kHz à 0 dBFS dans UN canal vaut -3,01 LKFS ; dans les deux
    // canaux l'énergie double (+3,01 dB), donc 0 dBFS -> 0,0 LKFS et -20 dBFS -> -20,0 LKFS (la
    // pondération K vaut environ +0,7 dB à 1 kHz, compensée par la constante -0,691).
    {
        const int n = (int)(sr * 5.0);
        std::vector<float> L((size_t)n), R((size_t)n);
        for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = 0.1f * (float)std::sin(2.0 * kPi * 997.0 * i / sr);
        const MeterReadings r = measure(sr, 5.0, L, R);
        std::printf("sonie sinus -20 dBFS : M %.2f S %.2f I %.2f LUFS, TP %.2f dBTP, corrélation %.2f, largeur %.0f %%\n", r.momentary, r.shortTerm, r.integrated,
                    std::max(r.truePeakHoldL, r.truePeakHoldR), r.corr, r.width);
        CHECK(std::fabs(r.integrated + 20.f) < 0.1f);
        CHECK(std::fabs(r.momentary - r.integrated) < 0.1f);
        CHECK(std::fabs(std::max(r.truePeakHoldL, r.truePeakHoldR) + 20.f) < 0.1f);
        CHECK(r.corr > 0.99f && r.width < 1.f);
        // porte : 5 s de son puis 5 s de silence, l'intégrée ne bouge pas
        std::vector<float> L2(L), R2(R);
        L2.resize((size_t)n * 2, 0.f);
        R2.resize((size_t)n * 2, 0.f);
        const MeterReadings g = measure(sr, 10.0, L2, R2);
        std::printf("porte : intégrée après 5 s de silence %.2f LUFS\n", g.integrated);
        CHECK(std::fabs(g.integrated + 20.f) < 0.15f);
    }
    // --- True Peak : sinus fs/4 déphasé de 45° (échantillons à 0,707, vraie crête à 1) ---
    {
        const int n = (int)sr;
        std::vector<float> L((size_t)n), R((size_t)n);
        for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (float)std::sin(kPi / 2.0 * i + kPi / 4.0);
        const MeterReadings r = measure(sr, 1.0, L, R);
        std::printf("True Peak fs/4 : crête échantillon %.2f dBFS, vraie crête %.2f dBTP\n", r.samplePeak, r.truePeakHoldL);
        CHECK(std::fabs(r.samplePeak + 3.01f) < 0.1f);
        CHECK(std::fabs(r.truePeakHoldL) < 0.3f);
    }
    // --- corrélation : L = -R puis bruits indépendants ---
    {
        const int n = (int)sr * 2;
        std::vector<float> L((size_t)n), R((size_t)n), L2((size_t)n), R2((size_t)n);
        Rng a, b;
        b.s = 4242;
        for (int i = 0; i < n; ++i) {
            L[(size_t)i] = 0.3f * a.next();
            R[(size_t)i] = -L[(size_t)i];
            L2[(size_t)i] = 0.3f * a.next();
            R2[(size_t)i] = 0.3f * b.next();
        }
        const MeterReadings r1 = measure(sr, 2.0, L, R), r2 = measure(sr, 2.0, L2, R2);
        std::printf("corrélation : L=-R %.2f, indépendants %.2f (graves %.2f, médiums %.2f, aigus %.2f), largeur %.0f %%\n", r1.corr, r2.corr, r2.corrLow, r2.corrMid, r2.corrHigh, r2.width);
        CHECK(r1.corr < -0.99f);
        CHECK(std::fabs(r2.corr) < 0.1f);
        CHECK(r2.width > 80.f && r2.width < 120.f);
    }
    // --- limiteur : bruit +12 dB, plafond -1 dBTP ---
    {
        MasterChain mc;
        mc.prepare(sr, 512);
        mc.setLimiter(true, -1.f);
        mc.reset();
        const int n = (int)sr * 3;
        std::vector<float> L((size_t)n), R((size_t)n);
        Rng a;
        for (int i = 0; i < n; ++i) {
            const float env = (i / 4800) % 3 == 0 ? 4.f : 0.6f;  // coups forts réguliers
            L[(size_t)i] = env * a.next();
            R[(size_t)i] = env * a.next();
        }
        for (int i = 0; i < n; i += 512) mc.process(&L[(size_t)i], &R[(size_t)i], std::min(512, n - i));
        TruePeak4x tl, tr;
        float worst = 0.f;
        bool nan = false;
        for (int i = 0; i < n; ++i) {
            worst = std::max(worst, std::max(tl.push(L[(size_t)i]), tr.push(R[(size_t)i])));
            if (!std::isfinite(L[(size_t)i])) nan = true;
        }
        std::printf("limiteur : vraie crête de sortie %.2f dBTP (plafond -1), latence %d échantillons\n", 20.f * std::log10(worst), mc.latencySamples());
        CHECK(!nan);
        CHECK(20.f * std::log10(worst) < -1.f + 0.3f);
        // latence : une impulsion ressort la_ échantillons plus tard (limiteur coupé)
        MasterChain m2;
        m2.prepare(sr, 512);
        std::vector<float> a1(512, 0.f), b1(512, 0.f);
        a1[10] = b1[10] = 0.5f;
        m2.process(a1.data(), b1.data(), 512);
        int at = -1;
        for (int i = 0; i < 512; ++i) if (std::fabs(a1[(size_t)i]) > 0.25f) at = i;
        CHECK(at == 10 + m2.latencySamples());
    }
    // --- EQ : cloche +6 dB à 1 kHz, Q 1 ; marche / arrêt sans saut ---
    {
        MasterChain mc;
        mc.prepare(sr, 512);
        mc.setEq(0, true, kMixBell, 1000.f, 6.f, 1.f);
        const int n = (int)sr;
        std::vector<float> L((size_t)n), R((size_t)n);
        for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = 0.1f * (float)std::sin(2.0 * kPi * 1000.0 * i / sr);
        for (int i = 0; i < n; i += 512) mc.process(&L[(size_t)i], &R[(size_t)i], std::min(512, n - i));
        float pk = 0.f;
        for (int i = n / 2; i < n; ++i) pk = std::max(pk, std::fabs(L[(size_t)i]));
        const float g = 20.f * std::log10(pk / 0.1f);
        std::printf("EQ cloche +6 dB @ 1 kHz : mesuré %+.2f dB, courbe %+.2f dB\n", g, MasterChain::responseDb(kMixBell, 1000.f, 6.f, 1.f, 1000.f, (float)sr));
        CHECK(std::fabs(g - 6.f) < 0.1f);
        CHECK(std::fabs(MasterChain::responseDb(kMixBell, 1000.f, 6.f, 1.f, 1000.f, (float)sr) - 6.f) < 0.05f);
        CHECK(std::fabs(MasterChain::responseDb(kMixHighShelf, 8000.f, 3.f, 0.7f, 20000.f, (float)sr) - 3.f) < 0.5f);
        // bascule marche / arrêt sur un sinus tenu
        MasterChain m3;
        m3.prepare(sr, 512);
        std::vector<float> a((size_t)n), b((size_t)n);
        for (int i = 0; i < n; ++i) a[(size_t)i] = b[(size_t)i] = 0.3f * (float)std::sin(2.0 * kPi * 440.0 * i / sr);
        float base = 0.f, jump = 0.f;
        for (int i = 1; i < n; ++i) base = std::max(base, std::fabs(a[(size_t)i] - a[(size_t)i - 1]));
        for (int i = 0; i < n; i += 512) {
            m3.setEq(1, (i / 4800) % 2 == 1, kMixBell, 440.f, 9.f, 1.f);
            m3.process(&a[(size_t)i], &b[(size_t)i], std::min(512, n - i));
        }
        for (int i = 1; i < n; ++i) jump = std::max(jump, std::fabs(a[(size_t)i] - a[(size_t)i - 1]));
        std::printf("EQ marche / arrêt : plus grand pas %.4f (sinus amplifié de 9 dB : %.4f)\n", jump, base * 2.82f);
        CHECK(jump < base * 2.82f * 1.2f);
    }
    // --- compresseur : passe-haut de détection ---
    {
        auto grFor = [&](double hz) {
            MasterChain mc;
            mc.prepare(sr, 512);
            mc.setComp(true, -30.f, 4.f);
            mc.reset();
            float gr = 0.f;
            std::vector<float> L(512), R(512);
            for (int b = 0; b < 200; ++b) {
                for (int i = 0; i < 512; ++i) L[(size_t)i] = R[(size_t)i] = 0.5f * (float)std::sin(2.0 * kPi * hz * (b * 512 + i) / sr);
                mc.process(L.data(), R.data(), 512);
                gr = mc.gainReductionDb();
            }
            return gr;
        };
        const float g40 = grFor(40.0), g1k = grFor(1000.0);
        std::printf("compresseur : réduction 40 Hz %.1f dB, 1 kHz %.1f dB\n", g40, g1k);
        CHECK(g1k - g40 > 6.f);
    }
    // --- CPU ---
    {
        MasterChain mc;
        mc.prepare(sr, 512);
        for (int k = 0; k < 6; ++k) mc.setEq(k, true, k % 3, 100.f * (float)(k + 1), 3.f, 1.f);
        mc.setComp(true, -20.f, 2.f);
        mc.setLimiter(true, -1.f);
        Meters mt;
        mt.prepare(sr);
        std::vector<float> L(512), R(512);
        Rng a;
        const auto t0 = std::chrono::steady_clock::now();
        const int blocks = (int)(sr * 10.0 / 512);
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < 512; ++i) { L[(size_t)i] = 0.3f * a.next(); R[(size_t)i] = 0.3f * a.next(); }
            mc.process(L.data(), R.data(), 512);
            mt.process(L.data(), R.data(), 512);
        }
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("CPU : 10 s de mastering (6 EQ, compresseur, limiteur) + mesures en %.3f s (%.2f %% d'un cœur)\n", el, el / 10.0 * 100.0);
    }
    std::printf(failures ? "%d échec(s)\n" : "tous les tests passent\n", failures);
    return failures ? 1 : 0;
}
