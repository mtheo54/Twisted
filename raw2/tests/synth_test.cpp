// Tests de la synthèse v6 : filtre multimode, modes de warp, bruit, unison 16, ENV 3 / 4 et
// routes par note (courbes, source qui dose), MPE, tables de 256 frames, import / resynthèse.
#include "core/FactoryData.h"
#include "core/Model.h"
#include "core/Wav.h"
#include "dsp/Engine.h"
#include "dsp/Fft.h"
#include "dsp/WavetableOps.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace raw2;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("ÉCHEC %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

namespace {
constexpr double kSr = 48000.0;

struct Out {
    std::vector<float> L, R;
    bool finite = true;
    float peak = 0.f;
};

// note tenue `seconds`, puis relâche ; events : expression envoyée au bloc 4
Out play(Model& m, int note, float seconds, const std::vector<NoteEvent>& extra = {}, int notes = 1) {
    Engine e(m);
    e.prepare(kSr, 256);
    Out o;
    std::vector<float> L(256), R(256);
    const int blocks = (int)(seconds * kSr / 256);
    for (int b = 0; b < blocks; ++b) {
        std::vector<NoteEvent> ev;
        if (b == 0)
            for (int k = 0; k < notes; ++k) {
                NoteEvent n{k, note + k * 7, 0.9f, true};
                n.channel = (uint8_t)(1 + k);
                ev.push_back(n);
            }
        if (b == 4) ev.insert(ev.end(), extra.begin(), extra.end());
        e.process(L.data(), R.data(), 256, ev.data(), (int)ev.size());
        for (int i = 0; i < 256; ++i) {
            if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) o.finite = false;
            o.peak = std::max(o.peak, std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
        }
        o.L.insert(o.L.end(), L.begin(), L.end());
        o.R.insert(o.R.end(), R.begin(), R.end());
    }
    return o;
}

// énergie de la bande [f0, f1] (Hz) sur la dernière seconde, par FFT
double band(const std::vector<float>& x, double f0, double f1) {
    const size_t N = 32768;
    if (x.size() < N) return 0.0;
    std::vector<std::complex<double>> a(N);
    const size_t st = x.size() - N;
    for (size_t i = 0; i < N; ++i) a[i] = x[st + i] * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * (double)i / (double)N));
    fft(a, false);
    double e = 0.0;
    for (size_t k = (size_t)(f0 / kSr * N); k <= (size_t)(f1 / kSr * N) && k < N / 2; ++k) e += std::norm(a[k]);
    return e;
}

double rms(const std::vector<float>& x, size_t from = 0) {
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i) s += (double)x[i] * x[i];
    return std::sqrt(s / std::max<size_t>(1, x.size() - from));
}

void base(Model& m) {
    m.applyInit(nullptr);  // un sinus, sans effet
    m.params.set(kFxWet, 0.f);
    m.params.set(kM1, 0.f);
    m.params.set(kM2, 0.f);
    m.params.set(kM3, 0.f);
    m.params.set(kQty, 0.f);
}
} // namespace

