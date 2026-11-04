// Éditeur : cadre général, en-tête, superpositions, actions. Les pages sont dans EditorPages.cpp,
// EditorMix.cpp et EditorWt.cpp.
#include "ui/Editor.h"
#include "core/Files.h"
#include "core/Wav.h"
#include "dsp/WavetableOps.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw2::ui {

namespace {
const Rect kPbPanel{320.f, 90.f, 640.f, 780.f};    // fenêtre de choix des presets
const Rect kInstPanel{370.f, 120.f, 540.f, 720.f};  // fenêtre de choix des instruments
const Rect kSavePanel{320.f, 330.f, 640.f, 236.f};  // fenêtre ENREGISTRER
constexpr float kPi = 3.14159265358979f;
const char* kTabs[8] = {"SON", "VIE", "MODULES", "ENV / LFO", "EFX", "MIX IA", "WAVETABLE", "JEU"};
// ordre d'affichage des onglets : JEU juste après SON (son numéro 7 garde la compatibilité des états)
const int kTabOrder[8] = {0, 7, 1, 2, 3, 4, 5, 6};
const char* kCats[7] = {"tous", "basse", "lead", "pad", "pluck", "arp", "fx"};
} // namespace

Editor::Editor(Model& m, ParamSink& sink, EditorState& st) : m_(m), sink_(sink), st_(st) {
    tab_ = m_.tab.load();
    for (auto& r : fxR_) r = 0.5f;
}

float Editor::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(rng_ & 0xFFFFFF) / 16777216.f;
}

// --- paramètres --------------------------------------------------------------------------------
void Editor::setParam(uint32_t id, float v) {
    cancelTarget(id);
    sink_.beginEdit(id);
    sink_.performEdit(id, v);
    sink_.endEdit(id);
}
void Editor::beginDrag(uint32_t id) {
    cancelTarget(id);
    sink_.beginEdit(id);
    dragParam_ = (int)id;
}
void Editor::dragParam(uint32_t id, float v) { sink_.performEdit(id, v); }
void Editor::endDrag(uint32_t id) {
    sink_.endEdit(id);
    dragParam_ = -1;
}

void Editor::knobParam(uint32_t id, Pt p, float size, float mod) {
    float v = toNormalized(id, P(id));
    const Rect kr{p.x, p.y, size, size};
    const Input& in = ui_.in();
    // glisser-déposer d'un LFO : ce potard est-il visé ?
    if (dd_.on && isModulatable(id) && ui_.hit(kr)) {
        dd_.hov = id;
        dd_.hovRect = kr;
    }
    // anneau d'une route libre : appuyer dessus puis glisser verticalement règle le dosage
    bool ring = rd_.route >= 0 && rd_.id == id;
    if (!ring && !dd_.on && in.pressed && ui_.hit(kr) && m_.routes.count() > 0) {
        const float s = size / 84.f, dx = in.mouse.x - (p.x + 42.f * s), dy = in.mouse.y - (p.y + 42.f * s);
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > 26.f * s && d < 37.f * s) {
            const int n = m_.routes.count();
            for (int i = 0; i < n; ++i)
                if ((uint32_t)m_.routes.r[(size_t)i].id.load() == id) {
                    rd_ = RingDrag{i, id, in.mouse.y, m_.routes.r[(size_t)i].amt.load()};
                    ring = true;
                    break;
                }
        }
    }
    if (ring) {
        ui_.drawKnob(p, size, v, mod, true);
    } else {
        bool began = false, ended = false;
        const bool changed = ui_.knob(makeId("knob", (int)id), p, size, v, mod, &began, &ended);
        if (began) beginDrag(id);
        if (changed) dragParam(id, fromNormalized(id, v));
        if (ended) endDrag(id);
    }
    knobRings(id, p, size);
}

void Editor::stepTargets() {
    if (now_ - st_.stepT < 40.0) return;
    st_.stepT = now_;
    for (uint32_t id = 0; id < kNumParams; ++id) {
        if (!st_.tgt[id]) continue;
        float step = 0.02f;
        if (id == kALvl || id == kBLvl) step = 0.5f / 18.2f;
        else if (id == kM1 || id == kM2) step = 2.f;
        else if (id == kM3) step = 1.f;
        const float cur = P(id), want = *st_.tgt[id];
        const float d = want - cur;
        float nv;
        if (std::fabs(d) <= step) {
            nv = want;
            st_.tgt[id].reset();
        } else {
            nv = cur + (d > 0 ? step : -step);
        }
        if (id == kM1 || id == kM2 || id == kM3) nv = std::round(nv);
        sink_.beginEdit(id);
        sink_.performEdit(id, nv);
        sink_.endEdit(id);
    }
}

// --- actions ------------------------------------------------------------------------------------
void Editor::loadPreset(int index) {
    for (auto& t : st_.tgt) t.reset();
    if (index >= 0 && index < kNumFactoryPresets) m_.applyFactoryPreset(index, &sink_);
    else m_.applyInit(&sink_);
    st_.undo.clear();
}

void Editor::loadPack(int pack, int preset) {
    for (auto& t : st_.tgt) t.reset();
    if (!m_.applyPackPreset(pack, preset, &sink_)) pbMsg_ = "lecture impossible (fichier d\xC3\xA9plac\xC3\xA9 ou abim\xC3\xA9 ?)";
    st_.undo.clear();
}

void Editor::panicNow() {
    m_.panic = true;
    mouseNote_ = -1;
    for (auto& t : st_.tgt) t.reset();
    for (auto& v : son_.sh) v = 1;
    for (auto& v : son_.pk) v = 1;
    for (auto& v : mix_.sh) v = 1;
    for (auto& v : mix_.pk) v = 1;
    if (st_.aiPhase == 1) st_.aiPhase = 0;
    ui_.releaseActive();
    paintMode_ = -1;
    bigLast_ = -1;
    panicT_ = now_;
}

void Editor::setTab(int t) {
    tab_ = t;
    m_.setTab(t);
    roleOpen_ = false;
    seedOpen_ = false;
    mixMenu_ = 0;
    fxMenu_ = 0;
}

void Editor::noteOn(int n) {
    if (mouseNote_ == n) return;
    if (mouseNote_ >= 0) m_.guiNotes.push(NoteEvent{0, 48 + mouseNote_, 0.f, false});
    mouseNote_ = n;
    m_.guiNotes.push(NoteEvent{0, 48 + n, 0.85f, true});
    hit_ = 1.f;
}

void Editor::noteOff(int n, bool force) {
    if (mouseNote_ != n) return;
    if (m_.kbHold.load() && !force) return;  // HOLD : la note reste tenue
    m_.guiNotes.push(NoteEvent{0, 48 + n, 0.f, false});
    mouseNote_ = -1;
}

void Editor::pushHist(const std::string& text, const std::string& status, const std::string& src, int undoKind, int undoSlot, uint32_t actUid) {
    const int t = (int)std::max(0.0, (now_ - st_.sessionT0) / 1000.0);
    st_.hist.insert(st_.hist.begin(), EditorState::Hist{fmt("%02d:%02d:%02d", t / 3600 % 100, t / 60 % 60, t % 60), text, status, src, undoKind, undoSlot, actUid});
    if (st_.hist.size() > 8) st_.hist.pop_back();
}

// --- spectres crantés ----------------------------------------------------------------------------
void Editor::Bars::resize(int n, double now) {
    if ((int)sh.size() == n) return;
    sh.assign((size_t)n, 1);
    pk.assign((size_t)n, 1);
    pkT.assign((size_t)n, now);
    pkD.assign((size_t)n, now);
    dropT.assign((size_t)n, now);
}

void Editor::Bars::push(int i, int nT, double now, double fall, double peakFall) {
    const size_t k = (size_t)i;
    if (nT >= sh[k]) sh[k] = nT;
    else if (now - dropT[k] >= fall) { sh[k]--; dropT[k] = now; }
    if (sh[k] >= pk[k]) { pk[k] = sh[k]; pkT[k] = now; pkD[k] = now; }
    else if (now - pkT[k] > 300.0 && now - pkD[k] >= peakFall) { pk[k]--; pkD[k] = now; }
}

