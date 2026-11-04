// Tests du cœur : presets, tables, moteur audio (pas de NaN, niveau raisonnable), vie, état.
#include "core/FactoryData.h"
#include "core/Files.h"
#include "core/Instruments.h"
#include "core/FactoryFx.h"
#include "core/Model.h"
#include "core/Wav.h"
#include "dsp/Engine.h"
#include "dsp/WavetableOps.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace raw2;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("ÉCHEC %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void waitTables() { std::this_thread::sleep_for(std::chrono::milliseconds(400)); }

static float render(Model& m, Engine& e, int note, float seconds, float* peakOut, float* rmsOut) {
    const int sr = 48000, block = 256;
    std::vector<float> L(block), R(block);
    double sum = 0.0;
    float peak = 0.f;
    long n = 0;
    bool nanSeen = false;
    const int blocks = (int)(seconds * sr / block);
    for (int b = 0; b < blocks; ++b) {
        NoteEvent ev[1];
        int nev = 0;
        if (b == 0) { ev[0] = NoteEvent{0, note, 0.9f, true}; nev = 1; }
        if (b == blocks * 3 / 4) { ev[0] = NoteEvent{10, note, 0.f, false}; nev = 1; }
        e.process(L.data(), R.data(), block, ev, nev);
        for (int i = 0; i < block; ++i) {
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) nanSeen = true;
            peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
            sum += (double)L[i] * L[i];
            ++n;
        }
    }
    (void)m;
    CHECK(!nanSeen);
    if (peakOut) *peakOut = peak;
    if (rmsOut) *rmsOut = (float)std::sqrt(sum / std::max(1L, n));
    return peak;
}

