#include "dsp/Engine.h"

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cmath>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <xmmintrin.h>
#define RAW2_HAS_SSE 1
#endif

namespace raw2 {

namespace {
constexpr float kPi = 3.14159265358979f;

// Padé (3,2) de tanh : vaut exactement ±1 avec une pente nulle en ±3, donc borner à ±3 donne une
// courbe continue et dérivable (à ±4, elle dépassait 1 puis retombait d'un coup : petit crénelage)
inline float fastTanh(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float softSafety(float x) {
    const float a = std::fabs(x);
    if (a < 0.95f) return x;
    const float y = 0.95f + 0.05f * fastTanh((a - 0.95f) / 0.05f);
    return x < 0.f ? -y : y;
}

struct DenormalGuard {
#ifdef RAW2_HAS_SSE
    unsigned int csr;
    DenormalGuard() : csr(_mm_getcsr()) { _mm_setcsr(csr | 0x8040); }
    ~DenormalGuard() { _mm_setcsr(csr); }
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    // ARM 64 bits (Apple Silicon) : bit FZ du registre FPCR (sous-normaux mis à zéro)
    uint64_t fpcr;
    DenormalGuard() {
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr | (1ull << 24)));
    }
    ~DenormalGuard() { __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr)); }
#endif
};

inline double noteHz(double n) { return 440.0 * std::pow(2.0, (n - 69.0) / 12.0); }

} // namespace

Engine::Engine(Model& m) : m_(m) {
    for (int i = 0; i <= 512; ++i) hann_[(size_t)i] = 0.5f - 0.5f * std::cos(2.f * kPi * (float)i / 512.f);
    for (int i = 0; i <= 1024; ++i) sin_[(size_t)i] = std::sin(2.f * kPi * (float)i / 1024.f);
    prepare(48000.0, 1024);
}

float Engine::frand() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(rng_ & 0xFFFFFF) / 16777216.f;
}

void Engine::prepare(double sr, int maxBlock) {
    sr_ = sr > 1000.0 ? sr : 48000.0;
    maxBlock_ = std::max(maxBlock, 32);
    tmpL_.assign((size_t)maxBlock_, 0.f);
    tmpR_.assign((size_t)maxBlock_, 0.f);
    mono_.assign((size_t)maxBlock_, 0.f);
    gbuf_.assign((size_t)1 << 17, 0.f);
    for (auto& d : dl_) d.assign(4096, 0.f);
    // COMB : une période à 20 Hz au plus (puissance de deux pour le masque circulaire)
    combLen_ = 64;
    while (combLen_ < (int)(sr_ / 20.0) + 8) combLen_ <<= 1;
    combPool_.assign((size_t)(kVoices + kGhosts) * 2 * (size_t)combLen_, 0.f);
    fx_.prepare(sr_, maxBlock_);
    master_.prepare(sr_, maxBlock_);
    m_.meters.prepare(sr_);
    m_.readouts.sampleRate = sr_;
    dcR_ = std::exp(-2.0 * 3.14159265358979 * 5.0 / sr_);
    dcK_ = (float)(1.0 - std::exp(-2.0 * 3.14159265358979 * 3.0 / sr_));
    fadeLen_ = std::max(16, (int)(0.005 * sr_));
    stealLen_ = std::max(16, (int)(0.003 * sr_));
    reset();
}

// Réglages du rack lus dans les paramètres ; la modulation rapide agit sur le premier réglage.
void Engine::updateFxSettings() {
    const ParamStore& P = m_.params;
    // valeur 0..1 de chaque source (mêmes lectures que l'interface)
    const float mv[kNumSources] = {std::min(1.f, liveSm_ * 2.f), 0.5f + 0.5f * srcNow_[1], (srcNow_[2] + 1.f) * 0.5f, std::min(1.f, srcNow_[3] / 0.6f),
                                   std::clamp((srcNow_[4] + 1.f) * 0.5f, 0.f, 1.f), std::clamp((srcNow_[5] + 1.f) * 0.5f, 0.f, 1.f)};
    for (int f = 0; f < kNumFx; ++f) {
        fxs_.on[f] = P.get(fxParam(f, kFxOn)) > 0.5f;
        const float depth = P.get(fxParam(f, kFxModDepth)) / 100.f;
        const int src = std::clamp(P.geti(fxParam(f, kFxModSrc)), 0, kNumSources - 1);
        fxs_.p[f][0] = std::clamp(pv(fxParam(f, kFxP0)) + depth * mv[src], 0.f, 1.f);
        fxs_.p[f][1] = pv(fxParam(f, kFxP1));
        fxs_.p[f][2] = pv(fxParam(f, kFxP2));
        fxs_.mix[f] = pv(fxParam(f, kFxMix));
        fxs_.order[f] = std::clamp(m_.fxOrder[(size_t)f].load(std::memory_order_relaxed), 0, kNumFx - 1);
    }
    fxs_.distMode = P.geti(kFxDistMode);
    const int os = P.geti(kFxOversample);
    fxs_.oversample = os <= 0 ? 1 : (os == 1 ? 2 : 4);
    fxs_.eqFreq[0] = pv(kFxEqF0);
    fxs_.eqFreq[1] = pv(kFxEqF1);
    fxs_.eqFreq[2] = pv(kFxEqF2);
    fxs_.wet = pv(kFxWet);
}

void Engine::reset() {
    for (auto& v : voices_) v = Voice{};
    for (auto& v : ghosts_) v = Voice{};
    modOff_.fill(0.f);
    nRouted_ = 0;
    for (auto& g : grains_) g.on = false;
    std::fill(gbuf_.begin(), gbuf_.end(), 0.f);
    for (auto& d : dl_) std::fill(d.begin(), d.end(), 0.f);
    master_.reset();
    fx_.reset();
    ctlLeft_ = 0;
    ctlAge_ = ctlDebt_ = 0;
    pulse_ = 0.f;
    for (auto& l : lfo_) l = Lfo{};
    snapMaster_ = true;  // le master repart de sa valeur actuelle, sans rampe depuis l'ancien preset
    smSnap_ = true;
    dcX_[0] = dcX_[1] = dcY_[0] = dcY_[1] = 0.0;
    outGain_ = 0.f;  // fondu d'entrée de 5 ms
    panicFade_ = -1;
    nMono_ = nPend_ = nPool_ = 0;
    keyDown_.fill(0);
    chordN_.fill(0);
    keysHeld_ = 0;
    arpNote_ = -1;
    arpPos_ = 0;
    arpIdx_ = 0;
    arpNext_ = -1.0;
    arpWasOn_ = false;
    lastPitch_ = -1.f;
    chBend_.fill(0.f);
    chPress_.fill(0.f);
    chTimbre_.fill(0.f);
    curChan_ = 0;
    curNoteId_ = -1;
    nVr_ = 0;
    newest_ = nullptr;
}

float Engine::smoothed(SmoothId k, float x, float a) {
    if (smSnap_) return sm_[k] = x;
    return sm_[k] += (x - sm_[k]) * a;
}

float Engine::EnvGen::step(float dt, const EnvShape& s) {
    if (stage < 0) return lv = 0.f;
    t += dt;
    if (stage == 0) {
        if (t < s.ta) return lv = from + (1.f - from) * std::pow(t / s.ta, s.invq);
        t -= s.ta;
        stage = 1;
    }
    if (stage == 1) {
        if (t < s.td) return lv = s.sus + (1.f - s.sus) * std::pow(1.f - t / s.td, s.q);
        stage = 2;
    }
    if (stage == 2) return lv = s.sus;
    if (t < s.tr) return lv = from * std::pow(1.f - t / s.tr, s.q);
    stage = -1;
    return lv = 0.f;
}

Engine::EnvShape Engine::envShape(int osc) const {
    const ParamStore& P = m_.params;
    if (osc >= 2) {  // ENV 3 / ENV 4 (modulation)
        const uint32_t b = osc == 2 ? (uint32_t)kEnv3Attack : (uint32_t)kEnv4Attack;
        EnvShape e;
        e.ta = attackSeconds(pv(b));
        e.td = decaySeconds(pv(b + 1));
        e.sus = pv(b + 2);
        e.tr = releaseSeconds(pv(b + 3));
        e.q = envCurveQ(pv(b + 4));
        e.invq = 1.f / e.q;
        return e;
    }
    // LIER A<->B : l'enveloppe B suit l'enveloppe A
    const bool b = osc == 1 && P.get(kEnvLink) < 0.5f;
    EnvShape e;
    const uint32_t ia = b ? (uint32_t)kEnvBAttack : (uint32_t)kAttack, id = b ? (uint32_t)kEnvBDecay : (uint32_t)kDecay;
    const uint32_t ir = b ? (uint32_t)kEnvBRelease : (uint32_t)kRelease, is = b ? (uint32_t)kEnvBSustain : (uint32_t)kSustain;
    const uint32_t ic = b ? (uint32_t)kEnvBCurve : (uint32_t)kEnvACurve;
    e.ta = attackSeconds(pv(ia));
    e.td = decaySeconds(pv(id));
    e.tr = releaseSeconds(pv(ir));
    e.sus = pv(is);
    e.q = envCurveQ(pv(ic));
    e.invq = 1.f / e.q;
    return e;
}

// LFO (page ENV / LFO et rack modulaire) : forme ou dessin, vitesse libre ou calée sur le tempo,
// phase, fade in, lissage. Un LFO du rack retiré vaut 0.
float Engine::lfoValue(int n, float dt) {
    const ParamStore& P = m_.params;
    Lfo& L = lfo_[n];
    if (n >= 2 && P.get(lfoP(n, kLfOn)) < 0.5f) {
        L.sm = 0.f;
        return 0.f;
    }
    const int sync = P.geti(lfoP(n, kLfSync)), shape = P.geti(lfoP(n, kLfShape));
    const float hz = sync > 0 ? lfoSyncFactor(sync) * m_.hostBpm.load(std::memory_order_relaxed) / 120.f : lfoRateHz(pv(lfoP(n, kLfRate)));
    L.ph += (double)dt * hz;
    L.ph -= std::floor(L.ph);
    L.age += dt;
    const double ph = L.ph + (double)pv(lfoP(n, kLfPhase));
    const float f = (float)(ph - std::floor(ph));
    float y;
    if (P.get(lfoP(n, kLfDraw)) > 0.5f) {  // ÉDITION / DRAW : forme dessinée (table d'un cycle)
        const LfoShapes& sh = m_.lfoShapes;
        const float q = f * (float)kLfoTab;
        const int i0 = std::clamp((int)q, 0, kLfoTab - 1);
        const float y0 = sh.tableAt(n, i0), y1 = sh.tableAt(n, i0 + 1);
        y = y0 + (y1 - y0) * (q - (float)i0);
    } else {
        switch (shape) {
            case kLfoSine: y = std::sin(2.f * kPi * f); break;
            case kLfoTri: y = f < 0.5f ? 4.f * f - 1.f : 3.f - 4.f * f; break;
            case kLfoSaw: y = 2.f * f - 1.f; break;
            case kLfoSqr: y = f < 0.5f ? 1.f : -1.f; break;
            default: {  // S&H : une nouvelle valeur à chaque quart de cycle
                const int step = (int)(f * 4.f);
                if (step != L.shStep) { L.shStep = step; L.sh = frand() * 2.f - 1.f; }
                y = L.sh;
            }
        }
    }
    const float fade = lfoFadeSeconds(pv(lfoP(n, kLfFade)));
    if (fade > 0.001f) y *= std::min(1.f, L.age / fade);
    // LISSAGE : moyenne glissante d'environ 0.12 cycle au maximum (comme la courbe de la maquette)
    const float sm = pv(lfoP(n, kLfSmooth));
    const float tau = sm * 0.12f / std::max(0.05f, hz);
    L.sm = tau < 1e-4f ? y : L.sm + (y - L.sm) * (1.f - std::exp(-dt / tau));
    return L.sm;
}

// Valeur d'un paramètre avec ses routes libres (décalage en course normalisée du potard).
float Engine::pv(uint32_t id) const {
    const float off = modOff_[id];
    const float v = m_.params.get(id);
    if (off == 0.f) return v;
    return fromNormalized(id, toNormalized(id, v) + off);
}

// Réglage calculé voix par voix (destination d'une route par note), ou -1.
int Engine::voiceDst(uint32_t id) {
    switch (id) {
        case kAPos: return kVdAPos;
        case kBPos: return kVdBPos;
        case kALvl: return kVdALvl;
        case kBLvl: return kVdBLvl;
        case kAPan: return kVdAPan;
        case kBPan: return kVdBPan;
        case kAWarp: return kVdAWarp;
        case kBWarp: return kVdBWarp;
        case kCutoff: return kVdCut;
        case kReso: return kVdReso;
        case kFiltDrive: return kVdDrive;
        case kFiltMorph: return kVdMorph;
        case kFiltMix: return kVdMix;
        case kNoiseLvl: return kVdNoise;
        case kSubLvl: return kVdSub;
        case kBlend: return kVdBlend;
        default: return -1;
    }
}