// --- image ----------------------------------------------------------------------------------------
void Editor::frame(Canvas& c, const Input& in) {
    now_ = in.timeMs;
    if (st_.sessionT0 < 0.0) st_.sessionT0 = now_;
    const double dtFrame = lastFrame_ > 0.0 ? std::min(100.0, now_ - lastFrame_) : 16.0;
    lastFrame_ = now_;
    T_ = &kThemes[std::clamp(m_.theme.load(), 0, kNumThemes - 1)];
    if (m_.tab.load() != tab_) tab_ = m_.tab.load();
    an_.update(m_.scope, m_.readouts.sampleRate.load());

    // impulsion visuelle à chaque nouvelle note (clavier ou MIDI)
    const int held = m_.readouts.heldNote.load();
    if (held >= 0) lastNote_ = held;
    if (held >= 0 && held != lastHeld_) hit_ = 1.f;
    lastHeld_ = held;
    hit_ = std::max(0.f, hit_ - 0.07f * (float)(dtFrame / 16.67));
    if (held >= 0 || mouseNote_ >= 0) hit_ = std::max(hit_, 0.6f);

    // calques bloquants connus d'avance
    ui_.beginFrame(c, *T_, in);
    // les panneaux doivent bloquer dès le début de l'image : sinon le fond (qui ferme au clic dehors)
    // prend le clic avant les éléments du panneau
    if (pbOpen_ || saveOpen_ || instOpen_) ui_.addBlocker(Rect{0, 0, (float)kWidth, (float)kHeight}, 10);
    if (instOpen_) ui_.addBlocker(kInstPanel, 11);
    if (pbOpen_) ui_.addBlocker(kPbPanel, 11);
    if (pbOpen_ && pbGenreOpen_) ui_.addBlocker(Rect{pbGenreRect_.x, pbGenreRect_.y, pbGenreRect_.w + 3.f, pbGenreRect_.h + 3.f}, 12);
    if (saveOpen_) ui_.addBlocker(kSavePanel, 11);
    if (thOpen_) {
        ui_.addBlocker(Rect{0, 0, (float)kWidth, (float)kHeight}, 5);
        ui_.addBlocker(Rect{190.1f, 68.5f, 216.f, 112.f}, 6);
    }
    if (roleOpen_ && tab_ == 0) ui_.addBlocker(Rect{1061.f, 707.2f, 174.f, 190.f}, 3);
    if (mixMenu_ == 1 && tab_ == 5) ui_.addBlocker(Rect{889.f, 164.f, 153.f, 185.f}, 3);
    if (mixMenu_ == 2 && tab_ == 5) ui_.addBlocker(Rect{1065.8f, 164.f, 153.f, 155.f}, 3);
    if (seedOpen_ && tab_ == 0) ui_.addBlocker(Rect{877.2f, 313.2f, 134.f, 128.f}, 3);
    if (fxMenu_ != 0 && tab_ == 4) ui_.addBlocker(Rect{fxMenuRect_.x, fxMenuRect_.y, fxMenuRect_.w + 3.f, fxMenuRect_.h + 3.f}, 3);

    // touches globales
    if (in.keyEsc && pbOpen_ && pbGenreOpen_) {
        pbGenreOpen_ = false;  // Échap ferme d'abord le menu GENRE
    } else if (in.keyEsc) {
        pbOpen_ = false;
        thOpen_ = false;
        saveOpen_ = false;
        instOpen_ = false;
        roleOpen_ = seedOpen_ = false;
        mixMenu_ = 0;
        fxMenu_ = 0;
        ui_.focus = 0;
    }
    handleDrops();
    stepTargets();
    dd_.hov = 0xFFFFFFFFu;  // reposé par le potard survolé pendant le dessin

    // effet de secousse du clic long (mode X-Y)
    const double fl = now_ - xyFlashT_;
    if (logoDown_ && now_ - logoT0_ >= 600.0) {
        st_.xy = !st_.xy;
        logoDown_ = false;
        xyFlashT_ = now_;
    }
    if (now_ - fxT_ >= 50.0) {
        fxT_ = now_;
        for (auto& r : fxR_) r = rnd();
    }
    const int stepN = logoDown_ ? (int)(std::min(1.0, (now_ - logoT0_) / 600.0) * 8.0) : 0;
    float amp = logoDown_ ? (float)stepN / 8.f * 5.f : 0.f;
    if (fl >= 0 && fl < 1300 && fl < 450) amp = 10.f * (1.f - (float)fl / 450.f);

    // CPU affiché : mis à jour toutes les 500 ms, par paliers
    if (now_ - cpuT_ >= 500.0) {
        cpuT_ = now_;
        cpuShown_ = m_.readouts.active.load() ? std::max(0.1f, m_.readouts.cpu.load()) : 0.f;
    }

    c.save();
    // fond de la plaque
    c.fillRect(Rect{0, 0, (float)kWidth, (float)kHeight}, T_->chassis);
    if (amp > 0.4f) c.translate(std::round((fxR_[0] - 0.5f) * 2.f * amp), std::round((fxR_[1] - 0.5f) * 2.f * amp));
    c.fillRect(Rect{-20, -20, (float)kWidth + 40, (float)kHeight + 40}, T_->chassis);
    c.dotGrid(Rect{-20, -20, (float)kWidth + 40, (float)kHeight + 40}, 0.f, 0.f, 16.f, 1.1f, T_->chDot);
    drawFooter();
    drawHeader();
    ui_.setLayer(0);
    switch (tab_) {
        case 0: drawSon(); break;
        case 1: drawVie(); break;
        case 2: drawMod(); break;
        case 3: drawEnv(); break;
        case 4: drawEfx(); break;
        case 5: drawMix(); break;
        case 7: drawJeu(); break;
        default: drawWt(); break;
    }
    dragOverlay();
    if (thOpen_) drawThemePanel();
    if (pbOpen_) drawPresetBrowser();
    if (instOpen_) drawInstBrowser();
    if (saveOpen_) drawSaveDialog();
    c.restore();
    drawFx();
    ui_.endFrame();
}

void Editor::drawFooter() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    // repères aux quatre coins
    const Color e = T.crs;
    const float w = 1.5f;
    c.fillRect(Rect{10, 10, 17, w}, e); c.fillRect(Rect{10, 10, w, 17}, e);
    c.fillRect(Rect{1253, 10, 17, w}, e); c.fillRect(Rect{1270 - w, 10, w, 17}, e);
    c.fillRect(Rect{10, 950 - w, 17, w}, e); c.fillRect(Rect{10, 933, w, 17}, e);
    c.fillRect(Rect{1253, 950 - w, 17, w}, e); c.fillRect(Rect{1270 - w, 933, w, 17}, e);
    const Font f{Face::Mono, 9.f, 3.06f}, fb{Face::MonoBold, 9.f, 3.06f};
    const std::string a = "DISTRIBU\xC3\x89 SUR ", b = "ONIRIQUE";
    const float wa = ui_.width(a, f), wb = ui_.width(b, fb);
    const float x = 640.f - (wa + wb) * 0.5f;
    ui_.text(a, x, 944.f, f, T.muted, 9.f);
    ui_.text(b, x + wa, 944.f, fb, T.ink, 9.f);
    // mentions sérigraphiées (maquette mise à jour)
    {
        const Font sf{Face::Mono, 7.5f, 1.35f};
        const Color mc = T.muted.withAlpha(0.85f);
        ui_.text("SPEC.NO: RAW-02 // PCB REV.C", 38.f, 944.5f, sf, mc, 7.5f);
        const std::string tp1 = "[TP1]", tp2 = "[TP2]";
        const float w2 = ui_.width(tp2, sf), w1 = ui_.width(tp1, sf), w0 = ui_.width("MIL-STD", sf);
        float x = 1242.f - (w0 + 5.f + 6.f + 5.f + w1 + 5.f + 6.f + 5.f + w2);
        x += ui_.text("MIL-STD", x, 944.5f, sf, mc, 7.5f) + 5.f;
        auto tp = [&](float cx) {
            c.fillEllipse(Pt{cx + 3.f, 948.3f}, 3.f, 3.f, Color{0, 0, 0, 0.5f});
            c.strokeEllipse(Pt{cx + 3.f, 948.3f}, 2.25f, 2.25f, Color::rgb8(201, 150, 74), 1.5f);
        };
        tp(x);
        x += 11.f;
        x += ui_.text(tp1, x, 944.5f, sf, mc, 7.5f) + 5.f;
        tp(x);
        x += 11.f;
        ui_.text(tp2, x, 944.5f, sf, mc, 7.5f);
    }
}

