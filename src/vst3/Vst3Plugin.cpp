// VST3 wrapper written directly against Steinberg's interface headers (pluginterfaces,
// MIT licence). No VST3 SDK helper classes: one "single component" object implements both
// the audio processor and the edit controller.
#include "../core/PluginCore.h"
#include "../platform/PlatformView.h"
#include "../ui/Editor.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef TW_VERSION_STRING
#define TW_VERSION_STRING "1.0.0"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

// Interface IDs used by this plugin (the full SDK defines these in vstinitiids.cpp).
namespace Steinberg {
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
DEF_CLASS_IID(IPlugViewContentScaleSupport)
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
} // namespace Vst
} // namespace Steinberg

namespace {

// Class ID of the plugin: never change it once released, hosts store it in projects.
const TUID kClassId = INLINE_UID(0x5477C1A0, 0x7E4B4D3A, 0x9A61B2C3, 0x4D6F6E6F);

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

#if defined(_WIN32)
const FIDString kPlatform = kPlatformTypeHWND;
#elif defined(__APPLE__)
const FIDString kPlatform = kPlatformTypeNSView;
#else
const FIDString kPlatform = kPlatformTypeX11EmbedWindowID;
#endif

class TwistedVst3;

// ------------------------------------------------------------------------------------------
// Editor view
// ------------------------------------------------------------------------------------------
class TwistedView final : public IPlugView, public IPlugViewContentScaleSupport {
public:
    TwistedView(TwistedVst3* plugin, tw::PluginCore& core, tw::ParamHost& host);
    virtual ~TwistedView();

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IPlugView::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IPlugView*>(this);
        } else if (FUnknownPrivate::iidEqual(iid, IPlugViewContentScaleSupport::iid)) {
            *obj = static_cast<IPlugViewContentScaleSupport*>(this);
        } else {
            *obj = nullptr;
            return kNoInterface;
        }
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs_; }
    uint32 PLUGIN_API release() override {
        const uint32 r = --refs_;
        if (r == 0) delete this;
        return r;
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override { return std::strcmp(type, kPlatform) == 0 ? kResultTrue : kResultFalse; }
    tresult PLUGIN_API attached(void* parent, FIDString type) override {
        if (std::strcmp(type, kPlatform) != 0) return kResultFalse;
        return view_->attach(parent) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API removed() override { view_->detach(); return kResultOk; }
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect* r) override {
        int w, h;
        view_->size(w, h);
        r->left = 0; r->top = 0; r->right = w; r->bottom = h;
        return kResultOk;
    }
    tresult PLUGIN_API onSize(ViewRect*) override { return kResultOk; }
    tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame* frame) override { frame_ = frame; return kResultOk; }
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* r) override { return getSize(r); }
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {
#if defined(__APPLE__)
        (void)factor;
        return kResultFalse;
#else
        view_->setScale(factor);
        if (frame_) {
            ViewRect r;
            getSize(&r);
            frame_->resizeView(this, &r);
        }
        return kResultOk;
#endif
    }

private:
    std::atomic<uint32> refs_{1};
    TwistedVst3* plugin_;
    std::unique_ptr<tw::Editor> editor_;
    std::unique_ptr<tw::PlatformView> view_;
    IPlugFrame* frame_ = nullptr;
};

// ------------------------------------------------------------------------------------------
// Plugin: component + processor + controller in one object
// ------------------------------------------------------------------------------------------
class TwistedVst3 final : public IComponent, public IAudioProcessor, public IEditController, public tw::ParamHost {
public:
    TwistedVst3() = default;
    virtual ~TwistedVst3() { core_.deactivate(); }

