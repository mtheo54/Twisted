// RAW 2 en VST3, écrit directement sur les interfaces Steinberg (pluginterfaces, licence MIT).
// Deux classes, comme le recommande le SDK : un processeur (audio) et un contrôleur (paramètres,
// fenêtre). Ils partagent le même modèle en mémoire, transmis par IConnectionPoint.
#include "core/Model.h"
#include "dsp/Engine.h"
#include "ui/Editor.h"
#if defined(_WIN32)
#include "platform/Win32View.h"
#define RAW2_HAS_VIEW 1
#elif defined(__APPLE__)
#include "platform/MacView.h"
#define RAW2_HAS_VIEW 1
#endif

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/keycodes.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef RAW2_VERSION_STRING
#define RAW2_VERSION_STRING "2.0.0"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace raw2::vst {

// Objet commun au processeur et au contrôleur (compté par références).
struct Shared {
    std::atomic<int> refs{1};
    Model model;
    Engine engine{model};
    ui::EditorState uiState;
    void addRef() { refs.fetch_add(1); }
    void release() {
        if (refs.fetch_sub(1) == 1) delete this;
    }
};

// Interface privée : permet au contrôleur de récupérer le modèle quand l'hôte relie directement
// les deux objets (sinon, un message IMessage transporte l'adresse).
class IRaw2Peer : public FUnknown {
public:
    virtual Shared* PLUGIN_API shared() = 0;
    static const FUID iid;
};
DECLARE_CLASS_IID(IRaw2Peer, 0x3A9E61C2, 0x5B0D4F77, 0x9C21A8E4, 0x7D10B355)

} // namespace raw2::vst

namespace Steinberg {
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
DEF_CLASS_IID(IPlugViewContentScaleSupport)
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IMidiMapping)
DEF_CLASS_IID(INoteExpressionController)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IConnectionPoint)
DEF_CLASS_IID(IMessage)
DEF_CLASS_IID(IAttributeList)
DEF_CLASS_IID(IHostApplication)
} // namespace Vst
} // namespace Steinberg