void Editor::drawHeader() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.setLayer(0);

    // --- logo (appui long : mode X-Y) ---
    {
        const Rect wrap{24.f, 22.5f, 142.2f, 40.f};
        const Ui::Interact it = ui_.interact(makeId("logo"), wrap, Cursor::Hand);
        if (it.pressed) { logoDown_ = true; logoT0_ = now_; }
        if (logoDown_ && (!it.held || !ui_.hit(wrap))) logoDown_ = false;
        const int stepN = logoDown_ ? (int)(std::min(1.0, (now_ - logoT0_) / 600.0) * 8.0) : 0;
        const double fl = now_ - xyFlashT_;
        const bool flashOn = fl >= 0 && fl < 480 && ((int)(fl / 80) % 2) == 0;
        Rect box{24.f, 22.5f, 40.f, 40.f};
        if (logoDown_) {
            box = box.moved(2.f, 2.f);
            const float off = stepN >= 8 ? 1.f : 3.f;
            ui_.border(Rect{box.x - off - 2.f, box.y - off - 2.f, box.w + 2 * off + 4.f, box.h + 2 * off + 4.f}, T.acc, 2.f);
        }
        ui_.box(box, flashOn ? T.acc : T.logoBg, T.btnEdge);
        const float ds = 14.f + (float)stepN * 2.f;
        const Pt dc{box.cx(), box.cy()};
        if (st_.xy) c.fillEllipse(dc, ds / 2, ds / 2, flashOn ? T.logoBg : T.acc);
        else c.fillRect(Rect{dc.x - ds / 2, dc.y - ds / 2, ds, ds}, flashOn ? T.logoBg : T.acc);
        ui_.text("ABRASION", 74.f, 25.f, Font{Face::Mono, 9.f, 3.33f}, T.muted, 9.f);
        const Font lf{Face::Pixel, 28.f, 1.68f};
        const float w = ui_.text("RAW", 72.f, 32.f, lf, T.ink, 28.f);
        ui_.text("2", 72.f + w, 32.f, lf, T.acc, 28.f);
        if (logoDown_) {  // barre de progression par paliers sous le logo
            for (int k = 0; k < stepN; ++k) {
                const float x0 = 24.f + (float)k * 142.2f / 8.f;
                for (float x = x0; x < x0 + 142.2f / 8.f; x += 17.f) c.fillRect(Rect{x, 68.5f, std::min(13.f, x0 + 142.2f / 8.f - x), 5.f}, T.acc);
            }
        }
        if (fl >= 0 && fl < 1800) {
            const std::string full = std::string("> MODE X-Y : ") + (st_.xy ? "ON" : "OFF");
            const size_t n = std::min(full.size(), (size_t)(fl / 20));
            if (n > 0) {
                const Font tf{Face::Mono, 11.f, 1.1f};
                const std::string s = full.substr(0, n);
                const float tw = ui_.width(s, tf);
                const Rect tr{24.f, 76.5f, tw + 22.f, normalLineHeight(tf) + 14.f};
                ui_.box(tr, T.scr, T.acc);
                ui_.text(s, tr.x + 11.f, tr.y + 7.f, tf, T.hl);
            }
        }
    }

    // --- languette des thèmes ---
    {
        const Rect r{190.1f, 22.5f, 22.f, 40.f};
        const Ui::Interact it = ui_.interact(makeId("themeTab"), r);
        if (it.clicked) {
            thOpen_ = !thOpen_;
            thT_ = now_;
        }
        ButtonStyle s = ui_.keyStyle();
        s.bg = thOpen_ ? T.tabBg : T.btnBg;
        ui_.drawButton(r, "", s, it.held);
        const Rect rr = it.held ? r.moved(2, 2) : r;
        ui_.box(Rect{rr.x + 6.f, rr.y + 8.5f, 10.f, 10.f}, T.acc, T.btnEdge, 0.f);
        ui_.textCenter(thOpen_ ? "\xE2\x96\xB4" : "\xE2\x96\xBE", Rect{rr.x, rr.y + 22.5f, rr.w, 10.f}, Font{Face::Mono, 8.f, 0.f}, thOpen_ ? T.tabFg : T.ink);
    }

    // --- onglets : bloc de touches en aluminium (façon enregistreur de poche) ---
    // Les touches se touchent, séparées par de fines rainures sombres : face plate en métal brossé
    // (dégradé léger, filet de lumière en haut), lèvre plus sombre en bas qui montre l'épaisseur,
    // coins presque droits. La page ouverte a sa touche enfoncée (lèvre réduite, face un peu plus
    // sombre) et un point orange au-dessus du nom, comme le témoin d'enregistrement.
    {
        const Font f{Face::Mono, 10.f, 0.7f};
        const Rect block{228.2f, 21.5f, 456.1f, 42.f};
        const Color white{1.f, 1.f, 1.f, 1.f};
        const bool dark = (T.chassis.r + T.chassis.g + T.chassis.b) / 3.f < 0.4f;
        const Color metal = dark ? T.chassis.mix(white, 0.10f) : T.chassis;
        const Color seam = metal.mix(T.line, dark ? 0.75f : 0.62f);
        const Color lip = metal.mix(T.line, dark ? 0.35f : 0.30f);
        c.fillRoundRect(block, 4.f, seam);  // le fond du logement : il fait les rainures
        const float gap = 2.f, in = 1.5f;
        float tws[8], sumW = 0.f;
        for (int slot = 0; slot < 8; ++slot) {
            tws[slot] = ui_.width(kTabs[kTabOrder[slot]], f);
            sumW += tws[slot];
        }
        // largeurs : une part égale + une part selon le nom, pour des touches presque égales
        const float avail = block.w - 2.f * in - 7.f * gap;
        const float base = (avail - sumW) / 8.f;
        float x = block.x + in;
        const float ky = block.y + in, kh = block.h - 2.f * in, lipH = 5.f;
        for (int slot = 0; slot < 8; ++slot) {
            const int i = kTabOrder[slot];
            const bool on = tab_ == i;
            const float w = slot == 7 ? block.r() - in - x : base + tws[slot];
            const Rect key{x, ky, w, kh};
            const Ui::Interact it = ui_.interact(makeId("tab", i), key);
            if (it.clicked) setTab(i);
            // glisser-déposer d'un LFO : survoler un onglet 250 ms l'ouvre (sauf MODULES et MIX IA)
            if (dd_.on && i != 2 && i != 5 && key.contains(ui_.in().mouse)) {
                if (dd_.tabHov != i) { dd_.tabHov = i; dd_.tabT = now_; }
                else if (now_ - dd_.tabT >= 250.0 && tab_ != i) setTab(i);
            } else if (dd_.tabHov == i) {
                dd_.tabHov = -1;
            }
            const bool down = on || it.held;
            const float lp = down ? 2.f : lipH;              // lèvre visible
            const float r = 2.5f;
            // corps de la touche : lèvre sombre en bas, face au-dessus
            c.fillRoundRect(key, r, lip);
            const Rect face{key.x, key.y + (down ? 2.f : 0.f), key.w, key.h - lp - (down ? 2.f : 0.f)};
            const Color top = metal.mix(white, dark ? 0.06f : (down ? 0.18f : 0.42f));
            const Color mid = metal.mix(white, dark ? 0.02f : (down ? 0.05f : 0.16f));
            const Color bot = metal.mix(T.line, dark ? 0.02f : (down ? 0.10f : 0.04f));
            const GradStop gs[3] = {{0.f, top}, {0.55f, mid}, {1.f, bot}};
            c.fillLinear(face, r, Pt{face.x, face.y}, Pt{face.x, face.b()}, gs, 3);
            c.fillRect(Rect{face.x + 2.f, face.y, face.w - 4.f, 1.f}, white.withAlpha(dark ? 0.12f : (down ? 0.35f : 0.85f)));  // filet de lumière
            c.fillRect(Rect{face.x + 2.f, face.b() - 1.f, face.w - 4.f, 1.f}, Color{0, 0, 0, dark ? 0.35f : 0.12f});        // arête basse
            if (it.hovered && !down) c.fillRoundRect(face, r, white.withAlpha(dark ? 0.04f : 0.18f));
            // témoin et nom
            if (on) {
                const Pt dot{face.cx(), face.y + 9.f};
                c.fillEllipse(dot, 4.5f, 4.5f, T.acc.withAlpha(0.22f));
                c.fillEllipse(dot, 3.f, 3.f, T.acc);
            }
            ui_.textCenter(kTabs[i], Rect{face.x, face.y + (on ? 15.f : 10.f), face.w, 14.f}, on ? Font{Face::MonoBold, 10.f, 0.7f} : f,
                           on ? T.ink : T.ink.withAlpha(0.78f));
            x += w + gap;
        }
    }

    // --- CPU / buffer / PANIC ---
    {
        const Rect r{704.2f, 22.f, 123.4f, 41.f};
        const Ui::Interact it = ui_.interact(makeId("perf"), r);
        if (it.clicked) {
            const int L[4] = {32, 64, 128, 256};
            int k = 0;
            for (int i = 0; i < 4; ++i) if (L[i] == m_.controlBlock.load()) k = i;
            m_.controlBlock = L[(k + 1) % 4];
        }
        ui_.box(r, T.scr, T.scrLine);
        const Font f{Face::Mono, 10.f, 0.6f};
        const std::string cpu = now_ - panicT_ < 700.0 ? std::string("RESET") : fmt("CPU: %.1f%%", cpuShown_);
        const float w = ui_.text(cpu, r.x + 10.f, r.y + 6.f, f, T.scrText);
        const float bx = r.x + 10.f + w + 8.f, frac = std::min(1.f, cpuShown_ / 8.f);
        for (float x = 0; x < 36.f * frac; x += 4.f) c.fillRect(Rect{bx + x, r.y + 9.5f, std::min(3.f, 36.f * frac - x), 6.f}, T.acc);
        ui_.text(fmt("%d SAMPLES", m_.controlBlock.load()) + (st_.xy ? " \xC2\xB7 X-Y" : ""), r.x + 10.f, r.y + 22.f, f, T.scrMuted);
        if (ui_.button(makeId("panic"), Rect{835.6f, 24.5f, 62.f, 36.f}, "PANIC", ui_.keyStyle())) panicNow();
    }

    // --- presets ---
    {
        int idx;
        std::string name;
        {
            std::lock_guard<std::mutex> lk(m_.presetMutex);
            idx = m_.presetIndex;
            name = m_.presetName;
        }
        // < > parcourent les presets d'usine (depuis INIT ou un pack : vers 01 ou 72, comme la maquette)
        const int N = kNumFactoryPresets;
        if (ui_.button(makeId("prev"), Rect{917.6f, 23.5f, 32.f, 38.f}, "<", ui_.keyStyle())) loadPreset(((idx < 0 ? 0 : idx) - 1 + N) % N);
        if (ui_.button(makeId("next"), Rect{1103.6f, 23.5f, 32.f, 38.f}, ">", ui_.keyStyle())) loadPreset(((idx < 0 ? -1 : idx) + 1) % N);
        const Rect pill{955.6f, 22.5f, 142.f, 40.f};
        const Ui::Interact it = ui_.interact(makeId("pill"), pill);
        if (it.clicked) {
            pbOpen_ = !pbOpen_;
            if (pbOpen_) {
                m_.scanPacks();
                pbMsg_.clear();
                pbGenreOpen_ = false;
            }
        }
        ui_.box(pill, T.scr, T.scrLine);
        const Font f{Face::Mono, 12.f, 0.f};
        const std::string num = idx >= 0 ? fmt("%02d", idx + 1) : (idx == Model::kPresetInit ? std::string("INIT") : std::string("USR"));
        std::string rest = " \xC2\xB7 " + (idx == Model::kPresetInit ? std::string("preset vierge") : name);
        const float wn = ui_.width(num, f);
        const float wTri = ui_.width(" \xE2\x96\xBE", f);
        float wr = ui_.width(rest, f);
        bool showTri = true;
        if (wn + wr + wTri > 126.f) {
            rest = ui_.fit(rest, f, 126.f - wn);
            wr = ui_.width(rest, f);
            showTri = false;
        }
        const float total = wn + wr + (showTri ? wTri : 0.f);
        const float x = pill.cx() - total * 0.5f, top = pill.y + 12.f;
        ui_.text(num, x, top, f, T.hl);
        ui_.text(rest, x + wn, top, f, T.scrText);
        if (showTri) ui_.text(" \xE2\x96\xBE", x + wn + wr, top, f, T.muted);
        if (ui_.button(makeId("save"), Rect{1141.6f, 22.5f, 114.4f, 40.f}, "ENREGISTRER", ui_.accentStyle())) {
            saveOpen_ = true;
            saveMsg_.clear();
            saveName_ = m_.nextUserName();  // range le son dans USER › MES PRESETS (nom modifiable)
        }
    }
}

