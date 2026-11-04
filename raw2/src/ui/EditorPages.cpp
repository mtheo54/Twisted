// Pages SON, VIE et MODULES.
#include "ui/Editor.h"
#include "core/Files.h"
#include "core/Wav.h"
#include "dsp/Bounce.h"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace raw2::ui {

namespace {
constexpr float kPi = 3.14159265358979f;
const char* kSrc[6] = {"VIE", "LFO 1", "LFO 2", "ENV", "CLAVIER", "AL\xC3\x89" "A"};
const char* kSrcSub[6] = {"grille", "sinus 0.35 Hz", "triangle 0.11 Hz", "enveloppe", "note jou\xC3\xA9" "e", "\xC3\xA9" "chantillon"};
const char* kDst[8] = {"PITCH A", "PITCH B", "POS. A", "POS. B", "CUTOFF", "R\xC3\x89" "SO", "GRAIN", "MASTER"};

struct RoleDef {
    const char* name;
    const char* range;
    int hl0, hl1;
};
const RoleDef kRoles[6] = {{"AUTO (PAD)", "80Hz-2kHz", 7, 9}, {"BASSE", "20Hz-250Hz", 4, 6}, {"LEAD", "500Hz-8kHz", 17, 22},
                           {"PAD", "80Hz-2kHz", 7, 9},        {"ARP / SEQ", "200Hz-4kHz", 12, 16}, {"TEXTURE", "2kHz-16kHz", 22, 27}};

const Wavetable& fallbackTable() {
    static const Wavetable t = [] {
        Wavetable w;
        buildFallback(w, 0.5f);
        return w;
    }();
    return t;
}

std::string pct(float v) { return fmt("%d %%", (int)std::lround(v * 100.f)); }
std::string signedPct(float v) {
    const int i = (int)std::lround(v);
    return (i > 0 ? "+" : "") + std::to_string(i) + " %";
}
std::string signed2(float v) {
    const float r = std::round(v * 100.f) / 100.f;
    return fmt(r >= 0.f ? "+%.2f" : "%.2f", r == 0.f ? 0.f : r);
}

std::string cpPrefix(const std::string& s, size_t n) {
    size_t i = 0, k = 0;
    while (i < s.size() && k < n) {
        ++i;
        while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) ++i;
        ++k;
    }
    return s.substr(0, i);
}
} // namespace

Color Editor::stageColor(int k) const {
    const Theme& T = *T_;
    const float q = (float)k / 5.f;
    Rgb8 A = T.accRGB, B = T.old;
    float f = q;
    if (T.hasMid) {
        if (q < 0.5f) { B = T.mid; f = q * 2.f; }
        else { A = T.mid; f = (q - 0.5f) * 2.f; }
    }
    return Color::rgb8((int)std::lround(A.r + (B.r - A.r) * f), (int)std::lround(A.g + (B.g - A.g) * f), (int)std::lround(A.b + (B.b - A.b) * f));
}

void Editor::legend(float x, float y) {
    const Font f{Face::Mono, 11.f, 0.f};
    const float w = ui_.text("jeune", x, y, f, T_->muted);
    float cx = x + w + 10.f;
    for (int k = 0; k < 6; ++k) {
        const Pt c{cx + 6.f, y + 7.f};
        ui_.c().fillEllipse(c, 6.f, 6.f, T_->ink);
        ui_.c().fillEllipse(c, 5.f, 5.f, stageColor(k));
        cx += 18.f;
    }
    ui_.text("ancienne", cx + 4.f, y, f, T_->muted);
}

// --- grille de la vie --------------------------------------------------------------------------
void Editor::lifeGrid(LifeEngine::World w, const Rect& area, float pitch, float rA, float rE, float rR, Id id, bool big) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    LifeView v;
    m_.life.view(w, v);
    const float s = area.w / ((float)v.GW * pitch);
    auto ctr = [&](int i) { return Pt{area.x + ((float)(i % v.GW) * pitch + pitch / 2) * s, area.y + ((float)(i / v.GW) * pitch + pitch / 2) * s}; };
    std::vector<Pt> alive[6], d1, d2;
    for (int i = 0; i < (int)v.g.size(); ++i) {
        if (v.g[(size_t)i]) {
            const float a = std::clamp(((float)v.age[(size_t)i] - 2.f) / 8.f, 0.f, 1.f);
            alive[std::clamp((int)std::lround(a * 5.f), 0, 5)].push_back(ctr(i));
        }
    }
    const double wall = LifeEngine::wallMs();
    for (const auto& d : v.dying) (wall - d.t < 40.0 ? d1 : d2).push_back(ctr(d.i));
    (void)rR;
    // fond : une lentille par case (maquette mise à jour), teintes tirées du fond de la grille
    {
        const Color lb = T.lifeBg, blk{0, 0, 0, 1}, wht{1, 1, 1, 1};
        c.lensGrid(Rect{area.x, area.y, (float)v.GW * pitch * s, (float)v.GH * pitch * s}, area.x, area.y, pitch * s, rA * s, lb.mix(blk, 0.55f), lb.mix(blk, 0.22f),
                   lb.mix(wht, 0.14f));
    }
    const Color deadBg = T.hasDeadFill ? T.deadFill : T.lifeBg;
    const Color accC = Color::rgb8(T.accRGB.r, T.accRGB.g, T.accRGB.b);
    c.fillCircles(d1.data(), (int)d1.size(), rA * s, deadBg.mix(accC, 0.55f));
    c.fillCircles(d2.data(), (int)d2.size(), rA * s, deadBg.mix(accC, 0.22f));
    const float ra = (T.hasCellEdge ? rE : rA) * s;
    // cellules vivantes : halo pulsé (4 paliers de 160 ms) pour les plus jeunes, comme le drop-shadow de la maquette
    const int ph = (int)(std::fmod(now_ / 160.0, 4.0));
    const float pulse = (float)(ph == 3 ? 1 : ph);
    for (int k = 0; k < 4; ++k) {
        const float g = (3.2f - (float)k * 0.6f + pulse * 0.9f) * s;
        c.fillCircles(alive[k].data(), (int)alive[k].size(), ra + g, stageColor(k).withAlpha(0.12f));
        c.fillCircles(alive[k].data(), (int)alive[k].size(), ra + g * 0.5f, stageColor(k).withAlpha(0.22f));
    }
    for (int k = 0; k < 6; ++k) {
        c.fillCircles(alive[k].data(), (int)alive[k].size(), ra, stageColor(k));
        if (T.hasCellEdge) c.strokeCircles(alive[k].data(), (int)alive[k].size(), ra - 0.5f * s, T.cellEdge, 1.f * s);
    }

    // page VIE : la fenêtre que montre la page SON est la zone écoutée (c'est elle qui module le son)
    if (big) {
        const float cw = pitch * s;
        const Rect zr{area.x + (float)LifeEngine::kWinX * cw, area.y + (float)LifeEngine::kWinY * cw, (float)LifeEngine::kWinW * cw, (float)LifeEngine::kWinH * cw};
        ui_.dashedBorder(zr, accC.withAlpha(0.85f), 6.f, 4.f);
        const Font zf{Face::Mono, 10.f, 1.f};
        const std::string zt = "ZONE \xC3\x89" "COUT\xC3\x89" "E \xC2\xB7 PAGE SON";
        const float tw = ui_.width(zt, zf) + 12.f;
        c.fillRect(Rect{zr.x, zr.y - 16.f, tw, 15.f}, T.lifeBg.withAlpha(0.85f));
        ui_.text(zt, zr.x + 6.f, zr.y - 14.5f, zf, accC);
    }

    // peinture à la souris
    const Ui::Interact it = ui_.interact(id, area, Cursor::Cross);
    const Pt m = ui_.in().mouse;
    const int cx = (int)std::floor((m.x - area.x) / s / pitch), cy = (int)std::floor((m.y - area.y) / s / pitch);
    const bool inside = cx >= 0 && cy >= 0 && cx < v.GW && cy < v.GH;
    const int cell = inside ? cy * v.GW + cx : -1;
    if (!big) {
        if (it.pressed && cell >= 0) {
            paintMode_ = v.g[(size_t)cell] ? 0 : 1;
            paintLast_ = cell;
            m_.life.setCell(w, cell, paintMode_ == 1);
        } else if (it.held && paintMode_ >= 0 && cell >= 0 && cell != paintLast_) {
            paintLast_ = cell;
            m_.life.setCell(w, cell, paintMode_ == 1);
        }
        if (it.released) paintMode_ = -1;
    } else {
        if (it.pressed && cell >= 0) {
            if (st_.brushMode == 2) {
                m_.life.stamp(w, st_.brushPat, cx, cy);
                bigLast_ = -1;
            } else {
                bigLast_ = cell;
                m_.life.paint(w, cx, cy, st_.brushSize, st_.brushMode == 1);
            }
        } else if (it.held && bigLast_ >= 0 && cell >= 0 && cell != bigLast_) {
            bigLast_ = cell;
            m_.life.paint(w, cx, cy, st_.brushSize, st_.brushMode == 1);
        }
        if (it.released) bigLast_ = -1;
    }
}

// --- oscillateurs ---------------------------------------------------------------------------------
// Colonne « bouton moleté + nom + afficheur » (maquette mise à jour).
void Editor::knobCol(uint32_t id, float cx, float top, float size, const std::string& label, const std::string& value, float mod, bool large) {
    knobParam(id, Pt{cx - size * 0.5f, top}, size, mod);
    const Theme& T = *T_;
    if (large) {
        ui_.textCenter(label, Rect{cx - 60.f, top + size + 6.f, 120.f, 14.f}, Font{Face::Mono, 11.f, 1.32f}, T.muted);
        ui_.lcd(value, cx, top + size + 24.f, true);
    } else if (size > 60.f) {
        ui_.textCenter(label, Rect{cx - 50.f, top + size, 100.f, 13.f}, Font{Face::Mono, 10.f, 1.f}, T.muted);
        ui_.lcd(value, cx, top + size + 16.f);
    } else {
        ui_.textCenter(label, Rect{cx - 45.f, top + size + 2.f, 90.f, 12.f}, Font{Face::Mono, 9.f, 0.72f}, T.muted);
        ui_.lcd(value, cx, top + size + 19.f);
    }
}

// Écran à lignes de balayage (oscilloscopes, sortie FX).
void Editor::scanScreen(const Rect& r) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.screen(r);
    c.save();
    c.clip(r.inset(1.f));
    for (float y = r.y + 1.f; y < r.b() - 1.f; y += 3.f) c.fillRect(Rect{r.x + 1.f, y, r.w - 2.f, 1.f}, Color{1, 1, 1, 0.04f});
    c.restore();
    c.fillRect(Rect{r.x + 1.f, r.y + 1.f, r.w - 2.f, 1.f}, Color{0, 0, 0, 0.4f});
    c.fillRect(Rect{r.x + 1.f, r.y + 1.f, 1.f, r.h - 2.f}, Color{0, 0, 0, 0.4f});
    (void)T;
}

// Trace lumineuse (halo puis trait), comme le filtre drop-shadow de la maquette.
void Editor::glowLine(const std::vector<Pt>& pts, Color col, float width) {
    Canvas& c = ui_.c();
    if (pts.size() < 2) return;
    c.polyline(pts.data(), (int)pts.size(), col.withAlpha(0.22f), 5.f);
    c.polyline(pts.data(), (int)pts.size(), col.withAlpha(0.35f), width + 1.5f);
    c.polyline(pts.data(), (int)pts.size(), col, width);
}