    // FUnknown
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, FUnknown::iid) || FUnknownPrivate::iidEqual(iid, IPluginBase::iid) ||
            FUnknownPrivate::iidEqual(iid, IComponent::iid)) {
            *obj = static_cast<IComponent*>(this);
        } else if (FUnknownPrivate::iidEqual(iid, IAudioProcessor::iid)) {
            *obj = static_cast<IAudioProcessor*>(this);
        } else if (FUnknownPrivate::iidEqual(iid, IEditController::iid)) {
            *obj = static_cast<IEditController*>(this);
        } else {
            *obj = nullptr;
            return kNoInterface;
        }
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs_; }
    uint32 PLUGIN_API release() override {
        const uint32 r = --refs_;
        if (r == 0) delete this;
        return r;
    }

    // IPluginBase (shared by IComponent and IEditController)
    tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }

    // IComponent
    tresult PLUGIN_API getControllerClassId(TUID) override { return kNotImplemented; }  // single component
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection) override { return type == kAudio ? 1 : 0; }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) override {
        if (type != kAudio || index != 0) return kInvalidArgument;
        bus.mediaType = kAudio;
        bus.direction = dir;
        bus.channelCount = channels_;
        toUtf16(dir == kInput ? "Input" : "Output", bus.name, 128);
        bus.busType = kMain;
        bus.flags = BusInfo::kDefaultActive;
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive(TBool state) override {
        if (state) core_.activate(sampleRate_, maxBlock_);
        else core_.deactivate();
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* s) override {
        if (!s) return kInvalidArgument;
        std::string data;
        char buf[4096];
        int32 got = 0;
        while (s->read(buf, sizeof buf, &got) == kResultOk && got > 0) data.append(buf, (size_t)got);
        core_.loadState(data);
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* s) override {
        if (!s) return kInvalidArgument;
        const std::string data = core_.saveState();
        int32 written = 0;
        return s->write((void*)data.data(), (int32)data.size(), &written);
    }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) override {
        if (numIns != 1 || numOuts != 1 || ins[0] != outs[0]) return kResultFalse;
        if (ins[0] == SpeakerArr::kStereo) { channels_ = 2; return kResultTrue; }
        if (ins[0] == SpeakerArr::kMono) { channels_ = 1; return kResultTrue; }
        return kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32 index, SpeakerArrangement& arr) override {
        if (index != 0) return kInvalidArgument;
        arr = channels_ == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        sampleRate_ = setup.sampleRate;
        maxBlock_ = setup.maxSamplesPerBlock;
        silence_.assign((size_t)std::max(maxBlock_, 64), 0.f);
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    tresult PLUGIN_API process(ProcessData& data) override {
        if (IParameterChanges* changes = data.inputParameterChanges) {
            for (int32 i = 0; i < changes->getParameterCount(); ++i) {
                IParamValueQueue* q = changes->getParameterData(i);
                if (!q || q->getPointCount() <= 0) continue;
                int32 offset;
                ParamValue v;
                if (q->getPoint(q->getPointCount() - 1, offset, v) == kResultOk && q->getParameterId() < tw::kNumParams)
                    core_.params.set(q->getParameterId(), tw::fromNormalized(q->getParameterId(), (float)v));
            }
        }
        if (data.numSamples <= 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 1) return kResultOk;
        if ((int32)silence_.size() < data.numSamples) return kResultOk;  // host broke its own max block size
        const float *inL = silence_.data(), *inR = silence_.data();
        if (data.numInputs > 0 && data.inputs[0].numChannels > 0) {
            inL = data.inputs[0].channelBuffers32[0];
            inR = data.inputs[0].numChannels > 1 ? data.inputs[0].channelBuffers32[1] : inL;
        }
        float* outL = data.outputs[0].channelBuffers32[0];
        float* outR = data.outputs[0].numChannels > 1 ? data.outputs[0].channelBuffers32[1] : nullptr;
        core_.process(inL, inR, outL, outR, data.numSamples);
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override { return (uint32)(sampleRate_ * 0.3); }  // ambience tail

    // IEditController
    tresult PLUGIN_API setComponentState(IBStream* s) override { return setState(s); }
    int32 PLUGIN_API getParameterCount() override { return tw::kNumParams; }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override {
        if (index < 0 || index >= (int32)tw::kNumParams) return kInvalidArgument;
        const tw::ParamInfo& p = tw::paramInfo((uint32_t)index);
        info.id = p.id;
        toUtf16(p.name, info.title, 128);
        toUtf16(p.name, info.shortTitle, 128);
        toUtf16(p.unit, info.units, 128);
        info.stepCount = p.steps;
        info.defaultNormalizedValue = tw::toNormalized(p.id, p.def);
        info.unitId = 0;  // root unit
        info.flags = ParameterInfo::kCanAutomate;
        if (p.steps > 1) info.flags |= ParameterInfo::kIsList;
        if (p.isBypass) info.flags |= ParameterInfo::kIsBypass;
        if (p.id == tw::kCameraBass) info.flags = 0;  // GUI setting
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue v, String128 out) override {
        if (id >= tw::kNumParams) return kInvalidArgument;
        toUtf16(tw::formatParam(id, tw::fromNormalized(id, (float)v)), out, 128);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID id, TChar* text, ParamValue& v) override {
        float plain;
        if (id >= tw::kNumParams || !tw::parseParam(id, fromUtf16(text).c_str(), plain)) return kResultFalse;
        v = tw::toNormalized(id, plain);
        return kResultOk;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue v) override { return id < tw::kNumParams ? tw::fromNormalized(id, (float)v) : v; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue v) override { return id < tw::kNumParams ? tw::toNormalized(id, (float)v) : v; }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return id < tw::kNumParams ? tw::toNormalized(id, core_.params.get(id)) : 0.0; }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override {
        if (id >= tw::kNumParams) return kInvalidArgument;
        core_.params.set(id, tw::fromNormalized(id, (float)v));
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler* h) override {
        if (h) h->addRef();
        if (handler_) handler_->release();
        handler_ = h;
        return kResultOk;
    }
    IPlugView* PLUGIN_API createView(FIDString name) override {
        if (!name || std::strcmp(name, ViewType::kEditor) != 0) return nullptr;
        return new TwistedView(this, core_, *this);
    }

    // tw::ParamHost: GUI gestures to the host (UI thread)
    void beginEdit(uint32_t id) override { if (handler_) handler_->beginEdit(id); }
    void performEdit(uint32_t id, float plain) override {
        core_.params.set(id, plain);
        if (handler_) handler_->performEdit(id, tw::toNormalized(id, core_.params.get(id)));
    }
    void endEdit(uint32_t id) override { if (handler_) handler_->endEdit(id); }

private:
    std::atomic<uint32> refs_{1};
    tw::PluginCore core_;
    IComponentHandler* handler_ = nullptr;
    double sampleRate_ = 48000.0;
    int32 maxBlock_ = 1024;
    int32 channels_ = 2;
    std::vector<float> silence_ = std::vector<float>(1024, 0.f);
};

