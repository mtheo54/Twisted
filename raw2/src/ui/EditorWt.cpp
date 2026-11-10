// Page WAVETABLE (§7.5) : cascade 3D / grille, éditeur de frame, traitements, import / export WAV.
#include "ui/Editor.h"
#include "core/Files.h"
#include "core/Wav.h"
#include "dsp/WavetableOps.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace raw2::ui {

namespace {
std::string p2(int n) { return fmt("%02d", n + 1); }
} // namespace

Wavetable& Editor::wtCur() { return m_.edit[(size_t)st_.wtTgt]; }
int Editor::wtFrames() {
    std::lock_guard<std::mutex> lk(m_.tableMutex);
    return std::max(1, wtCur().frames());
}
int Editor::wtSel() {
    const int last = wtFrames() - 1;
    return std::clamp((int)std::lround(P(st_.wtTgt == 0 ? kAPos : kBPos) * (float)last), 0, last);
}
void Editor::wtSetSel(int i) {
    const int last = wtFrames() - 1;
    setParam(st_.wtTgt == 0 ? kAPos : kBPos, last > 0 ? (float)std::clamp(i, 0, last) / (float)last : 0.f);
}

void Editor::wtPushUndo() {
    std::lock_guard<std::mutex> lk(m_.tableMutex);
    st_.undo.push_back(EditorState::Undo{st_.wtTgt, wtCur().fr, wtCur().hi, wtCur().hiOk});
    if (st_.undo.size() > 12) st_.undo.pop_front();
}

void Editor::wtMount() {
    bool mounted;
    {
        std::lock_guard<std::mutex> lk(m_.tableMutex);
        mounted = m_.mounted[(size_t)st_.wtTgt];
    }
    if (!mounted) m_.setMounted(st_.wtTgt, true);
}

void Editor::wtChanged(int frame) {
    {
        std::lock_guard<std::mutex> lk(m_.tableMutex);
        wtCur().spec.reset();
        // une frame retouchée repart de ses 256 points (sa version importée n'est plus la bonne)
        if (frame >= 0) wtCur().touch(frame);
    }
    m_.tablesChanged(st_.wtTgt, frame);
}

void Editor::wtSay(const std::string& m) {
    st_.wtMsg = m;
    st_.wtMsgT = now_;
}

// kind : 0..5 formes (sine..noise), 10 normaliser, 11 lisser, 12 distordre, 13 fondre, 14 inverser, 15 miroir, 16 RAZ
void Editor::wtOp(const std::string& label, int kind, bool forceNorm) {
    const int sel = wtSel();
    const bool table = st_.wtScopeTable;
    wtPushUndo();
    wtMount();
    {
        std::lock_guard<std::mutex> lk(m_.tableMutex);
        Wavetable& t = wtCur();
        std::vector<int> idx;
        if (table) for (int k = 0; k < t.frames(); ++k) idx.push_back(k);
        else idx.push_back(sel);
        static const Shape shapes[6] = {Shape::Sine, Shape::Saw, Shape::Square, Shape::Tri, Shape::Complex, Shape::Noise};
        if (kind < 6) applyPresetShape(t, shapes[kind], idx, table);
        else if (kind == 11) for (int k : idx) smoothFrame(t.fr[(size_t)k], st_.pSm);
        else if (kind == 12) for (int k : idx) distortFrame(t.fr[(size_t)k], st_.pDist);
        else if (kind == 13) blendBetweenFrames(t, idx, st_.pTsm);
        else if (kind == 14) for (int k : idx) for (float& v : t.fr[(size_t)k]) v = -v;
        else if (kind == 15) for (int k : idx) std::reverse(t.fr[(size_t)k].begin(), t.fr[(size_t)k].end());
        else if (kind == 16) for (int k : idx) for (int i = 0; i < kPoints; ++i) t.fr[(size_t)k][(size_t)i] = std::sin((float)i / kPoints * 6.2831853f);
        if (st_.autoNorm || forceNorm)
            for (int k : idx) normFrame(t.fr[(size_t)k]);
        for (int k : idx) t.touch(k);
    }
    wtChanged(table ? -1 : sel);
    wtSay(label + (table ? std::string(" \xC2\xB7 TABLE") : " \xC2\xB7 FRAME " + p2(sel)));
}

