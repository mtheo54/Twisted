// Vérifie qu'une licence signée pour un AUTRE produit (RAW) ne débloque jamais Twisted,
// même dans le pire cas : posée directement dans le fichier de stockage de Twisted
// (dossier partagé par accident, fichier copié à la main, etc.).
//
// Compiler et lancer :
//   g++ -std=c++17 -O2 -pthread -I.. test_cross_product.cpp -o test_cross_product
//   ./test_cross_product
//
// La clé publique et les licences ci-dessous sont des clés de TEST générées pour ce fichier,
// jamais celles du vrai serveur abrasion.dev.
#define TWISTED_SERVER_PUBLIC_KEY "GMHkE/Bu7008L6041kFHPfZGq2umsus4WfbZjlaQOsU="
#include "../src/license/controller.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <chrono>

using namespace tw::license;

namespace tw::license {
std::string gMachine = "CROSS-MACHINE";
std::string platformMachineRaw() { return gMachine; }
std::string platformDeviceName() { return ""; }
std::string platformDataDir() { return "/tmp/tw-cross-test"; }
struct N : Net {
    NetResult post(const std::string&, const std::string&, const std::string&) override {
        NetResult r; r.networkError = true; return r;
    }
};
std::unique_ptr<Net> makePlatformNet() { return std::make_unique<N>(); }
} // namespace tw::license

static void put(const std::string& pay, const std::string& sig) {
    LicenseStore s;
    s.license_payload = pay;
    s.license_signature = sig;
    s.key = "X";
    saveStore("twisted", s);
}

static const char* nm(Drm d) {
    switch (d) {
        case Drm::Valid: return "VALID";
        case Drm::Discover: return "DECOUVERTE";
        case Drm::Invalid: return "INVALIDE";
        case Drm::Brut: return "BRUT";
        case Drm::Revoked: return "REVOQUEE";
    }
    return "?";
}

static const char* RAWLIC_PAY = "{\"v\":1,\"plugin\":\"raw\",\"license\":\"RAW-REAL-OWNER\",\"machine\":\"073e293ead0d40898142827c1e3532b9ecaa97d1045139c8246ceeca2d8817bc\",\"name\":\"Omyll\",\"iat\":1789913600,\"exp\":2000000000}";
static const char* RAWLIC_SIG = "MiD1TtpkulM4jgvfC4wOppteM8PIpfYb7OocXwDV2QQOn/idrgUAvg2p4RJiwWiTQu+MWICwRjIXpnQmnElBAA==";
static const char* TWLIC_PAY = "{\"v\":1,\"plugin\":\"twisted\",\"license\":\"TWISTED-REAL-OWNER\",\"machine\":\"01c167a9c17d1eab5e127c7ad7b84e25ef55ecf35047145575f83de39ce2e5f5\",\"name\":\"Omyll\",\"iat\":1789913600,\"exp\":2000000000}";
static const char* TWLIC_SIG = "upY4Qr5UgFRkEP7+ndLyqVdx32Pe3tCg3vTAm5vj30UbRXEWKK/ii7kTV6UL21FhIx4GVfUXsVdiTrjTJoO9DA==";

int main() {
    system("rm -rf /tmp/tw-cross-test && mkdir -p /tmp/tw-cross-test");
    int bad = 0;

    // 1) Le scénario du bug signalé : une licence RAW authentique et valide, posée
    //    DANS le fichier de licence de Twisted lui-même (pire cas possible).
    put(RAWLIC_PAY, RAWLIC_SIG);
    {
        LicenseController c;
        c.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const Drm d = c.drm();
        const bool ok = d != Drm::Valid;
        printf("%s  licence RAW valide posee dans le fichier Twisted -> %s (attendu DECOUVERTE/INVALIDE, jamais VALID)\n",
               ok ? "OK  " : "FAIL", nm(d));
        if (!ok) bad++;
        c.stop();
    }

    // 2) Témoin : une vraie licence Twisted, même mécanisme, doit elle passer.
    system("rm -rf /tmp/tw-cross-test && mkdir -p /tmp/tw-cross-test");
    put(TWLIC_PAY, TWLIC_SIG);
    {
        LicenseController c;
        c.start();
        Drm d = Drm::Discover;
        for (int i = 0; i < 200; ++i) {
            d = c.drm();
            if (d == Drm::Valid) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const bool ok = d == Drm::Valid;
        printf("%s  licence TWISTED valide (temoin) -> %s (attendu VALID)\n", ok ? "OK  " : "FAIL", nm(d));
        if (!ok) bad++;
        c.stop();
    }

    printf(bad ? "\n%d ECHEC\n" : "\nTOUT OK : une licence d'un autre produit ne debloque jamais Twisted\n", bad);
    return bad;
}
