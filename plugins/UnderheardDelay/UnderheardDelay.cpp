#include "UnderheardDelay.h"
#include "IPlug_include_in_plug_src.h"

#include "NoteValues.h"
#if IPLUG_EDITOR
#include "UnderheardUI.h"
using namespace underheard_ui;
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using underheard::fx::DubDelay;

namespace {

constexpr int32_t kStateMagic = 'UDLY';
constexpr int32_t kStateVersion = 2; // 2: Warmth's 50% is version 1's 100%

// Starting points: each changes these parameters from their defaults (the rest stay at the
// defaults). Note indices are NoteValues(): 6 = 1/8, 8 = 1/8 dotted, 11 = 1/4 dotted.
struct Template
{
  const char* name;
  std::vector<std::pair<int, double>> set;
};
const std::vector<Template>& Templates()
{
  static const std::vector<Template> t = {
    {"Tape echo", {}},
    {"Dub echo", {{kParamMix, 40.}, {kParamSync, 1.}, {kParamNote, 8.}, {kParamGlide, 600.}, {kParamFeedback, 95.}, {kParamMode, 2.}, {kParamHeads, 5.},
                  {kParamLowCut, 200.}, {kParamHighCut, 1800.}, {kParamResonance, 45.}, {kParamDrive, 50.}, {kParamAge, 50.}, {kParamWow, 25.},
                  {kParamFlutter, 12.}, {kParamPhase, 90.}, {kParamSpread, 40.}}},
    {"Dub throw", {{kParamMix, 50.}, {kParamSync, 1.}, {kParamNote, 8.}, {kParamGlide, 800.}, {kParamFeedback, 100.}, {kParamMode, 2.}, {kParamHeads, 5.},
                   {kParamLowCut, 220.}, {kParamHighCut, 1600.}, {kParamResonance, 50.}, {kParamDrive, 55.}, {kParamAge, 55.}, {kParamWow, 25.},
                   {kParamFlutter, 12.}, {kParamPhase, 90.}, {kParamSpread, 50.}, {kParamSend, 1.}}},
    {"Slapback", {{kParamMix, 30.}, {kParamSync, 0.}, {kParamTime, 95.}, {kParamGlide, 30.}, {kParamFeedback, 8.}, {kParamMode, 0.}, {kParamLowCut, 90.},
                  {kParamHighCut, 5000.}, {kParamResonance, 0.}, {kParamDrive, 35.}, {kParamAge, 20.}, {kParamWow, 8.}, {kParamFlutter, 15.},
                  {kParamPhase, 0.}, {kParamSpread, 0.}}},
    {"Large atmosphere", {{kParamMix, 40.}, {kParamSync, 1.}, {kParamNote, 11.}, {kParamGlide, 1500.}, {kParamFeedback, 78.}, {kParamMode, 1.},
                          {kParamLowCut, 250.}, {kParamHighCut, 3200.}, {kParamResonance, 5.}, {kParamDrive, 20.}, {kParamAge, 65.}, {kParamWow, 50.},
                          {kParamFlutter, 8.}, {kParamPhase, 180.}, {kParamSpread, 100.}, {kParamDuck, 45.}}},
    {"Ping-pong eighths", {{kParamMix, 30.}, {kParamSync, 1.}, {kParamNote, 6.}, {kParamFeedback, 50.}, {kParamMode, 1.}, {kParamHighCut, 6000.},
                           {kParamAge, 20.}, {kParamSpread, 100.}}},
    {"Space echo", {{kParamMix, 35.}, {kParamSync, 0.}, {kParamTime, 450.}, {kParamFeedback, 85.}, {kParamMode, 2.}, {kParamHeads, 6.},
                    {kParamHighCut, 3500.}, {kParamDrive, 40.}, {kParamAge, 40.}, {kParamWow, 30.}, {kParamFlutter, 25.}}},
  };
  return t;
}

} // namespace

