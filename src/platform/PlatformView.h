// Native child window hosting the editor with an OpenGL 3.2 context.
//   Windows: Win32 child HWND + WGL, rendered from a WM_TIMER on the UI thread.
//   macOS:   NSOpenGLView subclass, rendered from an NSTimer on the main thread.
//   Linux:   X11 child window + GLX, rendered from its own thread (for testing).
#pragma once

#include <memory>

namespace tw {

class Editor;

class PlatformView {
public:
    virtual ~PlatformView() = default;
    // parent: HWND, NSView* or X11 Window id, provided by the host.
    virtual bool attach(void* parent) = 0;
    virtual void detach() = 0;
    // Device pixels per logical pixel, as told by the host (Windows / Linux). 0 = ask the system.
    virtual void setScale(float scale) = 0;
    // Window size in the unit the host expects (physical pixels on Windows/Linux, points on macOS).
    virtual void size(int& w, int& h) const = 0;

    static std::unique_ptr<PlatformView> create(Editor& editor);
};

// Preview app only: opens a top-level window holding the view and runs the event loop
// until the window is closed.
int runStandaloneWindow(PlatformView& view, const char* title);

} // namespace tw
