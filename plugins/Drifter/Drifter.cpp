#include "Drifter.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
#include "UnderheardUI.h"
#include "EditorContainer.h"
using namespace underheard_ui;
#endif
#if defined(VST3_API)
#include "pluginterfaces/base/ustring.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using underheard::DriftLanes;
using underheard::vst3host::HostedInstrument;
using underheard::vst3host::MidiEvent;
using underheard::vst3host::ParamChange;

namespace {

constexpr int32_t kStateMagic = 'DRFT';
constexpr int32_t kStateVersion = 2; // 2: + whether the lanes panel shows
constexpr double kBarChoices[] = {0.25, 0.5, 1., 2., 4., 8., 16., 32.};
constexpr int kNumBarChoices = 8;

// The window: the strip (header and drift row, then the lanes panel, which can be hidden) at
// least this wide, and under it the synth's editor, or a placeholder this tall.
constexpr int kStripWidth = 900, kStripTop = 152, kLanesHeight = 148, kNoEditorHeight = 140;

void PutBytes(IByteChunk& chunk, const std::vector<uint8_t>& b)
{
  const int32_t n = (int32_t)b.size();
  chunk.Put(&n);
  if (n > 0)
    chunk.PutBytes(b.data(), n);
}
int GetBytes(const IByteChunk& chunk, std::vector<uint8_t>& b, int pos)
{
  int32_t n = 0;
  pos = chunk.Get(&n, pos);
  if (pos < 0 || n < 0 || pos + n > chunk.Size())
    return -1;
  b.resize((size_t)n);
  if (n > 0)
    pos = chunk.GetBytes(b.data(), n, pos);
  return pos;
}

} // namespace

#if IPLUG_EDITOR
// One lane: on/off, its parameter, its range (the bar: drag either end; double-click for the
// whole range) with its home (the line) and where it is now (the dot), its value, and remove.
class LaneRowControl : public IControl
{
public:
  LaneRowControl(const IRECT& bounds, Drifter& d, int index)
  : IControl(bounds), mD(d), mIndex(index)
  {
    SetTooltip("Drag the ends of the bar to set the range this lane drifts in. Double-click: the whole range.");
  }

  void Draw(IGraphics& g) override
  {
    const IColor line(255, 70, 64, 58);
    if (!Active())
    {
      g.DrawRoundRect(line, mRECT, 3.f);
      return;
    }
    const Drifter::LaneView& v = mD.mLaneView[(size_t)mIndex];
    const bool on = v.on.load();
    const double lo = mDrag ? mLo : v.lo.load(), hi = mDrag ? mHi : v.hi.load(), home = v.home.load(), value = v.value.load();
    g.FillRoundRect(IColor(255, 36, 33, 30), mRECT, 3.f);
    // On / off.
    const IRECT box = OnBox();
    g.DrawRect(kText, box);
    if (on)
      g.FillRect(kAccent, box.GetPadded(-3.f));
    // The parameter, and its value now.
    const IText name(14.f, on ? kText : kTextDim, "Roboto-Regular", EAlign::Near);
    const IText val(13.f, on ? kText : kTextDim, "Roboto-Regular", EAlign::Far);
    const uint32_t id = v.id.load();
    const int p = mD.mMain ? mD.mMain->FindParameter(id) : -1;
    g.DrawText(name, Fit(p >= 0 ? mD.mMain->Parameter(p).title : "?", 14).c_str(), NameRect());
    if (p >= 0)
      g.DrawText(val, Fit(mD.mMain->ValueText(id, value), 10).c_str(), ValueRect());
    // The bar: the whole range of the parameter, the lane's range on it, home and now.
    const IRECT bar = Bar();
    auto at = [&](double x) { return bar.L + (float)std::clamp(x, 0., 1.) * bar.W(); };
    g.FillRect(line, bar);
    g.FillRect(on ? kAccent.WithOpacity(0.45f) : IColor(255, 90, 84, 76), IRECT(at(lo), bar.T, at(hi), bar.B));
    g.FillRect(kText, IRECT(at(lo) - 1.f, bar.T - 3.f, at(lo) + 1.f, bar.B + 3.f));
    g.FillRect(kText, IRECT(at(hi) - 1.f, bar.T - 3.f, at(hi) + 1.f, bar.B + 3.f));
    g.DrawLine(kText, at(home), bar.T - 5.f, at(home), bar.B + 5.f, nullptr, 1.5f);
    g.FillCircle(on ? kAccent : kTextDim, at(value), bar.MH(), 4.f);
    // Remove.
    const IRECT x = RemoveBox();
    g.DrawLine(kText, x.L + 3.f, x.T + 3.f, x.R - 3.f, x.B - 3.f, nullptr, 1.5f);
    g.DrawLine(kText, x.L + 3.f, x.B - 3.f, x.R - 3.f, x.T + 3.f, nullptr, 1.5f);
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    if (!Active())
      return;
    const Drifter::LaneView& v = mD.mLaneView[(size_t)mIndex];
    if (OnBox().GetPadded(4.f).Contains(x, y))
    {
      Drifter::Command c{Drifter::Command::kSetOn};
      c.index = mIndex;
      c.a = v.on.load() ? 0. : 1.;
      mD.Send(c);
    }
    else if (RemoveBox().GetPadded(4.f).Contains(x, y))
    {
      Drifter::Command c{Drifter::Command::kRemoveLane};
      c.index = mIndex;
      mD.Send(c);
    }
    else if (BarArea().Contains(x, y))
    {
      mLo = v.lo.load();
      mHi = v.hi.load();
      const double at = Position(x);
      mDrag = std::fabs(at - mLo) <= std::fabs(at - mHi) ? 1 : 2;
      DragTo(at);
    }
  }
  void OnMouseDrag(float x, float, float, float, const IMouseMod&) override
  {
    if (mDrag)
      DragTo(Position(x));
  }
  void OnMouseUp(float, float, const IMouseMod&) override { mDrag = 0; }
  void OnMouseDblClick(float x, float y, const IMouseMod&) override
  {
    if (Active() && BarArea().Contains(x, y))
      SetRange(0., 1.);
  }

private:
  bool Active() const { return mIndex < mD.mLaneCount.load(); }
  IRECT OnBox() const { return mRECT.GetFromLeft(26.f).GetCentredInside(14.f); }
  IRECT RemoveBox() const { return mRECT.GetFromRight(24.f).GetCentredInside(12.f); }
  IRECT NameRect() const { return IRECT(mRECT.L + 28.f, mRECT.T, mRECT.L + 128.f, mRECT.B); }
  IRECT Bar() const { return IRECT(mRECT.L + 134.f, mRECT.MH() - 4.f, mRECT.R - 98.f, mRECT.MH() + 4.f); }
  IRECT BarArea() const { return IRECT(Bar().L - 6.f, mRECT.T, Bar().R + 6.f, mRECT.B); }
  IRECT ValueRect() const { return IRECT(mRECT.R - 92.f, mRECT.T, mRECT.R - 26.f, mRECT.B); }
  double Position(float x) const { return std::clamp((double)(x - Bar().L) / Bar().W(), 0., 1.); }
  static std::string Fit(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + "."; }

