#include "core/Life.h"
#include "core/Json.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace raw2 {

namespace {

PatternInfo mkPattern(const char* name, std::vector<std::pair<int, int>> c) {
    int w = 0, h = 0;
    for (auto& p : c) { w = std::max(w, p.first + 1); h = std::max(h, p.second + 1); }
    return PatternInfo{name, std::move(c), w, h};
}

const std::vector<PatternInfo>& patterns() {
    static const std::vector<PatternInfo> P = [] {
        std::vector<PatternInfo> v;
        v.push_back(mkPattern("Glider", {{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}}));
        std::vector<std::pair<int, int>> pl;
        const int R[4] = {0, 5, 7, 12}, C[6] = {2, 3, 4, 8, 9, 10};
        for (int r : R)
            for (int c : C) { pl.push_back({c, r}); pl.push_back({r, c}); }
        v.push_back(mkPattern("Pulsar", pl));
        v.push_back(mkPattern("Gosper Gun", {{0, 4}, {0, 5}, {1, 4}, {1, 5}, {10, 4}, {10, 5}, {10, 6}, {11, 3}, {11, 7}, {12, 2}, {12, 8}, {13, 2}, {13, 8}, {14, 5}, {15, 3}, {15, 7}, {16, 4}, {16, 5}, {16, 6}, {17, 5}, {20, 2}, {20, 3}, {20, 4}, {21, 2}, {21, 3}, {21, 4}, {22, 1}, {22, 5}, {24, 0}, {24, 1}, {24, 5}, {24, 6}, {34, 2}, {34, 3}, {35, 2}, {35, 3}}));
        v.push_back(mkPattern("Spaceship", {{1, 0}, {4, 0}, {0, 1}, {0, 2}, {4, 2}, {0, 3}, {1, 3}, {2, 3}, {3, 3}}));
        v.push_back(mkPattern("R-pentomino", {{1, 0}, {2, 0}, {0, 1}, {1, 1}, {1, 2}}));
        return v;
    }();
    return P;
}

void initWorld(LifeWorld& w, int GW, int GH, float dens, uint32_t seed) {
    w.GW = GW; w.GH = GH; w.N = GW * GH;
    w.g.assign((size_t)w.N, 0);
    w.age.assign((size_t)w.N, 0);
    // même générateur que la maquette (Park-Miller)
    uint64_t s = seed;
    for (int i = 0; i < w.N; ++i) {
        s = (s * 16807) % 2147483647;
        w.g[(size_t)i] = ((double)s / 2147483647.0) < dens ? 1 : 0;
        w.age[(size_t)i] = w.g[(size_t)i];
    }
}

} // namespace

const PatternInfo& patternInfo(Pattern p) { return patterns()[(size_t)p]; }

double LifeEngine::wallMs() {
    using namespace std::chrono;
    return (double)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() / 1000.0;
}

LifeEngine::LifeEngine(ParamStore& params, bool threaded) : params_(params), threaded_(threaded) {
    initWorld(big_, kWorldW, kWorldH, 0.28f, 23);
    small_.GW = kWinW; small_.GH = kWinH; small_.N = kWinW * kWinH;
    for (int k = 0; k < 6; ++k) step(big_);
    big_.gen = 0;
    big_.dying.clear();
    count(big_);
    syncWindowLocked();
    lastWall_ = wallMs() / 1000.0;
    if (threaded_) thread_ = std::thread([this] { threadMain(); });
}

