#include "UnderheardReverb.h"
#include "IPlug_include_in_plug_src.h"

#include "AudioFile.h"
#include "MicModel.h"
#include "NoteValues.h"
#include "ReverbIR.h"
#include "RoomModel.h"
#if IPLUG_EDITOR
#include "UnderheardUI.h"
using namespace underheard_ui;
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>

using underheard::fx::ReverbCore;
using underheard::room::Convolver;
using underheard::room::Impulse;

namespace {

constexpr int32_t kStateMagic = 'URVB';
constexpr int32_t kStateVersion = 2; // 2: Warmth's 50% is version 1's 100%
constexpr double kPlateDecay = 2.0, kHallDecay = 3.0; // seconds at Decay x1
constexpr double kMaxRoomSeconds = 20.;

// Interleaves an impulse as stereo (a mono impulse on both sides), for saving and resampling.
std::vector<float> Interleave(const Impulse& imp)
{
  const int64_t n = imp.Frames();
  std::vector<float> st((size_t)(2 * n));
  for (int64_t i = 0; i < n; i++)
  {
    st[(size_t)(2 * i)] = imp.channels[0][(size_t)i];
    st[(size_t)(2 * i + 1)] = imp.channels[imp.channels.size() > 1 ? 1 : 0][(size_t)i];
  }
  return st;
}

// Starting points: each changes these parameters from their defaults (the rest stay at the
// defaults). Engines: 0 Rooms, 1 Recordings, 2 Plate, 3 Hall. Rooms: 1 Bathroom, 4 Living
// room, 8 Church hall.
struct Template
{
  const char* name;
  std::vector<std::pair<int, double>> set;
};
const std::vector<Template>& Templates()
{
  static const std::vector<Template> t = {
    {"Hall", {}},
    {"Large hall", {{kParamDecay, 1.8}, {kParamSize, 50.}, {kParamPreDelay, 35.}, {kParamModulation, 40.}, {kParamHighCut, 9000.}, {kParamWidth, 120.}}},
    {"Bright plate", {{kParamEngine, 2.}, {kParamDecay, 0.9}, {kParamPreDelay, 10.}, {kParamLowCut, 150.}, {kParamHighCut, 14000.}, {kParamAge, 0.},
                      {kParamModulation, 25.}}},
    {"Dark plate", {{kParamEngine, 2.}, {kParamDecay, 1.3}, {kParamPreDelay, 20.}, {kParamLowCut, 120.}, {kParamHighCut, 5000.}, {kParamAge, 50.}}},
    {"Living room", {{kParamEngine, 0.}, {kParamRoom, 4.}, {kParamDistance, 55.}, {kParamMix, 25.}, {kParamPreDelay, 0.}}},
    {"Tiled bathroom", {{kParamEngine, 0.}, {kParamRoom, 1.}, {kParamSurfaces, 60.}, {kParamDistance, 40.}, {kParamPreDelay, 0.}}},
    {"Church hall", {{kParamEngine, 0.}, {kParamRoom, 8.}, {kParamDecay, 1.2}, {kParamDistance, 75.}, {kParamBalance, 30.}, {kParamPreDelay, 15.},
                     {kParamMix, 35.}}},
    {"Frozen pad", {{kParamDecay, 4.}, {kParamSize, 100.}, {kParamModulation, 70.}, {kParamAge, 40.}, {kParamPreSync, 1.}, {kParamPreNote, 6.},
                    {kParamMix, 50.}, {kParamDuck, 30.}, {kParamWidth, 140.}}},
  };
  return t;
}

// The selected preset's shape with Size and Surfaces applied.
underheard::room::RoomShape CurrentShape(const IParam* room, const IParam* size, const IParam* surfaces)
{
  const auto& presets = underheard::room::RoomPresets();
  return underheard::room::MakeShape(presets[(size_t)std::clamp(room->Int(), 0, (int)presets.size() - 1)], size->Value() / 100., surfaces->Value() / 100.);
}

} // namespace

