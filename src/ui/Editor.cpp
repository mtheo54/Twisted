#include "Editor.h"

#include "Gl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

namespace {
// Palette from the prototype: lacquer and gold, single dark look.
const Color kSumi = hexColor(0x0d0a11), kLacquer = hexColor(0x16111a), kPanel = hexColor(0x201923);
const Color kLine = hexColor(0x3a2e3c), kWashi = hexColor(0xebe2d2), kMuted = hexColor(0x9b909d);
const Color kKin = hexColor(0xd6b062), kShu = hexColor(0xc0472f), kRei = hexColor(0x8ad6df), kOk = hexColor(0x7fbf7a);

constexpr float kHeaderH = 52, kOptionsH = 84, kStatusH = 26;

const char* const kClassNamesFr[kNumClasses] = {"Kick / 808", "Basse", "Voix", "Bus batterie", "Guitare / synthé", "Master"};
const char* const kOsLabels[5] = {"1×", "2×", "4×", "8×", "16×"};
const char* const kCharFr[4] = {"Bande magnétique", "Lampe triode", "Transistor", "Wavefolder"};
const char* const kBirdsText[5] = {"ni oiseaux ni dragon", "6 corbeaux le jour, dragon jeune la nuit",
                                   "14 corbeaux le jour, dragon adulte la nuit", "26 corbeaux le jour, dragon ancien la nuit",
                                   "40 corbeaux le jour, dragon gigantesque la nuit"};

const char* timeOfDay(float m) {
    return m < 25 ? "jour" : m < 45 ? "après-midi" : m < 62 ? "crépuscule" : m < 80 ? "soir" : "nuit";
}

std::string valueText(uint32_t id, float v) {
    char b[64];
    if (id == kInput || id == kOutput) std::snprintf(b, sizeof b, "%s%.1f dB", v > 0.05f ? "+" : (v < -0.05f ? "−" : ""), std::fabs(v));
    else if (id == kMix) std::snprintf(b, sizeof b, "%.0f %% · %s", v, timeOfDay(v));
    else std::snprintf(b, sizeof b, "%.0f %%", v);
    return b;
}
} // namespace

Editor::Editor(PluginCore& core, ParamHost& host) : core_(core), host_(host) {
    layout();
    seenRecognition_ = core_.analyzer().recognitionCount();
}

Editor::~Editor() = default;

void Editor::layout() {
    stage_ = {0, kHeaderH, (float)kWidth, kHeight - kHeaderH - kOptionsH - kStatusH};
    const float bottom = stage_.y + stage_.h;
    presetBox_ = {kWidth - 470.f, 11, 200, 30};
    analyseBtn_ = {kWidth - 256.f, 11, 116, 30};
    bypassBtn_ = {kWidth - 126.f, 11, 108, 30};

    struct K { uint32_t id; const char* label; const char* fx; int group; };
    const K ks[] = {
        {kInput, "ENTRÉE", "L'antenne de la tour s'allonge ou se rétracte", 0},
        {kElevate, "ELEVATE", "Saturation + enhance : braises, acier qui rougeoie, étoiles et fenêtres qui scintillent", 1},
        {kComp, "COMPRESSION", "Les immeubles pompent au passage de chaque onde", 2},
        {kPunch, "PUNCH", "La caméra tourne plus fort autour de la tour à chaque basse", 2},
        {kSub, "SUB", "La tour et l'image tremblent sur les basses", 2},
        {kMix, "DRY / WET", "Fait passer la ville du jour (dry) à la nuit (wet)", 3},
        {kSpace, "SPACE", "Stéréo + ambiance : la caméra recule, les ondes s'élargissent, brouillard et échos", 4},
        {kClip, "CLIPPER", "Un plafond lumineux descend et écrase les toits qui dépassent", 5},
        {kLimit, "LIMITER", "Une bulle entoure l'antenne : les ondes s'y arrêtent et la font briller", 5},
        {kOutput, "SORTIE", "Règle l'exposition de l'image", 6},
    };
    // spread across the width, with extra space between groups
    const float small = 21, big = 34, step = 84, groupGap = 30;
    float total = 0;
    int prevGroup = -1;
    for (const auto& k : ks) { if (prevGroup >= 0 && k.group != prevGroup) total += groupGap; total += step; prevGroup = k.group; }
    float x = (kWidth - total) / 2 + step / 2;
    prevGroup = -1;
    knobs_.clear();
    for (const auto& k : ks) {
        if (prevGroup >= 0 && k.group != prevGroup) x += groupGap;
        const bool isBig = k.id == kMix;
        knobs_.push_back({k.id, k.label, k.fx, x, bottom - (isBig ? 66.f : 56.f), isBig ? big : small});
        x += step;
        prevGroup = k.group;
    }

    const float oy = bottom + 14;
    osSeg_ = {190, oy, 230, 26};
    charBox_ = {590, oy, 180, 26};
    clipSeg_ = {870, oy, 200, 26};
    autoGainTgl_ = {24, oy + 36, 220, 22};
    cameraTgl_ = {270, oy + 36, 260, 22};
}

