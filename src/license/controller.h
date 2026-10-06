// Twisted - contrôleur de licence de fond.
// Un seul thread s'occupe de tout ce qui touche la licence et le réseau.
// Le reste du plugin (audio compris) ne lit qu'un état déjà calculé, sans verrou.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include "manager.h"

namespace tw::license {

std::unique_ptr<Net> makePlatformNet(); // fournie par la plateforme

// Niveau de DRM vu par l'interface et le son. Rangé dans un atomic : lecture libre partout.
enum class Drm : int { Valid = 0, Discover = 1, Invalid = 2, Brut = 3, Revoked = 4 };

// Ordres que l'interface envoie au contrôleur (jamais d'appel réseau depuis l'interface).
enum class Cmd : uint8_t { None, ActivateKey, Login, Logout, OpenActivation, RecheckNow };

class LicenseController {
public:
    LicenseController() {
        net_ = makePlatformNet();
        mgr_ = std::make_unique<LicenseManager>(net_.get());
        devId_ = mgr_->device();
        drm_.store(int(Drm::Discover), std::memory_order_relaxed);
    }
    ~LicenseController() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this] { loop(); });
    }
    void stop() {
        if (!running_.exchange(false)) return;
        { std::lock_guard<std::mutex> lk(mx_); wake_ = true; }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    // ---- lu partout, sans verrou ----
    Drm drm() const { return Drm(drm_.load(std::memory_order_relaxed)); }
    // Adresse du serveur réellement compilée dans ce build (pour l'afficher et lever tout doute).
    static const char* serverUrl() { return TWISTED_ABRASION_URL; }
    // Le son n'est jamais coupé par la licence : Engine::process() tourne toujours
    // normalement, sans gate ni vérification (jamais de crypto/E-S dans le thread audio).
    // Seules la tour 3D et l'interface sont verrouillées.
    bool uiLocked() const { return drm() == Drm::Brut; } // interface remplacée par l'écran BRUT

    // Instantané lisible pour l'interface (protégé par un court verrou).
    // seq avance à CHAQUE publication (même si le message répète le mot pour mot
    // une tentative précédente) : l'appelant compare seq, jamais le texte du message,
    // pour savoir si une réponse neuve est arrivée. Comparer le texte ferait rester
    // l'interface bloquée sur "en cours…" si deux échecs de suite donnent le même message.
    struct Snapshot { Drm drm; std::string user, message; long long exp; uint64_t seq; };
    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lk(snapMx_);
        return snap_;
    }

    // ---- ordres de l'interface (déposés, exécutés sur le thread de fond) ----
    // Chacun renvoie le seq() d'AVANT l'envoi : l'appelant attend que snapshot().seq
    // dépasse cette valeur pour savoir qu'une réponse est arrivée (voir snapshot()).
    uint64_t requestActivateKey(const std::string& key) { push(Cmd::ActivateKey, key, ""); return respSeq_.load(std::memory_order_relaxed); }
    uint64_t requestLogin(const std::string& email, const std::string& pass) { push(Cmd::Login, email, pass); return respSeq_.load(std::memory_order_relaxed); }
    void requestLogout() { push(Cmd::Logout, "", ""); }
    void requestRecheck() { push(Cmd::RecheckNow, "", ""); }

    // ---- portes de licence : chaque partie du plugin VÉRIFIE elle-même avant d'agir ----
    // Au lieu d'un booléen partagé (facile à forcer d'un seul patch), chaque appelant
    // rappelle la vérification Ed25519 sur la licence stockée. Forcer le plugin à se croire
    // activé demande alors de patcher CHAQUE porte, pas une seule, et chacune déclenche BRUT.
    //   Interface : rappelée à intervalle régulier par Editor::tick (thread GUI).
    //   Tower     : rappelée une fois, au moment où la tour finit de se construire
    //               (passage de towerShown à true) — pas à chaque image.
    enum class Gate { Interface, Tower };

    // true = cette partie peut fonctionner. false = sa vérification a échoué.
    // Si l'état AFFICHÉ se dit "valide" alors qu'une porte échoue, c'est un contournement -> BRUT.
    bool gate(Gate who) {
        const bool ok = (computeToken(nowSec()) == kValidToken);
        if (ok) gatesSeen_.fetch_or(1u << int(who), std::memory_order_relaxed);
        else if (drm() == Drm::Valid) forceBrut();
        return ok;
    }

#ifdef TWISTED_TEST_FORCE_VALID
public:
    void testForceDrmValid() { drm_.store(int(Drm::Valid), std::memory_order_relaxed); }
