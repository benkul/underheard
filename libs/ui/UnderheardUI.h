#pragma once
// The look and a few controls shared by the Underheard effect plugins (iPlug2 IGraphics).

#include "IControls.h"

#include <functional>
#include <string>
#include <vector>

namespace underheard_ui {

using namespace iplug;
using namespace iplug::igraphics;

const IColor kBackground(255, 24, 22, 20), kText(255, 226, 216, 200), kTextDim(255, 150, 140, 128), kAccent(255, 206, 146, 72);

inline IVStyle PanelStyle()
{
  return DEFAULT_STYLE.WithColor(kBG, COLOR_TRANSPARENT).WithColor(kFG, IColor(255, 70, 64, 58)).WithColor(kPR, kAccent)
                      .WithColor(kFR, IColor(255, 110, 100, 90)).WithColor(kX1, kAccent)
                      .WithLabelText(IText(14.f, kText, "Roboto-Regular")).WithValueText(IText(13.f, kTextDim, "Roboto-Regular"))
                      .WithDrawShadows(false).WithRoundness(0.2f);
}

// A button that opens a menu of names and reports the choice (TEMPLATES).
class MenuButton : public IVButtonControl
{
public:
  MenuButton(const IRECT& bounds, const char* label, const IVStyle& style, const std::vector<std::string>& items, std::function<void(int)> onChoose)
  : IVButtonControl(bounds, nullptr, label, style), mOnChoose(std::move(onChoose))
  {
    for (const auto& item : items)
      mMenu.AddItem(item.c_str());
  }
  void OnMouseDown(float, float, const IMouseMod&) override { GetUI()->CreatePopupMenu(*this, mMenu, mRECT); }
  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    if (menu && menu->GetChosenItemIdx() >= 0)
      mOnChoose(menu->GetChosenItemIdx());
  }

private:
  IPopupMenu mMenu;
  std::function<void(int)> mOnChoose;
};

// A button that's on only while it's held (the delay's Throw).
class HoldButton : public IVButtonControl
{
public:
  HoldButton(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style)
  : IVButtonControl(bounds, nullptr, label, style)
  {
    SetParamIdx(paramIdx);
  }
  void OnMouseDown(float, float, const IMouseMod&) override
  {
    SetValue(1.);
    SetDirty(true);
  }
  void OnMouseUp(float, float, const IMouseMod&) override
  {
    SetValue(0.);
    SetDirty(true);
  }
};

} // namespace underheard_ui
