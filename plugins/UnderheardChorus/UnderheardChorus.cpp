#include "UnderheardChorus.h"
#include "IPlug_include_in_plug_src.h"

#include "NoteValues.h"
#if IPLUG_EDITOR
#include "UnderheardUI.h"
using namespace underheard_ui;
#endif

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using underheard::fx::MultiChorus;

namespace {

constexpr int32_t kStateMagic = 'UCHO';
constexpr int32_t kStateVersion = 2; // 2: Warmth's 50% is version 1's 100%

// Starting points: each changes these parameters from their defaults (the rest stay at the
// defaults). Voices is the choice index (0 = one voice). Note indices are NoteValues():
// 15 = 1 bar, 16 = 2 bars.
struct Template
{
  const char* name;
  std::vector<std::pair<int, double>> set;
};
const std::vector<Template>& Templates()
{
  static const std::vector<Template> t = {
    {"Gentle chorus", {}},
    {"Wide stereo", {{kParamVoices, 2.}, {kParamRate, 0.35}, {kParamDepth, 55.}, {kParamDelay, 14.}, {kParamSpread, 100.}}},
    {"Vibrato", {{kParamMode, 1.}, {kParamVoices, 0.}, {kParamRate, 5.5}, {kParamDepth, 35.}, {kParamDelay, 5.}, {kParamPhase, 0.},
                 {kParamSpread, 0.}}},
    {"Ensemble strings", {{kParamMix, 60.}, {kParamMode, 2.}, {kParamVoices, 2.}, {kParamRate, 0.6}, {kParamDepth, 60.}, {kParamDelay, 12.},
                          {kParamPhase, 120.}, {kParamSpread, 100.}, {kParamTone, 7000.}}},
    {"Tape warble", {{kParamMode, 1.}, {kParamVoices, 0.}, {kParamShape, 2.}, {kParamRate, 1.2}, {kParamDepth, 30.}, {kParamDelay, 6.},
                     {kParamPhase, 25.}, {kParamSpread, 30.}, {kParamAge, 70.}}},
    {"Jet flanger", {{kParamVoices, 0.}, {kParamSync, 1.}, {kParamNote, 16.}, {kParamDepth, 85.}, {kParamDelay, 1.2}, {kParamFeedback, 70.},
                     {kParamShape, 1.}, {kParamTone, 14000.}, {kParamAge, 10.}}},
    {"Dimension", {{kParamMix, 45.}, {kParamVoices, 1.}, {kParamRate, 0.25}, {kParamShape, 1.}, {kParamDepth, 25.}, {kParamDelay, 8.},
                   {kParamPhase, 180.}, {kParamSpread, 100.}, {kParamTone, 10000.}, {kParamAge, 10.}}},
  };
  return t;
}

} // namespace

