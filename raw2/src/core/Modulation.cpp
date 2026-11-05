#include "core/Modulation.h"

#include <algorithm>
#include <cmath>

namespace raw2 {

// ---- routes libres --------------------------------------------------------------------------

int FreeRoutes::count() const {
    int n = 0;
    while (n < kMaxFreeRoutes && r[(size_t)n].src.load(std::memory_order_acquire) >= 0) ++n;
    return n;
}

int FreeRoutes::find(int src, uint32_t id) const {
    const int n = count();
    for (int i = 0; i < n; ++i)
        if (r[(size_t)i].src.load() == src && (uint32_t)r[(size_t)i].id.load() == id) return i;
    return -1;
}

int FreeRoutes::add(int src, uint32_t id, int amt) {
    if (src < 0 || src >= kNumRouteSrcs || !isModulatable(id)) return -1;
    // un LFO ne module pas ses propres réglages (comme la maquette)
    for (int f = 0; f < kLfFieldsPer && src < kNumLfos; ++f)
        if (lfoP(src, f) == id) return -1;
    const int e = find(src, id);
    if (e >= 0) return e;
    const int n = count();
    if (n >= kMaxFreeRoutes) return -1;
    FreeRoute& q = r[(size_t)n];
    q.id.store((int)id);
    q.amt.store(std::clamp(amt, -100, 100));
    q.src.store(src, std::memory_order_release);  // publiée en dernier : l'audio la lit entière
    return n;
}

void FreeRoutes::remove(int i) {
    const int n = count();
    if (i < 0 || i >= n) return;
    for (int k = i; k + 1 < n; ++k) {
        r[(size_t)k].id.store(r[(size_t)k + 1].id.load());
        r[(size_t)k].amt.store(r[(size_t)k + 1].amt.load());
        r[(size_t)k].src.store(r[(size_t)k + 1].src.load(), std::memory_order_release);
    }
    r[(size_t)n - 1].src.store(-1, std::memory_order_release);
}

void FreeRoutes::removeSource(int src) {
    for (int i = count() - 1; i >= 0; --i)
        if (r[(size_t)i].src.load() == src) remove(i);
}

void FreeRoutes::removeTarget(uint32_t first, uint32_t last) {
    for (int i = count() - 1; i >= 0; --i) {
        const uint32_t id = (uint32_t)r[(size_t)i].id.load();
        if (id >= first && id < last) remove(i);
    }
}

void FreeRoutes::clear() {
    for (auto& q : r) q.src.store(-1, std::memory_order_release);
}

json::Value FreeRoutes::save() const {
    json::Value a = json::Value::array();
    const int n = count();
    for (int i = 0; i < n; ++i) {
        json::Value o = json::Value::object();
        o.set("src", r[(size_t)i].src.load());
        o.set("param", paramDef((uint32_t)r[(size_t)i].id.load()).key);
        o.set("amt", r[(size_t)i].amt.load());
        a.push(o);
    }
    return a;
}

void FreeRoutes::load(const json::Value& v) {
    clear();
    if (!v.isArray()) return;
    for (const auto& o : v.items()) {
        const int id = paramIdFromKey(o["param"].str().c_str());
        if (id >= 0) add((int)o["src"].num(-1), (uint32_t)id, (int)o["amt"].num(50));
    }
}

// ---- formes dessinées -----------------------------------------------------------------------

LfoShapes::LfoShapes() {
    for (int l = 0; l < kNumLfos; ++l) reset(l);
}

void LfoShapes::reset(int lfo) {
    if (lfo < 0 || lfo >= kNumLfos) return;
    pts[(size_t)lfo].clear();
    edited[(size_t)lfo] = false;
    set(lfo, seed(0));
    pts[(size_t)lfo].clear();
}

// mêmes formes de départ que la maquette (S&H : suite pseudo-aléatoire fixe)
std::vector<LfoNode> LfoShapes::seed(int shape) {
    std::vector<LfoNode> a;
    auto N = [&](float x, float y) { a.push_back(LfoNode{x, y, 0.f, 0.f}); };
    switch (shape) {
        case kLfoSine:
            for (int i = 0; i <= 8; ++i) N((float)i / 8.f, std::sin(2.f * 3.14159265f * (float)i / 8.f));
            break;
        case kLfoTri: N(0.f, -1.f); N(0.5f, 1.f); N(1.f, -1.f); break;
        case kLfoSaw: N(0.f, -1.f); N(1.f, 1.f); break;
        case kLfoSqr: N(0.f, 1.f); N(0.495f, 1.f); N(0.505f, -1.f); N(1.f, -1.f); break;
        default: {
            long long x = 7;
            for (int i = 0; i < 8; ++i) {
                x = (x * 16807) % 2147483647;
                const float y = std::round(((float)x / 2147483647.f * 2.f - 1.f) * 8.f) / 8.f;
                N((float)i / 8.f, y);
                N((float)(i + 1) / 8.f - 0.01f, y);
            }
            N(1.f, a.back().y);
        }
    }
    return a;
}

float LfoShapes::segG(float t, float c, float s) {
    float u = c >= 0.f ? std::pow(t, 1.f + 3.f * c) : 1.f - std::pow(1.f - t, 1.f + 3.f * (-c));
    if (s > 0.f) u = u * (1.f - s) + s * u * u * (3.f - 2.f * u);
    return u;
}

float LfoShapes::eval(const std::vector<LfoNode>& p, float ph) {
    if (p.size() < 2) return 0.f;
    const float u = ph - std::floor(ph);
    for (size_t i = 0; i + 1 < p.size(); ++i) {
        if (u <= p[i + 1].x || i + 2 == p.size()) {
            const float d = p[i + 1].x - p[i].x;
            const float t = d > 1e-6f ? std::clamp((u - p[i].x) / d, 0.f, 1.f) : 1.f;
            return p[i].y + (p[i + 1].y - p[i].y) * segG(t, p[i].c, p[i].s);
        }
    }
    return 0.f;
}

void LfoShapes::set(int lfo, const std::vector<LfoNode>& p) {
    if (lfo < 0 || lfo >= kNumLfos || p.size() < 2) return;
    pts[(size_t)lfo] = p;
    const int next = 1 - cur_[(size_t)lfo].load(std::memory_order_relaxed);
    auto& t = tab_[(size_t)lfo][(size_t)next];
    for (int i = 0; i <= kLfoTab; ++i) t[(size_t)i].store(std::clamp(eval(p, (float)i / (float)kLfoTab * 0.999999f), -1.f, 1.f), std::memory_order_relaxed);
    cur_[(size_t)lfo].store(next, std::memory_order_release);
}

json::Value LfoShapes::save() const {
    json::Value a = json::Value::array();
    for (int l = 0; l < kNumLfos; ++l) {
        json::Value o = json::Value::object();
        o.set("snap", snap[(size_t)l]);
        o.set("edited", edited[(size_t)l]);
        if (l >= 2) o.set("grp", grp[(size_t)l - 2]);
        json::Value ps = json::Value::array();
        for (const LfoNode& n : pts[(size_t)l]) {
            json::Value q = json::Value::array();
            q.push(n.x); q.push(n.y); q.push(n.c); q.push(n.s);
            ps.push(q);
        }
        o.set("pts", ps);
        a.push(o);
    }
    return a;
}

void LfoShapes::load(const json::Value& v) {
    for (int l = 0; l < kNumLfos; ++l) {
        reset(l);
        snap[(size_t)l] = 0;
        if (l >= 2) grp[(size_t)l - 2] = 0;
    }
    if (!v.isArray()) return;
    for (int l = 0; l < kNumLfos && (size_t)l < v.size(); ++l) {
        const json::Value& o = v[(size_t)l];
        snap[(size_t)l] = std::clamp((int)o["snap"].num(0), 0, 2);
        if (l >= 2) grp[(size_t)l - 2] = std::clamp((int)o["grp"].num(0), 0, 3);
        std::vector<LfoNode> p;
        for (const auto& q : o["pts"].items()) {
            if (q.size() < 2 || p.size() >= 64) continue;
            LfoNode n{std::clamp((float)q[(size_t)0].num(), 0.f, 1.f), std::clamp((float)q[(size_t)1].num(), -1.f, 1.f),
                      std::clamp((float)q[(size_t)2].num(), -1.f, 1.f), std::clamp((float)q[(size_t)3].num(), 0.f, 1.f)};
            if (!p.empty() && n.x < p.back().x) n.x = p.back().x;
            p.push_back(n);
        }
        if (p.size() >= 2) {
            set(l, p);
            edited[(size_t)l] = o["edited"].boolean(true);
        }
    }
}

} // namespace raw2
