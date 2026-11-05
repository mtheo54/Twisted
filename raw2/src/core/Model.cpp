#include "core/Model.h"
#include "core/FactoryData.h"
#include "core/FactoryFx.h"
#include "core/Files.h"
#include "core/Wav.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace raw2 {

namespace {

const MipTable* sharedFallback() {
    static const MipTable* t = [] {
        auto* m = new MipTable();  // vit jusqu'au déchargement de la DLL, partagé
        Wavetable w;
        buildFallback(w, 0.5f);
        m->build(w);
        return m;
    }();
    return t;
}

json::Value tableJson(const Wavetable& t, bool mounted) {
    json::Value o = json::Value::object();
    o.set("name", t.name);
    o.set("mounted", mounted);
    if (t.spec) {
        json::Value s = json::Value::object();
        s.set("shape", shapeKey(t.spec->shape));
        s.set("morphStart", t.spec->morphStart);
        s.set("morphEnd", t.spec->morphEnd);
        o.set("shape", s);
    } else {
        auto put = [](std::vector<unsigned char>& b, size_t& p, float v) {
            const int q = (int)std::lround(std::clamp(v, -1.f, 1.f) * 32767.f);
            b[p++] = (unsigned char)(q & 0xFF);
            b[p++] = (unsigned char)((q >> 8) & 0xFF);
        };
        std::vector<unsigned char> b((size_t)t.frames() * kPoints * 2);
        size_t p = 0;
        for (const Frame& f : t.fr)
            for (float v : f) put(b, p, v);
        o.set("count", t.frames());
        o.set("frames", json::base64Encode(b.data(), b.size()));
        // version importée (2048 points) des frames intactes : la table garde tout son spectre
        std::string ok;
        for (int f = 0; f < t.frames(); ++f) ok += t.hiFrame(f) ? '1' : '0';
        if (ok.find('1') != std::string::npos) {
            std::vector<unsigned char> h;
            size_t q = 0;
            for (int f = 0; f < t.frames(); ++f)
                if (t.hiFrame(f)) {
                    h.resize(h.size() + (size_t)kHiLen * 2);
                    for (int i = 0; i < kHiLen; ++i) put(h, q, (*t.hi)[(size_t)f * kHiLen + (size_t)i]);
                }
            o.set("hiOk", ok);
            o.set("hi", json::base64Encode(h.data(), h.size()));
        }
    }
    return o;
}

bool tableFromJson(const json::Value& o, Wavetable& t, bool& mounted) {
    if (!o.isObject()) return false;
    mounted = o["mounted"].boolean(true);
    const std::string name = o["name"].str();
    if (o["shape"].isObject()) {
        Shape sh;
        if (!shapeFromKey(o["shape"]["shape"].str().c_str(), sh)) return false;
        buildFromSpec(t, ShapeSpec{sh, (float)o["shape"]["morphStart"].num(0), (float)o["shape"]["morphEnd"].num(1)}, name);
        return true;
    }
    std::vector<unsigned char> b;
    const int count = std::clamp((int)o["count"].num(kFrames), 1, kMaxFrames);
    if (!json::base64Decode(o["frames"].str(), b) || b.size() < (size_t)count * kPoints * 2) return false;
    auto get = [](const std::vector<unsigned char>& d, size_t& p) {
        const int16_t q = (int16_t)(d[p] | (d[p + 1] << 8));
        p += 2;
        return (float)q / 32767.f;
    };
    t.fr.assign((size_t)count, Frame{});
    size_t p = 0;
    for (Frame& f : t.fr)
        for (float& v : f) v = get(b, p);
    t.dropHi();
    const std::string ok = o["hiOk"].str();
    std::vector<unsigned char> h;
    if (ok.size() == (size_t)count && json::base64Decode(o["hi"].str(), h)) {
        const size_t nHi = (size_t)std::count(ok.begin(), ok.end(), '1');
        if (nHi > 0 && h.size() >= nHi * kHiLen * 2) {
            auto hi = std::make_shared<std::vector<float>>((size_t)count * kHiLen, 0.f);
            t.hiOk.assign((size_t)count, false);
            size_t q = 0;
            for (int f = 0; f < count; ++f) {
                if (ok[(size_t)f] != '1') continue;
                for (int i = 0; i < kHiLen; ++i) (*hi)[(size_t)f * kHiLen + (size_t)i] = get(h, q);
                t.hiOk[(size_t)f] = true;
            }
            t.hi = hi;
        }
    }
    t.name = name;
    t.spec.reset();
    return true;
}

} // namespace

Model::Model(Mode mode) : life(params, mode == Mode::Live), samples(mode == Mode::Offline), instruments(mode == Mode::Offline), offline_(mode == Mode::Offline) {
#if defined(_WIN32)
    log("RAW 2 v" RAW2_VERSION_STRING " (Windows) ouvert");
#elif defined(__APPLE__)
    log("RAW 2 v" RAW2_VERSION_STRING " (macOS) ouvert");
#else
    log("RAW 2 v" RAW2_VERSION_STRING " ouvert");
#endif
    samples.bindEpoch(&audioEpoch_, &inProcess_);
    instruments.bindEpoch(&audioEpoch_, &inProcess_);
    resetFxOrder();
    fallback_ = sharedFallback();
    for (auto& m : mip_) m.store(fallback_);
    for (auto& d : dirty_) d.assign(kMaxFrames, false);
    applyFactoryPreset(0, nullptr);
    if (offline_) {
        buildNow();
        return;
    }
    loadGlobalSettings();
    builder_ = std::thread([this] { builderMain(); });
}