#endif
private:
    std::unique_ptr<Net> net_;
    std::unique_ptr<LicenseManager> mgr_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> drm_{int(Drm::Discover)};
    std::atomic<unsigned> gatesSeen_{0};
    std::atomic<uint64_t> respSeq_{0};
    std::string devId_;
    // Valeur qu'un jeton de licence valide doit atteindre. Dérivée, pas 0/1 : empêche un "return 1" trivial.
    static constexpr uint64_t kValidToken = 0x5241570000000001ULL ^ 0x9E3779B97F4A7C15ULL;

    mutable std::mutex mx_;
    std::condition_variable cv_;
    bool wake_ = false;

    struct Order { Cmd cmd = Cmd::None; std::string a, b; };
    Order pending_;
    bool hasOrder_ = false;

    mutable std::mutex snapMx_;
    Snapshot snap_{Drm::Discover, "", "", 0, 0};

    std::mt19937 rng_{std::random_device{}()};

    static long long nowSec() { return (long long)std::time(nullptr); }

    void push(Cmd c, const std::string& a, const std::string& b) {
        { std::lock_guard<std::mutex> lk(mx_); pending_ = {c, a, b}; hasOrder_ = true; wake_ = true; }
        cv_.notify_all();
    }

    Drm toDrm(LicState s) const {
        switch (s) {
            case LicState::Valid: return Drm::Valid;
            case LicState::NotActivated: return Drm::Discover;
            case LicState::Revoked: return Drm::Revoked;
            default: return Drm::Invalid;
        }
    }

    void publish(Drm d) {
        // Vérifications de fond routinières : ne fait PAS avancer respSeq_. Si c'était le cas,
        // un contrôle périodique sans rapport pourrait faire croire à l'interface qu'une
        // activation qu'elle vient de soumettre a déjà répondu, avant que ce soit vrai.
        drm_.store(int(d), std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(snapMx_);
        snap_ = {d, mgr_->userName(), mgr_->lastMessage(), mgr_->expiry(), snap_.seq};
    }
    // Publie l'état ET un message, après une tentative d'activation/connexion : fait AVANCER
    // respSeq_. C'est la seule façon pour l'interface de savoir qu'une réponse est arrivée ;
    // les ordres sont traités un par un, donc la prochaine publishMsg() correspond forcément
    // à la demande en cours (l'interface n'en soumet pas une seconde pendant qu'une attend).
    void publishMsg(Drm d, const std::string& msg) {
        drm_.store(int(d), std::memory_order_relaxed);
        const uint64_t s = respSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
        std::lock_guard<std::mutex> lk(snapMx_);
        snap_ = {d, mgr_->userName(), msg, mgr_->expiry(), s};
    }

    // Relit la licence du disque et renvoie un jeton : kValidToken si et seulement si
    // signature + plugin + appareil + date sont bons ; 0 sinon. Appelé par chaque porte.
    uint64_t computeToken(long long now) {
        LicenseStore st = loadStore(mgr_->plugin);
        if (st.empty()) return 0;
        LicResult r = verifyLicense(st.license_payload, st.license_signature,
                                    TWISTED_SERVER_PUBLIC_KEY, mgr_->plugin, devId_, now);
        return r.usable ? kValidToken : 0;
    }
    void forceBrut() {
        drm_.store(int(Drm::Brut), std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(snapMx_);
        snap_.drm = Drm::Brut;
        if (snap_.message.empty()) snap_.message = "BRUT.";
    }

    // Une passe de vérification. detectBypass = true pour la vérif de fond.
    void verify(bool online, bool detectBypass) {
        const long long now = nowSec();
        LicState s = mgr_->check(now, online);
        Drm d = toDrm(s);

        // Détection de contournement, deux signaux indépendants :
        //  (a) une porte a tourné "en règle" alors que la licence signée n'est pas valide ;
        //  (b) l'état AFFICHÉ se dit "valide" alors que le jeton réel ne l'est pas
        //      (quelqu'un a forcé drm en mémoire sans licence).
        const unsigned seen = gatesSeen_.exchange(0, std::memory_order_relaxed);
        const bool displayLies = (drm() == Drm::Valid) && (computeToken(now) != kValidToken);
        if (detectBypass && ((seen != 0 && s != LicState::Valid) || displayLies)) {
            d = Drm::Brut;
        }
        publish(d);
    }

    void loop() {
        // Vérif immédiate au lancement (en ligne autorisé).
        verify(true, true);

        while (running_.load()) {
            // Attente 5 à 10 min, interrompue par un ordre ou l'arrêt.
            #ifdef TWISTED_TEST_FAST_RECHECK
            std::uniform_int_distribution<int> jitter(0, 0);
#else
            std::uniform_int_distribution<int> jitter(300, 600);
#endif
            const int waitSec = jitter(rng_);

            std::unique_lock<std::mutex> lk(mx_);
            cv_.wait_for(lk, std::chrono::seconds(waitSec), [this] { return wake_; });
            wake_ = false;
            Order ord;
            const bool got = hasOrder_;
            if (got) { ord = pending_; hasOrder_ = false; }
            lk.unlock();

            if (!running_.load()) break;

            if (got) {
                std::string err;
                switch (ord.cmd) {
                    case Cmd::ActivateKey:
                        if (mgr_->activateWithKey(ord.a, nowSec(), err)) publishMsg(Drm::Valid, "Activé. Merci !");
                        else publishMsg(toDrm(mgr_->state()), err);
                        break;
                    case Cmd::Login:
                        if (mgr_->loginWithAccount(ord.a, ord.b, nowSec(), err)) publishMsg(Drm::Valid, "Connecté. Merci !");
                        else publishMsg(toDrm(mgr_->state()), err);
                        break;
                    case Cmd::Logout:
                        mgr_->logout();
                        publish(Drm::Discover);
                        break;
                    case Cmd::RecheckNow:
                    case Cmd::OpenActivation:
                    default:
                        verify(true, true);
                        break;
                }
            } else {
                // Réveil normal : vérification de fond.
                verify(true, true);
            }
        }
    }

    void setMessage(const std::string& m) {
        std::lock_guard<std::mutex> lk(snapMx_);
        snap_.message = m;
    }
};

} // namespace tw::license
