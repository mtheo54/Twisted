#include "ui/Backdrop.h"

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
} // namespace

Backdrop::~Backdrop() { join(); }

void Backdrop::join() {
    if (th_.joinable()) th_.join();
}

void Backdrop::request(const std::string& path) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (path == path_) return;
        path_ = path;
        err_.clear();
        cur_.reset();
        t0_ = -1.0;
    }
    if (path.empty()) return;
    {
        std::lock_guard<std::mutex> lk(gCacheMu);
        auto it = gCache.find(path);
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
    th_ = std::thread([this, path] {
        auto fr = std::make_shared<media::Frames>();
        std::string err;
        const bool vid = media::isVideoFile(path);
        const bool ok = media::decode(path, vid ? kVidW : kMaxW, vid ? kVidH : kMaxH, vid ? kMaxVideoFrames : kMaxGifFrames, kFps, *fr, err);
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (path == path_) {
                if (ok) {
                    cur_ = fr;
                    serial_ = gSerial.fetch_add(1);
                    std::lock_guard<std::mutex> lk2(gCacheMu);
                    gCache[path] = cur_;
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

bool Backdrop::frame(double nowMs, ImageView& out, std::shared_ptr<const media::Frames>& keep) {
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
    return true;
}

} // namespace raw2::ui