LifeEngine::~LifeEngine() {
    {
        std::lock_guard<std::mutex> lk(m_);
        running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

float LifeEngine::frand() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(rng_ & 0xFFFFFF) / 16777216.f;
}

void LifeEngine::audioTick(int n, double sr) {
    if (!threaded_) {  // rendu hors ligne : la vie avance au rythme exact des échantillons
        std::lock_guard<std::mutex> lk(m_);
        tickLocked((double)n / std::max(1000.0, sr), wallMs());
        return;
    }
    audioSamples_.fetch_add((uint64_t)n, std::memory_order_relaxed);
    audioRate_.store(sr, std::memory_order_relaxed);
    lastAudioWall_.store(wallMs(), std::memory_order_relaxed);
}

void LifeEngine::count(LifeWorld& w) {
    int a = 0;
    double sx = 0.0;
    for (int i = 0; i < w.N; ++i)
        if (w.g[(size_t)i]) { ++a; sx += (double)(i % w.GW); }
    w.alive = a;
    w.live = std::clamp((float)a / ((float)w.N * 0.3f), 0.f, 1.f);
    w.centroid = a ? (float)(sx / a / (w.GW - 1) * 2.0 - 1.0) : 0.f;
    if (&w == &big_) syncWindowLocked();
}

void LifeEngine::syncWindowLocked() {
    LifeWorld& s = small_;
    s.g.resize((size_t)s.N);
    s.age.resize((size_t)s.N);
    for (int i = 0; i < s.N; ++i) {
        const int j = toWorld(i);
        s.g[(size_t)i] = big_.g[(size_t)j];
        s.age[(size_t)i] = big_.age[(size_t)j];
    }
    s.dying.clear();
    for (const auto& d : big_.dying) {
        const int x = d.i % kWorldW - kWinX, y = d.i / kWorldW - kWinY;
        if (x >= 0 && x < kWinW && y >= 0 && y < kWinH) s.dying.push_back({y * kWinW + x, d.t});
    }
    s.gen = big_.gen;
    int a = 0;
    double sx = 0.0;
    for (int i = 0; i < s.N; ++i)
        if (s.g[(size_t)i]) { ++a; sx += (double)(i % s.GW); }
    s.alive = a;
    s.live = std::clamp((float)a / ((float)s.N * 0.3f), 0.f, 1.f);
    s.centroid = a ? (float)(sx / a / (s.GW - 1) * 2.0 - 1.0) : 0.f;
    publishLocked();
}

// NAISSANCE au-delà du RESET : sans elle, le jeu de la vie finit presque toujours figé (blocs,
// clignotants) ou mort, quelle que soit la densité de départ. Ici, quand le monde s'éteint (densité
// sous NAISSANCE / 4) ou se fige (même image depuis 12 générations, période 1 ou 2), des germes
// naissent au hasard, autant plus nombreux que NAISSANCE est haute ; à 0, la vie suit Conway pur.
void LifeEngine::sustainLocked() {
    LifeWorld& w = big_;
    const float b = params_.get(kBirth);
    uint64_t h = 1469598103934665603ull;  // FNV-1a de la grille
    for (int i = 0; i < w.N; ++i) h = (h ^ w.g[(size_t)i]) * 1099511628211ull;
    stale_ = (h == hash_[0] || h == hash_[1]) ? stale_ + 1 : 0;
    hash_[1] = hash_[0];
    hash_[0] = h;
    if (b <= 0.001f) return;
    const float dens = (float)w.alive / (float)w.N, low = 0.25f * b;
    int germs = 0;
    if (dens < low) germs = 1 + (int)std::lround(6.f * b * (1.f - dens / low));
    else if (stale_ >= 12) germs = 1 + (int)std::lround(3.f * b);
    if (!germs) return;
    const double now = wallMs();
    for (int k = 0; k < germs; ++k) {
        // un germe sur deux dans la zone écoutée, pour que la page SON ne reste pas vide
        int cx, cy;
        if (k % 2 == 0) { cx = kWinX + (int)(frand() * kWinW); cy = kWinY + (int)(frand() * kWinH); }
        else { cx = (int)(frand() * kWorldW); cy = (int)(frand() * kWorldH); }
        // tache 3x3 à moitié pleine (souvent une graine féconde, comme le R-pentomino)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (frand() < 0.5f) setCellLocked(w, ((cy + dy + kWorldH) % kWorldH) * kWorldW + (cx + dx + kWorldW) % kWorldW, true, now);
    }
    stale_ = 0;
    count(w);
}

void LifeEngine::step(LifeWorld& w) {
    const int GW = w.GW, GH = w.GH;
    const bool hl = params_.get(kRule) > 0.5f;
    std::vector<uint8_t> ng((size_t)w.N);
    std::vector<uint16_t> nage((size_t)w.N);
    const double now = wallMs();
    int births = 0;
    for (int y = 0; y < GH; ++y) {
        const int ya = ((y + GH - 1) % GH) * GW, yb = y * GW, yc = ((y + 1) % GH) * GW;
        for (int x = 0; x < GW; ++x) {
            const int xa = (x + GW - 1) % GW, xc = (x + 1) % GW;
            const auto& g = w.g;
            const int n = g[(size_t)(ya + xa)] + g[(size_t)(ya + x)] + g[(size_t)(ya + xc)] + g[(size_t)(yb + xa)] +
                          g[(size_t)(yb + xc)] + g[(size_t)(yc + xa)] + g[(size_t)(yc + x)] + g[(size_t)(yc + xc)];
            const size_t i = (size_t)(yb + x);
            const bool al = g[i] ? (n == 2 || n == 3) : (n == 3 || (hl && n == 6));
            ng[i] = al ? 1 : 0;
            nage[i] = al ? (uint16_t)(g[i] ? std::min(60000, w.age[i] + 1) : 1) : 0;
            if (al && !g[i] && (&w != &big_ || (x >= kWinX && x < kWinX + kWinW && y >= kWinY && y < kWinY + kWinH))) ++births;
            if (g[i] && !al) w.dying.push_back({(int)i, now});
        }
    }
    w.g.swap(ng);
    w.age.swap(nage);
    ++w.gen;
    // naissances de la zone écoutée (le son), rapportées à sa surface
    const float nb = &w == &big_ ? (float)(kWinW * kWinH) : (float)w.N;
    w.births = std::clamp((float)births / (nb * 0.08f), 0.f, 1.f);
    if (&w == &big_) small_.births = w.births;
    count(w);
}

void LifeEngine::publishLocked() {
    const LifeWorld& w = small_;  // la zone écoutée, quelle que soit la page affichée
    soundLive_.store(w.live, std::memory_order_relaxed);
    soundBirths_.store(w.births, std::memory_order_relaxed);
    soundCentroid_.store(w.centroid, std::memory_order_relaxed);
}

void LifeEngine::threadMain() {
    std::unique_lock<std::mutex> lk(m_);
    while (running_) {
        cv_.wait_for(lk, std::chrono::milliseconds(3));
        if (!running_) break;
        // horloge : échantillons audio si l'hôte fait tourner le son, sinon horloge murale
        const double nowWall = wallMs();
        const uint64_t smp = audioSamples_.load(std::memory_order_relaxed);
        double dt;
        if (nowWall - lastAudioWall_.load(std::memory_order_relaxed) < 250.0) {
            dt = (double)(smp - lastSamples_) / std::max(1000.0, audioRate_.load(std::memory_order_relaxed));
        } else {
            dt = nowWall / 1000.0 - lastWall_;
        }
        lastSamples_ = smp;
        lastWall_ = nowWall / 1000.0;
        tickLocked(dt, nowWall);
    }
}

void LifeEngine::tickLocked(double dt, double nowWall) {
    const double d = std::clamp(dt, 0.0, 1.0);
    lifeTime_ += d;
    // temps écoulé depuis la dernière génération de chaque monde (et non différence de deux dates
    // absolues : les arrondis dépendraient alors de tout ce qui a été joué avant)
    big_.since += d;

    const bool frozen = params_.get(kFreeze) > 0.5f;
    const double period = 1.0 / speedToHz(params_.get(kSpeed));
    if (snapPend_ >= 0 && frozen) applySnapLocked(snapPend_, nowWall);
    bool stepped = false;
    if (!frozen) {
        if (big_.since >= period) {
            big_.since = 0.0;
            if (snapPend_ >= 0) applySnapLocked(snapPend_, nowWall);
            else {
                step(big_);
                sustainLocked();
                ++autoCount_;
                if (snapAuto_ && autoCount_ % 16 == 0) {
                    // capture automatique toutes les 16 générations
                    int idx = -1;
                    for (int k = 0; k < 8; ++k) if (!snaps_[(size_t)k]) { idx = k; break; }
                    if (idx < 0 || snapSel_ >= 0) idx = snapNext_;
                    snaps_[(size_t)idx] = LifeSnapshot{big_.g, big_.age, big_.gen};
                    snapSel_ = idx;
                    snapNext_ = (idx + 1) % 8;
                }
            }
            stepped = true;
        }
        // pas trop de retard accumulé (rendu hors ligne, réveil tardif)
        if (big_.since > period * 4) big_.since = 0.0;
    } else {
        big_.since = 0.0;
    }
    for (LifeWorld* w : {&small_, &big_})
        if (!w->dying.empty())
            w->dying.erase(std::remove_if(w->dying.begin(), w->dying.end(), [&](const LifeWorld::Dying& d) { return nowWall - d.t >= 80.0; }), w->dying.end());
    if (stepped) {
        rnd_.store(frand() * 2.f - 1.f, std::memory_order_relaxed);
        publishLocked();
        genCounter_.fetch_add(1, std::memory_order_relaxed);
    }
}

void LifeEngine::restart(uint32_t seed) {
    std::lock_guard<std::mutex> lk(m_);
    rng_ = seed ? seed : 0x2545F491u;
    big_.since = 0.0;
    stale_ = 0;
    autoCount_ = 0;
    rnd_.store(0.f, std::memory_order_relaxed);  // ALÉA : pas de valeur héritée du son d'avant
    publishLocked();
}

void LifeEngine::setSoundWorld(World w) {
    std::lock_guard<std::mutex> lk(m_);
    soundWorld_.store(w);
    publishLocked();
}

void LifeEngine::view(World which, LifeView& out) {
    std::lock_guard<std::mutex> lk(m_);
    const LifeWorld& w = which == Small ? small_ : big_;
    out.GW = w.GW; out.GH = w.GH;
    out.g = w.g;
    out.age = w.age;
    out.dying = w.dying;
    out.gen = w.gen;
    out.alive = w.alive;
}

void LifeEngine::setCellLocked(LifeWorld& w, int i, bool v, double now) {
    if (i < 0 || i >= w.N) return;
    if (v && !w.g[(size_t)i]) { w.g[(size_t)i] = 1; w.age[(size_t)i] = 1; }
    else if (!v && w.g[(size_t)i]) { w.g[(size_t)i] = 0; w.age[(size_t)i] = 0; w.dying.push_back({i, now}); }
}

void LifeEngine::setCell(World which, int i, bool v) {
    std::lock_guard<std::mutex> lk(m_);
    LifeWorld& w = world(which);
    if (which == Small) {
        if (i < 0 || i >= kWinW * kWinH) return;
        i = toWorld(i);
    }
    setCellLocked(w, i, v, wallMs());
    count(w);
    publishLocked();
}

void LifeEngine::paint(World which, int x, int y, int size, bool erase) {
    std::lock_guard<std::mutex> lk(m_);
    LifeWorld& w = world(which);
    if (which == Small) { x += kWinX; y += kWinY; }
    const int r = size - 1;
    const double now = wallMs();
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            setCellLocked(w, ((y + dy + w.GH) % w.GH) * w.GW + ((x + dx + w.GW) % w.GW), !erase, now);
    count(w);
    publishLocked();
}

void LifeEngine::placeLocked(LifeWorld& w, const PatternInfo& p, int ox, int oy, double now) {
    for (auto& c : p.cells) {
        const int x = ((ox + c.first) % w.GW + w.GW) % w.GW, y = ((oy + c.second) % w.GH + w.GH) % w.GH;
        setCellLocked(w, y * w.GW + x, true, now);
    }
}

void LifeEngine::stamp(World which, Pattern pat, int cx, int cy) {
    std::lock_guard<std::mutex> lk(m_);
    LifeWorld& w = world(which);
    if (which == Small) { cx += kWinX; cy += kWinY; }
    const PatternInfo& p = patternInfo(pat);
    placeLocked(w, p, cx - p.w / 2, cy - p.h / 2, wallMs());
    count(w);
    publishLocked();
}

void LifeEngine::reset(World which) {
    std::lock_guard<std::mutex> lk(m_);
    LifeWorld& w = world(which);
    const float p = params_.get(kBirth);
    const double now = wallMs();
    for (int i = 0; i < w.N; ++i) {
        const bool v = frand() < p;
        setCellLocked(w, i, v, now);
        if (v) w.age[(size_t)i] = 1;  // une cellule déjà vivante repart aussi de zéro : le monde
                                      // tiré ne dépend pas de celui d'avant (même son à chaque chargement)
    }
    w.gen = 0;
    w.births = small_.births = 0.f;  // le taux de naissances du monde précédent ne déborde pas
    rnd_.store(0.f, std::memory_order_relaxed);
    w.since = 0.0;
    count(w);
    publishLocked();
}

void LifeEngine::clear(World which) {
    std::lock_guard<std::mutex> lk(m_);
    LifeWorld& w = world(which);
    const double now = wallMs();
    for (int i = 0; i < w.N; ++i) setCellLocked(w, i, false, now);
    w.gen = 0;
    count(w);
    publishLocked();
}

void LifeEngine::inject(World which, float amount) {
    std::lock_guard<std::mutex> lk(m_);
    LifeWorld& w = world(which);
    const double now = wallMs();
    const int n = (int)std::lround(amount * (float)w.N * 0.08f);
    for (int k = 0; k < n; ++k) setCellLocked(w, (int)(frand() * (float)w.N) % w.N, true, now);
    count(w);
    publishLocked();
}

void LifeEngine::seed(Pattern p) {
    std::lock_guard<std::mutex> lk(m_);
    LifeWorld& w = big_;
    const double now = wallMs();
    if (p == Pattern::Glider) {  // dans la zone écoutée
        placeLocked(w, patternInfo(p), kWinX + (int)(frand() * kWinW), kWinY + (int)(frand() * kWinH), now);
    } else {
        for (int i = 0; i < w.N; ++i) setCellLocked(w, i, false, now);
        if (p == Pattern::Pulsar) placeLocked(w, patternInfo(p), kWinX + 11, kWinY + 1, now);
        else placeLocked(w, patternInfo(Pattern::Gun), kWinX, kWinY + 3, now);
    }
    w.gen = 0;
    w.since = 0.0;
    count(w);
    publishLocked();
}

void LifeEngine::snapCapture() {
    std::lock_guard<std::mutex> lk(m_);
    int idx = -1;
    for (int k = 0; k < 8; ++k)
        if (!snaps_[(size_t)k]) { idx = k; break; }
    if (idx < 0 || snapSel_ >= 0) idx = snapNext_;
    snaps_[(size_t)idx] = LifeSnapshot{big_.g, big_.age, big_.gen};
    snapSel_ = idx;
    snapNext_ = (idx + 1) % 8;
}

void LifeEngine::applySnapLocked(int i, double now) {
    const auto& sn = snaps_[(size_t)i];
    if (!sn) { snapPend_ = -1; return; }
    for (int j = 0; j < big_.N; ++j)
        if (big_.g[(size_t)j] && !sn->g[(size_t)j]) big_.dying.push_back({j, now});
    big_.g = sn->g;
    big_.age = sn->age;
    big_.gen = sn->gen;
    big_.since = 0.0;
    snapSel_ = i;
    snapPend_ = -1;
    count(big_);
    publishLocked();
}

void LifeEngine::snapRecall(int i) {
    std::lock_guard<std::mutex> lk(m_);
    if (i < 0 || i >= 8 || !snaps_[(size_t)i]) return;
    if (snapSync_ && params_.get(kFreeze) < 0.5f) snapPend_ = i;
    else applySnapLocked(i, wallMs());
}

void LifeEngine::snapClearSelected() {
    std::lock_guard<std::mutex> lk(m_);
    if (snapSel_ >= 0) {
        snaps_[(size_t)snapSel_].reset();
        if (snapPend_ == snapSel_) snapPend_ = -1;
        snapSel_ = -1;
    }
}

void LifeEngine::setSnapSync(bool v) { std::lock_guard<std::mutex> lk(m_); snapSync_ = v; }
void LifeEngine::setSnapAuto(bool v) { std::lock_guard<std::mutex> lk(m_); snapAuto_ = v; }

void LifeEngine::snapState(SnapState& out) {
    std::lock_guard<std::mutex> lk(m_);
    for (int k = 0; k < 8; ++k) {
        out.filled[(size_t)k] = (bool)snaps_[(size_t)k];
        if (snaps_[(size_t)k]) out.thumbs[(size_t)k] = snaps_[(size_t)k]->g;
        else out.thumbs[(size_t)k].clear();
    }
    out.selected = snapSel_;
    out.pending = snapPend_;
    out.sync = snapSync_;
    out.autoCapture = snapAuto_;
}

// --- état ------------------------------------------------------------------------------------
namespace {
std::string packBits(const std::vector<uint8_t>& g) {
    std::vector<unsigned char> b((g.size() + 7) / 8, 0);
    for (size_t i = 0; i < g.size(); ++i)
        if (g[i]) b[i / 8] |= (unsigned char)(1u << (i % 8));
    return json::base64Encode(b.data(), b.size());
}
bool unpackBits(const std::string& s, std::vector<uint8_t>& g) {
    std::vector<unsigned char> b;
    if (!json::base64Decode(s, b) || b.size() * 8 < g.size()) return false;
    for (size_t i = 0; i < g.size(); ++i) g[i] = (b[i / 8] >> (i % 8)) & 1;
    return true;
}
std::string packAges(const std::vector<uint16_t>& a) {
    std::vector<unsigned char> b(a.size());
    for (size_t i = 0; i < a.size(); ++i) b[i] = (unsigned char)std::min<int>(255, a[i]);
    return json::base64Encode(b.data(), b.size());
}
void unpackAges(const std::string& s, std::vector<uint16_t>& a) {
    std::vector<unsigned char> b;
    if (!json::base64Decode(s, b)) return;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] = b[i];
}
} // namespace

