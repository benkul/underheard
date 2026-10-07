#include "RoomBleed.h"
#include "IPlug_include_in_plug_src.h"

#include "AudioFile.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

using underheard::room::Convolver;
using underheard::room::Impulse;

namespace {

constexpr int32_t kStateMagic = 'RMBL';
constexpr int32_t kStateVersion = 3; // 2: the loaded room tone; 3: Warmth's 50% is version 2's 100%
const IColor kBackground(255, 24, 22, 20), kText(255, 226, 216, 200), kTextDim(255, 150, 140, 128), kAccent(255, 206, 146, 72);

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

} // namespace

RoomBleed::RoomBleed(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
, mTapeDir(underheard::TapeDirectory("RoomBleed"))
{
  GetParam(kParamOutput)->InitGain("Output", 0., -70., 12.);
  GetParam(kParamMix)->InitPercentage("Mix (dry-bled)", 100.);
  {
    IParam* room = GetParam(kParamRoom);
    const auto& presets = underheard::room::RoomPresets();
    room->InitEnum("Room", 4, (int)presets.size() + 1); // Living room
    for (int i = 0; i < (int)presets.size(); i++)
      room->SetDisplayText(i, presets[(size_t)i].name);
    room->SetDisplayText((int)presets.size(), "Loaded recording");
  }
  GetParam(kParamSize)->InitDouble("Size", 0., -100., 100., 1., "%");
  GetParam(kParamSurfaces)->InitDouble("Surfaces", 0., -100., 100., 1., "", 0, "", IParam::ShapeLinear(), IParam::kUnitCustom,
    [](double v, WDL_String& s) { s.SetFormatted(32, v < -2. ? "softer %.0f%%" : v > 2. ? "harder %.0f%%" : "as built", std::fabs(v)); });
  GetParam(kParamDistance)->InitDouble("Distance", 35., 0., 100., 0.1, "", 0, "", IParam::ShapeLinear(), IParam::kUnitCustom,
    [this](double v, WDL_String& s) {
      (void)v;
      s.SetFormatted(32, "%.2f m", DistanceMetres());
    });
  GetParam(kParamAim)->InitDouble("Aim", 0., 0., 360., 1., "deg"); // 0 points at the source; turns all the way round
  GetParam(kParamPattern)->InitEnum("Pattern", kPatternCardioid, {"Omni", "Cardioid", "Hypercardioid", "Figure-8"});
  GetParam(kParamStereo)->InitEnum("Output", 1, {"Mono", "Stereo"});
  GetParam(kParamPair)->InitEnum("Pair", kPairORTF, {"XY", "ORTF", "Spaced"});
  {
    IParam* mic = GetParam(kParamMic);
    const auto& mics = underheard::room::MicModels();
    mic->InitEnum("Mic", 4, (int)mics.size()); // the U 87
    for (int i = 0; i < (int)mics.size(); i++)
      mic->SetDisplayText(i, mics[(size_t)i].name);
  }
  GetParam(kParamWhere)->InitEnum("Where", 0, {"Same room", "Next door", "Below"});
  GetParam(kParamDoor)->InitPercentage("Door", 0.);
  GetParam(kParamTone)->InitEnum("Room Tone", underheard::room::RoomTone::kApartment,
                                 {"None", "Apartment", "Kitchen", "Office", "Hallway", "Basement", "Loaded recording"});
  GetParam(kParamToneLevel)->InitDouble("Room Tone Level", -50., -80., -20., 0.5, "dBFS");
  GetParam(kParamSourceX)->InitPercentage("Source X", 30.);
  GetParam(kParamSourceY)->InitPercentage("Source Y", 25.);
  GetParam(kParamBearing)->InitDouble("Mic Bearing", 53., 0., 360., 1., "deg"); // toward the far corner
  GetParam(kParamWarmth)->InitPercentage("Warmth", 50.);
  RebuildRoom(); // at 48 kHz until the host says otherwise

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
  };

  // Stage R1 layout; the room drawing comes in R6.
  mLayoutFunc = [&](IGraphics* g) {
    g->AttachCornerResizer(EUIResizerMode::Scale, false);
    g->AttachPanelBackground(kBackground);
    g->LoadFont("Roboto-Regular", ROBOTO_FN);
    const IVStyle style = DEFAULT_STYLE.WithColor(kBG, COLOR_TRANSPARENT).WithColor(kFG, IColor(255, 70, 64, 58)).WithColor(kPR, kAccent)
                                       .WithColor(kFR, IColor(255, 110, 100, 90)).WithColor(kX1, kAccent)
                                       .WithLabelText(IText(14.f, kText, "Roboto-Regular")).WithValueText(IText(13.f, kTextDim, "Roboto-Regular"))
                                       .WithDrawShadows(false).WithRoundness(0.2f);
    const IText rowLabel(14.f, kTextDim, "Roboto-Regular", EAlign::Near);
    IRECT b = g->GetBounds().GetPadded(-16.f);
    IRECT title = b.ReduceFromTop(36.f);
    g->AttachControl(new ITextControl(title.ReduceFromLeft(220.f), "ROOM BLEED", IText(22.f, kText, "Roboto-Regular", EAlign::Near)));
    g->AttachControl(new ITextControl(title, "", IText(15.f, kText, "Roboto-Regular", EAlign::Near)), kCtrlTagRoomInfo);
    g->AttachControl(new ITextControl(b.ReduceFromTop(24.f), "", IText(14.f, kAccent, "Roboto-Regular", EAlign::Near)), kCtrlTagStatus);
    b.ReduceFromTop(8.f);

    // The room, seen from above, on the right.
    const IRECT view = b.ReduceFromRight(410.f);
    b.ReduceFromRight(12.f);
    g->AttachControl(new IPanelControl(view, IColor(255, 30, 28, 25)));
    g->AttachControl(new roombleed_ui::RoomView(view.GetPadded(-8.f), {kParamDistance, kParamAim, kParamSourceX, kParamSourceY, kParamBearing},
                                                [this] { return MakeRoomView(); }), kCtrlTagRoomView);

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

    IRECT r = row("ROOM", 110.f);
    g->AttachControl(new IVMenuButtonControl(cell(r, 0, 2).GetMidVPadded(20.f), kParamRoom, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 2), kParamSize, "Size", style));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamSurfaces, "Surfaces", style));
    g->AttachControl(new IVButtonControl(cell(r, 4).GetMidVPadded(20.f), [this](IControl* c) {
      SplashClickActionFunc(c);
      LoadIrDialog();
    }, "LOAD IR", style));

    r = row("MIC", 56.f);
    g->AttachControl(new IVMenuButtonControl(cell(r, 0, 2).GetMidVPadded(20.f), kParamMic, "", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 2, 4).GetMidVPadded(20.f), kParamPattern, {"Omni", "Cardioid", "Hyper", "Figure-8"}, "", style), kCtrlTagPattern);

    r = row("PLACE", 110.f);
    g->AttachControl(new IVKnobControl(cell(r, 0), kParamDistance, "Distance", style));
    g->AttachControl(new IVKnobControl(cell(r, 1), kParamAim, "Aim", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 2, 3).GetMidVPadded(20.f), kParamWhere, {"Same room", "Next door", "Below"}, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamDoor, "Door", style), kCtrlTagDoor);

    r = row("ROOM TONE", 110.f);
    g->AttachControl(new IVMenuButtonControl(cell(r, 0, 2).GetMidVPadded(20.f), kParamTone, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 2), kParamToneLevel, "Level", style));
    g->AttachControl(new IVButtonControl(cell(r, 3).GetMidVPadded(20.f), [this](IControl* c) {
      SplashClickActionFunc(c);
      LoadToneDialog();
    }, "LOAD TONE", style));

    r = row("OUTPUT", 110.f);
    g->AttachControl(new IVTabSwitchControl(cell(r, 0).GetMidVPadded(20.f), kParamStereo, {"Mono", "Stereo"}, "", style));
    g->AttachControl(new IVTabSwitchControl(cell(r, 1, 2).GetMidVPadded(20.f), kParamPair, {"XY", "ORTF", "Spaced"}, "", style));
    g->AttachControl(new IVKnobControl(cell(r, 3), kParamMix, "Dry / Bled", style));
    g->AttachControl(new IVKnobControl(cell(r, 4), kParamOutput, "Output", style));
    g->AttachControl(new IVKnobControl(cell(r, 5), kParamWarmth, "Warmth", style));

    g->AttachControl(new IVPeakAvgMeterControl<2>(b.ReduceFromTop(60.f), "In", style, EDirection::Horizontal, {"L", "R"}), kCtrlTagInputMeter);
    g->AttachControl(new IVPeakAvgMeterControl<2>(b.ReduceFromTop(60.f), "Out", style, EDirection::Horizontal, {"L", "R"}), kCtrlTagOutputMeter);
  };
