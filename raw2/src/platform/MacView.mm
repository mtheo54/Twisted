// Vue Cocoa de RAW 2 : NSView « flipped » (y vers le bas, comme la maquette), dessinée avec
// CoreGraphics par CGCanvas, cadencée par un NSTimer à 60 images/s. Souris, molette, clavier
// (seulement pendant une saisie ou un panneau ouvert : sinon les touches restent à l'hôte),
// glisser-déposer de fichiers (WAV, packs) et glisser du son rendu vers la DAW.
// Compilé avec ARC.
#import <Cocoa/Cocoa.h>

#include "platform/MacView.h"
#include "ui/CGCanvas.h"

#include <algorithm>
#include <cmath>
#include <string>

#ifndef RAW2_OBJC_SUFFIX
#define RAW2_OBJC_SUFFIX dev
#endif
#define RAW2_CAT2(a, b) a##b
#define RAW2_CAT(a, b) RAW2_CAT2(a, b)
#define RAW2EditorNSView RAW2_CAT(RAW2EditorNSView_, RAW2_OBJC_SUFFIX)

namespace raw2 {
struct MacViewCore;
}

@interface RAW2EditorNSView : NSView <NSDraggingSource> {
@public
    raw2::MacViewCore* core;
    NSTimer* timer;
    NSTrackingArea* tracking;
}
@end

namespace raw2 {

namespace {
double nowMs() { return [[NSProcessInfo processInfo] systemUptime] * 1000.0; }

void appendUtf8(std::string& o, uint32_t c) {
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
}
} // namespace

// Partie C++ de la vue (l'objet Objective-C n'en garde qu'un pointeur).
struct MacViewCore {
    Model& model;
    ParamSink& sink;
    ui::EditorState& state;
    MacView::Host* host;
    ui::Editor editor;
    ui::CGCanvas canvas;
    ui::Input input;
    RAW2EditorNSView* view = nil;
    int zoomShown = 100;
    double drawMs = 0.0;     // temps moyen d'une image (lissé)
    bool slow = false;       // vrai : 30 images/s au lieu de 60
    unsigned tickN = 0;
    std::string pendingDrag;  // WAV demandé par l'éditeur : le glisser part au prochain mouseDragged
    int width = ui::Editor::kWidth, height = ui::Editor::kHeight;  // points

    MacViewCore(Model& m, ParamSink& s, ui::EditorState& st, MacView::Host* h) : model(m), sink(s), state(st), host(h), editor(m, s, st) {}

    void wantedSize(int& w, int& h) const {
        const float z = (float)model.zoomPct.load() / 100.f;
        w = (int)std::lround(ui::Editor::kWidth * z);
        h = (int)std::lround(ui::Editor::kHeight * z);
    }
    float scale() const { return (float)width / (float)ui::Editor::kWidth; }
    ui::Pt toLogical(NSPoint p) const { return ui::Pt{(float)p.x / scale(), (float)p.y / scale()}; }

    void draw(CGContextRef ctx) {
        const double t0 = nowMs();
        input.timeMs = t0;
        input.shift = ([NSEvent modifierFlags] & NSEventModifierFlagShift) != 0;
        canvas.setContext(ctx);
        CGContextSaveGState(ctx);
        CGContextScaleCTM(ctx, scale(), scale());
        editor.frame(canvas, input);
        CGContextRestoreGState(ctx);
        canvas.frameDone();
        // cadence adaptative : si une image coûte plus de 12 ms (Mac lent, Retina, page VIE),
        // on passe à 30 images/s pour laisser le fil principal à l'hôte ; retour à 60 sous 8 ms
        drawMs = drawMs * 0.9 + (nowMs() - t0) * 0.1;
        if (drawMs > 12.0) slow = true;
        else if (drawMs < 8.0) slow = false;
        // les évènements ponctuels ne valent que pour une image
        input.pressed = input.released = input.rightPressed = false;
        input.wheel = 0.f;
        input.typed.clear();
        input.keyEsc = input.keyEnter = input.keyBack = input.keyDelete = false;
        input.keyLeft = input.keyRight = input.keyHome = input.keyEnd = false;
        input.dropped.clear();
    }

