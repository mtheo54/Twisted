#include "core/FactoryFx.h"
#include "core/FactoryData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace raw2 {

namespace {

// Une ligne par preset (même ordre que presets.json). Chaque effet : « clé  réglage1 réglage2
// réglage3  m<MIX>  [@source±profondeur] », séparés par « ; ». L'ordre d'écriture est l'ordre du
// rack (les effets non cités suivent, coupés). Options : dist:tape|tube|fold|hard, os:1|2|4,
// eqf:<bas>,<méd>,<aigu>, wet:<0..1>. Sources de modulation : vie, lfo1, lfo2, env, kb, rnd.
// Repères (0..1 -> valeur réelle) :
//   hyper RATE 0,3..6,3 Hz · DETUNE · UNISON 2..7 voix      dist DRIVE · TONE · BIAS
//   flang RATE 0,4..2,4 Hz · DEPTH · FEEDBK                  phase RATE 0,3..2,3 Hz · DEPTH · FREQ
//   chor RATE 0,2..2,2 Hz · DELAY · DEPTH                    dly TIME 20..1000 ms · FEEDBK · FILTER
//   rev SIZE 0,3..7,3 s · DAMP · PRÉ 0..100 ms               comp THRESH 0..-40 dB · RATIO 1..20 · GAIN 0..12 dB
//   eq BAS · MÉD · AIGU (0,5 = 0 dB, ±12 dB)                 filt CUTOFF 20 Hz..20 kHz (log) · RESO · DRIVE
const char* const kRecipes[kNumFactoryPresets] = {
    /* 01 verre givré      lead  */ "chor .25 .50 .45 m.35 ; dly .36 .40 .60 m.25 ; rev .55 .40 .25 m.30",
    /* 02 neige lente      pad   */ "chor .15 .65 .60 m.45 ; rev .85 .55 .40 m.55",
    /* 03 sous la glace    basse */ "eq .62 .42 .48 ; comp .45 .35 .30 m1 ; rev .25 .70 .05 m.12 ; eqf:.12,.45,.85",
    /* 04 brume perso      pad   */ "phase .15 .60 .40 m.40 ; hyper .20 .50 .70 m.35 ; rev .75 .65 .25 m.50 ; filt .62 .25 .20 m.50 @lfo2+35",
    /* 05 sub velours      basse */ "filt .40 .20 .30 m1 ; comp .55 .30 .35 m.80 ; chor .15 .30 .25 m.15",
    /* 06 808 fumée        basse */ "dist .45 .40 .50 m.55 ; comp .50 .45 .40 m.90 ; eq .64 .40 .55 ; dist:tape os:2",
    /* 07 plug lourd       basse */ "dist .30 .55 .45 m.35 ; chor .25 .40 .35 m.25 ; comp .40 .30 .25 m.80 ; rev .20 .60 .10 m.10",
    /* 08 rage saw         basse */ "dist .80 .65 .55 m.80 ; filt .70 .45 .40 m1 @env+45 ; comp .55 .55 .45 m1 ; hyper .35 .30 .20 m.25 ; dist:hard os:4",
    /* 09 béton liquide    basse */ "flang .25 .40 .45 m.30 ; dist .40 .35 .50 m.40 ; comp .50 .40 .30 m.85",
    /* 10 néon grave       basse */ "dist .65 .70 .40 m.60 ; phase .55 .50 .45 m.30 ; comp .45 .50 .62 m.90 ; eq .58 .45 .62 ; dist:fold",
    /* 11 cœur de sub      basse */ "comp .40 .30 .30 m.70 ; eq .60 .46 .52 ; chor .20 .35 .20 m.12",
    /* 12 glitch bass      basse */ "dist .70 .60 .35 m.60 ; filt .58 .45 .45 m1 @rnd+40 ; flang .65 .50 .55 m.25 ; comp .50 .50 .58 m1 ; dist:fold os:4",
    /* 13 phonk nuit       basse */ "dist .55 .30 .60 m.60 ; flang .20 .45 .40 m.25 ; rev .30 .55 .15 m.15 ; comp .45 .45 .35 m.85 ; dist:tape",
    /* 14 tram 04h         basse */ "filt .50 .55 .30 m1 @lfo2+40 ; phase .20 .60 .35 m.35 ; rev .55 .65 .30 m.25 ; comp .40 .35 .25 m.70",
    /* 15 mur du son       basse */ "dist .95 .55 .65 m.90 ; hyper .40 .55 .60 m.40 ; eq .62 .38 .60 ; comp .65 .65 .75 m1 ; dist:hard os:4",
    /* 16 boue douce       basse */ "filt .42 .30 .55 m1 ; dist .35 .25 .45 m.35 ; chor .45 .55 .50 m.35 ; rev .30 .80 .10 m.18 ; dist:tape",
    /* 17 acid peluche     basse */ "filt .40 .80 .60 m1 @env+70 ; dist .50 .55 .40 m.50 ; dly .33 .45 .55 m.20 ; comp .45 .40 .30 m.80",
    /* 18 bas verglas      basse */ "chor .40 .55 .45 m.35 ; rev .50 .50 .25 m.30 ; eq .56 .44 .58 ; comp .35 .30 .25 m.60",
    /* 19 gros bébé        basse */ "dist .35 .50 .50 m.30 ; comp .55 .45 .40 m.90 ; chor .25 .30 .25 m.15",
    /* 20 hyper glace      lead  */ "hyper .55 .70 .85 m.60 ; dist .35 .70 .45 m.25 ; dly .17 .35 .65 m.20 ; rev .40 .35 .10 m.18 ; dist:hard",
    /* 21 pop bubblegum    lead  */ "chor .45 .40 .55 m.45 ; comp .40 .40 .35 m.60 ; dly .24 .30 .70 m.20",
    /* 22 sirène rose      lead  */ "phase .65 .70 .55 m.45 @lfo1+30 ; dist .45 .65 .40 m.30 ; rev .45 .30 .15 m.22 ; dist:fold",
    /* 23 cristal 2000     lead  */ "flang .15 .45 .55 m.30 ; dly .32 .45 .75 m.30 ; rev .60 .25 .30 m.30",
    /* 24 laser tendre     lead  */ "dly .49 .50 .50 m.32 ; rev .50 .45 .20 m.25 ; filt .80 .30 .20 m.60 @lfo1+25",
    /* 25 y2k diva         lead  */ "hyper .30 .45 .55 m.40 ; chor .55 .35 .60 m.30 ; eq .48 .55 .66 ; rev .35 .30 .10 m.20",
    /* 26 supersaw cœur    lead  */ "hyper .40 .80 1 m.70 ; comp .35 .45 .30 m.50 ; rev .50 .40 .15 m.25 ; eq .50 .46 .62",
    /* 27 voix de verre    lead  */ "phase .25 .40 .60 m.30 ; dly .36 .30 .65 m.22 ; rev .45 .50 .20 m.25",
    /* 28 plug flûte       lead  */ "chor .20 .55 .30 m.30 ; dly .74 .50 .45 m.30 ; rev .65 .55 .35 m.32 ; eq .44 .54 .60",
    /* 29 étincelle        lead  */ "dist .55 .80 .35 m.40 ; flang .70 .55 .60 m.35 ; dly .11 .40 .70 m.22 ; rev .30 .25 .05 m.15 ; dist:hard os:4",
    /* 30 néon chanté      lead  */ "chor .35 .45 .40 m.35 ; phase .30 .35 .50 m.20 ; dly .32 .35 .60 m.20 ; rev .40 .45 .20 m.20",
    /* 31 pixel love       lead  */ "dist .25 .75 .55 m.25 ; dly .12 .35 .80 m.25 ; eq .45 .50 .60 ; dist:hard os:1",
    /* 32 candy square     lead  */ "hyper .65 .40 .45 m.40 ; filt .70 .55 .35 m.70 @lfo1+40 ; dly .24 .40 .70 m.20 ; rev .35 .35 .10 m.18",
    /* 33 lumière froide   lead  */ "rev .80 .30 .40 m.45 ; chor .30 .60 .40 m.35 ; dly .49 .55 .40 m.28 ; hyper .15 .35 .60 m.30 ; eq .46 .48 .62",
    /* 34 tiktok bell      lead  */ "comp .35 .40 .30 m.50 ; eq .46 .52 .64 ; dly .24 .25 .75 m.22 ; rev .35 .40 .15 m.20",
    /* 35 spirale          lead  */ "phase .80 .80 .40 m.50 @rnd+50 ; dist .40 .60 .30 m.35 ; dly .17 .55 .55 m.28 ; flang .55 .60 .65 m.30 ; dist:fold os:2",
    /* 36 diamant digital  lead  */ "flang .30 .35 .40 m.30 ; hyper .35 .55 .70 m.45 ; rev .55 .30 .20 m.25 ; eq .46 .50 .64",
    /* 37 aurore tiède     pad   */ "chor .25 .60 .55 m.40 ; flang .08 .40 .35 m.20 ; dly .62 .55 .35 m.25 ; rev .80 .40 .35 m.50",
    /* 38 coton nuit       pad   */ "filt .50 .20 .45 m.90 ; phase .12 .45 .40 m.25 ; chor .50 .60 .55 m.45 ; rev .55 .75 .20 m.35",
    /* 39 velours bleu     pad   */ "hyper .25 .45 .75 m.45 ; phase .20 .45 .35 m.30 ; rev .70 .50 .30 m.45",
    /* 40 cloud nine       pad   */ "hyper .30 .60 .90 m.50 ; chor .20 .50 .50 m.30 ; dly .74 .60 .40 m.25 ; rev .90 .45 .50 m.50",
    /* 41 chambre d'écho   pad   */ "dly .49 .75 .30 m.45 ; filt .55 .55 .30 m.60 @lfo2+50 ; rev .65 .60 .20 m.35 ; phase .25 .55 .30 m.30",
    /* 42 sirop de lune    pad   */ "flang .10 .55 .50 m.30 ; chor .20 .55 .50 m.35 ; rev .75 .45 .35 m.45",
    /* 43 satin            pad   */ "chor .30 .50 .45 m.40 ; comp .30 .30 .25 m.50 ; rev .55 .45 .25 m.35",
    /* 44 mer calme        pad   */ "phase .08 .70 .45 m.40 ; rev .95 .35 .45 m.55 ; eq .50 .44 .58",
    /* 45 dream 3am        pad   */ "hyper .45 .75 .90 m.55 ; dist .20 .55 .45 m.20 ; phase .30 .55 .45 m.28 ; rev .70 .35 .30 m.45",
    /* 46 pastel           pad   */ "chor .40 .45 .50 m.50 ; filt .68 .30 .20 m.60 @lfo1+20 ; rev .60 .50 .25 m.35",
    /* 47 halo             pad   */ "rev .90 .25 .45 m.60 ; hyper .15 .55 .80 m.40 ; eq .44 .48 .62",
    /* 48 soupir           pad   */ "dist .20 .15 .55 m.20 ; filt .47 .30 .40 m1 ; phase .12 .50 .35 m.35 ; rev .65 .80 .25 m.40 ; dist:tape",
    /* 49 goutte           pluck */ "dly .24 .40 .70 m.30 ; rev .40 .45 .15 m.25 ; comp .35 .35 .30 m.50",
    /* 50 marimba glacé    pluck */ "eq .48 .54 .62 ; chor .35 .35 .35 m.25 ; rev .45 .30 .20 m.30",
    /* 51 kalimba rose     pluck */ "phase .35 .40 .55 m.25 ; dly .36 .30 .55 m.25 ; comp .40 .40 .30 m.50",
    /* 52 cloche mini      pluck */ "flang .20 .35 .45 m.25 ; dly .17 .45 .80 m.30 ; rev .55 .30 .25 m.30 ; eq .44 .50 .64",
    /* 53 verre cassé doux pluck */ "dist .30 .70 .35 m.30 ; dly .07 .55 .65 m.30 @rnd+40 ; filt .70 .50 .30 m.60 @env+30 ; rev .30 .40 .05 m.18 ; dist:fold os:2",
    /* 54 pluck cœur       pluck */ "chor .25 .45 .40 m.30 ; dly .49 .45 .45 m.28 ; rev .60 .45 .30 m.32 ; comp .40 .35 .30 m.50",
    /* 55 boîte à musique  pluck */ "eq .40 .50 .64 ; flang .08 .30 .30 m.20 ; rev .70 .35 .35 m.40",
    /* 56 tic toc          pluck */ "dist .20 .80 .50 m.20 ; dly .11 .30 .85 m.25 ; comp .40 .45 .35 m.60 ; dist:hard os:1",
    /* 57 escalier         arp   */ "dly .36 .45 .65 m.30 ; eq .46 .52 .60 ; rev .40 .40 .15 m.20",
    /* 58 manège           arp   */ "hyper .70 .50 .55 m.40 ; dist .40 .65 .40 m.30 ; dly .24 .50 .70 m.28 ; comp .40 .45 .35 m.50 ; dist:hard os:2",
    /* 59 dentelle         arp   */ "phase .25 .55 .55 m.30 ; hyper .20 .40 .55 m.30 ; dly .62 .50 .50 m.30 ; rev .70 .40 .35 m.38",
    /* 60 pulse pop        arp   */ "filt .55 .45 .30 m.70 @lfo1+45 ; comp .35 .40 .30 m.50 ; dly .32 .35 .60 m.22",
    /* 61 ricochet         arp   */ "flang .60 .70 .70 m.40 @rnd+40 ; dly .11 .65 .60 m.35 ; dist .30 .55 .40 m.25 ; comp .45 .45 .35 m.50 ; dist:fold",
    /* 62 lucioles         arp   */ "chor .20 .55 .55 m.35 ; dly .74 .55 .40 m.35 ; rev .85 .40 .45 m.50 ; filt .72 .25 .20 m.40 @lfo2+30",
    /* 63 texture neige    fx    */ "filt .65 .40 .20 m.60 @lfo2+40 ; hyper .10 .60 .80 m.40 ; rev .95 .45 .55 m.60",
    /* 64 grésil           fx    */ "dist .75 .75 .30 m.60 ; flang .85 .75 .80 m.45 @rnd+50 ; filt .60 .60 .50 m.70 @rnd+60 ; dly .05 .60 .70 m.30 ; dist:fold os:4",
    /* 65 radio morte      fx    */ "eq .30 .66 .30 ; dist .45 .25 .60 m.50 ; filt .55 .55 .60 m1 @lfo1+30 ; rev .35 .70 .10 m.25 ; dist:tape",
    /* 66 souffle          fx    */ "phase .10 .70 .50 m.50 ; filt .45 .35 .20 m.70 @lfo2+55 ; rev .90 .50 .40 m.55",
    /* 67 tempête rose     fx    */ "dist .85 .70 .50 m.60 ; hyper .60 .80 1 m.50 ; phase .70 .70 .60 m.40 ; rev .60 .35 .20 m.35 ; comp .55 .50 .40 m.70 ; dist:hard os:4",
    /* 68 eau de verre     fx    */ "flang .12 .65 .55 m.45 ; dly .45 .60 .55 m.35 ; rev .80 .30 .40 m.50 ; filt .75 .45 .25 m.40 @lfo1+35",
    /* 69 static kiss      fx    */ "filt .70 .70 .55 m.70 @rnd+70 ; dist .55 .60 .45 m.45 ; dly .09 .70 .60 m.30 ; phase .90 .60 .50 m.35 ; dist:fold os:2",
    /* 70 portail          fx    */ "flang .30 .80 .60 m.50 @lfo1+40 ; phase .20 .70 .70 m.40 ; dly .55 .50 .45 m.35 ; rev .85 .30 .55 m.50",
    /* 71 réverse          fx    */ "rev .70 .40 .90 m.60 ; flang .15 .50 .40 m.30 ; filt .60 .40 .25 m.60 @env-50",
    /* 72 poussière        fx    */ "dist .30 .20 .65 m.35 ; filt .50 .30 .40 m1 @lfo2+25 ; rev .55 .75 .20 m.30 ; comp .50 .40 .35 m.50 ; dist:tape",
};

int fxIndex(const std::string& k) {
    for (int f = 0; f < kNumFx; ++f)
        if (k == fxKey(f)) return f;
    return -1;
}

int modIndex(const std::string& k) {
    static const char* n[6] = {"vie", "lfo1", "lfo2", "env", "kb", "rnd"};
    for (int s = 0; s < 6; ++s)
        if (k == n[s]) return s;
    return -1;
}

bool num(const std::string& t, float& v) {
    char* end = nullptr;
    v = std::strtof(t.c_str(), &end);
    return end && *end == 0 && !t.empty();
}

} // namespace

