#include "ai/Assistant.h"
#include "core/Params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace raw2::ai {

bool networksTrained() { return kMixNet.trained && kRoleNet.trained; }

// --- MIX IA ---------------------------------------------------------------------------------------
MixAdvice adviseMix(const float* seg) {
    MixAdvice a;
    float y[kNumMixOut + kNumMixEq] = {};
    forward(kMixNet, seg, y);
    for (int k = 0; k < kNumMixOut; ++k) a.prob[k] = kMixNet.trained ? sigmoid(y[k]) : 0.f;
    const float sign[kNumMixEq] = {-1.f, 1.f, 1.f, -1.f};  // 250 Hz et 3.5 kHz creusent, 8 kHz et 80 Hz relèvent
    for (int e = 0; e < kNumMixEq; ++e) {
        const float g = sign[e] * std::fabs(y[kNumMixOut + e]);
        a.gainDb[e] = std::clamp(std::round(g * 2.f) / 2.f, MixChain::minGain(e), MixChain::maxGain(e));
        if (!kMixNet.trained) a.gainDb[e] = MixChain::defaultGain(e);
    }
    a.compThreshDb = std::clamp(std::round(seg[fLevel] + 6.f), -30.f, -6.f);
    return a;
}

// --- rôles ----------------------------------------------------------------------------------------
const char* roleName(int r) {
    static const char* n[kNumRoles] = {"BASSE", "LEAD", "PAD", "ARP / SEQ", "TEXTURE"};
    return n[std::clamp(r, 0, kNumRoles - 1)];
}

RoleGuess classifyRole(const ProbeResult& r) {
    RoleGuess g;
    float y[kNumRoles] = {};
    forward(kRoleNet, r.x.data(), y);
    softmax(y, kNumRoles);
    for (int k = 0; k < kNumRoles; ++k) g.p[k] = kRoleNet.trained ? y[k] : 1.f / kNumRoles;
    g.best = (int)(std::max_element(g.p, g.p + kNumRoles) - g.p);
    return g;
}

