// Outil promo : fait jouer une partition au moteur RAW 2 (hors ligne) et écrit un WAV stéréo 24 bits.
// Chaque piste est un vrai preset RAW 2 (usine ou fichier .raw) joué par son propre moteur ;
// le mix est la somme des pistes, normalisée à -1 dBFS crête, avec un fondu final.
//   raw2_promo_audio <partition.txt> <sortie.wav>
// Partition (une commande par ligne, # = commentaire) :
//   bpm 120
//   length 20            durée totale en secondes
//   fade 1.5             fondu de fin en secondes
//   track <gain dB> factory <n>        nouvelle piste : preset d'usine n
//   track <gain dB> file <chemin.raw>  nouvelle piste : preset .raw (pack)
//   set <id du paramètre> <valeur>      règle un paramètre de la piste courante (valeur « plain »)
//   n <temps> <durée> <note> [vél]     note : temps et durée en temps (noires), note MIDI
//   c <temps> <durée> <n1,n2,...> [vél] accord
#include "core/Files.h"
#include "core/Json.h"
#include "core/Model.h"
#include "core/Params.h"
#include "core/Wav.h"
#include "dsp/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace raw2;

namespace {

struct Ev {
    double beat;
    int note;
    float vel;
    bool on;
};

struct Track {
    float gainDb = 0.f;
    std::string kind, src;
    std::vector<std::pair<std::string, float>> sets;
    std::vector<Ev> evs;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: raw2_promo_audio partition.txt sortie.wav\n");
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::fprintf(stderr, "partition introuvable : %s\n", argv[1]);
        return 1;
    }
    double bpm = 120.0, length = 10.0, fade = 1.0;
    std::vector<Track> tracks;
    std::string line;
    while (std::getline(in, line)) {
        if (const size_t h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream ss(line);
        std::string cmd;
        if (!(ss >> cmd)) continue;
        if (cmd == "bpm") ss >> bpm;
        else if (cmd == "length") ss >> length;
        else if (cmd == "fade") ss >> fade;
        else if (cmd == "track") {
            Track t;
            ss >> t.gainDb >> t.kind;
            std::getline(ss >> std::ws, t.src);
            tracks.push_back(t);
        } else if (cmd == "set" && !tracks.empty()) {
            std::string id;
            float v = 0.f;
            ss >> id >> v;
            tracks.back().sets.push_back({id, v});
        } else if ((cmd == "n" || cmd == "c") && !tracks.empty()) {
            double t = 0, d = 0;
            std::string notes;
            float vel = 0.8f;
            ss >> t >> d >> notes;
            if (!(ss >> vel)) vel = 0.8f;
            std::stringstream ns(notes);
            std::string tok;
            while (std::getline(ns, tok, ',')) {
                const int n = std::atoi(tok.c_str());
                tracks.back().evs.push_back({t, n, vel, true});
                tracks.back().evs.push_back({t + d, n, 0.f, false});
            }
        }
    }
    const int sr = 48000, block = 64;
    const size_t total = (size_t)(length * sr);
    std::vector<float> mixL(total, 0.f), mixR(total, 0.f);
    for (size_t ti = 0; ti < tracks.size(); ++ti) {
        Track& tr = tracks[ti];
        auto m = std::make_unique<Model>();
        m->hostBpm = (float)bpm;
        if (tr.kind == "factory") m->applyFactoryPreset(std::atoi(tr.src.c_str()), nullptr);
        else {
            std::string txt;
            json::Value v;
            m->instruments.scan();
            if (!files::readText(tr.src, txt) || !json::Value::parse(txt, v)) {
                std::fprintf(stderr, "preset illisible : %s\n", tr.src.c_str());
                return 1;
            }
            m->applyPresetJson(v, nullptr);
        }
        for (auto& [id, val] : tr.sets) {
            bool found = false;
            for (uint32_t p = 0; p < kNumParams; ++p)
                if (id == paramDef(p).key) {
                    m->params.set(p, val);
                    found = true;
                }
            if (!found) std::fprintf(stderr, "paramètre inconnu : %s\n", id.c_str());
        }
        Engine e(*m);
        e.prepare((double)sr, block);
        std::this_thread::sleep_for(std::chrono::milliseconds(900));  // tables et instruments chargés
        {  // préchauffage silencieux (lissages, chargement des zones)
            std::vector<float> L(block), R(block);
            for (int b = 0; b < sr / block; ++b) e.process(L.data(), R.data(), block, nullptr, 0);
        }
        std::sort(tr.evs.begin(), tr.evs.end(), [](const Ev& a, const Ev& b) {
            if (a.beat != b.beat) return a.beat < b.beat;
            return !a.on && b.on;  // les relâchements d'abord
        });
        const float g = std::pow(10.f, tr.gainDb / 20.f);
        std::vector<float> L(block), R(block);
        size_t ei = 0;
        float peak = 0.f;
        for (size_t pos = 0; pos < total; pos += block) {
            const int n = (int)std::min<size_t>(block, total - pos);
            std::vector<NoteEvent> evs;
            while (ei < tr.evs.size()) {
                const size_t at = (size_t)std::llround(tr.evs[ei].beat * 60.0 / bpm * sr);
                if (at >= pos + n) break;
                const Ev& x = tr.evs[ei++];
                evs.push_back(NoteEvent{(int)(std::max(at, pos) - pos), x.note, x.vel, x.on});
            }
            e.process(L.data(), R.data(), n, evs.data(), (int)evs.size());
            for (int i = 0; i < n; ++i) {
                mixL[pos + i] += L[i] * g;
                mixR[pos + i] += R[i] * g;
                peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
            }
        }
        std::printf("piste %zu (%s %s) : %zu notes, crête %.1f dB\n", ti + 1, tr.kind.c_str(), tr.src.c_str(),
                    tr.evs.size() / 2, 20.0 * std::log10(std::max(1e-9f, peak)));
    }
    float pk = 1e-9f;
    for (size_t i = 0; i < total; ++i) pk = std::max(pk, std::max(std::fabs(mixL[i]), std::fabs(mixR[i])));
    const float norm = std::pow(10.f, -1.f / 20.f) / pk;
    const size_t fadeN = (size_t)(fade * sr);
    for (size_t i = 0; i < total; ++i) {
        float f = norm;
        if (i + fadeN > total) f *= (float)(total - i) / (float)std::max<size_t>(1, fadeN);
        mixL[i] *= f;
        mixR[i] *= f;
    }
    std::printf("mix : crête %.1f dB avant normalisation\n", 20.0 * std::log10(pk));
    if (!writeFile(argv[2], stereoToWav24(mixL, mixR, sr))) {
        std::fprintf(stderr, "écriture impossible : %s\n", argv[2]);
        return 1;
    }
    return 0;
}