const char* factoryFxRecipe(int preset) { return preset >= 0 && preset < kNumFactoryPresets ? kRecipes[preset] : ""; }

bool factoryFxSetup(int preset, FactoryFxSetup& out, std::string* err) {
    out = FactoryFxSetup{};
    auto fail = [&](const std::string& m) {
        if (err) *err = std::string("preset ") + std::to_string(preset + 1) + " : " + m;
        return false;
    };
    if (preset < 0 || preset >= kNumFactoryPresets) return fail("hors limites");
    for (int f = 0; f < kNumFx; ++f) {
        for (int j = 0; j < 3; ++j) out.p[f][j] = 0.5f;
        out.mix[f] = 0.3f;
    }
    out.modDepth[kFxDist] = 25.f;  // comme la maquette : la VIE pousse un peu la distorsion
    bool listed[kNumFx] = {};
    int nOrder = 0;
    // découpe en segments séparés par « ; », puis en mots
    std::string all = kRecipes[preset];
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t semi = all.find(';', pos);
        if (semi == std::string::npos) semi = all.size();
        std::string seg = all.substr(pos, semi - pos);
        pos = semi + 1;
        std::vector<std::string> w;
        {
            size_t a = 0;
            while (a < seg.size()) {
                while (a < seg.size() && seg[a] == ' ') ++a;
                size_t b = a;
                while (b < seg.size() && seg[b] != ' ') ++b;
                if (b > a) w.push_back(seg.substr(a, b - a));
                a = b;
            }
        }
        for (size_t k = 0; k < w.size();) {
            const std::string& t = w[k];
            if (t.rfind("dist:", 0) == 0) {
                static const char* m[4] = {"tape", "tube", "fold", "hard"};
                int found = -1;
                for (int q = 0; q < 4; ++q)
                    if (t.substr(5) == m[q]) found = q;
                if (found < 0) return fail("forme de distorsion inconnue " + t);
                out.distMode = found;
                ++k;
            } else if (t.rfind("os:", 0) == 0) {
                const std::string v = t.substr(3);
                if (v == "1") out.oversample = 0;
                else if (v == "2") out.oversample = 1;
                else if (v == "4") out.oversample = 2;
                else return fail("suréchantillonnage inconnu " + t);
                ++k;
            } else if (t.rfind("eqf:", 0) == 0) {
                if (std::sscanf(t.c_str() + 4, "%f,%f,%f", &out.eqFreq[0], &out.eqFreq[1], &out.eqFreq[2]) != 3) return fail("eqf mal écrit");
                ++k;
            } else if (t.rfind("wet:", 0) == 0) {
                if (!num(t.substr(4), out.wet)) return fail("wet mal écrit");
                ++k;
            } else {
                const int f = fxIndex(t);
                if (f < 0) return fail("effet inconnu « " + t + " »");
                if (listed[f]) return fail(std::string("effet cité deux fois : ") + t);
                if (k + 3 >= w.size()) return fail("réglages manquants pour " + t);
                listed[f] = true;
                out.on[f] = true;
                out.order[nOrder++] = f;
                for (int j = 0; j < 3; ++j)
                    if (!num(w[k + 1 + (size_t)j], out.p[f][j]) || out.p[f][j] < 0.f || out.p[f][j] > 1.f) return fail("réglage invalide pour " + t);
                k += 4;
                if (f == kFxEq) out.mix[f] = 1.f;  // l'égaliseur se règle par ses gains
                while (k < w.size() && (w[k][0] == 'm' || w[k][0] == '@')) {
                    if (w[k][0] == 'm') {
                        if (!num(w[k].substr(1), out.mix[f]) || out.mix[f] <= 0.f || out.mix[f] > 1.f) return fail("MIX invalide pour " + t);
                    } else {
                        const size_t sg = w[k].find_first_of("+-");
                        if (sg == std::string::npos) return fail("modulation mal écrite " + w[k]);
                        const int s = modIndex(w[k].substr(1, sg - 1));
                        float d = 0.f;
                        if (s < 0 || !num(w[k].substr(sg), d) || std::fabs(d) > 100.f) return fail("modulation mal écrite " + w[k]);
                        out.modSrc[f] = s;
                        out.modDepth[f] = d;
                    }
                    ++k;
                }
            }
        }
        if (semi == all.size()) break;
    }
    if (nOrder == 0) return fail("aucun effet");
    for (int f = 0; f < kNumFx; ++f)
        if (!listed[f]) out.order[nOrder++] = f;
    return true;
}

} // namespace raw2
