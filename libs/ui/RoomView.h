#pragma once
// The room seen from above: walls shaded by how hard they are, the source, and the mic (or pair)
// with its pickup pattern drawn around it. Drag the source to move it; drag the mic (or anywhere
// else in the room) to set which way it is from the source and how far; drag the dot in front
// of the mic to aim it.
//
// It drives five parameters, in this order: Distance, Aim, Source X, Source Y, Bearing. The
// picture comes from the same placement the room model uses, so it always matches what's heard.

#include "IControl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace roombleed_ui {

using namespace iplug;
using namespace igraphics;

namespace theme {
const IColor kBackground(255, 24, 22, 20);
const IColor kFloor(255, 34, 31, 28);
const IColor kText(255, 226, 216, 200);
const IColor kTextDim(255, 150, 140, 128);
const IColor kAccent(255, 206, 146, 72);
const IColor kSoftWall(255, 90, 72, 56);
const IColor kHardWall(255, 226, 210, 186);
const IColor kSource(255, 120, 186, 96);
} // namespace theme

struct RoomViewData
{
  double width = 4., depth = 5.;   // metres
  double sx = 1., sy = 1.;         // source
  double dirx = 1., diry = 0.;     // the line the mic moves along (unit)
  double cax = -1., cay = 0.;      // where the mic (or the pair's centre) points (unit)
  struct Mic { double x, y, ax, ay; };
  Mic mics[2];
  int numMics = 1;
  double distance = 1., maxDistance = 5.;
  double pattern = 0.5;            // a + (1 - a)·cos θ at the mid band
  double hardness[4] = {0.5, 0.5, 0.5, 0.5}; // walls x = 0, x = W, y = 0, y = D (0 soft .. 1 hard)
  int where = 0;                   // 0 same room, 1 next door, 2 below
  double door = 0.;
  std::string title;
  // Section: several sources (the desks) instead of one, each labelled. With seats, the view
  // only shows (it drives no parameters).
  struct Seat { double x, y; const char* label; };
  std::vector<Seat> seats;
};

class RoomView : public IControl
{
public:
  enum { kDistance = 0, kAim, kSourceX, kSourceY, kBearing };

  RoomView(const IRECT& bounds, const std::initializer_list<int>& params, std::function<RoomViewData()> get)
  : IControl(bounds, params)
  , mGet(std::move(get))
  {
  }

  // A picture only (Section's seated desks): drives no parameters.
  RoomView(const IRECT& bounds, std::function<RoomViewData()> get)
  : IControl(bounds, kNoParameter)
  , mGet(std::move(get))
  {
  }

