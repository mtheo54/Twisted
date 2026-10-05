// macOS: NSOpenGLView subclass with an OpenGL 3.2 core context (Retina aware, 4x MSAA).
// OpenGL is deprecated on macOS but still shipped and supported, including on Apple Silicon.
#define GL_SILENCE_DEPRECATION 1
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl3.h>

#include "PlatformView.h"

#include "../ui/Editor.h"
#include "../ui/Gl.h"

// Objective-C class names are global to the process: suffix them per build so two
// versions of the plugin loaded in the same host never clash.
#ifndef TW_OBJC_SUFFIX
#define TW_OBJC_SUFFIX 1_0_0
#endif
#define TW_CONCAT2(a, b) a##b
#define TW_CONCAT(a, b) TW_CONCAT2(a, b)
#define TwistedGLView TW_CONCAT(TwistedGLView_, TW_OBJC_SUFFIX)

@interface TwistedGLView : NSOpenGLView {
@public
    tw::Editor* editor;
    NSTimer* timer;
    NSTrackingArea* tracking;
    BOOL glReady;
}
@end

#define TwistedPreviewDelegate TW_CONCAT(TwistedPreviewDelegate_, TW_OBJC_SUFFIX)
@interface TwistedPreviewDelegate : NSObject <NSApplicationDelegate>
@end
@implementation TwistedPreviewDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { (void)app; return YES; }
@end

@implementation TwistedGLView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { (void)e; return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)prepareOpenGL {
    [super prepareOpenGL];
    [[self openGLContext] makeCurrentContext];
    GLint swap = 1;
    [[self openGLContext] setValues:&swap forParameter:NSOpenGLContextParameterSwapInterval];
    tw::loadGl(nullptr);
    const NSSize backing = [self convertSizeToBacking:NSMakeSize(1, 1)];
    editor->glInit((float)backing.width);
    glReady = YES;
}

- (void)tick:(NSTimer*)t {
    (void)t;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    if (!editor) return;
    [[self openGLContext] makeCurrentContext];
    if (!glReady) [self prepareOpenGL];
    const NSRect px = [self convertRectToBacking:[self bounds]];
    editor->render((int)px.size.width, (int)px.size.height);
    [[self openGLContext] flushBuffer];
}

- (void)updateTrackingAreas {
    if (tracking) [self removeTrackingArea:tracking];
    tracking = [[NSTrackingArea alloc] initWithRect:[self bounds]
                                            options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
                                              owner:self
                                           userInfo:nil];
    [self addTrackingArea:tracking];
    [super updateTrackingAreas];
}

- (NSPoint)local:(NSEvent*)e { return [self convertPoint:[e locationInWindow] fromView:nil]; }
- (BOOL)shift:(NSEvent*)e { return ([e modifierFlags] & NSEventModifierFlagShift) != 0; }

- (void)mouseDown:(NSEvent*)e { NSPoint p = [self local:e]; if (editor) editor->mouseDown((float)p.x, (float)p.y, [self shift:e]); }
- (void)mouseUp:(NSEvent*)e { NSPoint p = [self local:e]; if (editor) editor->mouseUp((float)p.x, (float)p.y); }
- (void)mouseDragged:(NSEvent*)e { NSPoint p = [self local:e]; if (editor) editor->mouseMove((float)p.x, (float)p.y, [self shift:e]); }
- (void)mouseMoved:(NSEvent*)e { NSPoint p = [self local:e]; if (editor) editor->mouseMove((float)p.x, (float)p.y, [self shift:e]); }
- (void)mouseExited:(NSEvent*)e { (void)e; if (editor) editor->mouseLeave(); }
- (void)scrollWheel:(NSEvent*)e {
    NSPoint p = [self local:e];
    const CGFloat dy = [e hasPreciseScrollingDeltas] ? [e scrollingDeltaY] / 10.0 : [e deltaY];
    if (editor && dy != 0) editor->mouseWheel((float)p.x, (float)p.y, (float)dy, [self shift:e]);
}

@end

namespace tw {
namespace {

class CocoaView final : public PlatformView {
public:
    explicit CocoaView(Editor& e) : editor_(e) {}
    ~CocoaView() override { detach(); }

    void setScale(float) override {}  // macOS works in points; the backing scale is read from the window
    void size(int& w, int& h) const override { w = Editor::kWidth; h = Editor::kHeight; }

    bool attach(void* parent) override {
        detach();
        NSOpenGLPixelFormatAttribute attrs[] = {NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
                                                NSOpenGLPFAColorSize, 24, NSOpenGLPFAAlphaSize, 8,
                                                NSOpenGLPFADepthSize, 24, NSOpenGLPFADoubleBuffer,
                                                NSOpenGLPFAAccelerated, NSOpenGLPFAMultisample,
                                                NSOpenGLPFASampleBuffers, 1, NSOpenGLPFASamples, 4, 0};
        NSOpenGLPixelFormat* pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if (!pf) {
            NSOpenGLPixelFormatAttribute basic[] = {NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
                                                    NSOpenGLPFAColorSize, 24, NSOpenGLPFADepthSize, 24,
                                                    NSOpenGLPFADoubleBuffer, 0};
            pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:basic];
        }
        if (!pf) return false;
        view_ = [[TwistedGLView alloc] initWithFrame:NSMakeRect(0, 0, Editor::kWidth, Editor::kHeight) pixelFormat:pf];
        if (!view_) return false;
        view_->editor = &editor_;
        view_->glReady = NO;
        [view_ setWantsBestResolutionOpenGLSurface:YES];
        [(__bridge NSView*)parent addSubview:view_];
        view_->timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:view_ selector:@selector(tick:) userInfo:nil repeats:YES];
        [[NSRunLoop currentRunLoop] addTimer:view_->timer forMode:NSRunLoopCommonModes];
        return true;
    }

    void detach() override {
        if (!view_) return;
        [view_->timer invalidate];
        view_->timer = nil;
        if (view_->glReady) {
            [[view_ openGLContext] makeCurrentContext];
            editor_.glDestroy();
            view_->glReady = NO;
        }
        view_->editor = nullptr;
        [view_ removeFromSuperview];
        view_ = nil;
    }

private:
    Editor& editor_;
    TwistedGLView* view_ = nil;
};
} // namespace

std::unique_ptr<PlatformView> PlatformView::create(Editor& editor) { return std::make_unique<CocoaView>(editor); }

int runStandaloneWindow(PlatformView& view, const char* title) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        TwistedPreviewDelegate* delegate = [[TwistedPreviewDelegate alloc] init];
        [NSApp setDelegate:delegate];
        NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, Editor::kWidth, Editor::kHeight)
                                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        [win setTitle:[NSString stringWithUTF8String:title]];
        [win center];
        if (!view.attach((__bridge void*)[win contentView])) return 1;
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
        view.detach();
    }
    return 0;
}

} // namespace tw
