// Packs de presets : le dossier Documents/Raw 2 est relu à l'ouverture de la banque.
//   Documents/Raw 2/Mes presets/*.raw     MES PRESETS (bouton ENREGISTRER, « + SAUVER LE SON ACTUEL »)
//   Documents/Raw 2/<pack>/*.raw          un pack par sous-dossier (pack.json facultatif : nom, auteur,
//                                         version ; un niveau de sous-dossiers en plus pour ranger)
//   Documents/Raw 2/*.raw2pack            un pack en un seul fichier (format « raw2-pack » de la maquette)
//   Documents/Raw 2/*.raw                 presets isolés, regroupés dans DIVERS
//   Documents/Raw 2/Wavetables            exports WAV de la page WAVETABLE (pas un pack)
#include "core/FactoryData.h"
#include "core/Files.h"
#include "core/Model.h"
#include "core/Wav.h"
#include "dsp/Wavetable.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

namespace raw2 {

namespace {

constexpr size_t kMaxPackPresets = 500;

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool endsWith(const std::string& s, const char* ext) {
    const std::string e = ext;
    return s.size() > e.size() && lower(s.substr(s.size() - e.size())) == e;
}

// texte affichable : pas de caractères de contrôle, longueur bornée (comme packClean de la maquette)
std::string clean(const std::string& s, size_t n) {
    std::string o;
    for (char c : s)
        if ((unsigned char)c >= 0x20 && c != '<' && c != '>') o += c;
    if (o.size() > n) {
        o.resize(n);
        while (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0x80) o.pop_back();  // pas de caractère UTF-8 coupé
        if (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0xC0) o.pop_back();
    }
    return o;
}

bool readJson(const std::string& path, json::Value& v) {
    std::string text;
    if (!files::readText(path, text) || text.size() > 32 * 1024 * 1024) return false;
    return json::Value::parse(text, v);
}

bool isPackJson(const json::Value& v) {
    if (!v.isObject() || !v["presets"].isArray()) return false;
    return v["presets"].size() > 0 || v["format"].str() == "raw2-pack";  // un pack créé vide reste visible
}

const char* kCats[6] = {"basse", "lead", "pad", "pluck", "arp", "fx"};

// métadonnées d'une entrée de raw2-pack : catégorie (premier mot des tags ou preset de base), style
void entryMeta(const json::Value& e, PackPreset& p) {
    p.name = clean(e["name"].str(), 40);
    p.tags = clean(e["tags"].str(), 40);
    if (e["base"].isNumber()) {
        const int b = (int)e["base"].num(-1);
        if (b >= 0 && b < kNumFactoryPresets) p.base = b;
    }
    if (e["params"].isObject()) {  // preset complet
        p.category = clean(e["category"].str(), 20);
        p.style = clean(e["style"].str(), 30);
    }
    std::string first = lower(p.tags.substr(0, p.tags.find_first_of(" ,\xC2")));
    for (const char* c : kCats)
        if (first == c) p.category = c;
    if (p.category.empty() && p.base >= 0) p.category = categoryName(kFactoryPresets[p.base].category);
    if (p.style.empty() && p.base >= 0) p.style = kFactoryPresets[p.base].style;
}

void addRawFile(Pack& pk, const std::string& path, const std::string& tags) {
    if (pk.presets.size() >= kMaxPackPresets) return;
    PackPreset p;
    p.path = path;
    p.name = fileBaseName(path);
    p.tags = tags;
    json::Value v;
    if (readJson(path, v) && v.isObject()) {
        if (!v["name"].str().empty()) p.name = clean(v["name"].str(), 40);
        p.category = clean(v["category"].str(), 20);
        p.style = clean(v["style"].str(), 30);
    }
    pk.presets.push_back(std::move(p));
}

bool loadPackFile(const std::string& path, Pack& pk) {
    json::Value v;
    if (!readJson(path, v) || !isPackJson(v)) return false;
    pk.folder = false;
    pk.path = path;
    pk.name = clean(v["name"].str(), 40);
    if (pk.name.empty()) pk.name = fileBaseName(path);
    pk.author = clean(v["author"].str(), 40);
    pk.version = clean(v["version"].str(), 12);
    const auto& items = v["presets"].items();
    for (size_t k = 0; k < items.size() && pk.presets.size() < kMaxPackPresets; ++k) {
        if (!items[k].isObject()) continue;
        PackPreset p;
        p.entry = (int)k;
        entryMeta(items[k], p);
        if (p.name.empty()) p.name = "Preset " + std::to_string(k + 1);
        pk.presets.push_back(std::move(p));
    }
    return !pk.presets.empty() || v["format"].str() == "raw2-pack";
}

// clés de la maquette (raw2-pack) -> paramètres du plugin
int mockupParam(const std::string& k) {
    const int direct = paramIdFromKey(k.c_str());
    if (direct >= 0) return direct;
    struct A { const char* m; uint32_t id; };
    static const A alias[] = {
        {"eA_a", kAttack}, {"eA_d", kDecay}, {"eA_s", kSustain}, {"eA_r", kRelease}, {"eA_c", kEnvACurve},
        {"eB_a", kEnvBAttack}, {"eB_d", kEnvBDecay}, {"eB_s", kEnvBSustain}, {"eB_r", kEnvBRelease}, {"eB_c", kEnvBCurve},
        {"l1_r", kLfo1Rate}, {"l1_p", kLfo1Phase}, {"l1_f", kLfo1Fade}, {"l1_s", kLfo1Smooth},
        {"l2_r", kLfo2Rate}, {"l2_p", kLfo2Phase}, {"l2_f", kLfo2Fade}, {"l2_s", kLfo2Smooth},
        {"x_eq_f0", kFxEqF0}, {"x_eq_f1", kFxEqF1}, {"x_eq_f2", kFxEqF2},
    };
    for (const A& a : alias)
        if (k == a.m) return (int)a.id;
    if (k.size() >= 5 && k.rfind("rk", 0) == 0) {  // rk<n>_<r|p|f|s> : LFO du rack (maquette)
        const size_t u = k.find('_');
        const int n = std::atoi(k.c_str() + 2);
        if (u != std::string::npos && n >= 1 && n <= kNumRackLfos && u + 2 == k.size()) {
            const char w = k[u + 1];
            const int f = w == 'r' ? kLfRate : (w == 'p' ? kLfPhase : (w == 'f' ? kLfFade : (w == 's' ? kLfSmooth : -1)));
            if (f >= 0) return (int)lfoP(1 + n, f);
        }
    }
    if (k.rfind("x_", 0) == 0) {  // x_<effet>_<0|1|2|m|md>
        const size_t u = k.find('_', 2);
        if (u == std::string::npos) return -1;
        const std::string fx = k.substr(2, u - 2), w = k.substr(u + 1);
        for (int f = 0; f < kNumFx; ++f) {
            if (fx != fxKey(f)) continue;
            if (w == "0" || w == "1" || w == "2") return (int)fxParam(f, kFxP0 + (w[0] - '0'));
            if (w == "m") return (int)fxParam(f, kFxMix);
            if (w == "md") return (int)fxParam(f, kFxModDepth);
        }
    }
    return -1;
}

int userNumber(const std::string& name) {
    if (name.rfind("USER ", 0) != 0) return 0;
    return std::atoi(name.c_str() + 5);
}

std::string fmtCount(size_t n) { return " (" + std::to_string(n) + " presets)"; }

// nom de fichier valable sous Windows et macOS (mêmes interdits que saveUserPreset)
std::string safeFileName(const std::string& raw) {
    std::string n;
    for (char ch : raw)
        if (!std::strchr("<>:\"/\\|?*", ch) && (unsigned char)ch >= 0x20) n += ch;
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    return n.empty() ? std::string("sans nom") : n;
}

// chemin libre : « nom.ext », sinon « nom 2.ext », « nom 3.ext »…
std::string freePath(const std::string& dir, const std::string& base, const char* ext, const std::string& keep = std::string()) {
    std::string p = files::join(dir, base + ext);
    for (int k = 2; files::exists(p) && p != keep && k < 1000; ++k) p = files::join(dir, base + " " + std::to_string(k) + ext);
    return p;
}

bool writeJson(const std::string& path, const json::Value& v) { return files::writeText(path, v.dump(true)); }

} // namespace

int Model::findPack(const std::string& path) const {
    for (size_t i = 0; i < packs.size(); ++i)
        if (packs[i].path == path) return (int)i;
    return -1;
}

int Model::createPack(const std::string& rawName, std::string& msg) {
    const std::string root = files::raw2Dir();
    files::makeDirs(root);
    const std::string name = clean(rawName.empty() ? std::string("NOUVEAU PACK") : rawName, 40);
    const std::string path = freePath(root, safeFileName(name), ".raw2pack");
    json::Value v = json::Value::object();
    v.set("format", "raw2-pack");
    v.set("name", fileBaseName(path));
    v.set("author", "");
    v.set("version", "1.0");
    v.set("presets", json::Value::array());
    if (!writeJson(path, v)) {
        msg = "\xC3\xA9" "chec : dossier Documents \xE2\x80\xBA Raw 2 inaccessible";
        return -1;
    }
    scanPacks();
    msg = "pack cr\xC3\xA9\xC3\xA9 \xC2\xB7 " + fileBaseName(path) + " \xC2\xB7 ajoute des sons avec \xC2\xAB + SON ACTUEL \xC2\xBB";
    return findPack(path);
}

bool Model::addCurrentToPack(int pi, std::string& msg) {
    if (pi < 0 || pi >= (int)packs.size()) return false;
    const Pack pk = packs[(size_t)pi];
    std::string name;
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        name = clean(presetName.empty() ? std::string("sans nom") : presetName, 40);
    }
    // nom déjà pris dans ce pack : « nom 2 », « nom 3 »…
    auto taken = [&](const std::string& n) {
        for (const PackPreset& p : pk.presets)
            if (p.name == n) return true;
        return false;
    };
    const std::string base = name;
    for (int k = 2; taken(name) && k < 1000; ++k) name = clean(base, 36) + " " + std::to_string(k);
    json::Value entry = presetJson(false);
    entry.set("name", name);
    bool ok;
    if (!pk.folder) {
        json::Value v;
        ok = readJson(pk.path, v) && isPackJson(v);
        if (ok && v["presets"].size() >= kMaxPackPresets) {
            msg = "pack plein (500 presets)";
            return false;
        }
        if (ok) {
            json::Value arr = v["presets"];
            arr.push(entry);
            v.set("presets", arr);
            ok = writeJson(pk.path, v);
        }
    } else {
        ok = writeJson(freePath(pk.path, safeFileName(name), ".raw"), entry);
    }
    if (!ok) {
        msg = "\xC3\xA9" "chec : \xC3\xA9" "criture impossible";
        return false;
    }
    scanPacks();
    msg = "ajout\xC3\xA9 \xC2\xB7 " + pk.name + " \xE2\x80\xBA " + name;
    return true;
}