// --- thèmes ---------------------------------------------------------------------------------------
void Editor::drawThemePanel() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.setLayer(5);
    const Rect all{0, 0, (float)kWidth, (float)kHeight};
    if (ui_.interact(makeId("thCatch"), all, Cursor::Arrow).pressed) {
        thOpen_ = false;
        ui_.releaseActive();
        return;
    }
    ui_.setLayer(6);
    const Rect panel{190.1f, 68.5f, 216.f, 112.f};
    const int st = std::min(5, (int)((now_ - thT_) / 160.0 * 6.0));
    c.save();
    c.clip(Rect{panel.x, panel.y, panel.w + 6.f, (panel.h + 6.f) * (float)st / 5.f});
    c.fillRect(panel.moved(4.f, 4.f), T.btnEdge);
    ui_.box(panel, T.card, T.btnEdge);
    for (int i = 0; i < kNumThemes; ++i) {
        const Theme& th = kThemes[i];
        const Rect r{205.1f + (float)i * 32.f, 81.5f, 26.f, 26.f};
        const Ui::Interact it = ui_.interact(makeId("sw", i), r);
        if (it.clicked) {
            m_.theme = i;
            m_.saveGlobalSettings();
            thOpen_ = false;
        }
        if (i == m_.theme.load()) ui_.border(Rect{r.x - 4.f, r.y - 4.f, r.w + 8.f, r.h + 8.f}, T.ink, 2.f);
        ui_.box(r, th.sw, T.ink);
        ui_.box(Rect{r.x + 8.f, r.y + 8.f, 10.f, 10.f}, th.acc, th.btnEdge, 0.f);
    }
    const Font f{Face::Mono, 10.f, 1.f};
    ui_.text(T.name, 205.1f, 115.5f, f, T.muted);
    // zoom de la fenêtre (homothétique, §2)
    ui_.text("ZOOM", 205.1f, 146.f, f, T.muted);
    const int zooms[4] = {75, 100, 125, 150};
    for (int k = 0; k < 4; ++k) {
        const Rect r{249.1f + (float)k * 36.f, 140.f, 32.f, 24.f};
        ButtonStyle s = ui_.miniStyle(m_.zoomPct.load() == zooms[k]);
        s.font = Font{Face::Mono, 10.f, 0.4f};
        if (ui_.button(makeId("zoom", k), r, std::to_string(zooms[k]), s)) {
            m_.zoomPct = zooms[k];
            m_.saveGlobalSettings();
        }
    }
    c.restore();
}

// --- choix du preset --------------------------------------------------------------------------------
// FACTORY : INIT + 72 presets d'usine ; USER : les packs du dossier Documents/Raw 2 (voir Packs.cpp).
// étoile à 5 branches (le glyphe ★ manque dans les polices embarquées)
void Editor::drawStar(Pt c0, float r, Color col, bool filled) {
    Pt p[10];
    for (int k = 0; k < 10; ++k) {
        const float a = -1.5707963f + (float)k * 0.62831853f, rr = (k & 1) ? r * 0.45f : r;
        p[k] = Pt{c0.x + rr * std::cos(a), c0.y + rr * std::sin(a)};
    }
    if (filled) ui_.c().fillPolygon(p, 10, col);
    else ui_.c().polyline(p, 10, col, 1.2f, true);
}