void Editor::oscCard(int osc, const Rect& card) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.card(card);
    ui_.cardHeader(card, osc == 0 ? "OSC A" : "OSC B", false, 14.f, 12.f);
    const uint32_t pOct = osc == 0 ? kAOct : kBOct, pSt = osc == 0 ? kASt : kBSt;
    const uint32_t pPos = osc == 0 ? kAPos : kBPos, pLvl = osc == 0 ? kALvl : kBLvl;
    const uint32_t pWarp = osc == 0 ? kAWarp : kBWarp, pPan = osc == 0 ? kAPan : kBPan, pMode = osc == 0 ? kAMode : kBMode;
    const float right = card.r() - 15.f, top = card.y + 13.f;

    bool mounted;
    std::string tname;
    {
        std::lock_guard<std::mutex> lk(m_.tableMutex);
        mounted = m_.mounted[(size_t)osc];
        tname = m_.edit[(size_t)osc].name;
    }
    // instrument joué en SAMPLE : son nom remplace celui de la table
    const bool instOn = !m_.instruments.name(osc).empty() && P(osc ? kBPlay : kAPlay) > 0.5f;
    if (instOn) {
        mounted = true;
        tname = std::string("INST \xC2\xB7 ") + m_.instruments.name(osc);
    }
    // octave / demi-tons et badge de la table, alignés à droite
    const Font pf{Face::Mono, 10.f, 0.6f};
    const int oct = m_.params.geti(pOct), st = m_.params.geti(pSt);
    const std::string so = (oct > 0 ? "+" : "") + std::to_string(oct) + " OCT";
    const std::string ss = (st > 0 ? "+" : "") + std::to_string(st) + " ST";
    const float w1 = ui_.width(so, pf) + 14.f, w2 = ui_.width(ss, pf) + 15.f;
    const Font bf{Face::Mono, 10.f, mounted ? 0.2f : 1.f};
    const Font xf{Face::Mono, 10.f, 0.4f};
    const float xw = mounted ? ui_.width("[ x ]", xf) + 4.f : 0.f;
    const float titleW = ui_.width(osc == 0 ? "OSC A" : "OSC B", Font{Face::Pixel, 16.f, 1.28f});
    const float maxName = std::clamp(card.w - 30.f - titleW - 12.f - (w1 + w2 + 3.f) - 6.f - 18.f - 6.f - xw, 30.f, 120.f);
    const std::string badgeText = mounted ? ui_.fit(tname, bf, maxName) : std::string("WAVETABLE");
    const float lw = ui_.width(badgeText, bf);
    const float bw = 18.f + lw + (mounted ? 6.f + xw : 0.f);
    const Rect badge{right - bw, top, bw, 21.f};
    ui_.box(badge, Color{0, 0, 0, 0}, mounted ? T.hl : T.rule);
    const Rect nameR{badge.x + 9.f, top, lw, 21.f};
    if (ui_.interact(makeId("wtName", osc), nameR).clicked) {
        st_.wtTgt = osc;
        setTab(6);
    }
    ui_.text(badgeText, badge.x + 9.f, top + 4.f, bf, T.ink);
    if (mounted) {
        const Rect xr{badge.r() - 9.f - xw, top + 1.f, xw, 19.f};
        if (ui_.interact(makeId("wtX", osc), xr).clicked) {  // retire la table, le son déposé et l'instrument
            m_.setMounted(osc, false);
            m_.samples.clear(osc);
            m_.instruments.clear(osc);
            setParam(osc ? kBPlay : kAPlay, 0.f);
        }
        ui_.text("[ x ]", xr.x + 2.f, top + 4.f, xf, T.hl);
    }
    {
        const Rect box{badge.x - 6.f - (w1 + w2 + 3.f), top, w1 + w2 + 3.f, 21.f};
        ui_.box(box, Color{0, 0, 0, 0}, T.rule);
        const Rect r1{box.x + 1.f, top + 1.f, w1, 19.f}, r2{box.x + 1.f + w1 + 1.f, top + 1.f, w2, 19.f};
        c.fillRect(Rect{r2.x - 1.f, r2.y, 1.f, r2.h}, T.rule);
        const Ui::Interact i1 = ui_.interact(makeId("oct", osc), r1), i2 = ui_.interact(makeId("st", osc), r2);
        if (i1.clicked) setParam(pOct, (float)(oct >= 2 ? -2 : oct + 1));
        if (i2.clicked) setParam(pSt, (float)(st >= 12 ? -12 : st + 1));
        const float wh = ui_.in().wheel;
        if (wh != 0.f && i1.hovered) setParam(pOct, (float)std::clamp(oct + (wh > 0 ? 1 : -1), -2, 2));
        if (wh != 0.f && i2.hovered) setParam(pSt, (float)std::clamp(st + (wh > 0 ? 1 : -1), -12, 12));
        ui_.textCenter(so, r1, pf, T.ink);
        ui_.textCenter(ss, r2, pf, T.ink);
    }

    // écran : forme d'onde jouée (position modulée, warp, mode de phase), graduation et télémétrie
    const Rect scr{card.x + 15.f, card.y + 53.f, card.w - 30.f, 306.2f};
    scanScreen(scr);
    const Rect area{scr.x + 7.f, scr.y + 7.f, scr.w - 14.f, scr.h - 14.f};
    const bool liveAudio = m_.readouts.active.load() && m_.readouts.voices.load() > 0;
    const float pos = std::clamp(liveAudio ? (osc == 0 ? m_.readouts.posA.load() : m_.readouts.posB.load()) : P(pPos), 0.f, 1.f);
    const int heldN = mouseNote_ >= 0 ? mouseNote_ : (m_.readouts.heldNote.load() >= 0 ? m_.readouts.heldNote.load() - 48 : -1);
    const float kf = heldN >= 0 ? std::pow(2.f, (float)(heldN - 12) / 12.f) : 1.f;
    const float bend = std::pow(2.f, P(kPitchBend) * 2.f / 12.f);
    const float ratio = std::pow(2.f, (float)m_.params.geti(pOct) + (float)m_.params.geti(pSt) / 12.f);
    const float cyc = 2.f * kf * ratio * bend;
    const float amp = (0.35f + 0.65f * P(pLvl)) * (1.f + 0.2f * hit_);
    const float tt = (float)(now_ / 1000.0), rate = osc == 0 ? 0.2f : 0.28f;
    const int mode = m_.params.geti(pMode);
    const float warp = std::clamp(P(pWarp) + P(kModWheel) * 0.3f, 0.f, 1.f);
    const float ph0 = mode == 1 ? 0.25f : (mode == 2 ? 0.5f : 0.f);
    auto wrp = [&](float u) {  // même déformation de phase que le moteur
        u += ph0;
        if (mode == 3) { u = u * 2.f; }
        if (mode == 4) u += tt * 0.15f;
        u -= std::floor(u);
        u += warp * 0.22f * std::sin(6.2831853f * u);
        return u;
    };
    c.save();
    c.clip(scr.inset(1.f));
    c.line(Pt{area.x, area.cy()}, Pt{area.r(), area.cy()}, T.scrMid, 1.f);
    float rmsAcc = 0.f;
    {
        std::lock_guard<std::mutex> lk(m_.tableMutex);
        const Wavetable& tab = m_.mounted[(size_t)osc] ? m_.edit[(size_t)osc] : fallbackTable();
        std::vector<Pt> pts;
        const Color wc = osc == 0 ? T.waveA : T.waveB;
        if (!st_.xy) {
            for (int i = 0; i <= 240; ++i) {
                const float x = (float)i / 240.f;
                const float v0 = sampleTable(tab, pos, wrp(cyc * x - tt * rate)) * amp;
                rmsAcc += v0 * v0;
                const float v = std::clamp(v0, -1.15f, 1.15f);
                pts.push_back(Pt{area.x + x * area.w, area.y + (55.f - v * 41.8f) / 110.f * area.h});
            }
        } else {
            const float span = 2.f / cyc;
            for (int i = 0; i <= 240; ++i) {
                const float u = (float)i / 240.f * span;
                const float a = sampleTable(tab, pos, wrp(cyc * u - tt * rate)) * amp, b = sampleTable(tab, pos, wrp(cyc * (u - 0.25f / cyc) - tt * rate)) * amp;
                rmsAcc += a * a;
                pts.push_back(Pt{area.x + (150.f + a * 44.f) / 300.f * area.w, area.y + (55.f - b * 44.f) / 110.f * area.h});
            }
        }
        glowLine(pts, wc, 1.5f);
    }
    c.restore();
    // graduation à gauche et télémétrie (mise à jour toutes les 120 ms)
    {
        const Font sf{Face::Mono, 8.f, 0.48f};
        const float gx = scr.x + 9.f, gy = scr.y + 9.f, gh = scr.h - 18.f;
        c.fillRect(Rect{gx, gy, 1.f, gh}, T.scrMid);
        static const char* lab[4] = {"+6dB", "0dB", "-6dB", "-\xE2\x88\x9E"};
        for (int k = 0; k < 4; ++k) ui_.text(lab[k], gx + 5.f, gy + (gh - 8.f) * (float)k / 3.f, sf, T.scrMuted, 8.f);
        if (now_ - teleT_[osc] > 120.0 || tele_[osc].empty()) {
            teleT_[osc] = now_;
            const int noteRel = std::max(heldN, 0);
            const float f0 = 130.81f * std::pow(2.f, (float)noteRel / 12.f) * ratio * bend;
            const float rms = 20.f * std::log10(std::max(1e-3f, std::sqrt(rmsAcc / 241.f)));
            tele_[osc] = fmt("F0: %.2f Hz // RMS: ", f0) + fmt("%.1f dB", rms);
        }
        const Font tf{Face::Mono, 9.f, 0.72f};
        ui_.textRight(tele_[osc], scr.r() - 11.f, scr.y + 7.f, tf, T.scrMuted);
    }
    if (osc == 0 ? dropA_ : dropB_) {
        c.fillRect(scr, T.scr.withAlpha(0.85f));
        for (int k = 0; k < 6; ++k) ui_.border(scr.inset((float)k * 2.f), T.acc.withAlpha(k ? 0.35f / (float)k : 1.f), k ? 2.f : 1.f);
        ui_.textCenter("> DROP WAV TO LOAD", scr, Font{Face::Mono, 12.f, 1.44f}, T.hl);
    }

    // mode de phase : un clic passe au suivant (SYNC 0°, 90°, 180°, LOOP, FREE)
    {
        ButtonStyle s;
        s.bg = Color{0, 0, 0, 0}; s.fg = T.ink; s.border = T.ink; s.shadow = false; s.dashed = true;
        s.font = Font{Face::Mono, 10.f, 1.4f};
        const std::string lbl = std::string("[ ") + oscModeName(mode) + " ]";
        if (ui_.button(makeId("oscMode", osc), Rect{card.x + 15.f, card.y + 365.2f, card.w - 30.f, 25.f}, lbl, s)) setParam(pMode, (float)((mode + 1) % 5));
    }

    // potards POSITION / WARP / PAN / NIVEAU
    const int target = m_.params.geti(kTarget);
    float posMod = 0.f, lvlMod = 0.f;
    {
        const float qty = P(kQty);
        if (target == 1) posMod += qty * (osc == 0 ? 0.25f : 0.375f) * 0.6f;
        if (target == 2) lvlMod += qty * 0.25f;
        for (int s = 0; s < kNumSources; ++s) {
            const size_t i = (size_t)ModMatrix::idx(s, osc == 0 ? 2 : 3);
            if (m_.matrix.on[i]) posMod += std::fabs((float)m_.matrix.amt[i] / 100.f) * 0.35f;
        }
        posMod = std::min(0.5f, posMod);
    }
    const float colW = (card.w - 30.f - 6.f) / 4.f;
    const float ky = card.y + 398.2f;
    const uint32_t ids[4] = {pPos, pWarp, pPan, pLvl};
    const char* names[4] = {"POSITION", "WARP", "PAN", "NIVEAU"};
    const std::string vals[4] = {pct(P(pPos)), pct(P(pWarp)), panText(P(pPan)), levelText(P(pLvl))};
    const float mods[4] = {posMod, P(kModWheel) * 0.3f, 0.f, lvlMod};
    for (int k = 0; k < 4; ++k) knobCol(ids[k], card.x + 15.f + (float)k * (colW + 2.f) + colW * 0.5f, ky, 52.f, names[k], vals[k], mods[k], false);
}

