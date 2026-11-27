#include "core/Skin.h"
#include "core/Files.h"
#include "core/Json.h"
#include "core/Zip.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace raw2 {

namespace {

struct Token {
    const char* name;
    size_t off;
};
#define TK(n) Token{#n, offsetof(Theme, n)}
const Token kTokens[] = {
    TK(chassis), TK(chDot), TK(card), TK(line), TK(rule), TK(ink), TK(muted), TK(scr), TK(scrLine), TK(scrGrid),
    TK(scrMid), TK(scrText), TK(scrMuted), TK(scrRule), TK(acc), TK(onAcc), TK(hl), TK(ledTab), TK(ptr), TK(tabBg),
    TK(tabFg), TK(logoBg), TK(btnBg), TK(btnEdge), TK(waveA), TK(waveB), TK(barHi), TK(barLo), TK(barPk), TK(tickOff),
    TK(knobFace), TK(knobLine), TK(lifeBg), TK(lifeLine), TK(dead), TK(railBg), TK(keyNat), TK(keyNatLine), TK(keySharp),
    TK(crs), TK(sw), TK(inputBg), TK(div), TK(deadFill), TK(cellEdge), TK(tagAcc),
};
#undef TK

// nom de dossier valable sous Windows et macOS
std::string safeName(const std::string& raw) {
    std::string n;
    for (char ch : raw)
        if (!std::strchr("<>:\"/\\|?*", ch) && (unsigned char)ch >= 0x20) n += ch;
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    if (n.size() > 60) n.resize(60);
    return n.empty() ? std::string("skin") : n;
}

std::string lower(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

bool endsWith(const std::string& s, const char* e) {
    const size_t n = std::strlen(e);
    return s.size() >= n && lower(s.substr(s.size() - n)) == e;
}

float clampf(double v, float lo, float hi) { return std::clamp((float)v, lo, hi); }

// fichiers qu'un skin peut contenir (le reste de l'archive est ignoré)
bool allowedFile(const std::string& name) {
    static const char* ext[] = {".json", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".webp", ".heic",
                                ".mp4", ".mov", ".m4v", ".wmv", ".avi"};
    for (const char* e : ext)
        if (endsWith(name, e)) return true;
    return false;
}

} // namespace

bool parseColor(const std::string& raw, Color& out) {
    std::string s;
    for (char ch : raw)
        if (ch != ' ') s += ch;
    if (s.empty()) return false;
    auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    if (s[0] == '#') {
        const std::string h = s.substr(1);
        for (char ch : h)
            if (hex(ch) < 0) return false;
        int r, g, b, a = 255;
        if (h.size() == 3) { r = hex(h[0]) * 17; g = hex(h[1]) * 17; b = hex(h[2]) * 17; }
        else if (h.size() == 6 || h.size() == 8) {
            r = hex(h[0]) * 16 + hex(h[1]); g = hex(h[2]) * 16 + hex(h[3]); b = hex(h[4]) * 16 + hex(h[5]);
            if (h.size() == 8) a = hex(h[6]) * 16 + hex(h[7]);
        } else return false;
        out = Color{r / 255.f, g / 255.f, b / 255.f, a / 255.f};
        return true;
    }
    float v[4] = {0, 0, 0, 1};
    int n = 0;
    if (std::sscanf(s.c_str(), "rgba(%f,%f,%f,%f)", &v[0], &v[1], &v[2], &v[3]) == 4) n = 4;
    else if (std::sscanf(s.c_str(), "rgb(%f,%f,%f)", &v[0], &v[1], &v[2]) == 3) n = 3;
    if (n == 0) return false;
    out = Color{std::clamp(v[0], 0.f, 255.f) / 255.f, std::clamp(v[1], 0.f, 255.f) / 255.f, std::clamp(v[2], 0.f, 255.f) / 255.f, std::clamp(v[3], 0.f, 1.f)};
    return true;
}

std::string colorHex(const Color& c) {
    auto q = [](float v) { return (int)std::lround(std::clamp(v, 0.f, 1.f) * 255.f); };
    char buf[16];
    if (c.a >= 0.999f) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", q(c.r), q(c.g), q(c.b));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", q(c.r), q(c.g), q(c.b), q(c.a));
    return buf;
}

