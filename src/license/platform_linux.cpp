// Twisted - identité machine + dossier de données sous Linux (pas une cible de diffusion :
// sert à compiler et tester la GUI, cf. PlatformView_x11.cpp). Pas de réseau ici.
#if !defined(_WIN32) && !defined(__APPLE__)
#include <cstdlib>
#include <memory>
#include <fstream>
#include <string>
#include "manager.h"
#include "store.h"

namespace tw::license {

std::string platformMachineRaw() {
    // /etc/machine-id est stable et déjà anonyme ; on le hache ensuite de toute façon.
    std::ifstream f("/etc/machine-id");
    std::string id;
    if (f) std::getline(f, id);
    if (id.empty()) { const char* h = std::getenv("HOSTNAME"); if (h) id = h; }
    return id.empty() ? std::string("twisted-unknown-machine") : id;
}

std::string platformDeviceName() {
    const char* h = std::getenv("HOSTNAME");
    return h ? std::string(h) : std::string();
}

std::string platformDataDir() {
    if (const char* x = std::getenv("XDG_DATA_HOME")) return std::string(x) + "/Twisted";
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/share/Twisted";
    return "/tmp/Twisted";
}

// Pas de réseau sur cette plateforme : tout appel renvoie "hors ligne".
std::unique_ptr<Net> makePlatformNet() { return nullptr; }

} // namespace tw::license
#endif
