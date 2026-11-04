#include "ui/EmbeddedFonts.h"

#include <cstring>

namespace raw2::ui {

const embedded::File* fontFile(Face f) {
    const char* want = "JetBrainsMono-Regular.ttf";
    switch (f) {
        case Face::MonoMedium: want = "JetBrainsMono-Medium.ttf"; break;
        case Face::MonoBold: want = "JetBrainsMono-Bold.ttf"; break;
        case Face::Pixel: want = "Silkscreen-Regular.ttf"; break;
        default: break;
    }
    for (const embedded::File* p = embedded::kFiles; p->name; ++p)
        if (std::strcmp(p->name, want) == 0) return p;
    return nullptr;
}

} // namespace raw2::ui
