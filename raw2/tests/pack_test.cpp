// Édition des packs par l'utilisateur : création d'un .raw2pack, ajout du son actuel, écrasement,
// renommage, ordre, infos, suppression, export et copie ; mêmes opérations sur un pack dossier.
// Tout se passe dans un dossier Documents temporaire (HOME redirigé), jamais chez l'utilisateur.
#include "core/FactoryData.h"
#include "core/Files.h"
#include "core/Model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
int main() {
    std::printf("pack_test : ignoré sous Windows (Documents non redirigeable)\n");
    return 0;
}
#else
#include <unistd.h>

using namespace raw2;

static int failures = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("ÉCHEC ligne %d : %s\n", __LINE__, #c);          \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static const Pack* packAt(Model& m, const std::string& path) {
    const int i = m.findPack(path);
    return i >= 0 ? &m.packs[(size_t)i] : nullptr;
}

int main() {
    char tmpl[] = "/tmp/raw2packXXXXXX";
    const char* home = mkdtemp(tmpl);
    if (!home) return 1;
    setenv("HOME", home, 1);
    const std::string root = files::raw2Dir();
    std::string msg;

    Model m(Model::Mode::Offline);
    m.scanPacks();

    // --- .raw2pack ----------------------------------------------------------------------------
    const int pi = m.createPack("MON PACK", msg);
    CHECK(pi >= 0);
    const std::string path = files::join(root, "MON PACK.raw2pack");
    CHECK(files::exists(path));
    CHECK(packAt(m, path) && packAt(m, path)->presets.empty() && !packAt(m, path)->folder);
    // nom déjà pris : « MON PACK 2 »
    const int pi2 = m.createPack("MON PACK", msg);
    CHECK(pi2 >= 0 && files::exists(files::join(root, "MON PACK 2.raw2pack")));

    m.applyFactoryPreset(5, nullptr);  // 808 fumée
    CHECK(m.addCurrentToPack(m.findPack(path), msg));
    CHECK(m.addCurrentToPack(m.findPack(path), msg));  // même nom : « 808 fumée 2 »
    const Pack* pk = packAt(m, path);
    CHECK(pk && pk->presets.size() == 2);
    if (pk && pk->presets.size() == 2) {
        CHECK(pk->presets[0].name == "808 fum\xC3\xA9" "e");
        CHECK(pk->presets[1].name == "808 fum\xC3\xA9" "e 2");
    }

    // écraser l'entrée 0 avec un autre son, puis la recharger : on retrouve ce son
    m.applyFactoryPreset(20, nullptr);
    const float cut20 = m.params.get(kCutoff), master20 = m.params.get(kMaster);
    CHECK(m.replacePackPreset(m.findPack(path), 0, msg));
    m.applyFactoryPreset(0, nullptr);
    CHECK(m.applyPackPreset(m.findPack(path), 0, nullptr));
    CHECK(std::fabs(m.params.get(kCutoff) - cut20) < 1e-5f && std::fabs(m.params.get(kMaster) - master20) < 1e-5f);
    CHECK(packAt(m, path)->presets[0].name == "808 fum\xC3\xA9" "e");  // le nom reste

    // renommer, favori compris
    m.favorites.insert("p:MON PACK/808 fum\xC3\xA9" "e 2");
    CHECK(m.renamePackPreset(m.findPack(path), 1, "Basse nuit", msg));
    CHECK(packAt(m, path)->presets[1].name == "Basse nuit");
    CHECK(m.favorites.count("p:MON PACK/Basse nuit") == 1);
    CHECK(!m.renamePackPreset(m.findPack(path), 1, "", msg));  // nom vide refusé

    // ordre
    CHECK(m.movePackPreset(m.findPack(path), 1, -1));
    CHECK(packAt(m, path)->presets[0].name == "Basse nuit");
    CHECK(!m.movePackPreset(m.findPack(path), 0, -1));  // déjà en tête

    // infos du pack
    CHECK(m.setPackInfo(m.findPack(path), "NUIT URBAINE", "Moi", "1.2", msg));
    pk = packAt(m, path);
    CHECK(pk && pk->name == "NUIT URBAINE" && pk->author == "Moi" && pk->version == "1.2");
    CHECK(m.favorites.count("p:NUIT URBAINE/Basse nuit") == 1);

    // export : un .raw2pack autonome dans Raw 2/Exports
    const std::string ex = m.exportPack(m.findPack(path), msg);
    CHECK(!ex.empty() && files::exists(ex));
    {
        std::string t;
        json::Value v;
        CHECK(files::readText(ex, t) && json::Value::parse(t, v));
        CHECK(v["format"].str() == "raw2-pack" && v["presets"].size() == 2 && v["name"].str() == "NUIT URBAINE");
    }

    // copie éditable
    const int dup = m.duplicatePack(m.findPack(path), msg);
    CHECK(dup >= 0 && m.packs[(size_t)dup].presets.size() == 2 && !m.packs[(size_t)dup].folder);

    // supprimer une entrée
    CHECK(m.deletePackPreset(m.findPack(path), 0, msg));
    pk = packAt(m, path);
    CHECK(pk && pk->presets.size() == 1 && pk->presets[0].name == "808 fum\xC3\xA9" "e");

    // un pack exporté se réinstalle (glisser sur la fenêtre des presets)
    const std::string reDir = files::join(home, "ailleurs");
    files::makeDirs(reDir);
    const std::string copy = files::join(reDir, "partage.raw2pack");
    CHECK(files::copyFile(ex, copy));
    CHECK(m.installPackFile(copy, msg) == 2);

    // --- pack dossier (un .raw par preset) ----------------------------------------------------
    const std::string dir = files::join(root, "DOSSIER");
    files::makeDirs(dir);
    m.applyFactoryPreset(3, nullptr);
    CHECK(files::writeText(files::join(dir, "01 Premier.raw"), m.presetJson().dump(true)));
    m.scanPacks();
    CHECK(packAt(m, dir) && packAt(m, dir)->presets.size() == 1);
    CHECK(m.addCurrentToPack(m.findPack(dir), msg));
    CHECK(packAt(m, dir)->presets.size() == 2);
    // renommer un .raw : le fichier suit, le numéro de rangement reste
    int k = -1;
    for (size_t i = 0; i < packAt(m, dir)->presets.size(); ++i)
        if (files::baseName(packAt(m, dir)->presets[i].path) == "01 Premier.raw") k = (int)i;
    CHECK(k >= 0);
    if (k >= 0) {
        CHECK(m.renamePackPreset(m.findPack(dir), k, "Premier pas", msg));
        CHECK(files::exists(files::join(dir, "01 Premier pas.raw")) && !files::exists(files::join(dir, "01 Premier.raw")));
    }
    CHECK(m.setPackInfo(m.findPack(dir), "MON DOSSIER", "Moi", "2.0", msg));
    CHECK(files::exists(files::join(dir, "pack.json")) && packAt(m, dir)->name == "MON DOSSIER");
    const std::string ex2 = m.exportPack(m.findPack(dir), msg);  // dossier -> un seul fichier
    CHECK(!ex2.empty() && files::exists(ex2));
    CHECK(m.deletePackPreset(m.findPack(dir), 0, msg));
    CHECK(packAt(m, dir)->presets.size() == 1);

    std::printf(failures ? "%d échec(s)\n" : "packs éditables : tout passe\n", failures);
    return failures ? 1 : 0;
}
#endif