UnderheardDelay::UnderheardDelay(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamOutput)->InitGain("Output", 0., -70., 12.);
  GetParam(kParamMix)->InitPercentage("Mix (dry-echoes)", 35.);
  GetParam(kParamSync)->InitEnum("Sync", 1, {"Free", "Sync"});
  GetParam(kParamTime)->InitDouble("Time", 375., 1., 2000., 0.1, "ms", 0, "", IParam::ShapePowCurve(2.));
  {
    IParam* note = GetParam(kParamNote);
    const auto& notes = underheard::fx::NoteValues();
    note->InitEnum("Note", 8, underheard::fx::kNumBarOrLess); // 1/8 dotted, the dub classic
    for (int i = 0; i < underheard::fx::kNumBarOrLess; i++)
      note->SetDisplayText(i, notes[(size_t)i].name);
  }
  GetParam(kParamGlide)->InitDouble("Glide", 150., 5., 3000., 1., "ms", 0, "", IParam::ShapePowCurve(2.5));
  GetParam(kParamFeedback)->InitDouble("Feedback", 45., 0., 110., 0.1, "%");
  GetParam(kParamPolarity)->InitEnum("Polarity", 0, {"Normal", "Inverted"});
  GetParam(kParamMode)->InitEnum("Heads", DubDelay::kSingle, {"Single", "Ping-pong", "Multi-head"});
  GetParam(kParamHeads)->InitEnum("Head Select", 6, {"1", "2", "3", "1+2", "1+3", "2+3", "1+2+3"});
  GetParam(kParamLowCut)->InitFrequency("Low Cut", 120., 20., 2000.);
  GetParam(kParamHighCut)->InitFrequency("High Cut", 4500., 300., 20000.);
  GetParam(kParamResonance)->InitPercentage("Resonance", 10.);
  GetParam(kParamDrive)->InitPercentage("Drive", 30.);
  GetParam(kParamAge)->InitPercentage("Age", 30.);
  GetParam(kParamWow)->InitPercentage("Wow", 20.);
  GetParam(kParamFlutter)->InitPercentage("Flutter", 15.);
  GetParam(kParamPhase)->InitDouble("Phase", 90., 0., 360., 1., "deg");
  GetParam(kParamSpread)->InitPercentage("Spread", 30.);
  GetParam(kParamSend)->InitEnum("Send", 0, {"Always", "Throw"});
  GetParam(kParamThrow)->InitBool("Throw", false);
  GetParam(kParamFreeze)->InitBool("Freeze", false);
  GetParam(kParamDuck)->InitPercentage("Duck", 0.);
  GetParam(kParamWarmth)->InitPercentage("Warmth", 50.);

  // The templates, as the host's factory presets (in the state format, so they load like a
  // saved project).
  for (const Template& t : Templates())
  {
    std::vector<double> v(kNumParams);
    for (int i = 0; i < kNumParams; i++)
      v[(size_t)i] = GetParam(i)->GetDefault();
    for (const auto& [param, value] : t.set)
      v[(size_t)param] = value;
    IByteChunk chunk;
    chunk.Put(&kStateMagic);
    chunk.Put(&kStateVersion);
    const int32_t n = kNumParams;
    chunk.Put(&n);
    for (double x : v)
      chunk.Put(&x);
    MakePresetFromChunk(t.name, chunk);
  }

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
  };

  mLayoutFunc = [&](IGraphics* g) {
    g->AttachCornerResizer(EUIResizerMode::Scale, false);
    g->AttachPanelBackground(kBackground);
    g->LoadFont("Roboto-Regular", ROBOTO_FN);
    const IVStyle style = PanelStyle();
    const IText rowLabel(14.f, kTextDim, "Roboto-Regular", EAlign::Near);
    IRECT b = g->GetBounds().GetPadded(-16.f);
    IRECT title = b.ReduceFromTop(36.f);
    g->AttachControl(new ITextControl(title.ReduceFromLeft(260.f), "UNDERHEARD DELAY", IText(22.f, kText, "Roboto-Regular", EAlign::Near)));
    {
      std::vector<std::string> names;
      for (const Template& t : Templates())
        names.push_back(t.name);
      g->AttachControl(new MenuButton(title.ReduceFromRight(150.f).GetPadded(-2.f), "TEMPLATES", style, names, [this](int i) { ApplyTemplate(i); }));
      title.ReduceFromRight(12.f);
    }
    g->AttachControl(new ITextControl(title, "", IText(15.f, kText, "Roboto-Regular", EAlign::Near)), kCtrlTagStatus);
    b.ReduceFromTop(8.f);

    // Meters down the right.
    IRECT meters = b.ReduceFromRight(110.f);
    b.ReduceFromRight(12.f);
    g->AttachControl(new IVPeakAvgMeterControl<2>(meters.FracRectHorizontal(0.5f), "In", style, EDirection::Vertical, {"L", "R"}), kCtrlTagInputMeter);
    g->AttachControl(new IVPeakAvgMeterControl<2>(meters.FracRectHorizontal(0.5f, true), "Out", style, EDirection::Vertical, {"L", "R"}), kCtrlTagOutputMeter);

    auto row = [&](const char* label, float h) {
      IRECT r = b.ReduceFromTop(h);
      b.ReduceFromTop(8.f);
      g->AttachControl(new ITextControl(r.ReduceFromLeft(90.f), label, rowLabel));
      return r;
    };
    auto cell = [](const IRECT& r, int i, int span = 1) {
      IRECT c = r.GetGridCell(0, i, 1, 6);
      for (int k = 1; k < span; k++)
        c = c.Union(r.GetGridCell(0, i + k, 1, 6));
      return c.GetPadded(-5.f);
    };

    IRECT r = row("TIME", 110.f);
    g->AttachControl(new IVTabSwitchControl(cell(r, 0).GetMidVPadded(20.f), kParamSync, {"Free", "Sync"}, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 1), kParamTime, "Time", style));
    g->AttachControl(new IVMenuButtonControl(cell(r, 2).GetMidVPadded(20.f), kParamNote, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamGlide, "Glide", style));
    g->AttachControl(new IVKnobControl(cell(r, 4), kParamFeedback, "Feedback", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 5).GetMidVPadded(34.f), kParamPolarity, {"+", "inv"}, "Polarity", style));

    r = row("HEADS", 56.f);
    g->AttachControl(new IVTabSwitchControl(cell(r, 0, 3).GetMidVPadded(20.f), kParamMode, {"Single", "Ping-pong", "Multi-head"}, "", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 3, 3).GetMidVPadded(20.f), kParamHeads, {"1", "2", "3", "1+2", "1+3", "2+3", "all"}, "", style));

    r = row("LOOP", 110.f);
    g->AttachControl(new IVKnobControl(cell(r, 0), kParamLowCut, "Low Cut", style));
    g->AttachControl(new IVKnobControl(cell(r, 1), kParamHighCut, "High Cut", style));
    g->AttachControl(new IVKnobControl(cell(r, 2), kParamResonance, "Resonance", style));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamDrive, "Drive", style));
    g->AttachControl(new IVKnobControl(cell(r, 4), kParamAge, "Age", style));

    r = row("TAPE", 110.f);
    g->AttachControl(new IVKnobControl(cell(r, 0), kParamWow, "Wow", style));
    g->AttachControl(new IVKnobControl(cell(r, 1), kParamFlutter, "Flutter", style));
    g->AttachControl(new IVKnobControl(cell(r, 2), kParamPhase, "Phase", style));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamSpread, "Spread", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamWarmth, "Warmth", style));

    r = row("PLAY", 110.f);
    g->AttachControl(new IVTabSwitchControl(cell(r, 0).GetMidVPadded(20.f), kParamSend, {"Always", "Throw"}, "", style));
    g->AttachControl(new HoldButton(cell(r, 1).GetMidVPadded(24.f), kParamThrow, "THROW", style));
    g->AttachControl(new IVToggleControl(cell(r, 2).GetMidVPadded(24.f), kParamFreeze, "", style, "FREEZE", "FROZEN"));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamDuck, "Duck", style));
    g->AttachControl(new IVKnobControl(cell(r, 4), kParamMix, "Dry / Echoes", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamOutput, "Output", style));
    UpdateEnabled();
  };
