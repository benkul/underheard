#include "EditorContainer.h"

#import <Cocoa/Cocoa.h>

// Flipped, so a hosted editor attached at (0, 0) sits at the top left.
@interface DrifterEditorContainer : NSView
@end
@implementation DrifterEditorContainer
- (BOOL)isFlipped { return YES; }
@end

namespace drifter_editor
{
static NSRect FrameIn(NSView* parent, int x, int y, int w, int h)
{
  const CGFloat top = parent.isFlipped ? y : parent.bounds.size.height - y - h;
  return NSMakeRect(x, top, w, h);
}

void* CreateContainer(void* parent, int x, int y, int w, int h)
{
  NSView* p = (__bridge NSView*)parent;
  if (!p)
    return nullptr;
  DrifterEditorContainer* v = [[DrifterEditorContainer alloc] initWithFrame:FrameIn(p, x, y, w, h)];
  [p addSubview:v];
  return (__bridge_retained void*)v;
}

void MoveContainer(void* container, int x, int y, int w, int h)
{
  NSView* v = (__bridge NSView*)container;
  if (v && v.superview)
    [v setFrame:FrameIn(v.superview, x, y, w, h)];
}

void DestroyContainer(void* container)
{
  if (!container)
    return;
  NSView* v = (__bridge_transfer NSView*)container;
  [v removeFromSuperview];
}
} // namespace drifter_editor