namespace raw2::vst {
DEF_CLASS_IID(IRaw2Peer)

namespace {

// Identifiants de classe : ne jamais les changer après publication (les projets les mémorisent).
const TUID kProcessorUID = INLINE_UID(0x6E2D9A41, 0xB7C34E15, 0x8F0A21D6, 0x5C9B7E03);
const TUID kControllerUID = INLINE_UID(0x1F84C7B2, 0x3D6A4A90, 0xA5E2F017, 0xC48B26D9);
const char* kVendor = "ABRASION";
const char* kName = "RAW 2";

// pression par note : type de Note Expression propre (les hôtes MPE l'envoient aussi en PolyPressure)
constexpr NoteExpressionTypeID kRaw2PressureTypeID = kCustomStart + 1;

void toUtf16(const std::string& s, TChar* out, int maxLen) {
    int o = 0;
    for (size_t i = 0; i < s.size() && o < maxLen - 1;) {
        uint32_t c = (unsigned char)s[i++];
        if (c >= 0xC0) {
            const int extra = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : 1);
            c &= 0x3F >> extra;
            for (int k = 0; k < extra && i < s.size(); ++k) c = (c << 6) | ((unsigned char)s[i++] & 0x3F);
        }
        out[o++] = (TChar)(c < 0x10000 ? c : '?');
    }
    out[o] = 0;
}

std::string fromUtf16(const TChar* s) {
    std::string out;
    for (; s && *s; ++s) {
        const uint32_t c = (uint32_t)*s;
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
        else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::string readAll(IBStream* s) {
    std::string data;
    char buf[8192];
    int32 got = 0;
    while (s->read(buf, (int32)sizeof buf, &got) == kResultOk && got > 0) {
        data.append(buf, (size_t)got);
        if (data.size() > (size_t)64 * 1024 * 1024) break;
    }
    return data;
}

template <class T>
bool iidIs(const TUID iid) { return FUnknownPrivate::iidEqual(iid, T::iid); }

#define RAW2_REFCOUNT                                                     \
    uint32 PLUGIN_API addRef() override { return ++refs_; }              \
    uint32 PLUGIN_API release() override {                               \
        const uint32 r = --refs_;                                        \
        if (r == 0) delete this;                                         \
        return r;                                                        \
    }

// =================================================================================================
// Processeur
// =================================================================================================
class Processor final : public IComponent, public IAudioProcessor, public IConnectionPoint, public IRaw2Peer {
public:
    Processor() : shared_(new Shared()) {}
    ~Processor() {
        if (peer_) peer_->release();
        if (host_) host_->release();
        shared_->release();
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs<FUnknown>(iid) || iidIs<IPluginBase>(iid) || iidIs<IComponent>(iid)) *obj = static_cast<IComponent*>(this);
        else if (iidIs<IAudioProcessor>(iid)) *obj = static_cast<IAudioProcessor*>(this);
        else if (iidIs<IConnectionPoint>(iid)) *obj = static_cast<IConnectionPoint*>(this);
        else if (iidIs<IRaw2Peer>(iid)) *obj = static_cast<IRaw2Peer*>(this);
        else { *obj = nullptr; return kNoInterface; }
        addRef();
        return kResultOk;
    }
    RAW2_REFCOUNT

    Shared* PLUGIN_API shared() override { return shared_; }

    // IPluginBase
    tresult PLUGIN_API initialize(FUnknown* context) override {
        if (context) context->queryInterface(IHostApplication::iid, (void**)&host_);
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override {
        if (host_) { host_->release(); host_ = nullptr; }
        return kResultOk;
    }

    // IComponent
    tresult PLUGIN_API getControllerClassId(TUID cid) override {
        std::memcpy(cid, kControllerUID, sizeof(TUID));
        return kResultOk;
    }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override {
        if (type == kAudio && dir == kOutput) return 1;
        if (type == kEvent && dir == kInput) return 1;
        return 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) override {
        if (index != 0) return kInvalidArgument;
        if (type == kAudio && dir == kOutput) {
            bus.mediaType = kAudio;
            bus.direction = kOutput;
            bus.channelCount = 2;
            toUtf16("Sortie", bus.name, 128);
            bus.busType = kMain;
            bus.flags = BusInfo::kDefaultActive;
            return kResultOk;
        }
        if (type == kEvent && dir == kInput) {
            bus.mediaType = kEvent;
            bus.direction = kInput;
            bus.channelCount = 16;
            toUtf16("MIDI", bus.name, 128);
            bus.busType = kMain;
            bus.flags = BusInfo::kDefaultActive;
            return kResultOk;
        }
        return kInvalidArgument;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive(TBool state) override {
        if (state) shared_->engine.prepare(sampleRate_, maxBlock_);
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* s) override {
        if (!s) return kInvalidArgument;
        return shared_->model.loadState(readAll(s), nullptr) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API getState(IBStream* s) override {
        if (!s) return kInvalidArgument;
        const std::string data = shared_->model.saveState();
        int32 written = 0;
        return s->write((void*)data.data(), (int32)data.size(), &written);
    }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32 numIns, SpeakerArrangement* outs, int32 numOuts) override {
        if (numIns != 0 || numOuts != 1) return kResultFalse;
        return outs[0] == SpeakerArr::kStereo ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement& arr) override {
        if (dir != kOutput || index != 0) return kInvalidArgument;
        arr = SpeakerArr::kStereo;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return (uint32)shared_->engine.latencySamples(); }  // anticipation du limiteur MIX IA (fixe)
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        sampleRate_ = setup.sampleRate > 0 ? setup.sampleRate : 48000.0;
        maxBlock_ = std::max<int32>(setup.maxSamplesPerBlock, 32);
        scratch_.assign((size_t)maxBlock_, 0.f);
        shared_->engine.prepare(sampleRate_, maxBlock_);
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    tresult PLUGIN_API process(ProcessData& data) override {
        Model& m = shared_->model;
        // tempo de l'hôte (synchro des LFO)
        if (data.processContext && (data.processContext->state & ProcessContext::kTempoValid) && data.processContext->tempo > 1.0)
            m.hostBpm.store((float)data.processContext->tempo, std::memory_order_relaxed);
        if (data.processContext) {
            const bool play = (data.processContext->state & ProcessContext::kPlaying) != 0 &&
                              (data.processContext->state & ProcessContext::kProjectTimeMusicValid) != 0;
            if (play) m.hostPpq.store(data.processContext->projectTimeMusic, std::memory_order_relaxed);
            m.hostPlaying.store(play, std::memory_order_relaxed);
        }
        // automations : chaque point est appliqué à son échantillon (sampleOffset), pas en début de bloc
        int npev = 0;
        if (IParameterChanges* changes = data.inputParameterChanges) {
            const int32 n = changes->getParameterCount();
            for (int32 i = 0; i < n; ++i) {
                IParamValueQueue* q = changes->getParameterData(i);
                if (!q) continue;
                const ParamID id = q->getParameterId();
                if (id >= kNumParams) continue;
                const int32 points = q->getPointCount();
                for (int32 k = 0; k < points; ++k) {
                    int32 offset;
                    ParamValue v;
                    if (q->getPoint(k, offset, v) != kResultOk) continue;
                    const float plain = fromNormalized(id, (float)v);
                    if (npev < (int)kMaxParamEvents) pevents_[(size_t)npev++] = ParamEvent{offset, (uint32_t)id, plain};
                    else m.params.set(id, plain);  // file pleine : valeur appliquée tout de suite
                }
            }
            // tri par offset (sans allocation)
            std::sort(pevents_.begin(), pevents_.begin() + npev, [](const ParamEvent& a, const ParamEvent& b) { return a.offset < b.offset; });
        }
        int nev = 0;
        if (IEventList* list = data.inputEvents) {
            const int32 count = list->getEventCount();
            for (int32 i = 0; i < count && nev < (int)kMaxEvents; ++i) {
                Event e{};
                if (list->getEvent(i, e) != kResultOk) continue;
                NoteEvent x;
                x.offset = e.sampleOffset;
                if (e.type == Event::kNoteOnEvent) {
                    x.note = e.noteOn.pitch;
                    x.velocity = e.noteOn.velocity;
                    x.on = e.noteOn.velocity > 0.f;
                    x.channel = (uint8_t)std::clamp<int>(e.noteOn.channel, 0, 15);
                    x.noteId = e.noteOn.noteId;
                } else if (e.type == Event::kNoteOffEvent) {
                    x.note = e.noteOff.pitch;
                    x.velocity = 0.f;
                    x.on = false;
                    x.channel = (uint8_t)std::clamp<int>(e.noteOff.channel, 0, 15);
                    x.noteId = e.noteOff.noteId;
                } else if (e.type == Event::kPolyPressureEvent) {  // aftertouch polyphonique
                    x.kind = kEvPolyPress;
                    x.note = e.polyPressure.pitch;
                    x.noteId = e.polyPressure.noteId;
                    x.value = e.polyPressure.pressure;
                } else if (e.type == Event::kNoteExpressionValueEvent) {  // MPE des hôtes VST3 (Note Expression)
                    const NoteExpressionValueEvent& ne = e.noteExpressionValue;
                    x.noteId = ne.noteId;
                    if (ne.typeId == kTuningTypeID) { x.kind = kEvNoteTune; x.value = (float)((ne.value - 0.5) * 240.0); }
                    else if (ne.typeId == kBrightnessTypeID) { x.kind = kEvNoteTimbre; x.value = (float)ne.value; }
                    else if (ne.typeId == kRaw2PressureTypeID) { x.kind = kEvPolyPress; x.note = -1; x.value = (float)ne.value; }
                    else continue;
                } else {
                    continue;
                }
                events_[(size_t)nev++] = x;
            }
            // l'hôte doit les trier, on s'en assure (tri par insertion, sans allocation)
            for (int i = 1; i < nev; ++i)
                for (int j = i; j > 0 && events_[(size_t)j].offset < events_[(size_t)j - 1].offset; --j) std::swap(events_[(size_t)j], events_[(size_t)j - 1]);
        }
        if (data.numSamples <= 0 || data.numOutputs < 1 || !data.outputs || data.outputs[0].numChannels < 1 || !data.outputs[0].channelBuffers32 ||
            !data.outputs[0].channelBuffers32[0]) {
            // appel « flush » sans audio : les automations sont quand même prises en compte
            for (int i = 0; i < npev; ++i) m.params.set(pevents_[(size_t)i].id, pevents_[(size_t)i].value);
            return kResultOk;
        }
        const int total = data.numSamples;
        for (int i = 0; i < nev; ++i) events_[(size_t)i].offset = std::clamp(events_[(size_t)i].offset, 0, total - 1);
        for (int i = 0; i < npev; ++i) pevents_[(size_t)i].offset = std::clamp(pevents_[(size_t)i].offset, 0, total - 1);
        float* L = data.outputs[0].channelBuffers32[0];
        float* R = data.outputs[0].numChannels > 1 ? data.outputs[0].channelBuffers32[1] : nullptr;
        // un hôte peut envoyer un bloc plus grand qu'annoncé (FL Studio, buffers variables) : on le
        // traite par tranches plutôt que de laisser un trou (un trou = un clic)
        int e0 = 0, p0 = 0;
        for (int done = 0; done < total;) {
            const int len = std::min(total - done, (int)maxBlock_);
            int e1 = e0, p1 = p0;
            while (e1 < nev && events_[(size_t)e1].offset < done + len) events_[(size_t)e1++].offset -= done;
            while (p1 < npev && pevents_[(size_t)p1].offset < done + len) pevents_[(size_t)p1++].offset -= done;
            shared_->engine.process(L + done, R ? R + done : scratch_.data(), len, events_.data() + e0, e1 - e0, pevents_.data() + p0, p1 - p0);
            e0 = e1;
            p0 = p1;
            done += len;
        }
        if (data.outputs[0].numChannels > 2)  // canaux en trop : silence
            for (int32 c = 2; c < data.outputs[0].numChannels; ++c)
                if (float* x = data.outputs[0].channelBuffers32[c]) std::fill(x, x + total, 0.f);
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }

    // IConnectionPoint
    tresult PLUGIN_API connect(IConnectionPoint* other) override {
        if (!other) return kInvalidArgument;
        if (peer_) peer_->release();
        peer_ = other;
        peer_->addRef();
        // adresse du modèle commun envoyée au contrôleur (même processus)
        if (host_) {
            IMessage* msg = nullptr;
            TUID cid, iid;
            std::memcpy(cid, IMessage::iid, sizeof(TUID));
            std::memcpy(iid, IMessage::iid, sizeof(TUID));
            if (host_->createInstance(cid, iid, (void**)&msg) == kResultOk && msg) {
                msg->setMessageID("raw2.shared");
                if (IAttributeList* a = msg->getAttributes()) a->setInt("ptr", (int64)(intptr_t)shared_);
                other->notify(msg);
                msg->release();
            }
        }
        return kResultOk;
    }
    tresult PLUGIN_API disconnect(IConnectionPoint* other) override {
        if (peer_ && peer_ == other) {
            peer_->release();
            peer_ = nullptr;
        }
        return kResultOk;
    }
    tresult PLUGIN_API notify(IMessage*) override { return kResultOk; }

private:
    static constexpr size_t kMaxEvents = 1024;
    static constexpr size_t kMaxParamEvents = 4096;
    std::atomic<uint32> refs_{1};
    Shared* shared_;
    IHostApplication* host_ = nullptr;
    IConnectionPoint* peer_ = nullptr;
    double sampleRate_ = 48000.0;
    int32 maxBlock_ = 1024;
    std::vector<float> scratch_ = std::vector<float>(1024, 0.f);
    std::vector<NoteEvent> events_ = std::vector<NoteEvent>(kMaxEvents);
    std::vector<ParamEvent> pevents_ = std::vector<ParamEvent>(kMaxParamEvents);
};

// =================================================================================================
// Contrôleur
// =================================================================================================
class Controller;

#if defined(RAW2_HAS_VIEW)
#if defined(_WIN32)
using NativeView = Win32View;
const FIDString kNativeType = kPlatformTypeHWND;
#else
using NativeView = MacView;
const FIDString kNativeType = kPlatformTypeNSView;
#endif

class PlugView final : public IPlugView, public IPlugViewContentScaleSupport, public NativeView::Host {
public:
    PlugView(Controller* c, Shared* s);
    ~PlugView();

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs<FUnknown>(iid) || iidIs<IPlugView>(iid)) *obj = static_cast<IPlugView*>(this);
        else if (iidIs<IPlugViewContentScaleSupport>(iid)) *obj = static_cast<IPlugViewContentScaleSupport*>(this);
        else { *obj = nullptr; return kNoInterface; }
        addRef();
        return kResultOk;
    }
    RAW2_REFCOUNT

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override { return type && std::strcmp(type, kNativeType) == 0 ? kResultTrue : kResultFalse; }
    tresult PLUGIN_API attached(void* parent, FIDString type) override {
        if (!type || std::strcmp(type, kNativeType) != 0) return kResultFalse;
        return view_->attach(parent) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API removed() override {
        view_->detach();
        return kResultOk;
    }
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16 key, int16 keyCode, int16) override {
        int vk = 0;
        switch (keyCode) {
            case KEY_ESCAPE: vk = 0x1B; break;
            case KEY_RETURN: case KEY_ENTER: vk = 0x0D; break;
            case KEY_BACK: vk = 0x08; break;
            case KEY_DELETE: vk = 0x2E; break;
            case KEY_LEFT: vk = 0x25; break;
            case KEY_RIGHT: vk = 0x27; break;
            case KEY_HOME: vk = 0x24; break;
            case KEY_END: vk = 0x23; break;
            default: break;
        }
        return view_->hostKey((char16_t)key, vk) ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect* r) override {
        int w, h;
        view_->preferredSize(w, h);
        r->left = 0; r->top = 0; r->right = w; r->bottom = h;
        return kResultOk;
    }
    tresult PLUGIN_API onSize(ViewRect* r) override {
        if (r) view_->setSize(r->right - r->left, r->bottom - r->top);
        return kResultOk;
    }
    tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame* frame) override {
        frame_ = frame;
        return kResultOk;
    }
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* r) override { return getSize(r); }
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor f) override {
        view_->setContentScale((float)f);
        if (frame_) {
            ViewRect r;
            getSize(&r);
            frame_->resizeView(this, &r);
        }
        return kResultOk;
    }
    void requestResize(int w, int h) override {
        ViewRect r(0, 0, w, h);
        if (frame_ && frame_->resizeView(this, &r) == kResultOk) return;
        view_->setSize(w, h);
    }

