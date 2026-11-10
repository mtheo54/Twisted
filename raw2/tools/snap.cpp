// Outil de développement : rend l'interface hors écran et l'enregistre en PNG (Cairo sous Linux,
// CoreGraphics sous macOS : le même rendu que le plugin Mac, vérifié en intégration continue).
// Le moteur audio tourne pour de vrai dans un thread (sans sortie son), une note est tenue.
//   raw2_snap <sortie.png> [onglet 0-4] [thème 0-5] [option]
//   option : theme | presets | save | xy | menus | drag
#include "core/Files.h"
#include "core/Model.h"
#include "dsp/Engine.h"
#include "ui/Editor.h"
#if defined(__APPLE__)
#include "ui/CGCanvas.h"
#include <ImageIO/ImageIO.h>
#else
#include "ui/CairoCanvas.h"
#include <cairo.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace raw2;

struct DirectSink : ParamSink {
    Model& m;
    explicit DirectSink(Model& mm) : m(mm) {}
    void beginEdit(uint32_t) override {}
    void performEdit(uint32_t id, float v) override { m.params.set(id, v); }
    void endEdit(uint32_t) override {}
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: raw2_snap out.png [tab] [theme] [option]\n");
        return 1;
    }
    const int tab = argc > 2 ? std::atoi(argv[2]) : 0;
    const int theme = argc > 3 ? std::atoi(argv[3]) : 0;
    const std::string opt = argc > 4 ? argv[4] : "";

    Model m;
    if (const char* p = std::getenv("RAW2_SNAP_PRESET")) m.applyFactoryPreset(std::atoi(p), nullptr);
    if (const char* p = std::getenv("RAW2_SNAP_PRESETFILE")) {  // un preset .raw (pack)
        std::string t;
        json::Value v;
        m.instruments.scan();
        if (files::readText(p, t) && json::Value::parse(t, v)) m.applyPresetJson(v, nullptr);
    }
    if (opt == "sample") {  // un son déposé sur l'OSC A, joué tel quel (page SON : forme d'onde complète)
        std::vector<float> x(48000 * 2);
        for (size_t i = 0; i < x.size(); ++i) {
            const double t = (double)i / 48000.0, env = std::exp(-1.6 * t) * (1.0 - std::exp(-60.0 * t));
            x[i] = (float)(env * (std::sin(2.0 * 3.14159265 * 110.0 * t) + 0.4 * std::sin(2.0 * 3.14159265 * 330.0 * t * (1.0 + 0.1 * t))) * (0.6 + 0.4 * std::sin(9.0 * t)));
        }
        m.samples.load(0, "vocal chop.wav", x, 48000.0);
        m.params.set(kAPlay, 1.f);
    }
    m.theme = theme;
    m.setTab(tab);
    Engine eng(m);
    eng.prepare(48000.0, 256);
    std::atomic<bool> run{true};
    std::thread audio([&] {
        float L[256], R[256];
        int b = 0;
        while (run) {
            NoteEvent ev{0, 60, 0.8f, true};
            eng.process(L, R, 256, &ev, b == 20 ? 1 : 0);
            ++b;
            std::this_thread::sleep_for(std::chrono::microseconds(5333));
        }
    });

    DirectSink sink(m);
    ui::EditorState st;
    if (opt == "route") {  // une route par note avec courbe et source auxiliaire, sélectionnée (page MODULES)
        const int i = m.routes.add(kRouteSrcEnv3, kCutoff, 60);
        m.routes.setShape(i, 45, kRouteSrcPress);
        st.routeSel = i;
    }
    if (opt == "env34") st.envAlt = {true, true};  // page ENV / LFO : ENV 3 et ENV 4
    if (opt == "about") st.openAbout = true;
    ui::Editor ed(m, sink, st);
#if defined(__APPLE__)
    // contexte bitmap 1280 x 960 (x2 si RAW2_SNAP_RETINA), axe y retourné vers le bas
    const int px = std::getenv("RAW2_SNAP_RETINA") ? 2 : 1;
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(nullptr, 1280 * px, 960 * px, 8, 0, space, kCGImageAlphaPremultipliedLast);
    CGContextTranslateCTM(ctx, 0, 960 * px);
    CGContextScaleCTM(ctx, px, -px);
    ui::CGCanvas canvas;
    canvas.setContext(ctx);
#else
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1280, 960);
    cairo_t* cr = cairo_create(surf);
    ui::CairoCanvas canvas(cr);