  // An end of the range: not past home (the drift moves away from home towards the ends).
  void DragTo(double at)
  {
    const double home = mD.mLaneView[(size_t)mIndex].home.load();
    if (mDrag == 1)
      mLo = std::min(at, home);
    else
      mHi = std::max(at, home);
    SetRange(mLo, mHi);
    SetDirty(false);
  }
  void SetRange(double lo, double hi)
  {
    Drifter::Command c{Drifter::Command::kSetRange};
    c.index = mIndex;
    c.a = lo;
    c.b = hi;
    mD.Send(c);
  }

  Drifter& mD;
  int mIndex;
  int mDrag = 0; // 1: the low end, 2: the high end
  double mLo = 0., mHi = 1.;
};
#endif

Drifter::Drifter(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamDrift)->InitBool("Drift", true);
  GetParam(kParamTiming)->InitEnum("Timing", 0, {"Seconds", "Bars"});
  GetParam(kParamLength)->InitDouble("Length", 20., 0.5, 600., 0.1, "s", 0, "", IParam::ShapePowCurve(3.));
  GetParam(kParamBars)->InitEnum("Length (bars)", 4, {"1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars"});
  GetParam(kParamCurve)->InitEnum("Curve", underheard::Drifter::kCurveSmooth, {"Linear", "Smooth", "Fast start", "Slow start"});
  GetParam(kParamSmear)->InitPercentage("Smear", 30.);
  GetParam(kParamReach)->InitPercentage("Reach", 30.);
  GetParam(kParamGravity)->InitPercentage("Gravity", 30.);
  for (int i = 0; i < kNumSlots; i++)
  {
    char name[32];
    std::snprintf(name, sizeof name, "Slot %d", i + 1);
    mSlotParam[(size_t)i] = -1;
    mSlotId[(size_t)i] = 0;
    // A slot shows its parameter's value as the synth writes it.
    GetParam(kParamSlot0 + i)->InitDouble(name, 0., 0., 1., 0.0001, "", 0, "", IParam::ShapeLinear(), IParam::kUnitCustom, [this, i](double v, WDL_String& s) {
      if (mMain && mSlotParam[(size_t)i] >= 0)
        s.Set(mMain->ValueText(mMain->Parameter(mSlotParam[(size_t)i]).id, v).c_str());
      else
        s.SetFormatted(16, "%.3f", v);
    });
  }
  mEvents.reserve(1024);
  mChanges.reserve(kNumSlots + DriftLanes::kMaxLanes + 64);
  mSlotSent.fill(-1.);
  for (auto& v : mSlotLoaded)
    v = -1.;
  mLaneSent.fill(-1.);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, GetEditorWidth(), GetEditorHeight(), PLUG_FPS, 1.);
  };

  // The strip across the top (header, drift row, the lanes panel that can be hidden), and under
  // it the synth's own editor (a native child view, placed by OpenSynthEditor) or a placeholder.
  mLayoutFunc = [&](IGraphics* g) {
    const float stripH = (float)StripHeight();
    if (g->NControls())
    {
      // Resized: the background, the placeholder and the lanes panel follow.
      g->GetBackgroundControl()->SetTargetAndDrawRECTs(g->GetBounds());
      if (IControl* ph = g->GetControlWithTag(kCtrlTagPlaceholder))
        ph->SetTargetAndDrawRECTs(g->GetBounds().GetReducedFromTop(stripH).GetPadded(-12.f));
      g->ForControlInGroup("lanes", [this](IControl* c) { c->Hide(!mShowLanes); });
      g->SetAllControlsDirty();
      return;
    }
    g->AttachPanelBackground(kBackground);
    g->LoadFont("Roboto-Regular", ROBOTO_FN);
    g->SetLayoutOnResize(true);
    const IVStyle style = PanelStyle();
    const IText rowLabel(14.f, kText, "Roboto-Regular", EAlign::Near);
    IRECT b = IRECT(0.f, 0.f, (float)kStripWidth, (float)kStripTop + kLanesHeight).GetPadded(-12.f);

    // Header: title, LOAD, the synth's name, the status, and the lanes panel's toggle.
    IRECT top = b.ReduceFromTop(34.f);
    g->AttachControl(new ITextControl(top.ReduceFromLeft(110.f), "DRIFTER", IText(22.f, kText, "Roboto-Regular", EAlign::Near)));
    g->AttachControl(new IVButtonControl(top.ReduceFromLeft(110.f).GetPadded(-2.f), [this](IControl* c) {
      SplashClickActionFunc(c);
      LoadDialog();
    }, "LOAD", style));
    top.ReduceFromLeft(12.f);
    g->AttachControl(new IVToggleControl(top.ReduceFromRight(120.f).GetPadded(-2.f), [this](IControl* c) {
      SplashClickActionFunc(c);
      mShowLanes = c->GetValue() > 0.5;
      FitWindow();
    }, "", style, "SHOW LANES", "HIDE LANES", mShowLanes));
    g->AttachControl(new ITextControl(top.ReduceFromLeft(260.f), "no synth loaded", IText(16.f, kText, "Roboto-Regular", EAlign::Near)), kCtrlTagName);
    g->AttachControl(new ITextControl(top, "", IText(14.f, kAccent, "Roboto-Regular", EAlign::Near)), kCtrlTagStatus);
    b.ReduceFromTop(8.f);

    // The drift row.
    auto cell = [](const IRECT& r, int i) { return r.GetGridCell(0, i, 1, 9).GetPadded(-4.f); };
    IRECT r = b.ReduceFromTop(90.f);
    b.ReduceFromTop(8.f);
    g->AttachControl(new ITextControl(r.ReduceFromLeft(70.f), "DRIFT", rowLabel));
    g->AttachControl(new IVToggleControl(cell(r, 0).GetMidVPadded(18.f), kParamDrift, "", style, "OFF", "ON"));
    g->AttachControl(new IVTabSwitchControl(cell(r, 1).GetMidVPadded(18.f), kParamTiming, {"s", "bars"}, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 2), kParamLength, "Length", style));
    g->AttachControl(new IVMenuButtonControl(cell(r, 3).GetMidVPadded(18.f), kParamBars, "", style));
    g->AttachControl(new IVMenuButtonControl(cell(r, 4).GetMidVPadded(18.f), kParamCurve, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamSmear, "Smear", style));
    g->AttachControl(new IVKnobControl(cell(r, 6), kParamReach, "Reach", style));
    g->AttachControl(new IVKnobControl(cell(r, 7), kParamGravity, "Gravity", style));
    {
      IRECT kr = cell(r, 8);
      g->AttachControl(new IVButtonControl(kr.FracRectVertical(0.5f, true).GetPadded(-2.f), [this](IControl* c) {
        SplashClickActionFunc(c);
        Send({Command::kKeep});
      }, "KEEP", style));
      g->AttachControl(new IVButtonControl(kr.FracRectVertical(0.5f).GetPadded(-2.f), [this](IControl* c) {
        SplashClickActionFunc(c);
        Send({Command::kReturn});
      }, "RETURN", style));
    }

    // The lanes panel: 8 rows (two columns of four), and adding / clearing.
    r = b;
    IRECT buttons = r.ReduceFromRight(150.f);
    g->AttachControl(new IVButtonControl(buttons.GetGridCell(0, 0, 3, 1).GetPadded(-3.f), [this](IControl* c) {
      SplashClickActionFunc(c);
      Send({mLearning.load() ? Command::kCancelLearn : Command::kLearn});
    }, "+ LANE (LEARN)", style), kCtrlTagLearn, "lanes");
    // Or pick from the synth's parameters (the slots' order).
    g->AttachControl(new DynamicMenuButton(buttons.GetGridCell(1, 0, 3, 1).GetPadded(-3.f), "+ FROM LIST", style, [this] {
      std::vector<std::string> names;
      for (int i = 0; i < mNumSlotsUsed.load(); i++)
        names.push_back(mMain->Parameter(mSlotParam[(size_t)i]).title);
      return names;
    }, [this](int i) {
      if (!mMain || i < 0 || i >= mNumSlotsUsed.load())
        return;
      Command c{Command::kAddLane};
      c.id = mSlotId[(size_t)i];
      c.a = mMain->ControllerValue(c.id);
      c.b = 0.;
      c.c = 1.;
      Send(c);
    }), kNoTag, "lanes");
    g->AttachControl(new IVButtonControl(buttons.GetGridCell(2, 0, 3, 1).GetPadded(-3.f), [this](IControl* c) {
      SplashClickActionFunc(c);
      Send({Command::kClearLanes});
    }, "CLEAR", style), kNoTag, "lanes");
    r.ReduceFromRight(8.f);
    for (int i = 0; i < DriftLanes::kMaxLanes; i++)
      g->AttachControl(new LaneRowControl(r.GetGridCell(i % 4, i / 4, 4, 2).GetPadded(-3.f), *this, i), kCtrlTagLane0 + i, "lanes");
    g->ForControlInGroup("lanes", [this](IControl* c) { c->Hide(!mShowLanes); });

    // Where the synth's editor goes.
    g->AttachControl(new ITextControl(g->GetBounds().GetReducedFromTop(stripH).GetPadded(-12.f), "", IText(16.f, kTextDim, "Roboto-Regular")),
                     kCtrlTagPlaceholder);
  };