bool Editor::glInit(float scale) {
    scale_ = scale;
    const bool s = scene_.init();
    const bool d = draw_.init(scale);
    glOk_ = s && d;
    return glOk_;
}

void Editor::glDestroy() {
    if (!glOk_) return;
    scene_.destroy();
    draw_.destroy();
    glOk_ = false;
}

// ------------------------------------------------------------------------------------------
// Parameter edits from the GUI
// ------------------------------------------------------------------------------------------
void Editor::setParam(uint32_t id, float plain) {
    host_.beginEdit(id);
    host_.performEdit(id, plain);
    host_.endEdit(id);
}

void Editor::applyPreset(int c) {
    const Preset& p = presetFor(c);
    const float to[] = {p.input, p.elevate, p.comp, p.punch, p.sub, p.mix, p.space, p.clip, p.limit, p.output};
    const uint32_t ids[] = {kInput, kElevate, kComp, kPunch, kSub, kMix, kSpace, kClip, kLimit, kOutput};
    for (int i = 0; i < 10; ++i) {
        morphFrom_[ids[i]] = core_.params.get(ids[i]);
        morphTo_[ids[i]] = to[i];
        if (!morphing_) host_.beginEdit(ids[i]);
    }
    morphing_ = true;
    morphT_ = 0;
    setParam(kOversampling, (float)p.oversampling);
    setParam(kCharacter, (float)p.character);
    setParam(kClipStyle, (float)p.clipStyle);
    currentPreset_ = c;
}

void Editor::toast(const std::string& text, float seconds) {
    toast_ = text;
    toastTime_ = seconds;
}