#endif
}

// ---- Rooms (main thread) ---------------------------------------------------------------

void RoomBleed::SetMessage(const std::string& msg, bool sticky)
{
  mMessage = msg;
  mMessageSticky = sticky;
  mMessageAt = Clock::now();
}

void RoomBleed::LoadIrDialog()
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

bool RoomBleed::LoadIrFile(const std::string& path)
{
  underheard::AudioData a;
  std::string error;
  if (!underheard::ReadWav(path, a, error))
  {
    SetMessage("can't load: " + error, true);
    return false;
  }
  Impulse imp = underheard::room::PrepareImpulse(a.stereo, a.sampleRate);
  if (imp.Frames() < 16)
  {
    SetMessage("can't load: no sound in that file", true);
    return false;
  }
  // Keep the prepared room with the project's files.
  const std::vector<float> st = Interleave(imp);
  underheard::SavedTape saved;
  if (!underheard::SaveTape(mTapeDir, st.data(), imp.Frames(), imp.sampleRate, saved, error))
  {
    SetMessage("couldn't save the room: " + error, true);
    return false;
  }
  mIrSaved = saved;
  UseImpulse(std::move(imp), std::filesystem::u8path(path).filename().u8string());
  SetParamFromPlugin(kParamRoom, LoadedRoomIndex());
  RebuildRoom();
  SetMessage("loaded");
  return true;
}