int main() {
    // formes de base : normalisées et finies
    for (int s = 0; s < 8; ++s) {
        Wavetable t;
        buildFromSpec(t, ShapeSpec{(Shape)s, 0.f, 1.f}, "x");
        float pk = 0.f;
        for (auto& f : t.fr) for (float v : f) { CHECK(std::isfinite(v)); pk = std::max(pk, std::fabs(v)); }
        CHECK(std::fabs(pk - 1.f) < 1e-3f);
    }
    // mipmaps : niveau 0 proche de la table d'origine pour une sinusoïde
    {
        Wavetable t;
        buildFromSpec(t, ShapeSpec{Shape::Sine, 0.f, 0.f}, "sin");
        MipTable mt;
        mt.build(t);
        float err = 0.f;
        for (int i = 0; i < 256; ++i) err = std::max(err, std::fabs(mt.read(0, 0.f, i / 256.f) - t.fr[0][i]));
        CHECK(err < 1e-3f);
        CHECK(MipTable::levelFor(40.f / 48000.f) == 0);
        CHECK(MipTable::levelFor(60.f / 48000.f) == 1);
        CHECK(MipTable::levelFor(440.f / 48000.f) == 7);
        CHECK(MipTable::levelFor(5000.f / 48000.f) == 14);
        // fondu entre niveaux : continu quand la note monte (même lecture des deux côtés d'un seuil)
        float worst = 0.f;
        for (int i = 1; i < 4000; ++i) {
            const float i0 = 20.f * std::pow(1000.f, (float)(i - 1) / 4000.f) / 48000.f, i1 = 20.f * std::pow(1000.f, (float)i / 4000.f) / 48000.f;
            float m0, m1;
            const int l0 = MipTable::levelMix(i0, m0), l1 = MipTable::levelMix(i1, m1);
            const float e0 = (float)l0 + m0, e1 = (float)l1 + m1;  // niveau « effectif »
            worst = std::max(worst, std::fabs(e1 - e0));
        }
        std::printf("fondu des mipmaps : plus grand pas %.3f niveau\n", worst);
        CHECK(worst < 0.05f);
    }

    Model m;
    Engine e(m);
    e.prepare(48000.0, 256);
    waitTables();

    // les 72 presets : son présent, pas de NaN, crête raisonnable
    float minRms = 1e9f, maxPeak = 0.f;
    for (int i = 0; i < kNumFactoryPresets; ++i) {
        m.applyFactoryPreset(i, nullptr);
        waitTables();
        m.panic = true;
        float pk = 0.f, rms = 0.f;
        const int note = kFactoryPresets[i].category == Category::Bass ? 36 : 60;
        render(m, e, note, 1.0f, &pk, &rms);
        minRms = std::min(minRms, rms);
        maxPeak = std::max(maxPeak, pk);
        std::printf("%02d %-18s crête %6.1f dBFS  rms %6.1f dBFS\n", i + 1, kFactoryPresets[i].name, 20 * std::log10(pk + 1e-9), 20 * std::log10(rms + 1e-9));
        CHECK(pk > 0.01f);
        CHECK(pk <= 1.0f);
    }

    // matrice, modules et chaîne de mix à fond : toujours stable
    for (int s = 0; s < kNumSources; ++s)
        for (int d = 0; d < kNumDests; ++d) { m.matrix.on[(size_t)ModMatrix::idx(s, d)] = true; m.matrix.amt[(size_t)ModMatrix::idx(s, d)] = (s * 37 + d * 11) % 200 - 100; }
    m.params.set(kM1, 100); m.params.set(kM2, -100); m.params.set(kM3, 100); m.params.set(kReso, 1.f);
    m.mix.addFilter(0, 250.f, -9.f, 1.2f); m.mix.addFilter(1, 8000.f, 9.f, 1.f); m.mix.addFilter(2, 60.f, 9.f, 0.7f); m.mix.addFilter(0, 3500.f, -9.f, 8.f);
    m.mix.comp = true; m.mix.limit = true;
    for (int t = 0; t < 3; ++t) {
        m.params.set(kTarget, (float)t);
        float pk = 0.f;
        render(m, e, 84, 0.5f, &pk, nullptr);
        CHECK(pk <= 1.0f);
    }

    // WAV : écriture puis relecture d'une table
    {
        std::vector<unsigned char> w = wavetableToWav(m.edit[0]);
        CHECK(w.size() == 44 + 64 * 2048 * 2);
        std::vector<float> data;
        CHECK(parseWav(w, data));
        Wavetable t;
        CHECK(tableFromAudio(data, "test", t));
        float err = 0.f;
        for (int k = 0; k < 64; k += 9)
            for (int i = 0; i < 256; ++i) err = std::max(err, std::fabs(t.fr[k][i] - m.edit[0].fr[k][i]));
        std::printf("aller-retour WAV : écart max %.3f\n", err);
        CHECK(err < 0.2f);
    }

    // état : sauvegarde puis rechargement
    {
        m.applyFactoryPreset(7, nullptr);
        m.params.set(kM1, -33);
        m.matrix.clear();
        m.matrix.on[5] = true; m.matrix.amt[5] = -42;
        const std::string st = m.saveState();
        Model m2;
        CHECK(m2.loadState(st, nullptr));
        CHECK(std::fabs(m2.params.get(kM1) + 33.f) < 0.01f);
        CHECK(m2.matrix.on[5] && m2.matrix.amt[5] == -42);
        CHECK(m2.presetName == "rage saw");
        std::printf("état : %zu octets\n", st.size());
    }

    // la vie avance
    {
        LifeView a, b;
        m.params.set(kSpeed, 1.f);
        m.params.set(kFreeze, 0.f);
        m.life.view(LifeEngine::Big, a);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        m.life.view(LifeEngine::Big, b);
        CHECK(b.gen > a.gen);
    }

    // effets des presets d'usine : recettes valides, et deux presets n'ont jamais le même jeu d'effets
    {
        std::set<int> sets;
        int firsts[kNumFx] = {};
        for (int i = 0; i < kNumFactoryPresets; ++i) {
            FactoryFxSetup fx;
            std::string err;
            const bool ok = factoryFxSetup(i, fx, &err);
            if (!ok) std::printf("  %s\n", err.c_str());
            CHECK(ok);
            int mask = 0, n = 0;
            for (int f = 0; f < kNumFx; ++f)
                if (fx.on[f]) { mask |= 1 << f; ++n; }
            CHECK(n >= 2 && n <= 6);
            CHECK(sets.insert(mask).second);
            ++firsts[fx.order[0]];
            bool seen[kNumFx] = {};
            for (int k = 0; k < kNumFx; ++k) { CHECK(!seen[fx.order[k]]); seen[fx.order[k]] = true; }
            // le preset chargé porte bien sa recette
            Model pm(Model::Mode::Offline);
            pm.applyFactoryPreset(i, nullptr);
            for (int f = 0; f < kNumFx; ++f) CHECK((pm.params.get(fxParam(f, kFxOn)) > 0.5f) == fx.on[f]);
            CHECK(pm.fxOrder[0].load() == fx.order[0]);
        }
        int kinds = 0;
        for (int c : firsts) kinds += c > 0;
        std::printf("effets des presets : %d jeux d'effets différents, %d effets différents en tête du rack\n", (int)sets.size(), kinds);
        CHECK((int)sets.size() == kNumFactoryPresets);
        CHECK(kinds >= 6);
    }

    // packs : une entrée « raw2-pack » de la maquette (preset de base + retouches, clés de la maquette)
    {
        Model pm(Model::Mode::Offline);
        json::Value e;
        CHECK(json::Value::parse(R"({"name":"Essai","tags":"basse","base":7,"p":{"aLvl":0.9,"eA_a":0.5,"eB_s":0.25,"l2_r":0.7,"x_dist_m":0.66,"x_rev_md":-40,"m1":60},
                                     "oct":{"a":-1,"b":1},"mode":{"a":3,"b":4},"rule":"hl","fx":{"on":{"rev":true,"hyper":false}},"wt":[["saw",0,1],["glass",0.2,0.8]]})", e));
        CHECK(pm.applyPackEntry(e, nullptr));
        CHECK(std::fabs(pm.params.get(kALvl) - 0.9f) < 1e-5f);
        CHECK(std::fabs(pm.params.get(kAttack) - 0.5f) < 1e-5f);
        CHECK(std::fabs(pm.params.get(kEnvBSustain) - 0.25f) < 1e-5f);
        CHECK(std::fabs(pm.params.get(kLfo2Rate) - 0.7f) < 1e-5f);
        CHECK(std::fabs(pm.params.get(fxParam(kFxDist, kFxMix)) - 0.66f) < 1e-5f);
        CHECK(std::fabs(pm.params.get(fxParam(kFxRev, kFxModDepth)) + 40.f) < 1e-4f);
        CHECK(pm.params.get(kM1) == 60.f && pm.params.get(kAOct) == -1.f && pm.params.get(kBOct) == 1.f);
        CHECK(pm.params.get(kAMode) == 3.f && pm.params.get(kRule) == 1.f);
        CHECK(pm.params.get(fxParam(kFxRev, kFxOn)) > 0.5f && pm.params.get(fxParam(kFxHyper, kFxOn)) < 0.5f);
        CHECK(pm.presetName == "Essai" && pm.presetCategory == "basse");
        // INIT : un sinus, effets coupés, matrice vide
        pm.applyInit(nullptr);
        CHECK(pm.presetIndex == Model::kPresetInit && pm.params.get(kBLvl) == 0.f);
        for (int f = 0; f < kNumFx; ++f) CHECK(pm.params.get(fxParam(f, kFxOn)) < 0.5f);
        // un preset complet (.raw) dans un pack passe tel quel
        pm.applyFactoryPreset(20, nullptr);
        const json::Value full = pm.presetJson();
        pm.applyInit(nullptr);
        CHECK(pm.applyPackEntry(full, nullptr));
        CHECK(pm.presetName == kFactoryPresets[20].name);
        // ancien preset (version 2) : l'enveloppe B reprend l'enveloppe A
        json::Value old = full;
        old.set("version", 2);
        CHECK(pm.applyPresetJson(old, nullptr));
        CHECK(std::fabs(pm.params.get(kEnvBAttack) - pm.params.get(kAttack)) < 1e-6f);
    }

    // anti-clic : rendu indépendant du découpage de l'hôte (blocs variables façon FL Studio),
    // pas de continu en sortie, automation à l'échantillon près, PANIC en fondu
    {
        auto play = [](int preset, bool randomBlocks, std::vector<float>& out, int paramAt) {
            Model pm(Model::Mode::Offline);
            if (preset < 0) pm.applyInit(nullptr);
            else pm.applyFactoryPreset(preset, nullptr);
            pm.life.restart(99);
            Engine pe(pm);
            pe.prepare(48000.0, 512);
            const int total = 48000, tOff = 36000;
            out.assign((size_t)total * 2, 0.f);
            std::vector<float> L(512), R(512);
            uint32_t rng = 7;
            for (int pos = 0; pos < total;) {
                rng = rng * 1664525u + 1013904223u;
                const int n = std::min(total - pos, randomBlocks ? 1 + (int)(rng >> 23) % 500 : 64);
                NoteEvent ev[2];
                int ne = 0;
                if (pos == 0) ev[ne++] = NoteEvent{0, 60, 0.9f, true};
                if (tOff >= pos && tOff < pos + n) ev[ne++] = NoteEvent{tOff - pos, 60, 0.f, false};
                ParamEvent pv[1];
                int np = 0;
                if (paramAt >= pos && paramAt < pos + n) pv[np++] = ParamEvent{paramAt - pos, kMaster, 0.f};
                pe.process(L.data(), R.data(), n, ev, ne, pv, np);
                for (int i = 0; i < n; ++i) { out[(size_t)(pos + i) * 2] = L[(size_t)i]; out[(size_t)(pos + i) * 2 + 1] = R[(size_t)i]; }
                pos += n;
            }
        };
        std::vector<float> a, b;
        play(-1, false, a, -1);
        play(-1, true, b, -1);
        float diff = 0.f, peak = 0.f, jump = 0.f;
        for (size_t i = 0; i < a.size(); ++i) { diff = std::max(diff, std::fabs(a[i] - b[i])); peak = std::max(peak, std::fabs(a[i])); }
        for (size_t i = 2; i < b.size(); i += 2) jump = std::max(jump, std::fabs(b[i] - b[i - 2]));
        std::printf("blocs variables : écart %.2e (crête %.3f), saut max %.4f\n", diff, peak, jump);
        CHECK(peak > 0.05f && diff < 1e-3f * peak);
        CHECK(jump < 0.05f);  // sinus de 262 Hz : aucun saut au départ ni à l'arrêt
        // continu : un pulse étroit (candy square) ne laisse pas de composante continue
        play(31, false, a, -1);
        double sum = 0.0, sq = 0.0;
        for (size_t i = 9600; i < 72000; ++i) { sum += a[i]; sq += (double)a[i] * a[i]; }
        const double mean = sum / 62400.0, rms = std::sqrt(sq / 62400.0);
        std::printf("continu : %.4f pour un RMS de %.4f\n", mean, rms);
        CHECK(std::fabs(mean) < 0.02 * rms);
        // automation : MASTER à 0 posé à l'échantillon 20000, le son ne bouge pas avant et baisse après
        play(-1, true, a, 20000);
        play(-1, true, b, -1);
        float before = 0.f, after = 0.f;
        for (int i = 0; i < 20000 * 2; ++i) before = std::max(before, std::fabs(a[(size_t)i] - b[(size_t)i]));
        for (int i = 22000 * 2; i < 24000 * 2; ++i) after = std::max(after, std::fabs(a[(size_t)i]));
        std::printf("automation : écart avant %.2e, niveau 40 ms après %.2e\n", before, after);
        CHECK(before < 1e-6f && after < 0.05f * peak);  // lissage de 12 ms
        // PANIC : fondu de 5 ms, pas de coupure sèche
        Model pm(Model::Mode::Offline);
        pm.applyFactoryPreset(25, nullptr);
        Engine pe(pm);
        pe.prepare(48000.0, 256);
        std::vector<float> L(256), R(256);
        float last = 0.f, pjump = 0.f, lvl = 0.f;
        for (int blk = 0; blk < 120; ++blk) {
            NoteEvent ev{0, 60, 0.9f, true};
            if (blk == 100) pm.panic = true;
            pe.process(L.data(), R.data(), 256, &ev, blk == 0 ? 1 : 0);
            for (int i = 0; i < 256; ++i) {
                if (blk >= 100) pjump = std::max(pjump, std::fabs(L[(size_t)i] - last));
                else if (blk > 50) lvl = std::max(lvl, std::fabs(L[(size_t)i] - last));
                last = L[(size_t)i];
            }
        }
        std::printf("PANIC : saut max %.4f (saut normal du son %.4f)\n", pjump, lvl);
        CHECK(pjump <= lvl * 1.5f + 1e-4f);
    }

    // un preset chargé après un autre sonne exactement comme chargé à neuf (même graine de vie) :
    // ni l'âge des cellules, ni le taux de naissances, ni la valeur ALÉA du son d'avant ne débordent
    {
        auto play = [](Model& m) {
            Engine e(m);
            e.prepare(48000.0, 256);
            std::vector<float> out;
            float L[256], R[256];
            for (int b = 0; b < 600; ++b) {  // 3,2 s : plusieurs générations de la vie
                NoteEvent ev{0, 60, 0.8f, true};
                e.process(L, R, 256, &ev, b == 0 ? 1 : 0);
                out.insert(out.end(), L, L + 256);
            }
            return out;
        };
        const int pairs[3][2] = {{57, 60}, {39, 42}, {63, 66}};
        float worst = 0.f;
        for (const auto& pr : pairs) {
            Model a(Model::Mode::Offline), b(Model::Mode::Offline);
            a.life.restart(9);
            a.applyFactoryPreset(pr[1], nullptr);
            b.applyFactoryPreset(pr[0], nullptr);
            play(b);
            b.params.resetDefaults();
            b.life.restart(9);
            b.applyFactoryPreset(pr[1], nullptr);
            const auto x = play(a), y = play(b);
            for (size_t i = 0; i < x.size(); ++i) worst = std::max(worst, std::fabs(x[i] - y[i]));
        }
        std::printf("preset rechargé après un autre : écart max %.3g\n", worst);
        CHECK(worst == 0.f);
    }

    // la Vie : un seul monde ; la page SON en montre la zone écoutée, et NAISSANCE l'entretient
    {
        Model m(Model::Mode::Offline);
        m.applyFactoryPreset(0, nullptr);
        m.params.set(kBirth, 0.3f);
        m.life.restart(3);
        m.life.reset(LifeEngine::Big);
        Engine e(m);
        e.prepare(48000.0, 512);
        float L[512], R[512];
        int minAlive = 1 << 30;
        bool same = true;
        for (int sec = 0; sec < 90; ++sec) {
            for (int b = 0; b < 94; ++b) {
                NoteEvent ev{0, 60, 0.8f, true};
                e.process(L, R, 512, &ev, sec == 0 && b == 0 ? 1 : 0);
            }
            LifeView sv, bv;
            m.life.view(LifeEngine::Small, sv);
            m.life.view(LifeEngine::Big, bv);
            for (int i = 0; i < sv.GW * sv.GH; ++i) {
                const int j = (LifeEngine::kWinY + i / sv.GW) * bv.GW + LifeEngine::kWinX + i % sv.GW;
                same = same && sv.g[(size_t)i] == bv.g[(size_t)j];
            }
            if (sec >= 10) minAlive = std::min(minAlive, sv.alive);
        }
        std::printf("vie : zone écoutée = fenêtre du monde %d, au moins %d cellules vivantes sur 90 s (NAISSANCE 30 %%)\n", (int)same, minAlive);
        CHECK(same);
        CHECK(minAlive >= 5);
    }

    // instrument multi-échantillonné (SFZ + WAV) : zones, boucle, hauteur jouée
    {
        const std::string dir = "raw2_test_inst";
        files::makeDirs(dir + "/samples");
        auto sineWav = [&](const std::string& path, double hz, int frames) {
            std::vector<float> L((size_t)frames), R((size_t)frames);
            for (int i = 0; i < frames; ++i) L[(size_t)i] = R[(size_t)i] = 0.5f * (float)std::sin(2.0 * 3.14159265358979 * hz * i / 48000.0);
            writeFile(path, stereoToWav24(L, R, 48000));
        };
        // 440 Hz exactement 100 cycles par boucle (48000 / 440 n'est pas entier : boucle de 10909 éch. ~ 100 cycles)
        sineWav(dir + "/samples/a4.wav", 440.0, 48000);
        sineWav(dir + "/samples/a2.wav", 110.0, 48000);
        files::writeText(dir + "/Test Sine.sfz",
                         "// name: Test Sine\n// category: SYNTH\n<control> default_path=samples/\n"
                         "<region> sample=a2.wav lokey=0 hikey=56 pitch_keycenter=45 loop_mode=loop_continuous loop_start=4800 loop_end=9600\n"
                         "<region> sample=a4.wav lokey=57 hikey=127 pitch_keycenter=69 loop_mode=loop_continuous loop_start=4800 loop_end=48000\n");
        std::string err;
        auto in = loadSfzInstrument(dir + "/Test Sine.sfz", &err);
        CHECK(in && in->zones.size() == 2);
        if (in) {
            CHECK(in->find(40, 100) && in->find(40, 100)->root == 45);
            CHECK(in->find(81, 100) && in->find(81, 100)->root == 69);
            CHECK(in->name == "Test Sine" && in->category == "SYNTH");
        }
        Model m(Model::Mode::Offline);
        m.applyInit(nullptr);
        m.params.set(kFxWet, 0.f);
        m.params.set(kAPlay, 1.f);
        m.params.set(kBLvl, 0.f);
        m.params.set(kCutoff, 1.f);
        m.params.set(kM1, 0.f);
        m.params.set(kM2, 0.f);
        m.params.set(kM3, 0.f);
        m.params.set(kQty, 0.f);
        m.params.set(kSustain, 1.f);
        m.instruments.load(0, dir + "/Test Sine.sfz");
        CHECK(m.instruments.has(0));
        Engine e(m);
        e.prepare(48000, 256);
        std::vector<float> out;
        float l[256], r[256];
        for (int b = 0; b < 600; ++b) {  // 3,2 s : au-delà d'1 s, seule la boucle fait durer le son
            NoteEvent ev{0, 81, 0.9f, true};  // A5 : zone A4 jouée une octave plus haut -> 880 Hz
            e.process(l, r, 256, &ev, b == 0 ? 1 : 0);
            out.insert(out.end(), l, l + 256);
        }
        int zc = 0;
        const size_t a0 = out.size() - 48000, a1 = out.size();
        float pk = 0.f;
        for (size_t i = a0 + 1; i < a1; ++i) {
            if (out[i - 1] <= 0.f && out[i] > 0.f) ++zc;
            pk = std::max(pk, std::fabs(out[i]));
        }
        std::printf("instrument SFZ : %d zones, A5 joué à %d Hz (880 attendu), boucle tenue après 3 s : crête %.2f\n", in ? (int)in->zones.size() : 0, zc, pk);
        CHECK(std::abs(zc - 880) <= 3);
        CHECK(pk > 0.02f);
    }

    std::printf("rms min %.1f dBFS, crête max %.1f dBFS\n", 20 * std::log10(minRms), 20 * std::log10(maxPeak));
    std::printf(failures ? "%d échec(s)\n" : "tous les tests passent\n", failures);
    return failures ? 1 : 0;
}