std::string LifeEngine::saveState() {
    std::lock_guard<std::mutex> lk(m_);
    json::Value v = json::Value::object();
    auto worldJson = [](const LifeWorld& w) {
        json::Value o = json::Value::object();
        o.set("cells", packBits(w.g));
        o.set("ages", packAges(w.age));
        o.set("gen", (double)w.gen);
        return o;
    };
    v.set("big", worldJson(big_));  // un seul monde (la page SON en montre une fenêtre)
    json::Value sn = json::Value::array();
    for (auto& s : snaps_) {
        if (!s) { sn.push(json::Value()); continue; }
        json::Value o = json::Value::object();
        o.set("cells", packBits(s->g));
        o.set("ages", packAges(s->age));
        o.set("gen", (double)s->gen);
        sn.push(o);
    }
    v.set("snapshots", sn);
    v.set("snapSync", snapSync_);
    v.set("snapAuto", snapAuto_);
    v.set("soundWorld", soundWorld_.load());
    return v.dump();
}

void LifeEngine::loadState(const std::string& s) {
    json::Value v;
    if (!json::Value::parse(s, v) || !v.isObject()) return;
    std::lock_guard<std::mutex> lk(m_);
    auto load = [](LifeWorld& w, const json::Value& o) {
        if (!o.isObject()) return;
        std::vector<uint8_t> g((size_t)w.N);
        if (!unpackBits(o["cells"].str(), g)) return;
        w.g = g;
        w.age.assign((size_t)w.N, 0);
        unpackAges(o["ages"].str(), w.age);
        for (size_t i = 0; i < w.g.size(); ++i)
            if (w.g[i] && !w.age[i]) w.age[i] = 1;
        w.gen = (uint32_t)o["gen"].num(0);
        w.dying.clear();
    };
    load(big_, v["big"]);  // un ancien état gardait aussi un petit monde séparé : il est ignoré
    count(big_);
    const json::Value& sn = v["snapshots"];
    for (size_t k = 0; k < 8; ++k) {
        snaps_[k].reset();
        const json::Value& o = sn[k];
        if (!o.isObject()) continue;
        LifeSnapshot s2;
        s2.g.assign((size_t)big_.N, 0);
        if (!unpackBits(o["cells"].str(), s2.g)) continue;
        s2.age.assign((size_t)big_.N, 0);
        unpackAges(o["ages"].str(), s2.age);
        s2.gen = (uint32_t)o["gen"].num(0);
        snaps_[k] = std::move(s2);
    }
    snapSync_ = v["snapSync"].boolean(true);
    snapAuto_ = v["snapAuto"].boolean(false);
    snapSel_ = snapPend_ = -1;
    soundWorld_.store(v["soundWorld"].num(0) > 0.5 ? Big : Small);
    publishLocked();
}

} // namespace raw2