// --- spectre de SORTIE ------------------------------------------------------------------------------
void Editor::spectrumBars(const Rect& box) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.screen(box);
    son_.resize(28, now_);
    an_.bands(28, bandTmp_);
    const RoleDef& rd = kRoles[shownRole()];
    for (int i = 0; i < 28; ++i) {
        const float db = bandTmp_[(size_t)i];
        const int nT = std::clamp((int)std::lround(1.f + (db + 60.f) / 54.f * 10.f), 1, 11);
        son_.push(i, nT, now_, 35.0, 60.0);
        const int n = son_.sh[(size_t)i], p = son_.pk[(size_t)i];
        const Color col = (i >= rd.hl0 && i <= rd.hl1) ? T.barPk : (n >= 5 ? T.barHi : T.barLo);
        const float x = box.x + 13.f + (float)i * 12.f, bottom = box.y + 87.f;
        for (int k = 0; k < n; ++k) c.fillRect(Rect{x, bottom - (float)k * 7.f - 5.f, 8.f, 5.f}, col);
        if (p > n) c.fillRect(Rect{x, bottom - (float)(p - 1) * 7.f - 5.f, 8.f, 5.f}, T.barPk);
    }
}

// --- terminal PROPOSITION ----------------------------------------------------------------------------
// Les propositions viennent de l'écoute interne (§9.5) : RoleNet reconnaît le rôle du son ; l'assistant
// essaie des retouches sur une copie du son et garde celles qui le rapprochent le plus du rôle visé.
namespace {
struct Rec {
    std::vector<std::pair<std::string, bool>> segs;
    std::string ok;
    bool idle = false;
    bool busy = false;
};

std::string pctText(float p) { return fmt("%d %%", (int)std::lround(p * 100.f)); }
std::string hzLabel(int hz) { return hz < 1000 ? fmt("%d Hz", (int)std::lround(hz / 10.0) * 10) : fmt("%.1f kHz", hz / 1000.0); }

Rec currentRec(const EditorState& st, bool busy) {
    Rec r;
    if (!ai::networksTrained()) {
        r.segs = {{"R\xC3\xA9seau des r\xC3\xB4les non entra\xC3\xAEn\xC3\xA9 : lance ", false}, {"raw2_train", true}, {" puis recompile.", false}};
        r.ok = "rien n'a chang\xC3\xA9";
        r.idle = r.busy = true;
        return r;
    }
    if (busy || !st.soundValid) {
        r.segs = {{"\xC3\x89" "coute interne du son (", false}, {"3 notes", true}, {"), puis essais de retouches\xE2\x80\xA6", false}};
        r.busy = true;
        return r;
    }
    const ai::SoundAnalysis& a = st.sound;
    const std::string R = ai::roleName(a.target);
    const size_t n = a.props.size() + 1;
    const size_t k = (size_t)std::max(0, st.rec) % n;
    if (a.silent) {
        r.segs = {{"Rien \xC3\xA0 l'\xC3\xA9" "coute : le son est muet. Remonte un oscillateur, puis ", false}, {"relance l'analyse ?", true}};
        r.ok = "[OK] analyse relanc\xC3\xA9" "e";
        r.idle = true;
        return r;
    }
    if (k < a.props.size()) {
        const ai::Proposal& p = a.props[k];
        if (k == 0) {
            r.segs.push_back({"R\xC3\xA9seau : ", false});
            r.segs.push_back({R + " " + pctText(p.before), true});
            if (a.guess.best != a.target) {
                r.segs.push_back({" (entendu : ", false});
                r.segs.push_back({ai::roleName(a.guess.best), true});
                r.segs.push_back({")", false});
            }
            if (a.bandHz > 0) {
                r.segs.push_back({a.bandExcess ? ", trop d'\xC3\xA9nergie vers " : ", manque vers ", false});
                r.segs.push_back({hzLabel(a.bandHz), true});
            }
            r.segs.push_back({". ", false});
        } else {
            r.segs.push_back({"Autre piste : ", false});
        }
        r.segs.push_back({p.what.back() == '\'' ? p.what : p.what + " ", false});
        r.segs.push_back({p.value, true});
        r.segs.push_back({" \xE2\x86\x92 ", false});
        r.segs.push_back({R + " " + pctText(p.after), true});
        r.segs.push_back({" ?", false});
        r.ok = "[OK] " + p.done + " \xC2\xB7 " + R + " " + pctText(p.after);
        return r;
    }
    if (a.props.empty()) {
        r.segs = {{"R\xC3\xA9seau : ", false}, {R + " " + pctText(a.guess.p[a.target]), true},
                  {fmt(". Aucune des %d retouches essay\xC3\xA9" "es ne l'en rapproche. ", a.tried), false}, {"Relancer l'analyse ?", true}};
    } else {
        r.segs = {{"Aucune autre proposition pour ce r\xC3\xB4le. ", false}, {"Relancer l'analyse ?", true}};
    }
    r.ok = "[OK] analyse relanc\xC3\xA9" "e";
    r.idle = true;
    return r;
}
} // namespace

int Editor::shownRole() const {
    if (st_.role != 0) return st_.role;
    return (st_.soundValid && st_.sound.autoRole) ? st_.sound.guess.best + 1 : 3;  // AUTO : rôle reconnu (PAD avant l'écoute)
}

std::string Editor::roleLabel(int r) const {
    if (r != 0) return kRoles[r].name;
    if (st_.soundValid && st_.sound.autoRole) return std::string("AUTO (") + ai::roleName(st_.sound.guess.best) + ")";
    return kRoles[0].name;
}

// Lance l'écoute interne quand le son a changé (après 600 ms de calme) et récupère le résultat.
void Editor::soundAdvisorFrame() {
    if (!ai::networksTrained()) return;
    uint64_t h = 1469598103934665603ull;
    auto mixIn = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (uint32_t i = 0; i < kNumParams; ++i) {
        if (i == kMaster) continue;
        mixIn((uint64_t)std::llround(P(i) * 10000.0));
    }
    {
        std::lock_guard<std::mutex> lk(m_.tableMutex);
        mixIn(m_.tableVersion[0]);
        mixIn(m_.tableVersion[1]);
    }
    for (size_t i = 0; i < m_.matrix.amt.size(); ++i) mixIn(m_.matrix.on[i].load() ? (uint64_t)(1000 + m_.matrix.amt[i].load()) : 0u);
    mixIn((uint64_t)st_.role);
    if (h != sigSeen_) {
        sigSeen_ = h;
        sigT_ = now_;
    }
    bool stepping = false;
    for (const auto& t : st_.tgt) stepping = stepping || t.has_value();
    if (!advisor_) advisor_ = std::make_unique<ai::SoundAdvisor>();
    if (forceSound_ || (h != st_.soundSig && h != askedSig_ && now_ - sigT_ > 600.0 && !stepping)) {
        forceSound_ = false;
        askedSig_ = h;
        advisor_->request(ai::snapshotSound(m_), st_.role == 0 ? -1 : st_.role - 1);
    }
    ai::SoundAnalysis res;
    if (advisor_->poll(res)) {  // le résultat correspond toujours à la dernière demande
        st_.sound = std::move(res);
        st_.soundValid = true;
        st_.soundSig = askedSig_;
        st_.rec = 0;
        awaitSound_ = false;
    }
}

void Editor::acceptRec() {
    const Rec r = currentRec(st_, awaitSound_ || (advisor_ && advisor_->busy()));
    if (r.busy) return;
    if (!r.idle) {
        const ai::Proposal& p = st_.sound.props[(size_t)st_.rec % (st_.sound.props.size() + 1)];
        for (const auto& ch : p.changes) {
            if (paramDef(ch.first).steps > 0) {
                cancelTarget(ch.first);
                setParam(ch.first, ch.second);
            } else {
                st_.tgt[ch.first] = ch.second;  // par paliers, jamais en continu
            }
        }
        pushHist(p.done, "APPLIQU\xC3\x89", "SON");
    } else {
        forceSound_ = true;
    }
    awaitSound_ = true;  // le son change : on attend la nouvelle écoute
    st_.status = r.ok;
    st_.statusOn = true;
    st_.rec = st_.rec + 1;
    st_.typeT = now_;
}

void Editor::ignoreRec() {
    const Rec r = currentRec(st_, awaitSound_ || (advisor_ && advisor_->busy()));
    if (r.busy) return;
    st_.status = "ignor\xC3\xA9 \xC2\xB7 rien n'a chang\xC3\xA9";
    st_.statusOn = false;
    if (!r.idle) {
        std::string all;
        for (const auto& s : r.segs) all += s.first;
        pushHist(cpPrefix(all, 40) + "\xE2\x80\xA6", "IGNOR\xC3\x89", "SON");
    }
    st_.rec = st_.rec + 1;
    st_.typeT = now_;
}