TwistedView::TwistedView(TwistedVst3* plugin, tw::PluginCore& core, tw::ParamHost& host) : plugin_(plugin) {
    plugin_->addRef();
    editor_ = std::make_unique<tw::Editor>(core, host);
    view_ = tw::PlatformView::create(*editor_);
}

TwistedView::~TwistedView() {
    view_.reset();
    editor_.reset();
    plugin_->release();
}

// ------------------------------------------------------------------------------------------
// Factory
// ------------------------------------------------------------------------------------------
class Factory final : public IPluginFactory3 {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IPluginFactory3::iid) || FUnknownPrivate::iidEqual(iid, IPluginFactory2::iid) ||
            FUnknownPrivate::iidEqual(iid, IPluginFactory::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override {
        *info = PFactoryInfo("Twisted Audio", "", "", PFactoryInfo::kUnicode);
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override {
        if (index != 0) return kInvalidArgument;
        *info = PClassInfo(kClassId, PClassInfo::kManyInstances, kVstAudioEffectClass, "Twisted");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override {
        if (index != 0) return kInvalidArgument;
        *info = PClassInfo2(kClassId, PClassInfo::kManyInstances, kVstAudioEffectClass, "Twisted", 0,
                            "Fx|Mastering|Dynamics|Distortion", "Twisted Audio", TW_VERSION_STRING, kVstVersionString);
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override {
        if (index != 0) return kInvalidArgument;
        std::memset((void*)info, 0, sizeof *info);
        std::memcpy(info->cid, kClassId, sizeof(TUID));
        info->cardinality = PClassInfo::kManyInstances;
        std::strncpy(info->category, kVstAudioEffectClass, PClassInfo::kCategorySize - 1);
        toUtf16("Twisted", info->name, PClassInfo::kNameSize);
        info->classFlags = 0;
        std::strncpy(info->subCategories, "Fx|Mastering|Dynamics|Distortion", PClassInfo2::kSubCategoriesSize - 1);
        toUtf16("Twisted Audio", info->vendor, PClassInfo2::kVendorSize);
        toUtf16(TW_VERSION_STRING, info->version, PClassInfo2::kVersionSize);
        toUtf16(kVstVersionString, info->sdkVersion, PClassInfo2::kVersionSize);
        return kResultOk;
    }
    tresult PLUGIN_API setHostContext(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override {
        *obj = nullptr;
        if (!FUnknownPrivate::iidEqual(cid, kClassId)) return kNoInterface;
        auto* plugin = new TwistedVst3();
        const tresult r = plugin->queryInterface(iid, obj);
        plugin->release();  // the interface returned by queryInterface holds the reference
        return r;
    }
};

Factory gFactory;
} // namespace

// ------------------------------------------------------------------------------------------
// Module entry points
// ------------------------------------------------------------------------------------------
#if defined(_WIN32)
#define TW_EXPORT extern "C" __declspec(dllexport)
#else
#define TW_EXPORT extern "C" __attribute__((visibility("default")))
#endif

TW_EXPORT IPluginFactory* PLUGIN_API GetPluginFactory() { return &gFactory; }

#if defined(_WIN32)
TW_EXPORT bool InitDll() { return true; }
TW_EXPORT bool ExitDll() { return true; }
#elif defined(__APPLE__)
TW_EXPORT bool bundleEntry(void*) { return true; }
TW_EXPORT bool bundleExit() { return true; }
#else
TW_EXPORT bool ModuleEntry(void*) { return true; }
TW_EXPORT bool ModuleExit() { return true; }
#endif
