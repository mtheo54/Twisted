// Fonds personnalisés sous Linux (outils de développement et tests) : PNG par Cairo seulement.
#include "platform/Media.h"
#include "platform/MediaScale.h"

#include <cairo.h>

namespace raw2::media {

bool decode(const std::string& path, int maxW, int maxH, int, float, Frames& out, std::string& error) {
    out = Frames{};
    if (isVideoFile(path)) {
        error = "vidéo non prise en charge sous Linux";
        return false;
    }
    cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        error = "image illisible (PNG seulement sous Linux)";
        return false;
    }
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
    int tw, th;
    fitSize(w, h, maxW, maxH, tw, th);
    out.w = tw;
    out.h = th;
    out.px.emplace_back((size_t)tw * th * 4);
    out.dur.push_back(1.f);
    scaleBGRA(cairo_image_surface_get_data(s), w, h, cairo_image_surface_get_stride(s), out.px[0].data(), tw, th);
    cairo_surface_destroy(s);
    return true;
}

} // namespace raw2::media