void Editor::drawInstBrowser() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.setLayer(10);
    const Rect all{0, 0, (float)kWidth, (float)kHeight};
    const Rect panel = kInstPanel;
    c.fillRect(all, Color{0, 0, 0, 0.55f});
    if (ui_.interact(makeId("instBack"), all, Cursor::Arrow).clicked && !panel.contains(ui_.in().mouse)) { instOpen_ = false; return; }
    ui_.setLayer(11);
    c.fillRect(panel.moved(6.f, 6.f), T.btnEdge);
    ui_.card(panel);
    ui_.cardHeader(panel, "INSTRUMENTS", false, 14.f, 12.f);
    ui_.tag(instOsc_ ? "OSC B" : "OSC A", panel.r() - 60.f, panel.y + 13.f, Ui::Tag::Line);
    {
        ButtonStyle xs;
        xs.bg = Color{0, 0, 0, 0};
        xs.fg = T.hl;
        xs.border = Color{0, 0, 0, 0};
        xs.shadow = false;
        xs.font = Font{Face::Mono, 12.f, 0.48f};
        if (ui_.button(makeId("instX"), Rect{panel.r() - 52.f, panel.y + 15.f, 40.f, 16.f}, "[ X ]", xs)) { instOpen_ = false; return; }
    }
    const std::vector<InstEntry> cat = m_.instruments.catalog();
    std::vector<std::string> cats{"TOUS"};
    for (const auto& e : cat)
        if (std::find(cats.begin(), cats.end(), e.category) == cats.end()) cats.push_back(e.category);
    // catégories (deux lignes au plus)
    float x = panel.x + 15.f, y = panel.y + 60.f;
    const Font cf{Face::Mono, 10.f, 0.6f};
    for (const auto& k : cats) {
        const float w = ui_.width(k, cf) + 18.f;
        if (x + w > panel.r() - 15.f) { x = panel.x + 15.f; y += 30.f; }
        ButtonStyle s = ui_.miniStyle((k == "TOUS" && instCat_.empty()) || k == instCat_);
        s.font = cf;
        if (ui_.button(makeId("instCat", (int)(&k - cats.data())), Rect{x, y, w, 24.f}, k, s)) {
            instCat_ = k == "TOUS" ? std::string() : k;
            instScroll_ = 0.f;
        }
        x += w + 5.f;
    }
    const Rect list{panel.x + 15.f, y + 34.f, panel.w - 30.f, panel.b() - 70.f - (y + 34.f)};
    ui_.box(list, T.card, T.rule);
    std::vector<const InstEntry*> rows;
    for (const auto& e : cat)
        if (instCat_.empty() || e.category == instCat_) rows.push_back(&e);
    const float rh = 34.f, maxScroll = std::max(0.f, (float)rows.size() * rh - list.h + 2.f);
    if (ui_.hit(list) && ui_.in().wheel != 0.f) instScroll_ -= ui_.in().wheel * rh * 2.f;
    instScroll_ = std::clamp(instScroll_, 0.f, maxScroll);
    const std::string cur = m_.instruments.name(instOsc_);
    c.save();
    c.clip(list.inset(1.f));
    for (size_t i = 0; i < rows.size(); ++i) {
        const Rect r{list.x + 1.f, list.y + 1.f + (float)i * rh - instScroll_, list.w - 2.f, rh};
        if (r.b() < list.y || r.y > list.b()) continue;
        const bool on = rows[i]->name == cur;
        if (on) c.fillRect(r, T.tabBg);
        c.fillRect(Rect{r.x, r.b() - 1.f, r.w, 1.f}, T.rule);
        if (ui_.interact(makeId("instRow", (int)i), r).clicked) {
            m_.instruments.load(instOsc_, rows[i]->path);
            m_.samples.clear(instOsc_);
            setParam(instOsc_ ? kBPlay : kAPlay, 1.f);
        }
        ui_.text(ui_.fit(rows[i]->name, Font{Face::Mono, 13.f, 0.f}, 300.f), r.x + 12.f, r.y + 9.f, Font{Face::Mono, 13.f, 0.f}, on ? T.tabFg : T.ink);
        const Rect badge{r.r() - 96.f, r.y + 8.f, 84.f, 18.f};
        ui_.box(badge, Color{0, 0, 0, 0}, on ? T.tabFg : T.rule);
        ui_.textCenter(rows[i]->category, badge, Font{Face::Mono, 9.f, 0.6f}, on ? T.tabFg : T.muted);
    }
    if (rows.empty()) {
        const Font ef{Face::Mono, 11.f, 0.f};
        const char* lines[4] = {"aucun instrument install\xC3\xA9.", "d\xC3\xA9" "compresse la banque RAW 2 (raw2-instruments.zip)",
                                "dans Documents \xE2\x80\xBA Raw 2 \xE2\x80\xBA Instruments ;", "tout dossier contenant un .sfz \xC3\xA0 WAV y est lu aussi."};
        for (int k = 0; k < 4; ++k) ui_.text(lines[k], list.x + 14.f, list.y + 16.f + (float)k * 20.f, ef, T.muted);
    }
    c.restore();
    // pied : état du chargement, actions
    const std::string err = m_.instruments.lastError(instOsc_);
    std::string st = m_.instruments.loading(instOsc_) ? "chargement de " + cur + "\xE2\x80\xA6" : (cur.empty() ? std::string("aucun instrument sur cet oscillateur") : "jou\xC3\xA9 : " + cur);
    if (!err.empty()) st = err;
    ui_.text(ui_.fit(st, Font{Face::Mono, 10.f, 0.4f}, panel.w - 260.f), panel.x + 15.f, panel.b() - 50.f, Font{Face::Mono, 10.f, 0.4f}, err.empty() ? T.hl : T.acc);
    if (ui_.button(makeId("instScan"), Rect{panel.r() - 230.f, panel.b() - 56.f, 104.f, 28.f}, "[ RELIRE ]", ui_.miniStyle(false))) m_.instruments.scan();
    if (ui_.button(makeId("instFolder"), Rect{panel.r() - 120.f, panel.b() - 56.f, 105.f, 28.f}, "[ DOSSIER ]", ui_.miniStyle(false))) {
        const std::string d = files::join(files::raw2Dir(), "Instruments");
        files::makeDirs(d);
        files::openFolder(d);
    }
    ui_.setLayer(0);
}

