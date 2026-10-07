#pragma once
// The tape drawing in each loop strip: the loop as a length of tape, darkening and breaking up
// as it wears, with the play head, splices, razor marks, and the loop's status written on it.
// Clicking it selects the loop. Dragging across the tape marks a cut; dragging a mark's edge
// moves it; double-clicking clears the marks.

#include "IControl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace splicer_ui {

using namespace iplug;
using namespace igraphics;

// Colours shared by the whole layout.
namespace theme {
const IColor kBackground(255, 24, 22, 20);
const IColor kPanel(255, 36, 33, 30);
const IColor kPanelSelected(255, 56, 50, 42);
const IColor kText(255, 226, 216, 200);
const IColor kTextDim(255, 150, 140, 128);
const IColor kTape(255, 156, 110, 62);
const IColor kTapeWorn(255, 58, 40, 26);
const IColor kTapeEmpty(255, 48, 44, 40);
const IColor kHead(255, 245, 238, 220);
const IColor kSplice(255, 20, 18, 16);
const IColor kMark(90, 236, 200, 96);
const IColor kRecord(255, 214, 66, 52);
const IColor kPlay(255, 120, 186, 96);
const IColor kAccent(255, 206, 146, 72);
} // namespace theme

struct TapeViewData
{
  bool empty = true, recording = false, playing = false;
  double length = 0.;            // frames
  double position = 0.;          // frames
  double markIn = -1., markOut = -1.;
  std::vector<float> age, shed;  // per bin along the tape
  std::vector<double> splices;   // frames
  std::string status, message;
};

class TapeView : public IControl
{
public:
  // onMarks(in, out) gets tape frames with in <= out, or (-1, -1) to clear.
  TapeView(const IRECT& bounds, std::function<void()> onSelect, std::function<void(double, double)> onMarks)
  : IControl(bounds)
  , mOnSelect(std::move(onSelect))
  , mOnMarks(std::move(onMarks))
  {
  }

  void SetData(const TapeViewData& d)
  {
    // While dragging (and just after), show the marks being drawn rather than the plugin's
    // copy, which arrives a moment later.
    const bool keepMarks = mDragging || Clock::now() < mHoldMarksUntil;
    const double in = mData.markIn, out = mData.markOut;
    mData = d;
    if (keepMarks)
    {
      mData.markIn = in;
      mData.markOut = out;
    }
    SetDirty(false);
  }

  void OnMouseDown(float x, float, const IMouseMod&) override
  {
    if (mOnSelect)
      mOnSelect();
    mDownX = x;
    mDragging = false;
    mEdge = EdgeAt(x);
  }