UnderheardReverb::UnderheardReverb(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
, mTapeDir(underheard::TapeDirectory("UnderheardReverb"))
{
  GetParam(kParamOutput)->InitGain("Output", 0., -70., 12.);
  GetParam(kParamMix)->InitPercentage("Mix (dry-reverb)", 30.);
  GetParam(kParamEngine)->InitEnum("Engine", ReverbCore::kHall, {"Rooms", "Recordings", "Plate", "Hall"});
  GetParam(kParamPreSync)->InitEnum("Pre-delay Sync", 0, {"Free", "Sync"});
  GetParam(kParamPreDelay)->InitDouble("Pre-delay", 20., 0., 500., 0.1, "ms", 0, "", IParam::ShapePowCurve(2.));
  {
    IParam* note = GetParam(kParamPreNote);
    note->InitEnum("Pre-delay Note", 3, 12); // 1/16; up to a quarter note
    for (int i = 0; i < 12; i++)
      note->SetDisplayText(i, underheard::fx::NoteValues()[(size_t)i].name);
  }
  GetParam(kParamDecay)->InitDouble("Decay", 1., 0.25, 4., 0.01, "", 0, "", IParam::ShapeExp(), IParam::kUnitCustom,
                                    [](double v, WDL_String& s) { s.SetFormatted(32, "x%.2f", v); });
  GetParam(kParamBalance)->InitDouble("Early/Late", 0., -100., 100., 1., "", 0, "", IParam::ShapeLinear(), IParam::kUnitCustom,
    [](double v, WDL_String& s) { s.SetFormatted(32, v < -99. ? "early only" : v > 99. ? "late only" : v < -2. ? "less late %.0f%%" : v > 2. ? "less early %.0f%%" : "both", std::fabs(v)); });
  GetParam(kParamLowCut)->InitFrequency("Low Cut", 80., 20., 1000.);
  GetParam(kParamHighCut)->InitFrequency("High Cut", 12000., 1000., 20000.);
  GetParam(kParamWidth)->InitDouble("Width", 100., 0., 150., 1., "%");
  GetParam(kParamModulation)->InitPercentage("Modulation", 30.);
  GetParam(kParamAge)->InitPercentage("Age", 20.);
  GetParam(kParamFreeze)->InitBool("Freeze", false);
  GetParam(kParamDuck)->InitPercentage("Duck", 0.);
  {
    IParam* room = GetParam(kParamRoom);
    const auto& presets = underheard::room::RoomPresets();
    room->InitEnum("Room", 4, (int)presets.size()); // Living room
    for (int i = 0; i < (int)presets.size(); i++)
      room->SetDisplayText(i, presets[(size_t)i].name);
  }
  GetParam(kParamSize)->InitDouble("Size", 0., -100., 100., 1., "%");
  GetParam(kParamSurfaces)->InitDouble("Surfaces", 0., -100., 100., 1., "", 0, "", IParam::ShapeLinear(), IParam::kUnitCustom,
    [](double v, WDL_String& s) { s.SetFormatted(32, v < -2. ? "softer %.0f%%" : v > 2. ? "harder %.0f%%" : "as built", std::fabs(v)); });
  GetParam(kParamDistance)->InitDouble("Distance", 55., 0., 100., 0.1, "", 0, "", IParam::ShapeLinear(), IParam::kUnitCustom,
    [this](double, WDL_String& s) { s.SetFormatted(32, "%.2f m", DistanceMetres()); });
  GetParam(kParamAim)->InitDouble("Aim", 0., 0., 360., 1., "deg");
  GetParam(kParamPattern)->InitEnum("Pattern", 0, {"Omni", "Cardioid", "Hypercardioid", "Figure-8"});
  GetParam(kParamStereo)->InitEnum("Mics", 1, {"Mono", "Stereo"});
  GetParam(kParamPair)->InitEnum("Pair", 2, {"XY", "ORTF", "Spaced"});
  {
    IParam* mic = GetParam(kParamMic);
    const auto& mics = underheard::room::MicModels();
    mic->InitEnum("Mic", underheard::room::IdealMicIndex(), (int)mics.size());
    for (int i = 0; i < (int)mics.size(); i++)
      mic->SetDisplayText(i, mics[(size_t)i].name);
  }
  GetParam(kParamSourceX)->InitPercentage("Source X", 30.);
  GetParam(kParamSourceY)->InitPercentage("Source Y", 25.);
  GetParam(kParamBearing)->InitDouble("Mic Bearing", 53., 0., 360., 1., "deg");
  GetParam(kParamWarmth)->InitPercentage("Warmth", 50.);

  // The templates, as the host's factory presets (in the state format, with no recording).
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
    const int64_t frames = 0;
    const uint64_t hash = 0;
    const int32_t channels = 0;
    const double rate = 0.;
    chunk.Put(&frames);
    chunk.Put(&hash);
    chunk.PutStr("");
    chunk.Put(&channels);
    chunk.Put(&rate);
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
    g->AttachControl(new ITextControl(title.ReduceFromLeft(260.f), "UNDERHEARD REVERB", IText(22.f, kText, "Roboto-Regular", EAlign::Near)));
    {
      std::vector<std::string> names;
      for (const Template& t : Templates())
        names.push_back(t.name);
      g->AttachControl(new MenuButton(title.ReduceFromRight(150.f).GetPadded(-2.f), "TEMPLATES", style, names, [this](int i) { ApplyTemplate(i); }));
      title.ReduceFromRight(12.f);
    }
    g->AttachControl(new ITextControl(title, "", IText(15.f, kText, "Roboto-Regular", EAlign::Near)), kCtrlTagInfo);
    g->AttachControl(new ITextControl(b.ReduceFromTop(24.f), "", IText(14.f, kAccent, "Roboto-Regular", EAlign::Near)), kCtrlTagStatus);
    b.ReduceFromTop(8.f);

    // The room, seen from above, on the right (Rooms only), and the meters under it.
    IRECT right = b.ReduceFromRight(410.f);
    b.ReduceFromRight(12.f);
    IRECT meters = right.ReduceFromBottom(120.f);
    right.ReduceFromBottom(8.f);
    g->AttachControl(new IPanelControl(right, IColor(255, 30, 28, 25)), kCtrlTagViewPanel);
    g->AttachControl(new roombleed_ui::RoomView(right.GetPadded(-8.f), {kParamDistance, kParamAim, kParamSourceX, kParamSourceY, kParamBearing},
                                                [this] { return MakeRoomView(); }), kCtrlTagRoomView);
    g->AttachControl(new ITextControl(right.GetPadded(-24.f), "", IText(15.f, kTextDim, "Roboto-Regular", EAlign::Center)), kCtrlTagEngineNote);
    g->AttachControl(new IVPeakAvgMeterControl<2>(meters.FracRectVertical(0.5f, true), "In", style, EDirection::Horizontal, {"L", "R"}), kCtrlTagInputMeter);
    g->AttachControl(new IVPeakAvgMeterControl<2>(meters.FracRectVertical(0.5f), "Out", style, EDirection::Horizontal, {"L", "R"}), kCtrlTagOutputMeter);

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

    IRECT r = row("ENGINE", 56.f);
    g->AttachControl(new IVTabSwitchControl(cell(r, 0, 4).GetMidVPadded(20.f), kParamEngine, {"Rooms", "Recordings", "Plate", "Hall"}, "", style));
    g->AttachControl(new IVButtonControl(cell(r, 4, 2).GetMidVPadded(20.f), [this](IControl* c) {
      SplashClickActionFunc(c);
      LoadIrDialog();
    }, "LOAD IR", style));

    r = row("SPACE", 110.f);
    g->AttachControl(new IVMenuButtonControl(cell(r, 0, 2).GetMidVPadded(20.f), kParamRoom, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 2), kParamSize, "Size", style));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamSurfaces, "Surfaces", style));
    g->AttachControl(new IVKnobControl(cell(r, 4), kParamDecay, "Decay", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamBalance, "Early/Late", style));

    r = row("MIC", 56.f);
    g->AttachControl(new IVMenuButtonControl(cell(r, 0, 2).GetMidVPadded(20.f), kParamMic, "", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 2, 4).GetMidVPadded(20.f), kParamPattern, {"Omni", "Cardioid", "Hyper", "Figure-8"}, "", style));

    r = row("PLACE", 110.f);
    g->AttachControl(new IVKnobControl(cell(r, 0), kParamDistance, "Distance", style));
    g->AttachControl(new IVKnobControl(cell(r, 1), kParamAim, "Aim", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 2).GetMidVPadded(20.f), kParamStereo, {"Mono", "Stereo"}, "", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 3, 2).GetMidVPadded(20.f), kParamPair, {"XY", "ORTF", "Spaced"}, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamWarmth, "Warmth", style));

    r = row("TIME", 110.f);
    g->AttachControl(new IVTabSwitchControl(cell(r, 0).GetMidVPadded(20.f), kParamPreSync, {"Free", "Sync"}, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 1), kParamPreDelay, "Pre-delay", style));
    g->AttachControl(new IVMenuButtonControl(cell(r, 2).GetMidVPadded(20.f), kParamPreNote, "", style));
    g->AttachControl(new IVToggleControl(cell(r, 3).GetMidVPadded(24.f), kParamFreeze, "", style, "FREEZE", "FROZEN"));
    g->AttachControl(new IVKnobControl(cell(r, 4), kParamDuck, "Duck", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamModulation, "Modulation", style));

    r = row("TONE", 110.f);
    g->AttachControl(new IVKnobControl(cell(r, 0), kParamLowCut, "Low Cut", style));
    g->AttachControl(new IVKnobControl(cell(r, 1), kParamHighCut, "High Cut", style));
    g->AttachControl(new IVKnobControl(cell(r, 2), kParamWidth, "Width", style));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamAge, "Age", style));
    g->AttachControl(new IVKnobControl(cell(r, 4), kParamMix, "Dry / Reverb", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamOutput, "Output", style));
    UpdateEnabled();
  };