void Editor::wtAction(int id) {
    const int sel = wtSel();
    switch (id) {
        case 0: wtOp("NORMALISER", 10, true); break;
        case 1: {  // dupliquer
            if (sel >= wtFrames() - 1) { wtSay("DUPLIQUER \xC2\xB7 derni\xC3\xA8re frame"); break; }
            wtPushUndo();
            wtMount();
            {
                std::lock_guard<std::mutex> lk(m_.tableMutex);
                wtCur().fr[(size_t)sel + 1] = wtCur().fr[(size_t)sel];
            }
            wtChanged(sel + 1);
            wtSetSel(sel + 1);
            wtSay("DUPLIQU\xC3\x89" "E \xE2\x86\x92 FRAME " + p2(sel + 1));
            break;
        }
        case 2: wtOp("INVERSION", 14, false); break;
        case 3: wtOp("MIROIR", 15, false); break;
        case 4: {
            std::lock_guard<std::mutex> lk(m_.tableMutex);
            st_.clip = wtCur().fr[(size_t)sel];
            wtSay("COPI\xC3\x89" "E \xC2\xB7 FRAME " + p2(sel));
            break;
        }
        case 5: {
            if (!st_.clip) { wtSay("PRESSE-PAPIER VIDE"); break; }
            wtPushUndo();
            wtMount();
            {
                std::lock_guard<std::mutex> lk(m_.tableMutex);
                wtCur().fr[(size_t)sel] = *st_.clip;
            }
            wtChanged(sel);
            wtSay("COLL\xC3\x89" "E \xC2\xB7 FRAME " + p2(sel));
            break;
        }
        case 6: {
            if (st_.undo.empty()) { wtSay("RIEN \xC3\x80 ANNULER"); break; }
            const EditorState::Undo u = st_.undo.back();
            st_.undo.pop_back();
            {
                std::lock_guard<std::mutex> lk(m_.tableMutex);
                m_.edit[(size_t)u.osc].fr = u.fr;
                m_.edit[(size_t)u.osc].hi = u.hi;
                m_.edit[(size_t)u.osc].hiOk = u.hiOk;
                m_.edit[(size_t)u.osc].spec.reset();
            }
            m_.tablesChanged(u.osc);
            wtSay("ANNUL\xC3\x89");
            break;
        }
        case 7: wtOp("RAZ", 16, false); break;
        case 10: {  // interpolation 01 -> 64 (toujours la table entière)
            wtPushUndo();
            wtMount();
            {
                std::lock_guard<std::mutex> lk(m_.tableMutex);
                interpolateTable(wtCur());
            }
            wtChanged(-1);
            wtSay("INTERPOL. 01\xE2\x86\x92" + p2(wtFrames() - 1) + " \xC2\xB7 TABLE");
            break;
        }
        case 20: case 21: {  // moins / plus de frames : 1, 8, 16, 32, 64, 128, 256 (interpolées)
            static const int steps[7] = {1, 8, 16, 32, 64, 128, 256};
            const int cur = wtFrames();
            int k = 0;
            while (k < 6 && steps[k] < cur) ++k;
            if (id == 20) k = steps[k] >= cur ? std::max(0, k - 1) : k;
            else k = steps[k] <= cur ? std::min(6, k + 1) : k;
            if (steps[k] == cur) { wtSay(fmt("TABLE \xC2\xB7 %d FRAMES", cur)); break; }
            const float pos = P(st_.wtTgt == 0 ? kAPos : kBPos);
            wtPushUndo();
            wtMount();
            {
                std::lock_guard<std::mutex> lk(m_.tableMutex);
                resizeTable(wtCur(), steps[k]);
                wtCur().spec.reset();
            }
            m_.tablesChanged(st_.wtTgt);
            setParam(st_.wtTgt == 0 ? kAPos : kBPos, pos);
            wtSay(fmt("TABLE \xC2\xB7 %d FRAMES", steps[k]));
            break;
        }
        case 11: wtOp("LISSAGE", 11, false); break;
        case 12: wtOp("DISTORSION", 12, true); break;
        case 13: wtOp("FONTE ENTRE FRAMES", 13, false); break;
        default: break;
    }
}

void Editor::soundAs(int osc, int how) {
    const uint32_t idPlay = osc ? kBPlay : kAPlay;
    if (how == 0) {  // AUDIO : le son tel quel (STRETCH / RESAMPLE, ROOT)
        m_.instruments.clear(osc);
        setParam(idPlay, 1.f);
        return;
    }
    const SampleSet* s = m_.samples.audio(osc);  // fil de l'interface : seul lui remplace le son
    if (!s || s->base.empty()) return;
    const std::vector<float> data = s->base;
    const double sr = s->sr;
    std::string base = s->name;
    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".wav") == 0) base.resize(base.size() - 4);
    Wavetable t;
    const int r = tableFromAudioEx(data, sr, base, how == 2 ? kImpSpectral : kImpAuto, st_.wtImpFrames, t);
    if (!r) { wtSay("SON TROP COURT POUR UNE TABLE"); return; }
    {
        std::lock_guard<std::mutex> lk(m_.tableMutex);
        t.name = base + (how == 2 ? " (resynth.)" : ".wav");
        m_.edit[(size_t)osc] = t;
    }
    m_.tablesChanged(osc);
    m_.setMounted(osc, true);
    m_.instruments.clear(osc);
    setParam(idPlay, 0.f);
    st_.soundAs[(size_t)osc] = how;
}