void Editor::proposition(const Rect& card) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.cardTerm(card);
    const float x = card.x + 23.f, w = card.w - 46.f, top = card.y + 21.f;
    ui_.text("PROPOSITION", x, top + 2.5f, Font{Face::Pixel, 16.f, 1.28f}, T.scrText, 16.f);
    // étiquettes sur la même ligne que le titre (maquette mise à jour)
    const float ty = top;
    const float tagW = ui_.tag("mix \xC2\xB7 ia", x + w, ty, Ui::Tag::Accent);
    {
        const std::string lbl = std::string("[ CIBLE : ") + roleLabel(st_.role) + " \xE2\x96\xBE ]";
        const Font f{Face::Mono, 10.f, 0.6f};
        const float bw = ui_.width(lbl, f) + 18.f;
        ButtonStyle s;
        s.bg = Color{0, 0, 0, 0};
        s.fg = T.scrText;
        s.border = T.scrMuted;
        s.shadow = false;
        s.dashed = true;
        s.font = f;
        if (ui_.button(makeId("roleBtn"), Rect{x + w - tagW - 8.f - bw, ty, bw, 21.f}, lbl, s)) roleOpen_ = !roleOpen_;
    }
    c.fillRect(Rect{x, top + 31.f, w, 1.f}, T.scrRule);
    const RoleDef& rd = kRoles[shownRole()];
    ui_.text(std::string("ROLE : ") + rd.name + " // ANALYSE SPECTRE : " + rd.range, x, top + 46.f, Font{Face::Mono, 10.f, 0.8f}, T.scrMuted);

    // machine à écrire : 20 ms par caractère, retour à la ligne par mots ; un texte qui change est retapé
    const Rec r = currentRec(st_, awaitSound_ || (advisor_ && advisor_->busy()));
    {
        std::string all;
        for (const auto& seg : r.segs) all += seg.first;
        if (all != st_.recShown) {
            st_.recShown = all;
            st_.typeT = now_;
        }
    }
    size_t left = (size_t)std::max(0.0, (now_ - st_.typeT) / 20.0);
    struct Ch { std::string s; bool hl; };
    std::vector<Ch> chars;
    for (const auto& seg : r.segs) {
        for (size_t i = 0; i < seg.first.size() && left > 0;) {
            size_t j = i + 1;
            while (j < seg.first.size() && ((unsigned char)seg.first[j] & 0xC0) == 0x80) ++j;
            chars.push_back({seg.first.substr(i, j - i), seg.second});
            i = j;
            --left;
        }
    }
    const Font nf{Face::Mono, 13.f, 0.f}, bfnt{Face::MonoBold, 13.f, 0.f};
    const float cw = ui_.width("M", nf), lh = 22.1f;
    float lineTop = top + 67.f;
    ui_.text(">", x, lineTop, nf, T.scrMuted, lh);
    float cx = x + cw + 8.f;
    size_t i = 0;
    while (i < chars.size()) {
        // longueur du mot suivant (espaces comprises jusqu'au prochain blanc)
        size_t j = i;
        while (j < chars.size() && chars[j].s != " ") ++j;
        const size_t wordEnd = j < chars.size() ? j + 1 : j;
        const float ww = (float)(j - i) * cw;
        if (cx + ww > x + w + 0.5f && cx > x + 0.5f) {
            lineTop += lh;
            cx = x;
        }
        for (size_t k = i; k < wordEnd; ++k) {
            ui_.text(chars[k].s, cx, lineTop, chars[k].hl ? bfnt : nf, chars[k].hl ? T.hl : T.scrText, lh);
            cx += cw;
        }
        i = wordEnd;
    }
    if (std::fmod(now_ / 1000.0, 1.0) < 0.5) {
        if (cx + cw * 2 > x + w) { lineTop += lh; cx = x; }
        ui_.text(" \xE2\x96\x88", cx, lineTop, nf, T.hl, lh);
    }

    // boutons
    const float by = card.b() - 89.f;
    ButtonStyle yes;
    yes.bg = T.acc; yes.fg = T.onAcc; yes.border = T.acc; yes.shadow = false;
    yes.font = Font{Face::MonoBold, 12.f, 1.2f};
    ButtonStyle no;
    no.bg = Color{0, 0, 0, 0}; no.fg = T.scrText; no.border = T.scrMuted; no.shadow = false;
    no.font = Font{Face::Mono, 12.f, 1.2f};
    const float bw = (w - 10.f) / 2.f;
    if (ui_.button(makeId("accept"), Rect{x, by, bw, 40.f}, "[ ACCEPTER ]", yes)) acceptRec();
    if (ui_.button(makeId("ignore"), Rect{x + bw + 10.f, by, bw, 40.f}, "[ IGNORER ]", no)) ignoreRec();
    const std::string status = st_.status.empty() ? std::string("rien ne change sans ton accord") : st_.status;
    ui_.text(status, x, by + 52.f, Font{Face::Mono, 11.f, 0.f}, (!st_.status.empty() && st_.statusOn) ? T.hl : T.scrMuted);
}

void Editor::depthModule(int i, const Rect& cell) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    static const char* names[3] = {"HARMONIQUES", "ESPACE", "TEXTURE"};
    static const char* subs[3] = {"grille \xE2\x86\x92 timbre", "grille \xE2\x86\x92 position", "grille \xE2\x86\x92 grains"};
    const uint32_t id = (uint32_t)(kM1 + i);
    const float v = P(id);
    const bool act = std::lround(v) != 0;
    const float cy = cell.cy();
    if (act) ui_.box(Rect{cell.x + 20.f, cy - 5.f, 10.f, 10.f}, T.acc, T.rule, 0.f);
    else ui_.box(Rect{cell.x + 20.f, cy - 5.f, 10.f, 10.f}, Color{0, 0, 0, 0}, T.muted, 0.f);
    ui_.text(names[i], cell.x + 42.f, cy - 15.f, Font{Face::Mono, 12.f, 1.44f}, act ? T.ink : T.muted);
    ui_.text(subs[i], cell.x + 42.f, cy + 1.f, Font{Face::Mono, 11.f, 0.f}, T.muted);
    const Rect d{cell.r() - 92.f, cy - 12.5f, 72.f, 25.f};
    if (dd_.on && ui_.hit(d)) {  // destination possible d'une route libre (glisser-déposer)
        dd_.hov = id;
        dd_.hovRect = d;
    }
    const Ui::Interact it = ui_.interact(makeId("depth", i), d, Cursor::ResizeEW);
    static float startX = 0.f, startV = 0.f;
    if (it.pressed) {
        startX = ui_.in().mouse.x;
        startV = v;
        beginDrag(id);
    }
    if (it.held && !it.pressed) {
        const float lo = i == 2 ? 0.f : -100.f;
        const float nv = std::clamp(std::round(startV + (ui_.in().mouse.x - startX) * 1.2f), lo, 100.f);
        if (nv != P(id)) dragParam(id, nv);
    }
    if (it.released) endDrag(id);
    ui_.textRight(signedPct(v), d.r(), d.y, Font{Face::MonoBold, 12.f, 0.f}, act ? T.ink : T.muted);
    const float ty = d.y + 22.f;
    c.fillRect(Rect{d.x, ty, 72.f, 3.f}, T.tickOff);
    c.fillRect(Rect{d.x + 36.f, ty - 2.f, 1.f, 7.f}, T.muted);
    const float fw = std::fabs(v) / 100.f * 36.f;
    c.fillRect(Rect{v >= 0 ? d.x + 36.f : d.x + 36.f - fw, ty, fw, 3.f}, T.ptr);
    // routes libres : la valeur modulée en direct, en orange
    const int nr = m_.routes.count();
    float mv = v;
    bool routed = false;
    for (int k = 0; k < nr; ++k)
        if ((uint32_t)m_.routes.r[(size_t)k].id.load() == id) {
            routed = true;
            const int src = m_.routes.r[(size_t)k].src.load();
            mv += (float)m_.routes.r[(size_t)k].amt.load() / 100.f * 200.f * m_.readouts.lfoVal[(size_t)std::clamp(src, 0, kNumLfos - 1)].load() * (i == 2 ? 0.5f : 1.f);
        }
    if (routed) {
        const float lo = i == 2 ? 0.f : -100.f;
        mv = std::clamp(mv, lo, 100.f);
        c.fillRect(Rect{d.x + 36.f + mv / 100.f * 36.f - 1.5f, ty - 3.f, 3.f, 9.f}, Color::rgb8(255, 122, 26));
    }
}

// --- clavier, molettes, MASTER (carte SORTIE de la maquette mise à jour) ---------------------------
namespace {
const int kScales[6][13] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1}, {0, 2, 4, 5, 7, 9, 11, -1}, {0, 2, 3, 5, 7, 8, 10, -1},
    {0, 2, 3, 5, 7, 9, 10, -1}, {0, 3, 5, 7, 10, -1}, {0, 3, 5, 6, 7, 10, -1}};
bool inScale(int scale, int n) {
    const int pc = ((n % 12) + 12) % 12;
    for (const int* p = kScales[std::clamp(scale, 0, 5)]; *p >= 0; ++p)
        if (*p == pc) return true;
    return false;
}
// note jouée : la note de la gamme juste en dessous (ou égale)
int quantize(int scale, int n) {
    for (int d = 0; d < 12; ++d)
        if (inScale(scale, n - d)) return n - d;
    return n;
}
const Color kCream{226.f / 255.f, 222.f / 255.f, 214.f / 255.f, 1.f};

// Couleurs du clavier et de son rail. ALU garde le dessin de la maquette (rail grège, touches
// crème) ; les autres thèmes clairs (ULTRAVIOLET, ONIRIQUE) prennent leur palette (rail, touches
// et dièses du thème) au lieu de ce grège chaud qui jurait avec elle ; les thèmes sombres gardent
// leur rail.
struct KbColors { Color rail, railEdge, nat, natSkirt, natInk, sharp, sharpSkirt, sharpHi; };
KbColors kbColors(const Theme& T) {
    KbColors k;
    const bool light = (T.railBg.r + T.railBg.g + T.railBg.b) / 3.f > 0.75f;
    const bool alu = std::strcmp(T.key, "alu") == 0;
    k.rail = light ? Color::rgb8(200, 196, 187) : T.railBg;
    k.railEdge = Color::rgb8(168, 164, 156);
    k.nat = kCream;
    k.natSkirt = Color::rgb8(156, 151, 142);
    k.natInk = Color::rgb8(26, 26, 26);
    k.sharp = Color::rgb8(28, 28, 28);
    k.sharpSkirt = Color{0, 0, 0, 1};
    k.sharpHi = Color::rgb8(68, 68, 68);
    if (light && !alu) {
        k.rail = T.railBg.mix(T.line, 0.16f);
        k.railEdge = T.railBg.mix(T.line, 0.32f);
        k.nat = T.keyNat.mix(k.rail, 0.35f);
        k.natSkirt = k.rail.mix(T.line, 0.3f);
        k.natInk = T.keyNatLine;
        k.sharp = T.keySharp;
        k.sharpSkirt = T.keySharp.mix(Color{0, 0, 0, 1}, 0.6f);
        k.sharpHi = T.keySharp.mix(Color{1, 1, 1, 1}, 0.22f);
    }
    return k;
}
} // namespace

// bouton « pilule » du bandeau du clavier (dégradé crème, ombre dure)
// rendu du son en cours (dernière note jouée, sinon C3) dans Documents › Raw 2 › Exports
std::string Editor::exportWav() {
    const std::string dir = files::exportsDir();
    files::makeDirs(dir);
    std::string name;
    {
        std::lock_guard<std::mutex> lk(m_.presetMutex);
        name = m_.presetName.empty() ? std::string("RAW2") : m_.presetName;
    }
    for (char& ch : name)
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') ch = '_';
    const int note = lastNote_ >= 0 ? lastNote_ : 48;
    static const char* nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const std::string path = dir + (dir.empty() ? "" : "/") + "RAW2 - " + name + " - " + nn[note % 12] + std::to_string(note / 12 - 1) + ".wav";
    return bounceSound(m_, path, note, m_.params.get(kArpOn) > 0.5f ? 4.0 : 1.5) ? path : std::string();
}

void Editor::keyboardCard(const Rect& so) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    ui_.card(so);
    ui_.cardHeader(so, "SORTIE", false, 14.f, 12.f);
    // maquette mise à jour : le panneau ne garde que le clavier (molettes et bandeau retirés ;
    // pitch bend et molette MOD restent joués en MIDI)
    const float tw = ui_.tag("clavier", so.r() - 15.f, so.y + 13.f, Ui::Tag::Line);
    // [ GLISSER EN .WAV ] : la note tenue (avec effets, accords, arpège) rendue dans un WAV, à
    // glisser dans la DAW ; un simple clic l'enregistre dans Documents › Raw 2 › Exports
    {
        const Rect br{so.r() - 15.f - tw - 10.f - 168.f, so.y + 11.f, 168.f, 24.f};
        const Ui::Interact it = ui_.interact(makeId("wavOut"), br, Cursor::Hand);
        if (it.pressed) wavPress_ = ui_.in().mouse;
        const float mv = std::hypot(ui_.in().mouse.x - wavPress_.x, ui_.in().mouse.y - wavPress_.y);
        const bool drag = it.held && mv > 6.f && dragOut_.empty() && !wavDragged_;
        if (drag || it.clicked) {
            const std::string path = exportWav();
            if (!path.empty()) {
                if (drag) { dragOut_ = path; wavDragged_ = true; }
                wavMsg_ = "â Documents âº Raw 2 âº Exports âº " + fileBaseName(path) + ".wav";
            } else {
                wavMsg_ = "export impossible (dossier Documents âº Raw 2)";
            }
            wavMsgT_ = now_;
        }
        if (it.released) wavDragged_ = false;
        ui_.box(br, it.held ? T.acc : T.btnBg, T.btnEdge);
        ui_.textCenter("\xE2\x86\x92 GLISSER EN .WAV", br, Font{Face::Mono, 10.f, 0.7f}, it.held ? T.onAcc : T.ink);
        if (now_ - wavMsgT_ < 4000.0)
            ui_.textRight(ui_.fit(wavMsg_, Font{Face::Mono, 9.f, 0.f}, 520.f), br.x - 10.f, so.y + 18.f, Font{Face::Mono, 9.f, 0.f}, T.hl);
    }
    const float x0 = so.x + 15.f, y0 = so.y + 57.2f;
    const Rect rail{x0, y0, 684.7f, 194.f};
    {
        const KbColors kc = kbColors(T);
        c.fillRoundRect(rail, 6.f, kc.railEdge);
        c.fillRoundRect(rail.inset(1.f), 5.f, kc.rail);
        c.fillRect(Rect{rail.x + 3.f, rail.y + 1.f, rail.w - 6.f, 2.f}, Color{0, 0, 0, 0.12f});
        c.fillRect(Rect{rail.x + 1.f, rail.y + 3.f, 2.f, rail.h - 6.f}, Color{0, 0, 0, 0.12f});
    }
    keyboard(Rect{rail.x + 11.f, rail.y + 11.f, 662.7f, 172.f});

    knobCol(kMaster, so.r() - 15.f - 38.f, y0 + 42.4f, 76.f, "MASTER", levelText(P(kMaster)), 0.f, false);
}

