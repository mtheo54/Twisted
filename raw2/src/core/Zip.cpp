#include "core/Zip.h"

#include <cstdint>

namespace raw2::zip {

namespace {
uint32_t rd32(const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
void wr32(std::vector<unsigned char>& o, uint32_t v) { for (int k = 0; k < 4; ++k) o.push_back((unsigned char)(v >> (8 * k))); }
void wr16(std::vector<unsigned char>& o, uint16_t v) { o.push_back((unsigned char)v); o.push_back((unsigned char)(v >> 8)); }
} // namespace

uint32_t crc32(const unsigned char* p, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool read(const std::vector<unsigned char>& a, std::vector<Entry>& out, std::string& error) {
    out.clear();
    const size_t n = a.size();
    if (n < 22) { error = "archive vide ou abîmée"; return false; }
    // fin du répertoire central (cherchée depuis la fin, commentaire éventuel compris)
    size_t eocd = std::string::npos;
    for (size_t i = n - 22 + 1; i-- > 0 && n - i <= 22 + 65535;)
        if (rd32(&a[i]) == 0x06054b50u) { eocd = i; break; }
    if (eocd == std::string::npos) { error = "ce n'est pas une archive .raw2skin"; return false; }
    const uint16_t count = rd16(&a[eocd + 10]);
    size_t p = rd32(&a[eocd + 16]);
    for (uint16_t e = 0; e < count; ++e) {
        if (p + 46 > n || rd32(&a[p]) != 0x02014b50u) { error = "répertoire de l'archive abîmé"; return false; }
        const uint16_t method = rd16(&a[p + 10]);
        const uint32_t csize = rd32(&a[p + 20]), usize = rd32(&a[p + 24]);
        const uint16_t nl = rd16(&a[p + 28]), xl = rd16(&a[p + 30]), cl = rd16(&a[p + 32]);
        const uint32_t loc = rd32(&a[p + 42]), crc = rd32(&a[p + 16]);
        if (p + 46 + nl > n) { error = "répertoire de l'archive abîmé"; return false; }
        std::string name((const char*)&a[p + 46], nl);
        p += 46 + (size_t)nl + xl + cl;
        if (name.empty() || name.back() == '/') continue;
        if (method != 0) {
            error = "archive recompress\xC3\xA9" "e : exporte-la depuis ABRASION SKIN STUDIO ou d\xC3\xA9pose le dossier du skin";
            return false;
        }
        if (loc + 30 > n || rd32(&a[loc]) != 0x04034b50u) { error = "entrée de l'archive abîmée"; return false; }
        const size_t data = (size_t)loc + 30 + rd16(&a[loc + 26]) + rd16(&a[loc + 28]);
        if (csize != usize || data + csize > n) { error = "entrée de l'archive abîmée"; return false; }
        Entry en;
        en.name = name;
        en.data.assign(a.begin() + (long)data, a.begin() + (long)(data + csize));
        if (crc32(en.data.data(), en.data.size()) != crc) {
            error = "archive abîmée (somme de contrôle)";
            return false;
        }
        out.push_back(std::move(en));
    }
    return true;
}

std::vector<unsigned char> write(const std::vector<Entry>& entries) {
    std::vector<unsigned char> o, cd;
    for (const Entry& e : entries) {
        const uint32_t crc = crc32(e.data.data(), e.data.size()), sz = (uint32_t)e.data.size(), off = (uint32_t)o.size();
        const uint16_t nl = (uint16_t)e.name.size();
        wr32(o, 0x04034b50u); wr16(o, 20); wr16(o, 0x0800); wr16(o, 0); wr16(o, 0); wr16(o, 0x21);
        wr32(o, crc); wr32(o, sz); wr32(o, sz); wr16(o, nl); wr16(o, 0);
        o.insert(o.end(), e.name.begin(), e.name.end());
        o.insert(o.end(), e.data.begin(), e.data.end());
        wr32(cd, 0x02014b50u); wr16(cd, 20); wr16(cd, 20); wr16(cd, 0x0800); wr16(cd, 0); wr16(cd, 0); wr16(cd, 0x21);
        wr32(cd, crc); wr32(cd, sz); wr32(cd, sz); wr16(cd, nl); wr16(cd, 0); wr16(cd, 0); wr16(cd, 0); wr16(cd, 0);
        wr32(cd, 0); wr32(cd, off);
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }
    const uint32_t cdOff = (uint32_t)o.size(), cdSize = (uint32_t)cd.size();
    o.insert(o.end(), cd.begin(), cd.end());
    wr32(o, 0x06054b50u); wr16(o, 0); wr16(o, 0); wr16(o, (uint16_t)entries.size()); wr16(o, (uint16_t)entries.size());
    wr32(o, cdSize); wr32(o, cdOff); wr16(o, 0);
    return o;
}

} // namespace raw2::zip
