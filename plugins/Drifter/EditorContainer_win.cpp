#include "EditorContainer.h"

#include <windows.h>

namespace drifter_editor
{
static const wchar_t* kClass = L"UnderheardDrifterEditor";

static void RegisterClassOnce()
{
  static bool done = false;
  if (done)
    return;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  done = true;
}

void* CreateContainer(void* parent, int x, int y, int w, int h)
{
  if (!parent)
    return nullptr;
  RegisterClassOnce();
  HWND v = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x, y, w, h, (HWND)parent, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
  return v;
}

void MoveContainer(void* container, int x, int y, int w, int h)
{
  if (container)
    SetWindowPos((HWND)container, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
}

void DestroyContainer(void* container)
{
  if (container)
    DestroyWindow((HWND)container);
}
} // namespace drifter_editor