void Editor::keyboard(const Rect& area) {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const float kw = area.w / 14.f;
    const int WK[7] = {0, 2, 4, 5, 7, 9, 11};
    const int scale = m_.kbScale.load(), ko = m_.kbOct.load() * 12;
    const int heldHost = m_.readouts.heldNote.load();
    const int shown = mouseNote_ >= 0 ? mouseNote_ : (heldHost >= 0 ? heldHost - 48 : -1000);
    const int blackSlots[10] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
    Rect blackR[10], whiteR[14];
    const KbColors kc = kbColors(T);
    // clavier de la maquette mise à jour (zone de 172 px) : touches plus grandes
    const bool big = area.h > 150.f;
    const float bw = big ? 30.f : 24.f, bh = big ? 54.f : 44.f, wy = big ? 64.f : 54.f, wpad = big ? 3.f : 2.75f;
    for (int k = 0; k < 10; ++k) blackR[k] = Rect{area.x + (float)(blackSlots[k] + 1) * kw - bw * 0.5f, area.y, bw, bh};
    for (int w = 0; w < 14; ++w) whiteR[w] = Rect{area.x + (float)w * kw + wpad, area.y + wy, kw - 2.f * wpad, area.h - wy};
    int hoverNote = -1;
    const Pt m = ui_.in().mouse;
    for (int k = 0; k < 10 && hoverNote < 0; ++k)
        if (blackR[k].contains(m)) hoverNote = (blackSlots[k] / 7) * 12 + WK[blackSlots[k] % 7] + 1;
    for (int w = 0; w < 14 && hoverNote < 0; ++w)
        if (whiteR[w].contains(m)) hoverNote = (w / 7) * 12 + WK[w % 7];
    const Ui::Interact it = ui_.interact(makeId("kbd"), area, Cursor::Hand);
    if (it.held && hoverNote >= 0 && ui_.hit(area)) noteOn(quantize(scale, hoverNote) + ko);
    if ((it.released || (it.held && hoverNote < 0)) && mouseNote_ >= 0) noteOff(mouseNote_);

    const Font nf{Face::MonoBold, 11.f, 0.22f}, xf{Face::MonoBold, 9.f, 0.18f};
    static const char* NN[7] = {"C", "D", "E", "F", "G", "A", "B"};
    for (int w = 0; w < 14; ++w) {
        const int n = (w / 7) * 12 + WK[w % 7];
        const bool on = shown == quantize(scale, n) + ko, dim = !inScale(scale, n);
        Rect r = whiteR[w];
        if (on) r = r.moved(0.f, 2.f);
        const float a = dim ? 0.45f : 1.f;
        if (!on) {
            c.fillRoundRect(r.moved(0.f, 5.f).inset(-1.f, 0.f), 14.f, Color{0, 0, 0, 0.12f * a});
            c.fillRoundRect(r.moved(0.f, 4.f), 14.f, kc.natSkirt.withAlpha(a));
        } else {
            c.fillRoundRect(r.inset(-4.f), 18.f, T.acc.withAlpha(0.25f));
        }
        c.fillRoundRect(r, 14.f, (on ? T.acc : kc.nat).withAlpha(a));
        c.fillRect(Rect{r.x + 12.f, r.y, r.w - 24.f, 1.5f}, Color{1, 1, 1, a});
        const Color ink = (on ? T.onAcc : kc.natInk).withAlpha(a);
        ui_.textCenter(std::to_string(w + 1), Rect{r.x, r.y + (big ? 11.f : 10.f), r.w, 12.f}, xf, ink);
        const std::string mark = (w % 7 == 0) ? "C" + std::to_string(3 + w / 7 + m_.kbOct.load()) : NN[w % 7];
        ui_.textCenter(mark, Rect{r.x, r.b() - 22.f, r.w, 14.f}, nf, ink);
    }
    static const char* BK[5] = {"C#", "D#", "F#", "G#", "A#"};
    for (int k = 0; k < 10; ++k) {
        const int s = blackSlots[k];
        const int n = (s / 7) * 12 + WK[s % 7] + 1;
        const bool on = shown == quantize(scale, n) + ko, dim = !inScale(scale, n);
        Rect r = blackR[k];
        if (on) r = r.moved(0.f, 2.f);
        const float a = dim ? 0.45f : 1.f;
        if (!on) {
            c.fillRoundRect(r.moved(0.f, 4.f).inset(-1.f, 0.f), 10.f, Color{0, 0, 0, 0.4f * a});
            c.fillRoundRect(r.moved(0.f, 3.f), 10.f, kc.sharpSkirt.withAlpha(a));
        } else {
            c.fillRoundRect(r.inset(-4.f), 13.f, T.acc.withAlpha(0.3f));
        }
        c.fillRoundRect(r, 10.f, (on ? T.acc : kc.sharp).withAlpha(a));
        c.fillRect(Rect{r.x + 8.f, r.y, r.w - 16.f, 1.f}, kc.sharpHi.withAlpha(a));
        const Pt led{r.cx(), r.y + (big ? 18.5f : 13.5f)};
        c.fillEllipse(led, on ? 6.f : 4.5f, on ? 6.f : 4.5f, T.acc.withAlpha(0.35f * a));
        c.fillEllipse(led, 3.5f, 3.5f, (on ? Color{1, 1, 1, 1} : T.acc).withAlpha(a));
        c.strokeEllipse(led, 3.f, 3.f, Color{0, 0, 0, 0.4f * a}, 1.f);
        ui_.textCenter(BK[k % 5], Rect{r.x, r.y + (big ? 28.f : 23.f), r.w, 12.f}, xf, (on ? T.onAcc : Color{1, 1, 1, 1}).withAlpha(a));
    }
}

// ================================================================================================
// SON
// ================================================================================================
void Editor::drawSon() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    soundAdvisorFrame();
    oscCard(0, Rect{24.f, 73.f, 361.8f, 499.2f});
    oscCard(1, Rect{894.2f, 73.f, 361.8f, 499.2f});

    // --- VIE ---
    const Rect vc{395.8f, 73.f, 488.4f, 499.2f};
    ui_.card(vc);
    ui_.cardHeader(vc, "VIE", false, 14.f, 12.f);
    {
        LifeView v;
        m_.life.view(LifeEngine::Small, v);
        const std::string tag = std::string(P(kRule) > 0.5f ? "B36/S23" : "B3/S23") + fmt(" \xC2\xB7 GEN %04u \xC2\xB7 %03d \xE2\x97\x8F", v.gen % 10000, v.alive);
        ui_.tag(tag, vc.r() - 15.f, vc.y + 13.f, Ui::Tag::Dark);
    }
    const Rect well{410.8f, 132.f, 458.4f, 218.2f};
    ui_.box(well, T.lifeBg, T.lifeLine);
    lifeGrid(LifeEngine::Small, Rect{423.8f, 145.f, 432.4f, 192.2f}, 11.f, 4.2f, 3.7f, 3.7f, makeId("lifeS"), false);
    legend(410.8f, 369.2f);
    {
        static const char* tg[3] = {"FILTRE", "POSITION", "NIVEAU"};
        const std::string lbl = std::string("CIBLE : ") + tg[std::clamp(m_.params.geti(kTarget), 0, 2)] + " \xE2\x96\xBE";
        ButtonStyle s;
        s.bg = Color{0, 0, 0, 0}; s.fg = T.ink; s.border = T.ink; s.shadow = false; s.dashed = true;
        s.font = Font{Face::Mono, 11.f, 1.1f};
        const float bw = ui_.width(lbl, s.font) + 22.f;
        if (ui_.button(makeId("cible"), Rect{869.2f - bw, 362.2f, bw, 28.f}, lbl, s)) setParam(kTarget, (float)((m_.params.geti(kTarget) + 1) % 3));
    }
    c.fillRect(Rect{410.8f, 398.2f, 458.4f, 1.f}, T.rule);
    if (ui_.button(makeId("reset"), Rect{410.8f, 407.2f, 152.1f, 32.f}, "RESET", ui_.keyStyle())) m_.life.reset(LifeEngine::Small);
    {
        ButtonStyle s = ui_.keyStyle();
        s.pressed = P(kFreeze) > 0.5f;
        if (ui_.button(makeId("gel"), Rect{568.9f, 407.2f, 152.1f, 32.f}, "GEL", s)) setParam(kFreeze, P(kFreeze) > 0.5f ? 0.f : 1.f);
    }
    if (ui_.button(makeId("seed"), Rect{727.1f, 407.2f, 142.1f, 32.f}, "SEED \xE2\x96\xBE", ui_.keyStyle())) seedOpen_ = !seedOpen_;
    {
        const float hz = speedToHz(P(kSpeed));
        const std::string sp = m_.bpmDisplay ? fmt("%d BPM", (int)std::lround(hz * 60.f)) : fmt("%.1f Hz", hz);
        knobCol(kSpeed, 491.f, 445.2f, 52.f, "VITESSE", sp, 0.f, false);
        knobCol(kBirth, 646.5f, 445.2f, 52.f, "NAISSANCE", pct(P(kBirth)), 0.f, false);
        knobCol(kQty, 795.5f, 445.2f, 52.f, "QUANTIT\xC3\x89", pct(P(kQty)), 0.f, false);
        // interrupteur HZ | BPM
        const Rect sw{458.5f, 540.2f, 65.f, 19.f};
        ui_.box(sw, Color{0, 0, 0, 0}, T.rule);
        const Rect a{459.5f, 541.2f, 27.6f, 17.f}, b{487.1f, 541.2f, 35.4f, 17.f};
        const bool bpm = m_.bpmDisplay;
        if (ui_.interact(makeId("swHz"), a).clicked) m_.bpmDisplay = false;
        if (ui_.interact(makeId("swBpm"), b).clicked) m_.bpmDisplay = true;
        if (!bpm) c.fillRect(a, T.tabBg);
        else c.fillRect(Rect{b.x + 1.f, b.y, b.w - 1.f, b.h}, T.tabBg);
        c.fillRect(Rect{b.x, b.y, 1.f, b.h}, T.rule);
        const Font f{Face::Mono, 10.f, 0.8f};
        ui_.textCenter("HZ", a, f, bpm ? T.muted : T.tabFg);
        ui_.textCenter("BPM", Rect{b.x + 1.f, b.y, b.w - 1.f, b.h}, f, bpm ? T.tabFg : T.muted);
    }

    // --- modules ---
    {
        const Rect strip{24.f, 582.2f, 1232.f, 56.f};
        ui_.box(strip, T.card, T.rule);
        const float cw = 1230.f / 3.f;
        for (int i = 0; i < 3; ++i) {
            const Rect cell{25.f + (float)i * cw, 583.2f, cw, 54.f};
            if (i < 2) c.fillRect(Rect{cell.r() - 1.f, cell.y, 1.f, cell.h}, T.rule);
            depthModule(i, Rect{cell.x, cell.y, cell.w - (i < 2 ? 1.f : 0.f), cell.h});
        }
    }

    // --- SORTIE : clavier et molettes ---
    keyboardCard(Rect{24.f, 648.2f, 814.7f, 287.8f});

    // --- PROPOSITION ---
    proposition(Rect{848.7f, 648.2f, 407.3f, 287.8f});

    // --- menus déroulants (calque 3) ---
    ui_.setLayer(3);
    if (seedOpen_) {
        const Rect menu{877.2f - 0.f, 313.2f, 132.f, 126.f};
        c.fillRect(menu.moved(2.f, 2.f), T.btnEdge);
        ui_.box(menu, T.card, T.rule);
        static const char* items[3] = {"Glider", "Pulsar", "Gosper Gun"};
        static const Pattern pats[3] = {Pattern::Glider, Pattern::Pulsar, Pattern::Gun};
        ButtonStyle s = ui_.keyStyle();
        s.shadow = false;
        for (int k = 0; k < 3; ++k) {
            const Rect r{menu.x + 9.f, menu.y + 9.f + (float)k * 38.f, 114.f, 32.f};
            ui_.box(r, s.bg, s.border);
            if (ui_.interact(makeId("seedIt", k), r).clicked) {
                m_.life.seed(pats[k]);
                seedOpen_ = false;
            }
            ui_.text(items[k], r.x + 15.f, r.y + 8.f, s.font, T.ink);
        }
    }
    if (roleOpen_) {
        const Rect menu{1061.f, 707.2f, 172.f, 188.f};
        c.fillRect(menu.moved(2.f, 2.f), T.scrMuted);
        ui_.box(menu, T.scr, T.scrMuted);
        for (int k = 0; k < 6; ++k) {
            const Rect r{menu.x + 1.f, menu.y + 1.f + (float)k * 31.f, menu.w - 2.f, 30.f};
            const bool on = k == st_.role;
            if (on) c.fillRect(r, T.acc);
            c.fillRect(Rect{r.x, r.b(), r.w, 1.f}, T.scrRule);
            if (ui_.interact(makeId("roleIt", k), r).clicked) {
                if (k != st_.role) forceSound_ = true;
                st_.role = k;
                st_.rec = 0;
                st_.typeT = now_;
                st_.status.clear();
                st_.statusOn = false;
                roleOpen_ = false;
            }
            ui_.text(roleLabel(k), r.x + 12.f, r.y + 8.f, Font{Face::Mono, 11.f, 0.66f}, on ? T.onAcc : T.scrText);
        }
    }
    ui_.setLayer(0);
}