void RoomBleed::LoadToneDialog()
{
#if IPLUG_EDITOR
  if (!GetUI())
    return;
  mDialogFile.Set("");
  GetUI()->PromptForFile(mDialogFile, mDialogPath, EFileAction::Open, "wav", [this](const WDL_String& file, const WDL_String&) {
    if (file.GetLength())
      LoadToneFile(file.Get());
  });
#endif
}

bool RoomBleed::LoadToneFile(const std::string& path)
{
  underheard::AudioData a;
  std::string error;
  if (!underheard::ReadWav(path, a, error, (int64_t)192000 * 600)) // up to 10 minutes
  {
    SetMessage("can't load the tone: " + error, true);
    return false;
  }
  if (a.frames < (int64_t)(a.sampleRate * 1.))
  {
    SetMessage("can't load the tone: under a second long", true);
    return false;
  }
  underheard::SavedTape saved;
  if (!underheard::SaveTape(mTapeDir, a.stereo.data(), a.frames, a.sampleRate, saved, error))
  {
    SetMessage("couldn't save the tone: " + error, true);
    return false;
  }
  mToneSaved = saved;
  mToneSource = std::move(a.stereo);
  mToneRate = a.sampleRate;
  mToneBuiltRate = 0.;
  BuildToneLoop();
  SetParamFromPlugin(kParamTone, underheard::room::RoomTone::kLoaded);
  SetMessage("tone loaded");
  return true;
}

