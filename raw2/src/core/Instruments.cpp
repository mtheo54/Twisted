#include "core/Instruments.h"

#include "core/Files.h"
#include "core/Wav.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>

namespace raw2 {

uint64_t nextSoundSerial() {
    static std::atomic<uint64_t> n{1};
    return n.fetch_add(1, std::memory_order_relaxed);
}

namespace {

// note SFZ : nombre MIDI ou nom (c4 = 60, c#4, db4)
int parseKey(const std::string& v, int def) {
    if (v.empty()) return def;
    if (std::isdigit((unsigned char)v[0]) || v[0] == '-') return std::clamp(std::atoi(v.c_str()), 0, 127);
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // a b c d e f g
    const char l = (char)std::tolower((unsigned char)v[0]);
    if (l < 'a' || l > 'g') return def;
    int n = base[l - 'a'];
    size_t i = 1;
    if (i < v.size() && v[i] == '#') { ++n; ++i; }
    else if (i < v.size() && v[i] == 'b') { --n; ++i; }
    if (i >= v.size()) return def;
    return std::clamp(n + 12 * (std::atoi(v.c_str() + i) + 1), 0, 127);
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

using Opcodes = std::map<std::string, std::string>;

// Découpe une ligne SFZ en opcodes : « clé=valeur », la valeur va jusqu'à la prochaine clé (les
// noms de fichiers peuvent contenir des espaces).
void splitOpcodes(const std::string& line, std::vector<std::pair<std::string, std::string>>& out) {
    std::vector<size_t> keys;  // début de chaque « clé= »
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '=') continue;
        size_t k = i;
        while (k > 0 && (std::isalnum((unsigned char)line[k - 1]) || line[k - 1] == '_')) --k;
        if (k < i && (k == 0 || std::isspace((unsigned char)line[k - 1]))) keys.push_back(k);
    }
    for (size_t j = 0; j < keys.size(); ++j) {
        const size_t eq = line.find('=', keys[j]);
        const size_t end = j + 1 < keys.size() ? keys[j + 1] : line.size();
        out.push_back({line.substr(keys[j], eq - keys[j]), trim(line.substr(eq + 1, end - eq - 1))});
    }
}

} // namespace

const InstZone* Instrument::find(int key, int vel) const {
    const InstZone* best = nullptr;
    int bestD = 1 << 30;
    for (const InstZone& z : zones) {
        const int dk = key < z.lokey ? z.lokey - key : (key > z.hikey ? key - z.hikey : 0);
        const int dv = vel < z.lovel ? z.lovel - vel : (vel > z.hivel ? vel - z.hivel : 0);
        const int d = dk * 1000 + dv;
        if (d < bestD) { bestD = d; best = &z; }
        if (d == 0) break;
    }
    return best;
}

