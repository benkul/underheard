#pragma once
// The look and a few controls shared by the Underheard effect plugins (iPlug2 IGraphics).

#include "IControls.h"
#include "Palette.h"

#include <functional>
#include <string>
#include <vector>

namespace underheard_ui {

using namespace iplug;
using namespace iplug::igraphics;

using underheard_palette::kBackground;
using underheard_palette::kText;
using underheard_palette::kTextDim;
using underheard_palette::kAccent;

inline IVStyle PanelStyle()
{
  return DEFAULT_STYLE.WithColor(kBG, COLOR_TRANSPARENT).WithColor(kFG, underheard_palette::kControl).WithColor(kPR, kAccent)
                      .WithColor(kFR, underheard_palette::kFrame).WithColor(kX1, kAccent)
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

// A button whose menu is built when it's clicked (from whatever is current).
class DynamicMenuButton : public IVButtonControl
{
public:
  DynamicMenuButton(const IRECT& bounds, const char* label, const IVStyle& style, std::function<std::vector<std::string>()> items, std::function<void(int)> onChoose)
  : IVButtonControl(bounds, nullptr, label, style), mItems(std::move(items)), mOnChoose(std::move(onChoose))
  {
  }
  void OnMouseDown(float, float, const IMouseMod&) override
  {
    mMenu.Clear();
    const auto items = mItems();
    if (items.empty())
      return;
    for (const auto& item : items)
      mMenu.AddItem(item.c_str());
    GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }
  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    if (menu && menu->GetChosenItemIdx() >= 0)
      mOnChoose(menu->GetChosenItemIdx());
  }

private:
  IPopupMenu mMenu;
  std::function<std::vector<std::string>()> mItems;
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
