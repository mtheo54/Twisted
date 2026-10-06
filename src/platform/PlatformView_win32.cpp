// Windows: child HWND with an OpenGL 3.2 core context (4x MSAA when available).
#include "PlatformView.h"

#include "../ui/Editor.h"
#include "../ui/Gl.h"

#include <cstdio>
#include <string>

namespace tw {

namespace {
// WGL_ARB_create_context / WGL_ARB_pixel_format constants
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091, WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126, WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x0001;
constexpr int WGL_DRAW_TO_WINDOW_ARB = 0x2001, WGL_SUPPORT_OPENGL_ARB = 0x2010, WGL_DOUBLE_BUFFER_ARB = 0x2011;
constexpr int WGL_PIXEL_TYPE_ARB = 0x2013, WGL_TYPE_RGBA_ARB = 0x202B, WGL_COLOR_BITS_ARB = 0x2014;
constexpr int WGL_DEPTH_BITS_ARB = 0x2022, WGL_SAMPLE_BUFFERS_ARB = 0x2041, WGL_SAMPLES_ARB = 0x2042;
constexpr int WGL_ACCELERATION_ARB = 0x2003, WGL_FULL_ACCELERATION_ARB = 0x2027;

typedef HGLRC(WINAPI* PFNCreateContextAttribs)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFNChoosePixelFormat)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);

HMODULE gOpenGl = nullptr;
void* getProc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (p == nullptr || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1) {
        if (!gOpenGl) gOpenGl = LoadLibraryA("opengl32.dll");
        p = (void*)GetProcAddress(gOpenGl, name);
    }
    return p;
}

HMODULE thisModule() {
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&thisModule, &m);
    return m;
}