#endif
    const auto t0 = std::chrono::steady_clock::now();
    auto now = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    // script de clics : "clicks:x,y;x,y;..." (deux images par clic, à partir de l'image 30) ;
    // "x,y@n" place un clic à l'image n (pour attendre la fin d'une écoute, par exemple)
    std::vector<ui::Pt> clicks;
    std::vector<int> clickFrame;
    if (opt.rfind("clicks:", 0) == 0) {
        const char* p = opt.c_str() + 7;
        float x, y;
        int n;
        while (std::sscanf(p, "%f,%f%n", &x, &y, &n) == 2) {
            clicks.push_back({x, y});
            p += n;
            int at = -1, m = 0;
            if (*p == '@' && std::sscanf(p + 1, "%d%n", &at, &m) == 1) p += 1 + m;
            clickFrame.push_back(at);
            if (*p == ';') ++p;
        }
    }
    // geste « drag:x1,y1>x2,y2 » après les clics : appui, 20 images de déplacement, relâchement
    // (pas de relâchement si RAW2_SNAP_HOLD=1 : l'image montre le glisser en cours) ;
    // l'option peut combiner « clicks:...|drag:... »
    float dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
    bool hasDrag = false;
    {
        const size_t d = opt.find("drag:");
        if (d != std::string::npos) hasDrag = std::sscanf(opt.c_str() + d + 5, "%f,%f>%f,%f", &dx1, &dy1, &dx2, &dy2) == 4;
    }
    const bool hold = std::getenv("RAW2_SNAP_HOLD") != nullptr;
    int clickAt = 30;
    if (const char* c = std::getenv("RAW2_SNAP_CLICKAT")) clickAt = std::atoi(c);
    ui::Input in;
    int frames = 110 + (int)clicks.size() * 2;
    for (int at : clickFrame) frames = std::max(frames, at + 20);
    if (const char* f = std::getenv("RAW2_SNAP_FRAMES")) frames = std::max(frames, std::atoi(f));  // laisser le temps à l'IA
    double frameSum = 0.0, frameMax = 0.0;
    int frameN = 0;
    for (int f = 0; f < frames; ++f) {
        in = ui::Input{};
        in.timeMs = now() + 1000.0;
        in.mouse = {-100.f, -100.f};
        if (opt == "theme" && f == 60) { in.mouse = {201.f, 42.f}; in.pressed = in.down = true; }
        if (opt == "theme" && f == 61) { in.mouse = {201.f, 42.f}; in.released = true; }
        if (opt == "presets" && f == 60) { in.mouse = {1020.f, 42.f}; in.pressed = in.down = true; }
        if (opt == "presets" && f == 61) { in.mouse = {1020.f, 42.f}; in.released = true; }
        if (opt == "save" && f == 60) { in.mouse = {1190.f, 42.f}; in.pressed = in.down = true; }
        if (opt == "save" && f == 61) { in.mouse = {1190.f, 42.f}; in.released = true; }
        if (opt == "menus" && f == 60) { in.mouse = {460.f, 505.f}; in.pressed = in.down = true; }
        if (opt == "menus" && f == 61) { in.mouse = {460.f, 505.f}; in.released = true; }
        if (opt == "menus" && f == 62) { in.mouse = {1080.f, 678.f}; in.pressed = in.down = true; }
        if (opt == "menus" && f == 63) { in.mouse = {1080.f, 678.f}; in.released = true; }
        if (opt == "xy" && f >= 20 && f < 70) { in.mouse = {40.f, 40.f}; in.down = true; in.pressed = f == 20; }
        if (opt == "xy" && f == 70) { in.mouse = {40.f, 40.f}; in.released = true; }
        if (opt == "analyse" && f == 5) { in.mouse = {140.f, 639.f}; in.pressed = in.down = true; }
        if (opt == "analyse" && f == 6) { in.mouse = {140.f, 639.f}; in.released = true; }
        for (size_t k = 0, slot = 0; k < clicks.size(); ++k) {
            const int at = clickFrame[k] >= 0 ? clickFrame[k] : clickAt + (int)(slot++) * 2;
            if (f == at || f == at + 1) {
                in.mouse = clicks[k];
                if (f == at) { in.pressed = in.down = true; } else { in.released = true; }
            }
        }
        if (hasDrag) {
            const int d0 = clickAt + (int)clicks.size() * 2 + 4;
            if (f >= d0 && f <= d0 + 20) {
                const float t = (float)(f - d0) / 20.f;
                in.mouse = {dx1 + (dx2 - dx1) * t, dy1 + (dy2 - dy1) * t};
                in.down = true;
                in.pressed = f == d0;
            } else if (f > d0 + 20 && hold) {
                in.mouse = {dx2, dy2};
                in.down = true;
            } else if (f == d0 + 21) {
                in.mouse = {dx2, dy2};
                in.released = true;
            }
        }
        if (opt == "drag") { in.dragOver = true; in.dragPos = tab == 6 ? ui::Pt{600.f, 780.f} : ui::Pt{200.f, 200.f}; }
        const auto f0 = std::chrono::steady_clock::now();
        ed.frame(canvas, in);
#if defined(__APPLE__)
        canvas.frameDone();
#endif
        const double fms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - f0).count();
        if (f >= 10) { frameSum += fms; frameMax = std::max(frameMax, fms); ++frameN; }  // après le démarrage
        std::this_thread::sleep_for(std::chrono::milliseconds(opt == "analyse" ? 50 : 16));
    }
    if (frameN) std::printf("image : %.1f ms en moyenne, %.1f ms au pire (%d images)\n", frameSum / frameN, frameMax, frameN);
#if defined(__APPLE__)
    bool saved = false;
    if (CGImageRef img = CGBitmapContextCreateImage(ctx)) {
        CFStringRef path = CFStringCreateWithCString(nullptr, argv[1], kCFStringEncodingUTF8);
        CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path, kCFURLPOSIXPathStyle, false);
        if (CGImageDestinationRef dst = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr)) {
            CGImageDestinationAddImage(dst, img, nullptr);
            saved = CGImageDestinationFinalize(dst);
            CFRelease(dst);
        }
        CFRelease(url);
        CFRelease(path);
        CGImageRelease(img);
    }
    run = false;
    audio.join();
    CGContextRelease(ctx);
    CGColorSpaceRelease(space);
    return saved ? 0 : 1;
#else
    cairo_surface_write_to_png(surf, argv[1]);
    run = false;
    audio.join();
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 0;
#endif
}