// ================================================================================================
// VIE
// ================================================================================================
void Editor::drawVie() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const Rect card{24.f, 73.f, 880.f, 863.f};
    ui_.card(card);
    ui_.cardHeader(card, "AUTOMATE", false, 14.f, 12.f);
    ui_.tag(std::string(P(kRule) > 0.5f ? "B36/S23" : "B3/S23") + " \xC2\xB7 64\xC3\x97" "58", card.r() - 15.f, 86.f, Ui::Tag::Dark);
    const Rect well{39.f, 132.f, 850.f, 765.f};
    ui_.box(well, T.lifeBg, T.lifeLine);
    c.save();
    c.clip(well.inset(1.f));
    lifeGrid(LifeEngine::Big, Rect{52.f, 141.1f, 824.f, 746.8f}, 12.f, 4.8f, 4.3f, 4.3f, makeId("lifeB"), true);
    c.restore();
    legend(39.f, 909.f);
    {
        static const char* modes[3] = {"glisser pour dessiner", "glisser pour effacer", ""};
        std::string hint = st_.brushMode == 2 ? std::string("cliquer pour poser \xC2\xAB ") + patternInfo(st_.brushPat).name + " \xC2\xBB" : std::string(modes[st_.brushMode]);
        hint += " \xC2\xB7 taille " + std::to_string(st_.brushSize);
        const Font hf{Face::Mono, 11.f, 0.66f};
        LifeView v;
        m_.life.view(LifeEngine::Big, v);
        const std::string stats = fmt("GEN %04u \xC2\xB7 %d VIVANTES \xC2\xB7 %d %%", v.gen % 10000, v.alive, (int)std::lround((float)v.alive / (float)(64 * 58) * 100.f));
        const Font sf{Face::Mono, 11.f, 0.88f};
        const float sw = ui_.width(stats, sf);
        ui_.text(stats, 889.f - sw, 909.f, sf, T.ink);
        // l'indice est centré entre la légende et les statistiques (justify-content: space-between)
        const float hw = ui_.width(hint, hf);
        const float lx = 246.8f, rx = 889.f - sw;
        ui_.text(hint, lx + (rx - lx - hw) * 0.5f, 909.f, hf, T.muted);
    }

    // --- CONTRÔLES ---
    const Rect pc{916.f, 73.f, 340.f, 863.f};
    ui_.card(pc);
    ui_.cardHeader(pc, "CONTR\xC3\x94LES", false, 14.f, 12.f);
    ui_.tag("brosses \xC2\xB7 motifs", pc.r() - 15.f, 86.f, Ui::Tag::Line);
    auto section = [&](const char* label, float y) {
        c.fillRect(Rect{931.f, y, 310.f, 1.f}, T.rule);
        ui_.text(label, 931.f, y + 11.f, Font{Face::Mono, 10.f, 1.4f}, T.muted);
    };
    const Font small{Face::Mono, 10.f, 1.2f};
    section("PINCEAU", 130.f);
    {
        static const char* m[3] = {"DESSINER", "GOMME", "MOTIF"};
        for (int k = 0; k < 3; ++k)
            if (ui_.button(makeId("brush", k), Rect{931.f + (float)k * 106.f, 160.f, 98.f, 30.f}, m[k], ui_.miniStyle(st_.brushMode == k))) st_.brushMode = k;
        ui_.text("TAILLE", 931.f, 206.5f, small, T.muted);
        for (int k = 0; k < 3; ++k)
            if (ui_.button(makeId("bsize", k), Rect{986.2f + (float)k * 49.5f, 198.f, 41.5f, 30.f}, std::to_string(k + 1), ui_.miniStyle(st_.brushSize == k + 1))) st_.brushSize = k + 1;
    }
    section("MOTIFS", 240.f);
    for (int k = 0; k < kNumPatterns; ++k) {
        const PatternInfo& p = patternInfo((Pattern)k);
        const bool on = st_.brushMode == 2 && (int)st_.brushPat == k;
        const Rect r{931.f, 270.f + (float)k * 38.f, 310.f, 34.f};
        if (ui_.interact(makeId("pat", k), r).clicked) {
            st_.brushPat = (Pattern)k;
            st_.brushMode = 2;
        }
        ui_.box(r, on ? T.tabBg : T.btnBg, T.btnEdge);
        const Color fg = on ? T.tabFg : T.ink;
        // aperçu du motif (svg 52 x 24, proportions conservées)
        const float s = std::min(52.f / (float)p.w, 24.f / (float)p.h);
        const float ox = 942.f + (52.f - (float)p.w * s) * 0.5f, oy = r.y + 5.f + (24.f - (float)p.h * s) * 0.5f;
        std::vector<Pt> pts;
        for (auto& cc : p.cells) pts.push_back(Pt{ox + ((float)cc.first + 0.5f) * s, oy + ((float)cc.second + 0.5f) * s});
        c.fillCircles(pts.data(), (int)pts.size(), 0.42f * s, fg);
        ui_.text(p.name, 1004.f, r.y + 9.f, Font{Face::Mono, 12.f, 0.f}, fg);
        ui_.textRight(fmt("%d\xC3\x97%d", p.w, p.h), 1230.f, r.y + 10.5f, Font{Face::Mono, 10.f, 0.f}, fg.withAlpha(0.7f));
    }
    section("SNAPSHOTS \xC2\xB7 TIMELAPSE", 472.f);
    {
        LifeEngine::SnapState ss;
        m_.life.snapState(ss);
        for (int k = 0; k < 8; ++k) {
            const Rect r{931.f + (float)k * 39.25f, 502.f, 35.3f, 32.f};
            if (ui_.interact(makeId("snap", k), r).clicked) m_.life.snapRecall(k);
            c.fillRect(r, T.scr);
            if (ss.pending == k) ui_.dashedBorder(r, T.acc);
            else if (ss.selected == k) ui_.border(r, T.acc, 2.f);
            else ui_.border(r, T.rule, 1.f);
            if (ss.filled[(size_t)k]) {
                const auto& g = ss.thumbs[(size_t)k];
                const float sx = 29.3f / 64.f, sy = 26.f / 58.f;
                for (size_t i = 0; i < g.size(); ++i)
                    if (g[i]) c.fillRect(Rect{r.x + 3.f + (float)(i % 64) * sx, r.y + 3.f + (float)(i / 64) * sy, sx, sy}, T.acc);
            }
            ui_.text(std::to_string(k + 1), r.x + 4.f, r.y + 2.f, Font{Face::Mono, 8.f, 0.f}, ss.filled[(size_t)k] ? T.scrText : T.scrMuted);
        }
        const float ws[4] = {85.3f, 68.9f, 68.9f, 68.9f};
        const char* labels[4] = {"\xE2\x97\x8F CAPT.", "SYNC", "AUTO", "VIDER"};
        const bool ons[4] = {false, ss.sync, ss.autoCapture, false};
        float x = 931.f;
        for (int k = 0; k < 4; ++k) {
            if (ui_.button(makeId("snapAct", k), Rect{x, 542.f, ws[k], 28.f}, labels[k], ui_.miniStyle(ons[k]))) {
                if (k == 0) m_.life.snapCapture();
                else if (k == 1) m_.life.setSnapSync(!ss.sync);
                else if (k == 2) m_.life.setSnapAuto(!ss.autoCapture);
                else m_.life.snapClearSelected();
            }
            x += ws[k] + 6.f;
        }
    }
    section("R\xC3\x89GLAGES", 582.f);
    {
        const Font lf{Face::Mono, 11.f, 1.1f};
        auto slider = [&](const char* label, uint32_t id, const std::string& val, float y) {
            ui_.text(label, 931.f, y, lf, T.muted);
            ui_.textRight(val, 1241.f + 1.1f, y, lf, T.ink);
            float v = toNormalized(id, P(id));
            bool b = false, e = false;
            if (ui_.hslider(makeId("vs", (int)id), Rect{931.f, y + 19.f, 310.f, 18.f}, v, &b, &e)) {
                if (dragParam_ != (int)id) beginDrag(id);
                dragParam(id, fromNormalized(id, v));
            } else if (b) {
                beginDrag(id);
                dragParam(id, fromNormalized(id, v));
            }
            if (e && dragParam_ == (int)id) endDrag(id);
        };
        slider("NAISSANCE", kBirth, pct(P(kBirth)), 612.f);
        slider("VITESSE", kSpeed, fmt("%.1f Hz", speedToHz(P(kSpeed))), 659.f);
        slider("LISSAGE MODULATION", kSmooth, formatParam(kSmooth, P(kSmooth)), 706.f);
        ui_.text("R\xC3\x88GLE", 931.f, 761.5f, small, T.muted);
        const bool hl = P(kRule) > 0.5f;
        if (ui_.button(makeId("rule", 0), Rect{979.f, 753.f, 127.f, 30.f}, "B3/S23", ui_.miniStyle(!hl))) setParam(kRule, 0.f);
        if (ui_.button(makeId("rule", 1), Rect{1114.f, 753.f, 127.f, 30.f}, "B36/S23", ui_.miniStyle(hl))) setParam(kRule, 1.f);
    }
    {
        const float ws[4] = {59.4f, 44.5f, 74.4f, 81.8f};
        const char* labels[4] = {"RESET", "GEL", "EFFACER", "INJECTER"};
        float x = 931.f;
        for (int k = 0; k < 4; ++k) {
            const bool on = k == 1 && P(kFreeze) > 0.5f;
            if (ui_.button(makeId("vieAct", k), Rect{x, 893.f, ws[k], 30.f}, labels[k], ui_.miniStyle(on))) {
                if (k == 0) m_.life.reset(LifeEngine::Big);
                else if (k == 1) setParam(kFreeze, P(kFreeze) > 0.5f ? 0.f : 1.f);
                else if (k == 2) m_.life.clear(LifeEngine::Big);
                else m_.life.stamp(LifeEngine::Big, st_.brushPat, 32, 29);
            }
            x += ws[k] + 8.f;
        }
    }
}

