// RAW 2 en Audio Unit v2 (instrument « aumu »), écrit directement sur l'API AudioComponent
// d'Apple, sans JUCE ni le SDK de classes AU. Le moteur, le modèle et l'interface sont ceux du
// VST3 ; ce fichier ne fait que la traduction :
//   - usine RAW2AUFactory (nommée dans Info.plist > AudioComponents) et table des sélecteurs ;
//   - propriétés : formats, fréquence, taille de bloc, paramètres (liste, infos, texte), état
//     (ClassInfo), presets d'usine, latence, rappels de l'hôte (tempo, lecture), vue Cocoa ;
//   - rendu avec les notes MIDI placées à l'échantillon près ;
//   - gestes de l'interface vers l'hôte (début / valeur / fin), pour l'automation.
// Compilé avec ARC.
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AudioUnitUtilities.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>

#include "core/FactoryData.h"
#include "core/Model.h"
#include "dsp/Engine.h"
#include "platform/MacView.h"
#include "ui/Editor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef RAW2_OBJC_SUFFIX
#define RAW2_OBJC_SUFFIX au_dev
#endif
#define RAW2_CAT2(a, b) a##b
#define RAW2_CAT(a, b) RAW2_CAT2(a, b)
#define RAW2AUViewFactory RAW2_CAT(RAW2AUViewFactory_, RAW2_OBJC_SUFFIX)
#define RAW2AUContainerView RAW2_CAT(RAW2AUContainerView_, RAW2_OBJC_SUFFIX)
#define RAW2_STR2(x) #x
#define RAW2_STR(x) RAW2_STR2(x)

#ifndef RAW2_AU_BUNDLE_ID
#define RAW2_AU_BUNDLE_ID "dev.abrasion.raw2.component"
#endif

namespace raw2::au {

constexpr OSType kType = 'aumu', kSubtype = 'Raw2', kManufacturer = 'Abrs';
constexpr AudioUnitPropertyID kInstanceProperty = 64001;  // privé : la vue retrouve l'instance
constexpr UInt32 kDefaultMaxFrames = 4096;
constexpr int kMaxMidi = 2048;

// File MIDI sans verrou, plusieurs producteurs (MusicDeviceMIDIEvent, quel que soit le fil de
// l'hôte) et un consommateur (le rendu) : file bornée de Vyukov, chaque case porte un numéro de
// séquence. Pleine : la note est perdue plutôt que d'attendre (le rendu ne bloque jamais).
class MidiQueue {
public:
    MidiQueue() {
        for (size_t i = 0; i < kCap; ++i) cells_[i].seq.store(i, std::memory_order_relaxed);
    }
    bool push(const NoteEvent& e) {
        size_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = cells_[pos & (kCap - 1)];
            const size_t seq = c.seq.load(std::memory_order_acquire);
            const intptr_t dif = (intptr_t)seq - (intptr_t)pos;
            if (dif == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    c.ev = e;
                    c.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false;  // pleine
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }
    bool pop(NoteEvent& e) {  // un seul consommateur : le fil de rendu
        Cell& c = cells_[tail_ & (kCap - 1)];
        const size_t seq = c.seq.load(std::memory_order_acquire);
        if ((intptr_t)seq - (intptr_t)(tail_ + 1) < 0) return false;  // vide
        e = c.ev;
        c.seq.store(tail_ + kCap, std::memory_order_release);
        ++tail_;
        return true;
    }

private:
    static constexpr size_t kCap = 2048;  // puissance de deux
    struct Cell {
        std::atomic<size_t> seq{0};
        NoteEvent ev;
    };
    std::array<Cell, kCap> cells_;
    std::atomic<size_t> head_{0};
    size_t tail_ = 0;
};

struct Listener {
    AudioUnitPropertyID id;
    AudioUnitPropertyListenerProc proc;
    void* user;
};
struct RenderNotify {
    AURenderCallback proc;
    void* user;
};

struct Instance;
// Ce que l'hôte reçoit : la table d'entrée (premier membre, structure C) et l'instance.
struct Plug {
    AudioComponentPlugInInterface iface;
    Instance* inst;
};

struct Instance : ParamSink {
    Plug plug{};
    AudioComponentInstance ci = nullptr;
    Model model;
    Engine engine{model};
    ui::EditorState uiState;
    double sampleRate = 44100.0;
    UInt32 maxFrames = kDefaultMaxFrames;
    bool initialized = false;
    UInt32 shouldAllocate = 1;
    HostCallbackInfo host{};
    std::vector<Listener> listeners;
    std::vector<RenderNotify> notifies;
    std::vector<float> bufL, bufR;
    MidiQueue midiIn;                                       // notes reçues (tous fils)
    std::vector<NoteEvent> midi = std::vector<NoteEvent>(kMaxMidi);  // notes de ce rendu (fil de rendu)
    std::vector<AUPreset> factory;  // noms des presets d'usine (CFString gardées)
    AUPreset present{0, nullptr};
    MacView* liveView = nullptr;    // vue ouverte (fermée si l'instance disparaît avant elle)