#endif
}

Drifter::~Drifter()
{
  CloseSynthEditor();
  // The hosted synths go before anything they might call back into.
  for (auto& h : mInstances)
    h->SetEditCallback(nullptr);
  mInstances.clear();
}

// ---- Main thread ----------------------------------------------------------------------------

void Drifter::Send(const Command& c)
{
  const int w = mQueueWrite.load(std::memory_order_relaxed), next = (w + 1) % kQueue;
  if (next == mQueueRead.load(std::memory_order_acquire))
    return; // full (the audio thread isn't running): dropped
  mQueue[(size_t)w] = c;
  mQueueWrite.store(next, std::memory_order_release);
}

void Drifter::LoadDialog()
{
#if IPLUG_EDITOR
  if (!GetUI())
    return;
  mDialogFile.Set("");
  GetUI()->PromptForFile(mDialogFile, mDialogPath, EFileAction::Open, "vst3", [this](const WDL_String& file, const WDL_String&) {
    if (file.GetLength())
    {
      LoadSynth(file.Get(), 0);
      Send({Command::kClearLanes}); // a new synth: the old lanes meant nothing to it
    }
  });
#endif
}

bool Drifter::LoadSynth(const std::string& path, int classIndex, const std::vector<uint8_t>* component, const std::vector<uint8_t>* controller)
{
  std::string error;
  auto h = HostedInstrument::Load(path, classIndex, error);
  if (!h)
  {
    SetMessage("can't load: " + error);
    return false;
  }
  CloseSynthEditor(); // (the old synth's)
  if (component)
    h->RestoreState(*component, controller ? *controller : std::vector<uint8_t>{});
  h->SetEditCallback([this](HostedInstrument::Edit e, uint32_t id, double v) { OnHostedEdit(e, id, v); });
  if (GetSampleRate() > 0.)
    h->Prepare(GetSampleRate(), std::max(GetBlockSize(), 64));
  mMain = h.get();
  mMainClass = classIndex;
  mInstances.push_back(std::move(h));
  mMissingPath.clear();
  mMissingComponent.clear();
  mMissingController.clear();
  UpdateSlots();
  // Hand it to the audio thread (an earlier offer it never took is freed here).
  if (Hosted* stale = mOffered.exchange(mMain))
    mInstances.erase(std::remove_if(mInstances.begin(), mInstances.end(), [stale](const auto& p) { return p.get() == stale; }), mInstances.end());
  SetLatency(mMain->Latency());
  SetMessage("");
  // Its editor, under the strip (or, with Drifter's window closed, the size it will need).
  if (GetUI())
    OpenSynthEditor(false);
  else
  {
    ProbeEditorSize();
    FitWindow();
  }
  return true;
}