Model::~Model() {
    {
        std::lock_guard<std::mutex> lk(buildMutex_);
        buildRun_ = false;
    }
    buildCv_.notify_all();
    if (builder_.joinable()) builder_.join();
    for (auto& m : mip_) m.store(fallback_);
    collectGarbage(true);
    for (auto* t : owned_) delete t;  // les tables de left_ sont aussi dans owned_
}

void Model::tablesChanged(int osc, int frame) {
    {
        std::lock_guard<std::mutex> lk(buildMutex_);
        if (frame < 0) fullDirty_[(size_t)osc] = true;
        else if (frame < kMaxFrames) dirty_[(size_t)osc][(size_t)frame] = true;
        pending_[(size_t)osc] = true;
    }
    {
        std::lock_guard<std::mutex> lk(tableMutex);
        ++tableVersion[(size_t)osc];
    }
    if (offline_) buildNow();
    else buildCv_.notify_all();
}

void Model::setMounted(int osc, bool m) {
    {
        std::lock_guard<std::mutex> lk(tableMutex);
        mounted[(size_t)osc] = m;
        ++tableVersion[(size_t)osc];
    }
    {
        std::lock_guard<std::mutex> lk(buildMutex_);
        pending_[(size_t)osc] = true;
    }
    if (offline_) buildNow();
    else buildCv_.notify_all();
}

void Model::swapPlaying(int osc, const MipTable* playing) {
    // Une table qui quitte mip_ ne peut plus être prise que par un bloc audio déjà commencé :
    // on note l'époque audio et si un bloc était en cours, pour la supprimer plus tard sans risque.
    const MipTable* before = mip_[(size_t)osc].exchange(playing, std::memory_order_seq_cst);
    const bool busy = inProcess_.load(std::memory_order_seq_cst);
    const uint64_t ep = audioEpoch_.load(std::memory_order_seq_cst);
    left_.erase(std::remove_if(left_.begin(), left_.end(), [&](const Retired& r) { return r.t == playing; }), left_.end());
    if (!before || before == playing || before == fallback_) return;
    if (before == owned_[0] || before == owned_[1]) left_.push_back({before, ep, busy});
    else retired_.push_back({before, ep, busy});
}

void Model::collectGarbage(bool force) {
    const uint64_t ep = audioEpoch_.load(std::memory_order_seq_cst);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(), [&](const Retired& r) {
        if (!(force || !r.wasInProcess || ep > r.epoch)) return false;
        delete r.t;
        return true;
    }), retired_.end());
}

void Model::builderMain() {
    std::unique_lock<std::mutex> lk(buildMutex_);
    while (buildRun_) {
        buildCv_.wait_for(lk, std::chrono::milliseconds(100), [&] { return !buildRun_ || pending_[0] || pending_[1]; });
        if (!buildRun_) break;
        buildPendingLocked(lk);
        // regroupe les modifications rapides (tracé à la souris)
        lk.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        lk.lock();
    }
}

void Model::buildNow() {
    std::unique_lock<std::mutex> lk(buildMutex_);
    buildPendingLocked(lk);
}

void Model::buildPendingLocked(std::unique_lock<std::mutex>& lk) {
    collectGarbage(false);
    for (int osc = 0; osc < 2; ++osc) {
        if (!pending_[(size_t)osc]) continue;
        pending_[(size_t)osc] = false;
        const bool full = fullDirty_[(size_t)osc];
        const auto dirty = dirty_[(size_t)osc];
        fullDirty_[(size_t)osc] = false;
        std::fill(dirty_[(size_t)osc].begin(), dirty_[(size_t)osc].end(), false);
        lk.unlock();

        Wavetable copy;
        bool isMounted;
        {
            std::lock_guard<std::mutex> tl(tableMutex);
            copy = edit[(size_t)osc];
            isMounted = mounted[(size_t)osc];
        }
        const MipTable* prev = owned_[(size_t)osc];
        bool anyDirty = full || !prev;
        for (bool d : dirty) anyDirty = anyDirty || d;
        const MipTable* next = prev;
        if (anyDirty) {
            const bool partial = !full && prev && prev->frames() == copy.frames();
            MipTable* t = partial ? new MipTable(*prev) : new MipTable();
            if (partial) t->rebuildFrames(copy, dirty);
            else t->build(copy);
            next = t;
        }

        lk.lock();
        if (next != prev) {
            owned_[(size_t)osc] = next;
            if (prev && mip_[(size_t)osc].load() != prev) {
                // déjà hors service : on reprend la trace de sa sortie, sinon suppression immédiate
                Retired rec{prev, 0, false};
                for (auto it = left_.begin(); it != left_.end(); ++it)
                    if (it->t == prev) { rec = *it; left_.erase(it); break; }
                retired_.push_back(rec);
            }
        }
        swapPlaying(osc, isMounted ? next : fallback_);
    }
}