int main() {
    // --- filtre multimode : chaque type, 12 et 24 dB, sortie finie et bornée -----------------
    {
        double lpLow = 0, lpHigh = 0, hpLow = 0, hpHigh = 0;
        for (int t = 0; t < kNumFiltTypes; ++t)
            for (int s24 = 0; s24 < 2; ++s24) {
                Model m(Model::Mode::Offline);
                base(m);
                m.params.set(kNoiseLvl, 0.8f);  // bruit blanc : spectre plat, on lit la forme du filtre
                m.params.set(kALvl, 0.f);
                m.params.set(kFiltType, (float)t);
                m.params.set(kFiltSlope, (float)s24);
                m.params.set(kCutoff, 0.5f);  // ~630 Hz
                m.params.set(kReso, 0.6f);
                m.params.set(kFiltMorph, 0.4f);
                m.params.set(kSustain, 1.f);
                const Out o = play(m, 60, 1.5f);
                CHECK(o.finite);
                CHECK(o.peak < 2.5f);
                CHECK(rms(o.L, o.L.size() / 2) > 1e-4);
                if (t == kFtLp && s24) { lpLow = band(o.L, 50, 300); lpHigh = band(o.L, 4000, 12000); }
                if (t == kFtHp && s24) { hpLow = band(o.L, 50, 300); hpHigh = band(o.L, 4000, 12000); }
            }
        std::printf("filtre : LP grave/aigu %.1f dB, HP grave/aigu %.1f dB\n", 10 * std::log10(lpLow / lpHigh), 10 * std::log10(hpLow / hpHigh));
        CHECK(lpLow > lpHigh * 100.0);  // passe-bas 24 dB : les aigus 20 dB plus bas au moins
        CHECK(hpHigh > hpLow * 100.0);
    }
    // --- COMB : crêtes aux multiples de la coupure ------------------------------------------
    {
        Model m(Model::Mode::Offline);
        base(m);
        m.params.set(kNoiseLvl, 0.8f);
        m.params.set(kALvl, 0.f);
        m.params.set(kFiltType, (float)kFtCombP);
        m.params.set(kCutoff, std::log(500.0f / 20.f) / std::log(1000.f));  // 500 Hz (+ suivi du clavier coupé)
        m.params.set(kFiltKey, 0.f);
        m.params.set(kReso, 0.9f);
        m.params.set(kSustain, 1.f);
        const Out o = play(m, 60, 1.5f);
        const double on = band(o.L, 980, 1020), off = band(o.L, 730, 770);
        std::printf("COMB + : pic 1 kHz / creux 750 Hz %.1f dB\n", 10 * std::log10(on / off));
        CHECK(o.finite && on > off * 4.0);
    }
    // --- modes de warp : finis, chacun change le son (sauf à 0 %) -----------------------------
    {
        Model m0(Model::Mode::Offline);
        base(m0);
        m0.params.set(kBLvl, 0.7f);
        const double ref = rms(play(m0, 48, 0.6f).L);
        for (int w = 0; w < kNumWarpModes; ++w) {
            Model m(Model::Mode::Offline);
            base(m);
            m.params.set(kBLvl, 0.7f);
            m.params.set(kBSt, 7.f);
            m.params.set(kAWarpMode, (float)w);
            m.params.set(kAWarp, 0.7f);
            const Out o = play(m, 48, 0.6f);
            CHECK(o.finite);
            CHECK(o.peak < 2.f);
            Model mz(Model::Mode::Offline);  // même réglage, warp à 0 : comme le son de départ
            base(mz);
            mz.params.set(kBLvl, 0.7f);
            mz.params.set(kAWarpMode, (float)w);
            mz.params.set(kAWarp, 0.f);
            CHECK(std::fabs(rms(play(mz, 48, 0.6f).L) - ref) < 1e-6 * std::max(1.0, ref * 1e3));
        }
    }
    // --- bruit : 4 types, couleur sombre / claire ----------------------------------------------
    {
        for (int t = 0; t < kNumNoiseTypes; ++t) {
            Model m(Model::Mode::Offline);
            base(m);
            m.params.set(kALvl, 0.f);
            m.params.set(kNoiseLvl, 1.f);
            m.params.set(kNoiseType, (float)t);
            m.params.set(kCutoff, 1.f);
            const Out o = play(m, 60, 0.8f);
            CHECK(o.finite && o.peak < 2.f && rms(o.L) > 1e-4);
        }
        double dark = 0, bright = 0;
        for (float c : {0.1f, 0.9f}) {
            Model m(Model::Mode::Offline);
            base(m);
            m.params.set(kALvl, 0.f);
            m.params.set(kNoiseLvl, 1.f);
            m.params.set(kNoiseColor, c);
            m.params.set(kCutoff, 1.f);
            m.params.set(kSustain, 1.f);
            const Out o = play(m, 60, 1.5f);
            const double r = band(o.L, 6000, 15000) / std::max(1e-12, band(o.L, 50, 400));
            (c < 0.5f ? dark : bright) = r;
        }
        std::printf("bruit : rapport aigu/grave sombre %.2e, clair %.2e\n", dark, bright);
        CHECK(bright > dark * 100.0);
    }
    // --- unison 16 : 16 voix pour une note, niveau stable avec BLEND ---------------------------
    {
        double r1 = 0, r16 = 0, r16b = 0;
        for (int k = 0; k < 3; ++k) {
            Model m(Model::Mode::Offline);
            base(m);
            m.params.set(kUnison, k == 0 ? 1.f : 16.f);
            m.params.set(kUniDetune, 0.5f);
            m.params.set(kUniBlend, k == 2 ? 0.2f : 1.f);
            m.params.set(kSustain, 1.f);
            const double r = rms(play(m, 57, 1.f).L, 12000);
            (k == 0 ? r1 : (k == 1 ? r16 : r16b)) = r;
        }
        std::printf("unison : rms 1 voix %.4f, 16 voix %.4f, 16 voix blend 20 %% %.4f\n", r1, r16, r16b);
        CHECK(r16 > r1 * 0.5 && r16 < r1 * 2.0);
        CHECK(r16b > r1 * 0.5 && r16b < r1 * 2.0);
    }
    // --- ENV 3 -> cutoff (route par note) avec courbe et source qui dose ----------------------
    {
        double rEnv = 0, rNone = 0;
        for (int k = 0; k < 2; ++k) {
            Model m(Model::Mode::Offline);
            base(m);
            m.params.set(kAPos, 0.f);
            m.applyFactoryPreset(0, nullptr);
            m.params.set(kFxWet, 0.f);
            m.params.set(kCutoff, 0.15f);
            m.params.set(kEnv3Attack, 0.f);
            m.params.set(kEnv3Decay, 0.9f);
            m.params.set(kEnv3Sustain, 1.f);
            if (k == 0) {
                const int i = m.routes.add(kRouteSrcEnv3, kCutoff, 90);
                CHECK(i >= 0);
                m.routes.setShape(i, 50, kRouteSrcVel);
            }
            const double r = band(play(m, 60, 1.5f).L, 3000, 12000);
            (k == 0 ? rEnv : rNone) = r;
        }
        std::printf("ENV 3 \xE2\x86\x92 cutoff : aigus %.1f dB plus forts\n", 10 * std::log10(rEnv / std::max(1e-12, rNone)));
        CHECK(rEnv > rNone * 10.0);
        // la route est sauvée avec sa courbe et sa source auxiliaire
        Model m(Model::Mode::Offline);
        m.routes.clear();
        const int i = m.routes.add(kRouteSrcPress, kAPos, -40);
        m.routes.setShape(i, -70, kRouteSrcEnv4);
        FreeRoutes r2;
        r2.load(m.routes.save());
        CHECK(r2.count() == 1 && r2.r[0].curve.load() == -70 && r2.r[0].aux.load() == kRouteSrcEnv4 && r2.r[0].amt.load() == -40);
        CHECK(std::fabs(routeCurve(0.5f, 100) - std::pow(0.5f, 0.25f)) < 1e-5f && std::fabs(routeCurve(-0.5f, -100) + std::pow(0.5f, 4.f)) < 1e-5f);
    }
    // --- MPE : bend de canal -> hauteur de la note de ce canal seulement ----------------------
    {
        Model m(Model::Mode::Offline);
        base(m);
        m.params.set(kMpe, 1.f);
        m.params.set(kSustain, 1.f);
        NoteEvent bend;
        bend.kind = kEvChanBend;
        bend.channel = 1;
        bend.value = 12.f / 48.f;  // +12 demi-tons avec l'amplitude MPE de 48
        const Out o = play(m, 57, 1.5f, {bend});
        // A3 (220 Hz) monté d'une octave : l'énergie passe à 440 Hz
        const double at440 = band(o.L, 430, 450), at220 = band(o.L, 212, 228);
        std::printf("MPE : 440 Hz / 220 Hz %.1f dB\n", 10 * std::log10(at440 / std::max(1e-12, at220)));
        CHECK(at440 > at220 * 100.0);
        // pression par note : finie
        NoteEvent pr;
        pr.kind = kEvChanPress;
        pr.channel = 1;
        pr.value = 0.8f;
        Model m2(Model::Mode::Offline);
        base(m2);
        m2.params.set(kMpe, 1.f);
        m2.routes.add(kRouteSrcPress, kCutoff, 60);
        CHECK(play(m2, 60, 0.5f, {pr}, 3).finite);
    }
    // --- tables de 256 frames : lecture, sauvegarde, export / import Serum ---------------------
    {
        Model m(Model::Mode::Offline);
        Wavetable t;
        std::vector<float> raw((size_t)256 * kHiLen);
        for (int f = 0; f < 256; ++f)
            for (int i = 0; i < kHiLen; ++i) {
                const double u = (double)i / kHiLen;
                double s = 0.0;
                for (int h = 1; h <= 1 + f; h += 2) s += std::sin(2.0 * 3.14159265358979 * h * u) / h;  // carré de plus en plus riche
                raw[(size_t)f * kHiLen + (size_t)i] = (float)s;
            }
        CHECK(tableFromAudioEx(raw, 44100.0, "carre", kImpAuto, 64, t) == kImpTable);
        CHECK(t.frames() == 256 && t.hiFrame(255));
        {
            std::lock_guard<std::mutex> lk(m.tableMutex);
            m.edit[0] = t;
        }
        m.setMounted(0, true);
        m.tablesChanged(0);
        const MipTable* mt = m.audioTable(0);
        CHECK(mt && mt->frames() == 256);
        // la frame 255 garde ses harmoniques au-delà de 127 (version 2048 points)
        {
            std::vector<std::complex<double>> a(kMipLen);
            const float* fr = mt->frame(0, 255);
            for (int i = 0; i < kMipLen; ++i) a[(size_t)i] = fr[i];
            fft(a, false);
            CHECK(std::abs(a[201]) > std::abs(a[1]) / 201.0 * 0.5);  // harmonique 201 présente
        }
        // état : la table revient entière (256 frames, version haute résolution)
        const std::string st = m.saveState();
        Model m2(Model::Mode::Offline);
        CHECK(m2.loadState(st, nullptr));
        CHECK(m2.edit[0].frames() == 256 && m2.edit[0].hiFrame(100));
        float err = 0.f;
        for (int i = 0; i < kHiLen; i += 7) err = std::max(err, std::fabs((*m2.edit[0].hi)[(size_t)100 * kHiLen + (size_t)i] - (*t.hi)[(size_t)100 * kHiLen + (size_t)i]));
        CHECK(err < 1e-3f);
        // export WAV puis import : mêmes frames
        const std::vector<unsigned char> wav = wavetableToWav(t);
        std::vector<float> back;
        double sr = 0;
        CHECK(parseWav(wav, back, &sr) && back.size() == raw.size());
        Wavetable t2;
        CHECK(tableFromAudioEx(back, sr, "carre2", kImpAuto, 64, t2) == kImpTable && t2.frames() == 256);
        // nombre de frames modifiable, frames interpolées
        resizeTable(t2, 32);
        CHECK(t2.frames() == 32 && t2.hiFrame(31));
        // le moteur joue une table de 256 frames, position balayée
        m.params.set(kALvl, 0.9f);
        m.routes.add(0, kAPos, 100);
        CHECK(play(m, 48, 0.8f).finite);
    }
    // --- son quelconque : hauteur détectée, cycles alignés, resynthèse -------------------------
    {
        const double sr = 44100.0, f0 = 196.0;  // G3, avec glissement de timbre
        std::vector<float> x((size_t)(sr * 2.0));
        for (size_t i = 0; i < x.size(); ++i) {
            const double t = (double)i / sr, br = t / 2.0;
            double s = 0.0;
            for (int h = 1; h <= 30; ++h) s += std::sin(2.0 * 3.14159265358979 * f0 * h * t) * std::pow((double)h, -(2.0 - 1.5 * br));
            x[i] = (float)(0.6 * s);
        }
        const double P = detectPeriod(x, sr, 20000);
        std::printf("hauteur : période %.2f (attendue %.2f)\n", P, sr / f0);
        CHECK(std::fabs(P - sr / f0) < 0.5);
        for (int mode : {kImpCycles, kImpSpectral}) {
            Wavetable t;
            CHECK(tableFromAudioEx(x, sr, "son", mode, 128, t) == mode);
            CHECK(t.frames() == 128 && t.hiFrame(0) && t.hiFrame(127));
            // premier cycle : sombre ; dernier : brillant (le timbre suit le son)
            auto bright = [&](int f) {
                std::vector<std::complex<double>> a(kHiLen);
                for (int i = 0; i < kHiLen; ++i) a[(size_t)i] = (*t.hi)[(size_t)f * kHiLen + (size_t)i];
                fft(a, false);
                double hi = 0, lo = 0;
                for (int h = 1; h <= 3; ++h) lo += std::norm(a[(size_t)h]);
                for (int h = 10; h <= 30; ++h) hi += std::norm(a[(size_t)h]);
                return hi / lo;
            };
            CHECK(bright(127) > bright(0) * 3.0);
            // une frame = un cycle : l'énergie est sur les harmoniques entières
            std::vector<std::complex<double>> a(kHiLen);
            for (int i = 0; i < kHiLen; ++i) a[(size_t)i] = (*t.hi)[(size_t)64 * kHiLen + (size_t)i];
            fft(a, false);
            CHECK(std::norm(a[1]) > 50.0 * std::norm(a[2]) / 4.0 || std::norm(a[1]) > std::norm(a[3]));
        }
    }
    std::printf(failures ? "%d échec(s)\n" : "synthèse v6 : tout passe\n", failures);
    return failures ? 1 : 0;
}
