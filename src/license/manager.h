// Twisted - machine à états de la licence. Transpose plugin-client.js (check/activate/login/refresh/logout).
// Le réseau est injecté (interface Net) pour rester testable sans serveur et hors du thread audio.
#pragma once
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include "license.h"
#include "store.h"

namespace tw::license {

// ====== CONFIGURATION — à changer ici ======================================
// Adresse du serveur abrasion.
//   Production  : https://abrasion.dev (HTTPS obligatoire).
//   Développement : http://127.0.0.1:3000 (PAS "localhost", qui peut résoudre en IPv6
//                   alors que le serveur n'écoute qu'en IPv4).
// Définir TWISTED_DEV_BUILD pour une version de développement. Une version publiée
// (sans TWISTED_DEV_BUILD) ne contient que https://abrasion.dev.
#ifdef TWISTED_DEV_BUILD
#define TWISTED_ABRASION_URL "http://127.0.0.1:3000"
#else
#define TWISTED_ABRASION_URL "https://abrasion.dev"
#endif

// Garde-fou : http:// n'est toléré que pour 127.0.0.1 / localhost, et seulement en build dev.
// Toute autre adresse http:// doit être refusée avant le moindre appel réseau.
constexpr bool startsWith(const char* s, const char* pre) {
    while (*pre) { if (*s != *pre) return false; ++s; ++pre; }
    return true;
}
constexpr bool urlAllowed(const char* url) {
    if (!url) return false;
    if (startsWith(url, "https://")) return true;
#ifdef TWISTED_DEV_BUILD
    return startsWith(url, "http://127.0.0.1") || startsWith(url, "http://localhost");
#else
    return false; // en version publiée, seul https:// passe
#endif
}
static_assert(urlAllowed(TWISTED_ABRASION_URL),
    "TWISTED_ABRASION_URL non autorisee : en version publiee, seul https:// est accepte "
    "(definir TWISTED_DEV_BUILD pour utiliser http://127.0.0.1:3000 en developpement).");

// Clé publique Ed25519 du VRAI serveur (32 octets, base64), obtenue avec `node admin.js pubkey`.
// Le plugin ne contient JAMAIS la clé de test de test-vectors.json : celle-ci reste
// confinée au test automatique (tests/test_vectors.cpp lit la clé de chaque vecteur).
//
// >>> COLLER LA VRAIE CLÉ PUBLIQUE ICI, entre les guillemets. <<<
#ifndef TWISTED_SERVER_PUBLIC_KEY
#define TWISTED_SERVER_PUBLIC_KEY "HW3NsP3RJZOU5XxC+19JaUr/B0EBLkugWidQKPO+68Q="
#endif
// Garde-fou : tant que la clé est vide, la compilation s'arrête ici avec ce message,
// pour qu'aucune version ne parte sans vraie clé (ni, par accident, avec la clé de test).
static_assert(sizeof(TWISTED_SERVER_PUBLIC_KEY) - 1 == 44,
    "TWISTED_SERVER_PUBLIC_KEY est vide ou invalide : colle la vraie cle publique du serveur "
    "(32 octets en base64 = 44 caracteres, via 'node admin.js pubkey') dans manager.h.");
// ===========================================================================

constexpr long long kRefreshBefore = 3 * 86400; // renouvellement sous 3 jours

// État global de la licence, tel que l'interface et le DRM le lisent.
enum class LicState {
    NotActivated, // niveau 1 : découverte
    Valid,        // niveau 0 : rien
    Invalid,      // niveau 2 : tribunal (signature fausse, autre appareil/plugin, horloge reculée)
    Revoked       // licence retirée : message puis retour découverte
};

// Réponse d'un appel réseau, remplie par la couche plateforme (thread réseau).
struct NetResult {
    bool networkError = false;  // pas joignable (hors ligne, DNS, timeout)
    int status = 0;             // code HTTP si réponse reçue
    std::string body;           // corps JSON
};

// Interface réseau : implémentée par net_win.cpp / net_mac.mm, bouchée dans les tests.
struct Net {
    virtual ~Net() = default;
    // POST JSON sur baseUrl + path ; bearer facultatif. Bloquant : appelé depuis le thread réseau.
    virtual NetResult post(const std::string& path, const std::string& jsonBody, const std::string& bearer) = 0;
};

inline bool jsonSub(const std::string& j, const char* key, std::string& out) {
    return jsonField(j, key, out); // réutilise le lecteur de store.h
}

// Extrait license.payload et license.signature d'une réponse serveur.
inline bool extractSigned(const std::string& body, std::string& payload, std::string& signature) {
    // la réponse a la forme {"license":{"payload":"...","signature":"..."}, ...}
    size_t lp = body.find("\"license\"");
    if (lp == std::string::npos) return false;
    std::string sub = body.substr(lp);
    return jsonField(sub, "payload", payload) && jsonField(sub, "signature", signature);
}

class LicenseManager {
public:
    const char* plugin = "twisted";