bool Model::replacePackPreset(int pi, int ui, std::string& msg) {
    if (pi < 0 || pi >= (int)packs.size() || ui < 0 || ui >= (int)packs[(size_t)pi].presets.size()) return false;
    const Pack pk = packs[(size_t)pi];
    const PackPreset p = pk.presets[(size_t)ui];
    json::Value entry = presetJson(false);
    entry.set("name", p.name);  // le son change, le nom reste
    bool ok;
    if (!p.path.empty()) {
        ok = writeJson(p.path, entry);
    } else {
        json::Value v;
        ok = readJson(pk.path, v) && isPackJson(v) && p.entry >= 0 && p.entry < (int)v["presets"].size();
        if (ok) {
            json::Value arr = v["presets"];
            arr.items()[(size_t)p.entry] = entry;
            v.set("presets", arr);
            ok = writeJson(pk.path, v);
        }
    }
    scanPacks();
    msg = ok ? "\xC3\xA9" "cras\xC3\xA9 avec le son actuel \xC2\xB7 " + p.name : std::string("\xC3\xA9" "chec : \xC3\xA9" "criture impossible");
    return ok;
}

bool Model::renamePackPreset(int pi, int ui, const std::string& rawName, std::string& msg) {
    if (pi < 0 || pi >= (int)packs.size() || ui < 0 || ui >= (int)packs[(size_t)pi].presets.size()) return false;
    const Pack pk = packs[(size_t)pi];
    const PackPreset p = pk.presets[(size_t)ui];
    const std::string name = clean(rawName, 40);
    if (name.empty()) {
        msg = "nom vide";
        return false;
    }
    bool ok;
    if (!p.path.empty()) {
        json::Value v;
        ok = readJson(p.path, v) && v.isObject();
        if (ok) {
            v.set("name", name);
            // le fichier prend le nouveau nom (numéro de rangement « 01 » gardé s'il y en a un)
            std::string prefix;
            const std::string old = fileBaseName(p.path);
            if (old.size() > 3 && std::isdigit((unsigned char)old[0]) && std::isdigit((unsigned char)old[1]) && old[2] == ' ') prefix = old.substr(0, 3);
            const std::string dir = p.path.substr(0, p.path.find_last_of("/\\"));
            const std::string to = freePath(dir, prefix + safeFileName(name), ".raw", p.path);
            ok = writeJson(to, v);
            if (ok && to != p.path) files::removeFile(p.path);
        }
    } else {
        json::Value v;
        ok = readJson(pk.path, v) && isPackJson(v) && p.entry >= 0 && p.entry < (int)v["presets"].size();
        if (ok) {
            json::Value arr = v["presets"];
            arr.items()[(size_t)p.entry].set("name", name);
            v.set("presets", arr);
            ok = writeJson(pk.path, v);
        }
    }
    if (ok) {
        // favori et preset chargé suivent le nouveau nom
        const std::string oldKey = "p:" + pk.name + "/" + p.name;
        if (favorites.erase(oldKey)) {
            favorites.insert("p:" + pk.name + "/" + name);
            saveGlobalSettings();
        }
        std::lock_guard<std::mutex> lk(presetMutex);
        if (presetIndex == kPresetPack && presetPack == pk.name && presetName == p.name) presetName = name;
    }
    scanPacks();
    msg = ok ? "renomm\xC3\xA9 \xC2\xB7 " + name : std::string("\xC3\xA9" "chec : \xC3\xA9" "criture impossible");
    return ok;
}