void Editor::wtImport(const std::string& path, bool toOsc, int osc) {
    std::vector<unsigned char> bytes;
    std::vector<float> data;
    double sr = 44100.0;
    const std::string base = fileBaseName(path);
    if (!readFile(path, bytes) || !parseWav(bytes, data, &sr)) {
        wtSay("IMPORT \xC3\x89" "CHOU\xC3\x89 \xC2\xB7 WAV NON RECONNU");
        return;
    }
    // le son déposé est aussi gardé tel quel : jouable en SAMPLE (page WAVETABLE)
    m_.samples.load(osc, base + ".wav", data, sr);
    m_.instruments.clear(osc);  // le son déposé remplace l'instrument
    static const char* how[5] = {"", "CYCLES", "RESYNTH\xC3\x88SE", "TABLE", "D\xC3\x89" "COUPE"};
    if (toOsc) {
        Wavetable t;
        const int r = tableFromAudioEx(data, sr, base, st_.wtImpMode, st_.wtImpFrames, t);
        if (!r) { wtSay("IMPORT \xC3\x89" "CHOU\xC3\x89 \xC2\xB7 FICHIER TROP COURT"); return; }
        wtSay(fmt("IMPORT \xC2\xB7 %d FRAMES \xC2\xB7 ", t.frames()) + how[r]);
        {
            std::lock_guard<std::mutex> lk(m_.tableMutex);
            t.name = base + ".wav";
            m_.edit[(size_t)osc] = t;
        }
        m_.tablesChanged(osc);
        m_.setMounted(osc, true);
        // un vrai son (pas une table toute faite, pas un cycle) : joué tel quel ; le choix AUDIO /
        // WAVETABLE / RESYNTHÈSE reste dans l'écran de l'oscillateur (page SON)
        const bool isTable = r == kImpTable || data.size() < (size_t)(0.1 * sr);
        st_.soundAs[(size_t)osc] = r == kImpSpectral ? 2 : 1;
        setParam(osc ? kBPlay : kAPlay, isTable ? 0.f : 1.f);
        return;
    }
    const int prevTgt = st_.wtTgt;
    st_.wtTgt = osc;
    const int sel = wtSel();
    if (!st_.wtImpTable) {
        // une frame : un cycle du son (hauteur détectée), gardé en 2048 points
        Wavetable one;
        if (!tableFromAudioEx(data, sr, base, st_.wtImpMode == kImpSpectral ? kImpSpectral : kImpCycles, 1, one)) {
            wtSay("IMPORT \xC3\x89" "CHOU\xC3\x89 \xC2\xB7 FICHIER TROP COURT");
            st_.wtTgt = prevTgt;
            return;
        }
        wtPushUndo();
        wtMount();
        {
            std::lock_guard<std::mutex> lk(m_.tableMutex);
            Wavetable& t = wtCur();
            const int mid = one.frames() / 2;
            t.fr[(size_t)sel] = one.fr[(size_t)mid];
            // version 2048 points : copie de la table haute résolution (frames intactes gardées)
            auto hi = std::make_shared<std::vector<float>>((size_t)t.frames() * kHiLen, 0.f);
            std::vector<bool> ok((size_t)t.frames(), false);
            for (int f = 0; f < t.frames(); ++f)
                if (t.hiFrame(f)) {
                    std::copy(t.hi->data() + (size_t)f * kHiLen, t.hi->data() + (size_t)(f + 1) * kHiLen, hi->data() + (size_t)f * kHiLen);
                    ok[(size_t)f] = true;
                }
            if (one.hiFrame(mid)) {
                std::copy(one.hi->data() + (size_t)mid * kHiLen, one.hi->data() + (size_t)(mid + 1) * kHiLen, hi->data() + (size_t)sel * kHiLen);
                ok[(size_t)sel] = true;
            }
            t.hi = hi;
            t.hiOk = ok;
            t.spec.reset();
        }
        m_.tablesChanged(st_.wtTgt, sel);
        wtSay("IMPORT \xC2\xB7 FRAME " + p2(sel) + " \xE2\x86\x90 " + base + ".wav");
    } else {
        Wavetable t;
        const int r = tableFromAudioEx(data, sr, base, st_.wtImpMode, st_.wtImpFrames, t);
        if (!r) { wtSay("IMPORT \xC3\x89" "CHOU\xC3\x89 \xC2\xB7 FICHIER TROP COURT"); st_.wtTgt = prevTgt; return; }
        wtPushUndo();
        wtMount();
        {
            std::lock_guard<std::mutex> lk(m_.tableMutex);
            wtCur().fr = t.fr;
            wtCur().hi = t.hi;
            wtCur().hiOk = t.hiOk;
            wtCur().spec.reset();
            wtCur().name = base;
        }
        m_.tablesChanged(st_.wtTgt);
        wtSay(fmt("IMPORT \xC2\xB7 TABLE %d FRAMES \xC2\xB7 ", t.frames()) + how[r] + " \xE2\x86\x90 " + base + ".wav");
    }
}