private:
    std::atomic<uint32> refs_{1};
    Controller* ctrl_;
    Shared* shared_;
    std::unique_ptr<NativeView> view_;
    IPlugFrame* frame_ = nullptr;
};
#endif

class Controller final : public IEditController, public IConnectionPoint, public IMidiMapping, public INoteExpressionController, public ParamSink {
public:
    Controller() = default;
    ~Controller() {
        if (handler_) handler_->release();
        if (shared_) shared_->release();
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs<FUnknown>(iid) || iidIs<IPluginBase>(iid) || iidIs<IEditController>(iid)) *obj = static_cast<IEditController*>(this);
        else if (iidIs<IConnectionPoint>(iid)) *obj = static_cast<IConnectionPoint*>(this);
        else if (iidIs<IMidiMapping>(iid)) *obj = static_cast<IMidiMapping*>(this);
        else if (iidIs<INoteExpressionController>(iid)) *obj = static_cast<INoteExpressionController*>(this);
        else { *obj = nullptr; return kNoInterface; }
        addRef();
        return kResultOk;
    }
    RAW2_REFCOUNT

    // molettes du clavier MIDI -> paramètres PITCH / MOD (automatisables)
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16, CtrlNumber cc, ParamID& id) override {
        if (bus != 0) return kResultFalse;
        if (cc == Steinberg::Vst::ControllerNumbers::kPitchBend) { id = raw2::kPitchBend; return kResultTrue; }
        if (cc == Steinberg::Vst::ControllerNumbers::kCtrlModWheel) { id = raw2::kModWheel; return kResultTrue; }
        if (cc == Steinberg::Vst::ControllerNumbers::kAfterTouch) { id = raw2::kAftertouch; return kResultTrue; }
        if (cc == 74) { id = raw2::kTimbre; return kResultTrue; }  // CC 74 (glissé MPE, « brillance »)
        return kResultFalse;
    }

    // Note Expression (MPE des hôtes VST3) : hauteur par note, brillance (timbre), pression
    int32 PLUGIN_API getNoteExpressionCount(int32 bus, int16) override { return bus == 0 ? 3 : 0; }
    tresult PLUGIN_API getNoteExpressionInfo(int32 bus, int16, int32 index, NoteExpressionTypeInfo& info) override {
        if (bus != 0 || index < 0 || index > 2) return kInvalidArgument;
        info = NoteExpressionTypeInfo{};
        info.unitId = -1;
        info.associatedParameterId = 0;
        if (index == 0) {
            info.typeId = kTuningTypeID;
            toUtf16("Hauteur", info.title, 128);
            toUtf16("Tune", info.shortTitle, 128);
            toUtf16("st", info.units, 128);
            info.valueDesc = NoteExpressionValueDescription{0.5, 0.0, 1.0, 0};
            info.flags = NoteExpressionTypeInfo::kIsBipolar;
        } else if (index == 1) {
            info.typeId = kBrightnessTypeID;
            toUtf16("Timbre", info.title, 128);
            toUtf16("Timb", info.shortTitle, 128);
            info.valueDesc = NoteExpressionValueDescription{0.0, 0.0, 1.0, 0};
            info.flags = NoteExpressionTypeInfo::kIsAbsolute;
        } else {
            info.typeId = kRaw2PressureTypeID;
            toUtf16("Pression", info.title, 128);
            toUtf16("Press", info.shortTitle, 128);
            info.valueDesc = NoteExpressionValueDescription{0.0, 0.0, 1.0, 0};
            info.flags = NoteExpressionTypeInfo::kIsAbsolute;
        }
        return kResultOk;
    }
    tresult PLUGIN_API getNoteExpressionStringByValue(int32, int16, NoteExpressionTypeID id, NoteExpressionValue v, String128 out) override {
        char b[48];
        if (id == kTuningTypeID) std::snprintf(b, sizeof b, "%+.2f st", (v - 0.5) * 240.0);
        else std::snprintf(b, sizeof b, "%.0f %%", v * 100.0);
        toUtf16(b, out, 128);
        return kResultOk;
    }
    tresult PLUGIN_API getNoteExpressionValueByString(int32, int16, NoteExpressionTypeID id, const TChar* text, NoteExpressionValue& v) override {
        const double x = std::atof(fromUtf16(text).c_str());
        v = id == kTuningTypeID ? std::clamp(0.5 + x / 240.0, 0.0, 1.0) : std::clamp(x / 100.0, 0.0, 1.0);
        return kResultOk;
    }

    tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override {
        if (handler_) { handler_->release(); handler_ = nullptr; }
        return kResultOk;
    }

    // --- paramètres ---
    ParamStore& params() { return shared_ ? shared_->model.params : local_; }
    tresult PLUGIN_API setComponentState(IBStream* s) override {
        if (!s) return kInvalidArgument;
        const std::string data = readAll(s);
        if (shared_) return kResultOk;  // le processeur a déjà chargé l'état dans le modèle commun
        json::Value v;
        if (json::Value::parse(data, v) && v["preset"]["params"].isObject())
            for (const auto& mm : v["preset"]["params"].members()) {
                const int id = paramIdFromKey(mm.first.c_str());
                if (id >= 0) local_.set((uint32_t)id, (float)mm.second.num());
            }
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream*) override { return kResultOk; }
    tresult PLUGIN_API getState(IBStream*) override { return kResultOk; }
    int32 PLUGIN_API getParameterCount() override { return kNumParams; }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override {
        if (index < 0 || index >= (int32)kNumParams) return kInvalidArgument;
        const ParamDef& p = paramDef((uint32_t)index);
        info.id = (ParamID)index;
        toUtf16(p.name, info.title, 128);
        toUtf16(p.key, info.shortTitle, 128);
        toUtf16(p.unit, info.units, 128);
        info.stepCount = p.steps;
        info.defaultNormalizedValue = toNormalized((uint32_t)index, p.def);
        info.unitId = 0;  // unité racine
        info.flags = ParameterInfo::kCanAutomate;
        if (p.steps > 0 && p.unit[0] == 0) info.flags |= ParameterInfo::kIsList;
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue v, String128 out) override {
        if (id >= kNumParams) return kInvalidArgument;
        toUtf16(formatParam(id, fromNormalized(id, (float)v)), out, 128);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID id, TChar* text, ParamValue& v) override {
        float plain;
        if (id >= kNumParams || !parseParam(id, fromUtf16(text).c_str(), plain)) return kResultFalse;
        v = toNormalized(id, plain);
        return kResultOk;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue v) override { return id < kNumParams ? fromNormalized(id, (float)v) : v; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue v) override { return id < kNumParams ? toNormalized(id, (float)v) : v; }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return id < kNumParams ? toNormalized(id, params().get(id)) : 0.0; }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override {
        if (id >= kNumParams) return kInvalidArgument;
        params().set(id, fromNormalized(id, (float)v));
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler* h) override {
        if (h) h->addRef();
        if (handler_) handler_->release();
        handler_ = h;
        return kResultOk;
    }
    IPlugView* PLUGIN_API createView(FIDString name) override {
#if defined(RAW2_HAS_VIEW)
        if (!name || std::strcmp(name, ViewType::kEditor) != 0) return nullptr;
        if (!shared_) shared_ = new Shared();  // pas relié au processeur : l'interface reste utilisable
        return new PlugView(this, shared_);
#else
        (void)name;
        return nullptr;
#endif
    }

    // --- ParamSink : gestes de l'interface vers l'hôte ---
    void beginEdit(uint32_t id) override {
        if (handler_) handler_->beginEdit(id);
    }
    void performEdit(uint32_t id, float plain) override {
        params().set(id, plain);
        if (handler_) handler_->performEdit(id, toNormalized(id, params().get(id)));
    }
    void endEdit(uint32_t id) override {
        if (handler_) handler_->endEdit(id);
    }

    // --- IConnectionPoint ---
    tresult PLUGIN_API connect(IConnectionPoint* other) override {
        if (!other) return kInvalidArgument;
        IRaw2Peer* p = nullptr;
        if (other->queryInterface(IRaw2Peer::iid, (void**)&p) == kResultOk && p) {
            attach(p->shared());
            p->release();
        }
        return kResultOk;
    }
    tresult PLUGIN_API disconnect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API notify(IMessage* msg) override {
        if (!msg || !msg->getMessageID() || std::strcmp(msg->getMessageID(), "raw2.shared") != 0) return kResultFalse;
        int64 ptr = 0;
        if (IAttributeList* a = msg->getAttributes())
            if (a->getInt("ptr", ptr) == kResultOk && ptr) attach((Shared*)(intptr_t)ptr);
        return kResultOk;
    }