bool Model::deletePackPreset(int pi, int ui, std::string& msg) {
    if (pi < 0 || pi >= (int)packs.size() || ui < 0 || ui >= (int)packs[(size_t)pi].presets.size()) return false;
    const Pack pk = packs[(size_t)pi];
    const PackPreset p = pk.presets[(size_t)ui];
    bool ok;
    if (!p.path.empty()) {
        ok = files::moveToTrash(p.path);
    } else {
        json::Value v;
        ok = readJson(pk.path, v) && isPackJson(v) && p.entry >= 0 && p.entry < (int)v["presets"].size();
        if (ok) {
            json::Value arr = v["presets"];
            arr.items().erase(arr.items().begin() + p.entry);
            v.set("presets", arr);
            ok = writeJson(pk.path, v);
        }
    }
    scanPacks();
    msg = ok ? "preset retir\xC3\xA9 \xC2\xB7 " + p.name : std::string("\xC3\xA9" "chec : \xC3\xA9" "criture impossible");
    return ok;
}

bool Model::movePackPreset(int pi, int ui, int delta) {
    if (pi < 0 || pi >= (int)packs.size() || packs[(size_t)pi].folder) return false;
    const Pack pk = packs[(size_t)pi];
    if (ui < 0 || ui >= (int)pk.presets.size()) return false;
    const int to = ui + delta;
    if (to < 0 || to >= (int)pk.presets.size()) return false;
    const int a = pk.presets[(size_t)ui].entry, b = pk.presets[(size_t)to].entry;
    json::Value v;
    if (!readJson(pk.path, v) || !isPackJson(v) || a < 0 || b < 0 || a >= (int)v["presets"].size() || b >= (int)v["presets"].size()) return false;
    json::Value arr = v["presets"];
    std::swap(arr.items()[(size_t)a], arr.items()[(size_t)b]);
    v.set("presets", arr);
    const bool ok = writeJson(pk.path, v);
    scanPacks();
    return ok;
}

