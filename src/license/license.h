// Twisted - vérification d'une licence signée, hors ligne.
// Reproduit exactement la logique de plugin-client.js / PLUGIN-LICENCE.md.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "ed25519_verify.h"

namespace tw::license {

// ---- base64 (décodage uniquement) ----
inline bool b64decode(const char* s, size_t n, unsigned char* out, size_t cap, size_t& outLen) {
    static int T[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i) T[i] = -1;
        const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) T[(unsigned char)A[i]] = i;
        init = true;
    }
    int val = 0, bits = -8;
    outLen = 0;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '=') break;
        if (c == '\n' || c == '\r' || c == ' ') continue;
        const int d = T[c];
        if (d < 0) return false; // caractère invalide
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 0) {
            if (outLen >= cap) return false;
            out[outLen++] = (unsigned char)((val >> bits) & 0xff);
            bits -= 8;
        }
    }
    return true;
}

// ---- résultat de la vérification ----
enum class LicReason { Ok, Signature, Plugin, Machine, Expired, Malformed };

struct LicResult {
    bool signatureValid = false;
    bool usable = false;
    LicReason reason = LicReason::Malformed;
    // champs lus dans le payload (valides seulement si signatureValid)
    std::string licenseKey, name, machine, plugin;
    long long iat = 0, exp = 0;
};

inline const char* reasonCode(LicReason r) {
    switch (r) {
        case LicReason::Ok: return "ok";
        case LicReason::Signature: return "signature";
        case LicReason::Plugin: return "plugin";
        case LicReason::Machine: return "machine";
        case LicReason::Expired: return "expired";
        default: return "malformed";
    }
}

// ---- mini lecteur JSON pour le payload (plat : {"clé":valeur}) ----
// On ne lit le JSON qu'APRÈS avoir validé la signature ; on ne cherche donc que
// des champs connus dans un texte de confiance, pas un parseur JSON général.
inline bool jsonStr(const std::string& j, const char* key, std::string& out) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return false;
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
    if (p >= j.size() || j[p] != '"') return false;
    ++p;
    out.clear();
    while (p < j.size() && j[p] != '"') {
        if (j[p] == '\\' && p + 1 < j.size()) { out.push_back(j[p + 1]); p += 2; }
        else out.push_back(j[p++]);
    }
    return p < j.size();
}
inline bool jsonInt(const std::string& j, const char* key, long long& out) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return false;
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
    bool neg = false;
    if (p < j.size() && (j[p] == '-' || j[p] == '+')) { neg = j[p] == '-'; ++p; }
    if (p >= j.size() || j[p] < '0' || j[p] > '9') return false;
    long long v = 0;
    int digits = 0;
    while (p < j.size() && j[p] >= '0' && j[p] <= '9') {
        if (++digits > 18) v = 9223372036854775807LL; // plafonne : pas d'overflow silencieux
        else v = v * 10 + (j[p] - '0');
        ++p;
    }
    out = neg ? -v : v;
    return true;
}

// Vérifie une licence signée.
//   payload   : le texte JSON EXACT reçu (jamais réécrit avant vérification)
//   sigB64    : la signature, 64 octets en base64
//   pubB64    : la clé publique du serveur, 32 octets en base64
//   expectPlugin  : "twisted"
//   expectMachine : l'identifiant de CET appareil
//   now       : l'heure courante en secondes depuis 1970
inline LicResult verifyLicense(const std::string& payload, const std::string& sigB64,
                               const std::string& pubB64, const char* expectPlugin,
                               const std::string& expectMachine, long long now) {
    LicResult r;
    unsigned char sig[80], pub[40];
    size_t sigLen = 0, pubLen = 0;
    const bool okSig = b64decode(sigB64.data(), sigB64.size(), sig, sizeof(sig), sigLen);
    const bool okPub = b64decode(pubB64.data(), pubB64.size(), pub, sizeof(pub), pubLen);

    if (!okSig || !okPub || sigLen != 64 || pubLen != 32 ||
        !ed::verify(sig, reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), pub)) {
        r.signatureValid = false;
        r.usable = false;
        r.reason = LicReason::Signature;
        return r;
    }
    r.signatureValid = true;

    // Signature valide : on peut lire le payload en confiance.
    if (!jsonStr(payload, "plugin", r.plugin) || !jsonStr(payload, "machine", r.machine) ||
        !jsonInt(payload, "exp", r.exp)) {
        r.usable = false;
        r.reason = LicReason::Malformed;
        return r;
    }
    jsonStr(payload, "license", r.licenseKey);
    jsonStr(payload, "name", r.name);
    jsonInt(payload, "iat", r.iat);

    // Même ordre de contrôle que le client de référence.
    if (r.plugin != expectPlugin) { r.reason = LicReason::Plugin; return r; }
    if (r.machine != expectMachine) { r.reason = LicReason::Machine; return r; }
    if (r.exp <= now) { r.reason = LicReason::Expired; return r; }

    r.usable = true;
    r.reason = LicReason::Ok;
    return r;
}

} // namespace tw::license