// ------------------------------------------------------------------------------------------
// Per-frame logic
// ------------------------------------------------------------------------------------------
void Editor::tick(float dt) {
    // knob morph towards a preset (one automation gesture per knob)
    if (morphing_) {
        morphT_ = std::min(1.f, morphT_ + dt / 0.7f);
        const float e = 1 - std::pow(1 - morphT_, 3.f);
        const uint32_t ids[] = {kInput, kElevate, kComp, kPunch, kSub, kMix, kSpace, kClip, kLimit, kOutput};
        for (uint32_t id : ids) host_.performEdit(id, morphFrom_[id] + (morphTo_[id] - morphFrom_[id]) * e);
        if (morphT_ >= 1) {
            for (uint32_t id : ids) host_.endEdit(id);
            morphing_ = false;
        }
    }

    // recognition
    Analyzer& an = core_.analyzer();
    if (listening_) {
        const float prog = an.listenProgress();
        char b[96];
        std::snprintf(b, sizeof b, "◎ Écoute… %.1f s", std::max(0.f, 2.f - 2.f * prog));
        toast(b, 1.f);
        listenWait_ += dt;
        if (prog <= 0.f && listenWait_ > 3.f) {
            toast("Aucun son reçu : lance la lecture dans ton DAW puis clique à nouveau sur Analyser.");
            listening_ = false;
        }
    }
    if (an.recognitionCount() != seenRecognition_) {
        seenRecognition_ = an.recognitionCount();
        const int c = an.recognizedClass();
        char b[160];
        std::snprintf(b, sizeof b, "Détecté : %s  %.0f %% · preset appliqué", kClassNamesFr[c], an.recognizedConfidence() * 100);
        listening_ = false;
        applyPreset(c);
        toast(b);
    }
    if (toastTime_ > 0) toastTime_ -= dt;

    // bass hits from the analysis thread
    BassHit hit;
    while (an.bassHits.pop(hit)) scene_.bassHit(hit.strength);

    // meters (peaks since the last frame)
    Meters& m = core_.engine().meters;
    const float ip = m.inPeak.exchange(0.f), op = m.outPeak.exchange(0.f);
    inPeak_ = std::max(ip, inPeak_ * std::exp(-dt * 3));
    outPeak_ = std::max(op, outPeak_ * std::exp(-dt * 3));
    const float rmsDb = 20 * std::log10(m.outRms.load() + 1e-9f) - 0.7f;
    lufs_ += (rmsDb - lufs_) * std::min(1.f, dt / 3.f);

    // scene
    SceneInput in;
    const ParamStore& p = core_.params;
    in.input = p.get(kInput); in.elevate = p.get(kElevate); in.comp = p.get(kComp); in.punch = p.get(kPunch);
    in.sub = p.get(kSub); in.mix = p.get(kMix); in.space = p.get(kSpace); in.clip = p.get(kClip);
    in.limit = p.get(kLimit); in.output = p.get(kOutput);
    in.oversampling = (int)p.get(kOversampling);
    in.clipStyle = (int)p.get(kClipStyle);
    in.bypass = p.get(kBypass) >= 0.5f;
    in.cameraOnBass = p.get(kCameraBass) >= 0.5f;
    in.licensed = core_.licensed.load();
    in.quickBuild = core_.towerShown.load();
    if (stage_.contains(mouseX_, mouseY_) && dragKnob_ < 0) {
        in.mouseX = (mouseX_ - stage_.x) / stage_.w - 0.5f;
        in.mouseY = (mouseY_ - stage_.y) / stage_.h - 0.5f;
    }
    scene_.update(dt, in);
    if (scene_.buildProgress() >= 1.f) core_.towerShown.store(true);
}

void Editor::render(int fbW, int fbH) {
    const auto now = std::chrono::steady_clock::now();
    float dt = firstFrame_ ? 1.f / 60 : std::chrono::duration<float>(now - last_).count();
    last_ = now;
    firstFrame_ = false;
    dt = std::min(dt, 0.05f);
    tick(dt);

    glViewport(0, 0, fbW, fbH);
    glClearColor(kLacquer.r, kLacquer.g, kLacquer.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!glOk_) return;

    const float sx = fbW / (float)kWidth, sy = fbH / (float)kHeight;
    const int vx = (int)(stage_.x * sx), vw = (int)(stage_.w * sx);
    const int vh = (int)(stage_.h * sy), vy = fbH - (int)((stage_.y + stage_.h) * sy);
    scene_.render(vx, vy, vw, vh, stage_.w / stage_.h);

    draw_.begin(fbW, fbH);
    drawHeader();
    drawKnobs();
    drawOverlays();
    drawOptions();
    drawStatus();
    drawMenu();
    draw_.end();
}

// ------------------------------------------------------------------------------------------
// Drawing
// ------------------------------------------------------------------------------------------
void Editor::drawButton(const Rect& r, const std::string& label, Color fg, Color border, Color bg) {
    draw_.roundRect(r.x, r.y, r.w, r.h, 6, bg);
    draw_.roundRectStroke(r.x, r.y, r.w, r.h, 6, 1, border);
    draw_.text(kFontUi, r.x + r.w / 2, r.y + r.h / 2 + 5, label, fg, 1);
}