// ---- The synth's editor ----------------------------------------------------------------------

double Drifter::PlatformScale() const
{
#if IPLUG_EDITOR && defined(OS_WIN)
  // Windows sizes in pixels; Drifter lays out in units the screen scale multiplies.
  if (const IGraphics* g = GetUI())
    return std::max(1.f, g->GetScreenScale());
#endif
  return 1.;
}

int Drifter::StripHeight() const { return kStripTop + (mShowLanes ? kLanesHeight : 0); }

void Drifter::ProbeEditorSize()
{
  int w = 0, h = 0;
  const double scale = PlatformScale();
  if (mMain && mMain->EditorSize(w, h) && w > 0 && h > 0)
  {
    mEditorW = (int)std::lround(w / scale);
    mEditorH = (int)std::lround(h / scale);
  }
  else
    mEditorW = mEditorH = 0;
}

void Drifter::OpenSynthEditor(bool deferFit)
{
#if IPLUG_EDITOR
  IGraphics* g = GetUI();
  if (!g || !mMain || mContainer)
    return;
  ProbeEditorSize();
  const double scale = PlatformScale();
  auto px = [scale](int v) { return (int)std::lround(v * scale); };
  if (mEditorW > 0)
    mContainer = drifter_editor::CreateContainer(g->GetWindow(), 0, px(StripHeight()), px(mEditorW), px(mEditorH));
  int w = 0, h = 0;
  if (mContainer)
  {
    // When the synth resizes its editor: the container and Drifter's window follow.
    mMain->SetEditorResizeCallback([this](int nw, int nh) {
      const double sc = PlatformScale();
      mEditorW = (int)std::lround(nw / sc);
      mEditorH = (int)std::lround(nh / sc);
      FitWindow();
    });
    if (mMain->OpenEditor(mContainer, scale, w, h) && w > 0 && h > 0)
    {
      mEditorW = (int)std::lround(w / scale);
      mEditorH = (int)std::lround(h / scale);
    }
    else
    {
      mMain->SetEditorResizeCallback(nullptr);
      drifter_editor::DestroyContainer(mContainer);
      mContainer = nullptr;
      mEditorW = mEditorH = 0;
    }
  }
  if (deferFit)
    mFitPending = true; // (from OnIdle: not inside the host opening Drifter's window)
  else
    FitWindow();
#else
  (void)deferFit;
#endif
}