// Resamples the loaded tone to the host rate, makes it loop seamlessly, and offers it to the
// audio thread. Old loops come back through mToneRetired and are freed here.
void RoomBleed::BuildToneLoop()
{
  const double fs = GetSampleRate() > 0. ? GetSampleRate() : 48000.;
  if (mToneSource.empty() || fs == mToneBuiltRate)
    return;
  if (mToneOffered.load() != nullptr)
    return; // the last one hasn't been taken yet; try again on the next idle tick
  const std::vector<float> st = std::fabs(mToneRate - fs) > 0.5 ? underheard::ResampleStereo(mToneSource.data(), (int64_t)mToneSource.size() / 2, fs / mToneRate)
                                                                 : mToneSource;
  auto loop = std::make_unique<underheard::room::ToneLoop>(underheard::room::MakeToneLoop(st, 1.0, fs));
  // Keep at most two: the one playing and the new one. Free whichever isn't in use.
  if (auto* retired = mToneRetired.exchange(nullptr))
  {
    if (mToneOwnedA.get() == retired) mToneOwnedA.reset();
    else if (mToneOwnedB.get() == retired) mToneOwnedB.reset();
  }
  if (mToneOwnedA && mToneOwnedB)
    return; // both still in use; try again later
  std::unique_ptr<underheard::room::ToneLoop>& slot = mToneOwnedA ? mToneOwnedB : mToneOwnedA;
  slot = std::move(loop);
  mToneOffered.store(slot.get());
  mToneBuiltRate = fs;
}

void RoomBleed::UseImpulse(Impulse imp, const std::string& name)
{
  mImpulse = std::move(imp);
  mIrChannels = (int)mImpulse.channels.size();
  mIrRate = mImpulse.sampleRate;
  mIrName = name;
  mRebuildPending = true;
}

int RoomBleed::LoadedRoomIndex() const { return (int)underheard::room::RoomPresets().size(); }

void RoomBleed::SetParamFromPlugin(int paramIdx, double value)
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

// The shape of the selected preset with Size and Surfaces applied (the living room for a loaded
// recording, which only uses it for distance limits).
static underheard::room::RoomShape CurrentShape(const IParam* room, const IParam* size, const IParam* surfaces)
{
  const auto& presets = underheard::room::RoomPresets();
  const int i = std::min(room->Int(), (int)presets.size() - 1);
  return underheard::room::MakeShape(presets[(size_t)(room->Int() < (int)presets.size() ? i : 4)], size->Value() / 100., surfaces->Value() / 100.);
}

#if IPLUG_EDITOR
// What the room drawing shows, from the current settings (the same placement the room model
// uses, so the picture matches the sound).
roombleed_ui::RoomViewData RoomBleed::MakeRoomView() const
{
  using namespace underheard::room;
  roombleed_ui::RoomViewData d;
  const auto& presets = RoomPresets();
  const bool loaded = GetParam(kParamRoom)->Int() >= (int)presets.size();
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
  d.where = GetParam(kParamWhere)->Int();
  d.door = GetParam(kParamDoor)->Value() / 100.;
  char title[128];
  std::snprintf(title, sizeof title, "%s   %.1f x %.1f m%s", loaded ? "your recording (placed in a living room)" : presets[(size_t)GetParam(kParamRoom)->Int()].name,
                shape.width, shape.depth, loaded ? "" : "   drag the source, the mic, or the dot to aim");
  d.title = title;
  return d;
}
#endif

double RoomBleed::DistanceMetres() const
{
  const double maxD = underheard::room::MaxDistance(CurrentShape(GetParam(kParamRoom), GetParam(kParamSize), GetParam(kParamSurfaces)),
                                                    GetParam(kParamSourceX)->Value() / 100., GetParam(kParamSourceY)->Value() / 100., GetParam(kParamBearing)->Value());
  const double t = GetParam(kParamDistance)->Value() / 100.;
  return 0.1 * std::pow(std::max(maxD, 0.11) / 0.1, t); // logarithmic: fine control up close
}

