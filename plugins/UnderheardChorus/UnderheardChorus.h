#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"
#include "IControls.h"

#include "MultiChorus.h"

#include <atomic>

const int kNumPresets = 7; // the templates (see Templates())

// Order is saved state, but the state stores a count, so parameters can be appended.
enum EParams
{
  kParamOutput = 0,
  kParamMix,        // dry <-> voices (chorus and ensemble)
  kParamMode,       // chorus, vibrato (voices only), ensemble
  kParamVoices,     // 1 .. 4
  kParamSync,       // free (Hz) or synced to the host tempo
  kParamRate,       // Hz, when free
  kParamNote,       // one LFO cycle, when synced (NoteValues())
  kParamDepth,
  kParamDelay,      // ms: the voices' centre time
  kParamShape,      // sine, triangle, wander
  kParamFeedback,   // -90 .. 90%
  kParamPhase,      // degrees: the right side's LFO offset
  kParamSpread,     // stereo width of the voices
  kParamTone,       // Hz: the voices' high cut
  kParamAge,        // tape warble, a softer top, a little saturation
  kParamWarmth,     // the suite's analog colour, on the voices
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

// See docs/EFFECTS-SPEC.md.
class UnderheardChorus final : public Plugin
{
public:
  UnderheardChorus(const InstanceInfo& info);

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
  void UpdateStatus();
  void ApplyTemplate(int index); // from the panel's TEMPLATES menu

  // Audio thread
  underheard::fx::MultiChorus mChorus;
  // For the status line (written by the audio thread)
  std::atomic<double> mShownRate{0.}, mShownTempo{0.};
  std::atomic<bool> mShownLocked{false};

  IPeakAvgSender<2> mInputPeakSender;
  IPeakAvgSender<2> mOutputPeakSender;
};
