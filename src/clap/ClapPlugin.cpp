// CLAP wrapper, written directly against the CLAP C headers (no helper library).
#include "../core/PluginCore.h"
#include "../core/SpscQueue.h"
#include "../platform/PlatformView.h"
#include "../ui/Editor.h"

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef TW_VERSION_STRING
#define TW_VERSION_STRING "1.0.0"
#endif

namespace {

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_MIXING, CLAP_PLUGIN_FEATURE_MASTERING,
                                 CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_COMPRESSOR, CLAP_PLUGIN_FEATURE_LIMITER,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT, "com.twisted.monomi", "Twisted", "Twisted Audio", "", "", "", TW_VERSION_STRING,
    "Mix assistant with audio recognition, oversampled saturation, clipper and limiter", kFeatures};

#if defined(_WIN32)
const char* const kGuiApi = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
const char* const kGuiApi = CLAP_WINDOW_API_COCOA;
#else
const char* const kGuiApi = CLAP_WINDOW_API_X11;
#endif

struct Edit {
    uint32_t id = 0;
    float value = 0;
    uint8_t kind = 0;  // 0 begin, 1 value, 2 end
};

class ClapPlugin final : public tw::ParamHost {
public:
    explicit ClapPlugin(const clap_host_t* host) : host_(host) {
        plugin_.desc = &kDescriptor;
        plugin_.plugin_data = this;
        plugin_.init = [](const clap_plugin_t* p) { return self(p)->init(); };
        plugin_.destroy = [](const clap_plugin_t* p) { delete self(p); };
        plugin_.activate = [](const clap_plugin_t* p, double sr, uint32_t minF, uint32_t maxF) { return self(p)->activate(sr, minF, maxF); };
        plugin_.deactivate = [](const clap_plugin_t* p) { self(p)->core_.deactivate(); self(p)->active_ = false; };
        plugin_.start_processing = [](const clap_plugin_t* p) { self(p)->processing_ = true; return true; };
        plugin_.stop_processing = [](const clap_plugin_t* p) { self(p)->processing_ = false; };
        plugin_.reset = [](const clap_plugin_t* p) { self(p)->core_.engine().reset(); };
        plugin_.process = [](const clap_plugin_t* p, const clap_process_t* pr) { return self(p)->process(pr); };
        plugin_.get_extension = [](const clap_plugin_t* p, const char* id) { return self(p)->extension(id); };
        plugin_.on_main_thread = [](const clap_plugin_t*) {};
    }

    const clap_plugin_t* clap() const { return &plugin_; }

    // ---- tw::ParamHost (GUI thread) ----
    void beginEdit(uint32_t id) override { pushEdit({id, 0, 0}); }
    void performEdit(uint32_t id, float v) override {
        core_.params.set(id, v);
        pushEdit({id, core_.params.get(id), 1});
    }
    void endEdit(uint32_t id) override { pushEdit({id, 0, 2}); }

private:
    static ClapPlugin* self(const clap_plugin_t* p) { return static_cast<ClapPlugin*>(p->plugin_data); }

    bool init() {
        hostParams_ = static_cast<const clap_host_params_t*>(host_->get_extension(host_, CLAP_EXT_PARAMS));
        return true;
    }

    bool activate(double sr, uint32_t, uint32_t maxFrames) {
        core_.activate(sr, (int)maxFrames);
        silence_.assign(std::max<uint32_t>(maxFrames, 64), 0.f);
        scratch_.assign(std::max<uint32_t>(maxFrames, 64), 0.f);
        active_ = true;
        return true;
    }

    void pushEdit(const Edit& e) {
        edits_.push(e);
        if (hostParams_ && hostParams_->request_flush && !processing_.load()) hostParams_->request_flush(host_);
    }