bool RoomBleed::RoomSettingsChanged()
{
  std::vector<double> now = {GetSampleRate(), (double)mImpulse.Frames()};
  for (int p : {kParamRoom, kParamSize, kParamSurfaces, kParamDistance, kParamAim, kParamPattern, kParamStereo, kParamPair, kParamMic, kParamWhere, kParamDoor, kParamSourceX, kParamSourceY, kParamBearing})
    now.push_back(GetParam(p)->Value());
  if (now == mBuiltSettings)
    return false;
  mBuiltSettings = now;
  return true;
}

// Builds the room for the current settings and hands a new convolver to the audio thread.
void RoomBleed::RebuildRoom()
{
  using namespace underheard::room;
  RoomSettingsChanged(); // record what this build is for
  mRebuildPending = false;
  const double fs = GetSampleRate() > 0. ? GetSampleRate() : 48000.;
  const double distance = DistanceMetres();
  const bool stereo = GetParam(kParamStereo)->Int() == 1;
  const MicModel& mic = MicModels()[(size_t)GetParam(kParamMic)->Int()];
  const MicVariant& variant = mic.Variant((MicPattern)GetParam(kParamPattern)->Int());
  const Where where = (Where)GetParam(kParamWhere)->Int();
  const bool elsewhere = where != Where::SameRoom;
  std::vector<std::vector<float>> irs;

  if (GetParam(kParamRoom)->Int() >= LoadedRoomIndex())
  {
    if (mImpulse.Frames() == 0)
    {
      mRoomInfo = "no recording loaded (LOAD IR)";
      return;
    }
    // A recording can't be re-placed, so Distance works on the balance: the direct sound (the
    // first 2.5 ms) falls off as 1/r from 1 m, the whole thing arrives later, and the air takes
    // some highs.
    std::vector<std::vector<float>> src = mImpulse.channels;
    if (std::fabs(mImpulse.sampleRate - fs) > 0.5)
    {
      const std::vector<float> st = underheard::ResampleStereo(Interleave(mImpulse).data(), mImpulse.Frames(), fs / mImpulse.sampleRate);
      for (size_t ch = 0; ch < src.size(); ch++)
      {
        src[ch].resize(st.size() / 2);
        for (size_t i = 0; i < src[ch].size(); i++)
          src[ch][i] = st[2 * i + ch];
      }
    }
    const size_t predelay = (size_t)(std::max(0., distance - 1.) / 343. * fs);
    const size_t directEnd = (size_t)(0.0025 * fs);
    const float directGain = (float)(1. / std::max(distance, 0.5));
    // Beyond the recording's own (assumed 1 m) distance, the air takes some highs.
    const double airCut = 20000. / (1. + (distance - 1.) / 8.);
    const double k = distance > 1.01 ? std::exp(-2. * 3.14159265358979 * std::min(airCut, 0.45 * fs) / fs) : 0.;
    for (auto& c : src)
    {
      std::vector<float> o(predelay + c.size(), 0.f);
      double z = 0.;
      for (size_t i = 0; i < c.size(); i++)
      {
        z = (1. - k) * (i < directEnd ? c[i] * directGain : c[i]) + k * z;
        o[predelay + i] = (float)z;
      }
      irs.push_back(std::move(o));
    }
    if (stereo && irs.size() == 1)
      irs.push_back(irs[0]);
    char info[192];
    std::snprintf(info, sizeof info, "%s   %.2f s, %s   mic %.2f m", mIrName.c_str(), (double)mImpulse.Frames() / mImpulse.sampleRate,
                  mIrChannels > 1 ? "stereo" : "mono", distance);
    mRoomInfo = info;
  }
  else
  {
    RoomSpec spec;
    spec.shape = CurrentShape(GetParam(kParamRoom), GetParam(kParamSize), GetParam(kParamSurfaces));
    // Heard from elsewhere, the source room is collected where the wall (or floor) is, by
    // something like an omni; the mic itself is in the listener's room.
    const Placement p = PlaceMics(spec.shape, GetParam(kParamSourceX)->Value() / 100., GetParam(kParamSourceY)->Value() / 100., GetParam(kParamBearing)->Value(),
                                  distance, elsewhere ? 0. : GetParam(kParamAim)->Value(), stereo && !elsewhere, GetParam(kParamPair)->Int());
    spec.source = p.source;
    spec.mics = p.mics;
    spec.tailCorrelation = p.tailCorrelation;
    if (!elsewhere)
    {
      spec.pickup.a = variant.a;
      spec.pickup.proximity = mic.proximity;
      spec.pickup.roomGain = mic.kind == MicKind::Contact ? 0.06 : 1.; // it hears the surface, not the air
    }
    spec.sampleRate = fs;
    RoomIR r = GenerateRoom(spec);
    irs = std::move(r.channels);
    char info[192];
    std::snprintf(info, sizeof info, "%s   %.1f x %.1f x %.1f m   decay %.2f s   %s %.2f m from the source", RoomPresets()[(size_t)GetParam(kParamRoom)->Int()].name,
                  spec.shape.width, spec.shape.depth, spec.shape.height, r.rt60[3], mic.name, distance);
    mRoomInfo = info;
  }

  if (elsewhere)
  {
    // The listener's room (a bedroom for now), heard through the chosen mic and pair from 1.5 m
    // off the wall it shares.
    RoomSpec listener;
    listener.shape = MakeShape(RoomPresets()[2], 0., 0.);
    const Placement lp = PlaceMics(listener.shape, 1.5, GetParam(kParamAim)->Value(), stereo, GetParam(kParamPair)->Int());
    listener.source = lp.source;
    listener.mics = lp.mics;
    listener.tailCorrelation = lp.tailCorrelation;
    listener.pickup.a = variant.a;
    listener.pickup.proximity = mic.proximity;
    listener.pickup.roomGain = mic.kind == MicKind::Contact ? 0.06 : 1.;
    listener.sampleRate = fs;
    listener.maxSeconds = 1.5;
    listener.seed = 7;
    irs = Occlude(irs, where, GetParam(kParamDoor)->Value() / 100., GenerateRoom(listener).channels, fs);
    mRoomInfo += where == Where::NextRoom ? "   heard from next door" : "   heard from below";
    if (where == Where::NextRoom)
      mRoomInfo += GetParam(kParamDoor)->Value() < 1. ? " (door shut)" : GetParam(kParamDoor)->Value() > 99. ? " (door open)" : " (door ajar)";
  }
  // The mic's frequency response goes into the impulse response itself.
  const std::vector<float> micFilter = MicFilter(variant.response, fs);
  for (auto& ir : irs)
    ApplyFilter(ir, micFilter);
  if (auto c = Convolver::Make(irs, fs))
    mSwitch.Offer(std::move(c)); // an unclaimed earlier offer comes back and is freed here
}

