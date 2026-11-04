#pragma once
// Accès aux polices embarquées (tableaux générés par cmake/EmbedFiles.cmake).
#include "ui/Canvas.h"

#include <cstddef>

namespace raw2::embedded {
struct File {
    const char* name;
    const unsigned char* data;
    size_t size;
};
extern const File kFiles[];
} // namespace raw2::embedded

namespace raw2::ui {
// Fichier TTF de chaque face (nullptr si absent).
const embedded::File* fontFile(Face f);
} // namespace raw2::ui