// Valeur d'une source de route : LFO -1..1, les autres 0..1. Sources par note : celles de la
// voix v (sinon de la note la plus récente).
float Engine::srcValue(int s, const Voice* v) const {
    const ParamStore& P = m_.params;
    if (!v) v = newest_;
    if (s >= 0 && s < kNumLfos) return lfoNow_[s];
    if (s >= kRouteSrcMacro && s < kRouteSrcVel) return P.get(kMacro1 + (uint32_t)(s - kRouteSrcMacro));
    switch (s) {
        case kRouteSrcVel: return v ? v->vel : lastVel_;
        case kRouteSrcWheel: return P.get(kModWheel);
        case kRouteSrcPress: return std::max(P.get(kAftertouch), v ? std::max(v->press, v->polyPress) : 0.f);
        case kRouteSrcTimbre: return std::max(P.get(kTimbre), v ? v->timbre : 0.f);
        case kRouteSrcEnv3: return v ? v->e3 : 0.f;
        case kRouteSrcEnv4: return v ? v->e4 : 0.f;
        case kRouteSrcKey: return v ? std::clamp((float)v->note / 127.f, 0.f, 1.f) : 0.5f;
        case kRouteSrcRnd: return v ? v->rnd : 0.f;
        default: return 0.f;
    }
}

// Routes libres « source → potard » : recalculées à chaque bloc de contrôle. Une route dont la
// source (ou la source qui la dose) change d'une note à l'autre, vers un réglage calculé par voix,
// est appliquée voix par voix (liste vr_) ; les autres décalent le réglage pour toutes les voix.
void Engine::applyRoutes() {
    for (int i = 0; i < nRouted_; ++i) modOff_[routed_[(size_t)i]] = 0.f;
    nRouted_ = 0;
    nVr_ = 0;
    const FreeRoutes& R = m_.routes;
    for (int i = 0; i < kMaxFreeRoutes; ++i) {
        const int src = R.r[(size_t)i].src.load(std::memory_order_acquire);
        if (src < 0) break;
        const int id = R.r[(size_t)i].id.load(std::memory_order_relaxed);
        if (src >= kNumRouteSrcs || id < 0 || (uint32_t)id >= kNumParams) continue;
        const float amt = (float)R.r[(size_t)i].amt.load(std::memory_order_relaxed) / 100.f;
        const int curve = R.r[(size_t)i].curve.load(std::memory_order_relaxed);
        int aux = R.r[(size_t)i].aux.load(std::memory_order_relaxed);
        if (aux >= kNumRouteSrcs) aux = -1;
        const int vd = voiceDst((uint32_t)id);
        if (vd >= 0 && (routeSrcPerNote(src) || (aux >= 0 && routeSrcPerNote(aux)))) {
            vr_[(size_t)nVr_++] = VRoute{src, aux, curve, vd, amt};
            continue;
        }
        float val = routeCurve(srcValue(src, nullptr), curve);
        if (aux >= 0) {
            const float a = srcValue(aux, nullptr);
            val *= routeSrcBipolar(aux) ? 0.5f + 0.5f * a : a;
        }
        if (modOff_[(size_t)id] == 0.f && nRouted_ < kMaxFreeRoutes) routed_[(size_t)nRouted_++] = (uint32_t)id;
        modOff_[(size_t)id] += amt * val;
    }
}

// Expression (MPE, aftertouch polyphonique, Note Expression VST3) : appliquée aux voix concernées.
void Engine::expression(const NoteEvent& e) {
    const int ch = std::clamp((int)e.channel, 0, 15);
    const float mpeRange = m_.params.get(kMpeRange);
    auto each = [&](auto&& match, auto&& apply) {
        for (auto& v : voices_)
            if (v.active && match(v)) apply(v);
    };
    switch (e.kind) {
        case kEvChanPress:
            chPress_[(size_t)ch] = std::clamp(e.value, 0.f, 1.f);
            each([&](const Voice& v) { return v.chan == ch; }, [&](Voice& v) { v.press = chPress_[(size_t)ch]; });
            break;
        case kEvChanBend:
            chBend_[(size_t)ch] = std::clamp(e.value, -1.f, 1.f) * mpeRange;
            each([&](const Voice& v) { return v.chan == ch; }, [&](Voice& v) { v.bendSt = chBend_[(size_t)ch]; });
            break;
        case kEvChanTimbre:
            chTimbre_[(size_t)ch] = std::clamp(e.value, 0.f, 1.f);
            each([&](const Voice& v) { return v.chan == ch; }, [&](Voice& v) { v.timbre = chTimbre_[(size_t)ch]; });
            break;
        case kEvPolyPress:
            each([&](const Voice& v) { return e.noteId >= 0 ? v.noteId == e.noteId : v.note == e.note; },
                 [&](Voice& v) { v.polyPress = std::clamp(e.value, 0.f, 1.f); });
            break;
        case kEvNoteTune:
            each([&](const Voice& v) { return e.noteId >= 0 && v.noteId == e.noteId; }, [&](Voice& v) { v.bendSt = std::clamp(e.value, -96.f, 96.f); });
            break;
        case kEvNoteTimbre:
            each([&](const Voice& v) { return e.noteId >= 0 && v.noteId == e.noteId; }, [&](Voice& v) { v.timbre = std::clamp(e.value, 0.f, 1.f); });
            break;
        default: break;
    }
}

// Une voix d'une note (rang u d'un unison de U voix) ; glideFrom : hauteur de départ du glide
// (demi-tons), ou -1 sans glide.
void Engine::startVoice(int note, float vel, int u, int U, float glideFrom) {
    Voice* v = nullptr;
    for (auto& x : voices_)  // même note (même rang d'unison) : on relance la voix
        if (x.active && x.note == note && x.uni == u) { v = &x; break; }
    if (!v)
        for (auto& x : voices_)
            if (!x.active) { v = &x; break; }
    if (!v) {  // vol : la voix relâchée la plus ancienne, sinon la plus ancienne
        for (auto& x : voices_)
            if (x.released && (!v || x.order < v->order)) v = &x;
        if (!v)
            for (auto& x : voices_)
                if (!v || x.order < v->order) v = &x;
    }
    // voix volée (autre note) : elle s'éteint en 3 ms dans une voix « fantôme », la nouvelle part de
    // zéro ; même chose pour une note rejouée qui lit un instrument ou un son déposé (sinon le son
    // continuerait là où il en était au lieu de repartir du début : la note « s'allongeait »)
    if (v->active && (v->note != note || playsSamples(*v))) toGhost(*v);
    const bool wasActive = v->active;
    // une note est-elle déjà tenue ? (sinon : départ « frais », les LFO repartent de leur phase)
    const Voice* held = nullptr;
    for (const auto& x : voices_)
        if (x.active && !x.released && &x != v && (!held || x.order > held->order)) held = &x;
    v->active = true;
    v->released = false;
    v->note = note;
    v->vel = std::clamp(vel, 0.f, 1.f);
    v->order = ++order_;
    // unison : désaccord et panoramique symétriques autour de la note
    {
        const ParamStore& P = m_.params;
        const float sp = U > 1 ? 2.f * (float)u / (float)(U - 1) - 1.f : 0.f;
        v->uni = u;
        v->uniN = U;
        v->uniDet = sp * 0.5f * P.get(kUniDetune) * uniRangeSemis(P.geti(kUniRange));  // écart total : DÉSACCORD x ÉCART MAX
        v->uniPan = sp * P.get(kUniWidth);
        v->glide = glideFrom >= 0.f ? glideFrom - (float)note : 0.f;
        v->punchT = 0.f;
        // expression : la note reprend l'état de son canal (MPE) ; aléa propre à la note
        const int ch = std::clamp(curChan_, 0, 15);
        const bool mpe = P.get(kMpe) > 0.5f;
        v->chan = ch;
        v->noteId = curNoteId_;
        v->bendSt = mpe ? chBend_[(size_t)ch] : 0.f;
        v->press = mpe ? chPress_[(size_t)ch] : 0.f;
        v->timbre = mpe ? chTimbre_[(size_t)ch] : 0.f;
        v->polyPress = 0.f;
        if (u == 0) {  // les copies d'unison d'une note partagent son aléa (tiré à part : le reste du hasard ne change pas)
            uint32_t hsh = (uint32_t)(order_ * 2654435761u) ^ 0x5bd1e995u;
            hsh ^= hsh >> 15;
            hsh *= 0x2c1b3c6du;
            hsh ^= hsh >> 12;
            noteRnd_ = (float)(hsh & 0xFFFFFF) / 16777216.f;
        }
        v->rnd = noteRnd_;
    }
    // RETRIG : l'enveloppe repart (du niveau courant si la voix sonnait, sans clic) ;
    // FREE : jouée en legato, la nouvelle note reprend l'enveloppe de la note tenue
    const bool freeA = m_.params.get(kEnvAMode) > 0.5f, freeB = m_.params.get(kEnvBMode) > 0.5f;
    if (!wasActive) {
        v->eA = v->eB = v->deA = v->deB = 0.f;
    } else {  // la voix sonnait : l'enveloppe repart du niveau réellement entendu
        v->envA.lv = v->eA;
        v->envB.lv = v->eB;
    }
    v->envCnt = 0;
    v->envSpan = 0;
    if (freeA && held) v->envA = held->envA;
    else v->envA.start(wasActive);
    if (freeB && held) v->envB = held->envB;
    else v->envB.start(wasActive);
    v->env3.start(wasActive);
    v->env4.start(wasActive);
    // legato FREE sur une voix muette : elle rejoint le niveau de la note tenue en 2 ms, sans clic
    if (!wasActive && held && (freeA || freeB)) v->envSpan = std::max(8, (int)(0.002 * sr_));
    if (!held) {
        for (int k = 0; k < kNumLfos; ++k) {
            lfo_[k].ph = 0.0;  // déclenchement au clavier : la PHASE et le FADE IN repartent
            lfo_[k].age = 0.f;
            lfo_[k].shStep = -1;
        }
    }
    if (!wasActive) {
        v->env = 0.f;
        // mode de phase : SYNC 0° / 90° / 180° (départ fixe), LOOP (départ à 0), FREE (départ libre)
        // (unison : chaque voix part d'une phase au hasard, sinon les copies s'annulent ou tapent ensemble)
        auto start = [&](int mode) { return U > 1 ? (double)frand() : (mode == 1 ? 0.25 : (mode == 2 ? 0.5 : (mode == 4 ? (double)frand() : 0.0))); };
        v->phA = start(m_.params.geti(kAMode));
        v->phB = start(m_.params.geti(kBMode));
        v->phS = 0.0;
        v->ic1 = v->ic2 = v->ic1r = v->ic2r = 0.f;
        v->ic3 = v->ic4 = v->ic3r = v->ic4r = 0.f;
        v->fresh = true;
        v->dcInit = true;
        v->adX[0] = v->adX[1] = 0.0;
        v->lin1[0] = v->lin1[1] = 0.f;
        // filtre multimode, bruit, warps croisés : départ propre
        for (auto& side : v->fs) std::fill(side, side + 8, 0.f);
        v->cLp[0] = v->cLp[1] = 0.f;
        v->cw = 0;
        if (filtType_ == kFtCombP || filtType_ == kFtCombN) {
            float* cb = combOf((int)(v - voices_.data()));
            std::fill(cb, cb + 2 * (size_t)combLen_, 0.f);
        }
        v->lastA = v->lastB = 0.f;
        v->nz[0] = 0x9E3779B9u ^ (uint32_t)(order_ * 2654435761u);
        v->nz[1] = 0x85EBCA6Bu ^ (uint32_t)(order_ * 40503u + 17u);
        if (!v->nz[0]) v->nz[0] = 1u;
        if (!v->nz[1]) v->nz[1] = 2u;
        for (auto& p3 : v->pk) std::fill(p3, p3 + 3, 0.f);
        v->nzF[0] = v->nzF[1] = 0.f;
        v->crk = v->nzHold = 0.f;
        v->nzPh = 0.0;
        v->e3 = v->e4 = 0.f;
        // dérive analogique : ±3 cents par oscillateur, ±4 % de coupure, tirés à chaque note
        v->driftA = (frand() - 0.5f) * 0.06f;
        v->driftB = (frand() - 0.5f) * 0.06f;
        v->driftCut = (frand() - 0.5f) * 0.11f;
        // lecture SAMPLE : la version et la position partent de la note jouée
        const ParamStore& P = m_.params;
        const float bend = P.get(kBendRange) * P.get(kPitchBend) + v->bendSt;
        startSample(*v, 0, (float)note + 12.f * P.get(kAOct) + P.get(kASt) + bend);
        startSample(*v, 1, (float)note + 12.f * P.get(kBOct) + P.get(kBSt) + bend);
    }
    forceControl();  // recalcul immédiat des modulations
}

void Engine::toGhost(Voice& v) {
    int gi = 0;
    for (int k = 1; k < kGhosts; ++k)
        if (ghosts_[(size_t)k].ghost < ghosts_[(size_t)gi].ghost) gi = k;
    const int vi = (int)(&v - voices_.data());
    ghosts_[(size_t)gi] = v;
    ghosts_[(size_t)gi].ghost = stealLen_;
    if (filtType_ == kFtCombP || filtType_ == kFtCombN)  // le fantôme garde la résonance du COMB
        std::copy(combOf(vi), combOf(vi) + 2 * (size_t)combLen_, combOf(kVoices + gi));
    ghostTargets_[(size_t)gi] = voiceTargets_[(size_t)vi];
    v.active = false;
}

void Engine::dropStaleSounds(Voice& v) const {
    if (v.zoneA && (!inst_[0] || inst_[0]->serial != v.instA)) { v.zoneA = nullptr; v.lostA = true; }
    if (v.zoneB && (!inst_[1] || inst_[1]->serial != v.instB)) { v.zoneB = nullptr; v.lostB = true; }
    if (v.smpDataA && (!smpSet_[0] || smpSet_[0]->serial != v.smpSetA)) v.smpDataA = nullptr;
    if (v.smpDataB && (!smpSet_[1] || smpSet_[1]->serial != v.smpSetB)) v.smpDataB = nullptr;
}

