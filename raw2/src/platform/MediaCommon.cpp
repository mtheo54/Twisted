// Fonds personnalisés : formats reconnus et réduction d'image commune à tous les systèmes.
#include "platform/Media.h"
#include "platform/MediaScale.h"

#include <algorithm>
#include <cctype>

namespace raw2::media {

static std::string ext(const std::string& p) {
    const size_t d = p.find_last_of('.');
    if (d == std::string::npos) return {};
    std::string e = p.substr(d + 1);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

bool isVideoFile(const std::string& path) {
    const std::string e = ext(path);
    return e == "mp4" || e == "mov" || e == "m4v" || e == "wmv" || e == "avi";
}

bool isMediaFile(const std::string& path) {
    const std::string e = ext(path);
    return isVideoFile(path) || e == "png" || e == "jpg" || e == "jpeg" || e == "gif" || e == "bmp" || e == "tif" ||
           e == "tiff" || e == "webp" || e == "heic";
}

void fitSize(int w, int h, int maxW, int maxH, int& tw, int& th) {
    const double k = std::min({1.0, (double)maxW / std::max(1, w), (double)maxH / std::max(1, h)});
    tw = std::max(1, (int)(w * k + 0.5));
    th = std::max(1, (int)(h * k + 0.5));
}

// réduction par moyenne de zone (pas de crénelage), BGRA prémultiplié
void scaleBGRA(const unsigned char* src, int sw, int sh, int sstride, unsigned char* dst, int dw, int dh) {
    const double fx = (double)sw / dw, fy = (double)sh / dh;
    for (int y = 0; y < dh; ++y) {
        const int y0 = (int)(y * fy), y1 = std::max(y0 + 1, std::min(sh, (int)((y + 1) * fy)));
        for (int x = 0; x < dw; ++x) {
            const int x0 = (int)(x * fx), x1 = std::max(x0 + 1, std::min(sw, (int)((x + 1) * fx)));
            unsigned acc[4] = {0, 0, 0, 0};
            for (int yy = y0; yy < y1; ++yy) {
                const unsigned char* p = src + (size_t)yy * sstride + (size_t)x0 * 4;
                for (int xx = x0; xx < x1; ++xx, p += 4)
                    for (int c = 0; c < 4; ++c) acc[c] += p[c];
            }
            const unsigned n = (unsigned)((y1 - y0) * (x1 - x0));
            unsigned char* q = dst + ((size_t)y * dw + x) * 4;
            for (int c = 0; c < 4; ++c) q[c] = (unsigned char)(acc[c] / n);
        }
    }
}

} // namespace raw2::media