// ---- State -----------------------------------------------------------------------------

// Format 1: magic, version, parameter count, the parameters, then the loaded room (frames,
// hash, path, channels, sample rate; frames == 0 for none).
bool RoomBleed::SerializeState(IByteChunk& chunk) const
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
  // Version 2: the loaded room tone.
  const int64_t toneFrames = mToneSaved.frames;
  const uint64_t toneHash = mToneSaved.hash;
  chunk.Put(&toneFrames);
  chunk.Put(&toneHash);
  chunk.PutStr(mToneSaved.path.c_str());
  chunk.Put(&mToneRate);
  return true;
}

int RoomBleed::UnserializeState(const IByteChunk& chunk, int startPos)
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
  // Before version 3, Warmth ran half as strong: the old full is the new 50%.
  if (version < 3 && n > kParamWarmth)
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
    SetMessage("couldn't read the room from the project", true);
    return p;
  }
  saved.path = path.Get();
  if (saved.frames > 0)
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
      UseImpulse(std::move(imp), std::filesystem::u8path(saved.path).filename().u8string());
      // Projects from before there was a Room choice only had the recording.
      if (n <= kParamRoom)
        GetParam(kParamRoom)->Set(LoadedRoomIndex());
      SetMessage("recording restored from the project", true);
    }
    else
      SetMessage("room not restored: " + error, true);
  }

  if (version >= 2 && p >= 0)
  {
    underheard::SavedTape tone;
    WDL_String tonePath;
    double toneRate = 0.;
    p = chunk.Get(&tone.frames, p);
    if (p >= 0) p = chunk.Get(&tone.hash, p);
    if (p >= 0) p = chunk.GetStr(tonePath, p);
    if (p >= 0) p = chunk.Get(&toneRate, p);
    if (p >= 0 && tone.frames > 0)
    {
      tone.path = tonePath.Get();
      std::vector<float> st;
      std::string error;
      if (underheard::LoadSavedTape(tone, mTapeDir, st, error))
      {
        mToneSaved = tone;
        mToneSource = std::move(st);
        mToneRate = toneRate > 0. ? toneRate : 48000.;
        mToneBuiltRate = 0.;
        BuildToneLoop();
      }
      else
        SetMessage("room tone not restored: " + error, true);
    }
  }
  return p;
}