bool Model::setPackInfo(int pi, const std::string& rawName, const std::string& author, const std::string& version, std::string& msg) {
    if (pi < 0 || pi >= (int)packs.size()) return false;
    const Pack pk = packs[(size_t)pi];
    if (pk.mine || (pk.folder && pk.path == files::raw2Dir())) {
        msg = "MES PRESETS et DIVERS gardent leur nom";
        return false;
    }
    const std::string name = clean(rawName.empty() ? pk.name : rawName, 40);
    bool ok;
    if (!pk.folder) {
        json::Value v;
        ok = readJson(pk.path, v) && isPackJson(v);
        if (ok) {
            v.set("name", name);
            v.set("author", clean(author, 40));
            v.set("version", clean(version, 12));
            ok = writeJson(pk.path, v);
        }
    } else {
        json::Value meta;
        if (!readJson(files::join(pk.path, "pack.json"), meta) || !meta.isObject()) meta = json::Value::object();
        meta.set("name", name);
        meta.set("author", clean(author, 40));
        meta.set("version", clean(version, 12));
        ok = writeJson(files::join(pk.path, "pack.json"), meta);
    }
    if (ok && name != pk.name) {  // favoris de ce pack : nouveau nom
        std::set<std::string> fav;
        for (const std::string& f : favorites)
            fav.insert(f.rfind("p:" + pk.name + "/", 0) == 0 ? "p:" + name + f.substr(2 + pk.name.size()) : f);
        favorites.swap(fav);
        saveGlobalSettings();
    }
    scanPacks();
    msg = ok ? "infos du pack enregistr\xC3\xA9" "es \xC2\xB7 " + name : std::string("\xC3\xA9" "chec : \xC3\xA9" "criture impossible");
    return ok;
}

