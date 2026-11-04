#include "platform/Win32View.h"
#include "ui/D2DCanvas.h"

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <ole2.h>
#include <oleidl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <cmath>

namespace raw2 {

namespace {

const wchar_t* kClassName = L"RAW2EditorView";
constexpr UINT_PTR kTimerId = 1;

HINSTANCE moduleHandle() {
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&moduleHandle, &h);
    return h;
}

double nowMs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

void appendUtf8(std::string& o, uint32_t c) {
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    for (auto& ch : w)
        if (ch == L'/') ch = L'\\';
    return w;
}

// --- glisser-déposer sortant : un fichier (CF_HDROP) vers la DAW ou l'Explorateur ---------------
class FileDataObject final : public IDataObject {
public:
    explicit FileDataObject(const std::wstring& path) : path_(path) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** obj) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *obj = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *obj = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f, STGMEDIUM* m) override {
        if (QueryGetData(f) != S_OK) return DV_E_FORMATETC;
        const size_t bytes = sizeof(DROPFILES) + (path_.size() + 2) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GHND, bytes);
        if (!h) return E_OUTOFMEMORY;
        auto* df = static_cast<DROPFILES*>(GlobalLock(h));
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        std::memcpy(reinterpret_cast<char*>(df) + sizeof(DROPFILES), path_.c_str(), (path_.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        m->tymed = TYMED_HGLOBAL;
        m->hGlobal = h;
        m->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f) override {
        return f && f->cfFormat == CF_HDROP && (f->tymed & TYMED_HGLOBAL) && f->dwAspect == DVASPECT_CONTENT ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) out->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** e) override {
        if (dir != DATADIR_GET) return E_NOTIMPL;
        FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        return SHCreateStdEnumFmtEtc(1, &f, e);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    LONG refs_ = 1;
    std::wstring path_;
};

class FileDropSource final : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** obj) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *obj = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *obj = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL esc, DWORD keys) override {
        if (esc) return DRAGDROP_S_CANCEL;
        if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    LONG refs_ = 1;
};

std::vector<std::string> filesFromDrop(HDROP drop) {
    std::vector<std::string> out;
    const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        const UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring w((size_t)len + 1, L'\0');
        DragQueryFileW(drop, i, w.data(), len + 1);
        w.resize(len);
        out.push_back(narrow(w));
    }
    return out;
}

} // namespace