void Editor::drawHeader() {
    draw_.rect(0, 0, kWidth, kHeaderH, kLacquer);
    draw_.rect(0, kHeaderH - 1, kWidth, 1, kLine);
    draw_.text(kFontTitle, 22, 35, "TWISTED", kWashi);
    draw_.text(kFontSmall, 150, 34, "monomi · tour d'observation", kMuted);

    draw_.text(kFontLabel, presetBox_.x - 10, presetBox_.y + 19, "PRESET", kMuted, 2);
    draw_.roundRect(presetBox_.x, presetBox_.y, presetBox_.w, presetBox_.h, 6, kPanel);
    draw_.roundRectStroke(presetBox_.x, presetBox_.y, presetBox_.w, presetBox_.h, 6, 1, kLine);
    draw_.text(kFontUi, presetBox_.x + 12, presetBox_.y + 20, currentPreset_ < 0 ? "Manuel" : kClassNamesFr[currentPreset_], kWashi);
    draw_.text(kFontSmall, presetBox_.x + presetBox_.w - 14, presetBox_.y + 19, "▾", kMuted, 1);

    drawButton(analyseBtn_, "◎ Analyser", kRei, withAlpha(kRei, 0.55f), kPanel);
    const bool byp = core_.params.get(kBypass) >= 0.5f;
    drawButton(bypassBtn_, "Bypass", byp ? Color{1, 1, 1, 1} : kWashi, byp ? kShu : kLine, byp ? kShu : kPanel);
    draw_.circle(bypassBtn_.x + 16, bypassBtn_.y + bypassBtn_.h / 2, 3.5f, byp ? hexColor(0xffd9cf) : kLine);
}

void Editor::drawKnobs() {
    const float bottom = stage_.y + stage_.h;
    // dark gradient so the knobs stay readable over the city
    draw_.rectGradient(0, bottom - 170, kWidth, 170, Color{kSumi.r, kSumi.g, kSumi.b, 0}, withAlpha(kSumi, 0.86f));
    const bool byp = core_.params.get(kBypass) >= 0.5f;
    const float dim = byp ? 0.45f : 1.f;
    const float a0 = -2.356f, a1 = 2.356f;  // -135..+135 degrees
    for (size_t i = 0; i < knobs_.size(); ++i) {
        const Knob& k = knobs_[i];
        const ParamInfo& pi = paramInfo(k.id);
        const float v = core_.params.get(k.id), n = toNormalized(k.id, v);
        const float ang = a0 + n * (a1 - a0);
        const bool bip = pi.min < 0 && pi.max > 0;
        const bool hot = (int)i == hoverKnob_ || (int)i == dragKnob_;
        draw_.circle(k.cx, k.cy, k.r - 4, withAlpha(kSumi, 0.55f));
        draw_.arc(k.cx, k.cy, k.r, a0, a1, 2.5f, withAlpha(kWashi, (hot ? 0.4f : 0.22f) * dim));
        const float from = bip ? 0.f : a0;
        const Color arcCol = k.id == kMix ? Color{kKin.r + (kRei.r - kKin.r) * n, kKin.g + (kRei.g - kKin.g) * n, kKin.b + (kRei.b - kKin.b) * n, 1} : kKin;
        if (std::fabs(ang - from) > 0.01f) draw_.arc(k.cx, k.cy, k.r, std::min(from, ang), std::max(from, ang), k.id == kMix ? 3.f : 2.5f, withAlpha(arcCol, dim));
        const float ix = std::sin(ang), iy = -std::cos(ang);
        draw_.line(k.cx + ix * (k.r - 12), k.cy + iy * (k.r - 12), k.cx + ix * (k.r - 4), k.cy + iy * (k.r - 4), 2, withAlpha(kWashi, dim));
        const float ly = k.cy + k.r + 16;
        draw_.text(kFontLabel, k.cx, ly, k.label, withAlpha(kWashi, 0.8f * dim), 1);
        draw_.text(kFontMono, k.cx, ly + 14, valueText(k.id, v), withAlpha(kKin, dim), 1);
    }
}