namespace {
// tout le pack en un seul JSON raw2-pack (les .raw d'un pack dossier deviennent des entrées complètes)
bool bundlePack(const Pack& pk, json::Value& out) {
    out = json::Value::object();
    out.set("format", "raw2-pack");
    out.set("name", pk.name);
    out.set("author", pk.author);
    out.set("version", pk.version.empty() ? std::string("1.0") : pk.version);
    json::Value arr = json::Value::array();
    json::Value src;
    if (!pk.folder && (!readJson(pk.path, src) || !isPackJson(src))) return false;
    for (const PackPreset& p : pk.presets) {
        if (!p.path.empty()) {
            json::Value e;
            if (!readJson(p.path, e) || !e.isObject()) continue;
            e.set("name", p.name);
            if (!p.tags.empty() && p.tags != "user") e.set("tags", p.tags);
            arr.push(e);
        } else if (p.entry >= 0 && p.entry < (int)src["presets"].size()) {
            arr.push(src["presets"][(size_t)p.entry]);
        }
    }
    out.set("presets", arr);
    return true;
}
} // namespace

int Model::duplicatePack(int pi, std::string& msg) {
    if (pi < 0 || pi >= (int)packs.size()) return -1;
    json::Value v;
    if (!bundlePack(packs[(size_t)pi], v)) {
        msg = "lecture du pack impossible";
        return -1;
    }
    const std::string name = clean(packs[(size_t)pi].name, 33) + " (copie)";
    const std::string path = freePath(files::raw2Dir(), safeFileName(name), ".raw2pack");
    v.set("name", fileBaseName(path));
    if (!writeJson(path, v)) {
        msg = "\xC3\xA9" "chec : \xC3\xA9" "criture impossible";
        return -1;
    }
    scanPacks();
    msg = "copie cr\xC3\xA9\xC3\xA9" "e \xC2\xB7 " + fileBaseName(path);
    return findPack(path);
}

std::string Model::exportPack(int pi, std::string& msg) {
    if (pi < 0 || pi >= (int)packs.size()) return {};
    json::Value v;
    if (!bundlePack(packs[(size_t)pi], v) || v["presets"].size() == 0) {
        msg = "rien \xC3\xA0 exporter (pack vide ou illisible)";
        return {};
    }
    const std::string dir = files::exportsDir();
    files::makeDirs(dir);
    const std::string path = files::join(dir, safeFileName(packs[(size_t)pi].name) + ".raw2pack");
    if (!writeJson(path, v)) {
        msg = "\xC3\xA9" "chec : \xC3\xA9" "criture impossible";
        return {};
    }
    msg = "export\xC3\xA9 \xC2\xB7 Raw 2 \xE2\x80\xBA Exports \xE2\x80\xBA " + files::baseName(path) + fmtCount(v["presets"].size());
    return path;
}

void Model::scanPacks() {
    const std::string root = files::raw2Dir(), mine = files::myPresetsDir();
    files::makeDirs(mine);
    // première ouverture : les presets enregistrés par les versions précédentes rejoignent MES PRESETS
    if (files::listFiles(mine, ".raw").empty())
        for (const std::string& f : files::listFiles(files::legacyPresetDir(), ".raw")) files::copyFile(f, files::join(mine, files::baseName(f)));

    std::vector<Pack> list;
    {
        Pack pk;
        pk.name = "MES PRESETS";
        pk.path = mine;
        pk.mine = true;
        for (const std::string& f : files::listFiles(mine, ".raw")) addRawFile(pk, f, "user");
        if (!pk.presets.empty()) list.push_back(std::move(pk));
    }
    auto addPackFiles = [&](const std::string& dir) {
        for (const char* ext : {".raw2pack", ".json"})
            for (const std::string& f : files::listFiles(dir, ext)) {
                if (lower(files::baseName(f)) == "pack.json") continue;
                Pack pk;
                if (loadPackFile(f, pk)) list.push_back(std::move(pk));
            }
    };
    for (const std::string& d : files::listDirs(root)) {
        const std::string bn = lower(files::baseName(d));
        if (bn == "mes presets" || bn == "wavetables") continue;
        Pack pk;
        pk.path = d;
        pk.name = clean(files::baseName(d), 40);
        json::Value meta;
        if (readJson(files::join(d, "pack.json"), meta) && meta.isObject()) {
            if (!meta["name"].str().empty()) pk.name = clean(meta["name"].str(), 40);
            pk.author = clean(meta["author"].str(), 40);
            pk.version = clean(meta["version"].str(), 12);
        }
        for (const std::string& f : files::listFiles(d, ".raw")) addRawFile(pk, f, "");
        for (const std::string& sub : files::listDirs(d))
            for (const std::string& f : files::listFiles(sub, ".raw")) addRawFile(pk, f, clean(files::baseName(sub), 30));
        if (!pk.presets.empty()) list.push_back(std::move(pk));
        addPackFiles(d);
    }
    addPackFiles(root);
    {
        Pack pk;
        pk.name = "DIVERS";
        pk.path = root;
        for (const std::string& f : files::listFiles(root, ".raw")) addRawFile(pk, f, "");
        if (!pk.presets.empty()) list.push_back(std::move(pk));
    }
    packs = std::move(list);
}