// --- cible de glisser-déposer OLE (survol + dépôt) ---------------------------------------------
class DropTarget final : public IDropTarget {
public:
    explicit DropTarget(Win32View::Host*, std::function<void(int kind, POINTL pt, IDataObject*)> cb) : cb_(std::move(cb)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** obj) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *obj = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *obj = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL pt, DWORD* effect) override {
        FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        accept_ = data && data->QueryGetData(&fmt) == S_OK;
        *effect = accept_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (accept_) cb_(0, pt, nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* effect) override {
        *effect = accept_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (accept_) cb_(0, pt, nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        cb_(1, POINTL{0, 0}, nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL pt, DWORD* effect) override {
        *effect = accept_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (accept_) cb_(2, pt, data);
        else cb_(1, pt, nullptr);
        return S_OK;
    }

private:
    LONG refs_ = 1;
    bool accept_ = false;
    std::function<void(int, POINTL, IDataObject*)> cb_;
};

struct Win32View::Impl {
    Model& model;
    ParamSink& sink;
    ui::EditorState& state;
    Host* host;
    ui::Editor editor;
    ui::D2DCanvas canvas;
    ui::Input input;
    HWND hwnd = nullptr;
    std::string dragOutPending;  // WAV à glisser vers la DAW (demandé par l'éditeur)
    ID2D1HwndRenderTarget* rt = nullptr;
    float contentScale = 0.f;  // 0 : pas fourni par l'hôte -> DPI de la fenêtre
    int zoomShown = 100;
    int width = 1280, height = 960;
    bool oleOk = false;
    DropTarget* drop = nullptr;
    HWND prevFocus = nullptr;
    bool tracking = false;

    Impl(Model& m, ParamSink& s, ui::EditorState& st, Host* h) : model(m), sink(s), state(st), host(h), editor(m, s, st) {}

    float dpiScale() const {
        if (contentScale > 0.f) return contentScale;
        if (!hwnd) return 1.f;
        typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
        static GetDpiForWindowFn fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        const UINT dpi = fn ? fn(hwnd) : 96;
        return dpi > 0 ? (float)dpi / 96.f : 1.f;
    }
    void wantedSize(int& w, int& h) const {
        const float s = (float)model.zoomPct.load() / 100.f * dpiScale();
        w = (int)std::lround(ui::Editor::kWidth * s);
        h = (int)std::lround(ui::Editor::kHeight * s);
    }
    float scale() const { return (float)width / (float)ui::Editor::kWidth; }
    ui::Pt toLogical(int x, int y) const { return ui::Pt{(float)x / scale(), (float)y / scale()}; }

    void render() {

        if (!rt) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
            D2D1_HWND_RENDER_TARGET_PROPERTIES hp = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU((UINT32)std::max(1L, rc.right - rc.left), (UINT32)std::max(1L, rc.bottom - rc.top)), D2D1_PRESENT_OPTIONS_IMMEDIATELY);
            if (FAILED(ui::D2DShared::get().d2d->CreateHwndRenderTarget(props, hp, &rt)) || !rt) return;
        }
        input.timeMs = nowMs();
        rt->BeginDraw();
        canvas.begin(rt, scale());
        input.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        editor.frame(canvas, input);
        canvas.end();
        dragOutPending = editor.takeDragOut();
        const HRESULT hr = rt->EndDraw();
        if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
            canvas.releaseDeviceResources();
            rt->Release();
            rt = nullptr;
        }
        // les évènements ponctuels ne valent que pour une image
        input.pressed = input.released = input.rightPressed = false;
        input.wheel = 0.f;
        input.typed.clear();
        input.keyEsc = input.keyEnter = input.keyBack = input.keyDelete = false;
        input.keyLeft = input.keyRight = input.keyHome = input.keyEnd = false;
        input.dropped.clear();
        if (!dragOutPending.empty()) {  // glisser du WAV vers la DAW (boucle modale d'OLE)
            const std::wstring w = widen(dragOutPending);
            dragOutPending.clear();
            if (oleOk) {
                auto* data = new FileDataObject(w);
                auto* src = new FileDropSource();
                DWORD effect = 0;
                DoDragDrop(data, src, DROPEFFECT_COPY, &effect);
                data->Release();
                src->Release();
            }
            input.down = false;
            input.released = true;
        }
        // le clavier n'est gardé que pendant la saisie (sinon il revient à l'hôte, pour FL Studio)
        if (GetFocus() == hwnd && !editor.wantsKeyboard() && !editor.anyOverlayOpen() && !input.down) {
            HWND back = prevFocus && IsWindow(prevFocus) ? prevFocus : GetParent(hwnd);
            prevFocus = nullptr;
            if (back) SetFocus(back);
        }
        // changement de zoom demandé dans le panneau des thèmes
        if (model.zoomPct.load() != zoomShown) {
            zoomShown = model.zoomPct.load();
            int w, h;
            wantedSize(w, h);
            if (host) host->requestResize(w, h);
            else setSize(w, h);
        }
    }

    void setSize(int w, int h) {
        width = std::max(1, w);
        height = std::max(1, h);
        if (hwnd) SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
        if (rt) rt->Resize(D2D1::SizeU((UINT32)width, (UINT32)height));
    }

    void grabFocus() {
        if (GetFocus() != hwnd) {
            prevFocus = GetFocus();
            SetFocus(hwnd);
        }
    }

    bool key(int vk, bool down) {
        if (!down) return false;
        switch (vk) {
            case VK_ESCAPE: input.keyEsc = true; return true;
            case VK_RETURN: input.keyEnter = true; return true;
            case VK_BACK: input.keyBack = true; return true;
            case VK_DELETE: input.keyDelete = true; return true;
            case VK_LEFT: input.keyLeft = true; return true;
            case VK_RIGHT: input.keyRight = true; return true;
            case VK_HOME: input.keyHome = true; return true;
            case VK_END: input.keyEnd = true; return true;
            default: return false;
        }
    }

    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        Impl* self = (Impl*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (msg == WM_NCCREATE) {
            self = (Impl*)((CREATESTRUCTW*)lp)->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
            case WM_TIMER: InvalidateRect(h, nullptr, FALSE); return 0;
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(h, &ps);
                self->render();
                EndPaint(h, &ps);
                return 0;
            }
            case WM_SIZE: {
                const int w = LOWORD(lp), hh = HIWORD(lp);
                if (w > 0 && hh > 0) {
                    self->width = w;
                    self->height = hh;
                    if (self->rt) self->rt->Resize(D2D1::SizeU((UINT32)w, (UINT32)hh));
                }
                return 0;
            }
            case WM_MOUSEMOVE: {
                self->input.mouse = self->toLogical(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                if (!self->tracking) {
                    TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
                    TrackMouseEvent(&t);
                    self->tracking = true;
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                self->tracking = false;
                if (!self->input.down) self->input.mouse = ui::Pt{-1000.f, -1000.f};
                return 0;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
                self->input.mouse = self->toLogical(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                self->input.down = true;
                self->input.pressed = true;
                SetCapture(h);
                self->grabFocus();
                return 0;
            case WM_LBUTTONUP:
                self->input.mouse = self->toLogical(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                self->input.down = false;
                self->input.released = true;
                ReleaseCapture();
                return 0;
            case WM_CAPTURECHANGED:
                if (self->input.down) {
                    self->input.down = false;
                    self->input.released = true;
                }
                return 0;
            case WM_RBUTTONDOWN: self->input.rightPressed = true; return 0;
            case WM_MOUSEWHEEL: self->input.wheel += (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA; return 0;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    LPCWSTR c = IDC_ARROW;
                    switch (self->editor.cursor()) {
                        case ui::Cursor::Hand: c = IDC_HAND; break;
                        case ui::Cursor::ResizeNS: c = IDC_SIZENS; break;
                        case ui::Cursor::ResizeEW: c = IDC_SIZEWE; break;
                        case ui::Cursor::Cross: c = IDC_CROSS; break;
                        case ui::Cursor::Text: c = IDC_IBEAM; break;
                        default: break;
                    }
                    SetCursor(LoadCursorW(nullptr, c));
                    return TRUE;
                }
                break;
            case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
            case WM_KEYDOWN:
                if (self->key((int)wp, true)) return 0;
                break;
            case WM_CHAR:
                if (wp >= 0x20 && wp != 0x7F && self->editor.wantsKeyboard()) {
                    static wchar_t high = 0;
                    const wchar_t ch = (wchar_t)wp;
                    if (ch >= 0xD800 && ch < 0xDC00) { high = ch; return 0; }
                    uint32_t cp = ch;
                    if (ch >= 0xDC00 && ch < 0xE000 && high) cp = 0x10000 + (((uint32_t)high - 0xD800) << 10) + (ch - 0xDC00);
                    high = 0;
                    appendUtf8(self->input.typed, cp);
                    return 0;
                }
                break;
            case WM_DROPFILES: {
                HDROP d = (HDROP)wp;
                POINT pt;
                DragQueryPoint(d, &pt);
                self->input.dropped = filesFromDrop(d);
                self->input.dropPos = self->toLogical(pt.x, pt.y);
                self->input.dragOver = false;
                DragFinish(d);
                return 0;
            }
            default: break;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    void dropEvent(int kind, POINTL p, IDataObject* data) {
        POINT pt{p.x, p.y};
        ScreenToClient(hwnd, &pt);
        if (kind == 0) {
            input.dragOver = true;
            input.dragPos = toLogical(pt.x, pt.y);
        } else if (kind == 1) {
            input.dragOver = false;
        } else if (data) {
            input.dragOver = false;
            FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM med{};
            if (data->GetData(&fmt, &med) == S_OK) {
                HDROP d = (HDROP)GlobalLock(med.hGlobal);
                if (d) {
                    input.dropped = filesFromDrop(d);
                    input.dropPos = toLogical(pt.x, pt.y);
                    GlobalUnlock(med.hGlobal);
                }
                ReleaseStgMedium(&med);
            }
        }
    }
};

Win32View::Win32View(Model& model, ParamSink& sink, ui::EditorState& state, Host* host) : d_(std::make_unique<Impl>(model, sink, state, host)) {}

Win32View::~Win32View() { detach(); }

bool Win32View::attach(void* parent) {
    if (!ui::D2DShared::get().ok) return false;
    const HINSTANCE inst = moduleHandle();
    WNDCLASSEXW wc{};
    if (!GetClassInfoExW(inst, kClassName, &wc)) {
        wc = WNDCLASSEXW{};
        wc.cbSize = sizeof wc;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Impl::proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) return false;
    }
    Impl& d = *d_;
    d.zoomShown = d.model.zoomPct.load();
    d.hwnd = CreateWindowExW(0, kClassName, L"RAW2", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 10, 10, (HWND)parent, nullptr, inst, &d);
    if (!d.hwnd) return false;
    hwnd_ = d.hwnd;
    int w, h;
    d.wantedSize(w, h);
    d.setSize(w, h);
    // glisser-déposer : OLE (avec survol) si possible, sinon WM_DROPFILES
    d.oleOk = SUCCEEDED(OleInitialize(nullptr));
    if (d.oleOk) {
        Impl* self = &d;
        d.drop = new DropTarget(nullptr, [self](int k, POINTL p, IDataObject* data) { self->dropEvent(k, p, data); });
        if (FAILED(RegisterDragDrop(d.hwnd, d.drop))) {
            d.drop->Release();
            d.drop = nullptr;
            DragAcceptFiles(d.hwnd, TRUE);
        }
    } else {
        DragAcceptFiles(d.hwnd, TRUE);
    }
    SetTimer(d.hwnd, kTimerId, 15, nullptr);
    return true;
}

void Win32View::detach() {
    Impl& d = *d_;
    if (!d.hwnd) return;
    KillTimer(d.hwnd, kTimerId);
    if (d.drop) {
        RevokeDragDrop(d.hwnd);
        d.drop->Release();
        d.drop = nullptr;
    }
    if (d.rt) {
        d.canvas.releaseDeviceResources();
        d.rt->Release();
        d.rt = nullptr;
    }
    DestroyWindow(d.hwnd);
    d.hwnd = nullptr;
    hwnd_ = nullptr;
    if (d.oleOk) {
        OleUninitialize();
        d.oleOk = false;
    }
}

void Win32View::setContentScale(float s) {
    d_->contentScale = s > 0.f ? s : 0.f;
    int w, h;
    d_->wantedSize(w, h);
    d_->setSize(w, h);
}

void Win32View::preferredSize(int& w, int& h) const { d_->wantedSize(w, h); }
void Win32View::setSize(int w, int h) { d_->setSize(w, h); }

bool Win32View::hostKey(char16_t ch, int vk) {
    Impl& d = *d_;
    if (!d.editor.wantsKeyboard() && !d.editor.anyOverlayOpen()) return false;
    if (vk && d.key(vk, true)) return true;
    if (ch >= 0x20 && d.editor.wantsKeyboard()) {
        appendUtf8(d.input.typed, (uint32_t)ch);
        return true;
    }
    return false;
}

} // namespace raw2