  void OnMouseDrag(float x, float, float, float, const IMouseMod&) override
  {
    if (!Markable() || (!mDragging && std::fabs(x - mDownX) < 4.f))
      return;
    mDragging = true;
    // Moving an edge keeps the other one; otherwise the drag starts a new mark.
    const double anchor = mEdge == 1 ? mData.markOut : mEdge == 2 ? mData.markIn : FramesAt(mDownX);
    const double here = FramesAt(x);
    SetMarks(std::min(anchor, here), std::max(anchor, here));
  }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    if (mDragging)
      mHoldMarksUntil = Clock::now() + std::chrono::milliseconds(200);
    mDragging = false;
  }

  void OnMouseDblClick(float, float, const IMouseMod&) override
  {
    if (Markable())
      SetMarks(-1., -1.);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    if (GetUI())
      GetUI()->SetMouseCursor(Markable() ? (EdgeAt(x) ? ECursor::SIZEWE : ECursor::IBEAM) : ECursor::ARROW);
    IControl::OnMouseOver(x, y, mod);
  }

  void OnMouseOut() override
  {
    if (GetUI())
      GetUI()->SetMouseCursor(ECursor::ARROW);
    IControl::OnMouseOut();
  }

  void Draw(IGraphics& g) override
  {
    const IRECT r = mRECT;
    const IText statusText(14.f, theme::kText, "Roboto-Regular", EAlign::Near, EVAlign::Top);
    const IText messageText(12.f, theme::kAccent, "Roboto-Regular", EAlign::Near, EVAlign::Bottom);

    const IRECT tape = TapeRect();
    g.FillRoundRect(theme::kTapeEmpty, tape, 3.f);

    if (!mData.empty && mData.length > 0.)
    {
      // The tape, darker where it's old and broken where oxide has shed.
      const int bins = (int)mData.age.size();
      const float bw = tape.W() / (float)std::max(bins, 1);
      for (int b = 0; b < bins; b++)
      {
        const float wear = std::min(1.f, mData.age[(size_t)b] / 3.f);
        IColor c = IColor::LinearInterpolateBetween(theme::kTape, theme::kTapeWorn, wear);
        const float shed = mData.shed[(size_t)b];
        IRECT cell(tape.L + b * bw, tape.T, tape.L + (b + 1) * bw + 0.5f, tape.B);
        if (shed > 0.05f)
          cell = cell.GetPadded(0.f, -cell.H() * 0.5f * std::min(1.f, shed), 0.f, -cell.H() * 0.5f * std::min(1.f, shed));
        g.FillRect(c, cell);
      }

      auto x = [&](double frames) { return tape.L + (float)(frames / mData.length) * tape.W(); };

      if (mData.markIn >= 0.)
      {
        if (mData.markOut >= 0.)
        {
          float a = x(mData.markIn), b = x(mData.markOut);
          if (a > b) std::swap(a, b);
          g.FillRect(theme::kMark, IRECT(a, tape.T - 3.f, b, tape.B + 3.f));
        }
        g.DrawLine(theme::kAccent, x(mData.markIn), tape.T - 4.f, x(mData.markIn), tape.B + 4.f, nullptr, 2.f);
        if (mData.markOut >= 0.)
          g.DrawLine(theme::kAccent, x(mData.markOut), tape.T - 4.f, x(mData.markOut), tape.B + 4.f, nullptr, 2.f);
      }

      for (double s : mData.splices)
        g.DrawLine(theme::kSplice, x(s), tape.T, x(s), tape.B, nullptr, 2.f);

      const IColor head = mData.recording ? theme::kRecord : theme::kHead;
      g.DrawLine(head, x(mData.position), tape.T - 6.f, x(mData.position), tape.B + 6.f, nullptr, 2.5f);
    }
    else if (mData.recording)
    {
      // First take: the tape runs out to the right as it's recorded.
      g.FillRoundRect(theme::kRecord.WithOpacity(0.5f), tape.GetFromLeft(tape.W() * 0.5f), 3.f);
    }

    g.DrawText(statusText, mData.status.c_str(), r.GetPadded(-6.f, -4.f, -6.f, -4.f));
    if (!mData.message.empty())
      g.DrawText(messageText, mData.message.c_str(), r.GetPadded(-6.f, -2.f, -6.f, -1.f));

    const IColor edge = mData.recording ? theme::kRecord : (mData.playing ? theme::kPlay : theme::kPanel);
    g.DrawRoundRect(edge, r.GetPadded(-1.f), 4.f, nullptr, 1.5f);
  }

private:
  using Clock = std::chrono::steady_clock;

  IRECT TapeRect() const { return mRECT.GetPadded(-4.f).GetFromBottom(mRECT.H() * 0.42f).GetTranslated(0.f, -14.f); }
  bool Markable() const { return !mData.empty && mData.length > 0.; }

  double FramesAt(float x) const
  {
    const IRECT t = TapeRect();
    return std::clamp((double)((x - t.L) / t.W()), 0., 1.) * mData.length;
  }

  float XAt(double frames) const
  {
    const IRECT t = TapeRect();
    return t.L + (float)(frames / mData.length) * t.W();
  }

  // 1 = the start mark is under x, 2 = the end mark, 0 = neither.
  int EdgeAt(float x) const
  {
    if (!Markable() || mData.markIn < 0. || mData.markOut < 0.)
      return 0;
    if (std::fabs(x - XAt(mData.markIn)) < 6.f)
      return 1;
    if (std::fabs(x - XAt(mData.markOut)) < 6.f)
      return 2;
    return 0;
  }

  void SetMarks(double in, double out)
  {
    mData.markIn = in;
    mData.markOut = out;
    mHoldMarksUntil = Clock::now() + std::chrono::milliseconds(200);
    if (mOnMarks)
      mOnMarks(in, out);
    SetDirty(false);
  }

  TapeViewData mData;
  std::function<void()> mOnSelect;
  std::function<void(double, double)> mOnMarks;
  float mDownX = 0.f;
  bool mDragging = false;
  int mEdge = 0;
  Clock::time_point mHoldMarksUntil{};
};

} // namespace splicer_ui

namespace splicer_ui {

// The background of a loop strip: lighter when its loop is selected; clicking it selects.
class StripPanel : public IControl
{
public:
  StripPanel(const IRECT& bounds, std::function<bool()> isSelected, std::function<void()> onSelect)
  : IControl(bounds)
  , mIsSelected(std::move(isSelected))
  , mOnSelect(std::move(onSelect))
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(mIsSelected() ? theme::kPanelSelected : theme::kPanel, mRECT, 6.f);
  }