#endif
}

// ---- Main thread ------------------------------------------------------------------------

void UnderheardReverb::SetMessage(const std::string& msg, bool sticky)
{
  mMessage = msg;
  mMessageSticky = sticky;
  mMessageAt = Clock::now();
}

void UnderheardReverb::SetParamFromPlugin(int paramIdx, double value)
{
  const double norm = GetParam(paramIdx)->ToNormalized(value);
  BeginInformHostOfParamChangeFromUI(paramIdx);
  SendParameterValueFromUI(paramIdx, norm);
  EndInformHostOfParamChangeFromUI(paramIdx);
#if IPLUG_EDITOR
  if (GetUI())
    GetUI()->ForControlWithParam(paramIdx, [norm](IControl* pControl) { pControl->SetValueFromDelegate(norm); });
#endif
}

// Sets every parameter to the template's values, telling the host (so it's undoable and
// automation sees it). A loaded recording stays loaded.
void UnderheardReverb::ApplyTemplate(int index)
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
    SetParamFromPlugin(i, v[(size_t)i]);
  UpdateEnabled();
}

void UnderheardReverb::LoadIrDialog()
{
#if IPLUG_EDITOR
  if (!GetUI())
    return;
  mDialogFile.Set("");
  GetUI()->PromptForFile(mDialogFile, mDialogPath, EFileAction::Open, "wav", [this](const WDL_String& file, const WDL_String&) {
    if (file.GetLength())
      LoadIrFile(file.Get());
  });
#endif
}