void Model::setParam(ParamSink* sink, uint32_t id, float v) {
    if (sink) {
        sink->beginEdit(id);
        sink->performEdit(id, v);
        sink->endEdit(id);
    } else {
        params.set(id, v);
    }
}

void Model::resetLifeAfterPreset() { life.reset(LifeEngine::Small); }

// Effets des presets d'usine : une recette par preset (src/core/FactoryFx.cpp), pour que chaque son
// ait son propre rack, son ordre et ses réglages.
namespace {
#include "core/FxTrim.inc"

// voix de chaque preset d'usine (page ENV / LFO) : générée par tools/gen_voices.py
struct FactoryVoice {
    float a[5], b[5];  // enveloppes A et B : attack, decay, sustain, release, courbe
    int modeA, modeB, link;
    int l1Shape;
    float l1[4];       // vitesse, phase, fade, lissage
    int l1Sync, l2Shape;
    float l2[4];
    int l2Sync;
    float cutoff, reso;
    const char* matrix;  // « source>destination:dosage » séparés par des espaces
    const char* rack;    // rack modulaire : LFO R1..R6, formes dessinées, routes libres (voir applyRack)
};
#include "core/FactoryVoice.inc"

void applyVoiceMatrix(ModMatrix& m, const char* spec) {
    static const char* src[kNumSources] = {"vie", "lfo1", "lfo2", "env", "kb", "rnd"};
    static const char* dst[kNumDests] = {"pitchA", "pitchB", "posA", "posB", "cutoff", "reso", "grain", "master"};
    m.clear();
    std::string s = spec;
    size_t p = 0;
    while (p < s.size()) {
        while (p < s.size() && s[p] == ' ') ++p;
        size_t e = s.find(' ', p);
        if (e == std::string::npos) e = s.size();
        const std::string t = s.substr(p, e - p);
        p = e;
        const size_t gt = t.find('>'), co = t.find(':');
        if (gt == std::string::npos || co == std::string::npos || co < gt) continue;
        int si = -1, di = -1;
        for (int k = 0; k < kNumSources; ++k) if (t.compare(0, gt, src[k]) == 0 && std::strlen(src[k]) == gt) si = k;
        for (int k = 0; k < kNumDests; ++k) if (t.compare(gt + 1, co - gt - 1, dst[k]) == 0 && std::strlen(dst[k]) == co - gt - 1) di = k;
        if (si < 0 || di < 0) continue;
        m.amt[(size_t)ModMatrix::idx(si, di)] = std::clamp(std::atoi(t.c_str() + co + 1), -100, 100);
        m.on[(size_t)ModMatrix::idx(si, di)] = true;
    }
}
} // namespace

void Model::applyFactoryFx(int i, ParamSink* sink) {
    if (i < 0 || i >= kNumFactoryPresets) return;
    FactoryFxSetup fx;
    if (!factoryFxSetup(i, fx)) return;  // recettes vérifiées par les tests
    for (int f = 0; f < kNumFx; ++f) {
        setParam(sink, fxParam(f, kFxOn), fx.on[f] ? 1.f : 0.f);
        for (int j = 0; j < 3; ++j) setParam(sink, fxParam(f, kFxP0 + j), fx.p[f][j]);
        setParam(sink, fxParam(f, kFxMix), fx.mix[f]);
        setParam(sink, fxParam(f, kFxModSrc), (float)fx.modSrc[f]);
        setParam(sink, fxParam(f, kFxModDepth), fx.modDepth[f]);
    }
    setParam(sink, kFxWet, fx.wet);
    setParam(sink, kFxDistMode, (float)fx.distMode);
    setParam(sink, kFxOversample, (float)fx.oversample);
    setParam(sink, kFxEqF0, fx.eqFreq[0]);
    setParam(sink, kFxEqF1, fx.eqFreq[1]);
    setParam(sink, kFxEqF2, fx.eqFreq[2]);
    // correction de volume avec effets (mesurée par tools/fx_trim) : même niveau perçu qu'en sec,
    // un peu plus présent, crêtes sous -3 dBFS
    setParam(sink, kMaster, std::clamp(kFactoryPresets[i].master + kFxTrimDb[i] / 18.2f, 0.05f, 1.f));
    for (int k = 0; k < kNumFx; ++k) fxOrder[(size_t)k].store(fx.order[k]);
}

