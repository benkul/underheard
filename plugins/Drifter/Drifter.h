#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "IPlugQueue.h"

#include "DriftLanes.h"
#include "HostedInstrument.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

const int kNumPresets = 1;
constexpr int kNumSlots = 128; // pass-through slots: the hosted synth's driftable parameters, in order

// Order is saved state, but the state stores a count, so parameters can be appended.
enum EParams
{
  kParamDrift = 0,  // drift on/off
  kParamTiming,     // Length in seconds or in bars
  kParamLength,     // seconds
  kParamBars,       // a choice of bars
  kParamCurve,
  kParamSmear,
  kParamReach,
  kParamGravity,
  kParamSlot0,      // ... kParamSlot0 + kNumSlots - 1
  kNumParams = kParamSlot0 + kNumSlots
};

enum ECtrlTags
{
  kCtrlTagName = 0,
  kCtrlTagStatus,
  kCtrlTagPlaceholder, // where the synth's editor goes, when there isn't one
  kCtrlTagLearn,
  kCtrlTagLane0,       // ... kCtrlTagLane0 + kMaxLanes - 1: the lane rows
  kNumCtrlTags = kCtrlTagLane0 + underheard::DriftLanes::kMaxLanes
};

using namespace iplug;
using namespace igraphics;

// See docs/DRIFTER-SPEC.md: hosts one VST3 instrument and drifts several of its parameters.
class Drifter final : public Plugin
{
public:
  Drifter(const InstanceInfo& info);
  ~Drifter() override;

  void OnIdle() override;
  void OnUIOpen() override;
  void OnUIClose() override;
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;
#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
#endif

  // Commands from the main thread to the audio thread (which owns the lanes).
  struct Command
  {
    enum Type { kAddLane, kRemoveLane, kSetRange, kSetOn, kLearn, kCancelLearn, kEdit, kKeep, kReturn, kClearLanes } type;
    int index = 0;
    uint32_t id = 0;
    double a = 0., b = 0., c = 0.;
    int edit = 0; // for kEdit: a DriftLanes::Edit
  };

private:
  friend class LaneRowControl;
  using Hosted = underheard::vst3host::HostedInstrument;

  // Main thread
  bool LoadSynth(const std::string& path, int classIndex, const std::vector<uint8_t>* component = nullptr, const std::vector<uint8_t>* controller = nullptr);
  void LoadDialog();
  void OnHostedEdit(Hosted::Edit e, uint32_t id, double value); // from the synth's editor
  void UpdateSlots();                                            // names and values, after a synth loads or renames
  void RenameSlots();
  void Send(const Command& c);
  void SetMessage(const std::string& msg) { mMessage = msg; }
  void UpdateStatus();

  // The synth's editor, under the strip (docs/DRIFTER-SPEC.md). Sizes in Drifter's own units
  // (Windows: the synth's pixels divided by the screen scale).
  void OpenSynthEditor(bool deferFit);
  void CloseSynthEditor();
  void ProbeEditorSize();   // the synth's editor size, without opening it (for the window's size before it opens)
  int StripHeight() const;  // the strip: with the lanes panel or without
  void FitWindow();         // Drifter's window: the strip + the editor (or the placeholder)
  double PlatformScale() const;

  // Hosted synths: every instance this plugin made (main thread owns them), the newest one,
  // and the hand-over to the audio thread.
  std::vector<std::unique_ptr<Hosted>> mInstances;
  Hosted* mMain = nullptr;
  int mMainClass = 0; // which instrument in its bundle
  std::atomic<Hosted*> mOffered{nullptr}, mRetired{nullptr};
  std::array<int, kNumSlots> mSlotParam{}; // slot -> the synth's parameter index (-1: none)
  std::atomic<uint32_t> mSlotId[kNumSlots];
  std::atomic<double> mSlotLoaded[kNumSlots]; // the synth's own values when its slots were set
  std::atomic<int> mNumSlotsUsed{0};
  // A synth that couldn't be found: kept so the project doesn't lose it.
  std::string mMissingPath, mMissingClassId;
  int mMissingClass = 0;
  std::vector<uint8_t> mMissingComponent, mMissingController;
  std::string mMessage;
  WDL_String mDialogFile, mDialogPath;
  int mDisplayTick = 0;
  void* mContainer = nullptr;            // the native view the synth's editor is attached to
  int mEditorW = 0, mEditorH = 0;        // the synth's editor (0: none)
  bool mShowLanes = true;                // saved with the project
  bool mFitPending = false;

  // Main -> audio commands: a small single-producer, single-consumer ring.
  static constexpr int kQueue = 256;
  std::array<Command, kQueue> mQueue;
  std::atomic<int> mQueueWrite{0}, mQueueRead{0};

  // Audio -> main: the lanes as they are now.
  struct LaneView
  {
    std::atomic<uint32_t> id{0};
    std::atomic<double> home{0.}, lo{0.}, hi{1.}, value{0.};
    std::atomic<bool> on{true};
  };
  std::array<LaneView, underheard::DriftLanes::kMaxLanes> mLaneView;
  std::atomic<int> mLaneCount{0};
  std::atomic<bool> mLearning{false};

  // Audio thread
  Hosted* mPlaying = nullptr;
  underheard::DriftLanes mLanes;
  IMidiQueue mMidiQueue;
  std::vector<underheard::vst3host::MidiEvent> mEvents;
  std::vector<underheard::vst3host::ParamChange> mChanges;
  std::array<double, kNumSlots> mSlotSent{};
  std::array<double, underheard::DriftLanes::kMaxLanes> mLaneSent{};
  std::vector<float> mOutL, mOutR;
  int64_t mSamplePos = 0;
};