private:
    void attach(Shared* s) {
        if (!s || s == shared_) return;
        if (shared_) return;  // déjà relié (ou interface déjà ouverte sur un modèle local)
        s->addRef();
        shared_ = s;
    }

    std::atomic<uint32> refs_{1};
    Shared* shared_ = nullptr;
    ParamStore local_;
    IComponentHandler* handler_ = nullptr;
};

#if defined(RAW2_HAS_VIEW)
PlugView::PlugView(Controller* c, Shared* s) : ctrl_(c), shared_(s) {
    ctrl_->addRef();
    shared_->addRef();
    view_ = std::make_unique<NativeView>(shared_->model, *ctrl_, shared_->uiState, this);
}
PlugView::~PlugView() {
    view_.reset();
    shared_->release();
    ctrl_->release();
}
#endif

// =================================================================================================
// Usine
// =================================================================================================
class Factory final : public IPluginFactory3 {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs<IPluginFactory3>(iid) || iidIs<IPluginFactory2>(iid) || iidIs<IPluginFactory>(iid) || iidIs<FUnknown>(iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override {
        *info = PFactoryInfo(kVendor, "https://abrasion.dev", "", PFactoryInfo::kUnicode);
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 2; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override {
        if (index == 0) *info = PClassInfo(kProcessorUID, PClassInfo::kManyInstances, kVstAudioEffectClass, kName);
        else if (index == 1) *info = PClassInfo(kControllerUID, PClassInfo::kManyInstances, kVstComponentControllerClass, "RAW 2 Controller");
        else return kInvalidArgument;
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override {
        if (index == 0)
            *info = PClassInfo2(kProcessorUID, PClassInfo::kManyInstances, kVstAudioEffectClass, kName, 0, "Instrument|Synth", kVendor, RAW2_VERSION_STRING, kVstVersionString);
        else if (index == 1)
            *info = PClassInfo2(kControllerUID, PClassInfo::kManyInstances, kVstComponentControllerClass, "RAW 2 Controller", 0, "", kVendor, RAW2_VERSION_STRING, kVstVersionString);
        else return kInvalidArgument;
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override {
        if (index < 0 || index > 1) return kInvalidArgument;
        std::memset((void*)info, 0, sizeof *info);
        std::memcpy(info->cid, index == 0 ? kProcessorUID : kControllerUID, sizeof(TUID));
        info->cardinality = PClassInfo::kManyInstances;
        std::strncpy(info->category, index == 0 ? kVstAudioEffectClass : kVstComponentControllerClass, PClassInfo::kCategorySize - 1);
        toUtf16(index == 0 ? kName : "RAW 2 Controller", info->name, PClassInfo::kNameSize);
        info->classFlags = 0;
        if (index == 0) std::strncpy(info->subCategories, "Instrument|Synth", PClassInfo2::kSubCategoriesSize - 1);
        toUtf16(kVendor, info->vendor, PClassInfo2::kVendorSize);
        toUtf16(RAW2_VERSION_STRING, info->version, PClassInfo2::kVersionSize);
        toUtf16(kVstVersionString, info->sdkVersion, PClassInfo2::kVersionSize);
        return kResultOk;
    }
    tresult PLUGIN_API setHostContext(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override {
        *obj = nullptr;
        FUnknown* inst = nullptr;
        if (FUnknownPrivate::iidEqual(cid, kProcessorUID)) inst = static_cast<IComponent*>(new Processor());
        else if (FUnknownPrivate::iidEqual(cid, kControllerUID)) inst = static_cast<IEditController*>(new Controller());
        else return kNoInterface;
        const tresult r = inst->queryInterface(iid, obj);
        inst->release();  // la référence est portée par l'interface rendue
        return r;
    }
};

Factory gFactory;

} // namespace
} // namespace raw2::vst

// --- points d'entrée du module ------------------------------------------------------------------
#if defined(_WIN32)
#define RAW2_EXPORT extern "C" __declspec(dllexport)
#else
#define RAW2_EXPORT extern "C" __attribute__((visibility("default")))
#endif

RAW2_EXPORT Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return &raw2::vst::gFactory; }

#if defined(_WIN32)
RAW2_EXPORT bool InitDll() { return true; }
RAW2_EXPORT bool ExitDll() { return true; }
#elif defined(__APPLE__)
RAW2_EXPORT bool bundleEntry(void*) { return true; }
RAW2_EXPORT bool bundleExit() { return true; }
#else
RAW2_EXPORT bool ModuleEntry(void*) { return true; }
RAW2_EXPORT bool ModuleExit() { return true; }
#endif