// enveloppes, LFO, filtre et liaisons de matrice propres au preset (page ENV / LFO)
namespace {
// formes dessinées des presets d'usine (ÉDITION / DRAW)
std::vector<LfoNode> drawnPattern(const std::string& n) {
    std::vector<LfoNode> a;
    auto N = [&](float x, float y, float c = 0.f, float sm = 0.f) { a.push_back(LfoNode{x, y, c, sm}); };
    if (n == "pluck") { N(0.f, 1.f, -0.6f); N(1.f, -1.f); }
    else if (n == "swell") { N(0.f, -1.f, 0.5f); N(0.7f, 1.f, -0.3f); N(1.f, -1.f); }
    else if (n == "breath") { N(0.f, -1.f, 0.3f, 0.6f); N(0.4f, 1.f, -0.2f, 0.6f); N(1.f, -1.f); }
    else if (n == "wobble") { N(0.f, -1.f, 0.f, 0.8f); N(0.25f, 1.f, 0.f, 0.8f); N(0.5f, -0.3f, 0.f, 0.8f); N(0.75f, 0.6f, 0.f, 0.8f); N(1.f, -1.f); }
    else if (n == "stairs") {
        const float y[4] = {-1.f, -0.33f, 0.33f, 1.f};
        for (int k = 0; k < 4; ++k) { N(0.25f * (float)k, y[k]); N(0.25f * (float)k + 0.249f, y[k]); }
        N(1.f, 1.f);
    } else if (n == "gate") {
        N(0.f, 1.f); N(0.2f, 1.f); N(0.26f, -1.f); N(0.5f, -1.f); N(0.52f, 1.f); N(0.62f, 1.f); N(0.68f, -1.f); N(1.f, -1.f);
    } else if (n == "bounce") {
        N(0.f, 1.f, -0.5f); N(0.3f, -1.f, 0.5f); N(0.5f, 0.4f, -0.5f); N(0.65f, -1.f, 0.5f); N(0.78f, -0.1f, -0.5f); N(0.88f, -1.f); N(1.f, -1.f);
    }
    return a;
}
int lfoIndex(const std::string& s) {  // l1, l2, r1..r6
    if (s.size() != 2) return -1;
    const int d = s[1] - '0';
    if (s[0] == 'l' && d >= 1 && d <= 2) return d - 1;
    if (s[0] == 'r' && d >= 1 && d <= kNumRackLfos) return d + 1;
    return -1;
}
} // namespace