    explicit LicenseManager(Net* net) : net_(net) { devId_ = deviceId(plugin); }

    const std::string& device() const { return devId_; }
    LicState state() const { return state_; }
    // Respect de la limite de débit : horodatage (secondes) avant lequel on ne retente aucun appel.
    long long networkPausedUntil() const { return pausedUntil_; }
    bool networkPaused(long long now) const { return now < pausedUntil_; }
    const std::string& userName() const { return user_; }
    long long expiry() const { return exp_; }
    const std::string& lastMessage() const { return message_; }

    // Vérifie la licence enregistrée. À appeler au lancement et périodiquement.
    // online=false force le mode hors ligne (jamais de réseau).
    LicState check(long long now, bool online) {
        store_ = loadStore(plugin);
        if (store_.empty()) return set(LicState::NotActivated, "");

        LicResult r = verify(now);
        if (!r.signatureValid || r.reason == LicReason::Plugin || r.reason == LicReason::Machine)
            return set(LicState::Invalid, "Licence non valide pour cet appareil.");

        // signature ok, bon plugin, bon appareil
        user_ = r.name; exp_ = r.exp;
        if (r.usable && r.exp - now > kRefreshBefore)
            return set(LicState::Valid, ""); // valable, aucun réseau nécessaire

        // bientôt expirée ou expirée : tenter un renouvellement si on est en ligne
        if (online && net_) {
            NetResult nr = store_.token.empty()
                ? callNet("/api/license/validate", deviceBody() + keyTail(), "", now)
                : callNet("/api/plugin/refresh", refreshBody(), store_.token, now);
            if (!nr.networkError) {
                if (nr.status == 200) {
                    std::string pay, sig;
                    if (extractSigned(nr.body, pay, sig)) {
                        store_.license_payload = pay; store_.license_signature = sig;
                        saveStore(plugin, store_);
                        LicResult r2 = verify(now);
                        if (r2.usable) { user_ = r2.name; exp_ = r2.exp; return set(LicState::Valid, ""); }
                    }
                } else if (nr.status == 403 || nr.status == 401 || nr.status == 404) {
                    // retirée / désactivée / introuvable
                    std::string code, msg; jsonField(nr.body, "error", code); jsonField(nr.body, "message", msg);
                    deleteStore(plugin); store_ = LicenseStore{};
                    if (code == "revoked") return set(LicState::Revoked, msg.empty() ? "Licence retirée." : msg);
                    return set(LicState::NotActivated, msg);
                }
                // autre code : on retombe sur la tolérance hors ligne ci-dessous
            }
        }
        // pas de réseau (ou erreur) : on tolère jusqu'à l'expiration
        if (r.usable) return set(LicState::Valid, "");
        return set(LicState::Invalid, "Licence expirée : connecte-toi à internet une fois.");
    }

    // Activer avec une clé. Renvoie true si la licence devient valide.
    bool activateWithKey(const std::string& key, long long now, std::string& errOut) {
        if (!net_) { errOut = "Pas de réseau disponible."; return false; }
        NetResult nr = callNet("/api/license/activate", deviceBody() + ",\"key\":\"" + jsonEscape(key) + "\"}", "", now);
        return accept(nr, now, key, "", "", errOut);
    }

    // Se connecter avec le compte. Le mot de passe n'est jamais stocké.
    bool loginWithAccount(const std::string& email, const std::string& password, long long now, std::string& errOut) {
        if (!net_) { errOut = "Pas de réseau disponible."; return false; }
        std::string body = deviceBody()
            + ",\"email\":\"" + jsonEscape(email) + "\",\"password\":\"" + jsonEscape(password) + "\"}";
        NetResult nr = callNet("/api/plugin/login", body, "", now);
        std::string token, user;
        if (nr.status == 200 && !nr.networkError) {
            jsonField(nr.body, "token", token);
            size_t up = nr.body.find("\"user\"");
            if (up != std::string::npos) { std::string sub = nr.body.substr(up); jsonField(sub, "name", user); }
        }
        return accept(nr, now, "", token, user, errOut);
    }