// SAMPLE : choisit la version STRETCH la plus proche (sinon la transposition se fait en RESAMPLE)
void Engine::startSample(Voice& v, int osc, float semis) {
    const ParamStore& P = m_.params;
    // instrument multi-échantillonné (prioritaire) : la zone de la note et de la vélocité
    const Instrument* I = inst_[osc];
    const bool useInst = I && P.get(osc ? kBPlay : kAPlay) > 0.5f;
    (osc ? v.instB : v.instA) = useInst ? I->serial : 0;
    (osc ? v.lostB : v.lostA) = false;
    (osc ? v.zoneB : v.zoneA) = useInst ? I->find((int)std::lround(semis), std::clamp((int)std::lround(v.vel * 127.f), 1, 127)) : nullptr;
    if (useInst) {
        const InstZone* z = osc ? v.zoneB : v.zoneA;
        (osc ? v.inPosB : v.inPosA) = z ? (double)z->offset : 0.0;
        (osc ? v.smpB : v.smpA) = false;
        return;
    }
    const SampleSet* S = smpSet_[osc];
    bool& on = osc ? v.smpB : v.smpA;
    on = S && P.get(osc ? kBPlay : kAPlay) > 0.5f;
    (osc ? v.smpSetB : v.smpSetA) = S ? S->serial : 0;
    if (!on) return;
    const float d = semis - P.get(osc ? kBSmpRoot : kASmpRoot);
    int dv = 0, len = 0;
    const float* data = nullptr;
    if (P.get(osc ? kBSmpMode : kASmpMode) < 0.5f) {  // STRETCH : durée conservée
        dv = (int)std::lround(d);
        data = S->version(dv, len);
    }
    if (!data) { dv = 0; data = S->version(0, len); }  // RESAMPLE (ou version pas encore prête)
    (osc ? v.smpDataB : v.smpDataA) = data;
    (osc ? v.smpLenB : v.smpLenA) = len;
    (osc ? v.smpDB : v.smpDA) = dv;
    (osc ? v.smpPosB : v.smpPosA) = 0.0;
}

// Couche des voix. POLY : U voices par note (unison), glide depuis la dernière note jouée.
// MONO / LEGATO : une seule note sonne (la dernière tenue) ; une note jouée pendant qu'une autre
// sonne reprend les mêmes voix et glisse (MONO relance les enveloppes, LEGATO non).
void Engine::noteOn(int note, float vel) {
    const ParamStore& P = m_.params;
    const int mode = P.geti(kPolyMode), U = std::clamp(P.geti(kUnison), 1, 16);
    const bool glideOn = P.get(kGlide) > 0.001f, legatoOnly = P.get(kGlideMode) > 0.5f;
    lastVel_ = std::clamp(vel, 0.f, 1.f);
    bool held = false;
    for (const auto& x : voices_) held = held || (x.active && !x.released);
    if (mode == 0) {
        const float from = (glideOn && lastPitch_ >= 0.f && (!legatoOnly || held)) ? lastPitch_ : -1.f;
        for (int u = 0; u < U; ++u) startVoice(note, vel, u, U, from);
        lastPitch_ = (float)note;
        return;
    }
    // pile des notes tenues (la plus récente en haut)
    int k = 0;
    for (int i = 0; i < nMono_; ++i)
        if (monoStack_[(size_t)i] != note) monoStack_[(size_t)k++] = monoStack_[(size_t)i];
    nMono_ = std::min(k, (int)monoStack_.size() - 1);
    monoStack_[(size_t)nMono_++] = note;
    if (held) {
        retarget(note, vel, mode == 1);
    } else {
        const float from = (glideOn && lastPitch_ >= 0.f && !legatoOnly) ? lastPitch_ : -1.f;
        for (int u = 0; u < U; ++u) startVoice(note, vel, u, U, from);
    }
    lastPitch_ = (float)note;
}

// MONO / LEGATO : les voix qui sonnent passent à une autre note (glide depuis la hauteur entendue)
void Engine::retarget(int note, float vel, bool retrigger) {
    const bool glideOn = m_.params.get(kGlide) > 0.001f;
    for (auto& v : voices_) {
        if (!v.active || v.released) continue;
        const float cur = (float)v.note + v.glide;
        v.note = note;
        v.glide = glideOn ? cur - (float)note : 0.f;
        v.vel = std::clamp(vel, 0.f, 1.f);
        v.order = ++order_;
        if (retrigger && playsSamples(v)) {
            // MONO sur un instrument / un son déposé : l'ancienne note s'éteint en 3 ms, la nouvelle
            // repart du début de l'échantillon (zone de la nouvelle note et de sa vélocité)
            const Voice keep = v;
            toGhost(v);
            v = keep;
            v.active = true;
            v.released = false;
            v.envA.lv = v.envB.lv = v.eA = v.eB = v.deA = v.deB = 0.f;
            v.envA.start(false);
            v.envB.start(false);
            v.env3.start(false);
            v.env4.start(false);
            v.envCnt = 0;
            v.envSpan = 0;
            v.punchT = 0.f;
            v.glide = 0.f;  // le son repart de sa vraie hauteur (pas de glissé d'échantillon)
            const ParamStore& P = m_.params;
            const float bend = P.get(kBendRange) * P.get(kPitchBend) + v.bendSt;
            startSample(v, 0, (float)note + 12.f * P.get(kAOct) + P.get(kASt) + bend);
            startSample(v, 1, (float)note + 12.f * P.get(kBOct) + P.get(kBSt) + bend);
            v.dcInit = true;
            v.fresh = true;
        } else if (retrigger) {
            v.envA.lv = v.eA;
            v.envB.lv = v.eB;
            v.envA.start(true);
            v.envB.start(true);
            v.env3.start(true);
            v.env4.start(true);
            v.envCnt = 0;
            v.punchT = 0.f;
        }
    }
    forceControl();
}

void Engine::noteOff(int note) {
    const int mode = m_.params.geti(kPolyMode);
    if (mode != 0 && nMono_ > 0) {
        const int top = monoStack_[(size_t)nMono_ - 1];
        int k = 0;
        for (int i = 0; i < nMono_; ++i)
            if (monoStack_[(size_t)i] != note) monoStack_[(size_t)k++] = monoStack_[(size_t)i];
        nMono_ = k;
        if (note == top && nMono_ > 0) {  // retour à la note encore tenue, sans relancer
            retarget(monoStack_[(size_t)nMono_ - 1], lastVel_, false);
            lastPitch_ = (float)monoStack_[(size_t)nMono_ - 1];
            return;
        }
        if (nMono_ > 0) return;
    }
    for (auto& v : voices_)
        if (v.active && !v.released && (v.note == note || mode != 0)) { v.released = true; v.envA.release(); v.envB.release(); v.env3.release(); v.env4.release(); }
}