bool UnderheardReverb::LoadIrFile(const std::string& path)
{
  underheard::AudioData a;
  std::string error;
  if (!underheard::ReadWav(path, a, error))
  {
    SetMessage("can't load: " + error, true);
    return false;
  }
  Impulse imp = underheard::room::PrepareImpulse(a.stereo, a.sampleRate, 10.);
  if (imp.Frames() < 16)
  {
    SetMessage("can't load: no sound in that file", true);
    return false;
  }
  const std::vector<float> st = Interleave(imp);
  underheard::SavedTape saved;
  if (!underheard::SaveTape(mTapeDir, st.data(), imp.Frames(), imp.sampleRate, saved, error))
  {
    SetMessage("couldn't save the recording: " + error, true);
    return false;
  }
  mIrSaved = saved;
  mImpulse = std::move(imp);
  mIrChannels = (int)mImpulse.channels.size();
  mIrRate = mImpulse.sampleRate;
  mIrName = std::filesystem::u8path(path).filename().u8string();
  SetParamFromPlugin(kParamEngine, ReverbCore::kRecordings);
  mRebuildPending = true;
  RebuildConvolver();
  SetMessage("loaded");
  return true;
}

double UnderheardReverb::DistanceMetres() const
{
  const double maxD = underheard::room::MaxDistance(CurrentShape(GetParam(kParamRoom), GetParam(kParamSize), GetParam(kParamSurfaces)),
                                                    GetParam(kParamSourceX)->Value() / 100., GetParam(kParamSourceY)->Value() / 100., GetParam(kParamBearing)->Value());
  const double t = GetParam(kParamDistance)->Value() / 100.;
  return 0.1 * std::pow(std::max(maxD, 0.11) / 0.1, t);
}

double UnderheardReverb::AlgoDecaySeconds() const
{
  return (GetParam(kParamEngine)->Int() == ReverbCore::kPlate ? kPlateDecay : kHallDecay) * GetParam(kParamDecay)->Value();
}