// --- PROPOSITION ----------------------------------------------------------------------------------
namespace {

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
std::string hzText(float hz) { return hz < 1000.f ? fmt("%.0f Hz", std::round(hz / 10.f) * 10.f) : fmt("%.1f kHz", hz / 1000.f); }
std::string msText(float s) { return s < 1.f ? fmt("%.0f ms", s * 1000.f) : fmt("%.1f s", s); }

struct Candidate {
    int group;
    Proposal p;
};

// Retouches que l'assistant sait essayer (mêmes commandes que l'interface).
std::vector<Candidate> candidates(const ParamStore& P) {
    std::vector<Candidate> c;
    auto add = [&](int group, std::vector<std::pair<uint32_t, float>> ch, std::string what, std::string value, std::string done) {
        for (auto& x : ch) x.second = clampParam(x.first, x.second);
        bool same = true;
        for (auto& x : ch) same = same && std::fabs(x.second - P.get(x.first)) < 1e-4f;
        if (same) return;
        Candidate k;
        k.group = group;
        k.p.changes = std::move(ch);
        k.p.what = std::move(what);
        k.p.value = std::move(value);
        k.p.done = std::move(done);
        c.push_back(std::move(k));
    };
    // niveaux des oscillateurs
    const char* oscN[2] = {"OSC A", "OSC B"};
    const uint32_t lvl[2] = {kALvl, kBLvl};
    for (int o = 0; o < 2; ++o) {
        const float v = P.get(lvl[o]);
        if (v < 0.05f) continue;  // oscillateur coupé : on n'y touche pas
        add(o, {{lvl[o], v - 3.f / 18.2f}}, std::string("Baisser ") + oscN[o] + " de", "-3 dB", std::string(oscN[o]) + " -3 dB");
        if (v + 2.f / 18.2f <= 1.f) add(o, {{lvl[o], v + 2.f / 18.2f}}, std::string("Monter ") + oscN[o] + " de", "+2 dB", std::string(oscN[o]) + " +2 dB");
    }
    // modules
    const char* modN[3] = {"HARMONIQUES", "ESPACE", "TEXTURE"};
    const uint32_t mod[3] = {kM1, kM2, kM3};
    const float modStep[3] = {35.f, 35.f, 40.f};
    for (int k = 0; k < 3; ++k) {
        const float v = P.get(mod[k]);
        for (float d : {modStep[k], -modStep[k]}) {
            const float nv = clampParam(mod[k], std::round(v + d));
            add(2 + k, {{mod[k], nv}}, std::string("Passer ") + modN[k] + " \xC3\xA0", fmt("%+.0f %%", nv), std::string(modN[k]) + fmt(" %+.0f %%", nv));
        }
    }
    // hauteur de l'oscillateur B
    {
        const float st = P.get(kBSt), oc = P.get(kBOct);
        if (st + 7.f <= 12.f) add(5, {{kBSt, st + 7.f}}, "D\xC3\xA9" "caler OSC B de", "+7 ST", "OSC B +7 ST");
        if (oc + 1.f <= 2.f) add(5, {{kBOct, oc + 1.f}}, "Monter OSC B d'", "1 octave", "OSC B +1 OCT");
        if (oc - 1.f >= -2.f) add(5, {{kBOct, oc - 1.f}}, "Descendre OSC B d'", "1 octave", "OSC B -1 OCT");
    }
    // filtre (0.1 = une octave)
    {
        const float v = P.get(kCutoff);
        for (float d : {0.1f, -0.1f}) {
            const float nv = clampParam(kCutoff, v + d);
            add(6, {{kCutoff, nv}}, d > 0 ? "Ouvrir le FILTRE \xC3\xA0" : "Fermer le FILTRE \xC3\xA0", hzText(cutoffToHz(nv)), "FILTRE " + hzText(cutoffToHz(nv)));
        }
    }
    // enveloppe
    {
        const float a = P.get(kAttack), r = P.get(kRelease), s = P.get(kSustain);
        for (float d : {-0.15f, 0.2f}) {
            const float nv = clampParam(kAttack, a + d);
            add(7, {{kAttack, nv}}, "Passer l'ATTAQUE \xC3\xA0", msText(attackSeconds(nv)), "ATTAQUE " + msText(attackSeconds(nv)));
        }
        for (float d : {-0.15f, 0.15f}) {
            const float nv = clampParam(kRelease, r + d);
            add(8, {{kRelease, nv}}, "Passer la REL\xC3\x82" "CHE \xC3\xA0", msText(releaseSeconds(nv)), "REL\xC3\x82" "CHE " + msText(releaseSeconds(nv)));
        }
        for (float d : {-0.3f, 0.3f}) {
            const float nv = clampParam(kSustain, s + d);
            add(9, {{kSustain, nv}}, "Passer le MAINTIEN \xC3\xA0", fmt("%.0f %%", nv * 100.f), fmt("MAINTIEN %.0f %%", nv * 100.f));
        }
    }
    // vie
    {
        const float v = P.get(kSpeed);
        for (float d : {0.15f, -0.15f}) {
            const float nv = clampParam(kSpeed, v + d);
            add(10, {{kSpeed, nv}}, "Passer VITESSE \xC3\xA0", fmt("%.1f Hz", speedToHz(nv)), fmt("VITESSE %.1f Hz", speedToHz(nv)));
        }
        const bool hl = P.get(kRule) > 0.5f;
        add(11, {{kRule, hl ? 0.f : 1.f}}, "Passer sur la r\xC3\xA8gle", hl ? "B3/S23 (Conway)" : "B36/S23 (HighLife)", hl ? "r\xC3\xA8gle B3/S23" : "r\xC3\xA8gle B36/S23");
    }
    // effets (page EFX) : dosage des effets actifs, ou couper / ajouter les plus marquants
    if (P.get(kFxWet) > 0.05f) {
        for (int f = 0; f < kNumFx; ++f) {
            const uint32_t on = fxParam(f, kFxOn), mixId = fxParam(f, kFxMix);
            const std::string nm = fxName(f);
            if (P.get(on) > 0.5f) {
                const float v = P.get(mixId);
                for (float d : {0.2f, -0.2f}) {
                    const float nv = clampParam(mixId, v + d);
                    if (nv < 0.05f) continue;
                    add(12 + f, {{mixId, nv}}, "Passer le MIX de " + nm + " \xC3\xA0", fmt("%.0f %%", nv * 100.f), nm + fmt(" MIX %.0f %%", nv * 100.f));
                }
                add(12 + f, {{on, 0.f}}, "Couper l'effet", nm, nm + " coup\xC3\xA9");
            } else if (f == kFxDist || f == kFxChor || f == kFxRev || f == kFxComp) {
                add(12 + f, {{on, 1.f}}, "Ajouter l'effet", nm, nm + " ajout\xC3\xA9");
            }
        }
    }
    return c;
}

} // namespace