// Un caractère UTF-16 (parfois une paire de substituts pour un seul caractère au-delà du
// plan de base) converti en UTF-8, pour Editor::textInput. Les clés de licence, e-mails et
// mots de passe restent presque toujours du BMP, mais on traite les paires correctement
// par politesse plutôt que de les ignorer en silence.
std::string utf16ToUtf8(const wchar_t* w, int n) {
    if (n <= 0) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(size_t(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, n, out.data(), len, nullptr, nullptr);
    return out;
}

// Presse-papiers (Ctrl+V) : texte Unicode uniquement, jamais de saut de ligne au milieu
// (Editor::textInput filtre déjà les caractères de contrôle, mais autant ne pas lire plus
// que nécessaire).
std::string clipboardText(HWND owner) {
    if (!OpenClipboard(owner)) return {};
    std::string out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* w = (const wchar_t*)GlobalLock(h)) {
            out = utf16ToUtf8(w, (int)wcslen(w));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

float systemScale(HWND hwnd) {
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
    if (fn && hwnd) {
        const UINT dpi = fn(hwnd);
        if (dpi) return dpi / 96.f;
    }
    HDC dc = GetDC(nullptr);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return dpi > 0 ? dpi / 96.f : 1.f;
}

class Win32View final : public PlatformView {
public:
    explicit Win32View(Editor& e) : editor_(e) {}
    ~Win32View() override { detach(); }

    void setScale(float s) override { hostScale_ = s; }
    void size(int& w, int& h) const override {
        const float s = scale();
        w = (int)(Editor::kWidth * s + 0.5f);
        h = (int)(Editor::kHeight * s + 0.5f);
    }

    bool attach(void* parent) override {
        detach();
        registerClass();
        parent_ = (HWND)parent;
        int w, h;
        size(w, h);
        hwnd_ = CreateWindowExW(0, className_, L"Twisted", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                                w, h, parent_, nullptr, thisModule(), this);
        if (!hwnd_) return false;
        dc_ = GetDC(hwnd_);
        if (!createContext()) return false;
        wglMakeCurrent(dc_, ctx_);
        if (!loadGl(&getProc)) return false;
        // No vsync wait: rendering runs on the host's UI thread and must never block it.
        typedef BOOL(WINAPI * SwapIntervalFn)(int);
        if (auto swap = (SwapIntervalFn)(void*)wglGetProcAddress("wglSwapIntervalEXT")) swap(0);
        editor_.glInit(scale());
        SetTimer(hwnd_, 1, 15, nullptr);
        return true;
    }

    void detach() override {
        if (!hwnd_) return;
        KillTimer(hwnd_, 1);
        if (ctx_) {
            wglMakeCurrent(dc_, ctx_);
            editor_.glDestroy();
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(ctx_);
            ctx_ = nullptr;
        }
        ReleaseDC(hwnd_, dc_);
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

private:
    float scale() const { return hostScale_ > 0 ? hostScale_ : systemScale(parent_); }

    void registerClass() {
        // Unique class name per loaded module, so two builds of the plugin never collide.
        swprintf(className_, 64, L"TwistedGLView_%p", (void*)thisModule());
        WNDCLASSEXW wc{};
        if (GetClassInfoExW(thisModule(), className_, &wc)) return;
        wc.cbSize = sizeof wc;
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = &Win32View::wndProc;
        wc.hInstance = thisModule();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = className_;
        RegisterClassExW(&wc);
    }

    bool createContext() {
        // A dummy window and context give us the WGL ARB entry points (MSAA pixel format, core profile).
        PFNChoosePixelFormat choosePf = nullptr;
        PFNCreateContextAttribs createCtx = nullptr;
        {
            HWND dummy = CreateWindowExW(0, className_, L"", WS_CHILD, 0, 0, 1, 1, hwnd_, nullptr, thisModule(), nullptr);
            HDC ddc = GetDC(dummy);
            PIXELFORMATDESCRIPTOR pfd{};
            pfd.nSize = sizeof pfd;
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = 32;
            pfd.cDepthBits = 24;
            SetPixelFormat(ddc, ChoosePixelFormat(ddc, &pfd), &pfd);
            HGLRC tmp = wglCreateContext(ddc);
            wglMakeCurrent(ddc, tmp);
            choosePf = (PFNChoosePixelFormat)(void*)wglGetProcAddress("wglChoosePixelFormatARB");
            createCtx = (PFNCreateContextAttribs)(void*)wglGetProcAddress("wglCreateContextAttribsARB");
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(tmp);
            ReleaseDC(dummy, ddc);
            DestroyWindow(dummy);
        }
        int format = 0;
        UINT count = 0;
        if (choosePf) {
            const int attrs[] = {WGL_DRAW_TO_WINDOW_ARB, 1, WGL_SUPPORT_OPENGL_ARB, 1, WGL_DOUBLE_BUFFER_ARB, 1,
                                 WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB, WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
                                 WGL_COLOR_BITS_ARB, 32, WGL_DEPTH_BITS_ARB, 24, WGL_SAMPLE_BUFFERS_ARB, 1, WGL_SAMPLES_ARB, 4, 0};
            if (!choosePf(dc_, attrs, nullptr, 1, &format, &count) || count == 0) format = 0;
        }
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof pfd;
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        if (!format) format = ChoosePixelFormat(dc_, &pfd);
        DescribePixelFormat(dc_, format, sizeof pfd, &pfd);
        if (!SetPixelFormat(dc_, format, &pfd)) return false;
        if (createCtx) {
            const int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 2,
                                 WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
            ctx_ = createCtx(dc_, nullptr, attrs);
        }
        if (!ctx_) ctx_ = wglCreateContext(dc_);  // legacy fallback (drivers usually give 3.x compatibility)
        return ctx_ != nullptr;
    }

    void paint() {
        if (!ctx_) return;
        wglMakeCurrent(dc_, ctx_);
        RECT r;
        GetClientRect(hwnd_, &r);
        editor_.render(r.right - r.left, r.bottom - r.top);
        SwapBuffers(dc_);
    }

    void toLogical(LPARAM lp, float& x, float& y) const {
        const float s = scale();
        x = (short)LOWORD(lp) / s;
        y = (short)HIWORD(lp) / s;
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* cs = (CREATESTRUCTW*)lp;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        }
        auto* self = (Win32View*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!self || self->hwnd_ != hwnd) return DefWindowProcW(hwnd, msg, wp, lp);
        float x, y;
        switch (msg) {
        case WM_TIMER:
            self->paint();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            self->paint();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            SetFocus(hwnd);
            self->toLogical(lp, x, y);
            self->editor_.mouseDown(x, y, (wp & MK_SHIFT) != 0);
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            self->toLogical(lp, x, y);
            self->editor_.mouseUp(x, y);
            return 0;
        case WM_MOUSEMOVE: {
            if (!self->tracking_) {
                TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                self->tracking_ = true;
            }
            self->toLogical(lp, x, y);
            self->editor_.mouseMove(x, y, (wp & MK_SHIFT) != 0);
            return 0;
        }
        case WM_MOUSELEAVE:
            self->tracking_ = false;
            self->editor_.mouseLeave();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            ScreenToClient(hwnd, &p);
            const float s = self->scale();
            self->editor_.mouseWheel(p.x / s, p.y / s, (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA,
                                     (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0);
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS;
        // ---- saisie clavier pour le panneau de licence (sans effet si aucun champ n'a le focus) ----
        case WM_CHAR: {
            if (!self->editor_.wantsKeyboard()) break;
            const wchar_t c = (wchar_t)wp;
            if (c < 0x20) break;  // Entrée/Retour/Tab/Échap arrivent par WM_KEYDOWN, pas ici
            if (c >= 0xD800 && c <= 0xDBFF) { self->pendingHighSurrogate_ = c; return 0; }  // substitut haut
            if (c >= 0xDC00 && c <= 0xDFFF) {  // substitut bas : combine avec celui d'avant
                if (self->pendingHighSurrogate_) {
                    const wchar_t pair[2] = {self->pendingHighSurrogate_, c};
                    self->editor_.textInput(utf16ToUtf8(pair, 2));
                    self->pendingHighSurrogate_ = 0;
                }
                return 0;
            }
            self->pendingHighSurrogate_ = 0;
            self->editor_.textInput(utf16ToUtf8(&c, 1));
            return 0;
        }
        case WM_KEYDOWN: {
            if (!self->editor_.wantsKeyboard()) break;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl && wp == 'V') { self->editor_.textInput(clipboardText(hwnd)); return 0; }
            switch (wp) {
                case VK_BACK: self->editor_.keyCommand(Editor::KeyCmd::Backspace); return 0;
                case VK_RETURN: self->editor_.keyCommand(Editor::KeyCmd::Enter); return 0;
                case VK_ESCAPE: self->editor_.keyCommand(Editor::KeyCmd::Escape); return 0;
                case VK_TAB: self->editor_.keyCommand(Editor::KeyCmd::Tab); return 0;
                default: break;
            }
            break;
        }
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    Editor& editor_;
    HWND parent_ = nullptr, hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC ctx_ = nullptr;
    float hostScale_ = 0.f;
    bool tracking_ = false;
    wchar_t className_[64] = {};
    wchar_t pendingHighSurrogate_ = 0;  // WM_CHAR livre les caractères astraux en deux messages
};
} // namespace

std::unique_ptr<PlatformView> PlatformView::create(Editor& editor) { return std::make_unique<Win32View>(editor); }

static LRESULT CALLBACK topWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int runStandaloneWindow(PlatformView& view, const char* title) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &topWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"TwistedPreview";
    RegisterClassExW(&wc);
    view.setScale(systemScale(nullptr));
    int w, h;
    view.size(w, h);
    RECT r{0, 0, w, h};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    wchar_t wtitle[128];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 128);
    HWND top = CreateWindowExW(0, wc.lpszClassName, wtitle, style | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                               r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!top || !view.attach(top)) return 1;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    view.detach();
    return 0;
}

} // namespace tw
