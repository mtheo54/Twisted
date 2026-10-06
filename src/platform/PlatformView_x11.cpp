// Linux: X11 child window + GLX 3.2 core context. The window runs its own render/event
// thread with its own X connection, so it does not depend on the host's event loop.
// Linux is not a release target; this exists to build and test the GUI on Linux.
#include "PlatformView.h"

#include "../ui/Editor.h"
#include "../ui/Gl.h"

#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace tw {
namespace {

typedef GLXContext (*CreateContextAttribsFn)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

void* getProc(const char* name) { return (void*)glXGetProcAddressARB((const GLubyte*)name); }

class X11View final : public PlatformView {
public:
    explicit X11View(Editor& e) : editor_(e) {}
    ~X11View() override { detach(); }

    void setScale(float s) override { scale_ = s > 0 ? s : 1.f; }
    void size(int& w, int& h) const override {
        w = (int)(Editor::kWidth * scale_);
        h = (int)(Editor::kHeight * scale_);
    }

    bool attach(void* parent) override {
        detach();
        parent_ = (Window)(uintptr_t)parent;
        running_ = true;
        ok_ = false;
        started_ = false;
        thread_ = std::thread([this] { run(); });
        while (!started_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return ok_.load();
    }

    void detach() override {
        if (!thread_.joinable()) return;
        running_ = false;
        thread_.join();
    }

private:
    void run() {
        XInitThreads();
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) { started_ = true; return; }
        const int screen = DefaultScreen(dpy);
        int fbAttrs[] = {GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT,
                         GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, True,
                         GLX_SAMPLE_BUFFERS, 1, GLX_SAMPLES, 4, None};
        int count = 0;
        GLXFBConfig* cfgs = glXChooseFBConfig(dpy, screen, fbAttrs, &count);
        if (!cfgs || count == 0) {
            fbAttrs[16] = None;  // retry without multisampling
            cfgs = glXChooseFBConfig(dpy, screen, fbAttrs, &count);
        }
        if (!cfgs || count == 0) { XCloseDisplay(dpy); started_ = true; return; }
        GLXFBConfig cfg = cfgs[0];
        XFree(cfgs);
        XVisualInfo* vi = glXGetVisualFromFBConfig(dpy, cfg);
        XSetWindowAttributes swa{};
        swa.colormap = XCreateColormap(dpy, parent_ ? parent_ : RootWindow(dpy, screen), vi->visual, AllocNone);
        swa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask |
                         StructureNotifyMask | KeyPressMask;
        int w, h;
        size(w, h);
        Window win = XCreateWindow(dpy, parent_ ? parent_ : RootWindow(dpy, screen), 0, 0, (unsigned)w, (unsigned)h, 0,
                                   vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
        XMapWindow(dpy, win);
        XSync(dpy, False);

        // Collage (Ctrl+V) : protocole ICCCM standard. On demande au propriétaire du
        // presse-papiers de convertir son contenu en UTF-8 et de le déposer dans une
        // propriété sur NOTRE fenêtre ; la réponse arrive plus tard comme SelectionNotify.
        const Atom atomClipboard = XInternAtom(dpy, "CLIPBOARD", False);
        const Atom atomUtf8 = XInternAtom(dpy, "UTF8_STRING", False);
        const Atom atomPasteProp = XInternAtom(dpy, "TWISTED_PASTE", False);

        auto createCtx = (CreateContextAttribsFn)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
        const int ctxAttrs[] = {0x2091 /*MAJOR*/, 3, 0x2092 /*MINOR*/, 2, 0x9126 /*PROFILE*/, 1 /*CORE*/, None};
        GLXContext ctx = createCtx ? createCtx(dpy, cfg, nullptr, True, ctxAttrs) : glXCreateNewContext(dpy, cfg, GLX_RGBA_TYPE, nullptr, True);
        XFree(vi);
        if (!ctx) { XDestroyWindow(dpy, win); XCloseDisplay(dpy); started_ = true; return; }
        glXMakeCurrent(dpy, win, ctx);
        const bool loaded = loadGl(&getProc);
        if (loaded) editor_.glInit(scale_);
        win_ = win;
        ok_ = loaded;
        started_ = true;

        auto next = std::chrono::steady_clock::now();
        while (running_.load()) {
            while (XPending(dpy)) {
                XEvent ev;
                XNextEvent(dpy, &ev);
                const float s = scale_;
                switch (ev.type) {
                case ButtonPress:
                    if (ev.xbutton.button == 1) {
                        XSetInputFocus(dpy, win, RevertToParent, CurrentTime);  // sinon KeyPress n'arrive jamais ici
                        editor_.mouseDown(ev.xbutton.x / s, ev.xbutton.y / s, ev.xbutton.state & ShiftMask);
                    } else if (ev.xbutton.button == 4 || ev.xbutton.button == 5)
                        editor_.mouseWheel(ev.xbutton.x / s, ev.xbutton.y / s, ev.xbutton.button == 4 ? 1.f : -1.f, ev.xbutton.state & ShiftMask);
                    break;
                case ButtonRelease:
                    if (ev.xbutton.button == 1) editor_.mouseUp(ev.xbutton.x / s, ev.xbutton.y / s);
                    break;
                case MotionNotify:
                    editor_.mouseMove(ev.xmotion.x / s, ev.xmotion.y / s, ev.xmotion.state & ShiftMask);
                    break;
                case LeaveNotify:
                    editor_.mouseLeave();
                    break;
                // ---- saisie clavier pour le panneau de licence ----
                // Pas de composition par touches mortes ici (XLookupString donne Latin-1 simple) :
                // largement suffisant pour une clé de licence, un e-mail ou un mot de passe ASCII
                // sur la seule plateforme qui ne sert qu'à tester la GUI, pas à la diffuser.
                case KeyPress: {
                    if (!editor_.wantsKeyboard()) break;
                    char buf[8] = {};
                    KeySym ks = NoSymbol;
                    const int n = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &ks, nullptr);
                    if ((ev.xkey.state & ControlMask) && (ks == XK_v || ks == XK_V)) {
                        XConvertSelection(dpy, atomClipboard, atomUtf8, atomPasteProp, win, CurrentTime);
                        break;
                    }
                    switch (ks) {
                        case XK_BackSpace: editor_.keyCommand(Editor::KeyCmd::Backspace); break;
                        case XK_Return: case XK_KP_Enter: editor_.keyCommand(Editor::KeyCmd::Enter); break;
                        case XK_Escape: editor_.keyCommand(Editor::KeyCmd::Escape); break;
                        case XK_Tab: editor_.keyCommand(Editor::KeyCmd::Tab); break;
                        default:
                            if (n > 0 && (unsigned char)buf[0] >= 0x20) editor_.textInput(std::string(buf, (size_t)n));
                            break;
                    }
                    break;
                }
                case SelectionNotify: {
                    if (!editor_.wantsKeyboard() || ev.xselection.property == None) break;
                    Atom type; int format; unsigned long nitems, after; unsigned char* data = nullptr;
                    if (XGetWindowProperty(dpy, win, atomPasteProp, 0, 1 << 20, False, AnyPropertyType,
                                           &type, &format, &nitems, &after, &data) == Success && data) {
                        editor_.textInput(std::string((const char*)data, nitems));
                        XFree(data);
                    }
                    XDeleteProperty(dpy, win, atomPasteProp);
                    break;
                }
                default:
                    break;
                }
            }
            XWindowAttributes wa;
            XGetWindowAttributes(dpy, win, &wa);
            if (loaded) editor_.render(wa.width, wa.height);
            ++frames_;
            if (shotPath_ && frames_ == shotFrame_) screenshot(wa.width, wa.height);
            glXSwapBuffers(dpy, win);
            next += std::chrono::microseconds(16667);
            std::this_thread::sleep_until(next);
            if (std::chrono::steady_clock::now() > next + std::chrono::milliseconds(100)) next = std::chrono::steady_clock::now();
        }
        if (loaded) editor_.glDestroy();
        glXMakeCurrent(dpy, None, nullptr);
        glXDestroyContext(dpy, ctx);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
    }