// ---- Main thread -----------------------------------------------------------------------

void RoomBleed::OnIdle()
{
  mInputPeakSender.TransmitData(*this);
  mOutputPeakSender.TransmitData(*this);
  mSwitch.TakeRetired(); // frees a convolver the audio thread has finished with

  // Rebuild the room once its settings have stopped changing for a moment.
  if (RoomSettingsChanged())
  {
    mSettingsChangedAt = Clock::now();
    mRebuildPending = true;
  }
  if (mRebuildPending && Clock::now() - mSettingsChangedAt > std::chrono::milliseconds(60))
    RebuildRoom();
  BuildToneLoop(); // does nothing unless the loaded tone needs (re)building for this rate
  UpdateStatus();
}

void RoomBleed::UpdateStatus()
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
  setText(kCtrlTagRoomInfo, mRoomInfo.c_str());
  if (IControl* view = ui->GetControlWithTag(kCtrlTagRoomView))
    view->SetDirty(false); // redraw with the current settings
  if (IControl* door = ui->GetControlWithTag(kCtrlTagDoor))
  {
    const bool nextDoor = GetParam(kParamWhere)->Int() == 1;
    if (door->IsDisabled() == nextDoor)
      door->SetDisabled(!nextDoor);
  }
  // Pattern only means something for multi-pattern mics and the ideal mic.
  if (IControl* pattern = ui->GetControlWithTag(kCtrlTagPattern))
  {
    const bool multi = underheard::room::MicModels()[(size_t)GetParam(kParamMic)->Int()].MultiPattern();
    if (pattern->IsDisabled() == multi)
      pattern->SetDisabled(!multi);
  }
  setText(kCtrlTagStatus, mMessage.c_str());
#endif
}

// ---- Audio thread ----------------------------------------------------------------------

#if IPLUG_DSP
void RoomBleed::OnReset()
{
  const double fs = GetSampleRate();
  mInputPeakSender.Reset(fs);
  mOutputPeakSender.Reset(fs);
  mSmoothCoef = 1. - std::exp(-1. / (0.02 * fs));
  const size_t block = (size_t)std::max(GetBlockSize(), 64);
  mMono.assign(block, 0.f);
  mWetL.assign(block, 0.f);
  mWetR.assign(block, 0.f);
  mMicPost.Prepare(fs);
  mWarmth.Prepare(fs);
  mMicConfigured = -1;
  mRoomTone.Prepare(fs, 0x70A3u);
  mToneBuiltRate = 0.; // rebuilt for this rate in OnIdle
  mRebuildPending = true; // the room has to match the host's sample rate (rebuilt in OnIdle)
}

void RoomBleed::GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const
{
  if (direction == ERoute::kInput)
    str.Set(busIdx == 0 ? "Main Input" : "SideChain");
  else
    str.Set("Output");
}