// Rack modulaire d'un preset d'usine, jetons séparés par des espaces :
//   r<n>:<forme>:<vitesse>:<sync>[:<phase>]  allume le LFO R<n> du rack
//   draw:<lfo>:<motif>                        forme dessinée (pluck, swell, breath, wobble, stairs, gate, bounce)
//   <lfo>><clé du paramètre>:<dosage %>       route libre (lfo : l1, l2, r1..r6)
void Model::applyRack(const char* spec, ParamSink* sink) {
    std::string s = spec ? spec : "";
    size_t p = 0;
    while (p < s.size()) {
        while (p < s.size() && s[p] == ' ') ++p;
        const size_t e = std::min(s.find(' ', p), s.size());
        const std::string t = s.substr(p, e - p);
        p = e;
        if (t.empty()) continue;
        std::vector<std::string> f;
        for (size_t a = 0;;) {
            const size_t b = t.find(':', a);
            f.push_back(t.substr(a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        if (f[0] == "draw" && f.size() == 3) {
            const int l = lfoIndex(f[1]);
            const auto nodes = drawnPattern(f[2]);
            if (l < 0 || nodes.size() < 2) continue;
            lfoShapes.set(l, nodes);
            lfoShapes.edited[(size_t)l] = true;
            setParam(sink, lfoP(l, kLfDraw), 1.f);
            if (l >= 2) setParam(sink, lfoP(l, kLfOn), 1.f);
        } else if (f[0].size() == 2 && f[0][0] == 'r' && f.size() >= 4) {
            const int l = lfoIndex(f[0]);
            if (l < 2) continue;
            setParam(sink, lfoP(l, kLfOn), 1.f);
            setParam(sink, lfoP(l, kLfShape), (float)std::atoi(f[1].c_str()));
            setParam(sink, lfoP(l, kLfRate), (float)std::atof(f[2].c_str()));
            setParam(sink, lfoP(l, kLfSync), (float)std::atoi(f[3].c_str()));
            if (f.size() >= 5) setParam(sink, lfoP(l, kLfPhase), (float)std::atof(f[4].c_str()));
        } else {
            const size_t gt = t.find('>'), co = t.rfind(':');
            if (gt == std::string::npos || co == std::string::npos || co < gt) continue;
            const int l = lfoIndex(t.substr(0, gt));
            const int id = paramIdFromKey(t.substr(gt + 1, co - gt - 1).c_str());
            if (l < 0 || id < 0 || !isModulatable((uint32_t)id)) continue;
            if (l >= 2) setParam(sink, lfoP(l, kLfOn), 1.f);
            routes.add(l, (uint32_t)id, std::clamp(std::atoi(t.c_str() + co + 1), -100, 100));
        }
    }
}

void Model::applyFactoryVoice(int i, ParamSink* sink) {
    if (i < 0 || i >= kNumFactoryPresets) return;
    const FactoryVoice& fv = kVoices[i];
    const uint32_t envA[5] = {kAttack, kDecay, kSustain, kRelease, kEnvACurve}, envB[5] = {kEnvBAttack, kEnvBDecay, kEnvBSustain, kEnvBRelease, kEnvBCurve};
    for (int k = 0; k < 5; ++k) {
        setParam(sink, envA[k], fv.a[k]);
        setParam(sink, envB[k], fv.b[k]);
    }
    setParam(sink, kEnvAMode, (float)fv.modeA);
    setParam(sink, kEnvBMode, (float)fv.modeB);
    setParam(sink, kEnvLink, (float)fv.link);
    for (int k = 0; k < 4; ++k) {
        setParam(sink, lfoParam(0, k), fv.l1[k]);
        setParam(sink, lfoParam(1, k), fv.l2[k]);
    }
    setParam(sink, kLfo1Shape, (float)fv.l1Shape);
    setParam(sink, kLfo1Sync, (float)fv.l1Sync);
    setParam(sink, kLfo2Shape, (float)fv.l2Shape);
    setParam(sink, kLfo2Sync, (float)fv.l2Sync);
    setParam(sink, kCutoff, fv.cutoff);
    setParam(sink, kReso, fv.reso);
    applyVoiceMatrix(matrix, fv.matrix);
    applyRack(fv.rack, sink);
}

void Model::applyFactoryPreset(int i, ParamSink* sink) {
    if (i < 0 || i >= kNumFactoryPresets) return;
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        presetNewer.clear();
    }
    const FactoryPreset& b = kFactoryPresets[i];
    setParam(sink, kAPos, b.aPos);
    setParam(sink, kBPos, b.bPos);
    setParam(sink, kALvl, b.aLevel);
    setParam(sink, kBLvl, b.bLevel);
    setParam(sink, kAOct, (float)b.aOct);
    setParam(sink, kASt, (float)b.aSemi);
    setParam(sink, kBOct, (float)b.bOct);
    setParam(sink, kBSt, (float)b.bSemi);
    setParam(sink, kSpeed, b.lifeSpeed);
    setParam(sink, kBirth, b.lifeBirth);
    setParam(sink, kQty, b.lifeQty);
    setParam(sink, kM1, (float)b.depthHarm);
    setParam(sink, kM2, (float)b.depthSpace);
    setParam(sink, kM3, (float)b.depthTexture);
    setParam(sink, kMaster, b.master);
    setParam(sink, kRule, b.highLife ? 1.f : 0.f);
    // rack modulaire, formes dessinées, lecture SAMPLE : valeurs par défaut, puis la voix du preset
    for (uint32_t id = kV3End; id < kNumParams; ++id)
        if (!isPerformanceParam(id)) setParam(sink, id, paramDef(id).def);
    routes.clear();
    for (int l = 0; l < kNumLfos; ++l) { lfoShapes.reset(l); lfoShapes.snap[(size_t)l] = 0; }
    lfoShapes.grp.fill(0);
    instruments.clear(0);
    instruments.clear(1);
    applyFactoryVoice(i, sink);
    // basses 808 / trap : jeu monophonique lié avec glissando (les notes qui se chevauchent glissent),
    // un peu de « punch » de hauteur à l'attaque et du sous-oscillateur
    {
        struct Glide808 { const char* name; float glide, punch, sub; };
        static const Glide808 k808[] = {
            {"808 fum\xC3\xA9" "e", 0.32f, 0.2f, 0.35f},
            {"b\xC3\xA9" "ton liquide", 0.28f, 0.15f, 0.2f},
            {"phonk nuit", 0.25f, 0.f, 0.2f},
            {"plug lourd", 0.22f, 0.f, 0.f},
        };
        for (const Glide808& g : k808) {
            if (std::strcmp(kFactoryPresets[i].name, g.name) != 0) continue;
            setParam(sink, kPolyMode, 2.f);
            setParam(sink, kGlide, g.glide);
            setParam(sink, kGlideMode, 1.f);
            setParam(sink, kPunch, g.punch);
            setParam(sink, kSubLvl, g.sub);
        }
    }
    // oscillateurs (maquette mise à jour) : warp, panoramique et mode de phase selon le numéro
    setParam(sink, kAWarp, (float)((i * 29) % 40) / 100.f);
    setParam(sink, kBWarp, (float)((i * 17) % 30) / 100.f);
    setParam(sink, kAPan, 0.45f - (float)((i * 7) % 11) / 100.f);
    setParam(sink, kBPan, 0.55f + (float)((i * 5) % 11) / 100.f);
    setParam(sink, kAMode, (i % 7 == 3) ? 3.f : (i % 11 == 5 ? 1.f : 0.f));
    setParam(sink, kBMode, (i % 5 == 2) ? 1.f : (i % 13 == 4 ? 4.f : 0.f));
    applyFactoryFx(i, sink);
    char num[8];
    std::snprintf(num, sizeof num, "%02d", i + 1);
    const std::string nm = std::string(num) + " \xC2\xB7 " + b.name;
    {
        std::lock_guard<std::mutex> lk(tableMutex);
        buildFromSpec(edit[0], b.oscA, nm);
        buildFromSpec(edit[1], b.oscB, nm + " (B)");
        mounted = {true, true};
    }
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        presetIndex = i;
        presetPack.clear();
        presetName = b.name;
        presetCategory = categoryName(b.category);
        presetStyle = b.style;
    }
    tablesChanged(0);
    tablesChanged(1);
    resetLifeAfterPreset();
}

json::Value Model::presetJson(bool withSamples) {
    json::Value v = json::Value::object();
    v.set("format", "RAW2-preset");
    v.set("version", kPresetFormat);
    v.set("app", RAW2_VERSION_STRING);  // version de RAW 2 qui a écrit le preset
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        v.set("name", presetName);
        v.set("category", presetCategory);
        v.set("style", presetStyle);
    }
    json::Value p = json::Value::object();
    // réglages de jeu (MPE, pression…) : dans l'état de l'instance seulement, pas dans un preset
    for (uint32_t i = 0; i < kNumParams; ++i)
        if (withSamples || !isPerformanceParam(i) || i == kPitchBend || i == kModWheel) p.set(paramDef(i).key, (double)params.get(i));
    v.set("params", p);
    json::Value order = json::Value::array();
    for (auto& o : fxOrder) order.push(fxKey(o.load()));
    v.set("fxOrder", order);
    {
        std::lock_guard<std::mutex> lk(tableMutex);
        v.set("oscA", tableJson(edit[0], mounted[0]));
        v.set("oscB", tableJson(edit[1], mounted[1]));
    }
    json::Value mx = json::Value::array();
    for (int s = 0; s < kNumSources; ++s)
        for (int d = 0; d < kNumDests; ++d)
            if (matrix.on[(size_t)ModMatrix::idx(s, d)]) {
                json::Value l = json::Value::object();
                l.set("src", s);
                l.set("dst", d);
                l.set("amt", matrix.amt[(size_t)ModMatrix::idx(s, d)].load());
                mx.push(l);
            }
    v.set("matrix", mx);
    v.set("routes", routes.save());
    v.set("lfoDraw", lfoShapes.save());
    // sons déposés : toujours dans l'état ; dans un preset seulement s'ils sont joués en SAMPLE
    for (int o = 0; o < 2; ++o)
        if (samples.has(o) && (withSamples || params.get(o ? kBPlay : kAPlay) > 0.5f)) v.set(o ? "sampleB" : "sampleA", samples.save(o));
    // instruments : référencés par leur nom (les fichiers restent dans Documents › Raw 2 › Instruments)
    for (int o = 0; o < 2; ++o)
        if (!instruments.name(o).empty()) v.set(o ? "instB" : "instA", instruments.save(o));
    return v;
}

