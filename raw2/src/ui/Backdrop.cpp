#include "ui/Backdrop.h"
#include "platform/MediaScale.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace raw2::ui {

namespace {
// cache commun aux instances : un fichier n'est décodé et gardé qu'une fois
std::mutex gCacheMu;
std::map<std::string, std::weak_ptr<const media::Frames>> gCache;
std::atomic<unsigned long long> gSerial{1};

constexpr int kMaxW = 1600, kMaxH = 1200;   // image fixe : nette jusqu'au zoom 125 %
constexpr int kVidW = 480, kVidH = 360;     // vidéo : fond assombri, petite taille suffit
constexpr float kFps = 12.f;
constexpr int kMaxVideoFrames = 120;        // 10 s en boucle
constexpr int kMaxGifFrames = 150;
// flou de boîte séparable (3 passes ≈ gaussien), BGRA prémultiplié
void boxBlur(std::vector<unsigned char>& px, int w, int h, int r) {
    if (r <= 0 || w <= 0 || h <= 0) return;
    std::vector<int> tmp((size_t)w * h * 4);
    auto pass = [&](bool horiz) {
        const int n = horiz ? w : h, m = horiz ? h : w;
        for (int j = 0; j < m; ++j) {
            int acc[4] = {0, 0, 0, 0};
            auto at = [&](int i) -> const unsigned char* {
                i = std::clamp(i, 0, n - 1);
                return px.data() + (horiz ? ((size_t)j * w + i) : ((size_t)i * w + j)) * 4;
            };
            for (int i = -r; i <= r; ++i)
                for (int c = 0; c < 4; ++c) acc[c] += at(i)[c];
            for (int i = 0; i < n; ++i) {
                int* d = tmp.data() + (horiz ? ((size_t)j * w + i) : ((size_t)i * w + j)) * 4;
                for (int c = 0; c < 4; ++c) d[c] = acc[c] / (2 * r + 1);
                const unsigned char *in = at(i + r + 1), *out = at(i - r);
                for (int c = 0; c < 4; ++c) acc[c] += in[c] - out[c];
            }
        }
        for (size_t k = 0; k < px.size(); ++k) px[k] = (unsigned char)tmp[k];
    };
    for (int k = 0; k < 3; ++k) { pass(true); pass(false); }
}

void makeBlur(media::Frames& f, int q) {
    if (q <= 0 || f.w <= 0 || f.h <= 0) return;
    const float b = (float)q / 10.f;
    const int tw = std::max(16, (int)(f.w > 360 ? 360.f - 220.f * b : (float)f.w * (1.f - 0.6f * b)));
    media::fitSize(f.w, f.h, tw, tw, f.bw, f.bh);
    const int r = 1 + (int)std::lround(3.f * b);
    f.blur.clear();
    for (const auto& px : f.px) {
        std::vector<unsigned char> s((size_t)f.bw * f.bh * 4);
        media::scaleBGRA(px.data(), f.w, f.h, f.w * 4, s.data(), f.bw, f.bh);
        boxBlur(s, f.bw, f.bh, r);
        f.blur.push_back(std::move(s));
    }
}
} // namespace

Backdrop::~Backdrop() { join(); }

void Backdrop::join() {
    if (th_.joinable()) th_.join();
}

void Backdrop::request(const std::string& path, float blur) {
    const int q = std::clamp((int)std::lround(blur * 10.f), 0, 10);
    const std::string key = path + "|" + std::to_string(q);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (path == path_ && q == blurQ_) return;
        path_ = path;
        blurQ_ = q;
        err_.clear();
        cur_.reset();
        t0_ = -1.0;
    }
    if (path.empty()) return;
    {
        std::lock_guard<std::mutex> lk(gCacheMu);
        auto it = gCache.find(key);
        if (it != gCache.end())
            if (auto sp = it->second.lock()) {
                std::lock_guard<std::mutex> lk2(mu_);
                cur_ = sp;
                serial_ = gSerial.fetch_add(1);
                return;
            }
    }
    join();
    busy_ = true;
    th_ = std::thread([this, path, q, key] {
        auto fr = std::make_shared<media::Frames>();
        std::string err;
        const bool vid = media::isVideoFile(path);
        const bool ok = media::decode(path, vid ? kVidW : kMaxW, vid ? kVidH : kMaxH, vid ? kMaxVideoFrames : kMaxGifFrames, kFps, *fr, err);
        if (ok) makeBlur(*fr, q);
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (path == path_ && q == blurQ_) {
                if (ok) {
                    cur_ = fr;
                    serial_ = gSerial.fetch_add(1);
                    std::lock_guard<std::mutex> lk2(gCacheMu);
                    gCache[key] = cur_;
                } else {
                    err_ = err.empty() ? std::string("fond illisible") : err;
                }
            }
        }
        busy_ = false;
    });
}

std::string Backdrop::error() const {
    std::lock_guard<std::mutex> lk(mu_);
    return err_;
}

bool Backdrop::video() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cur_ && cur_->px.size() > 1;
}

bool Backdrop::frame(double nowMs, ImageView& out, std::shared_ptr<const media::Frames>& keep, ImageView* blurOut) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!cur_ || cur_->px.empty() || cur_->w <= 0) return false;
    const media::Frames& f = *cur_;
    size_t k = 0;
    if (f.px.size() > 1) {
        if (t0_ < 0.0) t0_ = nowMs;
        double total = 0.0;
        for (float d : f.dur) total += d;
        double t = total > 0.0 ? std::fmod((nowMs - t0_) / 1000.0, total) : 0.0;
        while (k + 1 < f.px.size() && t >= f.dur[k]) { t -= f.dur[k]; ++k; }
    }
    out.bgra = f.px[k].data();
    out.w = f.w;
    out.h = f.h;
    out.stride = f.w * 4;
    out.id = serial_ * 100000ull + k;
    keep = cur_;
    if (blurOut) {
        *blurOut = ImageView{};
        if (k < f.blur.size() && f.bw > 0) {
            blurOut->bgra = f.blur[k].data();
            blurOut->w = f.bw;
            blurOut->h = f.bh;
            blurOut->stride = f.bw * 4;
            blurOut->id = serial_ * 100000ull + 50000ull + k;
        }
    }
    return true;
}

} // namespace raw2::ui