void Drifter::CloseSynthEditor()
{
#if IPLUG_EDITOR
  if (mMain)
  {
    mMain->CloseEditor();
    mMain->SetEditorResizeCallback(nullptr);
  }
  if (mContainer)
  {
    drifter_editor::DestroyContainer(mContainer);
    mContainer = nullptr;
  }
#endif
}

void Drifter::FitWindow()
{
  const int w = std::max(kStripWidth, mEditorW), h = StripHeight() + (mEditorW > 0 ? mEditorH : kNoEditorHeight);
#if IPLUG_EDITOR
  if (IGraphics* g = GetUI())
  {
    const double scale = PlatformScale();
    if (mContainer)
      drifter_editor::MoveContainer(mContainer, 0, (int)std::lround(StripHeight() * scale), (int)std::lround(mEditorW * scale),
                                    (int)std::lround(mEditorH * scale));
    if (w != g->Width() || h != g->Height())
      g->Resize(w, h, 1.f); // (lays the strip out again)
    else
      mLayoutFunc(g);
    UpdateStatus();
    return;
  }
#endif
  SetEditorSize(w, h);
}

void Drifter::OnUIOpen()
{
  Plugin::OnUIOpen();
  OpenSynthEditor(true);
  UpdateStatus();
}

void Drifter::OnUIClose()
{
  CloseSynthEditor();
  Plugin::OnUIClose();
}

// The slots follow the synth's driftable parameters, in order: named after them, holding their
// values (set quietly, so loading isn't a burst of automation).
void Drifter::UpdateSlots()
{
  const std::vector<int> params = mMain ? mMain->DriftableParameters() : std::vector<int>{};
  const int used = std::min((int)params.size(), kNumSlots);
  for (int i = 0; i < kNumSlots; i++)
  {
    mSlotParam[(size_t)i] = i < used ? params[(size_t)i] : -1;
    mSlotId[(size_t)i] = i < used ? mMain->Parameter(params[(size_t)i]).id : 0;
    if (i < used)
    {
      const double v = mMain->ControllerValue(mSlotId[(size_t)i]);
      mSlotLoaded[(size_t)i] = v;
      GetParam(kParamSlot0 + i)->Set(v);
#if defined(VST3_API)
      setParamNormalized(kParamSlot0 + i, v);
#endif
    }
  }
  mNumSlotsUsed = used;
  RenameSlots();
}

// Each slot takes its parameter's name, and the host is told (VST3). iPlug2 copies a parameter's
// title once, at start-up, so the stored VST3 titles are rewritten here directly.
void Drifter::RenameSlots()
{
#if defined(VST3_API)
  for (int i = 0; i < kNumSlots; i++)
    if (auto* p = getParameterObject(kParamSlot0 + i))
    {
      std::string name;
      if (mSlotParam[(size_t)i] >= 0)
        name = mMain->Parameter(mSlotParam[(size_t)i]).title;
      else
        name = "Slot " + std::to_string(i + 1) + " (unused)";
      Steinberg::UString(p->getInfo().title, 128).fromAscii(name.c_str());
    }
  if (componentHandler)
    componentHandler->restartComponent(Steinberg::Vst::kParamTitlesChanged | Steinberg::Vst::kParamValuesChanged);
#endif
}

// An edit from the synth's own editor: to the lanes (learn, grabbing) and the synth's processor
// (via the audio thread), and to its slot, so the host sees (and can record) the change.
void Drifter::OnHostedEdit(Hosted::Edit e, uint32_t id, double value)
{
  Command c{Command::kEdit};
  c.id = id;
  c.a = value;
  c.edit = e == Hosted::Edit::kBegin ? (int)DriftLanes::Edit::kBegin : e == Hosted::Edit::kEnd ? (int)DriftLanes::Edit::kEnd : (int)DriftLanes::Edit::kPerform;
  Send(c);
  for (int i = 0; i < mNumSlotsUsed.load(); i++)
    if (mSlotId[(size_t)i] == id)
    {
      if (e == Hosted::Edit::kBegin)
        BeginInformHostOfParamChangeFromUI(kParamSlot0 + i);
      else if (e == Hosted::Edit::kPerform)
        SendParameterValueFromUI(kParamSlot0 + i, value);
      else
        EndInformHostOfParamChangeFromUI(kParamSlot0 + i);
      break;
    }
}

