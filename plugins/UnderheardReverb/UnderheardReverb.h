#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"
#include "IControls.h"

#include "Convolver.h"
#include "ImpulsePrep.h"
#include "ReverbCore.h"
#include "TapeFiles.h"
#if IPLUG_EDITOR
#include "RoomView.h"
#endif

#include <chrono>
#include <string>
#include <vector>

const int kNumPresets = 8; // the templates (see Templates())

// Order is saved state, but the state stores a count, so parameters can be appended.
enum EParams
{
  kParamOutput = 0,
  kParamMix,        // dry <-> reverb
  kParamEngine,     // Rooms, Recordings, Plate, Hall
  kParamPreSync,    // pre-delay free (ms) or synced
  kParamPreDelay,   // ms
  kParamPreNote,    // note value, when synced
  kParamDecay,      // x0.25 .. x4: stretches every engine's decay
  kParamBalance,    // -100 early only .. 0 both .. 100 late only
  kParamLowCut,     // Hz, on the reverb
  kParamHighCut,    // Hz, on the reverb
  kParamWidth,      // 0 mono .. 150%
  kParamModulation, // Plate and Hall: tail movement
  kParamAge,        // a softer top, saturation, darker tanks
  kParamFreeze,
  kParamDuck,
  // Rooms (Room Bleed's engine)
  kParamRoom,       // a generated room preset
  kParamSize,       // -100 .. 100%: half to double (also the plate's and hall's size)
  kParamSurfaces,   // -100 .. 100%: softer to harder
  kParamDistance,   // 0 .. 100%: 0.1 m to as far as the room allows (logarithmic)
  kParamAim,        // degrees turned away from the source
  kParamPattern,    // for multi-pattern mics and the ideal mic
  kParamStereo,     // one mic or a pair
  kParamPair,       // XY, ORTF, spaced
  kParamMic,        // which microphone
  kParamSourceX,    // % of the room's width
  kParamSourceY,    // % of its depth
  kParamBearing,    // which way the mic is from the source, in plan (degrees)
  kParamWarmth,     // the suite's analog colour, on the reverb
  kNumParams
};

enum ECtrlTags
{
  kCtrlTagInputMeter = 0,
  kCtrlTagOutputMeter,
  kCtrlTagStatus,
  kCtrlTagInfo,
  kCtrlTagRoomView,
  kCtrlTagViewPanel,
  kCtrlTagEngineNote,
  kNumCtrlTags
};

using namespace iplug;
using namespace igraphics;

// See docs/EFFECTS-SPEC.md.
class UnderheardReverb final : public Plugin
{
public:
  UnderheardReverb(const InstanceInfo& info);

  void OnIdle() override;
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;
#if IPLUG_EDITOR
  void OnParamChangeUI(int paramIdx, EParamSource source) override;
  void OnUIOpen() override;
#endif
#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const override;
#endif

private:
  using Clock = std::chrono::steady_clock;

  // Main thread
  bool LoadIrFile(const std::string& path);
  void LoadIrDialog();
  void RebuildConvolver();         // Rooms or Recordings, from the current settings
  bool ConvolverSettingsChanged();
  double DistanceMetres() const;
  double AlgoDecaySeconds() const; // Plate and Hall
#if IPLUG_EDITOR
  roombleed_ui::RoomViewData MakeRoomView() const;
#endif
  void SetParamFromPlugin(int paramIdx, double value);
  void ApplyTemplate(int index);
  void SetMessage(const std::string& msg, bool sticky = false);
  void UpdateEnabled();
  void UpdateStatus();

  underheard::room::ConvolverSwitch mSwitch;
  underheard::room::Impulse mImpulse; // the loaded recording, prepared, at its own rate
  underheard::SavedTape mIrSaved;
  int mIrChannels = 0;
  double mIrRate = 0.;
  std::string mIrName, mTapeDir, mMessage, mInfo;
  bool mMessageSticky = false;
  Clock::time_point mMessageAt{}, mSettingsChangedAt{};
  std::vector<double> mBuiltSettings;
  bool mRebuildPending = true;
  WDL_String mDialogFile, mDialogPath;

  // Audio thread
  underheard::fx::ReverbCore mCore;
  std::vector<float> mInL, mInR, mOutL, mOutR;

  IPeakAvgSender<2> mInputPeakSender;
  IPeakAvgSender<2> mOutputPeakSender;
};