std::unique_ptr<Instrument> loadSfzInstrument(const std::string& sfzPath, std::string* err) {
    std::string text;
    if (!files::readText(sfzPath, text)) {
        if (err) *err = "fichier illisible";
        return nullptr;
    }
    auto inst = std::make_unique<Instrument>();
    inst->path = sfzPath;
    std::string dir = sfzPath;
    {
        const size_t s = dir.find_last_of("/\\");
        dir = s == std::string::npos ? std::string() : dir.substr(0, s);
        std::string base = sfzPath.substr(s == std::string::npos ? 0 : s + 1);
        if (base.size() > 4) base = base.substr(0, base.size() - 4);
        inst->name = base;
    }
    // commentaires (« // name: », « // category: » des instruments de RAW 2), #define, en-têtes
    std::map<std::string, std::string> defines;
    std::string defaultPath;
    Opcodes global, master, group, region;
    std::string header;
    std::vector<Opcodes> regions;
    auto flushRegion = [&]() {
        if (header != "region") return;
        Opcodes o = global;
        for (const auto& kv : master) o[kv.first] = kv.second;
        for (const auto& kv : group) o[kv.first] = kv.second;
        for (const auto& kv : region) o[kv.first] = kv.second;
        regions.push_back(o);
        region.clear();
    };
    size_t p = 0;
    bool inBlock = false;
    while (p <= text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // commentaires de bloc et de fin de ligne
        if (inBlock) {
            const size_t c = line.find("*/");
            if (c == std::string::npos) continue;
            line = line.substr(c + 2);
            inBlock = false;
        }
        for (size_t c; (c = line.find("/*")) != std::string::npos;) {
            const size_t d = line.find("*/", c + 2);
            if (d == std::string::npos) { line = line.substr(0, c); inBlock = true; break; }
            line.erase(c, d + 2 - c);
        }
        if (const size_t c = line.find("//"); c != std::string::npos) {
            const std::string com = trim(line.substr(c + 2));
            if (com.rfind("name:", 0) == 0) inst->name = trim(com.substr(5));
            else if (com.rfind("category:", 0) == 0) inst->category = trim(com.substr(9));
            else if (com.rfind("license:", 0) == 0) inst->license = trim(com.substr(8));
            line = line.substr(0, c);
        }
        line = trim(line);
        if (line.empty()) continue;
        if (line.rfind("#define", 0) == 0) {
            std::string rest = trim(line.substr(7));
            const size_t sp = rest.find_first_of(" \t");
            if (sp != std::string::npos) defines[rest.substr(0, sp)] = trim(rest.substr(sp));
            continue;
        }
        if (line[0] == '#') continue;  // #include et autres directives : non gérées
        // substitution des $VARIABLES (les plus longues d'abord)
        for (auto it = defines.rbegin(); it != defines.rend(); ++it)
            for (size_t q; (q = line.find(it->first)) != std::string::npos;) line.replace(q, it->first.size(), it->second);
        // une ligne peut contenir plusieurs en-têtes : on la découpe
        size_t q = 0;
        while (q < line.size()) {
            const size_t lt = line.find('<', q);
            const std::string chunk = trim(line.substr(q, lt == std::string::npos ? std::string::npos : lt - q));
            if (!chunk.empty()) {
                std::vector<std::pair<std::string, std::string>> ops;
                splitOpcodes(chunk, ops);
                for (auto& kv : ops) {
                    if (header == "control") { if (kv.first == "default_path") defaultPath = kv.second; }
                    else if (header == "global") global[kv.first] = kv.second;
                    else if (header == "master") master[kv.first] = kv.second;
                    else if (header == "group") group[kv.first] = kv.second;
                    else if (header == "region") region[kv.first] = kv.second;
                }
            }
            if (lt == std::string::npos) break;
            const size_t gt = line.find('>', lt);
            if (gt == std::string::npos) break;
            flushRegion();
            header = line.substr(lt + 1, gt - lt - 1);
            if (header == "group") group.clear();
            if (header == "master") { master.clear(); group.clear(); }
            if (header == "global") { global.clear(); master.clear(); group.clear(); }
            q = gt + 1;
        }
    }
    flushRegion();

    std::map<std::string, size_t> loaded;  // un WAV chargé une seule fois
    std::vector<int> bufCh;
    std::vector<double> bufSr;
    for (const Opcodes& o : regions) {
        auto get = [&](const char* k) { const auto it = o.find(k); return it == o.end() ? std::string() : it->second; };
        if (get("trigger") == "release" || get("trigger") == "release_key") continue;
        if (!get("seq_position").empty() && std::atoi(get("seq_position").c_str()) != 1) continue;  // alternances : la première
        std::string smp = get("sample");
        if (smp.empty() || smp[0] == '*') continue;  // générateurs internes : non gérés
        for (char& ch : smp)
            if (ch == '\\') ch = '/';
        const std::string full = files::join(dir, defaultPath + smp);
        size_t bi;
        if (const auto it = loaded.find(full); it != loaded.end()) {
            bi = it->second;
        } else {
            std::vector<unsigned char> bytes;
            std::vector<float> data;
            int ch = 0;
            double sr = 44100.0;
            if (!files::read(full, bytes) || !parseWavMulti(bytes, data, ch, sr, 48000u * 60u)) continue;  // FLAC, OGG… : non lus
            inst->bytes += data.size() * sizeof(float);
            inst->buffers.push_back(std::move(data));
            bufCh.push_back(ch);
            bufSr.push_back(sr);
            bi = inst->buffers.size() - 1;
            loaded[full] = bi;
        }
        InstZone z;
        z.ch = bufCh[bi];
        z.sr = bufSr[bi];
        const std::vector<float>& buf = inst->buffers[bi];
        z.data = buf.data();
        z.frames = (int)(buf.size() / (size_t)std::max(1, z.ch));
        const std::string key = get("key");
        z.lokey = parseKey(get("lokey"), key.empty() ? 0 : parseKey(key, 0));
        z.hikey = parseKey(get("hikey"), key.empty() ? 127 : parseKey(key, 127));
        z.root = parseKey(get("pitch_keycenter"), key.empty() ? 60 : parseKey(key, 60));
        z.lovel = std::clamp(get("lovel").empty() ? 1 : std::atoi(get("lovel").c_str()), 0, 127);
        z.hivel = std::clamp(get("hivel").empty() ? 127 : std::atoi(get("hivel").c_str()), 0, 127);
        z.tune = (float)std::atof(get("tune").c_str()) + 100.f * (float)std::atof(get("transpose").c_str());
        // volume de la zone + gains additifs des en-têtes (SFZ v2 : global_volume, master_volume, group_volume)
        const float volDb = (float)(std::atof(get("volume").c_str()) + std::atof(get("global_volume").c_str()) +
                                    std::atof(get("master_volume").c_str()) + std::atof(get("group_volume").c_str()));
        z.gain = std::pow(10.f, std::clamp(volDb, -144.f, 24.f) / 20.f);
        const std::string lm = get("loop_mode").empty() ? get("loopmode") : get("loop_mode");
        const std::string ls = get("loop_start").empty() ? get("loopstart") : get("loop_start");
        const std::string le = get("loop_end").empty() ? get("loopend") : get("loop_end");
        z.loopStart = std::clamp(std::atoi(ls.c_str()), 0, std::max(0, z.frames - 2));
        z.loopEnd = std::clamp(le.empty() ? z.frames - 1 : std::atoi(le.c_str()), 0, z.frames - 1);
        z.loop = (lm == "loop_continuous" || lm == "loop_sustain") && z.loopEnd > z.loopStart + 8;
        z.offset = std::clamp(std::atoi(get("offset").c_str()), 0, std::max(0, z.frames - 1));
        if (z.lokey > z.hikey) std::swap(z.lokey, z.hikey);
        inst->zones.push_back(z);
    }
    if (inst->zones.empty()) {
        if (err) *err = regions.empty() ? "aucune zone (<region>)" : "aucun WAV lisible (FLAC/OGG non lus : convertir en WAV)";
        return nullptr;
    }
    return inst;
}