#endif
}

// ---- Main thread ------------------------------------------------------------------------

void UnderheardDelay::UpdateEnabled()
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  const bool synced = GetParam(kParamSync)->Int() == 1;
  const bool multi = GetParam(kParamMode)->Int() == DubDelay::kMultiHead;
  ui->ForControlWithParam(kParamTime, [&](IControl* c) { c->SetDisabled(synced); });
  ui->ForControlWithParam(kParamNote, [&](IControl* c) { c->SetDisabled(!synced); });
  ui->ForControlWithParam(kParamHeads, [&](IControl* c) { c->SetDisabled(!multi); });
#endif
}

// Sets every parameter to the template's values, telling the host (so it's undoable and
// automation sees it).
void UnderheardDelay::ApplyTemplate(int index)
{
  const auto& all = Templates();
  if (index < 0 || index >= (int)all.size())
    return;
  std::vector<double> v(kNumParams);
  for (int i = 0; i < kNumParams; i++)
    v[(size_t)i] = GetParam(i)->GetDefault();
  for (const auto& [param, value] : all[(size_t)index].set)
    v[(size_t)param] = value;
  for (int i = 0; i < kNumParams; i++)
  {
    const double norm = GetParam(i)->ToNormalized(v[(size_t)i]);
    BeginInformHostOfParamChangeFromUI(i);
    SendParameterValueFromUI(i, norm);
    EndInformHostOfParamChangeFromUI(i);
#if IPLUG_EDITOR
    if (GetUI())
      GetUI()->ForControlWithParam(i, [norm](IControl* c) { c->SetValueFromDelegate(norm); });
#endif
  }
  UpdateEnabled();
}