    Instance() {
        for (int i = 0; i < kNumFactoryPresets; ++i) {
            AUPreset p;
            p.presetNumber = i;
            p.presetName = CFStringCreateWithCString(nullptr, kFactoryPresets[i].name, kCFStringEncodingUTF8);
            factory.push_back(p);
        }
        present.presetNumber = 0;
        present.presetName = (CFStringRef)CFRetain(factory[0].presetName);
        bufL.assign(maxFrames, 0.f);
        bufR.assign(maxFrames, 0.f);
    }
    ~Instance() override {
        if (liveView) liveView->detach();
        for (AUPreset& p : factory)
            if (p.presetName) CFRelease(p.presetName);
        if (present.presetName) CFRelease(present.presetName);
    }

    void prepare() {
        bufL.assign(maxFrames, 0.f);
        bufR.assign(maxFrames, 0.f);
        engine.prepare(sampleRate, (int)maxFrames);
    }
    void notifyProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el) {
        for (const Listener& l : listeners)
            if (l.id == id) l.proc(l.user, ci, id, scope, el);
    }

    // --- gestes de l'interface : l'hôte les voit (automation, affichage) ---
    AudioUnitParameter param(uint32_t id) const { return AudioUnitParameter{ci, (AudioUnitParameterID)id, kAudioUnitScope_Global, 0}; }
    void beginEdit(uint32_t id) override {
        AudioUnitEvent e{};
        e.mEventType = kAudioUnitEvent_BeginParameterChangeGesture;
        e.mArgument.mParameter = param(id);
        AUEventListenerNotify(nullptr, nullptr, &e);
    }
    void performEdit(uint32_t id, float plain) override {
        model.params.set(id, plain);
        const AudioUnitParameter p = param(id);
        AUParameterListenerNotify(nullptr, nullptr, &p);
    }
    void endEdit(uint32_t id) override {
        AudioUnitEvent e{};
        e.mEventType = kAudioUnitEvent_EndParameterChangeGesture;
        e.mArgument.mParameter = param(id);
        AUEventListenerNotify(nullptr, nullptr, &e);
    }
};

Instance* self(void* p) { return static_cast<Plug*>(p)->inst; }

AudioStreamBasicDescription streamFormat(double sr) {
    AudioStreamBasicDescription f{};
    f.mSampleRate = sr;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagsNativeEndian;
    f.mBytesPerPacket = 4;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = 4;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel = 32;
    return f;
}

// --- propriétés ---------------------------------------------------------------------------------
// propriétés qui n'existent qu'en portée globale (auval vérifie qu'elles sont refusées ailleurs)
bool globalOnly(AudioUnitPropertyID id) {
    switch (id) {
        case kAudioUnitProperty_ClassInfo:
        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
        case kAudioUnitProperty_MaximumFramesPerSlice:
        case kAudioUnitProperty_ParameterStringFromValue:
        case kAudioUnitProperty_SupportedNumChannels:
        case kAudioUnitProperty_HostCallbacks:
        case kAudioUnitProperty_CocoaUI:
        case kAudioUnitProperty_FactoryPresets:
        case kAudioUnitProperty_PresentPreset:
        case kAudioUnitProperty_LastRenderError:
        case kInstanceProperty: return true;
        default: return false;
    }
}

OSStatus getPropertyInfo(void* p, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el, UInt32* size, Boolean* writable) {
    (void)p;
    UInt32 sz = 0;
    Boolean w = false;
    if (globalOnly(id) && scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
    if (globalOnly(id) && el != 0) return kAudioUnitErr_InvalidElement;
    switch (id) {
        case kAudioUnitProperty_ClassInfo: sz = sizeof(CFPropertyListRef); w = true; break;
        case kAudioUnitProperty_SampleRate: sz = sizeof(Float64); w = true; break;
        case kAudioUnitProperty_StreamFormat:
            if (scope != kAudioUnitScope_Output) return kAudioUnitErr_InvalidScope;
            if (el != 0) return kAudioUnitErr_InvalidElement;
            sz = sizeof(AudioStreamBasicDescription);
            w = true;
            break;
        case kAudioUnitProperty_ElementCount: sz = sizeof(UInt32); break;
        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime: sz = sizeof(Float64); break;
        case kAudioUnitProperty_MaximumFramesPerSlice: sz = sizeof(UInt32); w = true; break;
        case kAudioUnitProperty_ParameterList:
            sz = scope == kAudioUnitScope_Global ? (UInt32)(kNumParams * sizeof(AudioUnitParameterID)) : 0;
            break;
        case kAudioUnitProperty_ParameterInfo:
            if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
            if (el >= kNumParams) return kAudioUnitErr_InvalidParameter;
            sz = sizeof(AudioUnitParameterInfo);
            break;
        case kAudioUnitProperty_ParameterStringFromValue: sz = sizeof(AudioUnitParameterStringFromValue); break;
        case kAudioUnitProperty_SupportedNumChannels: sz = sizeof(AUChannelInfo); break;
        case kAudioUnitProperty_HostCallbacks: sz = sizeof(HostCallbackInfo); w = true; break;
        case kAudioUnitProperty_CocoaUI: sz = sizeof(AudioUnitCocoaViewInfo); break;
        case kAudioUnitProperty_FactoryPresets: sz = sizeof(CFArrayRef); break;
        case kAudioUnitProperty_PresentPreset: sz = sizeof(AUPreset); w = true; break;
        case kAudioUnitProperty_ShouldAllocateBuffer: sz = sizeof(UInt32); w = true; break;
        case kAudioUnitProperty_LastRenderError: sz = sizeof(OSStatus); break;
        case kInstanceProperty: sz = sizeof(void*); break;
        default: return kAudioUnitErr_InvalidProperty;
    }
    if (size) *size = sz;
    if (writable) *writable = w;
    return noErr;
}

OSStatus getProperty(void* p, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el, void* out, UInt32* ioSize) {
    Instance* in = self(p);
    UInt32 need = 0;
    Boolean w;
    if (OSStatus e = getPropertyInfo(p, id, scope, el, &need, &w)) return e;
    if (need == 0) {  // liste vide (paramètres des portées entrée / sortie)
        if (ioSize) *ioSize = 0;
        return noErr;
    }
    if (!out || !ioSize || *ioSize < need) return kAudioUnitErr_InvalidPropertyValue;
    *ioSize = need;  // taille réellement écrite (l'hôte peut proposer un tampon plus grand)
    switch (id) {
        case kAudioUnitProperty_ClassInfo: {
            const std::string state = in->model.saveState();
            CFMutableDictionaryRef d = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            auto setInt = [&](CFStringRef k, SInt32 v) {
                CFNumberRef n = CFNumberCreate(nullptr, kCFNumberSInt32Type, &v);
                CFDictionarySetValue(d, k, n);
                CFRelease(n);
            };
            setInt(CFSTR(kAUPresetVersionKey), 0);
            setInt(CFSTR(kAUPresetTypeKey), (SInt32)kType);
            setInt(CFSTR(kAUPresetSubtypeKey), (SInt32)kSubtype);
            setInt(CFSTR(kAUPresetManufacturerKey), (SInt32)kManufacturer);
            CFDataRef data = CFDataCreate(nullptr, (const UInt8*)state.data(), (CFIndex)state.size());
            CFDictionarySetValue(d, CFSTR(kAUPresetDataKey), data);
            CFRelease(data);
            std::string name;
            {
                std::lock_guard<std::mutex> lk(in->model.presetMutex);
                name = in->model.presetName;
            }
            CFStringRef nm = CFStringCreateWithCString(nullptr, name.empty() ? "RAW 2" : name.c_str(), kCFStringEncodingUTF8);
            CFDictionarySetValue(d, CFSTR(kAUPresetNameKey), nm ? nm : CFSTR("RAW 2"));
            if (nm) CFRelease(nm);
            *(CFPropertyListRef*)out = d;  // l'appelant libère
            return noErr;
        }
        case kAudioUnitProperty_SampleRate: *(Float64*)out = in->sampleRate; return noErr;
        case kAudioUnitProperty_StreamFormat: *(AudioStreamBasicDescription*)out = streamFormat(in->sampleRate); return noErr;
        case kAudioUnitProperty_ElementCount:
            *(UInt32*)out = scope == kAudioUnitScope_Input ? 0 : 1;
            return noErr;
        case kAudioUnitProperty_Latency: *(Float64*)out = (Float64)in->engine.latencySamples() / in->sampleRate; return noErr;
        case kAudioUnitProperty_TailTime: *(Float64*)out = 2.0; return noErr;
        case kAudioUnitProperty_MaximumFramesPerSlice: *(UInt32*)out = in->maxFrames; return noErr;
        case kAudioUnitProperty_ParameterList: {
            auto* ids = (AudioUnitParameterID*)out;
            if (scope == kAudioUnitScope_Global)
                for (uint32_t i = 0; i < kNumParams; ++i) ids[i] = i;
            *ioSize = need;
            return noErr;
        }
        case kAudioUnitProperty_ParameterInfo: {
            const ParamDef& d = paramDef(el);
            AudioUnitParameterInfo info{};
            std::strncpy(info.name, d.name, sizeof(info.name) - 1);
            info.cfNameString = CFStringCreateWithCString(nullptr, d.name, kCFStringEncodingUTF8);
            info.flags = kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_HasCFNameString |
                         kAudioUnitParameterFlag_CFNameRelease | kAudioUnitParameterFlag_ValuesHaveStrings;
            if (d.steps == 0) info.flags |= kAudioUnitParameterFlag_IsHighResolution;
            info.unit = d.steps == 1 && d.min == 0.f && d.max == 1.f ? kAudioUnitParameterUnit_Boolean : kAudioUnitParameterUnit_Generic;
            info.minValue = d.min;
            info.maxValue = d.max;
            info.defaultValue = d.def;
            *(AudioUnitParameterInfo*)out = info;
            return noErr;
        }
        case kAudioUnitProperty_ParameterStringFromValue: {
            auto* s = (AudioUnitParameterStringFromValue*)out;
            if (s->inParamID >= kNumParams) return kAudioUnitErr_InvalidParameter;
            const float v = s->inValue ? *s->inValue : in->model.params.get(s->inParamID);
            const std::string t = formatParam(s->inParamID, v);
            s->outString = CFStringCreateWithCString(nullptr, t.c_str(), kCFStringEncodingUTF8);
            return noErr;
        }
        case kAudioUnitProperty_SupportedNumChannels: {
            AUChannelInfo ch{0, 2};
            *(AUChannelInfo*)out = ch;
            *ioSize = sizeof(AUChannelInfo);
            return noErr;
        }
        case kAudioUnitProperty_HostCallbacks: *(HostCallbackInfo*)out = in->host; return noErr;
        case kAudioUnitProperty_CocoaUI: {
            CFBundleRef b = CFBundleGetBundleWithIdentifier(CFSTR(RAW2_AU_BUNDLE_ID));
            if (!b) return kAudioUnitErr_InvalidProperty;
            AudioUnitCocoaViewInfo info;
            info.mCocoaAUViewBundleLocation = CFBundleCopyBundleURL(b);
            info.mCocoaAUViewClass[0] = CFStringCreateWithCString(nullptr, RAW2_STR(RAW2AUViewFactory), kCFStringEncodingUTF8);
            *(AudioUnitCocoaViewInfo*)out = info;  // l'hôte libère
            return noErr;
        }
        case kAudioUnitProperty_FactoryPresets: {
            CFMutableArrayRef a = CFArrayCreateMutable(nullptr, (CFIndex)in->factory.size(), nullptr);
            for (AUPreset& pr : in->factory) CFArrayAppendValue(a, &pr);
            *(CFArrayRef*)out = a;  // l'hôte libère le tableau (les AUPreset restent à l'instance)
            return noErr;
        }
        case kAudioUnitProperty_PresentPreset: {
            AUPreset pr = in->present;
            if (pr.presetName) CFRetain(pr.presetName);
            *(AUPreset*)out = pr;
            return noErr;
        }
        case kAudioUnitProperty_ShouldAllocateBuffer: *(UInt32*)out = in->shouldAllocate; return noErr;
        case kAudioUnitProperty_LastRenderError: *(OSStatus*)out = noErr; return noErr;
        case kInstanceProperty: *(void**)out = in; return noErr;
        default: return kAudioUnitErr_InvalidProperty;
    }
}

OSStatus setProperty(void* p, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el, const void* data, UInt32 size) {
    Instance* in = self(p);
    switch (id) {
        case kAudioUnitProperty_ClassInfo: {
            if (!data || size < sizeof(CFPropertyListRef)) return kAudioUnitErr_InvalidPropertyValue;
            CFPropertyListRef pl = *(const CFPropertyListRef*)data;
            if (!pl || CFGetTypeID(pl) != CFDictionaryGetTypeID()) return kAudioUnitErr_InvalidPropertyValue;
            auto d = (CFDictionaryRef)pl;
            auto num = [&](CFStringRef k, SInt32& v) {
                auto n = (CFNumberRef)CFDictionaryGetValue(d, k);
                return n && CFGetTypeID(n) == CFNumberGetTypeID() && CFNumberGetValue(n, kCFNumberSInt32Type, &v);
            };
            SInt32 t = 0, st = 0, mf = 0;
            if (!num(CFSTR(kAUPresetTypeKey), t) || !num(CFSTR(kAUPresetSubtypeKey), st) || !num(CFSTR(kAUPresetManufacturerKey), mf) ||
                (OSType)t != kType || (OSType)st != kSubtype || (OSType)mf != kManufacturer)
                return kAudioUnitErr_InvalidPropertyValue;
            auto blob = (CFDataRef)CFDictionaryGetValue(d, CFSTR(kAUPresetDataKey));
            if (!blob || CFGetTypeID(blob) != CFDataGetTypeID()) return kAudioUnitErr_InvalidPropertyValue;
            const std::string state((const char*)CFDataGetBytePtr(blob), (size_t)CFDataGetLength(blob));
            if (!in->model.loadState(state, nullptr)) return kAudioUnitErr_InvalidPropertyValue;
            // nom donné par l'hôte (preset .aupreset) : gardé tel quel
            auto nm = (CFStringRef)CFDictionaryGetValue(d, CFSTR(kAUPresetNameKey));
            if (nm && CFGetTypeID(nm) == CFStringGetTypeID()) {
                char buf[512];
                if (CFStringGetCString(nm, buf, sizeof buf, kCFStringEncodingUTF8)) {
                    std::lock_guard<std::mutex> lk(in->model.presetMutex);
                    in->model.presetName = buf;
                }
            }
            AudioUnitParameter all{in->ci, kAUParameterListener_AnyParameter, kAudioUnitScope_Global, 0};
            AUParameterListenerNotify(nullptr, nullptr, &all);
            return noErr;
        }
        case kAudioUnitProperty_SampleRate: {
            if (!data || size < sizeof(Float64)) return kAudioUnitErr_InvalidPropertyValue;
            const Float64 sr = *(const Float64*)data;
            if (sr < 8000.0 || sr > 768000.0) return kAudioUnitErr_InvalidPropertyValue;
            in->sampleRate = sr;
            if (in->initialized) in->prepare();
            in->notifyProperty(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0);
            return noErr;
        }
        case kAudioUnitProperty_StreamFormat: {
            if (scope != kAudioUnitScope_Output) return kAudioUnitErr_InvalidScope;
            if (el != 0) return kAudioUnitErr_InvalidElement;
            if (!data || size < sizeof(AudioStreamBasicDescription)) return kAudioUnitErr_InvalidPropertyValue;
            const auto& f = *(const AudioStreamBasicDescription*)data;
            const bool ok = f.mFormatID == kAudioFormatLinearPCM && (f.mFormatFlags & kAudioFormatFlagIsFloat) &&
                            (f.mFormatFlags & kAudioFormatFlagIsNonInterleaved) && f.mBitsPerChannel == 32 && f.mChannelsPerFrame == 2 &&
                            f.mSampleRate >= 8000.0 && f.mSampleRate <= 768000.0;
            if (!ok) return kAudioUnitErr_FormatNotSupported;
            if (in->initialized && f.mSampleRate != in->sampleRate) return kAudioUnitErr_Initialized;
            in->sampleRate = f.mSampleRate;
            in->notifyProperty(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0);
            return noErr;
        }
        case kAudioUnitProperty_MaximumFramesPerSlice: {
            if (!data || size < sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
            const UInt32 m = *(const UInt32*)data;
            if (m == 0 || m > 65536) return kAudioUnitErr_InvalidPropertyValue;
            in->maxFrames = m;
            if (in->initialized) in->prepare();
            in->notifyProperty(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0);
            return noErr;
        }
        case kAudioUnitProperty_HostCallbacks: {
            if (!data) return kAudioUnitErr_InvalidPropertyValue;
            in->host = HostCallbackInfo{};
            std::memcpy(&in->host, data, std::min<size_t>(size, sizeof(HostCallbackInfo)));
            return noErr;
        }
        case kAudioUnitProperty_PresentPreset: {
            if (!data || size < sizeof(AUPreset)) return kAudioUnitErr_InvalidPropertyValue;
            const AUPreset pr = *(const AUPreset*)data;
            if (pr.presetNumber >= 0) {
                if (pr.presetNumber >= kNumFactoryPresets) return kAudioUnitErr_InvalidPropertyValue;
                in->model.applyFactoryPreset((int)pr.presetNumber, nullptr);
                AudioUnitParameter all{in->ci, kAUParameterListener_AnyParameter, kAudioUnitScope_Global, 0};
                AUParameterListenerNotify(nullptr, nullptr, &all);
            }
            if (in->present.presetName) CFRelease(in->present.presetName);
            in->present.presetNumber = pr.presetNumber;
            in->present.presetName = pr.presetName ? (CFStringRef)CFRetain(pr.presetName) : nullptr;
            return noErr;
        }
        case kAudioUnitProperty_ShouldAllocateBuffer:
            if (!data || size < sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
            in->shouldAllocate = *(const UInt32*)data;
            return noErr;
        default: {
            UInt32 sz;
            Boolean w = false;
            if (getPropertyInfo(p, id, scope, el, &sz, &w) != noErr) return kAudioUnitErr_InvalidProperty;
            return kAudioUnitErr_PropertyNotWritable;
        }
    }
}

// --- paramètres -----------------------------------------------------------------------------------
OSStatus getParameter(void* p, AudioUnitParameterID id, AudioUnitScope scope, AudioUnitElement, AudioUnitParameterValue* v) {
    if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
    if (id >= kNumParams || !v) return kAudioUnitErr_InvalidParameter;
    *v = self(p)->model.params.get(id);
    return noErr;
}
OSStatus setParameter(void* p, AudioUnitParameterID id, AudioUnitScope scope, AudioUnitElement, AudioUnitParameterValue v, UInt32) {
    if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
    if (id >= kNumParams) return kAudioUnitErr_InvalidParameter;
    const ParamDef& d = paramDef(id);
    self(p)->model.params.set(id, std::clamp(v, d.min, d.max));
    return noErr;
}
OSStatus scheduleParameters(void* p, const AudioUnitParameterEvent* ev, UInt32 n) {
    for (UInt32 i = 0; i < n; ++i) {
        const AudioUnitParameterEvent& e = ev[i];
        const AudioUnitParameterValue v = e.eventType == kParameterEvent_Immediate ? e.eventValues.immediate.value : e.eventValues.ramp.endValue;
        if (OSStatus err = setParameter(p, e.parameter, e.scope, e.element, v, 0)) return err;
    }
    return noErr;
}

// --- MIDI -----------------------------------------------------------------------------------------
OSStatus midiEvent(void* p, UInt32 status, UInt32 d1, UInt32 d2, UInt32 offset) {
    Instance* in = self(p);
    const UInt32 kind = status & 0xF0;
    if (kind == 0x90 || kind == 0x80) {
        const bool on = kind == 0x90 && d2 > 0;
        in->midiIn.push(NoteEvent{(int)offset, (int)(d1 & 0x7F), on ? (float)(d2 & 0x7F) / 127.f : 0.f, on});
    } else if (kind == 0xB0 && d1 == 1) {  // molette de modulation
        in->model.params.set(kModWheel, fromNormalized(kModWheel, (float)(d2 & 0x7F) / 127.f));
    } else if (kind == 0xE0) {  // pitch bend 14 bits
        const float norm = (float)(((d2 & 0x7F) << 7) | (d1 & 0x7F)) / 16383.f;
        in->model.params.set(kPitchBend, fromNormalized(kPitchBend, norm));
    }
    return noErr;
}
OSStatus sysEx(void*, const UInt8*, UInt32) { return noErr; }

// --- rendu ----------------------------------------------------------------------------------------
OSStatus render(void* p, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 bus, UInt32 frames, AudioBufferList* io) {
    Instance* in = self(p);
    if (!in->initialized) return kAudioUnitErr_Uninitialized;
    if (bus != 0) return kAudioUnitErr_InvalidElement;
    if (frames > in->maxFrames) return kAudioUnitErr_TooManyFramesToProcess;
    if (!io || io->mNumberBuffers < 1) return kAudio_ParamError;
    for (const RenderNotify& r : in->notifies) {
        AudioUnitRenderActionFlags f = (flags ? *flags : 0) | kAudioUnitRenderAction_PreRender;
        r.proc(r.user, &f, ts, bus, frames, io);
    }
    Model& m = in->model;
    // tempo et position de lecture de l'hôte (synchro des LFO, arpégiateur, PUMP)
    if (in->host.beatAndTempoProc) {
        Float64 beat = 0, tempo = 0;
        if (in->host.beatAndTempoProc(in->host.hostUserData, &beat, &tempo) == noErr) {
            if (tempo > 1.0) m.hostBpm.store((float)tempo, std::memory_order_relaxed);
            m.hostPpq.store(beat, std::memory_order_relaxed);
        }
    }
    if (in->host.transportStateProc) {
        Boolean playing = false, changed = false, cycling = false;
        Float64 sampleInTime = 0, cs = 0, ce = 0;
        if (in->host.transportStateProc(in->host.hostUserData, &playing, &changed, &sampleInTime, &cycling, &cs, &ce) == noErr)
            m.hostPlaying.store(playing && in->host.beatAndTempoProc, std::memory_order_relaxed);
    }
    // notes reçues avant ce rendu (file sans verrou, quel que soit le fil de l'hôte) : triées par position
    int nev = 0;
    NoteEvent* ev = in->midi.data();
    while (nev < kMaxMidi && in->midiIn.pop(ev[nev])) ++nev;
    for (int i = 0; i < nev; ++i) ev[i].offset = std::clamp(ev[i].offset, 0, (int)frames - 1);
    for (int i = 1; i < nev; ++i)
        for (int j = i; j > 0 && ev[j].offset < ev[j - 1].offset; --j) std::swap(ev[j], ev[j - 1]);
    float* L = in->bufL.data();
    float* R = in->bufR.data();
    in->engine.process(L, R, (int)frames, ev, nev);
    // copie vers les tampons de l'hôte (ou les nôtres s'il n'en fournit pas)
    for (UInt32 b = 0; b < io->mNumberBuffers; ++b) {
        AudioBuffer& ab = io->mBuffers[b];
        const float* src = b == 0 ? L : R;
        if (!ab.mData) {
            ab.mData = (void*)src;
            ab.mDataByteSize = frames * sizeof(float);
            continue;
        }
        if (io->mNumberBuffers == 1) {  // sortie mono : moyenne des deux côtés
            float* d = (float*)ab.mData;
            for (UInt32 i = 0; i < frames; ++i) d[i] = 0.5f * (L[i] + R[i]);
        } else if (b < 2) {
            std::memcpy(ab.mData, src, frames * sizeof(float));
        } else {
            std::memset(ab.mData, 0, frames * sizeof(float));
        }
        ab.mDataByteSize = frames * sizeof(float);
    }
    for (const RenderNotify& r : in->notifies) {
        AudioUnitRenderActionFlags f = (flags ? *flags : 0) | kAudioUnitRenderAction_PostRender;
        r.proc(r.user, &f, ts, bus, frames, io);
    }
    return noErr;
}

OSStatus reset(void* p, AudioUnitScope, AudioUnitElement) {
    Instance* in = self(p);
    NoteEvent drop;
    while (in->midiIn.pop(drop)) {}
    in->engine.reset();
    return noErr;
}
OSStatus initialize(void* p) {
    Instance* in = self(p);
    in->prepare();
    in->initialized = true;
    return noErr;
}
OSStatus uninitialize(void* p) {
    self(p)->initialized = false;
    return noErr;
}

OSStatus addListener(void* p, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc, void* user) {
    self(p)->listeners.push_back(Listener{id, proc, user});
    return noErr;
}
OSStatus removeListener(void* p, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc) {
    auto& v = self(p)->listeners;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Listener& l) { return l.id == id && l.proc == proc; }), v.end());
    return noErr;
}
OSStatus removeListenerUser(void* p, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc, void* user) {
    auto& v = self(p)->listeners;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Listener& l) { return l.id == id && l.proc == proc && l.user == user; }), v.end());
    return noErr;
}
OSStatus addRenderNotify(void* p, AURenderCallback proc, void* user) {
    self(p)->notifies.push_back(RenderNotify{proc, user});
    return noErr;
}
OSStatus removeRenderNotify(void* p, AURenderCallback proc, void* user) {
    auto& v = self(p)->notifies;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const RenderNotify& r) { return r.proc == proc && r.user == user; }), v.end());
    return noErr;
}