bool UnderheardReverb::ConvolverSettingsChanged()
{
  std::vector<double> now = {GetSampleRate(), (double)mImpulse.Frames()};
  for (int p : {kParamEngine, kParamDecay, kParamBalance, kParamRoom, kParamSize, kParamSurfaces, kParamDistance, kParamAim, kParamPattern, kParamStereo, kParamPair,
                kParamMic, kParamSourceX, kParamSourceY, kParamBearing})
    now.push_back(GetParam(p)->Value());
  if (now == mBuiltSettings)
    return false;
  mBuiltSettings = now;
  return true;
}

// Builds the impulse for Rooms or Recordings and hands a new convolver to the audio thread.
void UnderheardReverb::RebuildConvolver()
{
  using namespace underheard::room;
  ConvolverSettingsChanged(); // record what this build is for
  mRebuildPending = false;
  const int engine = GetParam(kParamEngine)->Int();
  if (!ReverbCore::IsConvolution(engine))
    return;
  const double fs = GetSampleRate() > 0. ? GetSampleRate() : 48000.;
  const double decay = GetParam(kParamDecay)->Value(), balance = GetParam(kParamBalance)->Value() / 100.;
  Channels irs;
  char info[224];

  if (engine == ReverbCore::kRecordings)
  {
    if (mImpulse.Frames() == 0)
    {
      mInfo = "no recording loaded (LOAD IR)";
      // Silence, rather than whatever the convolver last held (a room, say).
      if (auto c = Convolver::Make(Channels{std::vector<float>(1, 0.f)}, fs))
        mSwitch.Offer(std::move(c));
      return;
    }
    irs = mImpulse.channels;
    if (std::fabs(mImpulse.sampleRate - fs) > 0.5)
    {
      const std::vector<float> st = underheard::ResampleStereo(Interleave(mImpulse).data(), mImpulse.Frames(), fs / mImpulse.sampleRate);
      for (size_t ch = 0; ch < irs.size(); ch++)
      {
        irs[ch].resize(st.size() / 2);
        for (size_t i = 0; i < irs[ch].size(); i++)
          irs[ch][i] = st[2 * i + ch];
      }
    }
    RemoveDirect(irs, fs);
    const double mixing = 0.08;
    const double before = MeasureDecay(irs, fs, Onset(irs));
    StretchDecay(irs, fs, decay, mixing);
    Balance(irs, fs, balance, (double)Onset(irs) / fs + mixing);
    std::snprintf(info, sizeof info, "%s   %s, decay %.2f s%s", mIrName.c_str(), mIrChannels > 1 ? "stereo" : "mono", before * decay,
                  decay > 1.01 ? " (a recording stretches only so far: its end fades)" : "");
  }
  else
  {
    RoomSpec spec;
    spec.shape = CurrentShape(GetParam(kParamRoom), GetParam(kParamSize), GetParam(kParamSurfaces));
    const double distance = DistanceMetres();
    const bool stereo = GetParam(kParamStereo)->Int() == 1;
    const MicModel& mic = MicModels()[(size_t)GetParam(kParamMic)->Int()];
    const MicVariant& variant = mic.Variant((MicPattern)GetParam(kParamPattern)->Int());
    const Placement p = PlaceMics(spec.shape, GetParam(kParamSourceX)->Value() / 100., GetParam(kParamSourceY)->Value() / 100., GetParam(kParamBearing)->Value(),
                                  distance, GetParam(kParamAim)->Value(), stereo, GetParam(kParamPair)->Int());
    spec.source = p.source;
    spec.mics = p.mics;
    spec.tailCorrelation = p.tailCorrelation;
    spec.pickup.a = variant.a;
    spec.pickup.proximity = mic.proximity;
    spec.pickup.roomGain = mic.kind == MicKind::Contact ? 0.06 : 1.;
    spec.sampleRate = fs;
    spec.maxSeconds = kMaxRoomSeconds;
    spec.decayScale = decay;
    spec.direct = false; // a reverb: the dry signal is the direct sound
    RoomIR r = GenerateRoom(spec);
    irs = std::move(r.channels);
    // A huge, long room can outlast the limit: fade it out rather than cut it off.
    if (!irs.empty() && (double)irs[0].size() >= (kMaxRoomSeconds - 0.01) * fs)
      FadeEnd(irs, 0.3);
    Balance(irs, fs, balance, r.mixingTime);
    const std::vector<float> micFilter = MicFilter(variant.response, fs);
    for (auto& ir : irs)
      ApplyFilter(ir, micFilter);
    std::snprintf(info, sizeof info, "%s   %.1f x %.1f x %.1f m   decay %.2f s   %s at %.2f m", RoomPresets()[(size_t)GetParam(kParamRoom)->Int()].name,
                  spec.shape.width, spec.shape.depth, spec.shape.height, r.rt60[3], mic.name, distance);
  }
  mInfo = info;
  NormaliseEnergy(irs);
  if (auto c = Convolver::Make(irs, fs))
    mSwitch.Offer(std::move(c)); // an unclaimed earlier offer comes back and is freed here
}