void Editor::drawOverlays() {
    // meters panel
    const Rect mp{kWidth - 210.f, stage_.y + 14, 196, 132};
    draw_.roundRect(mp.x, mp.y, mp.w, mp.h, 8, withAlpha(kLacquer, 0.8f));
    draw_.roundRectStroke(mp.x, mp.y, mp.w, mp.h, 8, 1, withAlpha(kLine, 0.8f));
    draw_.text(kFontLabel, mp.x + 14, mp.y + 20, "NIVEAUX", kMuted);
    auto meter = [&](float y, const char* label, float db) {
        draw_.text(kFontSmall, mp.x + 14, y + 7, label, kMuted);
        const Rect bar{mp.x + 52, y, 82, 6};
        draw_.roundRect(bar.x, bar.y, bar.w, bar.h, 3, kLine);
        const float f = std::clamp((db + 60) / 60, 0.f, 1.f);
        if (f > 0) draw_.roundRect(bar.x, bar.y, bar.w * f, bar.h, 3, f > 0.95f ? kShu : (f > 0.75f ? kKin : kOk));
        char b[32];
        if (db < -60) std::snprintf(b, sizeof b, "−∞");
        else std::snprintf(b, sizeof b, "%s%.1f", db < 0 ? "−" : "+", std::fabs(db));
        draw_.text(kFontMono, mp.x + mp.w - 14, y + 7, b, kWashi, 2);
    };
    meter(mp.y + 34, "IN", 20 * std::log10(inPeak_ + 1e-9f));
    meter(mp.y + 54, "OUT", 20 * std::log10(outPeak_ + 1e-9f));
    meter(mp.y + 74, "LUFS", lufs_);
    draw_.rect(mp.x + 14, mp.y + 92, mp.w - 28, 1, kLine);
    char gs[96];
    if (core_.params.get(kAutoGain) >= 0.5f) {
        const float t = core_.engine().autoTrimDb.load();
        std::snprintf(gs, sizeof gs, "Gain staging auto : %s%.1f dB", t >= 0 ? "+" : "−", std::fabs(t));
    } else {
        std::snprintf(gs, sizeof gs, "Gain staging manuel");
    }
    draw_.text(kFontSmall, mp.x + 14, mp.y + 110, gs, kMuted);
    draw_.text(kFontSmall, mp.x + 14, mp.y + 124, "cible −18 dBFS RMS", kMuted);

    // toast (analysis, oversampling)
    if (toastTime_ > 0 && !toast_.empty()) {
        const float a = std::min(1.f, toastTime_ / 0.5f);
        const float w = draw_.textWidth(kFontUi, toast_) + 26;
        draw_.roundRect(14, stage_.y + 14, w, 30, 6, withAlpha(kLacquer, 0.85f * a));
        draw_.roundRectStroke(14, stage_.y + 14, w, 30, 6, 1, withAlpha(kRei, 0.5f * a));
        draw_.text(kFontUi, 27, stage_.y + 34, toast_, withAlpha(kWashi, a));
    }

    // knob hint
    const int hk = dragKnob_ >= 0 ? dragKnob_ : hoverKnob_;
    if (hk >= 0) {
        const Knob& k = knobs_[(size_t)hk];
        const std::string label = std::string(k.label) + "  ";
        const float w = draw_.textWidth(kFontUi, label) + draw_.textWidth(kFontUi, k.fx) + 26;
        const float x = (kWidth - w) / 2, y = stage_.y + 160;
        draw_.roundRect(x, y, w, 30, 6, withAlpha(kLacquer, 0.85f));
        draw_.roundRectStroke(x, y, w, 30, 6, 1, withAlpha(kKin, 0.5f));
        const float lw = draw_.text(kFontUi, x + 13, y + 20, label, kKin);
        draw_.text(kFontUi, x + 13 + lw, y + 20, k.fx, kWashi);
    }

    // licence: no tower without it
    const float build = scene_.buildProgress();
    if (!core_.licensed.load() || build < 1.f) {
        const bool waiting = !core_.licensed.load();
        const std::string l1 = waiting ? "La tour attend sa licence" : "Construction de la tour…";
        const std::string l2 = waiting ? "Active Twisted avec ta clé de licence ou ton compte abrasion.dev"
                                       : "Licence reconnue";
        const float w = std::max(draw_.textWidth(kFontUi, l1), draw_.textWidth(kFontSmall, l2)) + 40;
        const float x = (kWidth - w) / 2, y = stage_.y + 214;
        const float a = waiting ? 1.f : std::min(1.f, (1.f - build) * 4.f);
        if (a > 0.01f) {
            draw_.roundRect(x, y, w, 50, 8, withAlpha(kLacquer, 0.85f * a));
            draw_.roundRectStroke(x, y, w, 50, 8, 1, withAlpha(kKin, 0.6f * a));
            draw_.text(kFontUi, kWidth / 2.f, y + 21, l1, withAlpha(kKin, a), 1);
            draw_.text(kFontSmall, kWidth / 2.f, y + 38, l2, withAlpha(kWashi, 0.8f * a), 1);
        }
    }

    // bypass badge
    if (core_.params.get(kBypass) >= 0.5f) {
        const float w = 170, x = (kWidth - w) / 2, y = stage_.y + 60;
        draw_.roundRect(x, y, w, 30, 4, kShu);
        draw_.text(kFontUi, x + w / 2, y + 20, "BYPASS · sudōshi", Color{1, 1, 1, 1}, 1);
    }
}

