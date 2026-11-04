// Hôte VST3 minimal : charge RAW2, relie processeur et contrôleur (directement, puis par messages
// seulement), joue des notes, automatise un paramètre, sauvegarde et recharge l'état.
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "pluginterfaces/gui/iplugview.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX  // sinon windows.h définit les macros min / max et casse std::max
#endif
#include <algorithm>
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>  // compilé en Objective-C++ sous macOS (fenêtre de l'éditeur)
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Steinberg {
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IConnectionPoint)
DEF_CLASS_IID(IMessage)
DEF_CLASS_IID(IAttributeList)
DEF_CLASS_IID(IHostApplication)
} // namespace Vst
} // namespace Steinberg

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("ÉCHEC ligne %d : %s\n", __LINE__, #c); ++failures; } } while (0)

#define REFS                                                                   \
    std::atomic<uint32> refs{1};                                               \
    uint32 PLUGIN_API addRef() override { return ++refs; }                     \
    uint32 PLUGIN_API release() override { const uint32 r = --refs; if (!r) delete this; return r; }

struct Attrs : IAttributeList {
    REFS
    std::map<std::string, int64> ints;
    tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
    tresult PLUGIN_API setInt(AttrID id, int64 v) override { ints[id] = v; return kResultOk; }
    tresult PLUGIN_API getInt(AttrID id, int64& v) override { auto it = ints.find(id); if (it == ints.end()) return kResultFalse; v = it->second; return kResultOk; }
    tresult PLUGIN_API setFloat(AttrID, double) override { return kResultOk; }
    tresult PLUGIN_API getFloat(AttrID, double&) override { return kResultFalse; }
    tresult PLUGIN_API setString(AttrID, const TChar*) override { return kResultOk; }
    tresult PLUGIN_API getString(AttrID, TChar*, uint32) override { return kResultFalse; }
    tresult PLUGIN_API setBinary(AttrID, const void*, uint32) override { return kResultOk; }
    tresult PLUGIN_API getBinary(AttrID, const void*&, uint32&) override { return kResultFalse; }
};

struct Message : IMessage {
    REFS
    std::string id;
    Attrs* attrs = new Attrs();
    ~Message() { attrs->release(); }
    tresult PLUGIN_API queryInterface(const TUID iid, void** o) override {
        if (FUnknownPrivate::iidEqual(iid, IMessage::iid)) { *o = this; addRef(); return kResultOk; }
        *o = nullptr; return kNoInterface;
    }
    FIDString PLUGIN_API getMessageID() override { return id.c_str(); }
    void PLUGIN_API setMessageID(FIDString m) override { id = m; }
    IAttributeList* PLUGIN_API getAttributes() override { return attrs; }
};

struct HostApp : IHostApplication {
    REFS
    tresult PLUGIN_API queryInterface(const TUID iid, void** o) override {
        if (FUnknownPrivate::iidEqual(iid, IHostApplication::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) { *o = this; addRef(); return kResultOk; }
        *o = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API getName(String128 name) override { name[0] = 'T'; name[1] = 0; return kResultOk; }
    tresult PLUGIN_API createInstance(TUID cid, TUID, void** o) override {
        if (FUnknownPrivate::iidEqual(cid, IMessage::iid)) { *o = static_cast<IMessage*>(new Message()); return kResultOk; }
        *o = nullptr; return kNoInterface;
    }
};

// relais qui ne laisse passer que notify() : simule un hôte qui ne relie pas directement
struct Proxy : IConnectionPoint {
    REFS
    IConnectionPoint* target;
    explicit Proxy(IConnectionPoint* t) : target(t) {}
    tresult PLUGIN_API queryInterface(const TUID iid, void** o) override {
        if (FUnknownPrivate::iidEqual(iid, IConnectionPoint::iid)) { *o = this; addRef(); return kResultOk; }
        *o = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API connect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API disconnect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API notify(IMessage* m) override { return target->notify(m); }
};

struct Stream : IBStream {
    REFS
    std::string data;
    size_t pos = 0;
    tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
    tresult PLUGIN_API read(void* b, int32 n, int32* got) override {
        const size_t k = std::min<size_t>((size_t)n, data.size() - pos);
        std::memcpy(b, data.data() + pos, k); pos += k; if (got) *got = (int32)k; return kResultOk;
    }
    tresult PLUGIN_API write(void* b, int32 n, int32* w) override { data.append((const char*)b, (size_t)n); if (w) *w = n; return kResultOk; }
    tresult PLUGIN_API seek(int64 p, int32, int64* r) override { pos = (size_t)p; if (r) *r = p; return kResultOk; }
    tresult PLUGIN_API tell(int64* p) override { *p = (int64)pos; return kResultOk; }
};

struct Events : IEventList {
    REFS
    std::vector<Event> ev;
    tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
    int32 PLUGIN_API getEventCount() override { return (int32)ev.size(); }
    tresult PLUGIN_API getEvent(int32 i, Event& e) override { e = ev[(size_t)i]; return kResultOk; }
    tresult PLUGIN_API addEvent(Event& e) override { ev.push_back(e); return kResultOk; }
};

struct Queue : IParamValueQueue {
    REFS
    ParamID id; ParamValue v;
    Queue(ParamID i, ParamValue val) : id(i), v(val) {}
    tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32, int32& off, ParamValue& val) override { off = 0; val = v; return kResultOk; }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
};

struct Changes : IParameterChanges {
    REFS
    std::vector<Queue*> q;
    ~Changes() { for (auto* x : q) x->release(); }
    tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
    int32 PLUGIN_API getParameterCount() override { return (int32)q.size(); }
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return q[(size_t)i]; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
};

struct PlugFrame : IPlugFrame {
    REFS
    int resizes = 0;
    tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
    tresult PLUGIN_API resizeView(IPlugView* v, ViewRect* r) override { ++resizes; v->onSize(r); return kResultOk; }
};

#if defined(_WIN32)
// Ouvre l'éditeur dans une fenêtre pendant quelques secondes (capture d'écran possible).
static void openEditor(IEditController* ctrl, int seconds) {
    IPlugView* view = ctrl->createView("editor");
    CHECK(view != nullptr);
    if (!view) return;
    CHECK(view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue);
    ViewRect r;
    CHECK(view->getSize(&r) == kResultOk);
    std::printf("éditeur : %d x %d\n", r.right - r.left, r.bottom - r.top);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"HostTestWnd";
    RegisterClassW(&wc);
    HWND top = CreateWindowW(L"HostTestWnd", L"hote", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, r.right + 16, r.bottom + 39, nullptr, nullptr, wc.hInstance, nullptr);
    PlugFrame* frame = new PlugFrame();
    view->setFrame(frame);
    CHECK(view->attached(top, kPlatformTypeHWND) == kResultOk);
    const DWORD t0 = GetTickCount();
    MSG msg;
    while (GetTickCount() - t0 < (DWORD)seconds * 1000) {
        for (int k = 0; k < 50 && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++k) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(5);
    }
    std::printf("fermeture de l'éditeur\n");
    CHECK(view->removed() == kResultOk);
    std::printf("éditeur fermé\n");
    view->setFrame(nullptr);
    view->release();
    frame->release();
    DestroyWindow(top);
}
#elif defined(__APPLE__)
// Ouvre l'éditeur dans une NSWindow quelques secondes (boucle d'évènements tournée à la main),
// change le zoom pour vérifier le redimensionnement, puis le referme.
static void openEditor(IEditController* ctrl, int seconds) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        IPlugView* view = ctrl->createView("editor");
        CHECK(view != nullptr);
        if (!view) return;
        CHECK(view->isPlatformTypeSupported(kPlatformTypeNSView) == kResultTrue);
        CHECK(view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultFalse);
        ViewRect r;
        CHECK(view->getSize(&r) == kResultOk);
        std::printf("éditeur : %d x %d\n", r.right - r.left, r.bottom - r.top);
        NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, r.right - r.left, r.bottom - r.top)
                                                    styleMask:NSWindowStyleMaskTitled
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        [win setReleasedWhenClosed:NO];
        [win orderFront:nil];
        PlugFrame* frame = new PlugFrame();
        view->setFrame(frame);
        CHECK(view->attached((__bridge void*)[win contentView], kPlatformTypeNSView) == kResultOk);
        CHECK([[[win contentView] subviews] count] == 1);
        const double t0 = [[NSProcessInfo processInfo] systemUptime];
        while ([[NSProcessInfo processInfo] systemUptime] - t0 < seconds) {
            @autoreleasepool {
                NSEvent* e;
                while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.005] inMode:NSDefaultRunLoopMode dequeue:YES]))
                    [NSApp sendEvent:e];
            }
        }
        std::printf("fermeture de l'éditeur\n");
        CHECK(view->removed() == kResultOk);
        CHECK([[[win contentView] subviews] count] == 0);
        std::printf("éditeur fermé\n");
        view->setFrame(nullptr);
        view->release();
        frame->release();
        [win close];
    }
}
#endif