// ================================================================================================
// MODULES
// ================================================================================================
void Editor::drawMod() {
    const Theme& T = *T_;
    // [ SIDE A ] matrice globale, [ SIDE B ] rack modulaire
    {
        const char* names[2] = {"[ SIDE A ]", "[ SIDE B ]"};
        for (int k = 0; k < 2; ++k) {
            ButtonStyle st = ui_.miniStyle(st_.modSide == k);
            st.font = Font{Face::Mono, 11.f, 1.1f};
            if (ui_.button(makeId("modSide", k), Rect{24.f + (float)k * 96.8f, 73.f, 96.8f, 32.f}, names[k], st)) st_.modSide = k;
        }
        int nk = 0;
        for (int k = 0; k < 48; ++k)
            if (m_.matrix.on[(size_t)k]) ++nk;
        int nr = 0;
        for (int r = 0; r < kNumRackLfos; ++r)
            if (P(lfoP(2 + r, kLfOn)) > 0.5f) ++nr;
        const std::string tg = st_.modSide == 0 ? std::string("matrice globale \xC2\xB7 VIE / LFO / ENV \xE2\x86\x92 destinations")
                                                : fmt("rack modulaire \xC2\xB7 %d LFO \xC2\xB7 %d route(s) libre(s)", nr, m_.routes.count());
        const float w = ui_.width(upper(tg), Font{Face::Mono, 10.f, 1.f}) + 18.f;
        ui_.tag(tg, 231.6f + w, 78.5f, Ui::Tag::Line);
        (void)nk;
        (void)T;
    }
    if (st_.modSide == 1) drawRack();
    else drawModMatrix();
}