void Drifter::UpdateStatus()
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  auto setText = [ui](int tag, const std::string& text) {
    if (auto* c = static_cast<ITextControl*>(ui->GetControlWithTag(tag)))
      if (text != c->GetStr())
        c->SetStr(text.c_str());
  };
  setText(kCtrlTagName, mMain ? mMain->Name() : mMissingPath.empty() ? "no synth loaded" : "missing synth");
  const int lanes = mLaneCount.load();
  std::string status = mMessage;
  if (mLearning.load())
    status = "move a control in the synth to make it a lane";
  else if (status.empty() && mMain && lanes == 0)
    status = "no lanes yet: + LANE (LEARN), then move a control in the synth";
  else if (status.empty() && lanes >= DriftLanes::kMaxLanes)
    status = "8 lanes: the most there can be";
  setText(kCtrlTagStatus, status);
  if (auto* learn = dynamic_cast<IVButtonControl*>(ui->GetControlWithTag(kCtrlTagLearn)))
  {
    const char* label = mLearning.load() ? "CANCEL LEARN" : "+ LANE (LEARN)";
    if (std::strcmp(learn->GetLabelStr(), label) != 0)
      learn->SetLabelStr(label);
  }
  // Under the strip: the synth's editor, or what's there instead.
  std::string placeholder;
  if (!mMain)
    placeholder = mMissingPath.empty() ? "LOAD a VST3 instrument: it plays here, and its own editor appears here."
                                       : "Can't find " + mMissingPath + "\nIts settings and the lanes are kept: put it back, or LOAD another synth.";
  else if (!mContainer)
    placeholder = mMain->Name() + " has no editor of its own: + FROM LIST adds its parameters as lanes.";
  if (auto* ph = static_cast<ITextControl*>(ui->GetControlWithTag(kCtrlTagPlaceholder)))
  {
    if (placeholder != ph->GetStr())
      ph->SetStr(placeholder.c_str());
    ph->Hide(placeholder.empty());
  }
  for (int i = 0; i < DriftLanes::kMaxLanes; i++)
    if (IControl* row = ui->GetControlWithTag(kCtrlTagLane0 + i))
      row->SetDirty(false);
#endif
}

void Drifter::OnIdle()
{
  // A synth the audio thread has let go of: free it.
  if (Hosted* r = mRetired.exchange(nullptr))
    mInstances.erase(std::remove_if(mInstances.begin(), mInstances.end(), [r](const auto& p) { return p.get() == r; }), mInstances.end());
  if (mFitPending)
  {
    mFitPending = false;
    FitWindow();
  }
  if (mMain)
  {
    const int flags = mMain->TakeRestartFlags();
    if (flags & HostedInstrument::kTitlesChanged)
    {
      mMain->RefreshParameters();
      UpdateSlots();
    }
    // The synth's own controls show where the lanes are, a few times a second.
    if (++mDisplayTick >= 5)
    {
      mDisplayTick = 0;
      for (int i = 0; i < mLaneCount.load(); i++)
        mMain->SetControllerValue(mLaneView[(size_t)i].id.load(), mLaneView[(size_t)i].value.load());
    }
  }
  UpdateStatus();
}

// ---- State ----------------------------------------------------------------------------------

// Version 1: magic, version, parameter count, the parameters (Drifter's own and the slots), the
// synth (path, class index, class ID, its component and controller state), then the lanes
// (count; each: ID, home, lo, hi, on).
bool Drifter::SerializeState(IByteChunk& chunk) const
{
  chunk.Put(&kStateMagic);
  chunk.Put(&kStateVersion);
  const int32_t n = kNumParams;
  chunk.Put(&n);
  for (int i = 0; i < kNumParams; i++)
  {
    const double v = GetParam(i)->Value();
    chunk.Put(&v);
  }
  std::vector<uint8_t> comp, ctrl;
  std::string path = mMissingPath, classId = mMissingClassId;
  int32_t cls = mMissingClass;
  if (mMain)
  {
    mMain->SaveState(comp, ctrl);
    path = mMain->BundlePath();
    classId = mMain->ClassId();
    cls = mMainClass;
  }
  else
  {
    comp = mMissingComponent;
    ctrl = mMissingController;
  }
  chunk.PutStr(path.c_str());
  chunk.Put(&cls);
  chunk.PutStr(classId.c_str());
  PutBytes(chunk, comp);
  PutBytes(chunk, ctrl);
  const int32_t lanes = mLaneCount.load();
  chunk.Put(&lanes);
  for (int i = 0; i < lanes; i++)
  {
    const LaneView& v = mLaneView[(size_t)i];
    const uint32_t id = v.id.load();
    const double home = v.home.load(), lo = v.lo.load(), hi = v.hi.load();
    const int32_t on = v.on.load();
    chunk.Put(&id);
    chunk.Put(&home);
    chunk.Put(&lo);
    chunk.Put(&hi);
    chunk.Put(&on);
  }
  const int32_t showLanes = mShowLanes;
  chunk.Put(&showLanes);
  return true;
}