void Editor::drawSegmented(const Rect& r, const char* const* labels, int n, int selected, std::vector<Rect>& hits) {
    hits.clear();
    draw_.roundRect(r.x, r.y, r.w, r.h, 6, kLacquer);
    const float w = r.w / n;
    for (int i = 0; i < n; ++i) {
        const Rect c{r.x + i * w, r.y, w, r.h};
        hits.push_back(c);
        if (i == selected) draw_.roundRect(c.x + 1, c.y + 1, c.w - 2, c.h - 2, 5, kPanel);
        if (i > 0) draw_.rect(c.x, c.y + 1, 1, c.h - 2, kLine);
        draw_.text(kFontMono, c.x + w / 2, c.y + r.h / 2 + 4, labels[i], i == selected ? kKin : kMuted, 1);
    }
    draw_.roundRectStroke(r.x, r.y, r.w, r.h, 6, 1, kLine);
}

void Editor::drawToggle(const Rect& r, const char* label, bool on) {
    draw_.roundRect(r.x, r.y + 3, 30, 16, 8, on ? Color{0.36f, 0.48f, 0.36f, 1} : kLine);
    draw_.circle(r.x + (on ? 22.f : 8.f), r.y + 11, 6, on ? kWashi : kMuted);
    draw_.text(kFontLabel, r.x + 40, r.y + 15, label, kMuted);
}