  void Draw(IGraphics& g) override
  {
    const RoomViewData d = mGet();
    mLast = d;
    Fit(d);
    const IText title(14.f, theme::kText, "Roboto-Regular", EAlign::Near, EVAlign::Top);
    const IText small(13.f, theme::kTextDim, "Roboto-Regular", EAlign::Near, EVAlign::Top);
    g.DrawText(title, d.title.c_str(), mRECT.GetFromTop(20.f));

    // Floor, then walls coloured by hardness.
    const IRECT floor(X(0.), Y(0.), X(d.width), Y(d.depth));
    g.FillRect(theme::kFloor, floor);
    auto wallColour = [&](int i) { return IColor::LinearInterpolateBetween(theme::kSoftWall, theme::kHardWall, (float)d.hardness[i]); };
    g.DrawLine(wallColour(0), floor.L, floor.T, floor.L, floor.B, nullptr, 4.f);
    g.DrawLine(wallColour(1), floor.R, floor.T, floor.R, floor.B, nullptr, 4.f);
    g.DrawLine(wallColour(2), floor.L, floor.T, floor.R, floor.T, nullptr, 4.f);
    g.DrawLine(wallColour(3), floor.L, floor.B, floor.R, floor.B, nullptr, 4.f);

    // The line the mic travels along, and the source.
    const double far = d.maxDistance;
    g.DrawDottedLine(theme::kTextDim.WithOpacity(0.5f), X(d.sx), Y(d.sy), X(d.sx + d.dirx * far), Y(d.sy + d.diry * far), nullptr, 1.f, 4.f);
    if (d.seats.empty())
    {
      g.FillCircle(theme::kSource, X(d.sx), Y(d.sy), 8.f);
      g.DrawText(small, "source", IRECT(X(d.sx) + 10.f, Y(d.sy) - 8.f, X(d.sx) + 80.f, Y(d.sy) + 10.f));
    }
    for (const auto& seat : d.seats)
    {
      g.FillCircle(theme::kSource, X(seat.x), Y(seat.y), 7.f);
      const IText label(13.f, theme::kText, "Roboto-Regular", EAlign::Center, EVAlign::Bottom);
      g.DrawText(label, seat.label, IRECT(X(seat.x) - 40.f, Y(seat.y) - 30.f, X(seat.x) + 40.f, Y(seat.y) - 9.f));
    }

    // Each mic with its pattern.
    for (int m = 0; m < d.numMics; m++)
    {
      const auto& mic = d.mics[m];
      const float cx = X(mic.x), cy = Y(mic.y), r = 34.f;
      const double aim = std::atan2(mic.ay, mic.ax);
      g.PathClear();
      for (int k = 0; k <= 72; k++)
      {
        const double th = 2. * 3.14159265358979 * k / 72.;
        const double gain = std::fabs(d.pattern + (1. - d.pattern) * std::cos(th));
        const float px = cx + (float)(r * gain * std::cos(aim + th)), py = cy + (float)(r * gain * std::sin(aim + th));
        if (k == 0) g.PathMoveTo(px, py); else g.PathLineTo(px, py);
      }
      g.PathClose();
      g.PathFill(theme::kAccent.WithOpacity(0.18f));
      g.PathStroke(theme::kAccent.WithOpacity(0.7f), 1.5f);
      g.FillCircle(theme::kText, cx, cy, 5.f);
    }

    // The aim handle, in front of the mic (or the pair's centre).
    const float hx = HandleX(d), hy = HandleY(d);
    const float mx = X(MicCentreX(d)), my = Y(MicCentreY(d));
    g.DrawLine(theme::kAccent, mx, my, hx, hy, nullptr, 1.5f);
    g.FillCircle(theme::kAccent, hx, hy, 6.f);

    char label[64];
    std::snprintf(label, sizeof label, "%.2f m", d.distance);
    g.DrawText(small, label, IRECT(mx + 10.f, my + 6.f, mx + 90.f, my + 24.f));

    if (d.where != 0)
    {
      char where[96];
      if (d.where == 1)
        std::snprintf(where, sizeof where, "heard from next door, door %s", d.door < 0.01 ? "shut" : d.door > 0.99 ? "open" : "ajar");
      else
        std::snprintf(where, sizeof where, "heard from below, through the floor");
      g.DrawText(IText(14.f, theme::kAccent, "Roboto-Regular", EAlign::Near, EVAlign::Top), where, mRECT.GetFromBottom(20.f));
      if (d.where == 1)
      {
        // The wall the listener is behind, with the doorway opening in it.
        const float gap = (float)(d.door * (floor.B - floor.T) * 0.3);
        const float mid = 0.5f * (floor.T + floor.B);
        g.DrawLine(theme::kAccent, floor.R + 6.f, floor.T, floor.R + 6.f, mid - gap / 2.f, nullptr, 3.f);
        g.DrawLine(theme::kAccent, floor.R + 6.f, mid + gap / 2.f, floor.R + 6.f, floor.B, nullptr, 3.f);
      }
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    if (NVals() < 5)
      return; // a picture only
    Fit(mLast);
    mMode = std::hypot(x - HandleX(mLast), y - HandleY(mLast)) < 12.f ? kModeAim
          : std::hypot(x - X(mLast.sx), y - Y(mLast.sy)) < 14.f   ? kModeSource
                                                                     : kModeMic;
    for (int v : Touched())
      if (v >= 0)
        GetDelegate()->BeginInformHostOfParamChangeFromUI(GetParamIdx(v));
    Drag(x, y);
  }

  void OnMouseDrag(float x, float y, float, float, const IMouseMod&) override
  {
    if (NVals() >= 5)
      Drag(x, y);
  }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    if (NVals() < 5)
      return;
    for (int v : Touched())
      if (v >= 0)
        GetDelegate()->EndInformHostOfParamChangeFromUI(GetParamIdx(v));
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    if (GetUI() && NVals() >= 5)
    {
      const bool handle = std::hypot(x - HandleX(mLast), y - HandleY(mLast)) < 12.f;
      const bool source = std::hypot(x - X(mLast.sx), y - Y(mLast.sy)) < 14.f;
      GetUI()->SetMouseCursor(handle ? ECursor::HAND : source ? ECursor::SIZEALL : ECursor::CROSS);
    }
    IControl::OnMouseOver(x, y, mod);
  }