int Drifter::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int32_t magic = 0, version = 0, n = 0;
  int p = chunk.Get(&magic, startPos);
  if (p < 0 || magic != kStateMagic)
    return UnserializeParams(chunk, startPos);
  p = chunk.Get(&version, p);
  if (p >= 0) p = chunk.Get(&n, p);
  if (p < 0 || n < 0)
    return -1;
  ENTER_PARAMS_MUTEX
  for (int i = 0; i < n && p >= 0; i++)
  {
    double v = 0.;
    p = chunk.Get(&v, p);
    if (p >= 0 && i < kNumParams)
      GetParam(i)->Set(v);
  }
  OnParamReset(kPresetRecall);
  LEAVE_PARAMS_MUTEX
  WDL_String path, classId;
  int32_t cls = 0;
  std::vector<uint8_t> comp, ctrl;
  if (p >= 0) p = chunk.GetStr(path, p);
  if (p >= 0) p = chunk.Get(&cls, p);
  if (p >= 0) p = chunk.GetStr(classId, p);
  if (p >= 0) p = GetBytes(chunk, comp, p);
  if (p >= 0) p = GetBytes(chunk, ctrl, p);
  int32_t lanes = 0;
  if (p >= 0) p = chunk.Get(&lanes, p);
  if (p < 0)
    return p;
  if (path.GetLength() && !LoadSynth(path.Get(), cls, &comp, &ctrl))
  {
    // Keep it, so saving again doesn't lose the synth or its settings.
    CloseSynthEditor();
    mMain = nullptr;
    mEditorW = mEditorH = 0;
    mMissingPath = path.Get();
    mMissingClass = cls;
    mMissingClassId = classId.Get();
    mMissingComponent = comp;
    mMissingController = ctrl;
    SetMessage("synth not found: " + mMissingPath);
  }
  Send({Command::kClearLanes});
  for (int i = 0; i < lanes && p >= 0; i++)
  {
    uint32_t id = 0;
    double home = 0., lo = 0., hi = 1.;
    int32_t on = 1;
    p = chunk.Get(&id, p);
    if (p >= 0) p = chunk.Get(&home, p);
    if (p >= 0) p = chunk.Get(&lo, p);
    if (p >= 0) p = chunk.Get(&hi, p);
    if (p >= 0) p = chunk.Get(&on, p);
    if (p < 0)
      break;
    Command c{Command::kAddLane};
    c.id = id;
    c.a = home;
    c.b = lo;
    c.c = hi;
    Send(c);
    if (!on)
    {
      Command off{Command::kSetOn};
      off.index = i;
      off.a = 0.;
      Send(off);
    }
    // (Also reflected at once, so saving before the audio thread runs keeps them.)
    mLaneView[(size_t)i].id = id;
    mLaneView[(size_t)i].home = home;
    mLaneView[(size_t)i].lo = lo;
    mLaneView[(size_t)i].hi = hi;
    mLaneView[(size_t)i].value = home;
    mLaneView[(size_t)i].on = on != 0;
  }
  mLaneCount = std::min<int>(lanes, DriftLanes::kMaxLanes);
  if (version >= 2 && p >= 0)
  {
    int32_t showLanes = 1;
    p = chunk.Get(&showLanes, p);
    mShowLanes = showLanes != 0;
  }
  FitWindow();
  return p;
}

// ---- Audio thread ---------------------------------------------------------------------------

#if IPLUG_DSP
void Drifter::OnReset()
{
  // (Not processing now: the synth can be set up directly.)
  const int block = std::max(GetBlockSize(), 64);
  mOutL.assign((size_t)block, 0.f);
  mOutR.assign((size_t)block, 0.f);
  if (Hosted* offered = mOffered.exchange(nullptr))
  {
    if (mPlaying && mPlaying != offered)
      mRetired.store(mPlaying);
    mPlaying = offered;
  }
  if (mPlaying)
  {
    mPlaying->Prepare(GetSampleRate(), block);
    SetLatency(mPlaying->Latency());
  }
  mMidiQueue.Resize(1024);
  mMidiQueue.Clear();
  mSlotSent.fill(-1.);
  mLaneSent.fill(-1.);
}

void Drifter::ProcessMidiMsg(const IMidiMsg& msg) { mMidiQueue.Add(msg); }