UnderheardChorus::UnderheardChorus(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamOutput)->InitGain("Output", 0., -70., 12.);
  GetParam(kParamMix)->InitPercentage("Mix (dry-voices)", 50.);
  GetParam(kParamMode)->InitEnum("Mode", MultiChorus::kChorus, {"Chorus", "Vibrato", "Ensemble"});
  GetParam(kParamVoices)->InitEnum("Voices", 1, {"1", "2", "3", "4"});
  GetParam(kParamSync)->InitEnum("Sync", 0, {"Free", "Sync"});
  GetParam(kParamRate)->InitDouble("Rate", 0.6, 0.02, 12., 0.01, "Hz", 0, "", IParam::ShapePowCurve(3.));
  {
    IParam* note = GetParam(kParamNote);
    const auto& notes = underheard::fx::NoteValues();
    note->InitEnum("Note", 15, (int)notes.size()); // a bar
    for (int i = 0; i < (int)notes.size(); i++)
      note->SetDisplayText(i, notes[(size_t)i].name);
  }
  GetParam(kParamDepth)->InitPercentage("Depth", 45.);
  GetParam(kParamDelay)->InitDouble("Delay", 10., 0.3, 25., 0.1, "ms", 0, "", IParam::ShapePowCurve(2.));
  GetParam(kParamShape)->InitEnum("Shape", MultiChorus::kSine, {"Sine", "Triangle", "Wander"});
  GetParam(kParamFeedback)->InitDouble("Feedback", 0., -90., 90., 0.1, "%");
  GetParam(kParamPhase)->InitDouble("Phase", 90., 0., 360., 1., "deg");
  GetParam(kParamSpread)->InitPercentage("Spread", 80.);
  GetParam(kParamTone)->InitFrequency("Tone", 9000., 1000., 20000.);
  GetParam(kParamAge)->InitPercentage("Age", 20.);
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
    g->AttachControl(new ITextControl(title.ReduceFromLeft(260.f), "UNDERHEARD CHORUS", IText(22.f, kText, "Roboto-Regular", EAlign::Near)));
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

    IRECT r = row("MOTION", 110.f);
    g->AttachControl(new IVTabSwitchControl(cell(r, 0).GetMidVPadded(20.f), kParamSync, {"Free", "Sync"}, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 1), kParamRate, "Rate", style));
    g->AttachControl(new IVMenuButtonControl(cell(r, 2).GetMidVPadded(20.f), kParamNote, "", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 3, 2).GetMidVPadded(20.f), kParamShape, {"Sine", "Triangle", "Wander"}, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamPhase, "Phase", style));

    r = row("VOICES", 110.f);
    g->AttachControl(new IVTabSwitchControl(cell(r, 0, 2).GetMidVPadded(20.f), kParamMode, {"Chorus", "Vibrato", "Ensemble"}, "", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 2).GetMidVPadded(20.f), kParamVoices, {"1", "2", "3", "4"}, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamDepth, "Depth", style));
    g->AttachControl(new IVKnobControl(cell(r, 4), kParamDelay, "Delay", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamFeedback, "Feedback", style));

    r = row("SOUND", 110.f);
    g->AttachControl(new IVKnobControl(cell(r, 0), kParamSpread, "Spread", style));
    g->AttachControl(new IVKnobControl(cell(r, 1), kParamTone, "Tone", style));
    g->AttachControl(new IVKnobControl(cell(r, 2), kParamAge, "Age", style));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamWarmth, "Warmth", style));
    g->AttachControl(new IVKnobControl(cell(r, 4), kParamMix, "Dry / Voices", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamOutput, "Output", style));
    UpdateEnabled();
  };
#endif
}

// ---- Main thread ------------------------------------------------------------------------

void UnderheardChorus::UpdateEnabled()
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  const bool synced = GetParam(kParamSync)->Int() == 1;
  const bool vibrato = GetParam(kParamMode)->Int() == MultiChorus::kVibrato;
  ui->ForControlWithParam(kParamRate, [&](IControl* c) { c->SetDisabled(synced); });
  ui->ForControlWithParam(kParamNote, [&](IControl* c) { c->SetDisabled(!synced); });
  ui->ForControlWithParam(kParamMix, [&](IControl* c) { c->SetDisabled(vibrato); }); // vibrato is voices only
#endif
}

// Sets every parameter to the template's values, telling the host (so it's undoable and
// automation sees it).
void UnderheardChorus::ApplyTemplate(int index)
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
void UnderheardChorus::OnParamChangeUI(int paramIdx, EParamSource)
{
  if (paramIdx == kParamSync || paramIdx == kParamMode)
    UpdateEnabled();
}

void UnderheardChorus::OnUIOpen()
{
  Plugin::OnUIOpen();
  UpdateEnabled();
}
#endif

void UnderheardChorus::UpdateStatus()
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  const double hz = mShownRate.load();
  WDL_String s;
  if (GetParam(kParamSync)->Int() == 1)
    s.SetFormatted(160, "%s at %.1f BPM: %.2f Hz%s", underheard::fx::NoteValues()[(size_t)GetParam(kParamNote)->Int()].name, mShownTempo.load(), hz,
                   mShownLocked.load() ? ", locked to the song" : "");
  else
    s.SetFormatted(160, "%.2f Hz", hz);
  if (auto* c = ui->GetControlWithTag(kCtrlTagStatus))
    if (std::string(c->As<ITextControl>()->GetStr()) != s.Get())
      c->As<ITextControl>()->SetStr(s.Get());