void Editor::drawPresetBrowser() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.setLayer(10);
    const Rect all{0, 0, (float)kWidth, (float)kHeight};
    const Rect panel = kPbPanel;
    c.fillRect(all, Color{0, 0, 0, 0.55f});
    {
        const Ui::Interact it = ui_.interact(makeId("pbBack"), all, Cursor::Arrow);
        if (it.clicked && !panel.contains(ui_.in().mouse)) { pbOpen_ = false; return; }
    }
    ui_.setLayer(11);
    c.fillRect(panel.moved(6.f, 6.f), T.btnEdge);
    ui_.card(panel);
    ui_.cardHeader(panel, "CHOIX DU PRESET", false, 14.f, 12.f);
    const bool user = pbView_ == 1;
    const std::string q = foldForSearch(pbQuery_);
    const bool filtered = pbCat_ != 0 || !pbGenre_.empty();
    int nUser = 0;
    for (const Pack& pk : m_.packs) nUser += (int)pk.presets.size();
    int cur;
    std::string curPack, curName;
    {
        std::lock_guard<std::mutex> lk(m_.presetMutex);
        cur = m_.presetIndex;
        curPack = m_.presetPack;
        curName = m_.presetName;
    }
    auto favKey = [](int kind, int a, const std::string& pack, const std::string& name) {
        return kind == 0 ? fmt("f:%d", a) : "p:" + pack + "/" + name;
    };
    auto keep = [&](const std::string& n, const std::string& cat, const std::string& style, const std::string& extra, const std::string& fk) {
        if (pbFav_ && !m_.favorites.count(fk)) return false;
        if (!q.empty() && foldForSearch(n + " " + cat + " " + style + " " + extra).find(q) == std::string::npos) return false;
        if (pbCat_ != 0 && cat != kCats[pbCat_]) return false;
        if (!pbGenre_.empty() && style != pbGenre_) return false;
        return true;
    };

    // en-tête : compte, [ X ]
    std::vector<int> facRows;  // -1 : INIT
    if (!user) {
        if (!filtered && !pbFav_ && (q.empty() || std::string("init preset vierge").find(q) != std::string::npos)) facRows.push_back(-1);
        for (int i = 0; i < kNumFactoryPresets; ++i) {
            const FactoryPreset& p = kFactoryPresets[i];
            if (keep(p.name, categoryName(p.category), p.style, "", favKey(0, i, "", ""))) facRows.push_back(i);
        }
    }
    {
        ButtonStyle xs;
        xs.bg = Color{0, 0, 0, 0};
        xs.fg = T.hl;
        xs.border = Color{0, 0, 0, 0};
        xs.shadow = false;
        xs.font = Font{Face::Mono, 12.f, 0.48f};
        if (ui_.button(makeId("pbX"), Rect{902.6f, 105.5f, 42.4f, 16.f}, "[ X ]", xs)) { pbOpen_ = false; return; }
        const int nFac = (int)facRows.size() - ((!facRows.empty() && facRows[0] == -1) ? 1 : 0);
        const std::string count = user ? fmt("%d packs", (int)m_.packs.size()) + fmt(" \xC2\xB7 %d presets", nUser) + (filtered ? " \xC2\xB7 filtre" : "")
                                       : fmt("%d", nFac) + fmt(" / %d presets", kNumFactoryPresets);
        ui_.tag(count, 894.6f, 103.f, Ui::Tag::Line);
    }
    ui_.textField(makeId("pbSearch"), Rect{335.f, 149.f, 520.2f, 41.f}, pbQuery_, "rechercher un nom, un style\xE2\x80\xA6", Font{Face::Mono, 13.f, 0.f}, T.scr, T.scrText,
                  T.rule, T.scrMuted);
    if (ui_.button(makeId("pbInit"), Rect{863.2f, 149.f, 81.8f, 41.f}, "[ INIT ]", ui_.miniStyle(cur == Model::kPresetInit))) loadPreset(-1);
    // FACTORY / USER
    {
        const char* v[2] = {"FACTORY", "USER"};
        float x = 335.f;
        for (int k = 0; k < 2; ++k) {
            const float w = std::round(ui_.width(v[k], Font{Face::Mono, 11.f, 0.88f}) + 22.f);
            if (ui_.button(makeId("pbView", k), Rect{x, 200.f, w, 28.f}, v[k], ui_.miniStyle(pbView_ == k))) {
                if (pbView_ != k) { pbScroll_ = 0.f; pbMsg_.clear(); }
                pbView_ = k;
                if (k == 1) m_.scanPacks();
            }
            x += w + 4.f;
        }
        // ★ FAVORIS (filtre) et HASARD (un preset tiré parmi ceux affichés)
        x += 10.f;
        {
            const Rect fr{x, 200.f, 112.f, 28.f};
            if (ui_.button(makeId("pbFav"), fr, "   FAVORIS", ui_.miniStyle(pbFav_))) { pbFav_ = !pbFav_; pbScroll_ = 0.f; }
            drawStar(Pt{fr.x + 16.f, fr.cy()}, 6.5f, pbFav_ ? T.tabFg : T.hl, true);
            x += 118.f;
        }
        if (ui_.button(makeId("pbDice"), Rect{x, 200.f, 104.f, 28.f}, "[ HASARD ]", ui_.miniStyle(false))) pbDice_ = true;
    }
    // catégories + GENRE
    {
        float x = 335.f;
        const Font f{Face::Mono, 11.f, 0.88f};
        for (int i = 0; i < 7; ++i) {
            const float w = std::round(ui_.width(kCats[i], f) + 22.f);
            if (ui_.button(makeId("pbCat", i), Rect{x, 238.f, w, 28.f}, kCats[i], ui_.miniStyle(pbCat_ == i))) {
                pbCat_ = i;
                pbScroll_ = 0.f;
            }
            x += w + 5.5f;
        }
        const std::string g = "[ GENRE : " + (pbGenre_.empty() ? std::string("TOUS") : upper(pbGenre_)) + " \xE2\x96\xBE ]";
        const float w = std::max(156.6f, std::round(ui_.width(g, f) + 22.f));
        if (ui_.button(makeId("pbGenre"), Rect{945.f - w, 238.f, w, 28.f}, g, ui_.miniStyle(pbGenreOpen_))) pbGenreOpen_ = !pbGenreOpen_;
    }

    float listTop = 278.f;
    if (user) {
        // actions des packs
        const Font f{Face::Mono, 11.f, 0.88f};
        float x = 335.f;
        auto act = [&](const char* id, const std::string& label) {
            const float w = std::round(ui_.width(label, f) + 24.f);
            const bool clicked = ui_.button(makeId(id), Rect{x, 276.f, w, 30.f}, label, ui_.miniStyle(false));
            x += w + 6.f;
            return clicked;
        };
        if (act("pbInstall", "[ INSTALLER UN PACK\xE2\x80\xA6 ]")) {
            files::makeDirs(files::raw2Dir());
            const bool opened = files::openFolder(files::raw2Dir());
            pbMsg_ = std::string(opened ? "dossier ouvert : " : "d\xC3\xA9pose tes packs dans ") + "Documents \xE2\x80\xBA Raw 2 (.raw2pack ou dossier de .raw), ou glisse-les ici";
        }
        if (act("pbDemo", "[ PACKS D\xC3\x89MO ]")) {
            m_.installDemoPacks(pbMsg_);
            pbExp_.insert("D\xC3\x89MO \xC2\xB7 NUIT FROIDE");
            pbExp_.insert("D\xC3\x89MO \xC2\xB7 RAGE LOURDE");
        }
        if (act("pbSaveNow", "[ + SAUVER LE SON ACTUEL ]")) {
            const std::string nm = m_.nextUserName();
            std::string path;
            if (m_.saveUserPreset(nm, path)) {
                pbMsg_ = "enregistr\xC3\xA9 \xC2\xB7 USER \xE2\x80\xBA MES PRESETS \xE2\x80\xBA " + nm;
                pbExp_.insert("MES PRESETS");
            } else {
                pbMsg_ = "\xC3\xA9" "chec : dossier Documents \xE2\x80\xBA Raw 2 inaccessible";
            }
        }
        if (!pbMsg_.empty()) ui_.text(ui_.fit(pbMsg_, Font{Face::Mono, 11.f, 0.66f}, 610.f), 335.f, 314.f, Font{Face::Mono, 11.f, 0.66f}, T.hl);
        listTop = pbMsg_.empty() ? 318.f : 340.f;
    }
    const Rect list{335.f, listTop, 610.f, 834.f - listTop};
    ui_.box(list, T.card, T.rule);

    // lignes : usine, ou packs (en-tête de pack + presets si déroulé)
    struct Row { int kind; int a, b; };  // kind 0 : usine (a = indice, -1 INIT), 1 : pack (a), 2 : preset de pack (a, b)
    std::vector<Row> rows;
    std::vector<float> heights;
    std::vector<std::pair<int, int>> userHits;  // presets de packs retenus par le filtre (même repliés)
    if (!user) {
        for (int i : facRows) { rows.push_back({0, i, 0}); heights.push_back(38.f); }
    } else {
        for (int pi = 0; pi < (int)m_.packs.size(); ++pi) {
            const Pack& pk = m_.packs[(size_t)pi];
            std::vector<int> hits;
            for (int ui = 0; ui < (int)pk.presets.size(); ++ui) {
                const PackPreset& p = pk.presets[(size_t)ui];
                if (keep(p.name, p.category, p.style, p.tags + " " + pk.name, favKey(1, 0, pk.name, p.name))) {
                    hits.push_back(ui);
                    userHits.push_back({pi, ui});
                }
            }
            if ((!q.empty() || filtered || pbFav_) && hits.empty()) continue;
            rows.push_back({1, pi, 0});
            heights.push_back(42.f);
            const bool open = !q.empty() || filtered || pbFav_ || pbExp_.count(pk.name) > 0;
            if (open)
                for (int ui : hits) { rows.push_back({2, pi, ui}); heights.push_back(38.f); }
        }
    }
    if (pbDice_) {  // HASARD : un preset au hasard parmi ceux que la liste montre
        pbDice_ = false;
        std::vector<std::pair<int, int>> pick;  // (-1, usine) ou (pack, preset)
        if (!user) {
            for (const Row& rw : rows)
                if (rw.kind == 0 && rw.a >= 0) pick.push_back({-1, rw.a});
        } else {
            pick = userHits;
        }
        if (!pick.empty()) {
            const auto& pk = pick[(size_t)(rnd() * (float)pick.size()) % pick.size()];
            if (pk.first < 0) loadPreset(pk.second);
            else loadPack(pk.first, pk.second);
            {
                std::lock_guard<std::mutex> lk(m_.presetMutex);
                cur = m_.presetIndex;
                curPack = m_.presetPack;
                curName = m_.presetName;
            }
        }
    }
    float totalH = 0.f;
    for (float h : heights) totalH += h;
    const float maxScroll = std::max(0.f, totalH - (list.h - 2.f));
    if (ui_.hit(list) && ui_.in().wheel != 0.f && !pbGenreOpen_) pbScroll_ -= ui_.in().wheel * 38.f * 2.f;
    pbScroll_ = std::clamp(pbScroll_, 0.f, maxScroll);
    c.save();
    c.clip(list.inset(1.f));
    float y = list.y + 1.f - pbScroll_;
    const Font numF{Face::Mono, 11.f, 0.f}, nameF{Face::Mono, 14.f, 0.f}, sf{Face::Mono, 10.f, 0.6f}, bf{Face::Mono, 10.f, 0.8f};
    for (size_t k = 0; k < rows.size(); ++k) {
        const Row& rw = rows[k];
        const Rect r{336.f, y, 608.f, heights[k]};
        y += heights[k];
        if (r.b() < list.y || r.y > list.b()) continue;
        const Rect hitR{r.x, std::max(r.y, list.y + 1.f), r.w, std::min(r.b(), list.b() - 1.f) - std::max(r.y, list.y + 1.f)};
        if (rw.kind == 1) {  // en-tête de pack
            const Pack& pk = m_.packs[(size_t)rw.a];
            const bool open = !q.empty() || filtered || pbExp_.count(pk.name) > 0;
            const bool here = cur == Model::kPresetPack && curPack == pk.name;
            c.fillRect(r, T.scr);
            c.fillRect(Rect{r.x, r.b() - 1.f, r.w, 1.f}, T.rule);
            ui_.text(open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", 348.f, r.y + 12.f, Font{Face::Mono, 13.f, 0.f}, T.hl);
            ui_.text(ui_.fit(pk.name, nameF, 190.f), 382.f, r.y + 11.5f, nameF, T.scrText);
            const std::string st = (pk.author.empty() ? std::string() : pk.author + " \xC2\xB7 ") + fmt("%d presets", (int)pk.presets.size()) +
                                   (pk.version.empty() ? std::string() : " \xC2\xB7 v" + pk.version);
            ui_.textRight(ui_.fit(st, sf, 180.f), 753.4f, r.y + 14.f, sf, T.scrMuted);
            const Rect badge{765.4f, r.y + 11.f, 66.f, 19.f};
            ui_.box(badge, Color{0, 0, 0, 0}, here ? T.hl : T.scrMuted);
            ui_.textCenter(pk.mine ? "MES" : (pk.folder ? "PACK" : "FICHIER"), badge, bf, here ? T.hl : T.scrMuted);
            bool rmHit = false;
            if (!pk.mine) {
                const Rect rb{843.4f, r.y + 10.f, 88.6f, 21.f};
                const bool confirm = pbRmPath_ == pk.path && now_ - pbRmT_ < 3000.0;
                const Ui::Interact ir = ui_.interact(makeId("pbRm", rw.a), rb);
                rmHit = ir.hovered || ir.pressed || ir.held;
                ui_.box(rb, confirm ? T.hl : Color{0, 0, 0, 0}, confirm ? T.hl : T.scrMuted);
                ui_.textCenter(confirm ? "[ CONFIRMER ]" : "[ RETIRER ]", Rect{rb.x, rb.y + 4.f, rb.w, 13.f}, Font{Face::Mono, 10.f, 0.6f}, confirm ? T.onAcc : T.scrText);
                if (ir.clicked) {
                    if (confirm) {  // second clic : le pack part à la corbeille
                        const std::string nm = pk.name;
                        pbMsg_ = m_.removePack(rw.a) ? "pack retir\xC3\xA9 (corbeille) \xC2\xB7 " + nm : std::string("impossible de retirer ce pack");
                        pbRmPath_.clear();
                        break;  // la liste a changé
                    }
                    pbRmPath_ = pk.path;
                    pbRmT_ = now_;
                }
            }
            if (!rmHit && hitR.h > 0 && ui_.interact(makeId("pbPack", rw.a), Rect{hitR.x, hitR.y, 500.f, hitR.h}).clicked) {
                if (pbExp_.count(pk.name)) pbExp_.erase(pk.name);
                else pbExp_.insert(pk.name);
            }
            continue;
        }
        bool on;
        std::string num, name, style, cat, starKey;
        if (rw.kind == 0) {
            on = rw.a == cur;
            if (rw.a < 0) { num = "--"; name = "INIT"; style = "preset vierge \xC2\xB7 sinus, FX coup\xC3\xA9s"; cat = "init"; }
            else {
                const FactoryPreset& p = kFactoryPresets[rw.a];
                num = fmt("%02d", rw.a + 1);
                name = p.name;
                style = p.style;
                cat = categoryName(p.category);
            }
            starKey = rw.a >= 0 ? favKey(0, rw.a, "", "") : std::string();
            if (hitR.h > 0 && ui_.interact(makeId("pbRow", rw.a + 1), Rect{hitR.x + 28.f, hitR.y, hitR.w - 28.f, hitR.h}).clicked) loadPreset(rw.a);
        } else {
            const Pack& pk = m_.packs[(size_t)rw.a];
            const PackPreset& p = pk.presets[(size_t)rw.b];
            on = cur == Model::kPresetPack && curPack == pk.name && curName == p.name;
            num = fmt("%02d", rw.b + 1);
            name = p.name;
            style = p.style;
            if (!p.category.empty()) style += (style.empty() ? "" : " \xC2\xB7 ") + p.category;
            if (p.base >= 0) style += fmt(" \xC2\xB7 base %02d", p.base + 1);
            cat = "user";
            starKey = favKey(1, 0, pk.name, p.name);
            if (hitR.h > 0 && ui_.interact(makeId("pbUrow", rw.a * 1000 + rw.b), Rect{hitR.x + 28.f, hitR.y, hitR.w - 28.f, hitR.h}).clicked) loadPack(rw.a, rw.b);
        }
        if (on) c.fillRect(r, T.tabBg);
        c.fillRect(Rect{r.x, r.b() - 1.f, r.w, 1.f}, T.rule);
        const Color fg = on ? T.tabFg : T.ink;
        const float ix = rw.kind == 2 ? 22.f : 0.f;
        if (!starKey.empty()) {  // étoile : favori (clic pour ajouter / retirer)
            const bool fav = m_.favorites.count(starKey) > 0;
            const Rect sr{338.f, r.y + 6.f, 26.f, 26.f};
            if (hitR.h > 0 && ui_.interact(makeId("pbStar", (int)k), sr).clicked) {
                if (fav) m_.favorites.erase(starKey);
                else m_.favorites.insert(starKey);
                m_.saveGlobalSettings();
            }
            drawStar(Pt{sr.cx(), sr.cy()}, 6.5f, fav ? T.hl : (on ? T.tabFg : T.rule), fav);
        }
        ui_.text(num, 366.f + ix, r.y + 11.5f, numF, on ? T.tabFg : T.hl);
        ui_.text(ui_.fit(name, nameF, 290.f), 396.f + ix, r.y + 9.5f, nameF, fg);
        ui_.textRight(ui_.fit(style, sf, 220.f), 854.f, r.y + 12.f, sf, on ? T.tabFg : T.muted);
        const Rect badge{866.f, r.y + 9.f, 66.f, 19.f};
        ui_.box(badge, Color{0, 0, 0, 0}, on ? T.tabFg : T.rule);
        ui_.textCenter(upper(cat.empty() ? std::string("?") : cat), badge, bf, fg);
    }
    if (rows.empty()) {
        const Font ef{Face::Mono, 12.f, 0.f};
        if (!user) ui_.text("aucun preset ne correspond", 356.f, list.y + 20.f, ef, T.muted);
        else if (m_.packs.empty()) {
            const char* lines[5] = {"aucun pack install\xC3\xA9.", "\xC2\xAB INSTALLER UN PACK\xE2\x80\xA6 \xC2\xBB ouvre le dossier Documents \xE2\x80\xBA Raw 2 :",
                                    "un dossier de .raw ou un fichier .raw2pack = un pack. \xC2\xAB PACKS D\xC3\x89MO \xC2\xBB",
                                    "installe deux packs d'exemple ; \xC2\xAB ENREGISTRER \xC2\xBB range le son actuel",
                                    "dans MES PRESETS. On peut aussi glisser un pack sur cette fen\xC3\xAAtre."};
            for (int k = 0; k < 5; ++k) ui_.text(lines[k], 356.f, list.y + 20.f + (float)k * 19.4f, ef, T.muted);
        } else ui_.text("aucun preset ne correspond", 356.f, list.y + 20.f, ef, T.muted);
    }
    c.restore();
    if (maxScroll > 0.f) {  // ascenseur
        const float h = list.h * (list.h / (list.h + maxScroll));
        const float yy = list.y + (list.h - h) * (pbScroll_ / maxScroll);
        c.fillRect(Rect{list.r() - 4.f, yy, 3.f, h}, T.muted);
    }
    ui_.text("clic : charger et \xC3\xA9" "couter \xC2\xB7 pack : cliquer pour d\xC3\xA9rouler \xC2\xB7 \xC3\x89" "chap : fermer", 335.f, 844.f, Font{Face::Mono, 10.f, 0.8f}, T.muted);

    // menu GENRE (au-dessus de la liste)
    if (pbGenreOpen_) {
        ui_.setLayer(12);
        std::vector<std::pair<std::string, int>> gl;
        for (int i = 0; i < kNumFactoryPresets; ++i) {
            const std::string st = kFactoryPresets[i].style;
            auto it2 = std::find_if(gl.begin(), gl.end(), [&](const auto& x) { return x.first == st; });
            if (it2 == gl.end()) gl.push_back({st, 1});
            else ++it2->second;
        }
        std::sort(gl.begin(), gl.end());
        gl.insert(gl.begin(), {std::string(), kNumFactoryPresets});
        const float rh = 27.f, w = 202.f, x0 = 945.f + 45.f - w, y0 = 269.f;
        const int vis = std::min((int)gl.size(), 20);
        const Rect menu{x0, y0, w, rh * (float)vis};
        pbGenreRect_ = menu;
        c.fillRect(menu.moved(3.f, 3.f), T.btnEdge);
        ui_.box(menu, T.card, T.rule);
        const float maxS = std::max(0.f, (float)gl.size() * rh - menu.h);
        if (ui_.hit(menu) && ui_.in().wheel != 0.f) pbGenreScroll_ -= ui_.in().wheel * rh * 2.f;
        pbGenreScroll_ = std::clamp(pbGenreScroll_, 0.f, maxS);
        c.save();
        c.clip(menu.inset(1.f));
        for (size_t k = 0; k < gl.size(); ++k) {
            const Rect r{menu.x + 1.f, menu.y + 1.f + (float)k * rh - pbGenreScroll_, menu.w - 2.f, rh};
            if (r.b() < menu.y || r.y > menu.b()) continue;
            const bool on = gl[k].first == pbGenre_;
            if (ui_.interact(makeId("pbGen", (int)k), r).clicked) {
                pbGenre_ = gl[k].first;
                pbGenreOpen_ = false;
                pbScroll_ = 0.f;
            }
            if (on) c.fillRect(r, T.ink);
            c.fillRect(Rect{r.x, r.b() - 1.f, r.w, 1.f}, T.rule);
            const Font gf{Face::Mono, 11.f, 0.66f};
            ui_.text(k == 0 ? std::string("TOUS LES GENRES") : gl[k].first, r.x + 12.f, r.y + 7.f, gf, on ? T.card : T.ink);
            ui_.textRight(std::to_string(gl[k].second), r.r() - 12.f, r.y + 7.f, gf, on ? T.card : T.muted);
        }
        c.restore();
    }
}