// --- table des sélecteurs ---------------------------------------------------------------------------
OSStatus open(void* p, AudioComponentInstance ci) {
    self(p)->ci = ci;
    return noErr;
}
OSStatus close(void* p) {
    delete self(p);
    return noErr;
}

AudioComponentMethod lookup(SInt16 sel) {
    switch (sel) {
        case kAudioUnitInitializeSelect: return (AudioComponentMethod)initialize;
        case kAudioUnitUninitializeSelect: return (AudioComponentMethod)uninitialize;
        case kAudioUnitGetPropertyInfoSelect: return (AudioComponentMethod)getPropertyInfo;
        case kAudioUnitGetPropertySelect: return (AudioComponentMethod)getProperty;
        case kAudioUnitSetPropertySelect: return (AudioComponentMethod)setProperty;
        case kAudioUnitAddPropertyListenerSelect: return (AudioComponentMethod)addListener;
        case kAudioUnitRemovePropertyListenerSelect: return (AudioComponentMethod)removeListener;
        case kAudioUnitRemovePropertyListenerWithUserDataSelect: return (AudioComponentMethod)removeListenerUser;
        case kAudioUnitAddRenderNotifySelect: return (AudioComponentMethod)addRenderNotify;
        case kAudioUnitRemoveRenderNotifySelect: return (AudioComponentMethod)removeRenderNotify;
        case kAudioUnitGetParameterSelect: return (AudioComponentMethod)getParameter;
        case kAudioUnitSetParameterSelect: return (AudioComponentMethod)setParameter;
        case kAudioUnitScheduleParametersSelect: return (AudioComponentMethod)scheduleParameters;
        case kAudioUnitRenderSelect: return (AudioComponentMethod)render;
        case kAudioUnitResetSelect: return (AudioComponentMethod)reset;
        case kMusicDeviceMIDIEventSelect: return (AudioComponentMethod)midiEvent;
        case kMusicDeviceSysExSelect: return (AudioComponentMethod)sysEx;
        default: return nullptr;
    }
}

} // namespace raw2::au