#endif
}

void UnderheardChorus::OnIdle()
{
  mInputPeakSender.TransmitData(*this);
  mOutputPeakSender.TransmitData(*this);
  UpdateStatus();
}

bool UnderheardChorus::SerializeState(IByteChunk& chunk) const
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

int UnderheardChorus::UnserializeState(const IByteChunk& chunk, int startPos)
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
  // Before version 2, Warmth ran half as strong: the old full is the new 50%.
  if (version < 2 && n > kParamWarmth)
    GetParam(kParamWarmth)->Set(GetParam(kParamWarmth)->Value() / 2.);
  OnParamReset(kPresetRecall);
  LEAVE_PARAMS_MUTEX
  return p;
}

// ---- Audio thread -----------------------------------------------------------------------

#if IPLUG_DSP
void UnderheardChorus::OnReset()
{
  const double fs = GetSampleRate();
  mInputPeakSender.Reset(fs);
  mOutputPeakSender.Reset(fs);
  mChorus.Prepare(fs);
  mChorus.Reset();
}

void UnderheardChorus::GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const
{
  if (direction == ERoute::kInput)
    str.Set(busIdx == 0 ? "Main Input" : "SideChain");
  else
    str.Set("Output");
}

void UnderheardChorus::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const double gain = GetParam(kParamOutput)->DBToAmp();
  const int nOut = NOutChansConnected();
  // Channels are laid out per bus at each bus's maximum width: main is 0-1, sidechain 2-3.
  const bool stereoIn = IsChannelConnected(ERoute::kInput, 1);
  const sample* mainIn[2] = {inputs[0], stereoIn ? inputs[1] : inputs[0]};

  const double tempo = GetTempo() > 0. ? GetTempo() : 120.;
  const bool synced = GetParam(kParamSync)->Int() == 1;
  const int note = GetParam(kParamNote)->Int();
  const double rate = synced ? 1. / underheard::fx::NoteSeconds(note, tempo) : GetParam(kParamRate)->Value();
  mChorus.SetRate(rate);
  // Synced and playing: the LFO's position follows the song, so the motion lands the same way
  // on every pass.
  const bool locked = synced && GetTransportIsRunning();
  if (locked)
    mChorus.SyncPhase(GetPPQPos() / underheard::fx::NoteValues()[(size_t)note].beats);
  mChorus.SetMode(GetParam(kParamMode)->Int());
  mChorus.SetVoices(GetParam(kParamVoices)->Int() + 1);
  mChorus.SetDepth(GetParam(kParamDepth)->Value() / 100.);
  mChorus.SetDelay(GetParam(kParamDelay)->Value());
  mChorus.SetShape(GetParam(kParamShape)->Int());
  mChorus.SetFeedback(GetParam(kParamFeedback)->Value() / 100.);
  mChorus.SetPhase(GetParam(kParamPhase)->Value());
  mChorus.SetSpread(GetParam(kParamSpread)->Value() / 100.);
  mChorus.SetTone(GetParam(kParamTone)->Value());
  mChorus.SetAge(GetParam(kParamAge)->Value() / 100.);
  mChorus.SetMix(GetParam(kParamMix)->Value() / 100.);
  mChorus.SetWarmth(GetParam(kParamWarmth)->Value() / 100.);

  for (int i = 0; i < nFrames; i++)
  {
    float l, r;
    mChorus.Process((float)mainIn[0][i], (float)mainIn[1][i], l, r);
    const double o[2] = {l * gain, r * gain};
    for (int c = 0; c < nOut; c++)
      outputs[c][i] = o[std::min(c, 1)];
  }
  mShownRate.store(rate);
  mShownTempo.store(tempo);
  mShownLocked.store(locked);

  mInputPeakSender.ProcessBlock(const_cast<sample**>(mainIn), nFrames, kCtrlTagInputMeter, 2, 0);
  mOutputPeakSender.ProcessBlock(outputs, nFrames, kCtrlTagOutputMeter, nOut, 0);
}
#endif // IPLUG_DSP
