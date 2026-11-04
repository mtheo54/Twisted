#include "core/Samples.h"

#include <algorithm>
#include <cmath>

namespace raw2 {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxSeconds = 6.0;  // 37 versions STRETCH de 6 s : environ 40 Mo par oscillateur

// passe-bas RBJ (Q 0.7071), en place
void lowpass(std::vector<float>& x, double fc, double sr) {
    const double w0 = 2.0 * kPi * std::min(fc, sr * 0.49) / sr, al = std::sin(w0) / (2.0 * 0.7071), cs = std::cos(w0);
    const double b0 = (1.0 - cs) / 2.0, b1 = 1.0 - cs, b2 = b0, a0 = 1.0 + al, a1 = -2.0 * cs, a2 = 1.0 - al;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (float& s : x) {
        const double v = (b0 * s + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = s; y2 = y1; y1 = v;
        s = (float)v;
    }
}

// étirement temporel WSOLA (fenêtre 1024, pas 512, recherche ±256), facteur alpha
std::vector<float> wsola(const std::vector<float>& x, double alpha, const std::atomic<bool>* cancel) {
    const int W = 1024, Hs = 512, tol = 256, N = (int)x.size();
    const int M = std::max(W, (int)std::ceil(N * alpha));
    const double Ha = Hs / alpha;
    std::vector<float> xp((size_t)(N + W + 2 * tol + 16), 0.f);
    const int O = tol;
    std::copy(x.begin(), x.end(), xp.begin() + O);
    std::vector<float> y((size_t)(M + W), 0.f), win((size_t)W);
    for (int i = 0; i < W; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / W));
    for (int i = 0; i < W; ++i) y[(size_t)i] += xp[(size_t)(O + i)] * (i < Hs ? 1.f : win[(size_t)i]);
    int prev = 0;
    for (int k = 1;; ++k) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        const int op = k * Hs;
        if (op >= M) break;
        const int nom = (int)std::lround(k * Ha);
        if (nom > N) break;
        const int nat = std::min(N, prev + Hs);
        int best = nom;
        double bc = -1e30;
        for (int d = -tol; d <= tol; d += 2) {
            const int pos = nom + d;
            if (pos < 0 || pos > N) continue;
            double c = 0.0, e = 1e-9;
            const float* a = &xp[(size_t)(O + pos)];
            const float* b = &xp[(size_t)(O + nat)];
            for (int i = 0; i < Hs; i += 2) { c += (double)a[i] * b[i]; e += (double)a[i] * a[i]; }
            c /= std::sqrt(e);
            if (c > bc) { bc = c; best = pos; }
        }
        for (int i = 0; i < W; ++i) y[(size_t)(op + i)] += xp[(size_t)(O + best + i)] * win[(size_t)i];
        prev = best;
    }
    y.resize((size_t)M);
    return y;
}

std::vector<float> stretchImpl(const std::vector<float>& x, double sr, int d, const std::atomic<bool>* cancel) {
    const double r = std::pow(2.0, d / 12.0);
    const int N = (int)x.size();
    std::vector<float> y = wsola(x, r, cancel);
    if (r > 1.0) { lowpass(y, 0.46 * sr / r, sr); lowpass(y, 0.46 * sr / r, sr); }
    std::vector<float> out((size_t)N, 0.f);
    const int L = (int)y.size();
    auto at = [&](int k) { return k >= 0 && k < L ? y[(size_t)k] : 0.f; };
    for (int i = 0; i < N; ++i) {
        const double p = i * r;
        const int k = (int)std::floor(p);
        const float f = (float)(p - k);
        const float y0 = at(std::max(0, k - 1)), y1 = at(k), y2 = at(k + 1), y3 = at(k + 2);
        out[(size_t)i] = y1 + 0.5f * f * (y2 - y0 + f * (2.f * y0 - 5.f * y1 + 4.f * y2 - y3 + f * (3.f * (y1 - y2) + y3 - y0)));
    }
    const int fo = std::min(N, 96);
    for (int i = 0; i < fo; ++i) out[(size_t)(N - 1 - i)] *= (float)i / (float)fo;
    return out;
}
} // namespace

std::vector<float> SampleBank::pitchStretch(const std::vector<float>& x, double sr, int d) { return stretchImpl(x, sr, d, nullptr); }

SampleBank::SampleBank(bool offline) : offline_(offline) {
    if (!offline_) worker_ = std::thread([this] { workerMain(); });
}

SampleBank::~SampleBank() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        run_ = false;
        for (auto* s : todo_) s->cancel = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    for (auto& s : set_) {
        SampleSet* p = s.exchange(nullptr);
        if (p) retired_.push_back({p, 0, false});
    }
    collect(true);
}

void SampleBank::publish(int osc, SampleSet* s) {
    SampleSet* before = set_[(size_t)osc].exchange(s, std::memory_order_seq_cst);
    if (before) {
        before->cancel = true;
        const bool busy = inProcess_ ? inProcess_->load(std::memory_order_seq_cst) : false;
        const uint64_t ep = epoch_ ? epoch_->load(std::memory_order_seq_cst) : 0;
        std::lock_guard<std::mutex> lk(mu_);
        retired_.push_back({before, ep, busy && !offline_});
    }
    collect(false);
}

// Un son retiré pendant qu'un bloc audio tournait n'est libéré qu'après la fin de ce bloc.
// La tâche de fond ne libère jamais rien : elle travaille sur un son tant qu'il est dans todo_.
void SampleBank::collect(bool force) {
    std::lock_guard<std::mutex> lk(mu_);
    collectLocked(force);
}