void Model::applyInit(ParamSink* sink) {
    instruments.clear(0);
    instruments.clear(1);
    for (uint32_t id = 0; id < kNumParams; ++id)
        if (id != kPitchBend && id != kModWheel) setParam(sink, id, paramDef(id).def);
    // valeurs de la maquette (applyInit) : un sinus, pas d'oscillateur B, aucun effet
    const std::pair<uint32_t, float> v[] = {
        {kAPos, 0.5f}, {kBPos, 0.5f}, {kALvl, 0.78f}, {kBLvl, 0.f}, {kAWarp, 0.f}, {kBWarp, 0.f}, {kAPan, 0.5f}, {kBPan, 0.5f},
        {kSpeed, 0.389f}, {kBirth, 0.3f}, {kQty, 0.6f}, {kMaster, 0.7f}, {kM1, 0.f}, {kM2, 0.f}, {kM3, 0.f},
        {kAttack, 0.02f}, {kDecay, 0.4f}, {kSustain, 1.f}, {kRelease, 0.15f}, {kEnvACurve, 0.5f},
        {kEnvBAttack, 0.02f}, {kEnvBDecay, 0.4f}, {kEnvBSustain, 1.f}, {kEnvBRelease, 0.15f}, {kEnvBCurve, 0.5f},
        {kLfo1Rate, 0.4f}, {kLfo1Phase, 0.f}, {kLfo1Fade, 0.f}, {kLfo1Smooth, 0.f},
        {kLfo2Rate, 0.25f}, {kLfo2Phase, 0.f}, {kLfo2Fade, 0.f}, {kLfo2Smooth, 0.f},
        {kCutoff, 1.f}, {kReso, 0.f}, {kAOct, 0.f}, {kASt, 0.f}, {kBOct, 0.f}, {kBSt, 0.f}, {kAMode, 0.f}, {kBMode, 0.f},
        {kRule, 0.f}, {kFxWet, 1.f},
    };
    for (const auto& p : v) setParam(sink, p.first, p.second);
    for (int f = 0; f < kNumFx; ++f) setParam(sink, fxParam(f, kFxOn), 0.f);
    resetFxOrder();
    matrix.clear();
    routes.clear();
    for (int l = 0; l < kNumLfos; ++l) { lfoShapes.reset(l); lfoShapes.snap[(size_t)l] = 0; }
    lfoShapes.grp.fill(0);
    {
        std::lock_guard<std::mutex> lk(tableMutex);
        buildFromSpec(edit[0], ShapeSpec{Shape::Sine, 0.f, 0.f}, "INIT");
        buildFromSpec(edit[1], ShapeSpec{Shape::Sine, 0.f, 0.f}, "INIT (B)");
        mounted = {true, true};
    }
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        presetIndex = kPresetInit;
        presetName = "INIT";
        presetCategory = "init";
        presetStyle = "preset vierge";
        presetPack.clear();
    }
    tablesChanged(0);
    tablesChanged(1);
    resetLifeAfterPreset();
}

