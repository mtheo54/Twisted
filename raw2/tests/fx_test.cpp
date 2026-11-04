// Tests du rack d'effets : stabilité aux réglages extrêmes, mesures (délai, réverbération, EQ,
// filtre, compresseur, repliement de la distorsion), absence de clic, DRY/WET, coût CPU.
#include "dsp/Effects.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace raw2;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("ÉCHEC %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const double kPi = 3.14159265358979323846;
static const char* kNames[kNumFx] = {"HYPER", "DIST", "FLANGER", "PHASER", "CHORUS", "DELAY", "REVERB", "COMP", "EQ", "FILTER", "VINTAGE", "PUMP"};

struct Rng {
    uint32_t s = 12345;
    float next() {  // 0..1
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(s & 0xFFFFFF) / 16777216.f;
    }
};

enum Sig { kNoise, kSaw, kImpulse, kSine };

// signal d'entrée stéréo, joué pendant `playSec` puis silence
static void makeInput(Sig sig, double sr, double seconds, double playSec, float freq, float amp,
                      std::vector<float>& L, std::vector<float>& R) {
    const size_t n = (size_t)(seconds * sr), play = (size_t)(playSec * sr);
    L.assign(n, 0.f);
    R.assign(n, 0.f);
    Rng rng;
    double ph = 0.0;
    for (size_t i = 0; i < std::min(n, play); ++i) {
        float l = 0.f, r = 0.f;
        switch (sig) {
            case kNoise: l = (rng.next() * 2.f - 1.f) * amp; r = (rng.next() * 2.f - 1.f) * amp; break;
            case kSaw: l = r = (float)(2.0 * ph - 1.0) * amp; break;
            case kImpulse: l = r = i == 0 ? amp : 0.f; break;
            case kSine: l = r = (float)std::sin(2.0 * kPi * ph) * amp; break;
        }
        ph += freq / sr;
        ph -= std::floor(ph);
        L[i] = l;
        R[i] = r;
    }
}

// traite tout le signal par blocs (tailles tirées dans `sizes` à tour de rôle)
static void run(FxRack& fx, const FxSettings& s, std::vector<float>& L, std::vector<float>& R,
                const std::vector<int>& sizes = {256}) {
    size_t pos = 0, k = 0;
    while (pos < L.size()) {
        const int b = (int)std::min<size_t>((size_t)sizes[k++ % sizes.size()], L.size() - pos);
        fx.process(L.data() + pos, R.data() + pos, b, s);
        pos += (size_t)b;
    }
}

static double energy(const std::vector<float>& L, const std::vector<float>& R, size_t a, size_t b) {
    double e = 0.0;
    for (size_t i = a; i < std::min(b, L.size()); ++i) e += (double)L[i] * L[i] + (double)R[i] * R[i];
    return e;
}

static double rmsDb(const std::vector<float>& x, size_t a, size_t b) {
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i];
    return 10.0 * std::log10(e / (double)(b - a) + 1e-30);
}

static bool allFinite(const std::vector<float>& L, const std::vector<float>& R, float& peak) {
    peak = 0.f;
    for (size_t i = 0; i < L.size(); ++i) {
        if (!std::isfinite(L[i]) || !std::isfinite(R[i])) return false;
        peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
    }
    return true;
}

static FxSettings solo(int k, float p0, float p1, float p2, float mix = 1.f) {
    FxSettings s;
    s.on[k] = true;
    s.p[k][0] = p0;
    s.p[k][1] = p1;
    s.p[k][2] = p2;
    s.mix[k] = mix;
    return s;
}