// --- couche d'entrée : accords (strum) et arpégiateur, calés sur le tempo de l'hôte ---------
namespace {
const double kArpBeats[6] = {1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
} // namespace

double Engine::beatAt(int64_t t) const { return blockBeat_ + (double)(t - blockT0_) * beatPerSmp_; }

void Engine::schedule(int64_t t, int note, float vel, bool on) {
    if (nPend_ >= (int)pend_.size()) {  // file pleine : l'évènement part tout de suite
        if (on) noteOn(note, vel);
        else noteOff(note);
        return;
    }
    pend_[(size_t)nPend_++] = Pending{t, note, vel, on, curChan_};
}

void Engine::inputOn(int note, float vel, int64_t t) {
    if (note < 0 || note > 127) return;
    const ParamStore& P = m_.params;
    const bool arp = P.get(kArpOn) > 0.5f, latch = P.get(kArpLatch) > 0.5f;
    if (arp && latch && keysHeld_ == 0) nPool_ = 0;  // LATCH : un nouvel accord remplace l'ancien
    if (!keyDown_[(size_t)note]) ++keysHeld_;
    keyDown_[(size_t)note] = 1;
    const int c = std::clamp(P.geti(kChord), 0, kNumChords - 1);
    const int64_t strum = (int64_t)(P.get(kChordStrum) * 0.08 * sr_);
    int n = 0;
    for (int i = 0; i < 6 && chordIntervals(c)[i] >= 0; ++i) {
        const int x = note + chordIntervals(c)[i];
        if (x > 127) break;
        chordOut_[(size_t)note][(size_t)n++] = (int8_t)x;
    }
    chordN_[(size_t)note] = (uint8_t)n;
    for (int i = 0; i < n; ++i) {
        const int x = chordOut_[(size_t)note][(size_t)i];
        if (arp) {
            bool dup = false;
            for (int k = 0; k < nPool_; ++k) dup = dup || pool_[(size_t)k] == x;
            if (!dup && nPool_ < (int)pool_.size()) { pool_[(size_t)nPool_] = x; poolVel_[(size_t)nPool_] = vel; ++nPool_; }
        } else if (i == 0 || strum == 0) {
            noteOn(x, vel);
        } else {
            schedule(t + strum * i, x, vel, true);
        }
    }
}

void Engine::inputOff(int note, int64_t t) {
    (void)t;
    if (note < 0 || note > 127 || !keyDown_[(size_t)note]) return;
    keyDown_[(size_t)note] = 0;
    keysHeld_ = std::max(0, keysHeld_ - 1);
    const ParamStore& P = m_.params;
    const bool arp = P.get(kArpOn) > 0.5f, latch = P.get(kArpLatch) > 0.5f;
    for (int i = 0; i < chordN_[(size_t)note]; ++i) {
        const int x = chordOut_[(size_t)note][(size_t)i];
        if (arp) {
            if (latch) continue;
            int k = 0;
            for (int j = 0; j < nPool_; ++j)
                if (pool_[(size_t)j] != x) { pool_[(size_t)k] = pool_[(size_t)j]; poolVel_[(size_t)k] = poolVel_[(size_t)j]; ++k; }
            nPool_ = k;
        } else {
            // strum pas encore joué : la note n'existe pas, on l'annule
            int k = 0;
            for (int j = 0; j < nPend_; ++j)
                if (!(pend_[(size_t)j].on && pend_[(size_t)j].note == x)) pend_[(size_t)k++] = pend_[(size_t)j];
            nPend_ = k;
            noteOff(x);
        }
    }
    chordN_[(size_t)note] = 0;
}

void Engine::arpAllOff() {
    if (arpNote_ >= 0) noteOff(arpNote_);
    arpNote_ = -1;
    int k = 0;  // les fins de notes de l'arpège en attente partent maintenant
    for (int j = 0; j < nPend_; ++j)
        if (pend_[(size_t)j].on) pend_[(size_t)k++] = pend_[(size_t)j];
    nPend_ = k;
}

void Engine::arpStep(int64_t t) {
    const ParamStore& P = m_.params;
    const double step = kArpBeats[std::clamp(P.geti(kArpRate), 0, 5)];
    ++arpIdx_;
    if (nPool_ == 0) return;
    const int oct = std::clamp(P.geti(kArpOct), 1, 4), mode = std::clamp(P.geti(kArpMode), 0, 4);
    // notes jouées dans l'ordre (JOUÉ) ou triées (les autres modes), répétées sur les octaves
    int base[64];
    const int nb = nPool_;
    for (int i = 0; i < nb; ++i) base[i] = pool_[(size_t)i];
    if (mode != 4) std::sort(base, base + nb);
    const int total = nb * oct;
    int idx;
    if (mode == 3) {
        idx = (int)(frand() * (float)total) % total;
    } else if (mode == 2 && total > 1) {  // HAUT/BAS sans répéter les extrémités
        const int period = 2 * total - 2;
        const int p = arpPos_ % period;
        idx = p < total ? p : period - p;
        arpPos_ = (arpPos_ + 1) % period;
    } else {
        idx = arpPos_ % total;
        arpPos_ = (arpPos_ + 1) % total;
        if (mode == 1) idx = total - 1 - idx;
    }
    const int note = std::min(127, base[idx % nb] + 12 * (idx / nb));
    float vel = 0.8f;
    for (int i = 0; i < nPool_; ++i)
        if (pool_[(size_t)i] == base[idx % nb]) vel = poolVel_[(size_t)i];
    if (arpNote_ >= 0) noteOff(arpNote_);
    curChan_ = 0;
    curNoteId_ = -1;
    noteOn(note, vel);
    arpNote_ = note;
    m_.readouts.arpStep.store((int)(arpIdx_ & 15), std::memory_order_relaxed);
    m_.readouts.arpNote.store(note, std::memory_order_relaxed);
    const double bps = std::max(1e-9, beatPerSmp_);
    const int64_t len = std::max<int64_t>(1, (int64_t)(std::clamp(P.get(kArpGate), 0.05f, 1.f) * step / bps) - 1);
    schedule(t + len, note, 0.f, false);
}

// prochain évènement interne (strum, fins de notes, pas d'arpège) dans le bloc, ou -1
int Engine::nextInternal(int64_t blockStart, int done, int numSamples) {
    int64_t best = blockStart + numSamples;
    for (int j = 0; j < nPend_; ++j) best = std::min(best, pend_[(size_t)j].t);
    if (m_.params.get(kArpOn) > 0.5f && nPool_ > 0 && beatPerSmp_ > 0.0) {
        const double step = kArpBeats[std::clamp(m_.params.geti(kArpRate), 0, 5)];
        const double sw = (arpIdx_ & 1) ? std::clamp(m_.params.get(kArpSwing), 0.f, 1.f) * 0.33 * step : 0.0;
        const double due = arpNext_ + sw;
        const int64_t ts = blockT0_ + (int64_t)std::ceil((due - blockBeat_) / beatPerSmp_ - 1e-9);
        best = std::min(best, ts);
    }
    if (best >= blockStart + numSamples) return -1;
    return (int)std::max<int64_t>(done, best - blockStart);
}

void Engine::fireInternal(int64_t t) {
    // évènements en attente échus (ordre : fins de notes d'abord)
    for (int pass = 0; pass < 2; ++pass) {
        int k = 0;
        for (int j = 0; j < nPend_; ++j) {
            const Pending& q = pend_[(size_t)j];
            if (q.t <= t && q.on == (pass == 1)) {
                if (q.on) {
                    curChan_ = q.chan;
                    curNoteId_ = -1;
                    noteOn(q.note, q.vel);
                } else {
                    noteOff(q.note);
                    if (q.note == arpNote_) arpNote_ = -1;
                }
            } else {
                pend_[(size_t)k++] = q;
            }
        }
        nPend_ = k;
    }
    const bool arp = m_.params.get(kArpOn) > 0.5f;
    if (!arp) {
        if (arpWasOn_) { arpAllOff(); nPool_ = 0; }
        arpWasOn_ = false;
        return;
    }
    if (!arpWasOn_) {  // arpège allumé : les touches tenues entrent dans la réserve
        arpWasOn_ = true;
        nPool_ = 0;
        for (int n = 0; n < 128; ++n)
            for (int i = 0; i < chordN_[(size_t)n] && keyDown_[(size_t)n]; ++i)
                if (nPool_ < (int)pool_.size()) { pool_[(size_t)nPool_] = chordOut_[(size_t)n][(size_t)i]; poolVel_[(size_t)nPool_] = 0.8f; ++nPool_; }
        for (auto& v : voices_)
            if (v.active && !v.released) { v.released = true; v.envA.release(); v.envB.release(); v.env3.release(); v.env4.release(); }
    }
    if (nPool_ == 0) {
        m_.readouts.arpStep.store(-1, std::memory_order_relaxed);
        if (arpNote_ >= 0) arpAllOff();
        arpPos_ = 0;
        arpIdx_ = 0;
        arpNext_ = -1.0;  // le prochain départ se cale sur la grille (ou tout de suite à l'arrêt)
        return;
    }
    const double step = kArpBeats[std::clamp(m_.params.geti(kArpRate), 0, 5)];
    const double b = beatAt(t);
    if (arpNext_ < 0.0) {  // premier pas : sur la grille de l'hôte en lecture, sinon immédiat
        arpNext_ = m_.hostPlaying.load(std::memory_order_relaxed) ? std::ceil(b / step - 1e-6) * step : b;
        arpIdx_ = (int64_t)std::llround(arpNext_ / step);
    }
    for (int guard = 0; guard < 8; ++guard) {
        const double sw = (arpIdx_ & 1) ? std::clamp(m_.params.get(kArpSwing), 0.f, 1.f) * 0.33 * step : 0.0;
        if (b + 1e-9 < arpNext_ + sw) break;
        arpNext_ += step;
        arpStep(t);
    }
}

void Engine::allOff(bool hard) {
    if (hard)
        for (auto& g : ghosts_) g.ghost = 0;
    for (auto& v : voices_) {
        if (hard) v.active = false;
        else if (v.active) { v.released = true; v.envA.release(); v.envB.release(); v.env3.release(); v.env4.release(); }
    }
}

void Engine::spawnGrain(float amount) {
    for (auto& g : grains_) {
        if (g.on) continue;
        static const float ratios[6] = {1.f, 1.f, 1.f, 2.f, 0.5f, 1.5f};
        g.inc = ratios[(int)(frand() * 6.f) % 6] * (1.f + (frand() - 0.5f) * 0.02f);
        g.len = (int)((0.03f + 0.07f * frand()) * (float)sr_);
        const double delay = (0.05 + 0.8 * frand()) * sr_ + (double)g.len * std::max(0.f, g.inc - 1.f) + 64.0;
        g.pos = (double)gw_ - delay;
        g.age = 0;
        const float pan = (frand() * 2.f - 1.f) * (0.4f + 0.6f * amount);
        g.gl = std::cos((pan + 1.f) * kPi / 4.f) * 0.9f;
        g.gr = std::sin((pan + 1.f) * kPi / 4.f) * 0.9f;
        g.on = true;
        return;
    }
}

void Engine::controlUpdate(int n) {
    ParamStore& P = m_.params;
    // un bloc interrompu (note, automation) a déjà fait avancer le temps des modulations
    int adv = n - ctlDebt_;
    if (adv < 0) { ctlDebt_ = -adv; adv = 0; }
    else ctlDebt_ = 0;
    const float dt = (float)adv / (float)sr_;
    const float smA = 1.f - std::exp(-dt / 0.012f);  // lissage des paramètres (12 ms)

    // --- vie ---------------------------------------------------------------------------------
    const float live = m_.life.soundLive();
    liveSm_ += (live - liveSm_) * std::min(1.f, dt / 0.08f);
    centroidSm_ += (m_.life.soundCentroid() - centroidSm_) * std::min(1.f, dt / 0.15f);
    const uint32_t gen = m_.life.generationCounter();
    const float hz = speedToHz(pv(kSpeed));
    const bool newGen = gen != lastGen_;
    if (newGen) {
        lastGen_ = gen;
        pulse_ = std::max(pulse_, m_.life.soundBirths());
    }
    pulse_ *= std::exp(-dt * hz / 0.35f);
    const float qty = pv(kQty);
    lifeMod_ = std::clamp(qty * (0.6f * (2.f * liveSm_ - 1.f) + 0.8f * pulse_), -1.f, 1.f);
    liveNow_ = liveSm_;

    // --- ENV 3 / ENV 4 de chaque voix, note la plus récente ---------------------------------
    {
        const EnvShape s3 = envShape(2), s4 = envShape(3);
        for (auto& v : voices_)
            if (v.active) {
                v.e3 = v.env3.step(dt, s3);
                v.e4 = v.env4.step(dt, s4);
            }
    }
    newest_ = nullptr;
    for (const auto& v : voices_)
        if (v.active && (!newest_ || v.order > newest_->order)) newest_ = &v;

    // --- sources globales -------------------------------------------------------------------
    // 8 LFO (les réglages routés utilisent les routes du bloc précédent), puis les routes libres
    for (int k = 0; k < kNumLfos; ++k) lfoNow_[k] = lfoValue(k, dt);
    applyRoutes();
    const float l1 = lfoNow_[0], l2 = lfoNow_[1];
    // LISSAGE MODULATION : passe-bas sur les sources globales (0 à 500 ms)
    const float tau = pv(kSmooth) * 0.5f;
    const float al = tau < 0.001f ? 1.f : 1.f - std::exp(-dt / tau);
    const float raw[kNumSources] = {(liveSm_ - 0.5f) * 2.f, l1, l2, 0.f, 0.f, m_.life.randomValue()};
    for (int k : {0, 1, 2, 5}) {
        srcSm_[k] += (raw[k] - srcSm_[k]) * al;
        srcNow_[(size_t)k] = srcSm_[k];
    }
    for (int d = 0; d < kNumDests; ++d) {
        float s = 0.f;
        for (int k : {0, 1, 2, 5}) {
            const size_t i = (size_t)ModMatrix::idx(k, d);
            if (m_.matrix.on[i].load(std::memory_order_relaxed))
                s += srcNow_[(size_t)k] * (float)m_.matrix.amt[i].load(std::memory_order_relaxed) / 100.f;
        }
        dstGlobal_[(size_t)d] = s;
    }
    auto voiceDst = [&](int d, float env, float key) {
        float s = dstGlobal_[(size_t)d];
        const size_t ie = (size_t)ModMatrix::idx(3, d), ik = (size_t)ModMatrix::idx(4, d);
        if (m_.matrix.on[ie].load(std::memory_order_relaxed)) s += env * (float)m_.matrix.amt[ie].load(std::memory_order_relaxed) / 100.f;
        if (m_.matrix.on[ik].load(std::memory_order_relaxed)) s += key * (float)m_.matrix.amt[ik].load(std::memory_order_relaxed) / 100.f;
        return s;
    };

    // voix la plus récente : sert aux affichages et aux destinations globales (GRAIN, MASTER)
    const Voice* newest = newest_;
    const float envN = newest ? newest->env : 0.f;
    const float keyN = newest ? std::clamp((float)(newest->note - 48) / 24.f, -1.f, 1.f) : 0.f;
    srcNow_[3] = envN;
    srcNow_[4] = keyN;

    // --- modules ------------------------------------------------------------------------------
    const float h = pv(kM1) / 100.f * (0.25f + 0.75f * liveSm_);  // HARMONIQUES
    space_ = pv(kM2) / 100.f;                                      // ESPACE
    const float modWheel = P.get(kModWheel);
    grainAmt_ = std::clamp(pv(kM3) / 100.f + voiceDst(6, envN, keyN) * 0.5f + modWheel * 0.4f, 0.f, 1.f);  // TEXTURE (+ molette MOD)
    masterTarget_ = smoothed(kSmMaster, levelToGain(pv(kMaster)), smA) * std::clamp(1.f + 0.3f * voiceDst(7, envN, keyN), 0.f, 2.f);
    if (snapMaster_) {
        masterGain_ = masterTarget_;
        snapMaster_ = false;
    }

    const int target = P.geti(kTarget);
    const float aPos = smoothed(kSmAPos, pv(kAPos), smA), bPos = smoothed(kSmBPos, pv(kBPos), smA);
    const float gA0 = smoothed(kSmALvl, levelToGain(pv(kALvl)), smA), gB0 = smoothed(kSmBLvl, levelToGain(pv(kBLvl)), smA);
    const float lvlMod = target == 2 ? std::clamp(1.f + 0.6f * lifeMod_, 0.f, 1.6f) : 1.f;
    const float bend = P.get(kBendRange) * P.get(kPitchBend);  // molette PITCH : +/- AMPLITUDE (2 demi-tons par défaut)
    const float semA = 12.f * P.get(kAOct) + P.get(kASt) + bend, semB = 12.f * P.get(kBOct) + P.get(kBSt) + bend;
    // warp (déformation de phase, + molette MOD), panoramique et mode de chaque oscillateur
    const float wA = std::clamp(smoothed(kSmAWarp, pv(kAWarp), smA) + modWheel * 0.3f, 0.f, 1.f);
    const float wB = std::clamp(smoothed(kSmBWarp, pv(kBWarp), smA) + modWheel * 0.3f, 0.f, 1.f);
    const int modeA = P.geti(kAMode), modeB = P.geti(kBMode);
    const int wmA = std::clamp(P.geti(kAWarpMode), 0, kNumWarpModes - 1), wmB = std::clamp(P.geti(kBWarpMode), 0, kNumWarpModes - 1);
    // largeur de bande ajoutée par le warp (choix du niveau de mipmap : pas de repliement)
    auto warpWidth = [](int wm, float w) {
        if (w <= 0.0005f) return 1.f;
        switch (wm) {
            case kWmClassic: return 1.f + 1.4f * w;
            case kWmSync: return 1.f + 7.f * w;
            case kWmBendP: case kWmBendN: return 1.f + 3.f * w;
            case kWmPwm: return 1.f / (1.f - 0.95f * w);
            case kWmAsym: return 0.5f / (0.5f - 0.45f * w);
            case kWmMirror: { const float p = 1.f - 0.5f * w; return std::min(16.f, 1.f / std::max(0.0625f, std::min(p, 1.f - p))); }
            case kWmFm: return 1.f + 12.f * w * w;
            default: return 1.f;
        }
    };
    auto panGains = [](float p, float& l, float& r) {
        l = std::cos(std::clamp(p, 0.f, 1.f) * kPi * 0.5f) * 1.41421f;
        r = std::sin(std::clamp(p, 0.f, 1.f) * kPi * 0.5f) * 1.41421f;
    };
    float pAL, pAR, pBL, pBR;
    panGains(smoothed(kSmAPan, pv(kAPan), smA), pAL, pAR);
    panGains(smoothed(kSmBPan, pv(kBPan), smA), pBL, pBR);
    const float cutBase = smoothed(kSmCutoff, pv(kCutoff), smA), reso = smoothed(kSmReso, pv(kReso), smA);
    const float drive = 1.f + 3.f * std::max(h, 0.f);
    // page JEU, page SYNTH (filtre multimode, bruit)
    slope24_ = P.get(kFiltSlope) > 0.5f;
    fdrive_ = pv(kFiltDrive);
    filtType_ = std::clamp(P.geti(kFiltType), 0, kNumFiltTypes - 1);
    const float fKey = pv(kFiltKey), fMorph = pv(kFiltMorph), fMix = pv(kFiltMix);
    nzType_ = std::clamp(P.geti(kNoiseType), 0, kNumNoiseTypes - 1);
    {
        const float c = pv(kNoiseColor);
        if (std::fabs(c - 0.5f) < 0.01f) nzMode_ = 2;
        else {
            nzMode_ = c < 0.5f ? 0 : 1;
            const float hzc = c < 0.5f ? 200.f * std::pow(100.f, c * 2.f) : 20.f * std::pow(400.f, (c - 0.5f) * 2.f);
            nzA_ = 1.f - std::exp(-2.f * kPi * std::min(hzc, (float)sr_ * 0.45f) / (float)sr_);
        }
    }
    const float noiseLvl = pv(kNoiseLvl), uniBlend = pv(kUniBlend);
    const float glideT = glideSeconds(pv(kGlide));
    const float glideK = glideT > 0.001f ? std::exp(-dt / (glideT * 0.3f)) : 0.f;
    const float punch = pv(kPunch) * 36.f, punchTau = punchSeconds(pv(kPunchTime));
    const float subLvl = pv(kSubLvl), subOct = P.get(kSubOct) > 0.5f ? 12.f : 0.f;
    const float velAmt = pv(kVelAmt), velCut = pv(kVelCut);
    const float blend = pv(kBlend);
    const float blA = std::min(1.f, 2.f * (1.f - blend)), blB = std::min(1.f, 2.f * blend);

    float posAShow = aPos, posBShow = bPos;
    for (size_t vi = 0; vi < voices_.size(); ++vi) {
        Voice& v = voices_[vi];
        if (!v.active) continue;
        const float key = std::clamp((float)(v.note - 48) / 24.f, -1.f, 1.f);
        auto D = [&](int d) { return voiceDst(d, v.env, key); };
        // routes par note (vélocité, pression, timbre, ENV 3 / 4, clavier, aléa) vers cette voix
        float vo[kNumVd] = {};
        for (int r = 0; r < nVr_; ++r) {
            const VRoute& q = vr_[(size_t)r];
            float val = routeCurve(srcValue(q.src, &v), q.curve);
            if (q.aux >= 0) {
                const float a = srcValue(q.aux, &v);
                val *= routeSrcBipolar(q.aux) ? 0.5f + 0.5f * a : a;
            }
            vo[q.vd] += q.amt * val;
        }
        const float pA = std::clamp(aPos + vo[kVdAPos] + D(2) * 0.35f + (target == 1 ? lifeMod_ * 0.25f : 0.f), 0.f, 1.f);
        const float pB = std::clamp(bPos + vo[kVdBPos] + D(3) * 0.35f + (target == 1 ? lifeMod_ * 0.375f : 0.f), 0.f, 1.f);
        // glide (rapproche la hauteur de la note), punch 808 (chute de hauteur à l'attaque), unison
        v.glide *= glideK;
        if (std::fabs(v.glide) < 0.002f) v.glide = 0.f;
        float pOff = 0.f;
        if (punch > 0.f) {
            pOff = punch * std::exp(-v.punchT / punchTau);
            v.punchT += dt;
        }
        const float common = (float)v.note + v.glide + v.uniDet + pOff + v.bendSt;
        const float pitchA = common + semA + D(0) * 6.f + v.driftA, pitchB = common + semB + D(1) * 6.f + v.driftB;
        // incréments bornés sous Nyquist (note très aiguë + octaves + transpositions, ou fréquence
        // d'échantillonnage basse) ; NaN -> 0 : la phase ne peut plus sortir de [0, 1)
        auto safeRate = [](double x) { return x > 0.0 && x < 64.0 ? (float)x : (x >= 64.0 ? 64.f : 0.f); };
        auto safeInc = [](double x) { return x > 0.0 && x < 0.49 ? (float)x : (x >= 0.49 ? 0.49f : 0.f); };
        v.incS = safeInc(noteHz(common + bend - subOct) / sr_);
        const double fA = noteHz(pitchA), fB = noteHz(pitchB);
        // FREE : la phase dérive lentement (+0.15 Hz), comme un oscillateur libre
        const float incA = safeInc((fA + (modeA == 4 ? 0.15 : 0.0)) / sr_), incB = safeInc((fB + (modeB == 4 ? 0.15 : 0.0)) / sr_);
        float oct = D(4) * 4.f + h * 2.f + fKey * (float)(v.note - 60) / 12.f + v.driftCut + velCut * 4.f * (v.vel - 1.f);
        if (target == 0) oct += lifeMod_ * 3.f;
        const float fc = std::clamp(cutoffToHz(std::clamp(cutBase + vo[kVdCut], 0.f, 1.f)) * std::pow(2.f, oct), 20.f, (float)sr_ * 0.45f);
        const float g = std::tan(kPi * fc / (float)sr_);
        const float rs = std::clamp(reso + D(5) * 0.5f + vo[kVdReso], 0.f, 1.f);
        const float k = 2.f - 1.94f * rs;
        // panoramique vivant : suit le centre de masse de la grille, dosé par ESPACE
        const float spread = ((vi & 1) ? 1.f : -1.f) * (0.3f + 0.7f * (float)(vi % 16) / 16.f);  // même étalement qu'avec 16 voix
        const float pan = std::clamp(space_ * (0.75f * centroidSm_ + 0.35f * spread) + v.uniPan, -1.f, 1.f);
        const float pl = std::cos((pan + 1.f) * kPi / 4.f) * 1.41421f, pr = std::sin((pan + 1.f) * kPi / 4.f) * 1.41421f;
        // unison : les voix du centre à plein, les autres dosées par BLEND, puissance totale constante
        {
            const int U = std::max(1, v.uniN);
            const int nC = U == 1 ? 1 : (U % 2 ? 1 : 2);
            const bool center = U == 1 || (U % 2 ? v.uni == (U - 1) / 2 : (v.uni == U / 2 - 1 || v.uni == U / 2));
            const float b = std::clamp(uniBlend, 0.f, 1.f);
            const float sum = (float)nC + (float)(U - nC) * b * b;
            v.uniG = (center ? 1.f : b) / std::sqrt(std::max(sum, 1e-6f));
        }
        const float amp = 0.32f * ((1.f - velAmt) + velAmt * v.vel) * v.uniG;
        float vblA = blA, vblB = blB;
        if (vo[kVdBlend] != 0.f) {
            const float bl = std::clamp(blend + vo[kVdBlend], 0.f, 1.f);
            vblA = std::min(1.f, 2.f * (1.f - bl));
            vblB = std::min(1.f, 2.f * bl);
        }
        const float gAv = vo[kVdALvl] != 0.f ? levelToGain(std::clamp(pv(kALvl) + vo[kVdALvl], 0.f, 1.f)) : gA0;
        const float gBv = vo[kVdBLvl] != 0.f ? levelToGain(std::clamp(pv(kBLvl) + vo[kVdBLvl], 0.f, 1.f)) : gB0;
        const float gA = gAv * lvlMod * amp * vblA, gB = gBv * lvlMod * amp * vblB;
        float vAL = pAL, vAR = pAR, vBL = pBL, vBR = pBR;
        if (vo[kVdAPan] != 0.f) panGains(smoothed(kSmAPan, pv(kAPan), 0.f) + vo[kVdAPan], vAL, vAR);
        if (vo[kVdBPan] != 0.f) panGains(smoothed(kSmBPan, pv(kBPan), 0.f) + vo[kVdBPan], vBL, vBR);
        const float wAv = std::clamp(wA + vo[kVdAWarp], 0.f, 1.f), wBv = std::clamp(wB + vo[kVdBWarp], 0.f, 1.f);
        // LOOP lit la table deux fois par cycle, le warp ajoute des harmoniques : niveau de mipmap adapté,
        // fondu vers le niveau suivant pour que le timbre suive la hauteur sans palier
        float mxA, mxB;
        const int lvA = MipTable::levelMix(incA * (modeA == 3 ? 2.f : 1.f) * warpWidth(wmA, wAv), mxA);
        const int lvB = MipTable::levelMix(incB * (modeB == 3 ? 2.f : 1.f) * warpWidth(wmB, wBv), mxB);
        VoiceTargets tg{pA, pB, g, k, pl, pr, gA * vAL, gA * vAR, gB * vBL, gB * vBR, wAv, wBv, drive};
        tg.mixA = mxA;
        tg.mixB = mxB;
        tg.sub = std::clamp(subLvl + vo[kVdSub], 0.f, 1.f) * amp * 1.4f;
        tg.fc = fc;
        tg.morph = std::clamp(fMorph + vo[kVdMorph], 0.f, 1.f);
        tg.fmix = std::clamp(fMix + vo[kVdMix], 0.f, 1.f);
        tg.fdrv = std::clamp(fdrive_ + vo[kVdDrive], 0.f, 1.f);
        tg.noise = std::clamp(noiseLvl + vo[kVdNoise], 0.f, 1.f) * amp * 1.6f;
        v.wmA = wmA;
        v.wmB = wmB;
        if (v.fresh) {
            v.posA = pA; v.posB = pB; v.g = g; v.k = k; v.panL = pl; v.panR = pr;
            v.aL = tg.aL; v.aR = tg.aR; v.bL = tg.bL; v.bR = tg.bR; v.wA = wAv; v.wB = wBv; v.drive = drive;
            v.mixA = mxA; v.mixB = mxB; v.sub = tg.sub; v.fmix = tg.fmix; v.noise = tg.noise;
            v.fresh = false;
        }
        if (lvA != v.levelA) v.mixA = mxA;  // changement de niveau : le dosage repart d'une valeur équivalente
        if (lvB != v.levelB) v.mixB = mxB;
        v.levelA = lvA;
        v.levelB = lvB;
        // SAMPLE : pas de lecture (taux du son / taux du moteur, transposition restante)
        if (v.zoneA) v.inRateA = safeRate(v.zoneA->sr / sr_ * std::pow(2.0, (pitchA - (float)v.zoneA->root + v.zoneA->tune * 0.01f) / 12.0));
        if (v.zoneB) v.inRateB = safeRate(v.zoneB->sr / sr_ * std::pow(2.0, (pitchB - (float)v.zoneB->root + v.zoneB->tune * 0.01f) / 12.0));
        if (v.smpA && smpSet_[0]) v.smpRateA = safeRate(smpSet_[0]->sr / sr_ * std::pow(2.0, (pitchA - P.get(kASmpRoot) - (float)v.smpDA) / 12.0));
        if (v.smpB && smpSet_[1]) v.smpRateB = safeRate(smpSet_[1]->sr / sr_ * std::pow(2.0, (pitchB - P.get(kBSmpRoot) - (float)v.smpDB) / 12.0));
        // cibles de fin de bloc (rampe linéaire dans renderBlock)
        v.incA = incA;
        v.incB = incB;
        v.modeA = modeA;
        v.modeB = modeB;
        voiceTargets_[vi] = tg;
        if (&v == newest) { posAShow = pA; posBShow = pB; }
    }

    smSnap_ = false;

    // --- lectures pour l'interface --------------------------------------------------------
    Readouts& R = m_.readouts;
    for (int s = 0; s < kNumSources; ++s) R.src[(size_t)s].store(srcNow_[(size_t)s], std::memory_order_relaxed);
    for (int d = 0; d < kNumDests; ++d) R.dst[(size_t)d].store(voiceDst(d, envN, keyN), std::memory_order_relaxed);
    // page ENV / LFO : point de lecture des enveloppes de la voix la plus récente et des LFO
    for (int k = 0; k < 4; ++k) {
        const EnvGen* e = newest ? (k == 0 ? &newest->envA : (k == 1 ? &newest->envB : (k == 2 ? &newest->env3 : &newest->env4))) : nullptr;
        R.envStage[(size_t)k].store(e ? e->stage : -1, std::memory_order_relaxed);
        R.envT[(size_t)k].store(e ? e->t : 0.f, std::memory_order_relaxed);
        R.envLv[(size_t)k].store(e ? e->lv : 0.f, std::memory_order_relaxed);
    }
    for (int k = 0; k < kNumLfos; ++k) {
        R.lfoPh[(size_t)k].store((float)lfo_[k].ph, std::memory_order_relaxed);
        R.lfoVal[(size_t)k].store(lfoNow_[k], std::memory_order_relaxed);
    }
    R.press.store(srcValue(kRouteSrcPress, newest), std::memory_order_relaxed);
    R.timbre.store(srcValue(kRouteSrcTimbre, newest), std::memory_order_relaxed);
    R.noteBend.store(newest ? newest->bendSt : 0.f, std::memory_order_relaxed);
    R.smpPos[0].store(newest && newest->smpA && newest->smpLenA > 0 ? std::clamp((float)(newest->smpPosA / (double)newest->smpLenA), 0.f, 1.f) : -1.f, std::memory_order_relaxed);
    R.smpPos[1].store(newest && newest->smpB && newest->smpLenB > 0 ? std::clamp((float)(newest->smpPosB / (double)newest->smpLenB), 0.f, 1.f) : -1.f, std::memory_order_relaxed);
    R.posA.store(posAShow, std::memory_order_relaxed);
    R.posB.store(posBShow, std::memory_order_relaxed);
    R.lifeMod.store(lifeMod_, std::memory_order_relaxed);

    // --- grains déclenchés par la grille ----------------------------------------------------
    if (grainAmt_ > 0.001f) {
        spawnAcc_ += dt * grainAmt_ * (6.f + 40.f * liveSm_);
        if (newGen) spawnAcc_ += m_.life.soundBirths() * 8.f * grainAmt_;
        while (spawnAcc_ >= 1.f) { spawnGrain(grainAmt_); spawnAcc_ -= 1.f; }
    } else {
        spawnAcc_ = 0.f;
    }
}

namespace {
// Primitive de fastTanh (Padé (3,2)), nulle en 0 : x²/18 + 4/3 ln((x² + 3) / 3) sur [-3, 3],
// puis |x| - 3 + F(3). Sert au drive anti-crénelé (ADAA d'ordre 1) des voix ; en double
// (différences de primitives).
inline double tanhPadeF(double x) {
    const double a = std::fabs(x);
    static const double F3 = 0.5 + 4.0 / 3.0 * std::log(4.0);
    if (a >= 3.0) return a - 3.0 + F3;
    return x * x / 18.0 + 4.0 / 3.0 * std::log(x * x / 3.0 + 1.0);
}
// (F(x) - F(x1)) / (x - x1) sans soustraire deux primitives : sur [-3, 3],
//   = (x + x1)/18 + 4/3 · (x + x1)/(x1² + 3) · log1p(q)/q,  q = (x² - x1²)/(x1² + 3),
// forme exacte et bien conditionnée (aucune annulation), donc en float et sans cas « dx petit ».
// Au-delà de ±3 du même côté, la forme vaut ±1 ; seul le passage de ±3 repasse en double.
inline float adaaTanh(float x, double& x1d) {
    const float x1 = (float)x1d, a = std::fabs(x), b = std::fabs(x1);
    float y;
    if (a < 3.f && b < 3.f) {
        const float s = x + x1, den = x1 * x1 + 3.f, q = (x - x1) * s / den;
        const float lq = std::fabs(q) < 1e-3f ? 1.f - q * (0.5f - q * (1.f / 3.f)) : std::log1p(q) / q;
        y = s * (1.f / 18.f) + (4.f / 3.f) * s / den * lq;
    } else if (a >= 3.f && b >= 3.f && (x > 0.f) == (x1 > 0.f)) {
        y = x > 0.f ? 1.f : -1.f;
    } else {
        const double F = tanhPadeF(x), F1 = tanhPadeF(x1), d = (double)x - (double)x1;
        y = std::fabs(d) > 1e-6 ? (float)((F - F1) / d) : fastTanh(0.5f * (x + x1));
    }
    x1d = x;
    return y;
}
// lecture cubique (stéréo) d'une zone d'instrument, boucle comprise ; après la fin d'un son
// sans boucle : silence. pos est ramenée dans la boucle ici.
inline void readInst(const InstZone& z, double& pos, float& l, float& r) {
    const int n = z.frames;
    if (!(pos >= -4.0)) pos = 0.0;  // NaN
    if (!z.loop && pos > (double)n + 4.0) {  // fin du son : silence, la position ne grandit plus
        pos = (double)n + 4.0;
        l = r = 0.f;
        return;
    }
    if (z.loop && pos >= (double)z.loopEnd) pos -= (double)(z.loopEnd - z.loopStart) * std::floor((pos - (double)z.loopStart) / (double)(z.loopEnd - z.loopStart));
    const int k = (int)pos;
    const float f = (float)(pos - (double)k);
    auto idx = [&](int i) {
        if (z.loop && i >= z.loopEnd) i -= z.loopEnd - z.loopStart;
        return i;
    };
    auto at = [&](int i, int c) {
        i = idx(i);
        return i >= 0 && i < n ? z.data[(size_t)i * (size_t)z.ch + (size_t)c] : 0.f;
    };
    for (int c = 0; c < 2; ++c) {
        const int cc = z.ch > 1 ? c : 0;
        const float y0 = at(k - 1, cc), y1 = at(k, cc), y2 = at(k + 1, cc), y3 = at(k + 2, cc);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        (c ? r : l) = (((c3 * f + c2) * f + c1) * f + y1) * z.gain;
    }
}
// lecture cubique d'un son (zéros hors du son)
inline float readSmp(const float* d, int n, double pos) {
    if (!(pos >= -4.0) || pos > (double)n + 4.0) return 0.f;  // NaN, avant ou après le son
    const int k = (int)pos;
    const float f = (float)(pos - (double)k);
    auto at = [&](int i) { return i >= 0 && i < n ? d[i] : 0.f; };
    const float y0 = at(k - 1), y1 = at(k), y2 = at(k + 1), y3 = at(k + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}
} // namespace

// Une voix sur n échantillons. Renvoie false quand la voix s'est éteinte (enveloppes au repos,
// ou fin du fondu d'une voix volée).
bool Engine::renderVoice(Voice& v, const VoiceTargets& t, float* L, float* R, float* mono, int n, int span, const EnvShape& esA, const EnvShape& esB,
                         float* comb) {
    const float inv = 1.f / (float)span;
    const bool full = n == span;
    constexpr int kEnvStep = 8;  // enveloppes calculées tous les 8 échantillons, interpolées entre
    const float envDt = (float)kEnvStep / (float)sr_;
    const MipTable* TA = tab_[0];
    const MipTable* TB = tab_[1];
    const float dPosA = (t.posA - v.posA) * inv, dPosB = (t.posB - v.posB) * inv, dG = (t.g - v.g) * inv, dK = (t.k - v.k) * inv;
    const float dPl = (t.panL - v.panL) * inv, dPr = (t.panR - v.panR) * inv;
    const float dAL = (t.aL - v.aL) * inv, dAR = (t.aR - v.aR) * inv, dBL = (t.bL - v.bL) * inv, dBR = (t.bR - v.bR) * inv;
    const float dWA = (t.wA - v.wA) * inv, dWB = (t.wB - v.wB) * inv, dDr = (t.drive - v.drive) * inv;
    const float dMA = (t.mixA - v.mixA) * inv, dMB = (t.mixB - v.mixB) * inv, dSub = (t.sub - v.sub) * inv;
    const float dFm = (t.fmix - v.fmix) * inv, dNz = (t.noise - v.noise) * inv;
    float posA = v.posA, posB = v.posB, g = v.g, k = v.k, pl = v.panL, pr = v.panR, dr = v.drive;
    float aL = v.aL, aR = v.aR, bL = v.bL, bR = v.bR, wA = v.wA, wB = v.wB, mA = v.mixA, mB = v.mixB;
    float fmix = v.fmix, nzg = v.noise;
    double phA = v.phA, phB = v.phB;
    float ic1 = v.ic1, ic2 = v.ic2, ic1r = v.ic1r, ic2r = v.ic2r;
    float ic3 = v.ic3, ic4 = v.ic4, ic3r = v.ic3r, ic4r = v.ic4r;
    float sub = v.sub;
    double phS = v.phS;
    const float incS = v.incS;
    const bool slope24 = slope24_;
    // drive du filtre : réglage du bloc (routes par note comprises)
    const float fd = t.fdrv, fdk = 1.f + 5.f * fd, fdInv = 1.f / fdk;
    float eA = v.eA, eB = v.eB, deA = v.deA, deB = v.deB;
    int envCnt = v.envCnt;
    float dcA = v.dcA, dcB = v.dcB;
    const float dcK = dcK_;
    const bool loopA = v.modeA == 3, loopB = v.modeB == 3;
    const int wmA = v.wmA, wmB = v.wmB;
    float lastA = v.lastA, lastB = v.lastB;
    // SAMPLE : le son de départ doit être encore celui de l'oscillateur (sinon il a pu être libéré)
    // (dropStaleSounds a déjà oublié les zones et sons dont l'instrument a changé)
    const bool sA = v.smpA && v.smpDataA;
    const bool iA = v.zoneA != nullptr;
    const bool iB = v.zoneB != nullptr;
    double ipA = v.inPosA, ipB = v.inPosB;
    const bool sB = v.smpB && v.smpDataB;
    double spA = v.smpPosA, spB = v.smpPosB;
    // voix volée : fondu linéaire jusqu'à zéro
    const float gStep = v.ghost > 0 ? 1.f / (float)stealLen_ : 0.f;
    float gg = v.ghost > 0 ? (float)v.ghost / (float)stealLen_ : 1.f;
    auto sinAt = [&](float u) {
        const float q = u * 1024.f;
        const int i0 = std::clamp((int)q, 0, 1023);
        return sin_[(size_t)i0] + (sin_[(size_t)(i0 + 1)] - sin_[(size_t)i0]) * (q - (float)i0);
    };
    auto wrap01 = [](float u) {
        u -= std::floor(u);
        return u >= 0.f && u < 1.f ? u : 0.f;  // NaN, 1 par arrondi
    };
    // déformation de la phase (modes de warp « de phase »), other : sortie de l'autre oscillateur
    auto warpPh = [&](float u, int wm, float w, float other) {
        switch (wm) {
            case kWmClassic: return wrap01(u + w * 0.22f * sinAt(u));
            case kWmSync: return wrap01(u * (1.f + 7.f * w));
            case kWmBendP: case kWmBendN: {
                const float x = 2.f * u - 1.f, e = wm == kWmBendP ? 1.f / (1.f + 4.f * w) : 1.f + 4.f * w;
                const float y = std::pow(std::fabs(x), e);
                return std::min(0.99999f, 0.5f * ((x < 0.f ? -y : y) + 1.f));
            }
            case kWmPwm: {
                const float d = 1.f - 0.95f * w;
                return u < d ? u / d : 0.f;
            }
            case kWmAsym: {
                const float kn = 0.5f - 0.45f * w;
                return u < kn ? 0.5f * u / kn : std::min(0.99999f, 0.5f + 0.5f * (u - kn) / (1.f - kn));
            }
            case kWmMirror: {  // aller jusqu'à p, puis retour : continu au bouclage
                const float p = 1.f - 0.5f * w;
                return u < p ? u / p : std::min(0.99999f, (1.f - u) / std::max(1e-4f, 1.f - p));
            }
            case kWmFm: return wrap01(u + w * w * 3.f * other);
            default: return u;
        }
    };
    // modes de warp « d'amplitude » (après la lecture de la table)
    auto warpAmp = [&](float s, float u, int wm, float w, float other) {
        switch (wm) {
            case kWmFlip: return u >= 1.f - w ? -s : s;
            case kWmQuant: {
                const float q = std::exp2(1.f + 9.f * (1.f - w));
                return std::round(s * q) / q;
            }
            case kWmAm: return s * (1.f + w * other) / (1.f + w);
            case kWmRm: return s + w * (s * other - s);
            default: return s;
        }
    };
    // phase lue : LOOP (deux passages par cycle) puis le warp de l'oscillateur
    auto readPhase = [&](double ph, bool loop, float w, int wm, float other) {
        float u = (float)ph;
        if (loop) { u *= 2.f; u -= std::floor(u); }
        if (!(u >= 0.f && u < 1.f)) u = u >= 1.f && u < 1e6f ? u - std::floor(u) : 0.f;  // NaN, hors cycle
        if (w > 0.0005f) u = warpPh(u, wm, w, other);
        return u;
    };
    // départ de note : composante continue mesurée sur un cycle (aucune transitoire du suivi)
    if (v.dcInit) {
        constexpr int kDcPts = 128;
        float sumA = 0.f, sumB = 0.f;
        for (int j = 0; j < kDcPts; ++j) {
            const double u = ((double)j + 0.5) / (double)kDcPts;
            const float uA = readPhase(u, loopA, wA, wmA, 0.f), uB = readPhase(u, loopB, wB, wmB, 0.f);
            sumA += warpAmp(TA->readMix(v.levelA, mA, posA, uA), uA, wmA, wA, 0.f);
            sumB += warpAmp(TB->readMix(v.levelB, mB, posB, uB), uB, wmB, wB, 0.f);
        }
        dcA = (v.smpA || v.zoneA) ? 0.f : sumA / (float)kDcPts;
        dcB = v.smpB ? 0.f : sumB / (float)kDcPts;
        v.dcInit = false;
    }
    // --- filtre multimode : réglages du bloc -------------------------------------------------
    const int ft = filtType_;
    const float morph = t.morph;
    // FORMANT : 3 bandes (voyelles A E I O U de basse, glissées par MORPH, décalées par CUTOFF)
    float fgF[3] = {0.f, 0.f, 0.f}, faF[3] = {0.f, 0.f, 0.f}, kF = 0.3f;
    // PHASE : 2 (12 dB) ou 4 (24 dB) passe-tout écartés autour de la coupure
    float gPh[4] = {0.f, 0.f, 0.f, 0.f};
    const int nPh = slope24 ? 4 : 2;
    // COMB : retard d'une période de la coupure, retour signé, amorti dans la boucle
    float cD = 2.f, cFb = 0.f, cDamp = 1.f;
    const int cMask = combLen_ - 1;
    const float rsB = std::clamp((2.f - t.k) / 1.94f, 0.f, 1.f);  // résonance 0..1 de la cible
    if (ft == kFtFormant) {
        static const float F[5][3] = {{600, 1040, 2250}, {400, 1620, 2400}, {250, 1750, 2600}, {400, 750, 2400}, {350, 600, 2400}};
        static const float A[5][3] = {{1.f, 0.447f, 0.355f}, {1.f, 0.251f, 0.355f}, {1.f, 0.0316f, 0.158f}, {1.f, 0.282f, 0.0891f}, {1.f, 0.1f, 0.0251f}};
        const float p = std::clamp(morph, 0.f, 1.f) * 4.f;
        const int i0 = std::min(3, (int)p);
        const float fr = p - (float)i0;
        const float shift = std::clamp(std::sqrt(t.fc / 632.f), 0.3f, 3.f);
        for (int b = 0; b < 3; ++b) {
            const float f = std::exp(std::log(F[i0][b]) + (std::log(F[i0 + 1][b]) - std::log(F[i0][b])) * fr) * shift;
            fgF[b] = std::tan(kPi * std::min(f, (float)sr_ * 0.45f) / (float)sr_);
            faF[b] = A[i0][b] + (A[i0 + 1][b] - A[i0][b]) * fr;
        }
        kF = 2.f / (5.f + 15.f * rsB);
    } else if (ft == kFtPhase) {
        const float spread = 0.25f + 1.5f * morph;
        for (int s = 0; s < nPh; ++s) {
            const float f = t.fc * std::exp2(((float)s - 0.5f * (float)(nPh - 1)) * spread);
            gPh[s] = std::tan(kPi * std::clamp(f, 20.f, (float)sr_ * 0.45f) / (float)sr_);
        }
    } else if (ft == kFtCombP || ft == kFtCombN) {
        cD = std::clamp((float)sr_ / std::max(t.fc, 20.f), 2.f, (float)(combLen_ - 4));
        cFb = (ft == kFtCombP ? 1.f : -1.f) * (0.2f + 0.77f * rsB);
        cDamp = 1.f - 0.92f * morph;
    }
    int cw = v.cw;
    // un étage SVF (Cytomic, trapèzes) : sorties passe-bas, passe-bande, passe-haut
    auto svf = [&](float x, float& s1, float& s2, float gg2, float kk, float& lp, float& bp) {
        if (fd > 0.001f) {
            x = fastTanh(x * fdk) * fdInv;
            s1 = 2.f * fastTanh(0.5f * s1);
        }
        const float a1 = 1.f / (1.f + gg2 * (gg2 + kk)), a2 = gg2 * a1, a3 = gg2 * a2;
        const float v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.f * v1 - s1;
        s2 = 2.f * v2 - s2;
        lp = v2;
        bp = v1;
        return x - kk * v1 - v2;  // passe-haut
    };
    auto pick = [&](float lp, float bp, float hp, float kk) {
        switch (ft) {
            case kFtHp: return hp;
            case kFtBp: return kk * bp;
            case kFtNotch: return lp + hp;
            case kFtPeak: return lp - hp;
            case kFtMorph: {
                const float b = kk * bp;
                return morph < 0.5f ? lp + (b - lp) * 2.f * morph : b + (hp - b) * (2.f * morph - 1.f);
            }
            default: return lp;
        }
    };
    // filtre d'un côté (c : 0 gauche, 1 droit)
    auto filt = [&](float x, int c, float& s1, float& s2, float& s3, float& s4) {
        float lp, bp, hp, y;
        switch (ft) {
            case kFtPhase: {
                float a = x;
                for (int s = 0; s < nPh; ++s) {
                    svf(a, v.fs[c][2 * s], v.fs[c][2 * s + 1], gPh[s], k, lp, bp);
                    a = a - 2.f * k * bp;  // passe-tout
                }
                y = 0.5f * (x + a);
                break;
            }
            case kFtCombP: case kFtCombN: {
                float* buf = comb + (size_t)c * (size_t)combLen_;
                const float rp = (float)cw - cD;
                const int r0 = (int)std::floor(rp);
                const float fr = rp - (float)r0;
                const float a = buf[(size_t)(r0 & cMask)], b = buf[(size_t)((r0 + 1) & cMask)];
                v.cLp[c] += (a + (b - a) * fr - v.cLp[c]) * cDamp;
                const float xin = fd > 0.001f ? fastTanh(x * fdk) * fdInv : x;
                y = xin + cFb * v.cLp[c];
                buf[(size_t)(cw & cMask)] = std::clamp(y, -8.f, 8.f);
                y *= 1.f - 0.5f * std::fabs(cFb);
                break;
            }
            case kFtFormant: {
                y = 0.f;
                for (int bnd = 0; bnd < 3; ++bnd) {
                    svf(x, v.fs[c][2 * bnd], v.fs[c][2 * bnd + 1], fgF[bnd], kF, lp, bp);
                    y += faF[bnd] * kF * bp;
                }
                y *= 1.6f;
                break;
            }
            default:
                if (slope24) {
                    // 24 dB : deux étages (Butterworth à Q 0,54 puis l'étage résonant), même type
                    hp = svf(x, s3, s4, g, 1.848f, lp, bp);
                    const float x2 = pick(lp, bp, hp, 1.848f);
                    const float k2 = k * 0.3825f;  // 0,765 sans résonance (paire Butterworth d'ordre 4)
                    hp = svf(x2, s1, s2, g, k2, lp, bp);
                    y = pick(lp, bp, hp, k2);
                } else {
                    hp = svf(x, s1, s2, g, k, lp, bp);
                    y = pick(lp, bp, hp, k);
                }
        }
        if (fd > 0.001f) y *= 1.f + 0.6f * fd;
        return y;
    };
    // bruit : un échantillon (gauche, droite)
    const int nzType = nzType_, nzMode = nzMode_;
    const float nzA = nzA_;
    const double nzInc = (double)incS * 16.0;
    auto rnd = [](uint32_t& s) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(s & 0xFFFFFF) * (2.f / 16777216.f) - 1.f;
    };
    auto noiseSample = [&](float& nl, float& nr) {
        switch (nzType) {
            case kNzPink: {
                for (int c = 0; c < 2; ++c) {
                    const float w = rnd(v.nz[c]);
                    float* b = v.pk[c];
                    b[0] = 0.99765f * b[0] + w * 0.0990460f;
                    b[1] = 0.96300f * b[1] + w * 0.2965164f;
                    b[2] = 0.57000f * b[2] + w * 1.0526913f;
                    (c ? nr : nl) = (b[0] + b[1] + b[2] + w * 0.1848f) * 0.22f;
                }
                break;
            }
            case kNzCrackle: {
                const float r = rnd(v.nz[0]);
                v.crk *= 0.55f;
                if (std::fabs(r) > 0.9993f) v.crk += rnd(v.nz[1]) * 1.6f;
                nl = nr = v.crk;
                break;
            }
            case kNzDigital: {
                v.nzPh += nzInc;
                if (v.nzPh >= 1.0) { v.nzPh -= std::floor(v.nzPh); v.nzHold = rnd(v.nz[0]) * 0.5f; }
                nl = nr = v.nzHold;
                break;
            }
            default:
                nl = rnd(v.nz[0]) * 0.5f;
                nr = rnd(v.nz[1]) * 0.5f;
        }
        if (nzMode != 2) {  // couleur : passe-bas (sombre) ou passe-haut (clair) d'un pôle
            v.nzF[0] += (nl - v.nzF[0]) * nzA;
            v.nzF[1] += (nr - v.nzF[1]) * nzA;
            if (nzMode == 0) { nl = v.nzF[0]; nr = v.nzF[1]; }
            else { nl -= v.nzF[0]; nr -= v.nzF[1]; }
        }
    };
    bool alive = true;
    for (int i = 0; i < n; ++i) {
        // enveloppes A et B (une par oscillateur) : un point tous les 8 échantillons, interpolé
        if (envCnt <= 0) {
            if (v.envA.idle() && v.envB.idle()) { alive = false; break; }
            const int seg = v.envSpan > 0 ? v.envSpan : kEnvStep;
            v.envSpan = 0;
            const float nA = v.envA.step(envDt, esA), nB = v.envB.step(envDt, esB);
            deA = (nA - eA) / (float)seg;
            deB = (nB - eB) / (float)seg;
            envCnt = seg;
        }
        --envCnt;
        eA += deA;
        eB += deB;
        // oscillateurs couplés en alternatif : la forme d'onde (pulse étroit, warp, LOOP) ne
        // laisse pas de continu, donc pas de « toc » quand les notes s'arrêtent
        float ra, rb, raR, rbR;
        if (iA) {
            readInst(*v.zoneA, ipA, ra, raR);
            ipA += v.inRateA;
        } else if (v.lostA) {
            ra = 0.f;
        } else if (v.smpA) {
            ra = sA ? readSmp(v.smpDataA, v.smpLenA, spA) : 0.f;
            spA += v.smpRateA;
        } else {
            const float u = readPhase(phA, loopA, wA, wmA, lastB);
            ra = TA->readMix(v.levelA, mA, posA, u);
            if (wmA >= kWmFlip && wA > 0.0005f) ra = warpAmp(ra, u, wmA, wA, lastB);
            dcA += (ra - dcA) * dcK;
        }
        if (!iA) raR = ra;
        if (iB) {
            readInst(*v.zoneB, ipB, rb, rbR);
            ipB += v.inRateB;
        } else if (v.lostB) {
            rb = 0.f;
        } else if (v.smpB) {
            rb = sB ? readSmp(v.smpDataB, v.smpLenB, spB) : 0.f;
            spB += v.smpRateB;
        } else {
            const float u = readPhase(phB, loopB, wB, wmB, lastA);
            rb = TB->readMix(v.levelB, mB, posB, u);
            if (wmB >= kWmFlip && wB > 0.0005f) rb = warpAmp(rb, u, wmB, wB, lastA);
            dcB += (rb - dcB) * dcK;
        }
        if (!iB) rbR = rb;
        lastA = ra - dcA;
        lastB = rb - dcB;
        const float sa = (ra - dcA) * eA, saR = (raR - dcA) * eA;
        const float sb = (rb - dcB) * eB, sbR = (rbR - dcB) * eB;
        float xl = sa * aL + sb * bL, xr = saR * aR + sbR * bR;
        // sous-oscillateur sinus (au centre), avant le drive : la 808 sature avec le reste
        if (sub > 0.00001f || dSub != 0.f) {
            const float ss = sinAt((float)phS) * sub * eA;
            xl += ss;
            xr += ss;
        }
        // bruit (enveloppe A), avant le drive et le filtre
        if (nzg > 0.00001f || dNz != 0.f) {
            float nl, nr;
            noiseSample(nl, nr);
            xl += nl * nzg * eA;
            xr += nr * nzg * eA;
        }
        phS += incS;
        if (phS >= 1.0) phS -= std::floor(phS);
        phA += v.incA;
        if (phA >= 1.0) phA -= std::floor(phA);
        phB += v.incB;
        if (phB >= 1.0) phB -= std::floor(phB);
        // drive (HARMONIQUES) : tanh anti-crénelée (ADAA d'ordre 1) ; la part linéaire est
        // moyennée sur deux échantillons pour rester alignée sur le demi-échantillon de l'ADAA
        if (dr > 1.01f) {
            const float nd = 0.9f / fastTanh(dr);
            const float yl = adaaTanh(xl * dr, v.adX[0]) * nd + 0.05f * (xl + v.lin1[0]);
            const float yr = adaaTanh(xr * dr, v.adX[1]) * nd + 0.05f * (xr + v.lin1[1]);
            v.lin1[0] = xl;
            v.lin1[1] = xr;
            xl = yl;
            xr = yr;
        } else {
            v.adX[0] = (double)(xl * dr); v.lin1[0] = xl;
            v.adX[1] = (double)(xr * dr); v.lin1[1] = xr;
        }
        // filtre multimode (un par côté) ; MIX : part filtrée
        float yl = filt(xl, 0, ic1, ic2, ic3, ic4);
        float yr = filt(xr, 1, ic1r, ic2r, ic3r, ic4r);
        ++cw;
        if (fmix < 0.9995f) {
            yl = xl + (yl - xl) * fmix;
            yr = xr + (yr - xr) * fmix;
        }
        if (gStep > 0.f) {
            gg -= gStep;
            if (gg <= 0.f) { alive = false; break; }
            yl *= gg;
            yr *= gg;
        }
        L[i] += yl * pl;
        R[i] += yr * pr;
        mono[i] += 0.5f * (yl + yr);
        posA += dPosA; posB += dPosB; g += dG; k += dK; pl += dPl; pr += dPr; dr += dDr;
        aL += dAL; aR += dAR; bL += dBL; bR += dBR; wA += dWA; wB += dWB; mA += dMA; mB += dMB; sub += dSub;
        fmix += dFm; nzg += dNz;
    }
    if (full) {
        v.posA = t.posA; v.posB = t.posB; v.g = t.g; v.k = t.k; v.panL = t.panL; v.panR = t.panR;
        v.aL = t.aL; v.aR = t.aR; v.bL = t.bL; v.bR = t.bR; v.wA = t.wA; v.wB = t.wB; v.drive = t.drive;
        v.mixA = t.mixA; v.mixB = t.mixB; v.sub = t.sub; v.fmix = t.fmix; v.noise = t.noise;
    } else {  // bloc de contrôle pas fini : la rampe continue au prochain appel
        v.posA = posA; v.posB = posB; v.g = g; v.k = k; v.panL = pl; v.panR = pr;
        v.aL = aL; v.aR = aR; v.bL = bL; v.bR = bR; v.wA = wA; v.wB = wB; v.drive = dr;
        v.mixA = mA; v.mixB = mB; v.sub = sub; v.fmix = fmix; v.noise = nzg;
    }
    v.cw = cw & cMask;
    v.lastA = std::isfinite(lastA) ? lastA : 0.f;
    v.lastB = std::isfinite(lastB) ? lastB : 0.f;
    v.phA = phA; v.phB = phB; v.phS = phS;
    v.smpPosA = spA; v.smpPosB = spB;
    v.inPosA = ipA; v.inPosB = ipB;
    v.eA = eA; v.eB = eB; v.deA = deA; v.deB = deB; v.envCnt = envCnt;
    v.dcA = dcA; v.dcB = dcB;
    v.env = eA;
    auto fin = [](float x) { return std::isfinite(x) ? x : 0.f; };
    v.ic1 = fin(ic1); v.ic2 = fin(ic2); v.ic1r = fin(ic1r); v.ic2r = fin(ic2r);
    v.ic3 = fin(ic3); v.ic4 = fin(ic4); v.ic3r = fin(ic3r); v.ic4r = fin(ic4r);
    for (auto& side : v.fs)
        for (float& x : side) x = fin(x);
    v.cLp[0] = fin(v.cLp[0]);
    v.cLp[1] = fin(v.cLp[1]);
    for (int c = 0; c < 2; ++c) v.nzF[c] = fin(v.nzF[c]);
    if (v.ghost > 0) v.ghost = alive ? std::max(1, (int)std::lround(gg * (float)stealLen_)) : 0;
    return alive;
}

// n échantillons, pris sur les span restants du bloc de contrôle : les rampes vont de la valeur
// courante à la cible en span échantillons, donc le rendu ne dépend pas du découpage de l'hôte.
void Engine::renderBlock(float* L, float* R, int n, int span) {
    span = std::max(span, n);
    const bool full = n == span;
    const float inv = 1.f / (float)span;
    float* mono = mono_.data();
    std::fill(L, L + n, 0.f);
    std::fill(R, R + n, 0.f);
    std::fill(mono, mono + n, 0.f);
    const EnvShape esA = envShape(0), esB = envShape(1);
    // passage à un COMB : les tampons repartent du silence (pas de restes d'anciennes notes)
    if (filtType_ != lastFiltType_) {
        if (filtType_ == kFtCombP || filtType_ == kFtCombN) std::fill(combPool_.begin(), combPool_.end(), 0.f);
        lastFiltType_ = filtType_;
    }
    for (size_t vi = 0; vi < voices_.size(); ++vi) {
        Voice& v = voices_[vi];
        if (v.active && !renderVoice(v, voiceTargets_[vi], L, R, mono, n, span, esA, esB, combOf((int)vi))) v.active = false;
    }
    for (size_t gi = 0; gi < ghosts_.size(); ++gi) {
        Voice& v = ghosts_[gi];
        if (v.ghost > 0 && !renderVoice(v, ghostTargets_[gi], L, R, mono, n, span, esA, esB, combOf(kVoices + (int)gi))) v.ghost = 0;
    }

    // --- TEXTURE : nuage de grains relus dans l'historique du son -----------------------------
    const int gmask = (int)gbuf_.size() - 1;
    for (int i = 0; i < n; ++i) gbuf_[(size_t)((gw_ + i) & gmask)] = mono[i];
    const float wet = grainAmt_ * 0.7f, dry = 1.f - 0.35f * grainAmt_;
    if (grainAmt_ > 0.001f || std::any_of(grains_.begin(), grains_.end(), [](const Grain& g) { return g.on; })) {
        for (int i = 0; i < n; ++i) { L[i] *= dry; R[i] *= dry; }
        for (auto& gr : grains_) {
            if (!gr.on) continue;
            for (int i = 0; i < n; ++i) {
                if (gr.age >= gr.len) { gr.on = false; break; }
                const double p = gr.pos;
                const int i0 = (int)std::floor(p);
                const float fr = (float)(p - i0);
                const float a = gbuf_[(size_t)(i0 & gmask)], b = gbuf_[(size_t)((i0 + 1) & gmask)];
                const float w = hann_[(size_t)(gr.age * 512 / gr.len)];
                const float s = (a + (b - a) * fr) * w * wet;
                L[i] += s * gr.gl;
                R[i] += s * gr.gr;
                gr.pos += gr.inc;
                ++gr.age;
            }
        }
    }
    gw_ = (gw_ + n) & gmask;

    // --- ESPACE : élargissement par deux retards modulés ---------------------------------------
    const float width = std::fabs(space_) * 0.45f;
    if (width > 0.002f) {
        const int dmask = 4095;
        const double rate = 0.15 + 1.2 * liveNow_;
        for (int i = 0; i < n; ++i) {
            const float in = (L[i] + R[i]) * 0.5f;
            dl_[0][(size_t)dw_] = in;
            dl_[1][(size_t)dw_] = in;
            chorusPh_ += rate / sr_;
            if (chorusPh_ >= 1.0) chorusPh_ -= 1.0;
            const float s = std::sin(2.f * kPi * (float)chorusPh_);
            for (int c = 0; c < 2; ++c) {
                const float ms = 7.f + (c ? -3.f : 3.f) * s + (c ? 2.f : 0.f);
                const float d = ms * 0.001f * (float)sr_;
                const float rp = (float)dw_ - d;
                const int r0 = (int)std::floor(rp);
                const float fr = rp - (float)r0;
                const float a = dl_[c][(size_t)(r0 & dmask)], b = dl_[c][(size_t)((r0 + 1) & dmask)];
                const float y = a + (b - a) * fr;
                // ESPACE négatif : retour en opposition de phase à droite, image plus large et instable
                if (c == 0) L[i] += y * width;
                else R[i] += y * width * (space_ >= 0.f ? 1.f : -1.f);
            }
            dw_ = (dw_ + 1) & dmask;
        }
    }

    // --- rack d'effets (page EFX), avant le master -------------------------------------------
    m_.dryScope.write(L, R, n);
    updateFxSettings();
    fx_.process(L, R, n, fxs_);
    // coupe-continu final, 5 Hz, en double (rien ne laisse de continu en sortie, effets compris)
    for (int c = 0; c < 2; ++c) {
        float* x = c ? R : L;
        double x1 = dcX_[c], y1 = dcY_[c];
        for (int i = 0; i < n; ++i) {
            const double in = x[i];
            y1 = in - x1 + dcR_ * y1;
            x1 = in;
            x[i] = (float)y1;
        }
        dcX_[c] = x1;
        dcY_[c] = y1;
    }

    // --- master, chaîne MIX IA (EQ, compresseur, limiteur True Peak), sécurité ----------------
    const float dM = (masterTarget_ - masterGain_) * inv;
    for (int i = 0; i < n; ++i) {
        masterGain_ += dM;
        L[i] *= masterGain_;
        R[i] *= masterGain_;
    }
    if (full) masterGain_ = masterTarget_;
    {
        MixChain& mix = m_.mix;
        for (int k = 0; k < kMixFilterSlots; ++k) {
            const MixFilter& f = mix.flt[(size_t)k];
            master_.setEq(k, f.on.load(std::memory_order_acquire), f.type.load(std::memory_order_relaxed), f.hz.load(std::memory_order_relaxed),
                          f.db.load(std::memory_order_relaxed), f.q.load(std::memory_order_relaxed));
        }
        master_.setComp(mix.comp.load(std::memory_order_relaxed), mix.compThreshDb.load(std::memory_order_relaxed), mix.compRatio.load(std::memory_order_relaxed));
        master_.setLimiter(mix.limit.load(std::memory_order_relaxed), mix.limitCeilDb.load(std::memory_order_relaxed));
    }
    master_.process(L, R, n);
    const float fadeStep = 1.f / (float)fadeLen_;
    for (int i = 0; i < n; ++i) {
        // anti-clic : fondu de sortie avant PANIC, fondu d'entrée après une remise à zéro
        if (panicFade_ >= 0) {
            outGain_ = std::min(outGain_, (float)panicFade_ * fadeStep);
            if (panicFade_ > 0) --panicFade_;
        } else if (outGain_ < 1.f) {
            outGain_ = std::min(1.f, outGain_ + fadeStep);
        }
        L[i] = softSafety(L[i]) * outGain_;
        R[i] = softSafety(R[i]) * outGain_;
    }
    m_.readouts.compGr.store(master_.gainReductionDb(), std::memory_order_relaxed);
    m_.meters.process(L, R, n);
}

void Engine::process(float* outL, float* outR, int numSamples, const NoteEvent* ev, int nev, const ParamEvent* pev, int npev) {
    DenormalGuard dg;
    const auto t0 = std::chrono::steady_clock::now();
    m_.audioBlockBegin();
    tab_[0] = m_.audioTable(0);
    tab_[1] = m_.audioTable(1);
    smpSet_[0] = m_.samples.audio(0);
    smpSet_[1] = m_.samples.audio(1);
    inst_[0] = m_.instruments.audio(0);
    inst_[1] = m_.instruments.audio(1);
    // un instrument ou un son remplacé depuis le départ d'une note : la voix l'oublie tout de suite
    // (sa mémoire sera libérée après ce bloc, la lire plantait le plugin)
    for (auto& v : voices_)
        if (v.active) dropStaleSounds(v);
    for (auto& v : ghosts_)
        if (v.ghost > 0) dropStaleSounds(v);

    // PANIC : 5 ms de fondu, puis tout s'arrête (voir plus bas)
    if (m_.panic.exchange(false) && panicFade_ < 0) {
        panicFade_ = fadeLen_;
        panicOrder_ = order_;
    }
    // horloge musicale du bloc : celle de l'hôte en lecture, sinon une horloge libre au tempo
    {
        const double bpm = std::clamp((double)m_.hostBpm.load(std::memory_order_relaxed), 20.0, 999.0);
        beatPerSmp_ = bpm / 60.0 / sr_;
        blockT0_ = time_;
        blockBeat_ = m_.hostPlaying.load(std::memory_order_relaxed) ? m_.hostPpq.load(std::memory_order_relaxed) : freeBeat_;
        if (m_.hostPlaying.load(std::memory_order_relaxed) && std::fabs(blockBeat_ - freeBeat_) > 1e-3 && arpNext_ >= 0.0 &&
            (arpNext_ < blockBeat_ - 4.0 || arpNext_ > blockBeat_ + 4.0))
            arpNext_ = -1.0;  // saut du transport (boucle, déplacement) : l'arpège se recale
        freeBeat_ = blockBeat_ + (double)numSamples * beatPerSmp_;
    }
    NoteEvent gn;
    while (m_.guiNotes.pop(gn)) {
        curChan_ = 0;
        curNoteId_ = -1;
        if (gn.on) inputOn(gn.note, gn.velocity, time_);
        else inputOff(gn.note, time_);
    }

    int done = 0, e = 0, pe = 0;
    while (done < numSamples) {
        fireInternal(time_ + done);
        // automations de l'hôte à l'échantillon près ; le bloc de contrôle est relancé (au plus
        // tous les 16 échantillons) pour que la nouvelle valeur parte tout de suite, en rampe
        bool moved = false;
        while (pe < npev && pev[pe].offset <= done) {
            m_.params.set(pev[pe].id, pev[pe].value);
            ++pe;
            moved = true;
        }
        if (moved && ctlAge_ >= 16) forceControl();
        while (e < nev && ev[e].offset <= done) {
            const NoteEvent& x = ev[e];
            if (x.kind != kEvNote) expression(x);
            else if (x.on && x.velocity > 0.f) {
                curChan_ = x.channel;
                curNoteId_ = x.noteId;
                inputOn(x.note, x.velocity, time_ + done);
            } else inputOff(x.note, time_ + done);
            ++e;
        }
        fireInternal(time_ + done);
        if (ctlLeft_ <= 0) {
            ctlLeft_ = std::clamp(m_.controlBlock.load(std::memory_order_relaxed), 16, 256);
            ctlAge_ = 0;
            controlUpdate(ctlLeft_);
        }
        int n = std::min({numSamples - done, ctlLeft_, maxBlock_});
        if (e < nev) n = std::min(n, std::max(1, ev[e].offset - done));
        if (pe < npev) n = std::min(n, std::max(1, pev[pe].offset - done));
        if (const int ni = nextInternal(time_, done, numSamples); ni >= 0) n = std::min(n, std::max(1, ni - done));
        if (panicFade_ > 0) n = std::min(n, panicFade_);
        fxs_.beat = beatAt(time_ + done);  // PUMP : position musicale de ce morceau du bloc
        fxs_.beatPerSample = beatPerSmp_;
        renderBlock(outL + done, outR + done, n, ctlLeft_);
        ctlLeft_ -= n;
        ctlAge_ += n;
        done += n;
        if (panicFade_ == 0) {  // fondu terminé : arrêt des voix d'avant le PANIC et des queues d'effets
            const std::array<Voice, kVoices> keep = voices_;
            reset();
            for (size_t i = 0; i < voices_.size(); ++i)
                if (keep[i].active && keep[i].order > panicOrder_) voices_[i] = keep[i];
        }
    }
    while (pe < npev) m_.params.set(pev[pe].id, pev[pe].value), ++pe;
    while (e < nev) {  // évènements au-delà du bloc
        const NoteEvent& x = ev[e];
        if (x.kind != kEvNote) expression(x);
        else if (x.on && x.velocity > 0.f) {
            curChan_ = x.channel;
            curNoteId_ = x.noteId;
            inputOn(x.note, x.velocity, time_ + numSamples);
        } else inputOff(x.note, time_ + numSamples);
        ++e;
    }
    time_ += numSamples;

    m_.scope.write(outL, outR, numSamples);
    m_.life.audioTick(numSamples, sr_);

    int count = 0, held = -1;
    uint64_t best = 0;
    float hit = 0.f;
    for (const auto& v : voices_) {
        if (!v.active) continue;
        ++count;
        hit = std::max(hit, v.env * v.vel);
        if (!v.released && v.order > best) { best = v.order; held = v.note; }
    }
    Readouts& R = m_.readouts;
    R.voices.store(count, std::memory_order_relaxed);
    R.heldNote.store(held, std::memory_order_relaxed);
    R.hit.store(hit, std::memory_order_relaxed);
    R.active.store(true, std::memory_order_relaxed);
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double budget = (double)numSamples / sr_;
    cpu_ += ((float)(el / budget * 100.0) - cpu_) * 0.05f;
    R.cpu.store(cpu_, std::memory_order_relaxed);
    m_.audioBlockEnd();
}

} // namespace raw2
