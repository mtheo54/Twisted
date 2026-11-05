// Fuzzer maison (sans libFuzzer) : mutations de vrais fichiers, lus par les mêmes fonctions que
// le plugin. À compiler avec ASan + UBSan. Usage : fuzz <cible> <graine> <itérations> [HOME]
//   cibles : json state preset pack wav sfz
#include "core/Files.h"
#include "core/Instruments.h"
#include "core/Json.h"
#include "core/Model.h"
#include "core/Wav.h"
#include "dsp/Engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace raw2;

static std::mt19937 rng(1234);
static size_t rnd(size_t n) { return n ? rng() % n : 0; }

static const char* kTokens[] = {"{", "}", "[", "]", ",", ":", "\"", "null", "true", "false", "-1", "0", "1e308", "-1e308", "1e-320",
                                "nan", "NaN", "Infinity", "999999999999", "-2147483649", "\\u0000", "\\ud800", "\"\xff\xfe\"", "0.5",
                                "\"presets\":", "\"params\":", "\"base\":", "\"p\":", "\"oscA\":", "\"wt\":", "\"instA\":", "\"format\":\"raw2-pack\"",
                                "region", "<region>", "<group>", "sample=", "lokey=", "hikey=", "pitch_keycenter=", "loop_start=", "loop_end=",
                                "loop_mode=loop_continuous", "offset=", "tune=", "volume=", "#define $X 9999999999", "lovel=", "hivel=", "999999",
                                "-999999", "c#-1", "g9", "key=", "\n", "//", "\x01"};

static std::string mutate(std::string s) {
    const int n = 1 + (int)rnd(6);
    for (int k = 0; k < n; ++k) {
        switch (rnd(7)) {
            case 0: if (!s.empty()) s[rnd(s.size())] = (char)rnd(256); break;                       // octet au hasard
            case 1: if (!s.empty()) s.erase(rnd(s.size()), 1 + rnd(16)); break;                     // effacement
            case 2: s.insert(rnd(s.size() + 1), kTokens[rnd(sizeof kTokens / sizeof *kTokens)]); break;  // jeton
            case 3: if (!s.empty()) s.resize(rnd(s.size())); break;                                 // troncature
            case 4: {                                                                               // copie d'un morceau
                if (s.size() < 4) break;
                const size_t a = rnd(s.size()), l = 1 + rnd(std::min<size_t>(64, s.size() - a));
                s.insert(rnd(s.size()), s.substr(a, l));
                break;
            }
            case 5: {  // un nombre remplacé par une valeur extrême
                const size_t a = s.find_first_of("0123456789", rnd(s.size()));
                if (a == std::string::npos) break;
                size_t b = a;
                while (b < s.size() && (std::isdigit((unsigned char)s[b]) || s[b] == '.' || s[b] == 'e' || s[b] == '-')) ++b;
                s.replace(a, b - a, kTokens[10 + rnd(10)]);
                break;
            }
            default: if (s.size() > 2) std::swap(s[rnd(s.size())], s[rnd(s.size())]); break;
        }
    }
    return s;
}

static bool finiteRender(Model& m, int blocks) {
    Engine e(m);
    e.prepare(48000.0, 256);
    float L[256], R[256];
    for (int b = 0; b < blocks; ++b) {
        NoteEvent ev[2] = {{0, 48, 1.f, true}, {10, 72, 0.3f, true}};
        e.process(L, R, 256, ev, b == 0 ? 2 : 0);
        for (int i = 0; i < 256; ++i)
            if (!std::isfinite(L[i]) || !std::isfinite(R[i]) || std::fabs(L[i]) > 8.f || std::fabs(R[i]) > 8.f) {
                std::printf("  sortie anormale : %g %g\n", L[i], R[i]);
                return false;
            }
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string target = argv[1];
    std::string seed;
    if (!files::readText(argv[2], seed)) { std::printf("graine illisible\n"); return 2; }
    const long iters = std::atol(argv[3]);
    if (argc > 4) setenv("HOME", argv[4], 1);
    int bad = 0;
    for (long it = 0; it < iters; ++it) {
        const std::string in = it == 0 ? seed : mutate(seed);
        if (target == "json") {
            json::Value v;
            if (json::Value::parse(in, v)) { json::Value w; (void)json::Value::parse(v.dump(true), w); }
        } else if (target == "state") {
            Model m(Model::Mode::Offline);
            if (m.loadState(in, nullptr) && it % 20 == 0 && !finiteRender(m, 8)) { ++bad; files::writeText("bad_state.txt", in); }
        } else if (target == "preset") {
            json::Value v;
            if (!json::Value::parse(in, v)) continue;
            Model m(Model::Mode::Offline);
            m.instruments.scan();
            if (m.applyPresetJson(v, nullptr) && it % 10 == 0 && !finiteRender(m, 8)) { ++bad; files::writeText("bad_preset.txt", in); }
        } else if (target == "pack") {
            const std::string p = files::join(files::raw2Dir(), "fuzz.raw2pack");
            files::writeText(p, in);
            Model m(Model::Mode::Offline);
            m.scanPacks();
            const int pi = m.findPack(p);
            if (pi >= 0)
                for (int k = 0; k < (int)m.packs[(size_t)pi].presets.size() && k < 4; ++k)
                    if (m.applyPackPreset(pi, k, nullptr) && it % 10 == 0 && !finiteRender(m, 4)) { ++bad; files::writeText("bad_pack.txt", in); }
            files::removeFile(p);
        } else if (target == "wav") {
            std::vector<unsigned char> b(in.begin(), in.end());
            std::vector<float> out;
            int ch = 0;
            double sr = 0;
            (void)parseWavMulti(b, out, ch, sr, 1u << 22);
            std::vector<float> mono;
            (void)parseWav(b, mono, &sr);
        } else if (target == "sfz") {
            // le SFZ muté est écrit à côté du vrai (mêmes WAV), puis chargé et joué
            const std::string dir = argc > 5 ? argv[5] : std::string(".");
            const std::string p = files::join(dir, "zz_fuzz.sfz");
            files::writeText(p, in);
            std::string err;
            auto inst = loadSfzInstrument(p, &err);
            if (inst)
                for (int key : {0, 21, 60, 108, 127})
                    for (int vel : {1, 64, 127}) (void)inst->find(key, vel);
            files::removeFile(p);
        }
        if (it % 500 == 0) std::printf("%s : %ld\n", target.c_str(), it);
    }
    std::printf("%s : %ld itérations, %d sortie(s) anormale(s)\n", target.c_str(), iters, bad);
    return bad ? 1 : 0;
}