void Editor::drawOptions() {
    const float y0 = stage_.y + stage_.h;
    draw_.rect(0, y0, kWidth, kOptionsH, kLacquer);
    const bool byp = core_.params.get(kBypass) >= 0.5f;
    (void)byp;
    draw_.text(kFontLabel, 24, osSeg_.y + 17, "SURÉCHANTILLONNAGE", kMuted);
    drawSegmented(osSeg_, kOsLabels, 5, (int)core_.params.get(kOversampling), osHits_);
    draw_.text(kFontLabel, charBox_.x - 12, charBox_.y + 17, "CARACTÈRE ELEVATE", kMuted, 2);
    draw_.roundRect(charBox_.x, charBox_.y, charBox_.w, charBox_.h, 6, kPanel);
    draw_.roundRectStroke(charBox_.x, charBox_.y, charBox_.w, charBox_.h, 6, 1, kLine);
    draw_.text(kFontUi, charBox_.x + 10, charBox_.y + 18, kCharFr[(int)core_.params.get(kCharacter) & 3], kWashi);
    draw_.text(kFontSmall, charBox_.x + charBox_.w - 12, charBox_.y + 17, "▾", kMuted, 1);
    draw_.text(kFontLabel, clipSeg_.x - 12, clipSeg_.y + 17, "CLIPPER", kMuted, 2);
    const char* clipLabels[3] = {"Hard", "Soft", "Analog"};
    drawSegmented(clipSeg_, clipLabels, 3, (int)core_.params.get(kClipStyle), clipHits_);
    drawToggle(autoGainTgl_, "GAIN STAGING AUTO", core_.params.get(kAutoGain) >= 0.5f);
    drawToggle(cameraTgl_, "CAMÉRA SUR LES BASSES", core_.params.get(kCameraBass) >= 0.5f);
}

void Editor::drawStatus() {
    const float y = kHeight - kStatusH;
    draw_.rect(0, y, kWidth, kStatusH, kSumi);
    draw_.rect(0, y, kWidth, 1, kLine);
#if TW_SIMD_SSE
    const char* simd = "SSE2 · 4 voies";
#elif TW_SIMD_NEON
    const char* simd = "NEON · 4 voies";
#else
    const char* simd = "scalaire";
#endif
    char b[256];
    std::snprintf(b, sizeof b, "OS %s polyphase IIR min-phase  ·  Plafond −1 dBFS  ·  SIMD %s  ·  Threads audio RT · analyse · UI, files SPSC sans verrou  ·  CPU %.1f %%",
                  kOsLabels[(int)core_.params.get(kOversampling)], simd, core_.cpuLoad.load() * 100);
    float x = 18;
    x += draw_.text(kFontMono, x, y + 17, "Latence ", kMuted);
    x += draw_.text(kFontMono, x, y + 17, "0 échantillon", kOk);
    draw_.text(kFontMono, x + 14, y + 17, b, kMuted);
}

void Editor::drawMenu() {
    menuHits_.clear();
    if (menu_ == Menu::None) return;
    const bool preset = menu_ == Menu::Preset;
    const Rect& anchor = preset ? presetBox_ : charBox_;
    const int n = preset ? kNumClasses + 1 : 4;
    const float itemH = 26, w = anchor.w;
    float y = anchor.y + anchor.h + 4;
    if (!preset) y = anchor.y - 4 - n * itemH;  // the character menu opens upwards
    draw_.roundRect(anchor.x, y, w, n * itemH, 6, kPanel);
    draw_.roundRectStroke(anchor.x, y, w, n * itemH, 6, 1, kLine);
    for (int i = 0; i < n; ++i) {
        const Rect r{anchor.x, y + i * itemH, w, itemH};
        menuHits_.push_back(r);
        if (r.contains(mouseX_, mouseY_)) draw_.rect(r.x + 1, r.y, r.w - 2, r.h, kLacquer);
        const char* label = preset ? (i == 0 ? "Manuel" : kClassNamesFr[i - 1]) : kCharFr[i];
        draw_.text(kFontUi, r.x + 12, r.y + 18, label, kWashi);
    }
}

// ------------------------------------------------------------------------------------------
// Mouse
// ------------------------------------------------------------------------------------------
int Editor::knobAt(float x, float y) const {
    for (size_t i = 0; i < knobs_.size(); ++i) {
        const Knob& k = knobs_[i];
        const float dx = x - k.cx, dy = y - k.cy;
        if (dx * dx + dy * dy <= (k.r + 8) * (k.r + 8)) return (int)i;
    }
    return -1;
}