const Theme* themeByKey(const std::string& key) {
    for (int i = 0; i < kNumThemes; ++i)
        if (key == kThemes[i].key) return &kThemes[i];
    return nullptr;
}

bool setThemeToken(Theme& t, const std::string& name, Color c) {
    if (name == "old" || name == "mid") {  // dégradé d'âge des cellules : couleur de fin (et du milieu)
        const Rgb8 v{(int)std::lround(c.r * 255.f), (int)std::lround(c.g * 255.f), (int)std::lround(c.b * 255.f)};
        if (name == "old") t.old = v;
        else { t.mid = v; t.hasMid = true; }
        return true;
    }
    for (const Token& k : kTokens)
        if (name == k.name) {
            *reinterpret_cast<Color*>(reinterpret_cast<char*>(&t) + k.off) = c;
            if (name == "acc") {  // dégradé d'âge des cellules de VIE : part de l'accent
                t.accRGB = Rgb8{(int)std::lround(c.r * 255.f), (int)std::lround(c.g * 255.f), (int)std::lround(c.b * 255.f)};
            } else if (name == "deadFill") t.hasDeadFill = true;
            else if (name == "cellEdge") t.hasCellEdge = true;
            else if (name == "tagAcc") t.hasTagAcc = true;
            return true;
        }
    return false;
}

std::vector<std::string> themeTokenNames() {
    std::vector<std::string> v;
    for (const Token& k : kTokens) v.push_back(k.name);
    v.push_back("old");
    v.push_back("mid");
    return v;
}

bool parseSkin(const std::string& text, Skin& s, std::string& error) {
    json::Value v;
    if (!json::Value::parse(text, v) || !v.isObject()) { error = "skin.json illisible"; return false; }
    if (v.has("format") && v["format"].str() != "raw2skin") { error = "ce fichier n'est pas un skin RAW 2"; return false; }
    if ((int)v["version"].num(1) > 1) { error = "skin cr\xC3\xA9\xC3\xA9 pour une version plus r\xC3\xA9" "cente de RAW 2"; return false; }
    s = Skin{};
    s.name = v["name"].str().empty() ? std::string("Skin") : v["name"].str();
    s.author = v["author"].str();
    if (themeByKey(v["base"].str())) s.base = v["base"].str();
    for (const auto& kv : v["colors"].members()) {
        Color c;
        if (parseColor(kv.second.str(), c)) s.colors.emplace_back(kv.first, c);
    }
    const json::Value& bg = v["background"];
    s.background = files::baseName(bg["file"].str());
    s.voile = clampf(bg["voile"].num(0.35), 0.f, 0.9f);
    s.grid = bg["grille"].boolean(true);
    const json::Value& gl = v["glass"];
    s.glass = gl["on"].boolean(false);
    s.blur = clampf(gl["flou"].num(0.6), 0.f, 1.f);
    s.tint = clampf(gl["teinte"].num(0.35), 0.f, 1.f);
    s.edge = clampf(gl["reflet"].num(0.6), 0.f, 1.f);
    s.cards = clampf(v["cards"]["opacite"].num(0.85), 0.5f, 1.f);
    for (const auto& kv : v["layout"].members()) {
        const json::Value& p = kv.second;
        if (p.isArray() && p.size() >= 2 && p[(size_t)0].isNumber() && p[(size_t)1].isNumber()) {
            Skin::Place pl;
            pl.x = (float)p[(size_t)0].num();
            pl.y = (float)p[(size_t)1].num();
            pl.scale = p.size() >= 3 ? clampf(p[(size_t)2].num(1.0), 0.4f, 1.6f) : 1.f;  // rapetissé / agrandi
            s.layout.emplace_back(kv.first, pl);
        }
    }
    return true;
}