void Model::log(const std::string& msg) const {
    if (!offline_) files::logLine(msg);
}

bool Model::applyPresetJson(const json::Value& v, ParamSink* sink) {
    if (!v.isObject() || !v["params"].isObject()) return false;
    {
        // preset d'une version plus récente : chargé quand même (réglages inconnus ignorés), l'interface prévient
        std::lock_guard<std::mutex> lk(presetMutex);
        if (v["version"].num(1) > kPresetFormat) {
            const std::string app = v["app"].str();
            presetNewer = app.empty() ? std::string("une version plus r\xC3\xA9" "cente") : "RAW 2 v" + app;
        } else {
            presetNewer.clear();
        }
    }
    if (!presetNewer.empty()) log("preset d'une version plus r\xC3\xA9" "cente (" + presetNewer + ") : " + v["name"].str());
    if (v["version"].num(1) < 2) {  // preset d'avant la page EFX : nouveaux réglages par défaut, effets coupés
        for (uint32_t id = kAWarp; id < kNumParams; ++id)
            if (!isPerformanceParam(id)) setParam(sink, id, paramDef(id).def);
        resetFxOrder();
    }
    // ordre des effets : un preset d'avant VINTAGE / PUMP en a 10, les effets absents vont à la fin
    if (v["fxOrder"].isArray() && v["fxOrder"].items().size() >= (size_t)kNumFxV1 && v["fxOrder"].items().size() <= (size_t)kNumFx) {
        const size_t n = v["fxOrder"].items().size();
        int ord[kNumFx];
        bool used[kNumFx] = {};
        bool ok = true;
        for (size_t k = 0; k < n && ok; ++k) {
            ord[k] = -1;
            for (int f = 0; f < kNumFx; ++f)
                if (v["fxOrder"][k].str() == fxKey(f)) ord[k] = f;
            ok = ord[k] >= 0 && !used[ord[k]];
            if (ok) used[ord[k]] = true;
        }
        size_t m = n;
        for (int f = 0; f < kNumFx && ok; ++f)
            if (!used[f]) ord[m++] = f;
        if (ok) for (int k = 0; k < kNumFx; ++k) fxOrder[(size_t)k].store(ord[k]);
    }
    if (v["version"].num(1) < 3) {  // preset d'avant la page ENV / LFO : réglages ENV / LFO par défaut
        for (uint32_t id = kEnvACurve; id < kV3End; ++id) setParam(sink, id, paramDef(id).def);
    }
    if (v["version"].num(1) < 4)  // d'avant le rack modulaire et la lecture SAMPLE
        for (uint32_t id = kV3End; id < kV4End; ++id) setParam(sink, id, paramDef(id).def);
    if (v["version"].num(1) < 5)  // d'avant la page JEU, VINTAGE et PUMP
        for (uint32_t id = kV4End; id < kV5End; ++id) setParam(sink, id, paramDef(id).def);
    if (v["version"].num(1) < 6)  // d'avant la page SYNTH (filtre multimode, warps, bruit, ENV 3 / 4, MPE)
        for (uint32_t id = kV5End; id < kNumParams; ++id)
            if (!isPerformanceParam(id)) setParam(sink, id, paramDef(id).def);
    for (const auto& m : v["params"].members()) {
        const int id = paramIdFromKey(m.first.c_str());
        if (id >= 0 && m.second.isNumber()) setParam(sink, (uint32_t)id, (float)m.second.num());
    }
    if (v["version"].num(1) < 3) {
        // une seule enveloppe avant la version 3 : l'OSC B reprend celle de l'OSC A
        const uint32_t from[4] = {kAttack, kDecay, kSustain, kRelease}, to[4] = {kEnvBAttack, kEnvBDecay, kEnvBSustain, kEnvBRelease};
        for (int k = 0; k < 4; ++k) setParam(sink, to[k], params.get(from[k]));
    }
    {
        std::lock_guard<std::mutex> lk(tableMutex);
        bool mt;
        if (tableFromJson(v["oscA"], edit[0], mt)) mounted[0] = mt;
        if (tableFromJson(v["oscB"], edit[1], mt)) mounted[1] = mt;
    }
    if (v["matrix"].isArray()) {
        matrix.clear();
        for (const auto& l : v["matrix"].items()) {
            const int s = (int)l["src"].num(-1), d = (int)l["dst"].num(-1);
            if (s < 0 || s >= kNumSources || d < 0 || d >= kNumDests) continue;
            matrix.amt[(size_t)ModMatrix::idx(s, d)] = std::clamp((int)l["amt"].num(0), -100, 100);
            matrix.on[(size_t)ModMatrix::idx(s, d)] = true;
        }
    }
    routes.load(v["routes"]);
    lfoShapes.load(v["lfoDraw"]);
    for (int o = 0; o < 2; ++o)
        if (v.has(o ? "sampleB" : "sampleA")) samples.loadJson(o, v[o ? "sampleB" : "sampleA"]);
    for (int o = 0; o < 2; ++o) {
        if (v.has(o ? "instB" : "instA")) instruments.loadJson(o, v[o ? "instB" : "instA"]);
        else instruments.clear(o);
    }
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        presetName = v["name"].str().empty() ? std::string("sans nom") : v["name"].str();
        presetCategory = v["category"].str();
        presetStyle = v["style"].str();
    }
    tablesChanged(0);
    tablesChanged(1);
    return true;
}