// --- enregistrement ----------------------------------------------------------------------------------
void Editor::drawSaveDialog() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.setLayer(10);
    const Rect all{0, 0, (float)kWidth, (float)kHeight};
    const Rect panel = kSavePanel;
    c.fillRect(all, Color{0, 0, 0, 0.55f});
    if (ui_.interact(makeId("svBack"), all, Cursor::Arrow).clicked && !panel.contains(ui_.in().mouse)) {
        saveOpen_ = false;
        ui_.focus = 0;
        return;
    }
    ui_.setLayer(11);
    c.fillRect(panel.moved(6.f, 6.f), T.btnEdge);
    ui_.card(panel);
    ui_.cardHeader(panel, "ENREGISTRER MON PRESET", false, 22.f, 20.f);
    const Id fid = makeId("svName");
    if (ui_.focus == 0) ui_.focus = fid;
    const Font f{Face::Mono, 14.f, 0.f};
    const Rect field{343.f, 399.f, 404.f, 43.f};
    const bool enter = ui_.textField(fid, field, saveName_, "nom du preset", f, T.inputBg, T.ink, T.rule, T.muted);
    ui_.text(".raw", 757.f, 411.5f, f, T.muted);
    ButtonStyle s = ui_.accentStyle();
    const bool click = ui_.button(makeId("svGo"), Rect{800.f, 400.f, 136.f, 41.f}, "ENREGISTRER", s);
    if (enter || click) {
        std::string path;
        if (m_.saveUserPreset(saveName_, path)) {
            saveMsg_ = "> enregistr\xC3\xA9 : " + path;
        } else {
            saveMsg_ = "> \xC3\xA9" "chec : nom vide ou dossier inaccessible";
        }
    }
    const Rect pathBox{343.f, 456.f, 594.f, 44.f};
    ui_.box(pathBox, T.scr, T.scr);
    const Font pf{Face::Mono, 12.f, 0.f};
    const std::string pre = "> Documents / Raw 2 / Mes presets / ";
    const float w = ui_.text(pre, pathBox.x + 16.f, pathBox.y + 13.f, pf, T.scrMuted);
    ui_.text(ui_.fit(saveName_ + ".raw", pf, pathBox.w - 32.f - w), pathBox.x + 16.f + w, pathBox.y + 13.f, pf, T.hl);
    if (!saveMsg_.empty()) ui_.text(ui_.fit(saveMsg_, Font{Face::Mono, 10.f, 0.6f}, 594.f), 343.f, 514.f, Font{Face::Mono, 10.f, 0.6f}, T.hl);
    else ui_.text("Entr\xC3\xA9" "e : enregistrer \xC2\xB7 \xC3\x89" "chap : fermer", 343.f, 514.f, Font{Face::Mono, 10.f, 0.8f}, T.muted);
}

