// Édition des packs par l'utilisateur : création d'un .raw2pack, ajout du son actuel, écrasement,
// renommage, ordre, infos, suppression, export et copie ; mêmes opérations sur un pack dossier.
// Tout se passe dans un dossier Documents temporaire (HOME redirigé), jamais chez l'utilisateur.
#include "core/FactoryData.h"
#include "core/Files.h"
#include "core/Instruments.h"
#include "core/Model.h"
#include "core/Skin.h"
#include "core/Zip.h"

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

    // --- contenu d'usine posé par l'installateur ---------------------------------------------------
    {
        const std::string fac = std::string(home) + "/usine";
        setenv("RAW2_FACTORY_DIR", fac.c_str(), 1);
        const std::string region = "<region> sample=a.wav key=60\n";
        files::makeDirs(fac + "/Instruments/Piano Test");
        files::writeText(fac + "/Instruments/Piano Test/Piano Test.sfz", "// name: Piano Test\n// category: KEYS\n" + region);
        files::makeDirs(fac + "/Instruments/Doublon");
        files::writeText(fac + "/Instruments/Doublon/Doublon.sfz", "// name: Doublon usine\n" + region);
        files::makeDirs(files::join(root, "Instruments/Doublon"));
        files::writeText(files::join(root, "Instruments/Doublon/Doublon.sfz"), "// name: Doublon perso\n" + region);
        const auto raws = files::listFiles(dir, ".raw");
        CHECK(!raws.empty());
        files::makeDirs(fac + "/PACK USINE");
        if (!raws.empty()) files::copyFile(raws[0], fac + "/PACK USINE/01 Son.raw");
        m.scanPacks();
        const std::string inst = files::join(root, "PACK USINE");
        CHECK(files::exists(files::join(inst, "01 Son.raw")));             // copié dans Documents › Raw 2
        CHECK(packAt(m, inst) && packAt(m, inst)->presets.size() == 1);
        files::removeFile(files::join(inst, "01 Son.raw"));
        rmdir(inst.c_str());
        m.scanPacks();
        CHECK(!files::exists(inst));                                        // supprimé : il ne revient pas
        m.instruments.scan();
        bool piano = false, perso = false, usine = false;
        for (const InstEntry& e : m.instruments.catalog()) {
            piano = piano || e.name == "Piano Test";
            perso = perso || e.name == "Doublon perso";
            usine = usine || e.name == "Doublon usine";
        }
        CHECK(piano);           // instrument d'usine trouvé
        CHECK(perso && !usine); // même dossier chez l'utilisateur : sa version gagne
        unsetenv("RAW2_FACTORY_DIR");
    }

    // --- skins (.raw2skin) ---------------------------------------------------------------------
    {
        Color c;
        CHECK(parseColor("#00C8FF", c) && std::fabs(c.g - 200.f / 255.f) < 1e-4f && c.a == 1.f);
        CHECK(parseColor("#14141880", c) && std::fabs(c.a - 128.f / 255.f) < 1e-4f);
        CHECK(parseColor("rgba(13,13,13,.10)", c) && std::fabs(c.a - 0.1f) < 1e-4f);
        CHECK(!parseColor("bleu", c));
        CHECK(colorHex(Color{1.f, 77.f / 255.f, 0.f, 1.f}) == "#FF4D00");
        Theme t = kThemes[0];
        CHECK(setThemeToken(t, "acc", Color{0.f, 1.f, 0.f, 1.f}) && t.accRGB.g == 255);
        CHECK(!setThemeToken(t, "inconnu", c));
        CHECK(themeByKey("nothing") == &kThemes[1] && themeByKey("x") == nullptr);

        // aller-retour skin.json
        Skin s;
        s.name = "N\xC3\xA9on / test";  // « / » interdit dans un nom de dossier
        s.base = "nothing";
        s.colors = {{"acc", Color{0.f, 0.78f, 1.f, 1.f}}, {"card", Color{0.08f, 0.08f, 0.09f, 0.5f}}};
        s.background = "fond.png";
        s.glass = true;
        s.blur = 0.7f;
        s.layout = {{"son.oscA", {894.f, 73.f, 1.f}}, {"son.vie", {400.f, 80.f, 0.75f}}};
        s.hidden = {"son.modules"};
        Skin::Place big;
        big.x = 24.f; big.y = 73.f; big.w = 480.f; big.h = 640.f;
        s.layout.emplace_back("son.oscB", big);
        Skin r;
        std::string err;
        CHECK(parseSkin(skinToJson(s), r, err));
        CHECK(r.name == s.name && r.base == "nothing" && r.glass && std::fabs(r.blur - 0.7f) < 1e-5f);
        CHECK(r.layout.size() == 3 && r.layout[2].second.w == 480.f && r.layout[2].second.h == 640.f);
        CHECK(r.colors.size() == 2 && r.layout[0].second.x == 894.f && r.layout[0].second.scale == 1.f);
        CHECK(std::fabs(r.layout[1].second.scale - 0.75f) < 1e-5f);
        CHECK(r.hidden.size() == 1 && r.hidden[0] == "son.modules");
        CHECK(parseSkin("{\"layout\":{\"son.vie\":[10,90,9]}}", r, err) && r.layout[0].second.scale == 1.6f);  // borné
        CHECK(!parseSkin("{\"format\":\"autre\"}", r, err));
        CHECK(!parseSkin("pas du json", r, err));

        // archive stockée : installée dans Documents/Raw 2/Skins/<nom>, chemins ignorés
        const std::string js = skinToJson(s);
        std::vector<zip::Entry> en(3);
        en[0].name = "skin.json";
        en[0].data.assign(js.begin(), js.end());
        en[1].name = "dossier/fond.png";
        en[1].data = {1, 2, 3, 4};
        en[2].name = "../../evil.mp4";
        en[2].data = {9};
        const std::vector<unsigned char> arc = zip::write(en);
        std::vector<zip::Entry> back;
        CHECK(zip::read(arc, back, err) && back.size() == 3 && back[1].data.size() == 4);
        const std::string file = files::join(home, "Test.raw2skin");
        CHECK(files::write(file, arc));
        CHECK(isSkinFile(file));
        const std::string zipFile = files::join(home, "Test.raw2skin.zip");  // export de la version web du studio
        CHECK(files::write(zipFile, arc) && isSkinFile(zipFile));
        std::string zdir;
        CHECK(installSkin(zipFile, zdir, err));
        std::string dir;
        CHECK(installSkin(file, dir, err));
        CHECK(dir == files::join(files::join(root, "Skins"), "N\xC3\xA9on  test"));
        CHECK(files::exists(files::join(dir, "skin.json")) && files::exists(files::join(dir, "fond.png")));
        CHECK(files::exists(files::join(dir, "evil.mp4")) && !files::exists(files::join(home, "evil.mp4")));
        Skin l;
        CHECK(loadSkin(dir, l, err) && l.background == "fond.png" && l.dir == dir);
        const std::vector<std::string> list = listSkins();
        CHECK(list.size() == 1 && list[0] == dir);
        // archive abîmée, archive compressée
        std::vector<unsigned char> bad = arc;
        bad[40] ^= 0xFF;  // donnée de skin.json
        CHECK(!zip::read(bad, back, err));
        std::vector<unsigned char> defl = arc;
        int marked = 0;
        for (size_t i = 0; i + 12 < defl.size(); ++i)  // méthode deflate dans le répertoire central
            if (defl[i] == 0x50 && defl[i + 1] == 0x4b && defl[i + 2] == 1 && defl[i + 3] == 2) { defl[i + 10] = 8; ++marked; }
        CHECK(marked == 3 && !zip::read(defl, back, err) && err.find("recompress") != std::string::npos);
        // dossier de skin déposé tel quel
        const std::string src = files::join(home, "MonSkin");
        files::makeDirs(src);
        Skin d;
        d.name = "Dossier";
        files::writeText(files::join(src, "skin.json"), skinToJson(d));
        files::write(files::join(src, "notes.txt"), {1});
        CHECK(isSkinFile(src) && installSkin(src, dir, err));
        CHECK(files::exists(files::join(dir, "skin.json")) && !files::exists(files::join(dir, "notes.txt")));
        CHECK(listSkins().size() == 2);
        std::printf("skins : format, archive et installation v\xC3\xA9rifi\xC3\xA9s\n");
    }

    std::printf(failures ? "%d échec(s)\n" : "packs éditables : tout passe\n", failures);
    return failures ? 1 : 0;
}
#endif