    // après chaque image : glisser sortant demandé, curseur, zoom
    void afterFrame() {
        const std::string out = editor.takeDragOut();
        if (!out.empty()) pendingDrag = out;
        if (view && view.window) {  // curseur seulement quand la souris est sur la vue
            const NSPoint mp = [view convertPoint:[view.window mouseLocationOutsideOfEventStream] fromView:nil];
            if (NSPointInRect(mp, view.bounds)) {
                NSCursor* c = [NSCursor arrowCursor];
                switch (editor.cursor()) {
                    case ui::Cursor::Hand: c = [NSCursor pointingHandCursor]; break;
                    case ui::Cursor::ResizeNS: c = [NSCursor resizeUpDownCursor]; break;
                    case ui::Cursor::ResizeEW: c = [NSCursor resizeLeftRightCursor]; break;
                    case ui::Cursor::Cross: c = [NSCursor crosshairCursor]; break;
                    case ui::Cursor::Text: c = [NSCursor IBeamCursor]; break;
                    default: break;
                }
                [c set];
            }
        }
        if (model.zoomPct.load() != zoomShown) {  // zoom choisi dans le panneau des thèmes
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
        if (view) [view setFrameSize:NSMakeSize(width, height)];
    }

    bool key(int vk) {
        switch (vk) {
            case 0x1B: input.keyEsc = true; return true;
            case 0x0D: input.keyEnter = true; return true;
            case 0x08: input.keyBack = true; return true;
            case 0x2E: input.keyDelete = true; return true;
            case 0x25: input.keyLeft = true; return true;
            case 0x27: input.keyRight = true; return true;
            case 0x24: input.keyHome = true; return true;
            case 0x23: input.keyEnd = true; return true;
            default: return false;
        }
    }
    // touches Mac (kVK_*) -> codes Windows de key()
    static int vkFromMac(unsigned short code) {
        switch (code) {
            case 53: return 0x1B;            // Échap
            case 36: case 76: return 0x0D;   // Retour, Entrée
            case 51: return 0x08;            // Effacement arrière
            case 117: return 0x2E;           // Suppr
            case 123: return 0x25;           // ←
            case 124: return 0x27;           // →
            case 115: return 0x24;           // Début
            case 119: return 0x23;           // Fin
            default: return 0;
        }
    }

    // glisser du fichier rendu vers la DAW ou le Finder, depuis l'évènement de souris en cours
    void startDrag(NSEvent* e) {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:pendingDrag.c_str()]];
        pendingDrag.clear();
        if (url && view) {
            NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:url];
            const NSPoint p = [view convertPoint:[e locationInWindow] fromView:nil];
            NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:[url path]];
            [item setDraggingFrame:NSMakeRect(p.x - 16, p.y - 16, 32, 32) contents:icon];
            [view beginDraggingSessionWithItems:@[ item ] event:e source:view];
        }
        input.down = false;  // la session de glisser garde la souris jusqu'au lâcher
        input.released = true;
    }

    std::vector<std::string> filesFrom(id<NSDraggingInfo> info) {
        std::vector<std::string> out;
        NSArray* urls = [[info draggingPasteboard] readObjectsForClasses:@[ [NSURL class] ]
                                                                 options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
        for (NSURL* u in urls)
            if ([u isFileURL] && [u path]) out.push_back(std::string([[u path] UTF8String]));
        return out;
    }
};

struct MacView::Impl {
    MacViewCore core;
    Impl(Model& m, ParamSink& s, ui::EditorState& st, Host* h) : core(m, s, st, h) {}
};

} // namespace raw2

@implementation RAW2EditorNSView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { (void)e; return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }

- (void)tick:(NSTimer*)t {
    (void)t;
    if (!core) return;
    NSWindow* w = self.window;
    // fenêtre cachée, réduite ou recouverte : on ne dessine pas
    if (!w || !([w occlusionState] & NSWindowOcclusionStateVisible) || [self isHiddenOrHasHiddenAncestor]) return;
    if (core->slow && (++core->tickN & 1)) return;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    if (!core) return;
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;
    core->draw(ctx);
    core->afterFrame();
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

- (void)mouseDown:(NSEvent*)e {
    if (!core) return;
    [self.window makeFirstResponder:self];
    core->input.mouse = core->toLogical([self local:e]);
    core->input.down = true;
    core->input.pressed = true;
}
- (void)mouseDragged:(NSEvent*)e {
    if (!core) return;
    core->input.mouse = core->toLogical([self local:e]);
    if (!core->pendingDrag.empty()) core->startDrag(e);
}
- (void)mouseUp:(NSEvent*)e {
    if (!core) return;
    core->input.mouse = core->toLogical([self local:e]);
    core->input.down = false;
    core->input.released = true;
    core->pendingDrag.clear();
}
- (void)mouseMoved:(NSEvent*)e {
    if (core) core->input.mouse = core->toLogical([self local:e]);
}
- (void)mouseExited:(NSEvent*)e {
    (void)e;
    if (core && !core->input.down) core->input.mouse = raw2::ui::Pt{-1000.f, -1000.f};
}
- (void)rightMouseDown:(NSEvent*)e {
    if (!core) return;
    core->input.mouse = core->toLogical([self local:e]);
    core->input.rightPressed = true;
}
- (void)scrollWheel:(NSEvent*)e {
    if (!core) return;
    const CGFloat dy = [e hasPreciseScrollingDeltas] ? [e scrollingDeltaY] / 12.0 : [e scrollingDeltaY];
    core->input.wheel += (float)dy;
}

// Le clavier n'est pris que pendant une saisie (nom de preset, recherche) ou un panneau ouvert
// (Échap le ferme) ; sinon la touche remonte à l'hôte (clavier MIDI d'Ableton, raccourcis).
- (void)keyDown:(NSEvent*)e {
    if (!core || (!core->editor.wantsKeyboard() && !core->editor.anyOverlayOpen())) {
        [super keyDown:e];
        return;
    }
    if (core->key(raw2::MacViewCore::vkFromMac([e keyCode]))) return;
    if (!core->editor.wantsKeyboard()) {
        [super keyDown:e];
        return;
    }
    if (([e modifierFlags] & NSEventModifierFlagCommand) && [[e charactersIgnoringModifiers] isEqualToString:@"v"]) {
        NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        if (s) core->input.typed += [s UTF8String];
        return;
    }
    NSString* chars = [e characters];
    for (NSUInteger i = 0; i < [chars length]; ++i) {
        const unichar ch = [chars characterAtIndex:i];
        if (ch < 0x20 || ch == 0x7F || (ch >= 0xF700 && ch <= 0xF8FF)) continue;  // touches de fonction
        uint32_t cp = ch;
        if (ch >= 0xD800 && ch < 0xDC00 && i + 1 < [chars length]) {
            const unichar lo = [chars characterAtIndex:++i];
            cp = 0x10000 + (((uint32_t)ch - 0xD800) << 10) + ((uint32_t)lo - 0xDC00);
        }
        raw2::appendUtf8(core->input.typed, cp);
    }
}
- (BOOL)performKeyEquivalent:(NSEvent*)e {
    if (core && core->editor.wantsKeyboard() && ([e modifierFlags] & NSEventModifierFlagCommand) &&
        [[e charactersIgnoringModifiers] isEqualToString:@"v"]) {
        [self keyDown:e];
        return YES;
    }
    return NO;
}

// --- glisser-déposer entrant -------------------------------------------------------------------
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
    if (!core) return NSDragOperationNone;
    core->input.dragOver = true;
    core->input.dragPos = core->toLogical([self convertPoint:[info draggingLocation] fromView:nil]);
    return NSDragOperationCopy;
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)info { return [self draggingEntered:info]; }
- (void)draggingExited:(id<NSDraggingInfo>)info {
    (void)info;
    if (core) core->input.dragOver = false;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
    if (!core) return NO;
    core->input.dragOver = false;
    core->input.dropped = core->filesFrom(info);
    core->input.dropPos = core->toLogical([self convertPoint:[info draggingLocation] fromView:nil]);
    return !core->input.dropped.empty();
}

// --- glisser sortant (son rendu vers la DAW) -----------------------------------------------------
- (NSDragOperation)draggingSession:(NSDraggingSession*)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    (void)session;
    (void)context;
    return NSDragOperationCopy;
}

@end

namespace raw2 {

MacView::MacView(Model& model, ParamSink& sink, ui::EditorState& state, Host* host) : d_(std::make_unique<Impl>(model, sink, state, host)) {}

MacView::~MacView() { detach(); }

bool MacView::attach(void* parent) {
    if (!parent) return false;
    detach();
    MacViewCore& c = d_->core;
    c.zoomShown = c.model.zoomPct.load();
    int w, h;
    c.wantedSize(w, h);
    c.width = w;
    c.height = h;
    RAW2EditorNSView* v = [[RAW2EditorNSView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    if (!v) return false;
    v->core = &c;
    c.view = v;
    [v registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    [(__bridge NSView*)parent addSubview:v];
    v->timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:v selector:@selector(tick:) userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:v->timer forMode:NSRunLoopCommonModes];
    return true;
}

void MacView::detach() {
    MacViewCore& c = d_->core;
    RAW2EditorNSView* v = c.view;
    if (!v) return;
    [v->timer invalidate];
    v->timer = nil;
    v->core = nullptr;
    [v unregisterDraggedTypes];
    [v removeFromSuperview];
    c.view = nil;
}

void MacView::preferredSize(int& w, int& h) const { d_->core.wantedSize(w, h); }
void MacView::setSize(int w, int h) { d_->core.setSize(w, h); }
void* MacView::nsview() const { return (__bridge void*)d_->core.view; }

bool MacView::hostKey(char16_t ch, int vk) {
    MacViewCore& c = d_->core;
    if (!c.editor.wantsKeyboard() && !c.editor.anyOverlayOpen()) return false;
    if (vk && c.key(vk)) return true;
    if (ch >= 0x20 && c.editor.wantsKeyboard()) {
        appendUtf8(c.input.typed, (uint32_t)ch);
        return true;
    }
    return false;
}

} // namespace raw2