    void drainEdits(const clap_output_events_t* out) {
        Edit e;
        while (edits_.pop(e)) {
            if (!out) continue;
            if (e.kind == 1) {
                clap_event_param_value_t ev{};
                ev.header = {sizeof ev, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
                ev.param_id = e.id;
                ev.note_id = -1; ev.port_index = -1; ev.channel = -1; ev.key = -1;
                ev.value = e.value;
                out->try_push(out, &ev.header);
            } else {
                clap_event_param_gesture_t ev{};
                ev.header = {sizeof ev, 0, CLAP_CORE_EVENT_SPACE_ID,
                             (uint16_t)(e.kind == 0 ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END), 0};
                ev.param_id = e.id;
                out->try_push(out, &ev.header);
            }
        }
    }

    void handleEvent(const clap_event_header_t* h) {
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
        const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
        core_.params.set(ev->param_id, (float)ev->value);
    }

    clap_process_status process(const clap_process_t* p) {
        drainEdits(p->out_events);
        const uint32_t n = p->frames_count;
        const uint32_t numEvents = p->in_events->size(p->in_events);
        const float *inL = silence_.data(), *inR = silence_.data();
        if (p->audio_inputs_count > 0 && p->audio_inputs[0].data32 && p->audio_inputs[0].channel_count > 0) {
            inL = p->audio_inputs[0].data32[0];
            inR = p->audio_inputs[0].channel_count > 1 ? p->audio_inputs[0].data32[1] : inL;
        }
        if (p->audio_outputs_count == 0 || !p->audio_outputs[0].data32) return CLAP_PROCESS_CONTINUE;
        float* outL = p->audio_outputs[0].data32[0];
        float* outR = p->audio_outputs[0].channel_count > 1 ? p->audio_outputs[0].data32[1] : nullptr;
        if (!outR) outR = scratch_.data();

        // Split the block at parameter events (sample-accurate automation).
        uint32_t pos = 0, ei = 0;
        while (pos < n) {
            while (ei < numEvents) {
                const clap_event_header_t* h = p->in_events->get(p->in_events, ei);
                if (h->time > pos) break;
                handleEvent(h);
                ++ei;
            }
            uint32_t next = n;
            if (ei < numEvents) next = std::min(n, p->in_events->get(p->in_events, ei)->time);
            if (next <= pos) next = pos + 1;
            core_.process(inL + pos, inR + pos, outL + pos, outR + pos, (int)(next - pos));
            pos = next;
        }
        for (; ei < numEvents; ++ei) handleEvent(p->in_events->get(p->in_events, ei));
        return CLAP_PROCESS_CONTINUE;
    }

    // ------------------------------------------------------------------ extensions
    const void* extension(const char* id) {
        if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audioPorts_;
        if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &params_;
        if (!std::strcmp(id, CLAP_EXT_STATE)) return &state_;
        if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &latency_;
        if (!std::strcmp(id, CLAP_EXT_GUI)) return &gui_;
        return nullptr;
    }

    static const clap_plugin_audio_ports_t audioPorts_;
    static const clap_plugin_params_t params_;
    static const clap_plugin_state_t state_;
    static const clap_plugin_latency_t latency_;
    static const clap_plugin_gui_t gui_;

    clap_plugin_t plugin_{};
    const clap_host_t* host_;
    const clap_host_params_t* hostParams_ = nullptr;
    tw::PluginCore core_;
    tw::SpscQueue<Edit> edits_{4096};
    std::vector<float> silence_, scratch_;
    std::atomic<bool> active_{false}, processing_{false};
    std::unique_ptr<tw::Editor> editor_;
    std::unique_ptr<tw::PlatformView> view_;

    friend struct Ext;
};

struct Ext {
    static ClapPlugin* s(const clap_plugin_t* p) { return static_cast<ClapPlugin*>(p->plugin_data); }

    // audio ports: one stereo in, one stereo out, processed in place
    static uint32_t portCount(const clap_plugin_t*, bool) { return 1; }
    static bool portGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
        if (index != 0) return false;
        info->id = 0;
        std::snprintf(info->name, sizeof info->name, "%s", isInput ? "Input" : "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = 0;
        return true;
    }

    // params
    static uint32_t paramCount(const clap_plugin_t*) { return tw::kNumParams; }
    static bool paramInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
        if (index >= tw::kNumParams) return false;
        const tw::ParamInfo& p = tw::paramInfo(index);
        std::memset(info, 0, sizeof *info);
        info->id = p.id;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        if (p.steps > 0) info->flags |= CLAP_PARAM_IS_STEPPED;
        if (p.isBypass) info->flags |= CLAP_PARAM_IS_BYPASS;
        if (p.id == tw::kCameraBass) info->flags = CLAP_PARAM_IS_STEPPED;  // GUI setting, not automatable
        std::snprintf(info->name, sizeof info->name, "%s", p.name);
        info->min_value = p.min;
        info->max_value = p.max;
        info->default_value = p.def;
        return true;
    }
    static bool paramValue(const clap_plugin_t* pl, clap_id id, double* v) {
        if (id >= tw::kNumParams) return false;
        *v = s(pl)->core_.params.get(id);
        return true;
    }
    static bool paramToText(const clap_plugin_t*, clap_id id, double v, char* out, uint32_t size) {
        if (id >= tw::kNumParams) return false;
        std::snprintf(out, size, "%s", tw::formatParam(id, (float)v).c_str());
        return true;
    }
    static bool paramFromText(const clap_plugin_t*, clap_id id, const char* text, double* v) {
        float f;
        if (id >= tw::kNumParams || !tw::parseParam(id, text, f)) return false;
        *v = f;
        return true;
    }
    static void paramFlush(const clap_plugin_t* pl, const clap_input_events_t* in, const clap_output_events_t* out) {
        ClapPlugin* p = s(pl);
        if (in)
            for (uint32_t i = 0; i < in->size(in); ++i) p->handleEvent(in->get(in, i));
        p->drainEdits(out);
    }