int main() {
    const double rates[2] = {48000.0, 44100.0};
    const float lv[3] = {0.f, 0.5f, 1.f};

    // ---- 1. chaque effet seul, réglages extrêmes, longue traîne : fini, borné, sans emballement
    {
        int runs = 0;
        float worst = 0.f;
        for (double sr : rates) {
            FxRack fx;
            fx.prepare(sr, 512);
            for (int k = 0; k < kNumFx; ++k) {
                const int modes = k == kFxDist ? 4 : 1, oss = k == kFxDist ? 3 : 1;
                for (int md = 0; md < modes; ++md)
                    for (int o = 0; o < oss; ++o)
                        for (int a = 0; a < 3; ++a)
                            for (Sig sig : {kNoise, kSaw, kImpulse}) {
                                FxSettings s = solo(k, lv[a], lv[a], lv[a]);
                                s.distMode = md;
                                s.oversample = o == 0 ? 1 : (o == 1 ? 2 : 4);
                                std::vector<float> L, R;
                                makeInput(sig, sr, 4.0, 1.0, 100.f, 1.f, L, R);
                                fx.reset();
                                run(fx, s, L, R, sig == kSaw ? std::vector<int>{1, 7, 511, 256} : std::vector<int>{256});
                                float pk = 0.f;
                                const bool fin = allFinite(L, R, pk);
                                const size_t S = (size_t)sr;
                                const double eAfter = energy(L, R, S, S + S / 2), eLast = energy(L, R, S * 7 / 2, S * 4);
                                const bool ok = fin && pk < 8.f && eLast <= eAfter * 1.1 + 1e-9;
                                if (!ok)
                                    std::printf("  %s mode %d os %d p %.1f sig %d sr %.0f : fini %d crête %.2f énergie %.3g -> %.3g\n",
                                                kNames[k], md, s.oversample, lv[a], (int)sig, sr, (int)fin, pk, eAfter, eLast);
                                CHECK(ok);
                                worst = std::max(worst, pk);
                                ++runs;
                            }
                // toutes les combinaisons des 3 boutons, rendu court
                for (int a = 0; a < 27; ++a)
                    for (Sig sig : {kNoise, kSaw}) {
                        FxSettings s = solo(k, lv[a % 3], lv[(a / 3) % 3], lv[a / 9]);
                        s.distMode = a % 4;
                        s.oversample = 1 << (a % 3);
                        std::vector<float> L, R;
                        makeInput(sig, sr, 1.0, 0.7, 100.f, 1.f, L, R);
                        fx.reset();
                        run(fx, s, L, R);
                        float pk = 0.f;
                        const bool fin = allFinite(L, R, pk);
                        if (!fin || pk >= 8.f) std::printf("  %s combinaison %d : fini %d crête %.2f\n", kNames[k], a, (int)fin, pk);
                        CHECK(fin && pk < 8.f);
                        worst = std::max(worst, pk);
                        ++runs;
                    }
            }
        }
        std::printf("extrêmes : %d rendus, crête max %.2f (%.1f dBFS)\n", runs, worst, 20.0 * std::log10(worst));
    }

    // ---- 2. les 10 ensemble, réglages et ordres aléatoires, blocs de tailles variées
    {
        Rng rng;
        rng.s = 777;
        float worst = 0.f;
        for (double sr : rates) {
            FxRack fx;
            fx.prepare(sr, 512);
            for (int trial = 0; trial < 12; ++trial) {
                std::vector<float> L, R;
                makeInput(trial & 1 ? kSaw : kNoise, sr, 3.0, 2.0, 55.f + 50.f * (float)trial, 0.7f, L, R);
                FxSettings s;
                const size_t seg = (size_t)(sr * 0.5);
                bool fin = true;
                for (size_t pos = 0; pos < L.size(); pos += seg) {  // réglages changés toutes les 0,5 s
                    for (int k = 0; k < kNumFx; ++k) {
                        s.on[k] = rng.next() < 0.85f;
                        for (int j = 0; j < 3; ++j) s.p[k][j] = rng.next();
                        s.mix[k] = rng.next();
                        s.order[k] = k;
                    }
                    for (int k = kNumFx - 1; k > 0; --k) std::swap(s.order[k], s.order[(int)(rng.next() * (float)(k + 1)) % (k + 1)]);
                    s.distMode = (int)(rng.next() * 4.f) % 4;
                    s.oversample = 1 << ((int)(rng.next() * 3.f) % 3);
                    for (float& f : s.eqFreq) f = rng.next();
                    s.wet = rng.next();
                    const size_t len = std::min(seg, L.size() - pos);
                    std::vector<float> l(L.begin() + (long)pos, L.begin() + (long)(pos + len)), r(R.begin() + (long)pos, R.begin() + (long)(pos + len));
                    run(fx, s, l, r, {1, 7, 256, 511, 64});
                    std::copy(l.begin(), l.end(), L.begin() + (long)pos);
                    std::copy(r.begin(), r.end(), R.begin() + (long)pos);
                }
                float pk = 0.f;
                fin = allFinite(L, R, pk);
                // borne de non-emballement : trois cloches à +12 dB et +12 dB de rattrapage du
                // compresseur peuvent légitimement porter une crête bien au-delà de 0 dBFS
                CHECK(fin && pk < 10.f);
                worst = std::max(worst, pk);
            }
        }
        std::printf("chaîne complète aléatoire : crête max %.2f\n", worst);
    }

    // ---- 3. délai : l'impulsion revient à TIME ± 1 ms
    for (double sr : rates) {
        for (float t : {0.f, 0.3f, 1.f}) {
            FxRack fx;
            fx.prepare(sr, 256);
            FxSettings s = solo(kFxDly, t, 0.f, 0.f);
            std::vector<float> L, R;
            makeInput(kImpulse, sr, 1.2, 1.2, 0.f, 1.f, L, R);
            run(fx, s, L, R);
            size_t best = 0;
            for (size_t i = 0; i < L.size(); ++i)
                if (std::fabs(L[i]) > std::fabs(L[best])) best = i;
            const double ms = 1000.0 * (double)best / sr, want = 20.0 + 980.0 * t;
            std::printf("délai %.0f Hz : TIME %.0f ms -> écho à %.2f ms (%.2f)\n", sr, want, ms, L[best]);
            CHECK(std::fabs(ms - want) <= 1.0);
        }
    }

    // ---- 4. réverbération : RT60 (T30 x 2, intégrale de Schroeder) croît avec SIZE
    {
        const double sr = 48000.0;
        double prev = 0.0;
        for (float size : {0.1f, 0.5f, 0.9f}) {
            FxRack fx;
            fx.prepare(sr, 256);
            FxSettings s = solo(kFxRev, size, 0.f, 0.f);
            std::vector<float> L, R;
            makeInput(kImpulse, sr, 14.0, 14.0, 0.f, 1.f, L, R);
            run(fx, s, L, R);
            std::vector<double> edc(L.size() + 1, 0.0);
            for (size_t i = L.size(); i-- > 0;) edc[i] = edc[i + 1] + (double)L[i] * L[i] + (double)R[i] * R[i];
            const auto timeAt = [&](double db) {
                const double lim = edc[0] * std::pow(10.0, db / 10.0);
                for (size_t i = 0; i < L.size(); ++i)
                    if (edc[i] < lim) return (double)i / sr;
                return 14.0;
            };
            const double rt = (timeAt(-35.0) - timeAt(-5.0)) * 2.0, nominal = 0.3 + 7.0 * size;
            std::printf("réverbération SIZE %.1f : RT60 mesuré %.2f s (nominal %.2f s)\n", size, rt, nominal);
            CHECK(rt > prev);
            CHECK(rt > nominal * 0.6 && rt < nominal * 1.5);
            float pk = 0.f;
            CHECK(allFinite(L, R, pk));
            prev = rt;
        }
    }

    // ---- 5. EQ : +12 dB sur une bande relève une sinusoïde à sa fréquence de 12 ± 1 dB
    for (double sr : rates) {
        for (int b = 0; b < 3; ++b) {
            FxRack fx;
            fx.prepare(sr, 256);
            FxSettings s = solo(kFxEq, 0.5f, 0.5f, 0.5f);
            s.p[kFxEq][b] = 1.f;
            const float f = 20.f * std::pow(1000.f, s.eqFreq[b]);
            std::vector<float> L, R;
            makeInput(kSine, sr, 1.0, 1.0, f, 0.25f, L, R);
            const std::vector<float> in = L;
            run(fx, s, L, R);
            const size_t a = (size_t)(sr * 0.5);
            const double g = rmsDb(L, a, L.size()) - rmsDb(in, a, L.size());
            std::printf("EQ bande %d (%.0f Hz, %.0f Hz) : %+.2f dB\n", b, f, sr, g);
            CHECK(std::fabs(g - 12.0) <= 1.0);
        }
    }

    // ---- 6. FILTRE : coupure 1 kHz, 4 kHz atténué de plus de 18 dB par rapport à 250 Hz
    for (double sr : rates) {
        double lvl[2];
        for (int t = 0; t < 2; ++t) {
            FxRack fx;
            fx.prepare(sr, 256);
            FxSettings s = solo(kFxFilt, (float)(std::log10(1000.0 / 20.0) / 3.0), 0.f, 0.f);
            std::vector<float> L, R;
            makeInput(kSine, sr, 0.6, 0.6, t ? 4000.f : 250.f, 0.5f, L, R);
            run(fx, s, L, R);
            lvl[t] = rmsDb(L, (size_t)(sr * 0.3), L.size());
        }
        std::printf("filtre %.0f Hz : 250 Hz %.1f dB, 4 kHz %.1f dB (écart %.1f dB)\n", sr, lvl[0], lvl[1], lvl[0] - lvl[1]);
        CHECK(lvl[0] - lvl[1] > 18.0);
    }

    // ---- 7. compresseur : -40 dB, 20:1
    {
        const double sr = 48000.0;
        FxRack fx;
        fx.prepare(sr, 256);
        FxSettings s = solo(kFxComp, 1.f, 1.f, 0.f);
        std::vector<float> L, R;
        makeInput(kSine, sr, 1.0, 1.0, 440.f, 0.9f, L, R);
        const std::vector<float> in = L;
        run(fx, s, L, R);
        const size_t a = (size_t)(sr * 0.5);
        const double red = rmsDb(in, a, L.size()) - rmsDb(L, a, L.size());
        std::printf("compresseur : réduction %.1f dB, affichage %.1f dB\n", red, fx.compGainReductionDb());
        CHECK(red > 20.0);
        CHECK(fx.compGainReductionDb() > 10.f);
        FxSettings off = s;
        off.on[kFxComp] = false;
        run(fx, off, L, R);
        CHECK(fx.compGainReductionDb() == 0.f);
    }

    // ---- 8. distorsion : moins de repliement en 4x qu'en 1x (sinusoïde 7 kHz, DRIVE à fond)
    {
        const double sr = 48000.0;
        const size_t N = 16384;
        double alias[3] = {0, 0, 0};
        for (int o = 0; o < 3; ++o) {
            FxRack fx;
            fx.prepare(sr, 256);
            FxSettings s = solo(kFxDist, 1.f, 1.f, 0.5f);
            s.distMode = 1;
            s.oversample = 1 << o;
            std::vector<float> L, R;
            makeInput(kSine, sr, 1.0, 1.0, 7000.f, 0.5f, L, R);
            run(fx, s, L, R);
            std::vector<std::complex<double>> X(N);
            for (size_t i = 0; i < N; ++i) {  // fenêtre de Blackman-Harris
                const double w = 0.35875 - 0.48829 * std::cos(2 * kPi * (double)i / (double)N) + 0.14128 * std::cos(4 * kPi * (double)i / (double)N) - 0.01168 * std::cos(6 * kPi * (double)i / (double)N);
                X[i] = std::complex<double>(L[L.size() - N + i] * w, 0.0);
            }
            fft(X, false);
            double tot = 0.0, harm = 0.0;
            for (size_t b = 0; b <= N / 2; ++b) {
                const double e = std::norm(X[b]);
                tot += e;
                const double f = (double)b * sr / (double)N;
                for (int h = 0; h <= 3; ++h)
                    if (std::fabs(f - 7000.0 * h) < 6.0 * sr / (double)N) { harm += e; break; }
            }
            alias[o] = 10.0 * std::log10((tot - harm) / tot + 1e-30);
            std::printf("distorsion %dx : énergie hors harmoniques %.1f dB\n", 1 << o, alias[o]);
        }
        CHECK(alias[2] < alias[0] - 15.0);
        CHECK(alias[1] < alias[0] - 6.0);
    }

    // ---- 9a. DRY/WET à 0 : la sortie redevient exactement l'entrée
    {
        const double sr = 48000.0;
        FxRack fx;
        fx.prepare(sr, 512);
        FxSettings s;
        Rng rng;
        for (int k = 0; k < kNumFx; ++k) {
            s.on[k] = true;
            s.mix[k] = 0.7f;
            for (int j = 0; j < 3; ++j) s.p[k][j] = rng.next();
        }
        std::vector<float> L, R;
        makeInput(kNoise, sr, 1.0, 1.0, 0.f, 0.5f, L, R);
        run(fx, s, L, R);
        s.wet = 0.f;
        makeInput(kSaw, sr, 1.0, 1.0, 110.f, 0.5f, L, R);
        const std::vector<float> inL = L, inR = R;
        run(fx, s, L, R, {256, 7, 1});
        float err = 0.f;
        for (size_t i = (size_t)(sr * 0.3); i < L.size(); ++i)
            err = std::max(err, std::max(std::fabs(L[i] - inL[i]), std::fabs(R[i] - inR[i])));
        std::printf("DRY/WET 0 : écart max %.2g\n", err);
        CHECK(err <= 1e-6f);
    }

    // ---- 9b. marche / arrêt, ordre, forme de distorsion, TIME : pas de clic
    {
        const double sr = 48000.0;
        const auto maxStep = [](const std::vector<float>& x, size_t a, size_t b) {
            float m = 0.f;
            for (size_t i = a + 1; i < b; ++i) m = std::max(m, std::fabs(x[i] - x[i - 1]));
            return m;
        };
        // rendu de 1,5 s : réglages `a` puis `b` à 0,75 s ; renvoie le saut max autour du changement
        // et le saut max en régime établi avant / après
        const auto probe = [&](const FxSettings& a, const FxSettings& b, float& around, float& steady) {
            FxRack fx;
            fx.prepare(sr, 256);
            std::vector<float> L, R;
            makeInput(kSine, sr, 1.5, 1.5, 220.f, 0.5f, L, R);
            const size_t half = (size_t)(sr * 0.75);
            for (size_t pos = 0; pos < L.size(); pos += 256)
                fx.process(L.data() + pos, R.data() + pos, (int)std::min<size_t>(256, L.size() - pos), pos < half ? a : b);
            const size_t w = (size_t)(sr * 0.1);
            around = maxStep(L, half - 256, half + w);
            steady = std::max(maxStep(L, half - w * 3, half - 256), maxStep(L, half + w * 2, half + w * 5));
        };
        const float dry = 0.5f * 2.f * (float)kPi * 220.f / (float)sr;
        float worst = 0.f;
        for (int k = 0; k < kNumFx; ++k) {
            FxSettings on = solo(k, 0.5f, 0.6f, 0.4f, 1.f), off = on;
            off.on[k] = false;
            float ar = 0.f, st = 0.f, ar2 = 0.f, st2 = 0.f;
            probe(off, on, ar, st);
            probe(on, off, ar2, st2);
            const float lim = 1.3f * std::max(st, dry) + 0.005f, lim2 = 1.3f * std::max(st2, dry) + 0.005f;
            if (ar > lim || ar2 > lim2) std::printf("  clic %s : %.4f / %.4f, %.4f / %.4f\n", kNames[k], ar, lim, ar2, lim2);
            CHECK(ar <= lim && ar2 <= lim2);
            worst = std::max(worst, std::max(ar / lim, ar2 / lim2));
        }
        {  // permutation de l'ordre avec plusieurs effets actifs
            FxSettings a;
            for (int k : {kFxDist, kFxEq, kFxFilt, kFxChor}) { a.on[k] = true; a.mix[k] = 0.8f; a.p[k][0] = a.p[k][1] = a.p[k][2] = 0.5f; }
            FxSettings b = a;
            std::swap(b.order[kFxDist], b.order[kFxFilt]);
            float ar = 0.f, st = 0.f;
            probe(a, b, ar, st);
            const float lim = 1.3f * std::max(st, dry) + 0.005f;
            std::printf("permutation : saut %.4f (limite %.4f)\n", ar, lim);
            CHECK(ar <= lim);
            // forme et suréchantillonnage de la distorsion
            FxSettings c = a;
            c.distMode = 3;
            c.oversample = 4;
            probe(a, c, ar, st);
            const float lim2 = 1.3f * std::max(st, dry) + 0.005f;
            std::printf("changement de distorsion : saut %.4f (limite %.4f)\n", ar, lim2);
            CHECK(ar <= lim2);
        }
        {  // TIME du délai : la tête de lecture glisse
            FxSettings a = solo(kFxDly, 0.2f, 0.3f, 0.3f, 0.5f), b = a;
            b.p[kFxDly][0] = 0.25f;
            float ar = 0.f, st = 0.f;
            probe(a, b, ar, st);
            const float lim = 2.5f * std::max(st, dry) + 0.005f;
            std::printf("TIME du délai : saut %.4f (limite %.4f)\n", ar, lim);
            CHECK(ar <= lim);
        }
        std::printf("marche / arrêt : pire rapport saut / limite %.2f\n", worst);
    }

    // ---- 10. niveaux : -6 dBFS en entrée, MIX 30 % puis 100 %, boutons au milieu
    {
        const double sr = 48000.0;
        std::printf("niveaux (scie 110 Hz à -6 dBFS) :\n");
        for (int k = 0; k < kNumFx; ++k) {
            double d[2];
            for (int mi = 0; mi < 2; ++mi) {
                FxRack fx;
                fx.prepare(sr, 256);
                FxSettings s = solo(k, 0.5f, 0.5f, 0.5f, mi ? 1.f : 0.3f);
                if (k == kFxComp) s.p[k][2] = 0.f;  // sans gain de compensation
                std::vector<float> L, R;
                makeInput(kSaw, sr, 2.0, 2.0, 110.f, 0.5f, L, R);
                const std::vector<float> in = L;
                run(fx, s, L, R);
                const size_t a = (size_t)(sr * 1.0);
                d[mi] = 0.5 * (rmsDb(L, a, L.size()) + rmsDb(R, a, R.size())) - rmsDb(in, a, L.size());
            }
            std::printf("  %-8s MIX 30 %% : %+5.1f dB   MIX 100 %% : %+5.1f dB\n", kNames[k], d[0], d[1]);
            CHECK(std::fabs(d[0]) < 4.0);
            if (k != kFxDist && k != kFxComp) CHECK(std::fabs(d[1]) < 9.0);  // gain voulu
        }
    }

    // ---- 11. CPU : les 10 effets, distorsion 4x, 10 s de stéréo
    {
        const double sr = 48000.0;
        FxRack fx;
        fx.prepare(sr, 256);
        FxSettings s;
        for (int k = 0; k < kNumFx; ++k) {
            s.on[k] = true;
            s.mix[k] = 0.5f;
            s.p[k][0] = s.p[k][1] = s.p[k][2] = 0.6f;
        }
        s.p[kFxHyper][2] = 1.f;  // 7 voix
        s.oversample = 4;
        std::vector<float> L, R;
        makeInput(kNoise, sr, 10.0, 10.0, 0.f, 0.5f, L, R);
        const auto t0 = std::chrono::steady_clock::now();
        run(fx, s, L, R);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("CPU : 10 s traitées en %.3f s (facteur temps réel %.4f, %.0fx plus vite que le temps réel)\n", el, el / 10.0, 10.0 / el);
        // la limite de temps n'a de sens que pour une version optimisée, sans sanitizers
#if defined(__SANITIZE_ADDRESS__)
#define RAW2_FX_TIMING 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RAW2_FX_TIMING 0
#endif
#endif
#ifndef RAW2_FX_TIMING
#ifdef NDEBUG
#define RAW2_FX_TIMING 1
#else
#define RAW2_FX_TIMING 0
#endif
#endif
        if (RAW2_FX_TIMING) CHECK(el < 1.0);
        float pk = 0.f;
        CHECK(allFinite(L, R, pk));
    }

    std::printf(failures ? "%d échec(s)\n" : "tous les tests passent\n", failures);
    return failures ? 1 : 0;
}
