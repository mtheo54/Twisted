// Fabrique le pack de presets « RAW 2 · INSTRUMENTS » : des sons construits sur la banque
// d'instruments réels (Documents › Raw 2 › Instruments), dans l'esprit French touch / filter house
// et hip-hop américain (boom bap, G-funk, soul, lo-fi, trap). Chaque preset part d'INIT.
//   raw2_inst_pack <dossier de sortie>   (écrit <sortie>/RAW 2 · INSTRUMENTS/*.raw + pack.json)
// Si la banque d'instruments est installée (Documents › Raw 2 › Instruments), chaque preset est
// joué (accord de trois notes, 2 s) et son MASTER (puis le niveau des OSC) est ajusté pour viser
// le volume médian des presets d'usine : RMS -20 dB, crêtes sous -3 dBFS.
#include "core/Files.h"
#include "core/Model.h"
#include "dsp/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace raw2;

namespace {

struct Recipe {
    const char* name;
    const char* category;  // basse, lead, pad, pluck, arp, fx
    const char* style;
    const char* instA;
    const char* instB;     // "" : pas de second instrument (OSC B coupé)
    std::function<void(ParamStore&)> set;
};

void fx(ParamStore& P, int f, float p0, float p1, float p2, float mix) {
    P.set(fxParam(f, kFxOn), 1.f);
    P.set(fxParam(f, kFxP0), p0);
    P.set(fxParam(f, kFxP1), p1);
    P.set(fxParam(f, kFxP2), p2);
    P.set(fxParam(f, kFxMix), mix);
}
// enveloppe de l'OSC A (et B, identique)
void env(ParamStore& P, float a, float d, float s, float r) {
    P.set(kAttack, a); P.set(kDecay, d); P.set(kSustain, s); P.set(kRelease, r);
    P.set(kEnvBAttack, a); P.set(kEnvBDecay, d); P.set(kEnvBSustain, s); P.set(kEnvBRelease, r);
}

const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> R = {
        // ---- French touch / filter house -----------------------------------------------------
        {"Filtre Maison", "pad", "french touch", "Wurlitzer EP200", "", [](ParamStore& P) {
             env(P, 0.02f, 0.6f, 0.7f, 0.35f);
             P.set(kChord, 5); P.set(kCutoff, 0.62f); P.set(kReso, 0.35f); P.set(kFiltSlope, 1);
             fx(P, kFxPump, 0.7f, 0.3f, 0.5f, 1.f); fx(P, kFxPhase, 0.3f, 0.5f, 0.5f, 0.35f); fx(P, kFxRev, 0.25f, 0.5f, 0.1f, 0.2f);
         }},
        {"Cordes Disco", "pad", "french touch", "OB String Machine", "String Machine", [](ParamStore& P) {
             env(P, 0.35f, 0.6f, 0.9f, 0.55f);
             P.set(kBLvl, 0.7f); P.set(kCutoff, 0.8f);
             fx(P, kFxChor, 0.35f, 0.5f, 0.6f, 0.45f); fx(P, kFxRev, 0.45f, 0.4f, 0.2f, 0.3f); fx(P, kFxPump, 0.45f, 0.3f, 0.6f, 1.f);
         }},
        {"Robot Funk", "lead", "french touch", "OB Funk Lead", "", [](ParamStore& P) {
             env(P, 0.01f, 0.5f, 0.8f, 0.25f);
             P.set(kPolyMode, 2); P.set(kGlide, 0.3f); P.set(kCutoff, 0.7f); P.set(kReso, 0.4f); P.set(kFiltDrive, 0.4f);
             fx(P, kFxDist, 0.35f, 0.6f, 0.5f, 0.5f); fx(P, kFxDly, 0.3f, 0.35f, 0.4f, 0.2f);
         }},
        {"Voix Robot", "pad", "french touch", "OB Choir Aah", "", [](ParamStore& P) {
             env(P, 0.25f, 0.6f, 0.85f, 0.5f);
             P.set(kCutoff, 0.6f); P.set(kReso, 0.55f); P.set(kFiltSlope, 1);
             fx(P, kFxFlang, 0.25f, 0.6f, 0.5f, 0.35f); fx(P, kFxChor, 0.3f, 0.5f, 0.5f, 0.4f); fx(P, kFxRev, 0.5f, 0.4f, 0.1f, 0.3f);
         }},
        {"Clavi Funky", "pluck", "funk", "Clavisynth", "", [](ParamStore& P) {
             env(P, 0.f, 0.35f, 0.3f, 0.2f);
             P.set(kCutoff, 0.68f); P.set(kReso, 0.3f); P.set(kVelCut, 0.6f);
             fx(P, kFxPhase, 0.45f, 0.6f, 0.4f, 0.4f); fx(P, kFxComp, 0.4f, 0.3f, 0.4f, 1.f);
         }},
        {"Cuivres Club", "pluck", "french touch", "OB Brass Stabs", "", [](ParamStore& P) {
             env(P, 0.f, 0.45f, 0.4f, 0.25f);
             P.set(kChord, 1); P.set(kCutoff, 0.75f);
             fx(P, kFxPump, 0.65f, 0.6f, 0.4f, 1.f); fx(P, kFxRev, 0.3f, 0.5f, 0.1f, 0.2f);
         }},
        {"Super Scie Club", "lead", "french touch", "OB Super Saw", "", [](ParamStore& P) {
             env(P, 0.02f, 0.6f, 0.85f, 0.4f);
             P.set(kUnison, 3); P.set(kUniDetune, 0.25f); P.set(kUniWidth, 0.8f); P.set(kCutoff, 0.78f);
             fx(P, kFxPump, 0.6f, 0.3f, 0.5f, 1.f); fx(P, kFxRev, 0.4f, 0.4f, 0.15f, 0.25f);
         }},
        {"Orgue Maison", "pad", "house", "OB Tonewheels", "", [](ParamStore& P) {
             env(P, 0.01f, 0.5f, 1.f, 0.2f);
             P.set(kCutoff, 0.85f);
             fx(P, kFxChor, 0.6f, 0.5f, 0.7f, 0.5f); fx(P, kFxDist, 0.25f, 0.5f, 0.5f, 0.3f); fx(P, kFxRev, 0.3f, 0.5f, 0.1f, 0.2f);
         }},
        // ---- hip-hop américain ----------------------------------------------------------------
        {"Wurli Boom Bap", "pluck", "boom bap", "Wurlitzer EP200", "", [](ParamStore& P) {
             env(P, 0.f, 0.65f, 0.5f, 0.35f);
             P.set(kChord, 9); P.set(kChordStrum, 0.25f); P.set(kCutoff, 0.7f);
             fx(P, kFxVintage, 0.45f, 0.35f, 0.35f, 1.f); fx(P, kFxComp, 0.45f, 0.35f, 0.4f, 1.f);
         }},
        {"Soul Échantillon", "pad", "soul", "CP80 Electric Grand", "", [](ParamStore& P) {
             env(P, 0.f, 0.7f, 0.6f, 0.45f);
             P.set(kChord, 4); P.set(kCutoff, 0.66f);
             fx(P, kFxVintage, 0.6f, 0.55f, 0.45f, 1.f); fx(P, kFxRev, 0.3f, 0.6f, 0.1f, 0.2f);
         }},
        {"Sifflet G-Funk", "lead", "g-funk", "G-Funk Whistle", "", [](ParamStore& P) {
             env(P, 0.03f, 0.5f, 0.9f, 0.3f);
             P.set(kPolyMode, 2); P.set(kGlide, 0.35f); P.set(kCutoff, 0.9f);
             fx(P, kFxDly, 0.35f, 0.35f, 0.5f, 0.25f); fx(P, kFxRev, 0.35f, 0.5f, 0.1f, 0.2f);
         }},
        {"Basse G-Funk", "basse", "g-funk", "G-Funk Bass", "", [](ParamStore& P) {
             env(P, 0.01f, 0.5f, 0.85f, 0.2f);
             P.set(kPolyMode, 2); P.set(kGlide, 0.25f); P.set(kCutoff, 0.62f); P.set(kFiltDrive, 0.3f);
             fx(P, kFxComp, 0.4f, 0.3f, 0.35f, 1.f);
         }},
        {"Basse Ouest", "basse", "west coast", "OB Bass", "", [](ParamStore& P) {
             env(P, 0.f, 0.5f, 0.8f, 0.2f);
             P.set(kPolyMode, 1); P.set(kGlide, 0.15f); P.set(kSubLvl, 0.35f); P.set(kSubOct, 1); P.set(kCutoff, 0.6f);
             fx(P, kFxDist, 0.3f, 0.5f, 0.5f, 0.35f);
         }},
        {"Piano Trap", "pad", "trap", "Grand Piano", "", [](ParamStore& P) {
             env(P, 0.f, 0.7f, 0.6f, 0.5f);
             P.set(kCutoff, 0.72f);
             fx(P, kFxRev, 0.55f, 0.65f, 0.15f, 0.35f); fx(P, kFxEq, 0.4f, 0.5f, 0.45f, 1.f);
         }},
        {"Vibes Jazz Hop", "pluck", "lo-fi", "Vibraphone", "", [](ParamStore& P) {
             env(P, 0.f, 0.7f, 0.6f, 0.5f);
             P.set(kChord, 5); P.set(kChordStrum, 0.4f);
             fx(P, kFxVintage, 0.4f, 0.45f, 0.25f, 1.f); fx(P, kFxRev, 0.4f, 0.55f, 0.1f, 0.25f);
         }},
        {"Sax Lo-Fi", "lead", "lo-fi", "Alto Sax", "", [](ParamStore& P) {
             env(P, 0.03f, 0.6f, 0.9f, 0.35f);
             P.set(kPolyMode, 2); P.set(kGlide, 0.15f); P.set(kCutoff, 0.8f);
             fx(P, kFxVintage, 0.55f, 0.4f, 0.3f, 1.f); fx(P, kFxRev, 0.35f, 0.5f, 0.1f, 0.25f);
         }},
        {"Orgue d'Église Soul", "pad", "soul", "OB Rotary Organ", "", [](ParamStore& P) {
             env(P, 0.02f, 0.5f, 1.f, 0.4f);
             P.set(kChord, 2); P.set(kCutoff, 0.8f);
             fx(P, kFxChor, 0.5f, 0.5f, 0.6f, 0.4f); fx(P, kFxRev, 0.55f, 0.5f, 0.2f, 0.3f);
         }},
        {"Hymne Cuivres", "pad", "trap", "OB Brass J60", "String Machine", [](ParamStore& P) {
             env(P, 0.15f, 0.6f, 0.9f, 0.5f);
             P.set(kBLvl, 0.65f); P.set(kCutoff, 0.75f);
             fx(P, kFxRev, 0.6f, 0.55f, 0.2f, 0.35f); fx(P, kFxComp, 0.35f, 0.3f, 0.3f, 1.f);
         }},
        {"Ballade FM", "pluck", "r&b", "FM Piano", "", [](ParamStore& P) {
             env(P, 0.f, 0.65f, 0.55f, 0.45f);
             P.set(kChord, 4); P.set(kChordStrum, 0.2f);
             fx(P, kFxChor, 0.3f, 0.5f, 0.45f, 0.4f); fx(P, kFxRev, 0.45f, 0.5f, 0.1f, 0.25f);
         }},
        {"Pianet Poussière", "pluck", "lo-fi", "Pianet T", "", [](ParamStore& P) {
             env(P, 0.f, 0.7f, 0.75f, 0.35f);
             P.set(kCutoff, 0.8f);
             fx(P, kFxVintage, 0.5f, 0.6f, 0.5f, 1.f); fx(P, kFxComp, 0.6f, 0.5f, 0.5f, 1.f); fx(P, kFxPump, 0.35f, 0.3f, 0.6f, 1.f);
         }},
        {"Chœur Drill", "pad", "drill", "OB Choir Aah", "Grand Piano", [](ParamStore& P) {
             env(P, 0.2f, 0.6f, 0.85f, 0.6f);
             P.set(kBLvl, 0.6f); P.set(kCutoff, 0.7f);
             fx(P, kFxRev, 0.7f, 0.6f, 0.2f, 0.4f); fx(P, kFxDly, 0.4f, 0.3f, 0.5f, 0.15f);
         }},
        {"Basse Slap Funk", "basse", "funk", "OB Slap Bass", "", [](ParamStore& P) {
             env(P, 0.f, 0.45f, 0.6f, 0.2f);
             P.set(kPolyMode, 1); P.set(kCutoff, 0.72f); P.set(kVelCut, 0.5f);
             fx(P, kFxComp, 0.5f, 0.4f, 0.4f, 1.f);
         }},
        {"Arpège Disco", "arp", "french touch", "OB Funk Lead", "", [](ParamStore& P) {
             env(P, 0.f, 0.4f, 0.5f, 0.3f);
             P.set(kArpOn, 1); P.set(kArpMode, 2); P.set(kArpRate, 3); P.set(kArpOct, 2); P.set(kArpGate, 0.5f); P.set(kChord, 5);
             fx(P, kFxDly, 0.3f, 0.4f, 0.4f, 0.25f); fx(P, kFxPump, 0.5f, 0.3f, 0.5f, 1.f);
         }},
        {"Nappe Chaude", "pad", "soul", "OB Pad Warmth", "", [](ParamStore& P) {
             env(P, 0.4f, 0.6f, 0.9f, 0.6f);
             P.set(kCutoff, 0.7f);
             fx(P, kFxVintage, 0.35f, 0.25f, 0.f, 1.f); fx(P, kFxRev, 0.6f, 0.55f, 0.2f, 0.35f);
         }},
    };
    return R;
}