    // state
    static bool stateSave(const clap_plugin_t* pl, const clap_ostream_t* os) {
        const std::string data = s(pl)->core_.saveState();
        size_t done = 0;
        while (done < data.size()) {
            const int64_t w = os->write(os, data.data() + done, data.size() - done);
            if (w <= 0) return false;
            done += (size_t)w;
        }
        return true;
    }
    static bool stateLoad(const clap_plugin_t* pl, const clap_istream_t* is) {
        std::string data;
        char buf[4096];
        for (;;) {
            const int64_t r = is->read(is, buf, sizeof buf);
            if (r < 0) return false;
            if (r == 0) break;
            data.append(buf, (size_t)r);
        }
        s(pl)->core_.loadState(data);
        return true;
    }

    static uint32_t latency(const clap_plugin_t*) { return 0; }

    // gui
    static bool guiSupported(const clap_plugin_t*, const char* api, bool floating) { return !floating && !std::strcmp(api, kGuiApi); }
    static bool guiPreferred(const clap_plugin_t*, const char** api, bool* floating) { *api = kGuiApi; *floating = false; return true; }
    static bool guiCreate(const clap_plugin_t* pl, const char* api, bool floating) {
        if (floating || std::strcmp(api, kGuiApi)) return false;
        ClapPlugin* p = s(pl);
        p->editor_ = std::make_unique<tw::Editor>(p->core_, *p);
        p->view_ = tw::PlatformView::create(*p->editor_);
        return true;
    }
    static void guiDestroy(const clap_plugin_t* pl) {
        ClapPlugin* p = s(pl);
        p->view_.reset();
        p->editor_.reset();
    }
    static bool guiSetScale(const clap_plugin_t* pl, double scale) {
#if defined(__APPLE__)
        (void)pl; (void)scale;
        return false;  // macOS uses points, the scale comes from the window
#else
        if (s(pl)->view_) s(pl)->view_->setScale((float)scale);
        return true;
#endif
    }
    static bool guiGetSize(const clap_plugin_t* pl, uint32_t* w, uint32_t* h) {
        if (!s(pl)->view_) return false;
        int iw, ih;
        s(pl)->view_->size(iw, ih);
        *w = (uint32_t)iw;
        *h = (uint32_t)ih;
        return true;
    }
    static bool guiCanResize(const clap_plugin_t*) { return false; }
    static bool guiResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
    static bool guiAdjust(const clap_plugin_t* pl, uint32_t* w, uint32_t* h) { return guiGetSize(pl, w, h); }
    static bool guiSetSize(const clap_plugin_t* pl, uint32_t w, uint32_t h) {
        uint32_t cw, ch;
        return guiGetSize(pl, &cw, &ch) && cw == w && ch == h;
    }
    static bool guiSetParent(const clap_plugin_t* pl, const clap_window_t* win) {
        if (!s(pl)->view_ || std::strcmp(win->api, kGuiApi)) return false;
#if defined(_WIN32)
        return s(pl)->view_->attach(win->win32);
#elif defined(__APPLE__)
        return s(pl)->view_->attach(win->cocoa);
#else
        return s(pl)->view_->attach((void*)(uintptr_t)win->x11);
#endif
    }
    static bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
    static void guiSuggestTitle(const clap_plugin_t*, const char*) {}
    static bool guiShow(const clap_plugin_t*) { return true; }
    static bool guiHide(const clap_plugin_t*) { return true; }
};

const clap_plugin_audio_ports_t ClapPlugin::audioPorts_ = {&Ext::portCount, &Ext::portGet};
const clap_plugin_params_t ClapPlugin::params_ = {&Ext::paramCount, &Ext::paramInfo, &Ext::paramValue,
                                                  &Ext::paramToText, &Ext::paramFromText, &Ext::paramFlush};
const clap_plugin_state_t ClapPlugin::state_ = {&Ext::stateSave, &Ext::stateLoad};
const clap_plugin_latency_t ClapPlugin::latency_ = {&Ext::latency};
const clap_plugin_gui_t ClapPlugin::gui_ = {&Ext::guiSupported, &Ext::guiPreferred, &Ext::guiCreate, &Ext::guiDestroy,
                                            &Ext::guiSetScale, &Ext::guiGetSize, &Ext::guiCanResize, &Ext::guiResizeHints,
                                            &Ext::guiAdjust, &Ext::guiSetSize, &Ext::guiSetParent, &Ext::guiSetTransient,
                                            &Ext::guiSuggestTitle, &Ext::guiShow, &Ext::guiHide};

// ---------------------------------------------------------------------------- factory / entry
uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? &kDescriptor : nullptr; }
const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, kDescriptor.id)) return nullptr;
    return (new ClapPlugin(host))->clap();
}
const clap_plugin_factory_t kFactory = {&factoryCount, &factoryDescriptor, &factoryCreate};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryFactory(const char* id) { return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &kFactory : nullptr; }

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, &entryInit, &entryDeinit, &entryFactory};