#if IPLUG_EDITOR
// What the room drawing shows: the same placement the room model uses.
roombleed_ui::RoomViewData UnderheardReverb::MakeRoomView() const
{
  using namespace underheard::room;
  roombleed_ui::RoomViewData d;
  const RoomShape shape = CurrentShape(GetParam(kParamRoom), GetParam(kParamSize), GetParam(kParamSurfaces));
  const bool stereo = GetParam(kParamStereo)->Int() == 1;
  const Placement p = PlaceMics(shape, GetParam(kParamSourceX)->Value() / 100., GetParam(kParamSourceY)->Value() / 100., GetParam(kParamBearing)->Value(),
                                DistanceMetres(), GetParam(kParamAim)->Value(), stereo, GetParam(kParamPair)->Int());
  d.width = shape.width;
  d.depth = shape.depth;
  d.sx = p.source.x;
  d.sy = p.source.y;
  d.distance = DistanceMetres();
  d.maxDistance = p.maxDistance;
  const double bearing = GetParam(kParamBearing)->Value() * 3.14159265358979 / 180.;
  d.dirx = std::cos(bearing);
  d.diry = std::sin(bearing);
  const double aim = GetParam(kParamAim)->Value() * 3.14159265358979 / 180.;
  d.cax = -d.dirx * std::cos(aim) + d.diry * std::sin(aim);
  d.cay = -d.dirx * std::sin(aim) - d.diry * std::cos(aim);
  d.numMics = (int)p.mics.size();
  for (int m = 0; m < d.numMics && m < 2; m++)
    d.mics[m] = {p.mics[(size_t)m].position.x, p.mics[(size_t)m].position.y, p.mics[(size_t)m].aim.x, p.mics[(size_t)m].aim.y};
  const MicModel& mic = MicModels()[(size_t)GetParam(kParamMic)->Int()];
  d.pattern = mic.Variant((MicPattern)GetParam(kParamPattern)->Int()).a[3];
  for (int w = 0; w < 4; w++)
  {
    double mean = 0.;
    for (double a : shape.surfaces[(size_t)w]->alpha)
      mean += a * shape.surfaceScale / kBands;
    d.hardness[w] = 1. - std::clamp(mean * 3., 0., 1.);
  }
  char title[128];
  std::snprintf(title, sizeof title, "%s   %.1f x %.1f m   drag the source, the mic, or the dot to aim", RoomPresets()[(size_t)GetParam(kParamRoom)->Int()].name,
                shape.width, shape.depth);
  d.title = title;
  return d;
}
#endif

void UnderheardReverb::UpdateEnabled()
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  const int engine = GetParam(kParamEngine)->Int();
  const bool rooms = engine == ReverbCore::kRooms, algo = !ReverbCore::IsConvolution(engine);
  const bool synced = GetParam(kParamPreSync)->Int() == 1;
  const bool multi = underheard::room::MicModels()[(size_t)GetParam(kParamMic)->Int()].MultiPattern();
  auto enable = [ui](int param, bool on) {
    ui->ForControlWithParam(param, [on](IControl* c) {
      if (c->IsDisabled() == on)
        c->SetDisabled(!on);
    });
  };
  for (int p : {kParamRoom, kParamSurfaces, kParamMic, kParamDistance, kParamAim, kParamStereo})
    enable(p, rooms);
  enable(kParamPattern, rooms && multi);
  enable(kParamPair, rooms && GetParam(kParamStereo)->Int() == 1);
  enable(kParamSize, rooms || algo);
  enable(kParamModulation, algo);
  enable(kParamPreDelay, !synced);
  enable(kParamPreNote, synced);
  if (IControl* view = ui->GetControlWithTag(kCtrlTagRoomView))
    if (view->IsHidden() == rooms)
      view->Hide(!rooms);
  if (auto* note = static_cast<ITextControl*>(ui->GetControlWithTag(kCtrlTagEngineNote)))
  {
    const char* text = engine == ReverbCore::kRecordings ? "Recordings: your own impulse responses.\nLOAD IR to choose a WAV."
                       : engine == ReverbCore::kPlate    ? "Plate: dense and smooth, bright to dark with Age."
                       : engine == ReverbCore::kHall     ? "Hall: wide and slow to build, with moving tails."
                                                         : "";
    if (strcmp(note->GetStr(), text) != 0)
      note->SetStr(text);
  }