#if IPLUG_EDITOR
void UnderheardDelay::OnParamChangeUI(int paramIdx, EParamSource)
{
  if (paramIdx == kParamSync || paramIdx == kParamMode)
    UpdateEnabled();
}

void UnderheardDelay::OnUIOpen()
{
  Plugin::OnUIOpen();
  UpdateEnabled();
}
#endif

void UnderheardDelay::UpdateStatus()
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  const double ms = mShownTime.load() * 1000.;
  WDL_String s;
  if (GetParam(kParamSync)->Int() == 1)
    s.SetFormatted(128, "every %.0f ms  (%s at %.1f BPM)", ms, underheard::fx::NoteValues()[(size_t)GetParam(kParamNote)->Int()].name, mShownTempo.load());
  else
    s.SetFormatted(128, "every %.0f ms", ms);
  if (GetParam(kParamFreeze)->Bool())
    s.Append("   FROZEN");
  else if (GetParam(kParamSend)->Int() == 1)
    s.Append(GetParam(kParamThrow)->Bool() ? "   THROWING" : "   send: throw only");
  if (auto* c = ui->GetControlWithTag(kCtrlTagStatus))
    if (std::string(c->As<ITextControl>()->GetStr()) != s.Get())
      c->As<ITextControl>()->SetStr(s.Get());
#endif
}

void UnderheardDelay::OnIdle()
{
  mInputPeakSender.TransmitData(*this);
  mOutputPeakSender.TransmitData(*this);
  UpdateStatus();
}

bool UnderheardDelay::SerializeState(IByteChunk& chunk) const
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
  return true;
}