  void OnMouseDown(float, float, const IMouseMod&) override { mOnSelect(); }

private:
  std::function<bool()> mIsSelected;
  std::function<void()> mOnSelect;
};

} // namespace splicer_ui

namespace splicer_ui {

// A checkbox for an on/off parameter: a box, a tick, and a label beside it.
class CheckboxControl : public IControl
{
public:
  CheckboxControl(const IRECT& bounds, int paramIdx, const char* label)
  : IControl(bounds, paramIdx)
  , mLabel(label)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT box = mRECT.GetFromLeft(mRECT.H()).GetCentredInside(16.f);
    g.FillRoundRect(theme::kTapeEmpty, box, 3.f);
    g.DrawRoundRect(theme::kTextDim, box, 3.f, nullptr, 1.5f);
    if (GetValue() > 0.5)
    {
      g.DrawLine(theme::kAccent, box.L + 3.f, box.MH(), box.MW() - 1.f, box.B - 3.f, nullptr, 2.5f);
      g.DrawLine(theme::kAccent, box.MW() - 1.f, box.B - 3.f, box.R - 2.f, box.T + 3.f, nullptr, 2.5f);
    }
    const IText text(14.f, theme::kText, "Roboto-Regular", EAlign::Near);
    g.DrawText(text, mLabel.c_str(), mRECT.GetReducedFromLeft(mRECT.H() + 2.f));
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    SetValue(GetValue() > 0.5 ? 0. : 1.);
    SetDirty(true);
  }

private:
  std::string mLabel;
};

// Where each drifting setting is: a track per setting with a mark at its home (the knob) and a
// dot where Drifter has moved it, plus the progress of the current shift.
struct DriftViewData
{
  bool enabled = false;
  double progress = 0.;
  std::vector<double> home, now;      // normalised 0 .. 1
  std::vector<std::string> names;
  // Headings over runs of targets: {first index, count, text}.
  struct Group { int first, count; std::string name; };
  std::vector<Group> groups;
};

class DriftView : public IControl
{
public:
  explicit DriftView(const IRECT& bounds) : IControl(bounds) { mIgnoreMouse = true; }

  void SetData(const DriftViewData& d)
  {
    mData = d;
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    const int n = (int)mData.home.size();
    if (n == 0)
      return;
    const IText label(13.f, theme::kText, "Roboto-Regular", EAlign::Center);
    const IText heading(13.f, theme::kAccent, "Roboto-Regular", EAlign::Center);
    IRECT area = mRECT;
    const IRECT headings = area.ReduceFromTop(18.f);
    for (const auto& grp : mData.groups)
    {
      const IRECT a = headings.GetGridCell(0, grp.first, 1, n), b = headings.GetGridCell(0, grp.first + grp.count - 1, 1, n);
      const IRECT span = a.Union(b).GetPadded(-4.f, 0.f, -4.f, 0.f);
      g.DrawText(heading, grp.name.c_str(), span);
      g.DrawLine(theme::kAccent.WithOpacity(0.5f), span.L, span.B - 1.f, span.R, span.B - 1.f, nullptr, 1.f);
    }
    const IRECT progress = area.ReduceFromBottom(8.f).GetPadded(-4.f, -2.f, -4.f, -2.f);
    g.FillRoundRect(theme::kTapeEmpty, progress, 2.f);
    if (mData.enabled)
      g.FillRoundRect(theme::kAccent.WithOpacity(0.6f), progress.GetFromLeft(progress.W() * (float)mData.progress), 2.f);

    for (int i = 0; i < n; i++)
    {
      IRECT col = area.GetGridCell(0, i, 1, n).GetPadded(-3.f);
      const IRECT name = col.ReduceFromBottom(18.f);
      g.DrawText(label, mData.names[(size_t)i].c_str(), name);
      const IRECT track = col.GetMidHPadded(3.f);
      g.FillRoundRect(theme::kTapeEmpty, track, 3.f);
      auto y = [&](double v) { return track.B - (float)v * track.H(); };
      g.DrawLine(theme::kTextDim, track.L - 5.f, y(mData.home[(size_t)i]), track.R + 5.f, y(mData.home[(size_t)i]), nullptr, 2.f);
      const IColor dot = mData.enabled ? theme::kAccent : theme::kTextDim;
      g.FillCircle(dot, track.MW(), y(mData.now[(size_t)i]), 5.f);
    }
  }

private:
  DriftViewData mData;
};

} // namespace splicer_ui