#endif
}

#if IPLUG_EDITOR
void UnderheardReverb::OnParamChangeUI(int paramIdx, EParamSource)
{
  if (paramIdx == kParamEngine || paramIdx == kParamPreSync || paramIdx == kParamMic || paramIdx == kParamStereo)
    UpdateEnabled();
}

void UnderheardReverb::OnUIOpen()
{
  Plugin::OnUIOpen();
  UpdateEnabled();
}
#endif

void UnderheardReverb::UpdateStatus()
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  if (!mMessage.empty() && !mMessageSticky && Clock::now() - mMessageAt > std::chrono::seconds(8))
    mMessage.clear();
  auto setText = [ui](int tag, const char* text) {
    if (auto* c = static_cast<ITextControl*>(ui->GetControlWithTag(tag)))
      if (strcmp(c->GetStr(), text) != 0)
        c->SetStr(text);
  };
  const int engine = GetParam(kParamEngine)->Int();
  std::string info = mInfo;
  if (!ReverbCore::IsConvolution(engine))
  {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s   decay %.1f s", engine == ReverbCore::kPlate ? "Plate" : "Hall", AlgoDecaySeconds());
    info = buf;
  }
  if (GetParam(kParamFreeze)->Bool())
    info += "   FROZEN";
  setText(kCtrlTagInfo, info.c_str());
  setText(kCtrlTagStatus, mMessage.c_str());
  if (IControl* view = ui->GetControlWithTag(kCtrlTagRoomView))
    if (!view->IsHidden())
      view->SetDirty(false);
#endif
}

void UnderheardReverb::OnIdle()
{
  mInputPeakSender.TransmitData(*this);
  mOutputPeakSender.TransmitData(*this);
  mSwitch.TakeRetired(); // frees a convolver the audio thread has finished with
  if (ConvolverSettingsChanged())
  {
    mSettingsChangedAt = Clock::now();
    mRebuildPending = true;
  }
  if (mRebuildPending && Clock::now() - mSettingsChangedAt > std::chrono::milliseconds(60))
    RebuildConvolver();
  UpdateStatus();
}

// ---- State -----------------------------------------------------------------------------

// Format 1: magic, version, parameter count, the parameters, then the loaded recording (frames,
// hash, path, channels, sample rate; frames == 0 for none).
bool UnderheardReverb::SerializeState(IByteChunk& chunk) const
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
  const int64_t frames = mIrSaved.frames;
  const uint64_t hash = mIrSaved.hash;
  const int32_t channels = mIrChannels;
  const double rate = mIrRate;
  chunk.Put(&frames);
  chunk.Put(&hash);
  chunk.PutStr(mIrSaved.path.c_str());
  chunk.Put(&channels);
  chunk.Put(&rate);
  return true;
}

int UnderheardReverb::UnserializeState(const IByteChunk& chunk, int startPos)
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
  if (p < 0)
    return p;

  underheard::SavedTape saved;
  WDL_String path;
  int32_t channels = 0;
  double rate = 0.;
  p = chunk.Get(&saved.frames, p);
  if (p >= 0) p = chunk.Get(&saved.hash, p);
  if (p >= 0) p = chunk.GetStr(path, p);
  if (p >= 0) p = chunk.Get(&channels, p);
  if (p >= 0) p = chunk.Get(&rate, p);
  if (p < 0)
  {
    SetMessage("couldn't read the recording from the project", true);
    return p;
  }
  saved.path = path.Get();
  if (saved.frames > 0) // a template (no recording) keeps whatever is loaded
  {
    std::vector<float> st;
    std::string error;
    if (underheard::LoadSavedTape(saved, mTapeDir, st, error))
    {
      Impulse imp;
      imp.sampleRate = rate > 0. ? rate : 48000.;
      imp.channels.assign((size_t)std::clamp(channels, 1, 2), std::vector<float>((size_t)saved.frames));
      for (size_t ch = 0; ch < imp.channels.size(); ch++)
        for (int64_t i = 0; i < saved.frames; i++)
          imp.channels[ch][(size_t)i] = st[(size_t)(2 * i) + ch];
      mIrSaved = saved;
      mImpulse = std::move(imp);
      mIrChannels = (int)mImpulse.channels.size();
      mIrRate = mImpulse.sampleRate;
      mIrName = std::filesystem::u8path(saved.path).filename().u8string();
      SetMessage("recording restored from the project", true);
    }
    else
      SetMessage("recording not restored: " + error, true);
  }
  mRebuildPending = true;
  return p;
}