bool Model::applyPackEntry(const json::Value& e, ParamSink* sink) {
    if (!e.isObject()) return false;
    if (e["params"].isObject()) return applyPresetJson(e, sink);  // preset complet dans le pack
    const int base = (int)e["base"].num(-1);
    if (base >= 0 && base < kNumFactoryPresets) applyFactoryPreset(base, sink);
    else applyInit(sink);
    if (e["p"].isObject())
        for (const auto& m : e["p"].members()) {
            const int id = mockupParam(m.first);
            if (id >= 0 && m.second.isNumber() && std::isfinite(m.second.num())) setParam(sink, (uint32_t)id, (float)m.second.num());
        }
    auto pairOf = [&](const char* key, uint32_t a, uint32_t b) {
        if (!e[key].isObject()) return;
        if (e[key]["a"].isNumber()) setParam(sink, a, (float)std::round(e[key]["a"].num()));
        if (e[key]["b"].isNumber()) setParam(sink, b, (float)std::round(e[key]["b"].num()));
    };
    pairOf("oct", kAOct, kBOct);
    pairOf("st", kASt, kBSt);
    pairOf("mode", kAMode, kBMode);
    if (e["rule"].str() == "hl") setParam(sink, kRule, 1.f);
    else if (e["rule"].str() == "life") setParam(sink, kRule, 0.f);
    if (e["fx"].isObject() && e["fx"]["on"].isObject())
        for (int f = 0; f < kNumFx; ++f) {
            const json::Value& o = e["fx"]["on"][fxKey(f)];
            if (o.type() == json::Value::Type::Bool) setParam(sink, fxParam(f, kFxOn), o.boolean() ? 1.f : 0.f);
        }
    const std::string name = clean(e["name"].str(), 40);
    if (e["wt"].isArray() && e["wt"].size() == 2) {
        ShapeSpec sp[2];
        bool ok = true;
        for (size_t k = 0; k < 2 && ok; ++k) {
            const json::Value& r = e["wt"][k];
            ok = r.isArray() && r.size() == 3 && shapeFromKey(r[(size_t)0].str().c_str(), sp[k].shape);
            if (ok) {
                sp[k].morphStart = std::clamp((float)r[(size_t)1].num(0), 0.f, 1.f);
                sp[k].morphEnd = std::clamp((float)r[(size_t)2].num(1), 0.f, 1.f);
            }
        }
        if (ok) {
            {
                std::lock_guard<std::mutex> lk(tableMutex);
                buildFromSpec(edit[0], sp[0], name);
                buildFromSpec(edit[1], sp[1], name + " (B)");
                mounted = {true, true};
            }
            tablesChanged(0);
            tablesChanged(1);
        }
    }
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        if (!name.empty()) presetName = name;
        PackPreset meta;
        entryMeta(e, meta);
        if (!meta.category.empty()) presetCategory = meta.category;
        if (!meta.style.empty()) presetStyle = meta.style;
    }
    return true;
}

bool Model::applyPackPreset(int pi, int ui, ParamSink* sink) {
    if (pi < 0 || pi >= (int)packs.size() || ui < 0 || ui >= (int)packs[(size_t)pi].presets.size()) return false;
    const Pack& pk = packs[(size_t)pi];
    const PackPreset& p = pk.presets[(size_t)ui];
    bool ok = false;
    if (!p.path.empty()) {
        json::Value v;
        ok = readJson(p.path, v) && applyPresetJson(v, sink);
    } else {
        json::Value v;
        ok = readJson(pk.path, v) && isPackJson(v) && p.entry >= 0 && p.entry < (int)v["presets"].size() && applyPackEntry(v["presets"][(size_t)p.entry], sink);
    }
    if (!ok) return false;
    {
        std::lock_guard<std::mutex> lk(presetMutex);
        presetIndex = kPresetPack;
        presetPack = pk.name;
        presetName = p.name;
    }
    resetLifeAfterPreset();
    return true;
}

std::string Model::nextUserName() {
    int n = 0;
    for (const std::string& f : files::listFiles(files::myPresetsDir(), ".raw")) n = std::max(n, userNumber(fileBaseName(f)));
    char b[32];
    std::snprintf(b, sizeof b, "USER %02d", n + 1);
    return b;
}