// --- usine ------------------------------------------------------------------------------------------
extern "C" __attribute__((visibility("default"))) void* RAW2AUFactory(const AudioComponentDescription* desc) {
    (void)desc;
    auto* in = new raw2::au::Instance();
    in->plug.inst = in;
    in->plug.iface.Open = raw2::au::open;
    in->plug.iface.Close = raw2::au::close;
    in->plug.iface.Lookup = raw2::au::lookup;
    in->plug.iface.reserved = nullptr;
    return &in->plug;
}

// --- vue Cocoa de l'Audio Unit ------------------------------------------------------------------------
// Conteneur : garde la vue de l'éditeur (MacView) et suit le zoom (taille demandée par l'éditeur).
@interface RAW2AUContainerView : NSView
- (instancetype)initWithInstance:(raw2::au::Instance*)instance;
@end

namespace {
struct ContainerHost : raw2::MacView::Host {
    __weak NSView* view = nil;
    void requestResize(int w, int h) override {
        NSView* v = view;
        if (v) [v setFrameSize:NSMakeSize(w, h)];
    }
};
} // namespace

@implementation RAW2AUContainerView {
    raw2::au::Instance* _instance;
    std::unique_ptr<ContainerHost> _host;
    std::unique_ptr<raw2::MacView> _editor;
}