// ---- Audio thread ----------------------------------------------------------------------

#if IPLUG_DSP
void UnderheardReverb::OnReset()
{
  const double fs = GetSampleRate();
  mInputPeakSender.Reset(fs);
  mOutputPeakSender.Reset(fs);
  const int block = std::max(GetBlockSize(), 64);
  for (auto* v : {&mInL, &mInR, &mOutL, &mOutR})
    v->assign((size_t)block, 0.f);
  mCore.Prepare(fs, block);
  mCore.Reset();
  mRebuildPending = true; // the impulse has to match the host's sample rate (rebuilt in OnIdle)
}

void UnderheardReverb::GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const
{
  if (direction == ERoute::kInput)
    str.Set(busIdx == 0 ? "Main Input" : "SideChain");
  else
    str.Set("Output");
}

void UnderheardReverb::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const double gain = GetParam(kParamOutput)->DBToAmp();
  const int nOut = NOutChansConnected();
  // Channels are laid out per bus at each bus's maximum width: main is 0-1, sidechain 2-3.
  const bool stereoIn = IsChannelConnected(ERoute::kInput, 1);
  const sample* mainIn[2] = {inputs[0], stereoIn ? inputs[1] : inputs[0]};

  const int engine = GetParam(kParamEngine)->Int();
  const double tempo = GetTempo() > 0. ? GetTempo() : 120.;
  mCore.SetEngine(engine);
  mCore.SetPreDelay(GetParam(kParamPreSync)->Int() == 1 ? underheard::fx::NoteSeconds(GetParam(kParamPreNote)->Int(), tempo)
                                                        : GetParam(kParamPreDelay)->Value() / 1000.);
  mCore.SetDecay(AlgoDecaySeconds());
  mCore.SetSize(std::pow(2., GetParam(kParamSize)->Value() / 100.));
  mCore.SetBalance(GetParam(kParamBalance)->Value() / 100.);
  mCore.SetModulation(GetParam(kParamModulation)->Value() / 100.);
  mCore.SetLowCut(GetParam(kParamLowCut)->Value());
  mCore.SetHighCut(GetParam(kParamHighCut)->Value());
  mCore.SetAge(GetParam(kParamAge)->Value() / 100.);
  mCore.SetWidth(GetParam(kParamWidth)->Value() / 100.);
  mCore.SetFreeze(GetParam(kParamFreeze)->Bool());
  mCore.SetDuck(GetParam(kParamDuck)->Value() / 100.);
  mCore.SetMix(GetParam(kParamMix)->Value() / 100.);
  mCore.SetWarmth(GetParam(kParamWarmth)->Value() / 100.);

  for (int start = 0; start < nFrames;)
  {
    const int n = std::min(nFrames - start, (int)mInL.size());
    for (int i = 0; i < n; i++)
    {
      mInL[(size_t)i] = (float)mainIn[0][start + i];
      mInR[(size_t)i] = (float)mainIn[1][start + i];
    }
    mCore.Process(mInL.data(), mInR.data(), mOutL.data(), mOutR.data(), n,
                  [this](const float* mono, float* l, float* r, int m) { mSwitch.Process(mono, l, r, m); });
    for (int i = 0; i < n; i++)
    {
      const double o[2] = {mOutL[(size_t)i] * gain, mOutR[(size_t)i] * gain};
      for (int c = 0; c < nOut; c++)
        outputs[c][start + i] = o[std::min(c, 1)];
    }
    start += n;
  }

  mInputPeakSender.ProcessBlock(const_cast<sample**>(mainIn), nFrames, kCtrlTagInputMeter, 2, 0);
  mOutputPeakSender.ProcessBlock(outputs, nFrames, kCtrlTagOutputMeter, nOut, 0);
}
#endif // IPLUG_DSP