SoundAdvisor::SoundAdvisor() { thread_ = std::thread([this] { threadMain(); }); }

SoundAdvisor::~SoundAdvisor() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cancel_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void SoundAdvisor::request(const SoundSnapshot& snap, int role) {
    {
        std::lock_guard<std::mutex> lk(m_);
        job_ = snap;
        jobRole_ = role;
        haveJob_ = true;
        ++serial_;
        cancel_ = true;  // une analyse plus ancienne s'arrête au prochain rendu
        busy_ = true;
    }
    cv_.notify_all();
}

bool SoundAdvisor::poll(SoundAnalysis& out) {
    std::lock_guard<std::mutex> lk(m_);
    if (!haveResult_) return false;
    out = result_;
    haveResult_ = false;
    return true;
}

void SoundAdvisor::threadMain() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);  // jamais devant l'audio de l'hôte
#endif
    std::unique_lock<std::mutex> lk(m_);
    while (true) {
        cv_.wait(lk, [&] { return quit_ || haveJob_; });
        if (quit_) break;
        SoundSnapshot snap = std::move(job_);
        const int role = jobRole_;
        const uint64_t serial = serial_;
        haveJob_ = false;
        cancel_ = false;
        lk.unlock();
        SoundAnalysis res = run(snap, role, serial);
        lk.lock();
        if (!cancel_ && !haveJob_ && !quit_) {
            result_ = std::move(res);
            haveResult_ = true;
            busy_ = false;
        }
    }
}

SoundAnalysis SoundAdvisor::run(const SoundSnapshot& snap, int role, uint64_t serial) {
    SoundAnalysis a;
    a.serial = serial;
    if (!probe_) probe_ = std::make_unique<ProbeRenderer>();
    probe_->load(snap);
    Model& m = probe_->model();
    const ProbeResult base = probe_->render(1);
    a.guess = classifyRole(base);
    a.autoRole = role < 0;
    a.target = role < 0 ? a.guess.best : std::clamp(role, 0, kNumRoles - 1);
    a.silent = base.silent;
    if (base.silent || !kRoleNet.trained) return a;

    // écart au profil spectral du rôle visé (pour expliquer la proposition)
    {
        float best = 4.f;
        for (int b = 0; b < kBands; ++b) {
            float cur = 0.f;
            for (int s = 0; s < kProbeSegs; ++s) cur += base.x[(size_t)(s * kSegDim + fBand0 + b)] / kProbeSegs;
            const float prof = kRoleProfile[a.target][b];
            if (std::max(cur, prof) < -35.f) continue;
            const float d = cur - prof;
            if (std::fabs(d) > best) {
                best = std::fabs(d);
                a.bandHz = (int)bandCenterHz(b);
                a.bandExcess = d > 0.f;
            }
        }
    }

    // essais : chaque retouche est rendue puis réécoutée par le réseau
    struct Scored { float gain; int group; Proposal p; };
    std::vector<Scored> scored;
    const float before = a.guess.p[a.target];
    for (Candidate& c : candidates(m.params)) {
        if (cancel_) return a;
        std::vector<std::pair<uint32_t, float>> old;
        for (auto& ch : c.p.changes) {
            old.push_back({ch.first, m.params.get(ch.first)});
            m.params.set(ch.first, ch.second);
        }
        const ProbeResult r = probe_->render(1);
        for (auto& o : old) m.params.set(o.first, o.second);
        ++a.tried;
        if (r.silent || r.peakDb > -0.5f) continue;  // pas de proposition qui coupe ou fait saturer
        const RoleGuess g = classifyRole(r);
        c.p.before = before;
        c.p.after = g.p[a.target];
        scored.push_back({c.p.after - before, c.group, c.p});
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& x, const Scored& y) { return x.gain > y.gain; });
    std::vector<int> used;
    for (const Scored& s : scored) {
        if (s.gain < 0.02f || a.props.size() >= 3) break;
        if (std::find(used.begin(), used.end(), s.group) != used.end()) continue;
        used.push_back(s.group);
        a.props.push_back(s.p);
    }
    return a;
}

} // namespace raw2::ai