void SampleBank::collectLocked(bool force) {
    const uint64_t ep = epoch_ ? epoch_->load(std::memory_order_seq_cst) : 0;
    for (size_t i = 0; i < retired_.size();) {
        const Retired& r = retired_[i];
        const bool inTodo = std::find(todo_.begin(), todo_.end(), r.s) != todo_.end();
        if (force || (!inTodo && (!r.busy || ep > r.epoch))) {
            delete r.s;
            retired_.erase(retired_.begin() + (long)i);
        } else {
            ++i;
        }
    }
}

bool SampleBank::load(int osc, const std::string& name, const std::vector<float>& data, double sr) {
    if (osc < 0 || osc > 1 || data.size() < 64 || !(sr > 1000.0)) return false;
    const size_t n = std::min(data.size(), (size_t)(sr * kMaxSeconds));
    float pk = 1e-9f;
    for (size_t i = 0; i < n; ++i) pk = std::max(pk, std::fabs(data[i]));
    size_t a = 0;
    while (a + 1 < n && std::fabs(data[a]) < 0.004f * pk) ++a;  // silence du début retiré
    a = a > 8 ? a - 8 : 0;
    auto* s = new SampleSet();
    s->name = name;
    s->sr = sr;
    s->base.resize(n - a);
    const float g = 0.95f / pk;
    for (size_t i = 0; i < s->base.size(); ++i) s->base[i] = data[a + i] * g;
    for (auto& v : s->ver) v.store(nullptr);
    if (offline_) {
        computeAll(s);
        publish(osc, s);
        return true;
    }
    publish(osc, s);
    {
        std::lock_guard<std::mutex> lk(mu_);
        todo_.push_back(s);
    }
    cv_.notify_all();
    return true;
}

void SampleBank::clear(int osc) {
    if (osc < 0 || osc > 1) return;
    SampleSet* before = set_[(size_t)osc].exchange(nullptr, std::memory_order_seq_cst);
    if (!before) return;
    before->cancel = true;
    const bool busy = inProcess_ ? inProcess_->load(std::memory_order_seq_cst) : false;
    const uint64_t ep = epoch_ ? epoch_->load(std::memory_order_seq_cst) : 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        retired_.push_back({before, ep, busy && !offline_});
    }
    collect(false);
}

std::string SampleBank::name(int osc) const {
    const SampleSet* s = set_[(size_t)osc].load();
    return s ? s->name : std::string();
}

double SampleBank::seconds(int osc) const {
    const SampleSet* s = set_[(size_t)osc].load();
    return s ? (double)s->base.size() / s->sr : 0.0;
}

int SampleBank::ready(int osc) const {
    const SampleSet* s = set_[(size_t)osc].load();
    return s ? s->ready.load() : 0;
}

// ordre de calcul de la maquette : vers l'aigu d'abord (+1..+24), puis vers le grave (-1..-12)
void SampleBank::computeAll(SampleSet* s) {
    std::vector<int> order;
    for (int d = 1; d <= SampleSet::kMaxD; ++d) order.push_back(d);
    for (int d = -1; d >= SampleSet::kMinD; --d) order.push_back(d);
    for (int d : order) {
        if (s->cancel.load()) return;
        const size_t k = (size_t)(d - SampleSet::kMinD);
        s->st[k] = stretchImpl(s->base, s->sr, d, &s->cancel);
        if (s->cancel.load()) return;
        s->len[k].store((int)s->st[k].size());
        s->ver[k].store(s->st[k].data(), std::memory_order_release);
        s->ready.fetch_add(1);
    }
}

void SampleBank::workerMain() {
    std::unique_lock<std::mutex> lk(mu_);
    while (run_) {
        cv_.wait(lk, [&] { return !run_ || !todo_.empty(); });
        if (!run_) break;
        SampleSet* s = todo_.front();
        lk.unlock();
        computeAll(s);  // le son reste dans todo_ : collect() ne le libère pas pendant le calcul
        lk.lock();
        todo_.erase(std::find(todo_.begin(), todo_.end(), s));
        collectLocked(false);
    }
}

json::Value SampleBank::save(int osc) const {
    const SampleSet* s = set_[(size_t)osc].load();
    if (!s) return json::Value();
    json::Value o = json::Value::object();
    o.set("name", s->name);
    o.set("sr", s->sr);
    std::vector<unsigned char> b(s->base.size() * 2);
    for (size_t i = 0; i < s->base.size(); ++i) {
        const int q = (int)std::lround(std::clamp(s->base[i], -1.f, 1.f) * 32767.f);
        b[2 * i] = (unsigned char)(q & 0xFF);
        b[2 * i + 1] = (unsigned char)((q >> 8) & 0xFF);
    }
    o.set("data", json::base64Encode(b.data(), b.size()));
    return o;
}

void SampleBank::loadJson(int osc, const json::Value& o) {
    if (!o.isObject()) { clear(osc); return; }
    std::vector<unsigned char> b;
    if (!json::base64Decode(o["data"].str(), b) || b.size() < 128) { clear(osc); return; }
    std::vector<float> d(b.size() / 2);
    for (size_t i = 0; i < d.size(); ++i) d[i] = (float)(int16_t)(b[2 * i] | (b[2 * i + 1] << 8)) / 32767.f;
    load(osc, o["name"].str(), d, o["sr"].num(48000.0));
}

} // namespace raw2