void Drifter::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  (void)inputs;
  const int nOut = NOutChansConnected();
  mChanges.clear();
  auto change = [this](uint32_t id, double v) {
    if (mChanges.size() < mChanges.capacity()) // (reserved: never grows on the audio thread)
      mChanges.push_back({id, 0, v});
  };

  // A newly loaded synth: take it (its slots already hold its values), and hand back the old one.
  if (mRetired.load() == nullptr)
    if (Hosted* next = mOffered.exchange(nullptr))
    {
      if (mPlaying)
        mRetired.store(mPlaying);
      mPlaying = next;
      // What the synth holds, so automation that arrived with this block still reaches it.
      for (int i = 0; i < kNumSlots; i++)
        mSlotSent[(size_t)i] = mSlotLoaded[(size_t)i].load();
      mLaneSent.fill(-1.);
    }

  // Commands from the main thread.
  for (int r = mQueueRead.load(std::memory_order_relaxed); r != mQueueWrite.load(std::memory_order_acquire); r = (r + 1) % kQueue)
  {
    const Command& c = mQueue[(size_t)r];
    switch (c.type)
    {
      case Command::kAddLane: mLanes.Add(c.id, c.a, c.b, c.c); break;
      case Command::kRemoveLane: mLanes.Remove(c.index); break;
      case Command::kSetRange: mLanes.SetRange(c.index, c.a, c.b); break;
      case Command::kSetOn: mLanes.SetOn(c.index, c.a > 0.5); break;
      case Command::kLearn: mLanes.StartLearn(); break;
      case Command::kCancelLearn: mLanes.CancelLearn(); break;
      case Command::kKeep: mLanes.Keep(); break;
      case Command::kReturn: mLanes.Return(); break;
      case Command::kClearLanes: mLanes.Clear(); break;
      case Command::kEdit:
        mLanes.OnEdit((DriftLanes::Edit)c.edit, c.id, c.a);
        if (c.edit == (int)DriftLanes::Edit::kPerform)
          change(c.id, c.a); // the synth's processor hears its editor's change
        break;
    }
    mQueueRead.store((r + 1) % kQueue, std::memory_order_release);
    mLaneSent.fill(-1.);
  }

  // The drift settings.
  underheard::Drifter& eng = mLanes.Engine();
  eng.SetEnabled(GetParam(kParamDrift)->Bool());
  eng.SetCurve(GetParam(kParamCurve)->Int());
  eng.SetSmear(GetParam(kParamSmear)->Value() / 100.);
  eng.SetReach(GetParam(kParamReach)->Value() / 100.);
  eng.SetGravity(GetParam(kParamGravity)->Value() / 100.);
  if (GetParam(kParamTiming)->Int() == 1)
    mLanes.SetLengthBars(kBarChoices[std::clamp(GetParam(kParamBars)->Int(), 0, kNumBarChoices - 1)]);
  else
    mLanes.SetLengthSeconds(GetParam(kParamLength)->Value());

  // The slots (automation from the host): to the synth, or, for a drifting parameter, its home.
  const int used = mNumSlotsUsed.load();
  for (int i = 0; i < used; i++)
  {
    const double v = GetParam(kParamSlot0 + i)->Value();
    if (v != mSlotSent[(size_t)i])
    {
      mSlotSent[(size_t)i] = v;
      const uint32_t id = mSlotId[(size_t)i];
      if (mLanes.OnAutomation(id, v) < 0)
        change(id, v);
    }
  }

  // The drift: only while the song plays.
  const ITimeInfo& t = mTimeInfo;
  const double tempo = t.mTempo > 0. ? t.mTempo : 120.;
  const double beatsPerBar = t.mNumerator > 0 && t.mDenominator > 0 ? t.mNumerator * 4. / t.mDenominator : 4.;
  mLanes.Advance(nFrames / GetSampleRate(), t.mTransportIsRunning, tempo, beatsPerBar);
  for (int i = 0; i < mLanes.Count(); i++)
  {
    const double v = mLanes.Value(i);
    if (std::fabs(v - mLaneSent[(size_t)i]) > 1e-7)
    {
      mLaneSent[(size_t)i] = v;
      change(mLanes.LaneAt(i).id, v);
    }
  }

  // The synth plays the block (in pieces if the host's block is bigger than prepared).
  if (!mPlaying)
  {
    for (int c = 0; c < nOut; c++)
      std::fill(outputs[c], outputs[c] + nFrames, 0.);
    mMidiQueue.Flush(nFrames);
  }
  else
  {
    underheard::vst3host::Transport tr;
    tr.sampleRate = GetSampleRate();
    tr.tempo = tempo;
    tr.ppq = t.mPPQPos;
    tr.playing = t.mTransportIsRunning;
    tr.samplePos = (int64_t)t.mSamplePos;
    const int chunk = (int)mOutL.size();
    for (int pos = 0; pos < nFrames; pos += chunk)
    {
      const int n = std::min(chunk, nFrames - pos);
      mEvents.clear();
      while (!mMidiQueue.Empty() && mMidiQueue.Peek().mOffset < pos + n)
      {
        const IMidiMsg& m = mMidiQueue.Peek();
        if (mEvents.size() < mEvents.capacity())
          mEvents.push_back({std::max(0, m.mOffset - pos), m.mStatus, m.mData1, m.mData2});
        mMidiQueue.Remove();
      }
      mPlaying->Process(mEvents.data(), (int)mEvents.size(), pos == 0 ? mChanges.data() : nullptr, pos == 0 ? (int)mChanges.size() : 0, tr, mOutL.data(),
                        mOutR.data(), n);
      for (int i = 0; i < n; i++)
      {
        if (nOut > 0) outputs[0][pos + i] = mOutL[(size_t)i];
        if (nOut > 1) outputs[1][pos + i] = mOutR[(size_t)i];
      }
      tr.samplePos += n;
    }
    mMidiQueue.Flush(nFrames);
  }

  // What the main thread shows (and saves).
  const int lanes = mLanes.Count();
  for (int i = 0; i < lanes; i++)
  {
    const DriftLanes::Lane& l = mLanes.LaneAt(i);
    LaneView& v = mLaneView[(size_t)i];
    v.id = l.id;
    v.home = l.home;
    v.lo = l.lo;
    v.hi = l.hi;
    v.on = l.on;
    v.value = mLanes.Value(i);
  }
  mLaneCount = lanes;
  mLearning = mLanes.Learning();
}
#endif // IPLUG_DSP