bool Model::saveUserPreset(const std::string& rawName, std::string& outPath) {
    std::string name;
    for (char c : rawName)  // caractères interdits dans un nom de fichier Windows
        if (!std::strchr("<>:\"/\\|?*", c) && (unsigned char)c >= 0x20) name += c;
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    if (name.empty()) return false;
    const std::string dir = files::myPresetsDir();
    files::makeDirs(dir);
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        presetName = name;
    }
    json::Value v = presetJson();
    outPath = files::join(dir, name + ".raw");
    if (!files::writeText(outPath, v.dump(true))) return false;
    scanPacks();
    std::lock_guard<std::mutex> lk(presetMutex);
    presetIndex = kPresetPack;
    presetPack = "MES PRESETS";
    return true;
}

void Model::setTab(int t) {
    tab = t;
    life.setSoundWorld(t == 1 ? LifeEngine::Big : LifeEngine::Small);
}

int Model::zoomFrom(const json::Value& o) {
    int z = (int)o["zoom"].num(kDefaultZoom);
    // avant la v1.0.0, la taille de base (100 %) était trop grande sur un écran de bureau : elle
    // devient 85 % (les autres tailles, dont 75 %, ne changent pas)
    if (!o["zoomV"].isNumber() && z == 100) z = kDefaultZoom;
    for (int k : kZooms)
        if (z == k) return z;
    return kDefaultZoom;
}

void Model::loadGlobalSettings() {
    std::string text;
    json::Value v;
    if (!files::readText(files::settingsPath(), text) || !json::Value::parse(text, v)) return;
    theme = std::clamp((int)v["theme"].num(0), 0, kNumThemes - 1);
    zoomPct = zoomFrom(v);
    favorites.clear();
    if (v["favorites"].isArray())
        for (const auto& f : v["favorites"].items())
            if (f.isString()) favorites.insert(f.str());
}

void Model::saveGlobalSettings() {
    json::Value v = json::Value::object();
    v.set("theme", theme.load());
    v.set("zoom", zoomPct.load());
    v.set("zoomV", 1);
    json::Value fav = json::Value::array();
    for (const auto& f : favorites) fav.push(f);
    v.set("favorites", fav);
    const std::string p = files::settingsPath();
    const size_t s = p.find_last_of("/\\");
    if (s != std::string::npos) files::makeDirs(p.substr(0, s));
    files::writeText(p, v.dump(true));
}