// ---- banque --------------------------------------------------------------------------------

InstrumentBank::InstrumentBank(bool offline) : offline_(offline) {
    if (!offline_) worker_ = std::thread([this] { workerMain(); });
}

InstrumentBank::~InstrumentBank() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        run_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    for (auto& c : cur_) delete c.exchange(nullptr);
    for (auto& r : retired_) delete r.in;
}

void InstrumentBank::scan() {
    std::vector<InstEntry> out;
    for (const std::string& d : files::listDirs(files::join(files::raw2Dir(), "Instruments"))) {
        for (const std::string& f : files::listFiles(d, ".sfz")) {
            InstEntry e;
            e.path = f;
            e.name = files::baseName(f);
            if (e.name.size() > 4) e.name = e.name.substr(0, e.name.size() - 4);
            // en-tête de RAW 2 : nom et catégorie lus dans les premières lignes
            std::string text;
            if (files::readText(f, text)) {
                const std::string head = text.substr(0, 600);
                auto field = [&](const char* k) {
                    const size_t q = head.find(k);
                    if (q == std::string::npos) return std::string();
                    const size_t e2 = head.find('\n', q);
                    return trim(head.substr(q + std::char_traits<char>::length(k), e2 == std::string::npos ? std::string::npos : e2 - q - std::char_traits<char>::length(k)));
                };
                if (const std::string n = field("// name:"); !n.empty()) e.name = n;
                e.category = field("// category:");
            }
            if (e.category.empty()) e.category = "SFZ";
            out.push_back(e);
        }
    }
    std::sort(out.begin(), out.end(), [](const InstEntry& a, const InstEntry& b) { return a.category != b.category ? a.category < b.category : a.name < b.name; });
    std::lock_guard<std::mutex> lk(mu_);
    catalog_ = std::move(out);
}

std::vector<InstEntry> InstrumentBank::catalog() const {
    std::lock_guard<std::mutex> lk(mu_);
    return catalog_;
}

void InstrumentBank::publish(int osc, Instrument* in) {
    Instrument* before = cur_[(size_t)osc].exchange(in, std::memory_order_acq_rel);
    if (!before) return;
    const uint64_t ep = epoch_ ? epoch_->load(std::memory_order_seq_cst) : 0;
    const bool busy = inProcess_ ? inProcess_->load(std::memory_order_seq_cst) : false;
    retired_.push_back({before, ep, busy && !offline_});
    collectLocked(false);
}

void InstrumentBank::collectLocked(bool force) {
    const uint64_t ep = epoch_ ? epoch_->load(std::memory_order_seq_cst) : 0;
    for (size_t i = 0; i < retired_.size();) {
        const Retired& r = retired_[i];
        if (force || !r.busy || ep > r.epoch) {
            delete r.in;
            retired_.erase(retired_.begin() + (long)i);
        } else {
            ++i;
        }
    }
}

void InstrumentBank::load(int osc, const std::string& path) {
    if (osc < 0 || osc > 1) return;
    std::unique_lock<std::mutex> lk(mu_);
    wantName_[(size_t)osc] = files::baseName(path);
    error_[(size_t)osc].clear();
    const uint64_t t = ++ticket_[(size_t)osc];
    if (offline_) {
        lk.unlock();
        std::string err;
        auto in = loadSfzInstrument(path, &err);
        lk.lock();
        if (!in) { error_[(size_t)osc] = err; return; }
        wantName_[(size_t)osc] = in->name;
        publish(osc, in.release());
        return;
    }
    pending_[(size_t)osc] = true;
    jobs_.push_back({osc, path, t});
    cv_.notify_all();
}

