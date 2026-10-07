#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"
#include "IControls.h"

#include "Convolver.h"
#include "ImpulsePrep.h"
#include "MicModel.h"
#include "Occlusion.h"
#include "RoomTone.h"
#include "RoomModel.h"
#include "TapeFiles.h"
#include "Warmth.h"
#if IPLUG_EDITOR
#include "RoomView.h"
#endif

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

const int kNumPresets = 1;

// Order is saved state, but the state stores a count, so parameters can be appended.
enum EParams
{
  kParamOutput = 0,
  kParamMix,        // dry <-> bled
  kParamRoom,       // a generated room preset, or the loaded recording
  kParamSize,       // -100 .. 100%: half to double the preset's size
  kParamSurfaces,   // -100 .. 100%: softer to harder
  kParamDistance,   // 0 .. 100%: 0.1 m to as far as the room allows (logarithmic)
  kParamAim,        // degrees turned away from the source (0 .. 360)
  kParamPattern,    // the pattern, for multi-pattern mics (U 87, C414) and the ideal mic
  kParamStereo,     // mono (one mic) or stereo (a pair)
  kParamPair,       // XY, ORTF, spaced
  kParamMic,        // which microphone (docs/MIC-DATA.md)
  kParamWhere,      // same room, next door, the floor below
  kParamDoor,       // next door: 0 closed .. 100% open
  kParamTone,       // room tone: none, a generated room, or the loaded recording
  kParamToneLevel,  // dBFS (RMS)
  kParamSourceX,    // where the source stands: % of the room's width
  kParamSourceY,    // and of its depth
  kParamBearing,    // which way the mic is from the source, in plan (degrees; 0 = toward x = W)
  kParamWarmth,     // the suite's analog colour, on the bled sound
  kNumParams
};

enum EPattern { kPatternOmni = 0, kPatternCardioid, kPatternHyper, kPatternFigure8, kNumPatterns };
enum EPair { kPairXY = 0, kPairORTF, kPairSpaced };

enum ECtrlTags
{
  kCtrlTagInputMeter = 0,
  kCtrlTagOutputMeter,
  kCtrlTagStatus,
  kCtrlTagRoomInfo,
  kCtrlTagPattern,
  kCtrlTagDoor,
  kCtrlTagRoomView,
  kNumCtrlTags
};

using namespace iplug;
using namespace igraphics;

// See docs/ROOM-BLEED-SPEC.md. Stage R1: convolution with a loaded room recording.
class RoomBleed final : public Plugin
{
public:
  RoomBleed(const InstanceInfo& info);

  void OnIdle() override;
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;
#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const override;
#endif

private:
  using Clock = std::chrono::steady_clock;

  // Main thread
  bool LoadIrFile(const std::string& path);
  bool LoadToneFile(const std::string& path);
  void LoadToneDialog();
  void BuildToneLoop();               // resamples the loaded tone and hands it to the audio thread
  void UseImpulse(underheard::room::Impulse imp, const std::string& name);
  void RebuildRoom();               // from the current settings: generated or loaded
  bool RoomSettingsChanged();
  int LoadedRoomIndex() const;      // the Room choice that means "the loaded recording"
  double DistanceMetres() const;
#if IPLUG_EDITOR
  roombleed_ui::RoomViewData MakeRoomView() const;
#endif
  void SetParamFromPlugin(int paramIdx, double value);
  void LoadIrDialog();
  void SetMessage(const std::string& msg, bool sticky = false);
  void UpdateStatus();

  underheard::room::ConvolverSwitch mSwitch;
  underheard::room::Impulse mImpulse;      // the prepared room, at its own sample rate
  underheard::SavedTape mIrSaved;          // where it's kept (frames == 0: none)
  // The loaded room tone: as recorded (interleaved stereo), where it's kept, and the loops
  // handed to the audio thread (the main thread owns them; the audio thread borrows).
  std::vector<float> mToneSource;
  double mToneRate = 0., mToneBuiltRate = 0.;
  underheard::SavedTape mToneSaved;
  std::atomic<underheard::room::ToneLoop*> mToneOffered{nullptr}, mToneRetired{nullptr};
  std::unique_ptr<underheard::room::ToneLoop> mToneOwnedA, mToneOwnedB; // at most two alive
  int mIrChannels = 0;
  double mIrRate = 0.;
  std::string mIrName, mTapeDir, mMessage;
  bool mMessageSticky = false;
  Clock::time_point mMessageAt{};
  std::vector<double> mBuiltSettings;     // what the current room was built from
  Clock::time_point mSettingsChangedAt{};
  bool mRebuildPending = true;
  std::string mRoomInfo;
  WDL_String mDialogFile, mDialogPath;

  // Audio thread
  underheard::room::MicPost mMicPost;
  underheard::fx::Warmth mWarmth;
  underheard::room::RoomTone mRoomTone;
  underheard::room::ToneLoop* mToneCurrent = nullptr;
  int mMicConfigured = -1;
  std::vector<float> mMono, mWetL, mWetR;
  double mMixSmoothed = 1., mSmoothCoef = 1.;

  IPeakAvgSender<2> mInputPeakSender;
  IPeakAvgSender<2> mOutputPeakSender;
};