// --- effet du clic long ----------------------------------------------------------------------------------
void Editor::drawFx() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const double fl = now_ - xyFlashT_;
    const bool trig = fl >= 0 && fl < 1300, hold = logoDown_;
    if (!trig && !hold) return;
    const int stepN = hold ? (int)(std::min(1.0, (now_ - logoT0_) / 600.0) * 8.0) : 0;
    const float q8 = (float)stepN / 8.f;
    const Rect all{0, 0, (float)kWidth, (float)kHeight};
    if (trig && fl < 400) {
        const Color cols[3] = {T.acc, T.scr, T.chassis};
        c.fillRect(all, cols[(int)(fl / 50) % 3]);
    }
    const float scan = hold ? 0.12f + q8 * 0.4f : (trig && fl >= 400 ? 0.3f : 0.f);
    if (scan > 0.f)
        for (float y = 0; y < kHeight; y += 5.f) c.fillRect(Rect{0, y, (float)kWidth, 2.f}, Color{0, 0, 0, scan});
    // halo intérieur (ombre portée vers l'intérieur) : quelques cadres dégressifs
    const float glow = (hold ? q8 : 0.6f) * 170.f;
    for (int k = 0; k < 8; ++k) {
        const float d = glow * (float)k / 8.f;
        ui_.border(Rect{d, d, kWidth - 2 * d, kHeight - 2 * d}, T.acc.withAlpha(0.10f * (1.f - (float)k / 8.f)), std::max(1.f, glow / 8.f));
    }
    const int barsN = trig ? (fl < 700 ? 10 : 0) : (hold ? (int)(q8 * 6.f) : 0);
    for (int k = 0; k < barsN; ++k) {
        const float hh = 6.f + std::round(fxR_[(size_t)((k + 2) % 16)] * (trig ? 46.f : 22.f));
        const float l = std::round((fxR_[(size_t)((k + 5) % 16)] - 0.5f) * 80.f), r = std::round((fxR_[(size_t)((k + 9) % 16)] - 0.5f) * 80.f);
        c.fillRect(Rect{l, fxR_[(size_t)(k % 16)] * kHeight, kWidth - l - r, hh}, (k % 2 ? T.acc : T.scr).withAlpha(trig ? 0.85f : 0.5f));
    }
    if (hold) {
        for (float x = 0; x < kWidth * q8; x += 80.f) c.fillRect(Rect{x, 0, std::min(72.f, kWidth * q8 - x), 14.f}, T.acc);
    }
    if (hold && stepN >= 2) {
        const Font f{Face::Pixel, 26.f + (float)stepN * 4.f, 0.1f * (26.f + (float)stepN * 4.f)};
        const std::string s = "> X-Y ";
        const float cell = f.size * 0.62f;
        const float w = ui_.width(s, f) + 8.f * (cell + 4.f);
        const float x = 640.f - w * 0.5f, top = kHeight * 0.44f;
        const float off = (float)stepN;
        ui_.text(s, x + off, top, f, T.ink);
        ui_.text(s, x - off, top, f, T.hl);
        ui_.text(s, x, top, f, T.acc);
        // jauge ▮▯ dessinée (absente de la police)
        float bx = x + ui_.width(s, f);
        for (int k = 0; k < 8; ++k) {
            const Rect b{bx, top + f.size * 0.25f, cell, f.size * 0.85f};
            if (k < stepN) c.fillRect(b, T.acc);
            else ui_.border(b, T.acc, 2.f);
            bx += cell + 4.f;
        }
    } else if (trig && fl >= 60 && !(fl > 1000 && ((int)(fl / 80) % 2))) {
        const std::string full = std::string("X-Y : ") + (st_.xy ? "ON" : "OFF");
        const std::string s = full.substr(0, std::max<size_t>(1, std::min(full.size(), (size_t)((fl - 60) / 40))));
        const Font f{Face::Pixel, 96.f, 7.68f};
        const float w = ui_.width(s, f), x = 640.f - w * 0.5f, top = kHeight * 0.38f;
        ui_.text(s, x + 8.f, top, f, T.acc);
        ui_.text(s, x - 8.f, top, f, T.ink);
        for (int dx = -2; dx <= 2; dx += 2)
            for (int dy = -2; dy <= 2; dy += 2)
                if (dx || dy) ui_.text(s, x + (float)dx, top + (float)dy, f, T.ink);
        ui_.text(s, x, top, f, T.chassis);
    }
}

// --- glisser-déposer de fichiers WAV ------------------------------------------------------------------------
void Editor::handleDrops() {
    const Input& in = ui_.in();
    const Rect scrA{39.f, 126.f, 331.8f, 306.2f}, scrB{909.2f, 126.f, 331.8f, 306.2f};
    const Rect dz{381.f, 782.2f, 556.f, 99.8f};
    dropA_ = in.dragOver && tab_ == 0 && scrA.contains(in.dragPos);
    dropB_ = in.dragOver && tab_ == 0 && scrB.contains(in.dragPos);
    dropWt_ = in.dragOver && tab_ == 6 && dz.contains(in.dragPos);
    if (in.dropped.empty()) return;
    const std::string& path = in.dropped.front();
    if (pbOpen_) {  // pack (.raw2pack, dossier) ou preset (.raw) déposé sur la banque : copié dans Documents/Raw 2
        int n = 0;
        for (const std::string& p : in.dropped) {
            std::string msg;
            n += m_.installPackFile(p, msg);
            pbMsg_ = msg;
        }
        if (in.dropped.size() > 1) pbMsg_ = fmt("%d presets ajout\xC3\xA9s", n);
        pbView_ = 1;
        return;
    }
    if (tab_ == 0 && scrA.contains(in.dropPos)) wtImport(path, true, 0);
    else if (tab_ == 0 && scrB.contains(in.dropPos)) wtImport(path, true, 1);
    else if (tab_ == 6 && dz.contains(in.dropPos)) wtImport(path, false, st_.wtTgt);
}

} // namespace raw2::ui