bool InstrumentBank::loadByName(int osc, const std::string& name) {
    std::string path;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (catalog_.empty()) {
            // première demande : le catalogue n'a jamais été lu
        }
        for (const auto& e : catalog_)
            if (e.name == name) path = e.path;
    }
    if (path.empty()) {
        scan();
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& e : catalog_)
            if (e.name == name) path = e.path;
    }
    if (path.empty()) {
        std::lock_guard<std::mutex> lk(mu_);
        wantName_[(size_t)osc] = name;
        error_[(size_t)osc] = "instrument introuvable : installe la banque de sons dans Documents \xE2\x80\xBA Raw 2 \xE2\x80\xBA Instruments";
        if (!offline_) files::logLine("instrument introuvable : " + name);
        return false;
    }
    load(osc, path);
    return true;
}

void InstrumentBank::clear(int osc) {
    if (osc < 0 || osc > 1) return;
    std::lock_guard<std::mutex> lk(mu_);
    ++ticket_[(size_t)osc];  // un chargement en cours ne sera pas publié
    pending_[(size_t)osc] = false;
    wantName_[(size_t)osc].clear();
    error_[(size_t)osc].clear();
    publish(osc, nullptr);
}

bool InstrumentBank::overview(int osc, int key, int vel, int cols, InstOverview& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    const Instrument* in = cur_[(size_t)osc].load();
    if (!in || in->zones.empty()) return false;
    const InstZone* z = in->find(key, vel);
    if (!z) return false;
    const int zi = (int)(z - in->zones.data());
    if (out.serial == in->serial && out.zone == zi && (int)out.mx.size() == cols) return true;
    out.serial = in->serial;
    out.zone = zi;
    out.zones = (int)in->zones.size();
    out.lokey = z->lokey;
    out.hikey = z->hikey;
    out.root = z->root;
    out.loop = z->loop;
    out.seconds = z->sr > 0.0 ? (double)z->frames / z->sr : 0.0;
    out.mn.assign((size_t)cols, 0.f);
    out.mx.assign((size_t)cols, 0.f);
    const size_t n = (size_t)std::max(0, z->frames), ch = (size_t)std::max(1, z->ch);
    for (int k = 0; k < cols && n > 0 && z->data; ++k) {
        const size_t a = n * (size_t)k / (size_t)cols, b = std::max(a + 1, n * (size_t)(k + 1) / (size_t)cols);
        float lo = 0.f, hi = 0.f;
        for (size_t i = a; i < b && i < n; ++i) {
            const float v = z->data[i * ch];
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        out.mn[(size_t)k] = lo;
        out.mx[(size_t)k] = hi;
    }
    return true;
}

std::string InstrumentBank::name(int osc) const {
    std::lock_guard<std::mutex> lk(mu_);
    return wantName_[(size_t)osc];
}

std::string InstrumentBank::category(int osc) const {
    const Instrument* in = cur_[(size_t)osc].load();
    return in ? in->category : std::string();
}

std::string InstrumentBank::lastError(int osc) const {
    std::lock_guard<std::mutex> lk(mu_);
    return error_[(size_t)osc];
}

json::Value InstrumentBank::save(int osc) const {
    json::Value v = json::Value::object();
    v.set("name", name(osc));
    return v;
}

void InstrumentBank::loadJson(int osc, const json::Value& v) {
    if (!v.isObject() || v["name"].str().empty()) { clear(osc); return; }
    loadByName(osc, v["name"].str());
}

void InstrumentBank::workerMain() {
    std::unique_lock<std::mutex> lk(mu_);
    while (run_) {
        cv_.wait_for(lk, std::chrono::milliseconds(200), [&] { return !run_ || !jobs_.empty(); });
        collectLocked(false);
        if (!run_ || jobs_.empty()) continue;
        const Job job = jobs_.front();
        jobs_.erase(jobs_.begin());
        lk.unlock();
        std::string err;
        auto in = loadSfzInstrument(job.path, &err);
        lk.lock();
        if (job.ticket != ticket_[(size_t)job.osc]) continue;  // remplacé ou annulé entre-temps
        pending_[(size_t)job.osc] = false;
        if (!in) {
            error_[(size_t)job.osc] = err;
            files::logLine("instrument illisible : " + job.path + " (" + err + ")");
            continue;
        }
        wantName_[(size_t)job.osc] = in->name;
        publish(job.osc, in.release());
    }
}

} // namespace raw2