- (instancetype)initWithInstance:(raw2::au::Instance*)instance {
    int w = 1280, h = 960;
    self = [super initWithFrame:NSMakeRect(0, 0, w, h)];
    if (!self) return nil;
    _instance = instance;
    _host = std::make_unique<ContainerHost>();
    _host->view = self;
    _editor = std::make_unique<raw2::MacView>(instance->model, *instance, instance->uiState, _host.get());
    _editor->preferredSize(w, h);
    [self setFrameSize:NSMakeSize(w, h)];
    _editor->attach((__bridge void*)self);
    instance->liveView = _editor.get();
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    if (_editor) _editor->setSize((int)size.width, (int)size.height);
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (!self.window && _editor) {  // fenêtre de l'hôte fermée : on arrête de dessiner
        _editor->detach();
        if (_instance && _instance->liveView == _editor.get()) _instance->liveView = nullptr;
    } else if (self.window && _editor && !_editor->nsview()) {
        _editor->attach((__bridge void*)self);
        if (_instance) _instance->liveView = _editor.get();
    }
}

- (void)dealloc {
    if (_editor) {
        _editor->detach();
        if (_instance && _instance->liveView == _editor.get()) _instance->liveView = nullptr;
    }
}

@end

@interface RAW2AUViewFactory : NSObject <AUCocoaUIBase>
@end

@implementation RAW2AUViewFactory
- (unsigned)interfaceVersion { return 0; }
- (NSString*)description { return @"RAW 2"; }
- (NSView*)uiViewForAudioUnit:(AudioUnit)unit withSize:(NSSize)size {
    (void)size;
    void* ptr = nullptr;
    UInt32 sz = sizeof(ptr);
    if (AudioUnitGetProperty(unit, raw2::au::kInstanceProperty, kAudioUnitScope_Global, 0, &ptr, &sz) != noErr || !ptr) return nil;
    return [[RAW2AUContainerView alloc] initWithInstance:(raw2::au::Instance*)ptr];
}
@end
