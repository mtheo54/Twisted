#pragma once
// ZIP minimal pour les skins (.raw2skin) : entrées « stockées » (sans compression), ce qu'écrit
// ABRASION SKIN STUDIO. Une archive recompressée (méthode deflate) est refusée avec un message clair :
// on peut alors déposer le dossier du skin tel quel.
#include <cstdint>
#include <string>
#include <vector>

namespace raw2::zip {

struct Entry {
    std::string name;
    std::vector<unsigned char> data;
};

// Lit toutes les entrées (les dossiers sont ignorés). Faux et message si l'archive est illisible.
bool read(const std::vector<unsigned char>& archive, std::vector<Entry>& out, std::string& error);
// Écrit une archive stockée (tests, export).
std::vector<unsigned char> write(const std::vector<Entry>& entries);
uint32_t crc32(const unsigned char* p, size_t n);

} // namespace raw2::zip