std::string Model::saveState() {
    json::Value v = json::Value::object();
    v.set("format", "RAW2-state");
    v.set("version", 1);
    v.set("preset", presetJson(true));
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        v.set("presetIndex", presetIndex);
        v.set("presetPack", presetPack);
    }
    v.set("life", life.saveState());
    json::Value mx = json::Value::object();
    json::Value fl = json::Value::array();
    for (const auto& f : mix.flt) {
        if (!f.on.load()) continue;
        json::Value o = json::Value::object();
        o.set("type", f.type.load());
        o.set("hz", (double)f.hz.load());
        o.set("db", (double)f.db.load());
        o.set("q", (double)f.q.load());
        fl.push(o);
    }
    mx.set("filters", fl);
    mx.set("compThresh", (double)mix.compThreshDb.load());
    mx.set("comp", mix.comp.load());
    mx.set("limit", mix.limit.load());
    mx.set("limitCeil", (double)mix.limitCeilDb.load());
    v.set("mix", mx);
    json::Value ui = json::Value::object();
    ui.set("theme", theme.load());
    ui.set("zoom", zoomPct.load());
    ui.set("zoomV", 1);
    // « tab » : 5 pages (v1), « tab2 » : 6 pages (v2, EFX en 3), « tab3 » : 7 pages (ENV / LFO en 3)
    {
        // « tab4 » : 8 pages (JEU en 7) ; les anciens lecteurs reçoivent SON pour la page JEU
        // « tab5 » : 9 pages (SYNTH en 8) ; les anciens lecteurs reçoivent SON pour la page SYNTH
        const int t5 = tab.load(), t4 = t5 == 8 ? 0 : t5, t3 = t4 == 7 ? 0 : t4, t2 = t3 == 3 ? 0 : (t3 > 3 ? t3 - 1 : t3);
        ui.set("tab", std::min(4, t2 >= 4 ? t2 - 1 : t2));
        ui.set("tab2", t2);
        ui.set("tab3", t3);
        ui.set("tab4", t4);
        ui.set("tab5", t5);
    }
    ui.set("lfoBpm1", lfoBpm[0].load());
    ui.set("lfoBpm2", lfoBpm[1].load());
    ui.set("bpm", bpmDisplay.load());
    ui.set("samples", controlBlock.load());
    ui.set("kbOct", kbOct.load());
    ui.set("kbScale", kbScale.load());
    ui.set("kbHold", kbHold.load());
    v.set("ui", ui);
    return v.dump();
}

bool Model::loadState(const std::string& s, ParamSink* sink) {
    json::Value v;
    if (!json::Value::parse(s, v) || !v.isObject() || v["format"].str() != "RAW2-state") {
        log("\xC3\xA9tat du projet illisible (" + std::to_string(s.size()) + " octets) : RAW 2 garde le son actuel");
        return false;
    }
    applyPresetJson(v["preset"], sink);
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        presetIndex = (int)v["presetIndex"].num(0);
        presetPack = v["presetPack"].str();
        if (presetIndex >= kNumFactoryPresets) {  // ancien état : preset utilisateur numéroté après l'usine
            presetIndex = kPresetPack;
            if (presetPack.empty()) presetPack = "MES PRESETS";
        }
        if (presetIndex < kPresetPack) presetIndex = 0;
    }
    if (v["life"].isString()) life.loadState(v["life"].str());
    const json::Value& mx = v["mix"];
    mix.clear();
    if (mx["filters"].isArray()) {
        for (const auto& o : mx["filters"].items())
            mix.addFilter(std::clamp((int)o["type"].num(0), 0, 2), std::clamp((float)o["hz"].num(1000), 16.f, 20000.f), std::clamp((float)o["db"].num(0), -18.f, 18.f),
                          std::clamp((float)o["q"].num(1), 0.1f, 18.f));
    } else if (mx["eq"].isArray()) {  // état d'avant la liste de filtres : les quatre corrections fixes
        const struct { int type; float hz, q; } ref[kNumMixEq] = {{0, 250.f, 1.2f}, {1, 8000.f, 1.f}, {0, 80.f, 1.f}, {0, 3500.f, 2.f}};
        for (int e = 0; e < kNumMixEq; ++e)
            if (mx["eq"][(size_t)e].boolean(false))
                mix.addFilter(ref[e].type, ref[e].hz, std::clamp((float)mx["gains"][(size_t)e].num(MixChain::defaultGain(e)), -9.f, 9.f), ref[e].q);
    }
    mix.compThreshDb = std::clamp((float)mx["compThresh"].num(-18.0), -40.f, -6.f);
    mix.comp = mx["comp"].boolean(false);
    mix.limit = mx["limit"].boolean(false);
    mix.limitCeilDb = std::clamp((float)mx["limitCeil"].num(-1.0), -3.f, 0.f);
    const json::Value& ui = v["ui"];
    if (ui.isObject()) {
        theme = std::clamp((int)ui["theme"].num(0), 0, kNumThemes - 1);
        zoomPct = zoomFrom(ui);
        // « tab2 » : 6 pages (EFX ajoutée en 3) ; les anciens états n'ont que « tab » sur 5 pages
        if (ui["tab5"].isNumber()) setTab(std::clamp((int)ui["tab5"].num(0), 0, 8));
        else if (ui["tab4"].isNumber()) setTab(std::clamp((int)ui["tab4"].num(0), 0, 7));
        else if (ui["tab3"].isNumber()) setTab(std::clamp((int)ui["tab3"].num(0), 0, 6));
        else if (ui["tab2"].isNumber()) { const int t = std::clamp((int)ui["tab2"].num(0), 0, 5); setTab(t >= 3 ? t + 1 : t); }
        else { const int t = std::clamp((int)ui["tab"].num(0), 0, 4); setTab(t >= 3 ? t + 2 : t); }
        lfoBpm[0] = ui["lfoBpm1"].boolean(false);
        lfoBpm[1] = ui["lfoBpm2"].boolean(false);
        bpmDisplay = ui["bpm"].boolean(false);
        const int smp = (int)ui["samples"].num(64);
        controlBlock = (smp == 32 || smp == 64 || smp == 128 || smp == 256) ? smp : 64;
        kbOct = std::clamp((int)ui["kbOct"].num(0), -2, 2);
        kbScale = std::clamp((int)ui["kbScale"].num(0), 0, 5);
        kbHold = ui["kbHold"].boolean(false);
    }
    return true;
}

} // namespace raw2