  void OnMouseOut() override
  {
    if (GetUI())
      GetUI()->SetMouseCursor(ECursor::ARROW);
    IControl::OnMouseOut();
  }

private:
  // Room metres -> screen, fitted into the bounds below the title, keeping proportions.
  void Fit(const RoomViewData& d)
  {
    const IRECT area = mRECT.GetReducedFromTop(26.f).GetReducedFromBottom(26.f).GetPadded(-14.f);
    mScale = (float)std::min(area.W() / d.width, area.H() / d.depth);
    mOx = area.MW() - (float)(d.width * mScale) / 2.f;
    mOy = area.MH() - (float)(d.depth * mScale) / 2.f;
  }
  float X(double m) const { return mOx + (float)m * mScale; }
  float Y(double m) const { return mOy + (float)m * mScale; }
  double ToMx(float x) const { return (x - mOx) / mScale; }
  double ToMy(float y) const { return (y - mOy) / mScale; }

  static double MicCentreX(const RoomViewData& d) { return d.numMics == 2 ? 0.5 * (d.mics[0].x + d.mics[1].x) : d.mics[0].x; }
  static double MicCentreY(const RoomViewData& d) { return d.numMics == 2 ? 0.5 * (d.mics[0].y + d.mics[1].y) : d.mics[0].y; }
  float HandleX(const RoomViewData& d) const { return X(MicCentreX(d)) + (float)d.cax * 52.f; }
  float HandleY(const RoomViewData& d) const { return Y(MicCentreY(d)) + (float)d.cay * 52.f; }

  enum EMode { kModeMic, kModeAim, kModeSource };

  // The values a drag changes.
  std::array<int, 2> Touched() const
  {
    if (mMode == kModeAim) return {kAim, -1};
    if (mMode == kModeSource) return {kSourceX, kSourceY};
    return {kDistance, kBearing};
  }

  // How far the mic can go from (sx, sy) along a bearing (staying 0.3 m inside the walls).
  static double Reach(const RoomViewData& d, double sx, double sy, double bx, double by)
  {
    auto reach = [](double from, double dir, double lo, double hi) {
      if (dir > 1e-9) return (hi - from) / dir;
      if (dir < -1e-9) return (lo - from) / dir;
      return 1e9;
    };
    return std::max(0.1, std::min(reach(sx, bx, 0.3, d.width - 0.3), reach(sy, by, 0.3, d.depth - 0.3)));
  }

  void Drag(float x, float y)
  {
    const RoomViewData& d = mLast;
    if (mMode == kModeSource)
    {
      SetValue(std::clamp(ToMx(x) / d.width, 0., 1.), kSourceX);
      SetValue(std::clamp(ToMy(y) / d.depth, 0., 1.), kSourceY);
      SetDirty(true, kSourceX);
      SetDirty(true, kSourceY);
      return;
    }
    if (mMode == kModeMic)
    {
      // Which way the mouse is from the source, and how far (logarithmic, like the knob).
      const double px = ToMx(x) - d.sx, py = ToMy(y) - d.sy, len = std::hypot(px, py);
      if (len < 1e-6)
        return;
      double bearing = std::atan2(py, px) * 180. / 3.14159265358979;
      if (bearing < 0.)
        bearing += 360.;
      const double maxD = Reach(d, d.sx, d.sy, px / len, py / len);
      const double metres = std::clamp(len, 0.1, maxD);
      const double norm = maxD > 0.11 ? std::log(metres / 0.1) / std::log(maxD / 0.1) : 0.;
      SetValue(bearing / 360., kBearing);
      SetValue(std::clamp(norm, 0., 1.), kDistance);
      SetDirty(true, kBearing);
      SetDirty(true, kDistance);
      return;
    }
    {
      // Aim: how far the mic is turned from pointing at the source toward the mouse, all the
      // way round (the same direction of turn the room model uses).
      const double mx = MicCentreX(d), my = MicCentreY(d);
      const double tx = d.sx - mx, ty = d.sy - my, px = ToMx(x) - mx, py = ToMy(y) - my;
      if (std::hypot(tx, ty) < 1e-9 || std::hypot(px, py) < 1e-9)
        return;
      double degrees = std::atan2(tx * py - ty * px, tx * px + ty * py) * 180. / 3.14159265358979;
      if (degrees < 0.)
        degrees += 360.;
      SetValue(degrees / 360., kAim);
      SetDirty(true, kAim);
    }
  }

  std::function<RoomViewData()> mGet;
  RoomViewData mLast;
  float mScale = 1.f, mOx = 0.f, mOy = 0.f;
  EMode mMode = kModeMic;
};

} // namespace roombleed_ui
