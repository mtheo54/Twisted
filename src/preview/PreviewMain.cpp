// Twisted Preview: opens the plugin window without a DAW. A background thread plays a
// synthetic groove through the real engine (silently, there is no audio output) so the
// analysis thread, the waves and the camera react exactly as they do in a host.
#include "../core/PluginCore.h"
#include "../platform/PlatformView.h"
#include "../ui/Editor.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {
class DirectHost final : public tw::ParamHost {
public:
    explicit DirectHost(tw::PluginCore& c) : core_(c) {}
    void beginEdit(uint32_t) override {}
    void performEdit(uint32_t id, float v) override { core_.params.set(id, v); }
    void endEdit(uint32_t) override {}

private:
    tw::PluginCore& core_;
};
} // namespace

int main() {
    tw::PluginCore core;
    const double fs = 48000.0;
    const int block = 256;
    core.activate(fs, block);
    // Optional start values, e.g. TWISTED_PREVIEW_PARAMS="mix=10;bypass=1" (keys from Params.cpp)
    if (const char* init = std::getenv("TWISTED_PREVIEW_PARAMS")) {
        std::string s(init);
        for (char& c : s) if (c == ';') c = '\n';
        core.params.deserialize(s);
    }

    // TWISTED_PREVIEW_LICENSED n'existe plus : la licence est désormais un vrai contrôleur
    // (core.license), qui lit le fichier de licence du disque et parle au serveur, exactement
    // comme dans un vrai DAW. Pour prévisualiser "sans licence" / "avec licence", il suffit de
    // lancer cet aperçu sans puis avec une licence activée — pas besoin de la simuler.

    std::atomic<bool> running{true};
    std::thread audio([&] {
        std::vector<float> L(block), R(block), oL(block), oR(block);
        long t = 0;
        auto next = std::chrono::steady_clock::now();
        while (running.load()) {
            for (int i = 0; i < block; ++i, ++t) {
                const double s = t / fs, beat = std::fmod(s, 60.0 / 92.0), half = std::fmod(s, 30.0 / 92.0);
                const double kick = std::sin(2 * 3.14159265358979323846 * (45 + 110 * std::exp(-beat * 30)) * beat) * std::exp(-beat * 7);
                const double hat = (half < 0.03 ? ((t * 1103515245 + 12345) >> 16 & 0x7fff) / 16384.0 - 1.0 : 0.0) * 0.12;
                const double pad = 0.08 * std::sin(2 * 3.14159265358979323846 * 110 * s) + 0.05 * std::sin(2 * 3.14159265358979323846 * 164.8 * s);
                L[(size_t)i] = (float)(0.55 * kick + hat + pad);
                R[(size_t)i] = (float)(0.55 * kick - hat * 0.6 + pad);
            }
            core.process(L.data(), R.data(), oL.data(), oR.data(), block);
            next += std::chrono::microseconds((long long)(block / fs * 1e6));
            std::this_thread::sleep_until(next);
        }
    });

    DirectHost host(core);
    tw::Editor editor(core, host);
    auto view = tw::PlatformView::create(editor);
    const int rc = tw::runStandaloneWindow(*view, "Twisted Preview");
    view.reset();
    running = false;
    audio.join();
    core.deactivate();
    return rc;
}