void Editor::wtExport() {
    std::vector<unsigned char> wav;
    std::string name;
    {
        std::lock_guard<std::mutex> lk(m_.tableMutex);
        wav = wavetableToWav(wtCur());
        for (char ch : wtCur().name) {
            const unsigned char u = (unsigned char)ch;
            if ((u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u >= 0x80) name += ch;  // accents gardés (UTF-8)
            else if (!name.empty() && name.back() != '_') name += '_';
        }
    }
    while (!name.empty() && name.back() == '_') name.pop_back();
    if (name.empty()) name = "wavetable";
    const std::string dir = files::wavetableDir();
    files::makeDirs(dir);
    const std::string path = files::join(dir, name + ".wav");
    if (writeFile(path, wav)) wtSay("EXPORT \xC2\xB7 " + name + fmt(".wav \xC2\xB7 %d\xC3\x97" "2048 \xC2\xB7 Documents/RAW2 banque presets/wavetables", (int)((wav.size() - 44) / 4096)));
    else wtSay("EXPORT IMPOSSIBLE \xC2\xB7 DOSSIER INACCESSIBLE");
}

void Editor::drawWt() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const int tg = st_.wtTgt, sel = wtSel();
    const std::string O = tg == 0 ? "OSC A" : "OSC B";
    std::unique_lock<std::mutex> lk(m_.tableMutex);
    const Wavetable tab = wtCur();  // copie : le verrou est relâché pendant les interactions
    const bool mounted = m_.mounted[(size_t)tg];
    lk.unlock();
    const bool liveAudio = m_.readouts.active.load() && m_.readouts.voices.load() > 0;
    const float livePos = std::clamp(liveAudio ? (tg == 0 ? m_.readouts.posA.load() : m_.readouts.posB.load()) : P(tg == 0 ? kAPos : kBPos), 0.f, 1.f);

    // ============ TABLE ============
    const Rect tc{24.f, 73.f, 330.f, 863.f};
    ui_.card(tc);
    ui_.cardHeader(tc, "TABLE", false, 14.f, 12.f);
    {
        const Font f{Face::Mono, 10.f, 1.f};
        const std::string nm = ui_.fit(upper(tab.name), f, 152.f);
        ui_.tag(nm, tc.r() - 15.f, 86.f, Ui::Tag::Line);
    }
    if (ui_.button(makeId("wtTgt", 0), Rect{39.f, 130.f, 147.f, 30.f}, "OSC A", ui_.miniStyle(tg == 0))) st_.wtTgt = 0;
    if (ui_.button(makeId("wtTgt", 1), Rect{192.f, 130.f, 147.f, 30.f}, "OSC B", ui_.miniStyle(tg == 1))) st_.wtTgt = 1;
    if (ui_.button(makeId("wtView", 0), Rect{39.f, 166.f, 147.f, 30.f}, "CASCADE", ui_.miniStyle(!st_.wtGrid))) st_.wtGrid = false;
    if (ui_.button(makeId("wtView", 1), Rect{192.f, 166.f, 147.f, 30.f}, "GRILLE", ui_.miniStyle(st_.wtGrid))) st_.wtGrid = true;
    const Rect vbox{39.f, 210.f, 300.f, 367.3f};
    ui_.screen(vbox);
    const int nf = std::max(1, tab.frames());
    // cascade : au plus 64 lignes (une table de 256 frames montre une frame sur quatre)
    const int rows = std::min(nf, 64);
    auto rowOf = [&](float frame) { return nf > 1 ? frame * (float)(std::max(rows, 2) - 1) / (float)(nf - 1) : 0.f; };
    if (!st_.wtGrid) {
        const float s = 286.f / 340.f, ox0 = 46.f, oy0 = 217.f;
        auto P2 = [&](float x, float y) { return Pt{ox0 + x * s, oy0 + y * s}; };
        std::vector<Pt> pts, poly;
        const float rowK = 63.f / (float)std::max(1, rows - 1);  // même profondeur quel que soit le nombre de lignes
        for (int r = rows - 1; r >= 0; --r) {
            const int k = rows > 1 ? (int)std::lround((float)r * (float)(nf - 1) / (float)(rows - 1)) : 0;
            const Frame& a = tab.fr[(size_t)k];
            const float kk = (float)r * rowK;
            const float ox = 10.f + kk * 1.4f, oy = 380.f - kk * 5.3f;
            pts.clear();
            for (int j = 0; j <= 64; ++j) pts.push_back(P2(ox + (float)j * 3.4375f, oy - a[(size_t)((j * 4) & 255)] * 20.f));
            poly = pts;
            poly.push_back(P2(ox + 220.f, oy + 40.f));
            poly.push_back(P2(ox, oy + 40.f));
            c.fillPolygon(poly.data(), (int)poly.size(), T.scr);
            c.polyline(pts.data(), (int)pts.size(), T.waveA.withAlpha(0.95f - 0.6f * kk / 63.f), 1.f * s);
        }
        {
            const Frame& a = tab.fr[(size_t)sel];
            const float kk = rowOf((float)sel) * rowK;
            const float ox = 10.f + kk * 1.4f, oy = 380.f - kk * 5.3f;
            pts.clear();
            for (int j = 0; j <= 64; ++j) pts.push_back(P2(ox + (float)j * 3.4375f, oy - a[(size_t)((j * 4) & 255)] * 20.f));
            c.polyline(pts.data(), (int)pts.size(), T.acc, 2.f * s);
        }
        {
            const float fp = livePos * 63.f, ox = 10.f + fp * 1.4f, oy = 380.f - fp * 5.3f;  // position 0..1 : même profondeur
            pts.clear();
            for (int j = 0; j <= 64; ++j) pts.push_back(P2(ox + (float)j * 3.4375f, oy - sampleTable(tab, livePos, (float)j / 64.f) * 20.f));
            c.dashedPolyline(pts.data(), (int)pts.size(), T.hl, 1.2f * s, 3.f * s, 2.f * s);
        }
        const Font f{Face::Mono, 9.f * s, 1.f * s};
        c.text("01", P2(236.f, 396.f).x, P2(236.f, 396.f).y, f, T.scrMuted);
        c.text(p2(nf - 1), P2(322.f, 50.f).x, P2(322.f, 50.f).y, f, T.scrMuted);
    } else {
        // grille : 8 colonnes jusqu'à 64 frames, 16 au-delà
        const int cols = nf <= 64 ? 8 : 16, gr = (nf + cols - 1) / cols;
        const float cw = (300.f - 12.f - 3.f * (float)(cols - 1)) / (float)cols;
        const float chh = std::min(32.f, (280.f - 3.f * (float)(std::max(gr, 1) - 1)) / (float)std::max(gr, 1));
        for (int k = 0; k < nf; ++k) {
            const Rect r{45.f + (float)(k % cols) * (cw + 3.f), 216.f + (float)(k / cols) * (chh + 3.f), cw, chh};
            const bool on = k == sel;
            if (ui_.interact(makeId("wtCell", k), r).clicked) wtSetSel(k);
            c.fillRect(r, T.scr);
            ui_.border(r, on ? T.acc : T.scrRule, on ? 2.f : 1.f);
            std::vector<Pt> pts;
            for (int j = 0; j < 64; ++j) pts.push_back(Pt{r.x + (float)j / 63.f * r.w, r.y + (16.f - tab.fr[(size_t)k][(size_t)(j * 4)] * 13.f) / 32.f * r.h});
            c.polyline(pts.data(), (int)pts.size(), on ? T.acc : T.waveA, 1.2f);
            if (cols == 8) ui_.text(p2(k), r.x + 2.f, r.y + 1.f, Font{Face::Mono, 7.f, 0.28f}, on ? T.acc : T.scrMuted);
        }
    }
    // nombre de frames : moins / plus (1, 8, 16, 32, 64, 128, 256 ; les frames sont interpolées)
    if (ui_.button(makeId("wtNf", 0), Rect{39.f, 587.3f, 32.f, 32.f}, "\xE2\x88\x92", ui_.miniStyle(false))) wtAction(20);
    ui_.textCenter(fmt("%d FRAMES", nf), Rect{71.f, 596.8f, 112.f, 14.f}, Font{Face::MonoBold, 11.f, 0.8f}, T.ink);
    if (ui_.button(makeId("wtNf", 1), Rect{183.f, 587.3f, 32.f, 32.f}, "+", ui_.miniStyle(false))) wtAction(21);
    if (ui_.button(makeId("wtPrev"), Rect{258.f, 587.3f, 37.5f, 32.f}, "\xE2\x97\x80", ui_.miniStyle(false))) wtSetSel(sel - 1);
    if (ui_.button(makeId("wtNext"), Rect{301.5f, 587.3f, 37.5f, 32.f}, "\xE2\x96\xB6", ui_.miniStyle(false))) wtSetSel(sel + 1);
    {
        float rms = 0.f;
        for (float v : tab.fr[(size_t)sel]) rms += v * v;
        rms = std::sqrt(rms / kPoints);
        const char* labels[3] = {"FRAME", "RMS", "R\xC3\x89SOL."};
        const std::string vals[3] = {p2(sel) + "/" + p2(nf - 1), fmt("%d %%", (int)std::lround(rms * 100.f)), tab.hiFrame(sel) ? "2048 pts" : "256 pts"};
        for (int k = 0; k < 3; ++k) {
            const Rect r{39.f + (float)k * 102.65f, 629.3f, 94.7f, 55.f};
            ui_.box(r, T.railBg, T.rule);
            ui_.text(labels[k], r.x + 13.f, r.y + 9.f, Font{Face::Mono, 10.f, 1.2f}, T.muted);
            ui_.text(vals[k], r.x + 13.f, r.y + 26.f, Font{Face::MonoBold, 15.f, 0.f}, T.ink);
        }
    }
    ui_.text(mounted ? "\xE2\x97\x8F MONT\xC3\x89" "E SUR " + O : std::string("\xE2\x97\x8B NON MONT\xC3\x89" "E"), 39.f, 696.3f, Font{Face::Mono, 11.f, 0.88f}, T.hl);
    if (ui_.button(makeId("wtMount"), Rect{39.f, 718.3f, 300.f, 34.f}, mounted ? "[ D\xC3\x89MONTER DE " + O + " ]" : "[ MONTER SUR " + O + " ]", ui_.miniStyle(mounted)))
        m_.setMounted(tg, !mounted);
    ui_.text("LECTURE \xC2\xB7 2 CYCLES", 39.f, 766.3f, Font{Face::Mono, 10.f, 1.f}, T.muted);
    {
        const Rect rb{39.f, 785.3f, 300.f, 137.7f};
        ui_.screen(rb);
        const Rect a{44.f, 790.3f, 290.f, 127.7f};
        c.line(Pt{a.x, a.y + 45.f / 90.f * a.h}, Pt{a.r(), a.y + 45.f / 90.f * a.h}, T.scrMid, 1.f);
        std::vector<Pt> pts;
        for (int i = 0; i <= 160; ++i) pts.push_back(Pt{a.x + (float)i / 160.f * a.w, a.y + (45.f - sampleTable(tab, livePos, (float)i / 80.f) * 38.f) / 90.f * a.h});
        c.polyline(pts.data(), (int)pts.size(), T.waveA, 1.6f);
    }

    // ============ ÉDITEUR DE FRAME ============
    const Rect ec{366.f, 73.f, 586.f, 515.7f};
    ui_.card(ec);
    ui_.cardHeader(ec, "\xC3\x89" "DITEUR DE FRAME", false, 14.f, 12.f);
    ui_.tag("FRAME " + p2(sel) + " \xC2\xB7 " + O, ec.r() - 15.f, 86.f, Ui::Tag::Dark);
    {
        const char* tools[3] = {"CRAYON", "LISSAGE", "DISTORSION"};
        for (int k = 0; k < 3; ++k)
            if (ui_.button(makeId("wtTool", k), Rect{381.f + (float)k * 150.6f, 130.f, 144.6f, 30.f}, tools[k], ui_.miniStyle(st_.wtTool == k))) st_.wtTool = k;
        if (ui_.button(makeId("wtAuto"), Rect{832.7f, 130.f, 104.3f, 30.f}, st_.autoNorm ? "\xE2\x96\xA0 AUTO-NORM" : "\xE2\x96\xA1 AUTO-NORM", ui_.miniStyle(st_.autoNorm)))
            st_.autoNorm = !st_.autoNorm;
    }
    const Rect es{381.f, 174.f, 556.f, 313.6f};
    ui_.box(es, T.scr, T.scrLine);
    {
        const float s = 554.f / 640.f, ox = 382.f, oy = 175.f;
        auto P2 = [&](float x, float y) { return Pt{ox + x * s, oy + y * s}; };
        for (int i = 0; i <= 8; ++i) c.fillRect(Rect{ox + (float)i * 80.f * s, oy, 1.f, 360.f * s}, T.scrGrid);
        for (int i = 1; i < 8; ++i)
            if (i != 4) c.fillRect(Rect{ox, oy + (float)i * 45.f * s, 640.f * s, 1.f}, T.scrGrid);
        c.fillRect(Rect{ox, oy + 180.f * s, 640.f * s, 1.f}, T.scrMid);
        auto framePts = [&](const Frame& b, int step) {
            std::vector<Pt> v;
            for (int i = 0; i < 256; i += step) v.push_back(P2((float)i * 640.f / 255.f, 180.f - b[(size_t)i] * 170.f));
            return v;
        };
        if (sel > 0) { auto v = framePts(tab.fr[(size_t)sel - 1], 2); c.dashedPolyline(v.data(), (int)v.size(), T.waveB.withAlpha(0.35f), 1.f, 4.f, 3.f); }
        if (sel < nf - 1) { auto v = framePts(tab.fr[(size_t)sel + 1], 2); c.dashedPolyline(v.data(), (int)v.size(), T.waveB.withAlpha(0.35f), 1.f, 1.f, 3.f); }
        if (std::fabs(livePos * (float)(nf - 1) - (float)sel) > 0.4f) {
            std::vector<Pt> v;
            for (int i = 0; i < 256; i += 2) v.push_back(P2((float)i * 640.f / 255.f, 180.f - sampleTable(tab, livePos, (float)i / 256.f) * 170.f));
            c.dashedPolyline(v.data(), (int)v.size(), T.hl.withAlpha(0.6f), 1.f, 3.f, 2.f);
        }
        const Frame& a = tab.fr[(size_t)sel];
        std::vector<Pt> main = framePts(a, 1), area;
        area.push_back(P2(0.f, 180.f));
        area.insert(area.end(), main.begin(), main.end());
        area.push_back(P2(640.f, 180.f));
        c.fillPolygon(area.data(), (int)area.size(), T.waveA.withAlpha(0.1f));
        c.polyline(main.data(), (int)main.size(), T.waveA.withAlpha(0.18f), 6.f * s);
        c.polyline(main.data(), (int)main.size(), T.waveA, 2.f * s);
        for (int i = 0; i < 256; i += 8) {
            const Pt p = P2((float)i * 640.f / 255.f, 180.f - a[(size_t)i] * 170.f);
            c.fillRect(Rect{p.x - 2.f * s, p.y - 2.f * s, 4.f * s, 4.f * s}, T.acc);
        }
        const Font f{Face::Mono, 10.f * s, 1.f * s};
        c.text("+1.0", P2(8.f, 14.f).x, P2(8.f, 14.f).y, f, T.scrMuted);
        c.text("-1.0", P2(8.f, 354.f).x, P2(8.f, 354.f).y, f, T.scrMuted);

        // tracé à la souris
        const Rect area2{382.f, 175.f, 554.f, 311.6f};
        const Ui::Interact it = ui_.interact(makeId("wtEd"), area2, Cursor::Cross);
        const Pt m = ui_.in().mouse;
        const int x = std::clamp((int)std::lround((m.x - area2.x) / area2.w * 255.f), 0, 255);
        const float v = std::clamp(1.f - 2.f * (m.y - area2.y) / area2.h, -1.f, 1.f);
        auto apply = [&](bool first) {
            std::lock_guard<std::mutex> lk2(m_.tableMutex);
            Frame& fr = wtCur().fr[(size_t)sel];
            if (st_.wtTool == 0) penStroke(fr, first ? x : wtLastX_, first ? v : wtLastV_, x, v);
            else if (st_.wtTool == 1) penSmooth(fr, x);
            else penDistort(fr, x, st_.pDist);
            wtCur().spec.reset();
        };
        if (it.pressed) {
            wtPushUndo();
            wtMount();
            apply(true);
            wtDrawing_ = true;
            wtLastX_ = x;
            wtLastV_ = v;
            m_.tablesChanged(tg, sel);
        } else if (it.held && wtDrawing_ && (x != wtLastX_ || v != wtLastV_)) {
            apply(false);
            wtLastX_ = x;
            wtLastV_ = v;
            m_.tablesChanged(tg, sel);
        }
        if (it.released && wtDrawing_) {
            wtDrawing_ = false;
            if (st_.autoNorm) {
                {
                    std::lock_guard<std::mutex> lk2(m_.tableMutex);
                    normFrame(wtCur().fr[(size_t)sel]);
                }
                m_.tablesChanged(tg, sel);
            }
        }
    }
    ui_.text("SPECTRE \xC2\xB7 H1\xE2\x80\x93H32", 381.f, 497.6f, Font{Face::Mono, 10.f, 1.f}, T.muted);
    ui_.textRight("pointill\xC3\xA9s : frames voisines", 937.f + 1.f, 497.6f, Font{Face::Mono, 10.f, 1.f}, T.muted);
    {
        const Rect hb{381.f, 516.6f, 556.f, 59.1f};
        ui_.screen(hb);
        const float s = 542.f / 640.f, ox = 388.f, oy = 521.6f;
        const Frame& a = tab.fr[(size_t)sel];
        for (int h = 1; h <= 32; ++h) {
            double re = 0, im = 0;
            for (int i = 0; i < 256; ++i) {
                const double ph = 2.0 * 3.141592653589793 * h * i / 256.0;
                re += a[(size_t)i] * std::cos(ph);
                im += a[(size_t)i] * std::sin(ph);
            }
            const double mag = std::sqrt(re * re + im * im) * 2.0 / 256.0, db = 20.0 * std::log10(mag + 1e-5);
            const float hh = (float)std::max(1.0, std::min(1.0, (db + 60.0) / 60.0) * 52.0);
            c.fillRect(Rect{ox + ((float)(h - 1) * 20.f + 2.f) * s, oy + (56.f - hh) * s, 16.f * s, hh * s}, T.acc);
        }
    }

    // ============ IMPORT / EXPORT ============
    const Rect ic{366.f, 600.7f, 586.f, 335.3f};
    ui_.card(ic);
    ui_.cardHeader(ic, "IMPORT / EXPORT", false, 14.f, 12.f);
    ui_.tag(".wav", ic.r() - 15.f, 613.7f, Ui::Tag::Line);
    // méthode d'import d'un son quelconque, et nombre de frames voulu
    {
        const char* modes[3] = {"AUTO", "CYCLES", "RESYNTH."};
        ButtonStyle bs;
        for (int k = 0; k < 3; ++k) {
            bs = ui_.miniStyle(st_.wtImpMode == k);
            bs.font = Font{Face::Mono, 9.f, 0.6f};
            if (ui_.button(makeId("wtImpM", k), Rect{574.f + (float)k * 60.f, 609.7f, 56.f, 24.f}, modes[k], bs)) st_.wtImpMode = k;
        }
        const int fn[3] = {64, 128, 256};
        for (int k = 0; k < 3; ++k) {
            bs = ui_.miniStyle(st_.wtImpFrames == fn[k]);
            bs.font = Font{Face::Mono, 9.f, 0.6f};
            if (ui_.button(makeId("wtImpF", k), Rect{760.f + (float)k * 40.f, 609.7f, 36.f, 24.f}, std::to_string(fn[k]), bs)) st_.wtImpFrames = fn[k];
        }
    }
    if (ui_.button(makeId("wtImp", 0), Rect{381.f, 657.7f, 182.7f, 34.f}, "FRAME ACTIVE", ui_.miniStyle(!st_.wtImpTable))) st_.wtImpTable = false;
    if (ui_.button(makeId("wtImp", 1), Rect{569.7f, 657.7f, 182.7f, 34.f}, "TABLE ENTI\xC3\x88RE", ui_.miniStyle(st_.wtImpTable))) st_.wtImpTable = true;
    if (ui_.button(makeId("wtExp"), Rect{758.3f, 657.7f, 178.7f, 34.f}, "[ EXPORT .WAV ]", ui_.miniStyle(false))) wtExport();
    // LECTURE AU CLAVIER · SON DÉPOSÉ : WAVEFORM (table d'onde) ou SAMPLE (son joué tel quel)
    {
        const int o = st_.wtTgt;
        const uint32_t idPlay = o ? kBPlay : kAPlay, idMode = o ? kBSmpMode : kASmpMode, idRoot = o ? kBSmpRoot : kASmpRoot;
        const bool smp = P(idPlay) > 0.5f, has = m_.samples.has(o);
        ui_.text("LECTURE AU CLAVIER \xC2\xB7 SON D\xC3\x89POS\xC3\x89", 381.f, 703.7f, Font{Face::Mono, 10.f, 1.f}, T.muted);
        const bool inst = smp && !m_.instruments.name(o).empty();
        if (ui_.button(makeId("pmMode", 0), Rect{381.f, 722.7f, 180.f, 30.f}, "WAVEFORM", ui_.miniStyle(!smp))) setParam(idPlay, 0.f);
        if (ui_.button(makeId("pmMode", 1), Rect{567.f, 722.7f, 180.f, 30.f}, "SAMPLE", ui_.miniStyle(smp && !inst))) {
            m_.instruments.clear(o);
            setParam(idPlay, 1.f);
        }
        if (ui_.button(makeId("pmMode", 2), Rect{753.f, 722.7f, 184.f, 30.f}, "INSTRUMENT \xE2\x96\xBE", ui_.miniStyle(inst))) {
            instOpen_ = true;
            instOsc_ = o;
            m_.instruments.scan();
        }
        float iy = 758.7f;
        if (smp && !inst) {
            const bool res = P(idMode) > 0.5f;
            const Font bf{Face::Mono, 11.f, 1.1f};
            ButtonStyle a = ui_.miniStyle(!res), b = ui_.miniStyle(res), r = ui_.miniStyle(false);
            a.font = b.font = r.font = bf;
            const std::string rootTxt = "[ ROOT : " + formatParam(idRoot, P(idRoot)) + " ]";
            const float rw = ui_.width(rootTxt, bf) + 18.f, bw = (556.f - rw - 12.f) / 2.f;
            if (ui_.button(makeId("pmStr", 0), Rect{381.f, 758.7f, bw, 28.f}, "STRETCH", a)) setParam(idMode, 0.f);
            if (ui_.button(makeId("pmStr", 1), Rect{381.f + bw + 6.f, 758.7f, bw, 28.f}, "RESAMPLE", b)) setParam(idMode, 1.f);
            if (ui_.button(makeId("pmRoot"), Rect{937.f - rw, 758.7f, rw, 28.f}, rootTxt, r)) {  // C2 → C3 → C4 → C5
                const int root = (int)P(idRoot);
                setParam(idRoot, root >= 72 ? 36.f : (float)(root + 12));
            }
            iy = 792.7f;
        }
        std::string info;
        if (inst) {
            const std::string err = m_.instruments.lastError(o);
            info = "INSTRUMENT \xC2\xB7 " + m_.instruments.name(o) + (m_.instruments.loading(o) ? " \xC2\xB7 chargement\xE2\x80\xA6" : "") +
                   (err.empty() ? std::string(" \xC2\xB7 multi-\xC3\xA9" "chantillons, jou\xC3\xA9 sur tout le clavier") : " \xC2\xB7 " + err);
        } else if (!has) info = std::string("aucun son d\xC3\xA9pos\xC3\xA9 sur ") + O + " \xC2\xB7 le clavier joue la table d'onde";
        else if (!smp) info = "WAVEFORM \xC2\xB7 " + m_.samples.name(o) + " est lu comme table d'onde \xC2\xB7 passe en SAMPLE pour jouer le son tel quel";
        else {
            info = "SAMPLE \xC2\xB7 " + m_.samples.name(o) + fmt(" \xC2\xB7 %.2f s \xC2\xB7 ", m_.samples.seconds(o));
            if (P(idMode) > 0.5f) info += "RESAMPLE : la dur\xC3\xA9" "e change avec la hauteur";
            else {
                info += "STRETCH : dur\xC3\xA9" "e conserv\xC3\xA9" "e \xC3\xA0 toutes les notes";
                const int rd = m_.samples.ready(o);
                if (rd < SampleBank::total()) info += fmt(" \xC2\xB7 calcul %d/%d", rd, SampleBank::total());
            }
        }
        ui_.text(ui_.fit(info, Font{Face::Mono, 10.f, 0.5f}, 556.f), 381.f, iy, Font{Face::Mono, 10.f, 0.5f}, T.hl);
    }
    {
        const float dzTop = P(st_.wtTgt ? kBPlay : kAPlay) > 0.5f && m_.instruments.name(st_.wtTgt).empty() ? 812.f : 782.2f;
        const Rect dz{381.f, dzTop, 556.f, 882.f - dzTop};
        c.fillRect(dz, T.scr);
        if (dropWt_) {
            ui_.border(dz, T.acc, 1.f);
            for (int k = 1; k < 6; ++k) ui_.border(dz.inset((float)k * 2.f), T.acc.withAlpha(0.35f / (float)k), 2.f);
        } else {
            ui_.dashedBorder(dz, T.scrRule);
        }
        const Color fg = dropWt_ ? T.hl : T.scrText;
        ui_.textCenter("> DROP WAV TO LOAD", Rect{dz.x, dz.cy() - 17.f, dz.w, 18.f}, Font{Face::MonoBold, 14.f, 1.68f}, fg);
        static const char* why[3] = {" \xC2\xB7 table Serum / Vital telle quelle, sinon cycles", " \xC2\xB7 un cycle align\xC3\xA9 par frame",
                                     " \xC2\xB7 harmoniques resynth\xC3\xA9tis\xC3\xA9" "es"};
        const std::string sub = std::string("cible : ") + O + (st_.wtImpTable ? fmt(" \xC2\xB7 %d FRAMES", st_.wtImpFrames) : " \xC2\xB7 FRAME " + p2(sel)) +
                                why[std::clamp(st_.wtImpMode, 0, 2)];
        ui_.textCenter(sub, Rect{dz.x, dz.cy() + 5.f, dz.w, 13.f}, Font{Face::Mono, 10.f, 0.8f}, fg.withAlpha(0.7f));
    }
    ui_.text("> " + (now_ - st_.wtMsgT < 6000.0 ? ui_.fit(st_.wtMsg, Font{Face::Mono, 11.f, 0.66f}, 510.f) : std::string("pr\xC3\xAAt")), 381.f, 892.f, Font{Face::Mono, 11.f, 0.66f}, T.hl);
    ui_.text(fmt("export : %d frames \xC3\x97 2048 pts \xC2\xB7 16 bits \xC2\xB7 compatible import Serum", nf), 381.f, 910.f, Font{Face::Mono, 10.f, 0.6f}, T.muted);

    // ============ TRAITEMENT ============
    const Rect pc{964.f, 73.f, 292.f, 299.f};
    ui_.card(pc);
    ui_.cardHeader(pc, "TRAITEMENT", false, 14.f, 12.f);
    ui_.tag("frames", pc.r() - 15.f, 86.f, Ui::Tag::Line);
    {
        const Font lf{Face::Mono, 10.f, 1.f};
        ButtonStyle bs = ui_.miniStyle(false);
        bs.font = Font{Face::Mono, 10.f, 0.8f};
        // MORPH · POSITION
        ui_.text("MORPH \xC2\xB7 POSITION", 979.f, 132.f, lf, T.muted);
        ui_.textRight(p2(sel) + " / " + p2(nf - 1), 1241.f + 1.f, 132.f, lf, T.ink);
        {
            float v = nf > 1 ? (float)sel / (float)(nf - 1) : 0.f;
            if (ui_.hslider(makeId("wtSl", 0), Rect{979.f, 160.f, 158.f, 18.f}, v)) {
                const int ns = (int)std::lround(v * (float)(nf - 1));
                if (ns != sel) wtSetSel(ns);
            }
            const Ui::Interact it = ui_.interact(makeId("wtInterp"), Rect{1145.f, 150.f, 96.f, 38.f});
            ui_.drawButton(Rect{1145.f, 150.f, 96.f, 38.f}, "", bs, it.held);
            const float dy = it.held ? 2.f : 0.f, dx = dy;
            ui_.textCenter("INTERP.", Rect{1145.f + dx, 156.f + dy, 96.f, 13.f}, bs.font, T.ink);
            ui_.textCenter("01\xE2\x86\x92" + p2(nf - 1), Rect{1145.f + dx, 169.f + dy, 96.f, 13.f}, bs.font, T.ink);
            if (it.clicked) wtAction(10);
        }
        struct S { const char* label; float* v; const char* btn; int act; float y; };
        const S rows[3] = {{"LISSAGE", &st_.pSm, "LISSER", 11, 202.f}, {"DISTORSION HARM.", &st_.pDist, "DISTORDRE", 12, 259.f}, {"LISSAGE ENTRE FRAMES", &st_.pTsm, "FONDRE", 13, 316.f}};
        for (int k = 0; k < 3; ++k) {
            const S& r = rows[k];
            ui_.text(r.label, 979.f, r.y, lf, T.muted);
            ui_.textRight(fmt("%d %%", (int)std::lround(*r.v * 100.f)), 1241.f + 1.f, r.y, lf, T.ink);
            ui_.hslider(makeId("wtSl", k + 1), Rect{979.f, r.y + 21.5f, 158.f, 18.f}, *r.v);
            if (ui_.button(makeId("wtBtn", k), Rect{1145.f, r.y + 18.f, 96.f, 25.f}, r.btn, bs)) wtAction(r.act);
        }
    }

    // ============ PRÉRÉGLAGES / OUTILS ============
    const Rect fc{964.f, 384.f, 292.f, 552.f};
    ui_.card(fc);
    ui_.cardHeader(fc, "PR\xC3\x89R\xC3\x89GLAGES", false, 14.f, 12.f);
    ui_.tag("formes", fc.r() - 15.f, 397.f, Ui::Tag::Line);
    if (ui_.button(makeId("wtScope", 0), Rect{979.f, 441.f, 128.f, 30.f}, "FRAME", ui_.miniStyle(!st_.wtScopeTable))) st_.wtScopeTable = false;
    if (ui_.button(makeId("wtScope", 1), Rect{1113.f, 441.f, 128.f, 30.f}, "TABLE", ui_.miniStyle(st_.wtScopeTable))) st_.wtScopeTable = true;
    {
        const char* names[6] = {"SINE", "SAW", "SQUARE", "TRIANGLE", "COMPLEX", "NOISE"};
        for (int k = 0; k < 6; ++k) {
            const Rect r{979.f + (float)(k % 3) * 89.35f, 479.f + (float)(k / 3) * 71.5f, 83.3f, 65.5f};
            if (ui_.button(makeId("wtShape", k), r, names[k], ui_.miniStyle(false))) wtOp(names[k], k, true);
        }
        ui_.text("OUTILS", 979.f, 630.f, Font{Face::Mono, 10.f, 1.f}, T.muted);
        const char* acts[8] = {"NORMALISER", "DUPLIQUER", "INVERSER", "MIROIR", "COPIER", "COLLER", "ANNULER", "RAZ"};
        for (int k = 0; k < 8; ++k) {
            const Rect r{979.f + (float)(k % 2) * 134.f, 649.f + (float)(k / 2) * 70.f, 128.f, 64.f};
            if (ui_.button(makeId("wtAct", k), r, acts[k], ui_.miniStyle(false))) wtAction(k);
        }
    }
}

} // namespace raw2::ui