void Editor::drawModMatrix() {
    Canvas& c = ui_.c();
    const Theme& T = *T_;
    const Rect card{24.f, 115.f, 880.f, 821.f};
    ui_.card(card);
    ui_.cardHeader(card, "MATRICE DE ROUTAGE", false, 14.f, 12.f);
    ui_.tag("6 sources \xC3\x97 8 destinations", card.r() - 15.f, 128.f, Ui::Tag::Line);

    const float gx = 40.f, gy = 175.f, colW = (889.f - 40.f - 150.f) / 8.f, hdrH = 104.f, footH = 44.f;
    const float rowH = (892.f - gy - hdrH - footH) / 6.f;
    const Rect box{39.f, 174.f, 850.f, 719.f};
    c.fillRect(Rect{box.x, box.y, box.w, 1.f}, T.rule);
    c.fillRect(Rect{box.x, box.b() - 1.f, box.w, 1.f}, T.rule);
    c.fillRect(Rect{box.x, box.y, 1.f, box.h}, T.rule);

    auto linked = [&](int s, int d) { return m_.matrix.on[(size_t)ModMatrix::idx(s, d)].load(); };
    auto amount = [&](int s, int d) { return m_.matrix.amt[(size_t)ModMatrix::idx(s, d)].load(); };
    bool hasCol[8] = {}, hasRow[6] = {};
    for (int s = 0; s < 6; ++s)
        for (int d = 0; d < 8; ++d)
            if (linked(s, d)) hasCol[d] = hasRow[s] = true;

    // survol (ligne / colonne) déterminé avant le dessin
    const Pt m = ui_.in().mouse;
    hovR_ = hovC_ = -2;
    if (ui_.hit(Rect{gx, gy, 849.f, hdrH + 6.f * rowH})) {
        const int col = m.x < gx + 150.f ? -1 : std::min(7, (int)((m.x - gx - 150.f) / colW));
        const int row = m.y < gy + hdrH ? -1 : std::min(5, (int)((m.y - gy - hdrH) / rowH));
        if (!(col == -1 && row == -1)) { hovR_ = row; hovC_ = col; }
    }
    const Color tint = T.railBg;
    auto cellRect = [&](int r, int col) {
        const float x = col < 0 ? gx : gx + 150.f + (float)col * colW;
        const float y = r < 0 ? gy : gy + hdrH + (float)r * rowH;
        return Rect{x, y, col < 0 ? 150.f : colW, r < 0 ? hdrH : rowH};
    };
    auto seg = [&](float v, float wpx, float left, float bottom) {
        const float w = std::round(std::fabs(v) * wpx / 6.f) * 6.f;
        const float x0 = left + std::min(0.f, v) * wpx;
        for (float x = 0; x < w; x += 6.f) c.fillRect(Rect{x0 + x, bottom - 11.f, std::min(4.f, w - x), 4.f}, T.ptr);
    };

    // en-tête
    {
        const Rect r = cellRect(-1, -1);
        ui_.text("SOURCES \xE2\x86\x93", r.x + 12.f, r.b() - 40.f, Font{Face::Mono, 10.f, 1.4f}, T.muted);
        ui_.text("DESTINATIONS \xE2\x86\x92", r.x + 12.f, r.b() - 24.f, Font{Face::Mono, 10.f, 1.4f}, T.muted);
    }
    for (int d = 0; d < 8; ++d) {
        const Rect r = cellRect(-1, d);
        if (hovC_ == d) c.fillRect(r, tint);
        if (hasCol[d]) c.fillRect(Rect{r.x, r.y, r.w, 3.f}, T.acc);
        const Font f{Face::Mono, 11.f, 1.54f};
        const float tw = ui_.width(kDst[d], f);
        c.save();
        c.translate(r.cx(), r.b() - 12.f);
        c.rotate(-kPi / 2.f);
        c.text(kDst[d], 0.f, baselineIn(f, -normalLineHeight(f) / 2.f), f, hasCol[d] || hovC_ == d ? T.ink : T.muted);
        c.restore();
        (void)tw;
    }
    // lignes
    const int sel = st_.mxSel;
    for (int s = 0; s < 6; ++s) {
        const Rect r = cellRect(s, -1);
        if (hovR_ == s) c.fillRect(r, tint);
        if (hasRow[s]) c.fillRect(Rect{r.x, r.y, 3.f, r.h}, T.acc);
        const float cy = r.y + (r.h - 6.f - 33.f) / 2.f;
        ui_.text(kSrc[s], r.x + 12.f, cy, Font{Face::MonoBold, 13.f, 1.56f}, hasRow[s] || hovR_ == s ? T.ink : T.muted);
        std::string sub = kSrcSub[s];
        if (s == 1 || s == 2) {  // LFO : forme et vitesse réglées sur la page ENV / LFO
            static const char* shp[kNumLfoShapes] = {"sinus", "triangle", "dent de scie", "carr\xC3\xA9", "al\xC3\xA9" "atoire"};
            const int n = s - 1, sync = (int)P(lfoParam(n, 5));
            sub = std::string(shp[std::clamp((int)P(lfoParam(n, 4)), 0, kNumLfoShapes - 1)]) + " " +
                  (sync > 0 ? std::string(lfoSyncName(sync)) : fmt("%.2f Hz", lfoRateHz(P(lfoParam(n, 0)))));
        } else if (s == 3) {
            sub = "enveloppe A";
        }
        ui_.text(sub, r.x + 12.f, cy + 20.f, Font{Face::Mono, 10.f, 0.6f}, T.muted);
        {  // niveau de la source : 12 cases de 4 px
            const float v = m_.readouts.src[(size_t)s].load();
            const float lvl = s == 3 ? std::min(1.f, v / 0.6f) : s == 4 ? v : (v + 1.f) * 0.5f;
            const int nOn = std::clamp((int)std::lround(lvl * 12.f), 0, 12);
            for (int q = 0; q < 12; ++q) c.fillRect(Rect{r.x + 12.f + (float)q * 6.f, r.b() - 11.f, 4.f, 4.f}, q < nOn ? T.ptr : T.tickOff);
        }
        for (int d = 0; d < 8; ++d) {
            const Rect cr = cellRect(s, d);
            const bool cross = hovR_ == s || hovC_ == d;
            if (cross) c.fillRect(cr, tint);
            const int key = ModMatrix::idx(s, d);
            if (ui_.interact(makeId("pin", key), cr).clicked) {
                // clic : relier, re-clic : sélectionner puis couper
                if (!linked(s, d)) {
                    m_.matrix.amt[(size_t)key] = 50;
                    m_.matrix.on[(size_t)key] = true;
                    st_.mxSel = key;
                } else if (st_.mxSel != key) {
                    st_.mxSel = key;
                } else {
                    m_.matrix.on[(size_t)key] = false;
                    st_.mxSel = -1;
                    for (int k = 0; k < 48; ++k)
                        if (m_.matrix.on[(size_t)k]) { st_.mxSel = k; break; }
                }
            }
            const Pt ctr{cr.cx(), cr.cy()};
            if (linked(s, d)) {
                const int a = amount(s, d);
                const float sz = 18.f + std::round((float)std::abs(a) / 100.f * 16.f);
                if (a >= 0) {
                    c.fillEllipse(ctr, sz / 2, sz / 2, T.ink);
                    c.fillEllipse(ctr, sz / 2 - 1.f, sz / 2 - 1.f, T.acc);
                } else {
                    c.strokeEllipse(ctr, sz / 2 - 1.5f, sz / 2 - 1.5f, T.acc, 3.f);
                }
                c.fillEllipse(ctr, 2.3f, 2.3f, T.ink);
                seg(m_.readouts.src[(size_t)s].load() * (float)a / 100.f, 30.f, cr.cx(), cr.b());
            } else {
                c.strokeEllipse(ctr, 7.5f, 7.5f, T.muted, 1.f);
                c.fillEllipse(ctr, 2.3f, 2.3f, T.ink);
            }
            if (sel == key) ui_.border(cr.inset(0.f), T.ink, 2.f);
        }
    }
    // ligne SORTIE
    {
        const Rect r = cellRect(6, -1);
        const Rect fr{r.x, r.y, 150.f, footH};
        c.fillRect(fr, T.railBg);
        ui_.text("SORTIE", fr.x + 12.f, fr.y + 15.5f, Font{Face::Mono, 10.f, 1.4f}, T.muted);
        for (int d = 0; d < 8; ++d) {
            const Rect cr{gx + 150.f + (float)d * colW, r.y, colW, footH};
            c.fillRect(cr, T.railBg);
            const float v = m_.readouts.dst[(size_t)d].load();
            ui_.textCenter(signed2(v), Rect{cr.x, cr.y + 15.f, cr.w, 14.f}, Font{Face::MonoBold, 11.f, 0.44f}, std::fabs(v) > 0.005f ? T.ink : T.muted);
            c.fillRect(Rect{cr.x + 10.f, cr.b() - 10.f, cr.w - 21.f, 3.f}, T.tickOff);
        }
    }
    // filets de la grille
    for (int d = 0; d <= 8; ++d) {
        const float x = gx + 150.f + (float)d * colW - 1.f;
        c.fillRect(Rect{x, gy, 1.f, hdrH + 6.f * rowH + footH}, T.rule);
    }
    c.fillRect(Rect{gx + 150.f - 1.f, gy, 1.f, hdrH + 6.f * rowH + footH}, T.rule);
    for (int r = 0; r <= 6; ++r) {
        const float y = gy + hdrH + (float)r * rowH - 1.f;
        c.fillRect(Rect{gx, y, 849.f, 1.f}, T.rule);
    }
    // légende
    {
        const Font f{Face::Mono, 11.f, 0.66f}, big{Face::Mono, 14.f, 0.66f};
        float x = 39.f;
        x += ui_.text("\xE2\x97\x8F", x, 903.f, big, T.hl);
        x += ui_.text(" positif", x, 907.f, f, T.muted) + 18.f;
        x += ui_.text("\xE2\x97\x8B", x, 903.f, big, T.hl);
        x += ui_.text(" invers\xC3\xA9", x, 907.f, f, T.muted) + 18.f;
        ui_.text("taille = dosage", x, 907.f, f, T.muted);
        ui_.textRight("clic : relier \xC2\xB7 re-clic : couper", 889.f + 0.66f, 907.f, f, T.muted);
    }

    // --- LIAISON ---
    const Rect lc{916.f, 115.f, 340.f, 191.f};
    ui_.card(lc);
    ui_.cardHeader(lc, "LIAISON", false, 14.f, 12.f);
    ui_.tag("dosage", lc.r() - 15.f, 128.f, Ui::Tag::Line);
    const bool hasSel = st_.mxSel >= 0 && m_.matrix.on[(size_t)st_.mxSel].load();
    const int selAmt = hasSel ? m_.matrix.amt[(size_t)st_.mxSel].load() : 0;
    ui_.text(hasSel ? std::string(kSrc[st_.mxSel / 8]) + "  \xE2\x86\x92  " + kDst[st_.mxSel % 8] : std::string("aucune liaison s\xC3\xA9lectionn\xC3\xA9" "e"), 931.f, 176.f,
             Font{Face::MonoBold, 15.f, 0.9f}, T.ink);
    const Font lf{Face::Mono, 11.f, 1.1f};
    ui_.text("DOSAGE", 931.f, 210.f, lf, T.muted);
    ui_.textRight(hasSel ? signedPct((float)selAmt) : std::string("\xE2\x80\x93"), 1241.f + 1.1f, 210.f, lf, T.ink);
    {
        const Rect tr{931.f, 229.f, 310.f, 18.f};
        const Ui::Interact it = ui_.interact(makeId("mxSl"), tr, Cursor::ResizeEW);
        if (it.held && hasSel) m_.matrix.amt[(size_t)st_.mxSel] = std::clamp((int)std::lround((ui_.in().mouse.x - tr.x) / tr.w * 200.f - 100.f), -100, 100);
        ui_.box(tr, T.railBg, T.rule);
        const float inner = tr.w - 2.f, mid = tr.x + 1.f + inner / 2.f;
        const float fw = std::fabs((float)selAmt) / 200.f * inner;
        c.fillRect(Rect{selAmt >= 0 ? mid : mid - fw, tr.y + 1.f, fw, 16.f}, T.acc);
        c.fillRect(Rect{mid, tr.y - 2.f, 1.f, 22.f}, T.ink);
    }
    {
        const char* labels[3] = {"COUPER", "TOUT EFFACER", "AL\xC3\x89" "ATOIRE"};
        float x = 931.f;
        for (int k = 0; k < 3; ++k) {
            const float w = ui_.width(labels[k], Font{Face::Mono, 11.f, 0.88f}) + 22.f;
            if (ui_.button(makeId("mxAct", k), Rect{x, 263.f, w, 30.f}, labels[k], ui_.miniStyle(false))) {
                if (k == 0 && hasSel) {
                    m_.matrix.on[(size_t)st_.mxSel] = false;
                    st_.mxSel = -1;
                    for (int q = 0; q < 48; ++q)
                        if (m_.matrix.on[(size_t)q]) { st_.mxSel = q; break; }
                } else if (k == 1) {
                    m_.matrix.clear();
                    st_.mxSel = -1;
                } else if (k == 2) {
                    m_.matrix.clear();
                    st_.mxSel = -1;
                    for (int q = 0; q < 4; ++q) {
                        const int key = (int)(rnd() * 6.f) % 6 * 8 + (int)(rnd() * 8.f) % 8;
                        m_.matrix.amt[(size_t)key] = (int)std::lround((rnd() * 1.6f - 0.8f) * 100.f);
                        m_.matrix.on[(size_t)key] = true;
                        if (st_.mxSel < 0) st_.mxSel = key;
                    }
                }
            }
            x += w + 8.f;
        }
    }

    // --- PATCHS ACTIFS ---
    std::vector<int> keys;
    for (int k = 0; k < 48; ++k)
        if (m_.matrix.on[(size_t)k]) keys.push_back(k);
    const int nRoutes = m_.routes.count();
    const int nItems = (int)keys.size() + nRoutes;
    const int visible = std::max(3, std::min(6, nItems));
    const float listH = std::max(98.f, (float)visible * 34.f - 4.f);
    const Rect pc{916.f, 318.f, 340.f, 74.f + listH};
    ui_.card(pc);
    ui_.cardHeader(pc, "PATCHS ACTIFS", false, 14.f, 12.f);
    ui_.tag(fmt("%d / 48", nItems), pc.r() - 15.f, 331.f, Ui::Tag::Line);
    {
        static float scroll = 0.f;
        const Rect area{931.f, 375.f, 310.f, listH};
        const float maxS = std::max(0.f, (float)nItems * 34.f - 4.f - listH);
        if (ui_.hit(area) && ui_.in().wheel != 0.f) scroll -= ui_.in().wheel * 34.f;
        scroll = std::clamp(scroll, 0.f, maxS);
        c.save();
        c.clip(Rect{area.x, area.y, area.w + 4.f, area.h});
        for (size_t i = 0; i < keys.size(); ++i) {
            const int k = keys[i];
            const Rect r{931.f, 375.f + (float)i * 34.f - scroll, 310.f, 30.f};
            if (r.b() < area.y || r.y > area.b()) continue;
            const bool on = st_.mxSel == k;
            if (ui_.interact(makeId("patch", k), r).clicked) st_.mxSel = k;
            ui_.box(r, on ? T.tabBg : Color{0, 0, 0, 0}, on ? T.ink : T.rule);
            const Color fg = on ? T.tabFg : T.ink;
            ui_.text(std::string(kSrc[k / 8]) + " \xE2\x86\x92 " + kDst[k % 8], r.x + 11.f, r.y + 8.f, Font{Face::Mono, 11.f, 0.f}, fg);
            ui_.textRight(signedPct((float)m_.matrix.amt[(size_t)k].load()), r.r() - 11.f, r.y + 8.f, Font{Face::MonoBold, 11.f, 0.f}, fg);
        }
        // routes libres (glissées depuis [✛ MOD]) : cadre orange en pointillés, clic : couper
        int cut = -1;
        for (int i = 0; i < nRoutes; ++i) {
            const Rect r{931.f, 375.f + (float)(keys.size() + (size_t)i) * 34.f - scroll, 310.f, 30.f};
            if (r.b() < area.y || r.y > area.b()) continue;
            if (ui_.interact(makeId("route", i), r).clicked) cut = i;
            const Pt q[5] = {{r.x + 0.5f, r.y + 0.5f}, {r.r() - 0.5f, r.y + 0.5f}, {r.r() - 0.5f, r.b() - 0.5f}, {r.x + 0.5f, r.b() - 0.5f}, {r.x + 0.5f, r.y + 0.5f}};
            c.dashedPolyline(q, 5, Color::rgb8(255, 122, 26), 1.f, 3.f, 3.f);
            const int src = m_.routes.r[(size_t)i].src.load(), amt = m_.routes.r[(size_t)i].amt.load();
            const Font rf{Face::Mono, 11.f, 0.f};
            const std::string amtTxt = signedPct((float)amt) + "  \xE2\x9C\x95";
            const float aw = ui_.width(amtTxt, Font{Face::MonoBold, 11.f, 0.f});
            ui_.text(ui_.fit(std::string(routeSrcName(src)) + " \xE2\x87\xA2 " + upper(paramDef((uint32_t)m_.routes.r[(size_t)i].id.load()).name), rf, r.w - 30.f - aw),
                     r.x + 11.f, r.y + 8.f, rf, T.ink);
            ui_.textRight(amtTxt, r.r() - 11.f, r.y + 8.f, Font{Face::MonoBold, 11.f, 0.f}, T.ink);
        }
        if (cut >= 0) m_.routes.remove(cut);
        c.restore();
        if (nItems == 0) ui_.text("aucune liaison, clique sur la matrice", 931.f, 383.f, Font{Face::Mono, 11.f, 0.f}, T.muted);
    }

    // --- SORTIE live ---
    const Rect sc{916.f, pc.b() + 12.f, 340.f, 936.f - pc.b() - 12.f};
    ui_.cardTerm(sc);
    ui_.cardHeader(sc, "SORTIE", true, 22.f, 20.f);
    ui_.tag("live", sc.r() - 23.f, sc.y + 21.f, Ui::Tag::Accent);
    {
        const float scrH = std::max(60.f, sc.h - 323.f);
        const Rect scr{939.f, sc.y + 67.f, 294.f, scrH};
        ui_.screen(scr);
        const Rect area{948.f, scr.y + 9.f, 276.f, scrH - 23.f};
        c.save();
        c.clip(scr.inset(1.f));
        c.line(Pt{area.x, area.cy()}, Pt{area.r(), area.cy()}, T.scrMid, 1.f);
        const auto& L = an_.scopeL();
        std::vector<Pt> pts;
        const int n = (int)L.size();
        for (int i = 0; i <= 300 && n > 0; ++i) {
            const float v = std::clamp(L[(size_t)std::min(n - 1, i * n / 301)] * 2.f, -1.15f, 1.15f);
            pts.push_back(Pt{area.x + (float)i / 300.f * area.w, area.y + (40.f - v * 30.4f) / 80.f * area.h});
        }
        c.polyline(pts.data(), (int)pts.size(), T.waveA, 1.5f);
        c.restore();
        const float vy = scr.b() + 14.f;
        for (int d = 0; d < 8; ++d) {
            const float x = 939.f + (float)(d % 2) * 156.f, y = vy + (float)(d / 2) * 25.f;
            const float v = m_.readouts.dst[(size_t)d].load();
            ui_.text(kDst[d], x, y, Font{Face::Mono, 11.f, 0.f}, T.scrMuted);
            ui_.textRight(signed2(v), x + 138.f, y, Font{Face::MonoBold, 11.f, 0.f}, std::fabs(v) > 0.005f ? T.hl : T.scrMuted);
            c.fillRect(Rect{x, y + 18.f, 138.f, 1.f}, T.scrRule);
        }
        const float fy = sc.b() - 37.f;
        if (std::fmod(now_ / 1000.0, 1.0) < 0.5) ui_.text("\xE2\x96\x88", 939.f, fy, Font{Face::Mono, 11.f, 0.f}, T.hl);
        ui_.text(" le patch agit sur le son en direct", 939.f + 6.6f, fy, Font{Face::Mono, 11.f, 0.f}, T.scrMuted);
    }
}

} // namespace raw2::ui