struct Level { double rmsDb, peakDb; };

// accord tenu 1.5 s puis relâché 0.5 s (les basses : une note, une octave plus bas)
Level measure(Model& m, bool bass) {
    m.life.restart(1);
    Engine e(m);
    e.prepare(48000.0, 256);
    float l[256], r[256], pk = 0.f;
    double s = 0.0;
    long n = 0;
    const int chord[3] = {48, 55, 60}, low[1] = {36};
    const int* notes = bass ? low : chord;
    const int nn = bass ? 1 : 3;
    for (int b = 0; b < 375; ++b) {
        NoteEvent ev[3];
        int ne = 0;
        if (b == 0) for (int k = 0; k < nn; ++k) ev[ne++] = NoteEvent{0, notes[k], 0.8f, true};
        if (b == 281) for (int k = 0; k < nn; ++k) ev[ne++] = NoteEvent{0, notes[k], 0.f, false};
        e.process(l, r, 256, ev, ne);
        for (int i = 0; i < 256; ++i) {
            s += (double)l[i] * l[i] + (double)r[i] * r[i];
            pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
            ++n;
        }
    }
    return {10.0 * std::log10(s / (2.0 * n) + 1e-12), 20.0 * std::log10(pk + 1e-9)};
}

// MASTER d'abord (0 dB au maximum), puis le niveau des OSC s'il faut encore monter
void calibrate(Model& m, bool bass, const char* name) {
    ParamStore& P = m.params;
    Level lv{};
    for (int it = 0; it < 6; ++it) {
        lv = measure(m, bass);
        const double d = std::min(-20.0 - lv.rmsDb, -3.0 - lv.peakDb);
        if (std::fabs(d) < 0.4) break;
        float mv = P.get(kMaster) + (float)(d / 18.2);
        float over = 0.f;
        if (mv > 1.f) { over = mv - 1.f; mv = 1.f; }
        P.set(kMaster, std::clamp(mv, 0.05f, 1.f));
        if (over > 0.f) {
            P.set(kALvl, std::min(1.f, P.get(kALvl) + over));
            if (P.get(kBLvl) > 0.f) P.set(kBLvl, std::min(1.f, P.get(kBLvl) + over));
        }
    }
    lv = measure(m, bass);
    std::printf("  %-24s RMS %6.1f dB  cr\xC3\xAAte %5.1f dB  master %.2f\n", name, lv.rmsDb, lv.peakDb, P.get(kMaster));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : raw2_inst_pack <dossier de sortie>\n");
        return 1;
    }
    const std::string dir = files::join(argv[1], "RAW 2 \xC2\xB7 INSTRUMENTS");
    files::makeDirs(dir);
    int n = 0;
    for (const Recipe& r : recipes()) {
        Model m(Model::Mode::Offline);
        m.applyInit(nullptr);
        ParamStore& P = m.params;
        P.set(kAPlay, 1.f);
        P.set(kALvl, 0.85f);
        P.set(kBLvl, 0.f);
        P.set(kM1, 0.f);
        P.set(kM2, 10.f);
        P.set(kM3, 0.f);
        P.set(kCutoff, 0.8f);
        P.set(kReso, 0.1f);
        if (r.instB[0]) P.set(kBPlay, 1.f);
        r.set(P);
        m.instruments.scan();
        if (m.instruments.loadByName(0, r.instA) && (!r.instB[0] || m.instruments.loadByName(1, r.instB)))
            calibrate(m, std::string(r.category) == "basse", r.name);
        else
            std::printf("  %-24s (instrument absent : volume non calibr\xC3\xA9)\n", r.name);
        json::Value v = m.presetJson(false);
        v.set("name", r.name);
        v.set("category", r.category);
        v.set("style", r.style);
        json::Value a = json::Value::object();
        a.set("name", r.instA);
        v.set("instA", a);
        if (r.instB[0]) {
            json::Value b = json::Value::object();
            b.set("name", r.instB);
            v.set("instB", b);
        }
        char num[16];
        std::snprintf(num, sizeof num, "%02d ", ++n);
        files::writeText(files::join(dir, std::string(num) + r.name + ".raw"), v.dump(true));
    }
    json::Value meta = json::Value::object();
    meta.set("name", "RAW 2 \xC2\xB7 INSTRUMENTS");
    meta.set("author", "ABRASION");
    meta.set("version", "1.0");
    files::writeText(files::join(dir, "pack.json"), meta.dump(true));
    std::printf("%d presets \xC3\xA9" "crits dans %s\n", n, dir.c_str());
    return 0;
}