void Editor::mouseDown(float x, float y, bool shift) {
    (void)shift;
    mouseX_ = x;
    mouseY_ = y;
    if (menu_ != Menu::None) {
        for (size_t i = 0; i < menuHits_.size(); ++i)
            if (menuHits_[i].contains(x, y)) {
                if (menu_ == Menu::Preset) {
                    if (i == 0) currentPreset_ = -1;
                    else applyPreset((int)i - 1);
                } else {
                    setParam(kCharacter, (float)i);
                }
            }
        menu_ = Menu::None;
        return;
    }
    const int k = knobAt(x, y);
    if (k >= 0) {
        const auto now = std::chrono::steady_clock::now();
        const uint32_t id = knobs_[(size_t)k].id;
        if (k == lastClickKnob_ && std::chrono::duration<float>(now - lastClickTime_).count() < 0.35f) {
            setParam(id, paramInfo(id).def);  // double click: default value
            lastClickKnob_ = -1;
            currentPreset_ = -1;
            return;
        }
        lastClickKnob_ = k;
        lastClickTime_ = now;
        dragKnob_ = k;
        dragStartY_ = y;
        dragStartNorm_ = toNormalized(id, core_.params.get(id));
        host_.beginEdit(id);
        return;
    }
    if (presetBox_.contains(x, y)) { menu_ = Menu::Preset; return; }
    if (charBox_.contains(x, y)) { menu_ = Menu::Character; return; }
    if (analyseBtn_.contains(x, y)) {
        core_.analyzer().requestRecognition();
        listening_ = true;
        listenWait_ = 0;
        toast("◎ Écoute… 2.0 s", 1.f);
        return;
    }
    if (bypassBtn_.contains(x, y)) { setParam(kBypass, core_.params.get(kBypass) >= 0.5f ? 0.f : 1.f); return; }
    for (size_t i = 0; i < osHits_.size(); ++i)
        if (osHits_[i].contains(x, y)) {
            setParam(kOversampling, (float)i);
            toast(std::string("Suréchantillonnage ") + kOsLabels[i] + " : " + kBirdsText[i]);
            return;
        }
    for (size_t i = 0; i < clipHits_.size(); ++i)
        if (clipHits_[i].contains(x, y)) { setParam(kClipStyle, (float)i); return; }
    if (autoGainTgl_.contains(x, y)) { setParam(kAutoGain, core_.params.get(kAutoGain) >= 0.5f ? 0.f : 1.f); return; }
    if (cameraTgl_.contains(x, y)) { setParam(kCameraBass, core_.params.get(kCameraBass) >= 0.5f ? 0.f : 1.f); return; }
}

void Editor::mouseUp(float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
    if (dragKnob_ >= 0) {
        host_.endEdit(knobs_[(size_t)dragKnob_].id);
        dragKnob_ = -1;
    }
}

void Editor::mouseMove(float x, float y, bool shift) {
    mouseX_ = x;
    mouseY_ = y;
    if (dragKnob_ >= 0) {
        const uint32_t id = knobs_[(size_t)dragKnob_].id;
        const float n = std::clamp(dragStartNorm_ + (dragStartY_ - y) / 220.f * (shift ? 0.25f : 1.f), 0.f, 1.f);
        host_.performEdit(id, fromNormalized(id, n));
        currentPreset_ = -1;
        return;
    }
    hoverKnob_ = knobAt(x, y);
}

void Editor::mouseWheel(float x, float y, float dy, bool shift) {
    const int k = knobAt(x, y);
    if (k < 0) return;
    const uint32_t id = knobs_[(size_t)k].id;
    const float n = std::clamp(toNormalized(id, core_.params.get(id)) + (dy > 0 ? 1.f : -1.f) * (shift ? 0.005f : 0.02f), 0.f, 1.f);
    setParam(id, fromNormalized(id, n));
    currentPreset_ = -1;
}

void Editor::mouseLeave() {
    mouseX_ = mouseY_ = -1;
    hoverKnob_ = -1;
}

} // namespace tw