std::string skinToJson(const Skin& s) {
    json::Value v = json::Value::object();
    v.set("format", "raw2skin");
    v.set("version", 1);
    v.set("name", s.name);
    v.set("author", s.author);
    v.set("base", s.base);
    json::Value c = json::Value::object();
    for (const auto& kv : s.colors) c.set(kv.first, colorHex(kv.second));
    v.set("colors", c);
    json::Value bg = json::Value::object();
    bg.set("file", s.background);
    bg.set("voile", s.voile);
    bg.set("grille", s.grid);
    v.set("background", bg);
    json::Value gl = json::Value::object();
    gl.set("on", s.glass);
    gl.set("flou", s.blur);
    gl.set("teinte", s.tint);
    gl.set("reflet", s.edge);
    v.set("glass", gl);
    json::Value cd = json::Value::object();
    cd.set("opacite", s.cards);
    v.set("cards", cd);
    json::Value l = json::Value::object();
    for (const auto& kv : s.layout) {
        json::Value p = json::Value::array();
        p.push(kv.second.x);
        p.push(kv.second.y);
        if (std::fabs(kv.second.scale - 1.f) > 1e-4f) p.push(kv.second.scale);
        l.set(kv.first, p);
    }
    v.set("layout", l);
    return v.dump(true);
}

bool loadSkin(const std::string& dir, Skin& out, std::string& error) {
    std::string text;
    if (!files::readText(files::join(dir, "skin.json"), text)) { error = "skin.json introuvable"; return false; }
    if (!parseSkin(text, out, error)) return false;
    out.dir = dir;
    if (!out.background.empty() && !files::exists(files::join(dir, out.background))) out.background.clear();
    return true;
}

bool isSkinFile(const std::string& path) {
    // .zip : un skin exporté depuis la version web du studio (le navigateur n'enregistre pas .raw2skin)
    if (endsWith(path, ".raw2skin") || endsWith(path, ".zip")) return true;
    if (files::baseName(path) == "skin.json") return true;
    return files::isDir(path) && files::exists(files::join(path, "skin.json"));
}

bool installSkin(const std::string& path, std::string& dir, std::string& error) {
    std::vector<zip::Entry> entries;
    if (endsWith(path, ".raw2skin") || endsWith(path, ".zip")) {
        std::vector<unsigned char> data;
        if (!files::read(path, data)) { error = "fichier illisible"; return false; }
        if (!zip::read(data, entries, error)) return false;
    } else {
        // dossier (ou skin.json seul) : on prend skin.json et les images / vidéos à côté
        const std::string src = files::isDir(path) ? path : path.substr(0, path.size() - files::baseName(path).size());
        for (const std::string& f : files::listFiles(src, "")) {
            const std::string b = files::baseName(f);
            if (!allowedFile(b)) continue;
            zip::Entry e;
            e.name = b;
            if (files::read(f, e.data)) entries.push_back(std::move(e));
        }
    }
    const zip::Entry* js = nullptr;
    for (const zip::Entry& e : entries)
        if (files::baseName(e.name) == "skin.json") js = &e;
    if (!js) { error = "skin.json absent du skin"; return false; }
    Skin s;
    if (!parseSkin(std::string(js->data.begin(), js->data.end()), s, error)) return false;
    const std::string root = files::join(files::raw2Dir(), "Skins");
    dir = files::join(root, safeName(s.name));
    if (!files::makeDirs(dir)) { error = "impossible de cr\xC3\xA9" "er le dossier du skin"; return false; }
    for (const std::string& old : files::listFiles(dir, ""))  // ancienne version du même skin
        if (allowedFile(files::baseName(old))) files::removeFile(old);
    for (const zip::Entry& e : entries) {
        const std::string b = files::baseName(e.name);  // jamais de chemin : rien n'est écrit hors du dossier
        if (b.empty() || b[0] == '.' || !allowedFile(b)) continue;
        if (!files::write(files::join(dir, b), e.data)) { error = "\xC3\xA9" "criture impossible"; return false; }
    }
    return true;
}

std::vector<std::string> listSkins() {
    std::vector<std::string> user, fac;
    auto scan = [](const std::string& root, std::vector<std::string>& out) {
        if (root.empty() || !files::isDir(root)) return;
        for (const std::string& d : files::listDirs(root))
            if (files::exists(files::join(d, "skin.json"))) out.push_back(d);
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(files::baseName(a)) < lower(files::baseName(b)); });
    };
    scan(files::join(files::raw2Dir(), "Skins"), user);
    scan(files::join(files::factoryDir(), "Skins"), fac);
    // un skin d'usine que l'utilisateur a (ré)installé sous le même nom n'apparaît qu'une fois
    for (const std::string& f : fac) {
        bool dup = false;
        for (const std::string& u : user)
            if (lower(files::baseName(u)) == lower(files::baseName(f))) dup = true;
        if (!dup) user.push_back(f);
    }
    return user;
}

} // namespace raw2