int Model::installPackFile(const std::string& path, std::string& msg) {
    const std::string root = files::raw2Dir();
    files::makeDirs(root);
    int added = 0;
    if (files::isDir(path)) {
        const std::string to = files::join(root, files::baseName(path));
        if (!files::copyTree(path, to)) { msg = "copie impossible"; return 0; }
        scanPacks();
        for (const Pack& pk : packs)
            if (pk.path == to || pk.path.rfind(to, 0) == 0) added += (int)pk.presets.size();
    } else if (endsWith(path, ".raw")) {
        const std::string to = files::join(files::myPresetsDir(), files::baseName(path));
        if (!files::copyFile(path, to)) { msg = "copie impossible"; return 0; }
        scanPacks();
        added = 1;
        msg = "preset ajout\xC3\xA9 \xC2\xB7 USER \xE2\x80\xBA MES PRESETS";
        return added;
    } else if (endsWith(path, ".raw2pack") || endsWith(path, ".json")) {
        Pack pk;
        if (!loadPackFile(path, pk)) { msg = "pack invalide (format raw2-pack attendu)"; return 0; }
        const std::string to = files::join(root, files::baseName(path));
        if (!files::copyFile(path, to)) { msg = "copie impossible"; return 0; }
        scanPacks();
        added = (int)pk.presets.size();
    } else {
        msg = "fichier non reconnu (.raw2pack, .raw ou dossier de .raw)";
        return 0;
    }
    msg = added > 0 ? "pack install\xC3\xA9 \xC2\xB7 " + std::to_string(added) + " presets" : std::string("aucun preset trouv\xC3\xA9");
    return added;
}

int Model::installDemoPacks(std::string& msg) {
    const std::string root = files::raw2Dir();
    files::makeDirs(root);
    int n = 0;
    // pack « dossier » : un fichier .raw complet par preset, plus pack.json
    {
        const std::string dir = files::join(root, "D\xC3\x89MO \xC2\xB7 NUIT FROIDE");
        files::makeDirs(dir);
        json::Value meta = json::Value::object();
        meta.set("name", "D\xC3\x89MO \xC2\xB7 NUIT FROIDE");
        meta.set("author", "ABRASION");
        meta.set("version", "1.0");
        files::writeText(files::join(dir, "pack.json"), meta.dump(true));
        struct E { const char* name; const char* tags; int base; std::vector<std::pair<const char*, double>> p; };
        const E list[] = {
            {"Verglas", "pad", 3, {{"aLvl", 0.7}, {"speed", 0.2}}},
            {"Souffle noir", "pad", 38, {{"master", 0.62}}},
            {"Cristal sombre", "lead", 22, {{"aWarp", 0.3}, {"bPan", 0.7}}},
            {"Pulse minuit", "arp", 48, {}},
        };
        Model tmp(Mode::Offline);
        int k = 0;
        for (const E& e : list) {
            json::Value entry = json::Value::object();
            entry.set("name", e.name);
            entry.set("tags", e.tags);
            entry.set("base", e.base);
            json::Value p = json::Value::object();
            for (const auto& q : e.p) p.set(q.first, q.second);
            entry.set("p", p);
            tmp.applyPackEntry(entry, nullptr);
            char num[8];
            std::snprintf(num, sizeof num, "%02d ", ++k);
            if (files::writeText(files::join(dir, std::string(num) + e.name + ".raw"), tmp.presetJson().dump(true))) ++n;
        }
    }
    // pack « fichier » : format raw2-pack de la maquette (preset d'usine de départ + retouches)
    {
        json::Value pk = json::Value::object();
        pk.set("format", "raw2-pack");
        pk.set("name", "D\xC3\x89MO \xC2\xB7 RAGE LOURDE");
        pk.set("author", "ABRASION");
        pk.set("version", "1.0");
        json::Value arr = json::Value::array();
        auto add = [&](const char* name, int base, const char* key, double val) {
            json::Value e = json::Value::object();
            e.set("name", name);
            e.set("tags", "basse");
            e.set("base", base);
            json::Value p = json::Value::object();
            if (key) p.set(key, val);
            e.set("p", p);
            arr.push(e);
        };
        add("Mur noir", 7, "aLvl", 0.9);
        add("B\xC3\xA9ton chaud", 8, "m1", 60.0);
        add("Glitch sec", 11, nullptr, 0.0);
        pk.set("presets", arr);
        if (files::writeText(files::join(root, "D\xC3\x89MO \xC2\xB7 RAGE LOURDE.raw2pack"), pk.dump(true))) n += 3;
    }
    scanPacks();
    msg = "packs d\xC3\xA9mo install\xC3\xA9s \xC2\xB7 " + std::to_string(n) + " presets";
    return n;
}

bool Model::removePack(int pi) {
    if (pi < 0 || pi >= (int)packs.size() || packs[(size_t)pi].mine) return false;
    const Pack& pk = packs[(size_t)pi];
    bool ok;
    if (pk.name == "DIVERS" && pk.path == files::raw2Dir()) {  // presets isolés : seulement ces fichiers
        ok = true;
        for (const PackPreset& p : pk.presets) ok = files::moveToTrash(p.path) && ok;
    } else {
        ok = files::moveToTrash(pk.path);
    }
    scanPacks();
    return ok;
}

} // namespace raw2
