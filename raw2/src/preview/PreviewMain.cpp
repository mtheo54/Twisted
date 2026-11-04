// RAW2Preview : l'interface et le son de RAW 2 sans DAW (Windows). Sortie audio WASAPI partagée,
// notes jouées avec le clavier à l'écran. Pratique pour vérifier l'interface et les presets.
#include "core/Model.h"
#include "dsp/Engine.h"
#include "platform/Win32View.h"

#include <algorithm>
#include <windows.h>
#include <objbase.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

using namespace raw2;

namespace {

struct DirectSink : ParamSink {
    Model& m;
    explicit DirectSink(Model& mm) : m(mm) {}
    void beginEdit(uint32_t) override {}
    void performEdit(uint32_t id, float v) override { m.params.set(id, v); }
    void endEdit(uint32_t) override {}
};

// {00000003-0000-0010-8000-00AA00389B71} : sous-format « float » de WAVE_FORMAT_EXTENSIBLE
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

class AudioOut {
public:
    AudioOut(Model& m, Engine& e) : model_(m), engine_(e) {}
    ~AudioOut() { stop(); }
    void start() { thread_ = std::thread([this] { run(); }); }
    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    void run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IMMDeviceEnumerator* en = nullptr;
        IMMDevice* dev = nullptr;
        IAudioClient* client = nullptr;
        IAudioRenderClient* render = nullptr;
        WAVEFORMATEX* fmt = nullptr;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) &&
                  SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
                  SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) && SUCCEEDED(client->GetMixFormat(&fmt));
        bool isFloat = false;
        if (ok) {
            isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                      (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && IsEqualGUID(((WAVEFORMATEXTENSIBLE*)fmt)->SubFormat, kSubtypeFloat));
            ok = isFloat && fmt->wBitsPerSample == 32 &&
                 SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, 0, fmt, nullptr)) &&
                 SUCCEEDED(client->SetEventHandle(ev)) && SUCCEEDED(client->GetService(__uuidof(IAudioRenderClient), (void**)&render));
        }
        UINT32 bufferFrames = 0;
        if (ok) ok = SUCCEEDED(client->GetBufferSize(&bufferFrames));
        if (ok) {
            const int channels = fmt->nChannels;
            const int block = 256;
            engine_.prepare((double)fmt->nSamplesPerSec, block);
            std::vector<float> L(block), R(block);
            client->Start();
            while (running_) {
                WaitForSingleObject(ev, 200);
                UINT32 pad = 0;
                if (FAILED(client->GetCurrentPadding(&pad))) break;
                UINT32 frames = bufferFrames - pad;
                if (frames == 0) continue;
                BYTE* data = nullptr;
                if (FAILED(render->GetBuffer(frames, &data))) break;
                float* out = (float*)data;
                UINT32 done = 0;
                while (done < frames) {
                    const int n = (int)std::min<UINT32>(block, frames - done);
                    engine_.process(L.data(), R.data(), n, nullptr, 0);
                    for (int i = 0; i < n; ++i) {
                        float* f = out + (size_t)(done + (UINT32)i) * (size_t)channels;
                        for (int c = 0; c < channels; ++c) f[c] = c == 0 ? L[(size_t)i] : (c == 1 ? R[(size_t)i] : 0.f);
                    }
                    done += (UINT32)n;
                }
                render->ReleaseBuffer(frames, 0);
            }
            client->Stop();
        } else {
            // pas de carte son utilisable : le moteur tourne quand même pour animer l'interface
            engine_.prepare(48000.0, 256);
            std::vector<float> L(256), R(256);
            while (running_) {
                engine_.process(L.data(), R.data(), 256, nullptr, 0);
                Sleep(5);
            }
        }
        if (fmt) CoTaskMemFree(fmt);
        if (render) render->Release();
        if (client) client->Release();
        if (dev) dev->Release();
        if (en) en->Release();
        CloseHandle(ev);
        CoUninitialize();
    }

    Model& model_;
    Engine& engine_;
    std::atomic<bool> running_{true};
    std::thread thread_;
};

struct PreviewFrame : Win32View::Host {
    HWND top = nullptr;
    Win32View* view = nullptr;
    void requestResize(int w, int h) override {
        RECT r{0, 0, w, h};
        AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
        SetWindowPos(top, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
        if (view) view->setSize(w, h);
    }
};

LRESULT CALLBACK topProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    typedef BOOL(WINAPI * SetDpiCtx)(HANDLE);
    if (auto fn = (SetDpiCtx)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")) fn((HANDLE)-4);  // per-monitor v2

    Model model;
    Engine engine(model);
    DirectSink sink(model);
    ui::EditorState state;
    AudioOut audio(model, engine);
    audio.start();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = topProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"RAW2Preview";
    RegisterClassExW(&wc);
    PreviewFrame frame;
    frame.top = CreateWindowExW(0, wc.lpszClassName, L"RAW 2 \x2014 ABRASION", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                                CW_USEDEFAULT, 800, 600, nullptr, nullptr, inst, nullptr);
    Win32View view(model, sink, state, &frame);
    frame.view = &view;
    if (!view.attach(frame.top)) {
        MessageBoxW(frame.top, L"Direct2D indisponible.", L"RAW 2", MB_ICONERROR);
        return 1;
    }
    int w, h;
    view.preferredSize(w, h);
    frame.requestResize(w, h);
    ShowWindow(frame.top, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    view.detach();
    audio.stop();
    return 0;
}