    // Libérer cet appareil et effacer la licence locale.
    void logout() {
        if (net_) {
            const long long now = (long long)std::time(nullptr);
            if (!store_.token.empty()) callNet("/api/plugin/logout", "{}", store_.token, now);
            else if (!store_.key.empty()) callNet("/api/license/deactivate", deviceBody() + keyTail(), "", now);
        }
        deleteStore(plugin);
        store_ = LicenseStore{};
        set(LicState::NotActivated, "");
    }

private:
    Net* net_;
    std::string devId_, user_, message_;
    long long pausedUntil_ = 0; // secondes ; 0 = pas de pause

    // Tout appel réseau passe par ici. Gère les limites du serveur :
    //  - 429 (trop de requêtes) : pause d'au moins 1 h, jamais de boucle.
    //  - 503 (serveur très sollicité) : courte pause 1-2 min, licence locale conservée.
    NetResult callNet(const std::string& path, const std::string& body, const std::string& bearer, long long now) {
        if (now < pausedUntil_) { NetResult r; r.networkError = true; return r; } // en pause : on ne tente pas
        NetResult r = net_->post(path, body, bearer);
        if (!r.networkError) {
            if (r.status == 429) pausedUntil_ = now + 3600;       // ≥ 1 h
            else if (r.status == 503) pausedUntil_ = now + 90;    // ~1,5 min, puis on réessaiera
        }
        return r;
    }
    long long exp_ = 0;
    LicState state_ = LicState::NotActivated;
    LicenseStore store_;

    LicState set(LicState s, const std::string& msg) { state_ = s; message_ = msg; return s; }

    LicResult verify(long long now) {
        return verifyLicense(store_.license_payload, store_.license_signature,
                             TWISTED_SERVER_PUBLIC_KEY, plugin, devId_, now);
    }

    std::string deviceBody() const {
        std::string b = "{\"plugin\":\"" + std::string(plugin) + "\",\"machine_id\":\"" + devId_ + "\"";
        std::string name = platformDeviceName();
        if (!name.empty()) b += ",\"machine_name\":\"" + jsonEscape(name) + "\"";
        return b; // volontairement sans '}' : complété par keyTail() ou un champ supplémentaire
    }
    std::string keyTail() const { return ",\"key\":\"" + jsonEscape(store_.key) + "\"}"; }
    std::string refreshBody() const {
        return "{\"plugin\":\"" + std::string(plugin) + "\",\"machine_id\":\"" + devId_ + "\"}";
    }

    bool accept(const NetResult& nr, long long now, const std::string& key,
                const std::string& token, const std::string& user, std::string& errOut) {
        if (nr.networkError) { errOut = "Serveur injoignable. Vérifie ta connexion (ou que le serveur local tourne)."; return false; }
        if (nr.status != 200) {
            std::string msg; jsonField(nr.body, "message", msg);
            errOut = msg.empty() ? ("Erreur " + std::to_string(nr.status)) : msg;
            return false;
        }
        std::string pay, sig;
        if (!extractSigned(nr.body, pay, sig)) { errOut = "Réponse du serveur illisible."; return false; }
        // On NE fait confiance à la réponse qu'après avoir vérifié la signature.
        LicResult r = verifyLicense(pay, sig, TWISTED_SERVER_PUBLIC_KEY, plugin, devId_, now);
        if (!r.signatureValid) { errOut = "Signature de licence invalide : réponse non fiable."; return false; }
        if (r.reason == LicReason::Machine || r.reason == LicReason::Plugin) {
            errOut = "Licence destinée à un autre appareil ou plugin."; return false;
        }
        store_ = LicenseStore{};
        store_.license_payload = pay; store_.license_signature = sig;
        store_.key = key.empty() && token.empty() ? store_.key : key; // clé seulement en activation par clé
        store_.key = key; store_.token = token; store_.user = user.empty() ? r.name : user;
        saveStore(plugin, store_);
        user_ = store_.user; exp_ = r.exp;
        if (!r.usable) { errOut = "Licence expirée."; set(LicState::Invalid, errOut); return false; }
        set(LicState::Valid, "");
        return true;
    }
};

} // namespace tw::license