int UnderheardDelay::UnserializeState(const IByteChunk& chunk, int startPos)
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
  GetParam(kParamThrow)->Set(0.); // momentary: never reopens held
  // Before version 2, Warmth ran half as strong: the old full is the new 50%.
  if (version < 2 && n > kParamWarmth)
    GetParam(kParamWarmth)->Set(GetParam(kParamWarmth)->Value() / 2.);
  OnParamReset(kPresetRecall);
  LEAVE_PARAMS_MUTEX
  return p;
}

// ---- Audio thread -----------------------------------------------------------------------

#if IPLUG_DSP
void UnderheardDelay::OnReset()
{
  const double fs = GetSampleRate();
  mInputPeakSender.Reset(fs);
  mOutputPeakSender.Reset(fs);
  mDelay.Prepare(fs);
  mDelay.Reset();
}

void UnderheardDelay::GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const
{
  if (direction == ERoute::kInput)
    str.Set(busIdx == 0 ? "Main Input" : "SideChain");
  else
    str.Set("Output");
}

void UnderheardDelay::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const double gain = GetParam(kParamOutput)->DBToAmp();
  const int nOut = NOutChansConnected();
  // Channels are laid out per bus at each bus's maximum width: main is 0-1, sidechain 2-3.
  const bool stereoIn = IsChannelConnected(ERoute::kInput, 1);
  const sample* mainIn[2] = {inputs[0], stereoIn ? inputs[1] : inputs[0]};

  const double tempo = GetTempo();
  const bool synced = GetParam(kParamSync)->Int() == 1;
  const double time = synced ? underheard::fx::NoteSeconds(GetParam(kParamNote)->Int(), tempo) : GetParam(kParamTime)->Value() / 1000.;
  mDelay.SetTime(time);
  mDelay.SetGlide(GetParam(kParamGlide)->Value() / 1000.);
  mDelay.SetFeedback(GetParam(kParamFeedback)->Value() / 100.);
  mDelay.SetPolarity(GetParam(kParamPolarity)->Int() == 1);
  mDelay.SetMode(GetParam(kParamMode)->Int());
  mDelay.SetHeads(GetParam(kParamHeads)->Int());
  mDelay.SetLowCut(GetParam(kParamLowCut)->Value());
  mDelay.SetHighCut(GetParam(kParamHighCut)->Value());
  mDelay.SetResonance(GetParam(kParamResonance)->Value() / 100.);
  mDelay.SetDrive(GetParam(kParamDrive)->Value() / 100.);
  mDelay.SetAge(GetParam(kParamAge)->Value() / 100.);
  mDelay.SetWow(GetParam(kParamWow)->Value() / 100.);
  mDelay.SetFlutter(GetParam(kParamFlutter)->Value() / 100.);
  mDelay.SetPhase(GetParam(kParamPhase)->Value());
  mDelay.SetSpread(GetParam(kParamSpread)->Value() / 100.);
  mDelay.SetSendOpen(GetParam(kParamSend)->Int() == 0 || GetParam(kParamThrow)->Bool());
  mDelay.SetFreeze(GetParam(kParamFreeze)->Bool());
  mDelay.SetDuck(GetParam(kParamDuck)->Value() / 100.);
  mDelay.SetMix(GetParam(kParamMix)->Value() / 100.);
  mDelay.SetWarmth(GetParam(kParamWarmth)->Value() / 100.);

  for (int i = 0; i < nFrames; i++)
  {
    float l, r;
    mDelay.Process((float)mainIn[0][i], (float)mainIn[1][i], l, r);
    const double o[2] = {l * gain, r * gain};
    for (int c = 0; c < nOut; c++)
      outputs[c][i] = o[std::min(c, 1)];
  }
  mShownTime.store(mDelay.CurrentTime());
  mShownTempo.store(tempo);

  mInputPeakSender.ProcessBlock(const_cast<sample**>(mainIn), nFrames, kCtrlTagInputMeter, 2, 0);
  mOutputPeakSender.ProcessBlock(outputs, nFrames, kCtrlTagOutputMeter, nOut, 0);
}
#endif // IPLUG_DSP
