#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"
#include "IControls.h"

#include "DubDelay.h"

#include <atomic>

const int kNumPresets = 7; // the templates (see kTemplates)

// Order is saved state, but the state stores a count, so parameters can be appended.
enum EParams
{
  kParamOutput = 0,
  kParamMix,        // dry <-> echoes
  kParamSync,       // free (ms) or synced to the host tempo
  kParamTime,       // ms, when free
  kParamNote,       // note value, when synced (NoteValues())
  kParamGlide,      // ms: how fast a time change slides
  kParamFeedback,   // 0 .. 110%
  kParamPolarity,   // normal or inverted repeats
  kParamMode,       // single, ping-pong, multi-head
  kParamHeads,      // multi-head: which heads play (1, 2, 3, 1+2, 1+3, 2+3, 1+2+3)
  kParamLowCut,     // Hz, in the loop
  kParamHighCut,    // Hz, in the loop
  kParamResonance,  // on the high cut
  kParamDrive,
  kParamAge,
  kParamWow,
  kParamFlutter,
  kParamPhase,      // degrees: the right side's wobble offset
  kParamSpread,     // left/right time offset, or the ping-pong width
  kParamSend,       // always, or only while Throw is held
  kParamThrow,      // momentary
  kParamFreeze,
  kParamDuck,
  kParamWarmth,     // the suite's analog colour, on the echoes
  kNumParams
};

enum ECtrlTags
{
  kCtrlTagInputMeter = 0,
  kCtrlTagOutputMeter,
  kCtrlTagStatus,
  kNumCtrlTags
};

using namespace iplug;
using namespace igraphics;

// See docs/EFFECTS-SPEC.md: a tape echo that flexes into dub.
class UnderheardDelay final : public Plugin
{
public:
  UnderheardDelay(const InstanceInfo& info);

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
  void UpdateEnabled();
  void ApplyTemplate(int index); // from the panel's TEMPLATES menu
  void UpdateStatus();

  // Audio thread
  underheard::fx::DubDelay mDelay;
  // For the status line (written by the audio thread)
  std::atomic<double> mShownTime{0.}, mShownTempo{0.};

  IPeakAvgSender<2> mInputPeakSender;
  IPeakAvgSender<2> mOutputPeakSender;
};