void RoomBleed::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const double gain = GetParam(kParamOutput)->DBToAmp();
  const double mixTarget = GetParam(kParamMix)->Value() / 100.;
  const int nOut = NOutChansConnected();
  // Channels are laid out per bus at each bus's maximum width: main is 0-1, sidechain 2-3.
  const bool stereoIn = IsChannelConnected(ERoute::kInput, 1);
  const sample* mainIn[2] = {inputs[0], stereoIn ? inputs[1] : inputs[0]};
  const bool haveRoom = mSwitch.HasConvolver();
  mWarmth.SetAmount(GetParam(kParamWarmth)->Value() / 100.);
  // A new loaded tone: switch to it and hand the old one back to the main thread.
  if (mToneRetired.load() == nullptr)
    if (auto* next = mToneOffered.exchange(nullptr))
    {
      if (mToneCurrent)
        mToneRetired.store(mToneCurrent);
      mToneCurrent = next;
    }
  mRoomTone.SetLoop(mToneCurrent);
  mRoomTone.SetPreset(GetParam(kParamTone)->Int());
  const auto& micModel = underheard::room::MicModels()[(size_t)GetParam(kParamMic)->Int()];
  // The tone is the listener's room, heard by the mic (a contact pickup hears little air).
  const float toneGain = (float)(std::pow(10., (GetParam(kParamToneLevel)->Value() + 10.) / 20.) *
                                 (micModel.kind == underheard::room::MicKind::Contact ? 0.1 : 1.));
  // The mic's real-time part (noise, saturation, auto-gain) follows the Mic and Pattern choice.
  const int micChoice = GetParam(kParamMic)->Int() * 8 + GetParam(kParamPattern)->Int();
  if (micChoice != mMicConfigured)
  {
    const auto& mic = underheard::room::MicModels()[(size_t)GetParam(kParamMic)->Int()];
    mMicPost.Configure(mic, mic.Variant((underheard::room::MicPattern)GetParam(kParamPattern)->Int()));
    mMicConfigured = micChoice;
  }

  for (int start = 0; start < nFrames;)
  {
    const int n = std::min(nFrames - start, (int)mMono.size());
    // The source is a point in the room: a stereo input is summed to mono.
    for (int i = 0; i < n; i++)
      mMono[(size_t)i] = (float)(0.5 * (mainIn[0][start + i] + mainIn[1][start + i]));
    mSwitch.Process(mMono.data(), mWetL.data(), mWetR.data(), n);
    if (haveRoom)
    {
      mRoomTone.Process(mWetL.data(), mWetR.data(), n, toneGain);
      mMicPost.Process(mWetL.data(), mWetR.data(), n);
      for (int i = 0; i < n; i++)
        mWarmth.Process(mWetL[(size_t)i], mWetR[(size_t)i]);
    }

    for (int i = 0; i < n; i++)
    {
      mMixSmoothed += (mixTarget - mMixSmoothed) * mSmoothCoef;
      const double a = mMixSmoothed * 1.5707963267948966;
      const double dryG = std::cos(a), wetG = std::sin(a);
      // With no room loaded yet the sound passes straight through.
      const double wl = haveRoom ? mWetL[(size_t)i] : mainIn[0][start + i];
      const double wr = haveRoom ? mWetR[(size_t)i] : mainIn[1][start + i];
      const double o[2] = {(mainIn[0][start + i] * dryG + wl * wetG) * gain, (mainIn[1][start + i] * dryG + wr * wetG) * gain};
      for (int c = 0; c < nOut; c++)
        outputs[c][start + i] = o[std::min(c, 1)];
    }
    start += n;
  }

  mInputPeakSender.ProcessBlock(const_cast<sample**>(mainIn), nFrames, kCtrlTagInputMeter, 2, 0);
  mOutputPeakSender.ProcessBlock(outputs, nFrames, kCtrlTagOutputMeter, nOut, 0);
}
#endif // IPLUG_DSP
