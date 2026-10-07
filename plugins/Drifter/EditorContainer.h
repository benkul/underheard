#pragma once
// A native child view inside Drifter's window that the hosted synth's editor attaches to, so it
// can sit under Drifter's strip (docs/DRIFTER-SPEC.md). An NSView on macOS, an HWND on Windows.
// Positions are from the parent's top left, in the platform's units (points on macOS, pixels
// on Windows).

namespace drifter_editor
{
void* CreateContainer(void* parent, int x, int y, int w, int h); // nullptr on failure
void MoveContainer(void* container, int x, int y, int w, int h);
void DestroyContainer(void* container);
} // namespace drifter_editor