    // Test hook: TWISTED_SCREENSHOT=file.ppm [TWISTED_SCREENSHOT_FRAME=n] saves one frame.
    void screenshot(int w, int h) {
        std::vector<unsigned char> px((size_t)w * h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
        if (FILE* f = std::fopen(shotPath_, "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int y = h - 1; y >= 0; --y) std::fwrite(&px[(size_t)y * w * 3], 1, (size_t)w * 3, f);
            std::fclose(f);
        }
    }

    Editor& editor_;
    const char* shotPath_ = std::getenv("TWISTED_SCREENSHOT");
    int shotFrame_ = std::getenv("TWISTED_SCREENSHOT_FRAME") ? std::atoi(std::getenv("TWISTED_SCREENSHOT_FRAME")) : 180;
    int frames_ = 0;
    Window parent_ = 0, win_ = 0;
    float scale_ = 1.f;
    std::thread thread_;
    std::atomic<bool> running_{false}, ok_{false}, started_{false};
};
} // namespace

std::unique_ptr<PlatformView> PlatformView::create(Editor& editor) { return std::make_unique<X11View>(editor); }

int runStandaloneWindow(PlatformView& view, const char* title) {
    (void)title;
    if (!view.attach(nullptr)) return 1;
    // No window manager integration: runs for TWISTED_PREVIEW_SECONDS (default: until killed).
    const char* secs = std::getenv("TWISTED_PREVIEW_SECONDS");
    const double limit = secs ? std::atof(secs) : 1e9;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < limit)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    view.detach();
    return 0;
}

} // namespace tw