static void runOnce(IPluginFactory* f, bool direct) {
    HostApp* host = new HostApp();
    IComponent* comp = nullptr;
    PClassInfo ci;
    f->getClassInfo(0, &ci);
    CHECK(f->createInstance(ci.cid, IComponent::iid, (void**)&comp) == kResultOk && comp);
    CHECK(comp->initialize(host) == kResultOk);
    TUID ctrlId;
    CHECK(comp->getControllerClassId(ctrlId) == kResultOk);
    IEditController* ctrl = nullptr;
    CHECK(f->createInstance(ctrlId, IEditController::iid, (void**)&ctrl) == kResultOk && ctrl);
    ctrl->initialize(host);
    IConnectionPoint *cpA = nullptr, *cpB = nullptr;
    comp->queryInterface(IConnectionPoint::iid, (void**)&cpA);
    ctrl->queryInterface(IConnectionPoint::iid, (void**)&cpB);
    CHECK(cpA && cpB);
    Proxy* pA = new Proxy(cpA);
    Proxy* pB = new Proxy(cpB);
    if (direct) { cpA->connect(cpB); cpB->connect(cpA); }
    else { cpA->connect(pB); cpB->connect(pA); }

    IAudioProcessor* proc = nullptr;
    comp->queryInterface(IAudioProcessor::iid, (void**)&proc);
    CHECK(proc);
    CHECK(comp->getBusCount(kAudio, kOutput) == 1 && comp->getBusCount(kEvent, kInput) == 1);
    SpeakerArrangement out = SpeakerArr::kStereo;
    CHECK(proc->setBusArrangements(nullptr, 0, &out, 1) == kResultTrue);
    ProcessSetup setup{kRealtime, kSample32, 512, 44100.0};
    CHECK(proc->setupProcessing(setup) == kResultOk);
    comp->setActive(true);
    proc->setProcessing(true);

    std::vector<float> L(512), R(512);
    float* chans[2] = {L.data(), R.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = chans;
    float peak = 0.f;
    bool finite = true;
    for (int b = 0; b < 120; ++b) {
        Events* ev = new Events();
        if (b == 2) { Event e{}; e.type = Event::kNoteOnEvent; e.sampleOffset = 100; e.noteOn.pitch = 57; e.noteOn.velocity = 0.9f; ev->addEvent(e); }
        if (b == 80) { Event e{}; e.type = Event::kNoteOffEvent; e.sampleOffset = 5; e.noteOff.pitch = 57; ev->addEvent(e); }
        Changes* ch = new Changes();
        if (b == 10) ch->q.push_back(new Queue(14 /*master*/, 0.9));
        ProcessData d{};
        d.processMode = kRealtime;
        d.symbolicSampleSize = kSample32;
        d.numSamples = 512;
        d.numOutputs = 1;
        d.outputs = &bus;
        d.inputEvents = ev;
        d.inputParameterChanges = ch;
        CHECK(proc->process(d) == kResultOk);
        for (int i = 0; i < 512; ++i) {
            finite = finite && std::isfinite(L[(size_t)i]) && std::isfinite(R[(size_t)i]);
            peak = std::max(peak, std::fabs(L[(size_t)i]));
        }
        ev->release();
        ch->release();
    }
    CHECK(finite);
    CHECK(peak > 0.01f && peak <= 1.f);
    std::printf("%s : crête %.3f\n", direct ? "liaison directe" : "liaison par messages", peak);
    // le contrôleur voit la valeur automatisée (même modèle)
    CHECK(std::fabs(ctrl->getParamNormalized(14) - 0.9) < 1e-4);
    CHECK(ctrl->getParameterCount() > 20);
    String128 txt;
    CHECK(ctrl->getParamStringByValue(1, 1.0, txt) == kResultOk);

#if defined(_WIN32) || defined(__APPLE__)
    if (direct) openEditor(ctrl, 6);
#endif
    // état : sauvegarde puis rechargement dans une nouvelle instance
    Stream* s = new Stream();
    CHECK(comp->getState(s) == kResultOk && s->data.size() > 100);
    IComponent* comp2 = nullptr;
    f->createInstance(ci.cid, IComponent::iid, (void**)&comp2);
    comp2->initialize(host);
    s->pos = 0;
    CHECK(comp2->setState(s) == kResultOk);
    Stream* s2 = new Stream();
    comp2->getState(s2);
    CHECK(s2->data.find("\"master\":0.9") != std::string::npos);
    s->pos = 0;
    CHECK(ctrl->setComponentState(s) == kResultOk);
    comp2->terminate();
    comp2->release();
    s->release();
    s2->release();

    proc->setProcessing(false);
    comp->setActive(false);
    if (direct) { cpA->disconnect(cpB); cpB->disconnect(cpA); }
    else { cpA->disconnect(pB); cpB->disconnect(pA); }
    pA->release();
    pB->release();
    cpA->release();
    cpB->release();
    proc->release();
    ctrl->terminate();
    ctrl->release();
    comp->terminate();
    comp->release();
    host->release();
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    using GetFactory = IPluginFactory* (*)();
#if defined(_WIN32)
    HMODULE lib = LoadLibraryA(argv[1]);
    if (!lib) { std::printf("LoadLibrary : %lu\n", GetLastError()); return 1; }
    auto gf = (GetFactory)(void*)GetProcAddress(lib, "GetPluginFactory");
    auto init = (bool (*)())(void*)GetProcAddress(lib, "InitDll");
    CHECK(gf && init && init());
#else
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::printf("dlopen : %s\n", dlerror()); return 1; }
    auto gf = (GetFactory)dlsym(lib, "GetPluginFactory");
#if defined(__APPLE__)
    auto entry = (bool (*)(void*))dlsym(lib, "bundleEntry");  // un hôte passe le CFBundleRef
#else
    auto entry = (bool (*)(void*))dlsym(lib, "ModuleEntry");
#endif
    CHECK(gf && entry && entry(nullptr));
#endif
    IPluginFactory* f = gf();
    CHECK(f && f->countClasses() == 2);
    PFactoryInfo fi;
    f->getFactoryInfo(&fi);
    std::printf("fabricant : %s\n", fi.vendor);
    runOnce(f, true);
    runOnce(f, false);
#if defined(_WIN32)
    auto exitFn = (bool (*)())(void*)GetProcAddress(lib, "ExitDll");
#elif defined(__APPLE__)
    auto exitFn = (bool (*)())dlsym(lib, "bundleExit");
#else
    auto exitFn = (bool (*)())dlsym(lib, "ModuleExit");
#endif
    if (exitFn) exitFn();
    std::printf(failures ? "%d échec(s)\n" : "hôte de test : tout passe\n", failures);
    return failures ? 1 : 0;
}
