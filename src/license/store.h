// Twisted - identifiant d'appareil anonyme + lecture/écriture du fichier de licence.
// Aucune dépendance. L'ID machine système est fourni par la plateforme (device_id.*).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include "ed25519_verify.h" // pour Sha512 — on dérive SHA-256 ci-dessous

namespace tw::license {

// ---- SHA-256 (FIPS 180-4), autonome ----
struct Sha256 {
    uint32_t h[8];
    uint64_t len = 0;
    unsigned char buf[64];
    int n = 0;
    static uint32_t rotr(uint32_t x, int c) { return (x >> c) | (x << (32 - c)); }
    void init() {
        static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
        for (int i = 0; i < 8; ++i) h[i] = iv[i];
        len = 0; n = 0;
    }
    void block(const unsigned char* p) {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (uint32_t(p[i*4])<<24)|(uint32_t(p[i*4+1])<<16)|(uint32_t(p[i*4+2])<<8)|uint32_t(p[i*4+3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    void update(const unsigned char* p, size_t l) {
        len += l;
        while (l) {
            int take = 64 - n; if (take > int(l)) take = int(l);
            for (int i = 0; i < take; ++i) buf[n+i] = p[i];
            n += take; p += take; l -= size_t(take);
            if (n == 64) { block(buf); n = 0; }
        }
    }
    void final(unsigned char out[32]) {
        uint64_t bits = len * 8;
        unsigned char pad = 0x80; update(&pad, 1);
        unsigned char z = 0; while (n != 56) update(&z, 1);
        unsigned char lb[8]; for (int i = 0; i < 8; ++i) lb[7-i] = (unsigned char)(bits >> (8*i));
        update(lb, 8);
        for (int i = 0; i < 8; ++i) for (int j = 0; j < 4; ++j) out[i*4+j] = (unsigned char)(h[i] >> (24 - 8*j));
    }
};

inline std::string sha256hex(const std::string& in) {
    unsigned char d[32];
    Sha256 s; s.init(); s.update((const unsigned char*)in.data(), in.size()); s.final(d);
    static const char* H = "0123456789abcdef";
    std::string o; o.reserve(64);
    for (int i = 0; i < 32; ++i) { o.push_back(H[d[i] >> 4]); o.push_back(H[d[i] & 15]); }
    return o;
}

// Fournis par la plateforme (device_id_win.cpp / _mac.mm / _stub.cpp) :
//   platformMachineRaw() : une chaîne stable propre à la machine (jamais envoyée telle quelle)
//   platformDeviceName() : nom lisible de l'appareil (facultatif, peut être vide)
//   platformDataDir()    : dossier de données utilisateur où ranger le fichier de licence
std::string platformMachineRaw();
std::string platformDeviceName();
std::string platformDataDir();

// Identifiant d'appareil : empreinte anonyme, 64 hex. Jamais la donnée brute.
inline std::string deviceId(const char* plugin) {
    return sha256hex(platformMachineRaw() + "|" + plugin);
}

// ---- fichier de licence (petit JSON plat écrit/lu par nous-mêmes) ----
struct LicenseStore {
    std::string license_payload, license_signature; // la licence signée
    std::string key;    // si activation par clé
    std::string token;  // si connexion par compte
    std::string user;   // nom affiché (facultatif)
    bool loaded = false;

    bool empty() const { return license_payload.empty() || license_signature.empty(); }
};

inline std::string jsonEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else o.push_back(c);
    }
    return o;
}

inline std::string licensePath(const char* plugin) {
    std::string dir = platformDataDir();
    if (dir.empty()) return {};
#if defined(_WIN32)
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (dir.back() != sep) dir.push_back(sep);
    return dir + "license-" + plugin + ".json";
}

inline bool saveStore(const char* plugin, const LicenseStore& s) {
    const std::string path = licensePath(plugin);
    if (path.empty()) return false;
    std::string j = "{";
    j += "\"payload\":\"" + jsonEscape(s.license_payload) + "\",";
    j += "\"signature\":\"" + jsonEscape(s.license_signature) + "\"";
    if (!s.key.empty())   j += ",\"key\":\"" + jsonEscape(s.key) + "\"";
    if (!s.token.empty()) j += ",\"token\":\"" + jsonEscape(s.token) + "\"";
    if (!s.user.empty())  j += ",\"user\":\"" + jsonEscape(s.user) + "\"";
    j += "}";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(j.data(), 1, j.size(), f) == j.size();
    std::fclose(f);
    return ok;
}

inline bool deleteStore(const char* plugin) {
    const std::string path = licensePath(plugin);
    if (path.empty()) return false;
    return std::remove(path.c_str()) == 0;
}

// Lecteur JSON plat, réservé à NOTRE fichier (clés connues, contenu de confiance).
inline bool jsonField(const std::string& j, const char* key, std::string& out) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return false;
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
    if (p >= j.size() || j[p] != '"') return false;
    ++p; out.clear();
    while (p < j.size() && j[p] != '"') {
        if (j[p] == '\\' && p + 1 < j.size()) {
            char c = j[p+1];
            out.push_back(c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c);
            p += 2;
        } else out.push_back(j[p++]);
    }
    return p < j.size();
}

inline LicenseStore loadStore(const char* plugin) {
    LicenseStore s;
    const std::string path = licensePath(plugin);
    if (path.empty()) return s;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return s;
    std::string j;
    char tmp[4096]; size_t r;
    while ((r = std::fread(tmp, 1, sizeof(tmp), f)) > 0) j.append(tmp, r);
    std::fclose(f);
    if (!jsonField(j, "payload", s.license_payload) || !jsonField(j, "signature", s.license_signature)) return s;
    jsonField(j, "key", s.key);
    jsonField(j, "token", s.token);
    jsonField(j, "user", s.user);
    s.loaded = true;
    return s;
}

} // namespace tw::license
