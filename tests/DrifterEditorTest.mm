// Drifter's window on macOS, opened the way a host opens it (libs/vst3host), with Section inside:
// the synth's own editor sits under the strip, Drifter's window is sized around it, hiding the
// lanes panel moves it up, and a missing synth closes it. Live itself (Windows) is on the
// checklist; this checks the mechanics. Saves a picture of the window when the screen allows.
#include "HostedInstrument.h"
#include "DrifterTestState.h"

#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace underheard::vst3host;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static void Pump(double secs) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:secs]]; }

// The synth's editor's container (Drifter's), anywhere under `v`.
static NSView* FindContainer(NSView* v)
{
  if ([NSStringFromClass([v class]) isEqualToString:@"DrifterEditorContainer"])
    return v;
  for (NSView* s in v.subviews)
    if (NSView* f = FindContainer(s))
      return f;
  return nil;
}

static void Snapshot(NSWindow* w, const char* path)
{
  Pump(0.3);
  const std::string cmd = "screencapture -x -o -l " + std::to_string((long)w.windowNumber) + " '" + path + "' 2>/dev/null";
  if (std::system(cmd.c_str()) == 0)
    printf("(picture: %s)\n", path);
  else
    printf("(no picture: the screen can't be captured from here)\n");
}

int main(int argc, char** argv)
{
  @autoreleasepool
  {
    const char* pictures = argc > 1 ? argv[1] : nullptr; // a folder for pictures, if wanted
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    std::string error;
    auto section = HostedInstrument::Load(Plugin("Section.vst3"), 0, error);
    int sw = 0, sh = 0;
    CHECK(section && section->EditorSize(sw, sh), "Section has an editor (%d x %d)", sw, sh);
    std::vector<uint8_t> comp, ctrl;
    section->SaveState(comp, ctrl);
    section.reset();

    auto d = HostedInstrument::Load(Plugin("Drifter.vst3"), 0, error);
    CHECK(d != nullptr, "Drifter loads (%s)", error.c_str());
    if (!d)
      return 1;
    d->Prepare(48000., 512);

    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 900, 440) styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed = NO;
    [window orderFrontRegardless];
    int lastW = 0, lastH = 0, resizes = 0;
    d->SetEditorResizeCallback([&](int w, int h) {
      lastW = w;
      lastH = h;
      resizes++;
      [window setContentSize:NSMakeSize(w, h)];
    });

    int w = 0, h = 0;
    CHECK(d->OpenEditor((__bridge void*)window.contentView, 1., w, h) && w == 900 && h == 300 + 140,
          "Drifter's window with no synth: the strip and a placeholder (%d x %d)", w, h);
    [window setContentSize:NSMakeSize(w, h)];
    Pump(0.3);
    CHECK(FindContainer(window.contentView) == nil, "no synth: nothing embedded");
    if (pictures)
      Snapshot(window, (std::string(pictures) + "/drifter-empty.png").c_str());

    // A project with Section inside, opened while Drifter's window is open.
    d->RestoreState(DrifterState(Plugin("Section.vst3"), comp, ctrl, {}, 20.), {});
    Pump(0.5);
    const int wantW = std::max(900, sw), wantH = 300 + sh;
    CHECK(lastW == wantW && lastH == wantH, "loading Section: Drifter's window becomes the strip + Section's editor (%d x %d; want %d x %d)", lastW, lastH,
          wantW, wantH);
    NSView* box = FindContainer(window.contentView);
    CHECK(box && box.subviews.count == 1, "Section's editor is embedded in Drifter's window");
    if (box)
    {
      NSRect f = [box convertRect:box.bounds toView:box.superview];
      const NSView* parent = box.superview;
      const double top = parent.isFlipped ? f.origin.y : parent.bounds.size.height - f.origin.y - f.size.height;
      CHECK(f.origin.x == 0 && top == 300 && f.size.width == sw && f.size.height == sh, "under the strip, at its own size (%.0f, %.0f, %.0f x %.0f)", f.origin.x,
            top, f.size.width, f.size.height);
    }
    if (pictures)
      Snapshot(window, (std::string(pictures) + "/drifter-section.png").c_str());

    // The lanes panel hidden (saved with the project): Section's editor moves up.
    d->RestoreState(DrifterState(Plugin("Section.vst3"), comp, ctrl, {}, 20., 0), {});
    Pump(0.5);
    box = FindContainer(window.contentView);
    CHECK(lastW == wantW && lastH == 152 + sh && box, "lanes hidden: the window is shorter (%d x %d)", lastW, lastH);
    if (box)
    {
      const NSView* parent = box.superview;
      const double top = parent.isFlipped ? box.frame.origin.y : parent.bounds.size.height - box.frame.origin.y - box.frame.size.height;
      CHECK(top == 152, "and Section's editor sits right under the drift row (%.0f)", top);
    }
    if (pictures)
      Snapshot(window, (std::string(pictures) + "/drifter-section-nolanes.png").c_str());

    // A missing synth: its editor goes, the placeholder says so.
    d->RestoreState(DrifterState("/nowhere/Gone.vst3", comp, ctrl, {}, 20., 0), {});
    Pump(0.3);
    CHECK(FindContainer(window.contentView) == nil && lastW == 900 && lastH == 152 + 140, "a missing synth: no editor, the placeholder (%d x %d)", lastW,
          lastH);

    // And back, then Drifter's window closes with Section's editor in it.
    d->RestoreState(DrifterState(Plugin("Section.vst3"), comp, ctrl, {}, 20., 1), {});
    Pump(0.3);
    CHECK(FindContainer(window.contentView) != nil && lastH == wantH, "Section again, with the lanes panel");
    d->CloseEditor();
    Pump(0.2);
    CHECK(FindContainer(window.contentView) == nil && window.contentView.subviews.count == 0, "closing Drifter's window takes Section's editor with it");
    // Reopened: Section's editor comes back, and the window is its size from the start.
    CHECK(d->OpenEditor((__bridge void*)window.contentView, 1., w, h) && w == wantW && h == wantH, "reopened at the right size (%d x %d)", w, h);
    Pump(0.3);
    CHECK(FindContainer(window.contentView) != nil, "with Section's editor in it");
    d->CloseEditor();
    d.reset();
    [window close];
    printf("%s\n", fails ? "FAILED" : "all passed");
  }
  return fails ? 1 : 0;
}
