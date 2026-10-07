#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"
#include "IControls.h"

#include "CaptureBuffer.h"
#include "Drifter.h"
#include "FxChain.h"
#include "NoteValues.h"
#include "TapeFiles.h"
#include "TapeLoop.h"
#include "TapeStorage.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

const int kNumPresets = 1;

constexpr int kNumLoops = 4;

// Per-loop parameters. Each section is one block per loop, and sections come one after another
// in EParams, so adding a section never moves an existing parameter (see LoopParam()).
enum ELoopParam
{
  // Section A (stage 2)
  kLoopRecord = 0,
  kLoopPlay,
  kLoopClear,
  kLoopCatch,
  kLoopSpeed,     // octaves, -2 .. +1 (0.25x .. 2x)
  kLoopReverse,
  kLoopSource,    // main input or sidechain
  kLoopErase,
  kLoopFeedback,
  kLoopSplice,
  kLoopDry,
  kLoopPan,
  kNumLoopParamsA,
  // Section B (stage 3)
  kLoopWear = kNumLoopParamsA,
  kLoopWearRate,
  kLoopRestore,
  kLoopWow,
  kLoopFlutter,
  kNumLoopParamsAB,
  // Section C (stage 4)
  kLoopRazor = kNumLoopParamsAB, // mark the cut's start, then its end (a third press clears)
  kLoopCutReverse,               // flip the marked cut
  kLoopCutRemove,                // take it out
  kLoopCutIsolate,               // keep only it
  kNumLoopParamsABC,
  // Section D (stage 6)
  kLoopSend = kNumLoopParamsABC, // into the effects chain (after pan, before Dry)
  kNumLoopParamsABCD,
  // Section E
  kLoopWearLimit = kNumLoopParamsABCD, // the terminal point of wear
  kLoopRecover,                        // at the limit: heal back to new (else hold)
  kNumLoopParams
};
constexpr int kNumLoopParamsB = kNumLoopParamsAB - kNumLoopParamsA;
constexpr int kNumLoopParamsC = kNumLoopParamsABC - kNumLoopParamsAB;
constexpr int kNumLoopParamsD = kNumLoopParamsABCD - kNumLoopParamsABC;
constexpr int kNumLoopParamsE = kNumLoopParams - kNumLoopParamsABCD;

enum ESource { kSourceMain = 0, kSourceSidechain, kNumSources };
enum ESync { kSyncFree = 0, kSyncBeats, kSyncBars };

// Order is saved state once projects exist: append only, never reorder.
enum EParams
{
  kParamOutput = 0,
  kParamMix,          // input <-> tape
  kParamMotor,        // seconds
  kParamCatchLength,  // seconds
  kParamSync,
  kParamLoops,        // loop blocks, section A
  kParamHiss = kParamLoops + kNumLoops * kNumLoopParamsA,
  kParamLoopsB,       // loop blocks, section B
  kParamLoopsC = kParamLoopsB + kNumLoops * kNumLoopParamsB,
  // The effects chain (stage 6)
  kParamChorusOn = kParamLoopsC + kNumLoops * kNumLoopParamsC,
  kParamChorusRate,
  kParamChorusDepth,
  kParamChorusMix,
  kParamDelayOn,
  kParamDelayTime,
  kParamDelayFeedback,
  kParamDelayTone,
  kParamDelayMix,
  kParamReverbOn,
  kParamReverbSize,
  kParamReverbDecay,
  kParamReverbTone,
  kParamReverbMix,
  kParamLoopsD,
  // Drifter (rudimentary: it drifts the effects chain)
  kParamDriftOn = kParamLoopsD + kNumLoops * kNumLoopParamsD,
  kParamDriftLength,
  kParamDriftCurve,
  kParamDriftSmear,
  kParamDriftReach,
  kParamDriftGravity,
  kParamDriftKeep,
  kParamDriftReturn,
  kParamInputLevel,   // the live (main) input onto the Tape side, like a loop's Level
  kParamInputSend,    // the live (main) input into the effects chain
  kParamLoopsE,       // loop blocks, section E
  // The fuller effects (2026-10-04): the standalone plugins' engines in the chain.
  kParamFxWarmth = kParamLoopsE + kNumLoops * kNumLoopParamsE, // on the chain's output
  kParamChorusMode,     // chorus, vibrato, ensemble
  kParamChorusVoices,   // 1 .. 4
  kParamChorusFeedback, // -90 .. 90%
  kParamDelayNote,      // Free (Time), or a note value synced to the host
  kParamDelayHeads,     // single, ping-pong, multi-head
  kParamReverbEngine,   // plate, hall
  kNumParams
};

// The settings Drifter moves.
constexpr int kDriftTargets[] = {kParamChorusRate, kParamChorusDepth, kParamChorusMix, kParamDelayTime, kParamDelayFeedback, kParamDelayTone,
                                 kParamDelayMix, kParamReverbSize, kParamReverbDecay, kParamReverbTone, kParamReverbMix};
constexpr int kNumDriftTargets = (int)(sizeof(kDriftTargets) / sizeof(kDriftTargets[0]));

constexpr int LoopParam(int loop, int p)
{
  return p < kNumLoopParamsA ? kParamLoops + loop * kNumLoopParamsA + p
       : p < kNumLoopParamsAB ? kParamLoopsB + loop * kNumLoopParamsB + (p - kNumLoopParamsA)
       : p < kNumLoopParamsABC ? kParamLoopsC + loop * kNumLoopParamsC + (p - kNumLoopParamsAB)
       : p < kNumLoopParamsABCD ? kParamLoopsD + loop * kNumLoopParamsD + (p - kNumLoopParamsABC)
                                : kParamLoopsE + loop * kNumLoopParamsE + (p - kNumLoopParamsABCD);
}

enum ECut { kCutNone = 0, kCutReverse, kCutRemove, kCutIsolate };

enum ECtrlTags
{
  kCtrlTagInputMeter = 0,
  kCtrlTagOutputMeter,
  kCtrlTagTape,                         // one per loop
  kCtrlTagFocusTitle = kCtrlTagTape + kNumLoops,
  kCtrlTagRowLabel,                     // four
  kCtrlTagDriftView = kCtrlTagRowLabel + 4,
  kNumCtrlTags
};

using namespace iplug;
using namespace igraphics;

class Splicer final : public Plugin
{
public:
  Splicer(const InstanceInfo& info);
  ~Splicer() override;

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
  static constexpr double kTapeRate = underheard::TapeLoop::kTapeRate;
  static constexpr int64_t kMaxTapeFrames = (int64_t)(4 * 60 * kTapeRate);
  // Storage is grown ahead of a take by this much: 5 s of recording at 2x.
  static constexpr int64_t kTapeHeadroom = (int64_t)(10 * kTapeRate);
  static constexpr double kCaptureSeconds = 32.; // a little more than the longest catch

  struct Loop
  {
    // Two tapes: one playing, one being filled by a load or catch on the main thread.
    std::unique_ptr<underheard::TapeStorage> tapes[2];
    std::unique_ptr<underheard::TapeLoop> engine;

    // Audio thread
    bool lastRecord = false, lastPlay = false, lastClear = false, lastCatch = false, lastRestore = false;
    bool lastRazor = false, lastCut[4] = {};
    int64_t markIn = -1, markOut = -1, markLength = 0;
    int markDir = 1;
    uint32_t seenMarkSeq = 0;
    double drySmoothed = 1., panSmoothed = 0., sendSmoothed = 0.;

    // Audio thread -> main thread
    std::atomic<int> fixRecord{-1}, fixPlay{-1}, fixClear{-1}, fixCatch{-1}, fixRestore{-1}; // button values to show
    std::atomic<int> fixRazor{-1}, fixCut[4] = {{-1}, {-1}, {-1}, {-1}};
    std::atomic<int64_t> markInShown{-1}, markOutShown{-1};
    // Main thread -> audio thread: marks drawn on the tape (-1 = cleared), new when the
    // sequence number changes.
    std::atomic<int64_t> uiMarkIn{-1}, uiMarkOut{-1};
    std::atomic<uint32_t> uiMarkSeq{0};
    std::atomic<int> cutOp{kCutNone};    // a cut to make (ECut)
    std::atomic<int64_t> cutIn{0}, cutOut{0}, cutLength{0};
    std::atomic<int> cutDir{1};
    std::atomic<bool> cutNeedsMarks{false}, loadRefused{false};
    std::atomic<int64_t> catchAt{-1};    // capture position of a Catch press
    std::atomic<int> catchSource{0};
    std::atomic<double> catchSpeed{1.};
    std::atomic<int64_t> framesInUse{0}, length{0};
    std::atomic<double> position{0.}, speed{0.};
    std::atomic<int> state{0}, activeTape{0}, passes{0}, wearPhase{0};
    std::atomic<uint64_t> version{0};

    // Main thread -> audio thread: a filled tape to switch to (-1 = none). The fields below it
    // are written before loadTape and read after it.
    std::atomic<int> loadTape{-1};
    std::atomic<int64_t> loadLength{0};
    std::atomic<bool> loadPlay{false};
    bool loadIsEdit = false;
    underheard::TapeEdit stageEdit;
    std::vector<float> stageAge, stageShed;
    int64_t stageSplices[underheard::TapeLoop::kMaxSplices] = {};
    int stageNumSplices = 0;

    // Main thread
    int inFlight = -1;             // tape handed to the audio thread, until it's active
    bool inFlightIsSaved = false;  // it came from the saved file, so it doesn't need saving again
    underheard::SavedTape saved;   // frames == 0: nothing saved (empty loop)
    uint64_t savedVersion = 0, seenVersion = 0;
    Clock::time_point changedAt{};
    std::string message;
    Clock::time_point messageAt{};
    bool messageSticky = false;
  };

  // Main thread
  void SyncFixes(int loop);
  void SetParamFromPlugin(int paramIdx, double value);
  int PrepareIdleTape(int loop);
  bool StartLoad(int loop, const float* stereo, int64_t frames, bool play, bool fromSave, const int64_t* splices = nullptr, int numSplices = 0);
  void DoCut(int loop, int op, int64_t in, int64_t out, int dir, int64_t length);
  void SetMarksFromUI(int loop, double in, double out);
  void FinishLoads(int loop);
  void DoCatch(int loop, int64_t at, int source, double speed);
  void LoadWavDialog(int loop);
  void LoadWavFile(int loop, const std::string& path);
  bool SaveLoop(int loop) const;
  void SetMessage(int loop, const std::string& msg, bool sticky = false);
  void UpdateStatus();
#if IPLUG_EDITOR
  void BuildUI(IGraphics* g);
  void SelectLoop(int loop);
#endif
  // Which loop the focus panel shows, or kFocusEffects / kFocusDrifter (UI only, not saved).
  static constexpr int kFocusEffects = kNumLoops, kFocusDrifter = kNumLoops + 1;
  int mSelectedLoop = 0;
  underheard::fx::FxChain mFx;

  // Drifter. Audio thread: the engine, button edges, and Keep's hand-over (while the host
  // catches up with a kept value, that value stands in for the setting).
  underheard::Drifter mDrifter{kNumDriftTargets};
  bool mLastDriftKeep = false, mLastDriftReturn = false;
  double mKeepOverride[kNumDriftTargets] = {};
  int mKeepOverrideBlocks[kNumDriftTargets] = {};
  // Audio thread -> main thread
  std::atomic<double> mDriftHome[kNumDriftTargets], mDriftNow[kNumDriftTargets];
  std::atomic<double> mDriftProgress{0.};
  std::atomic<bool> mKeepPending{false};
  std::atomic<double> mKeepValue[kNumDriftTargets];
  std::atomic<int> mFixDriftKeep{-1}, mFixDriftReturn{-1};

  // Audio thread
  void SyncTransport(int loop, ESync sync);

  mutable Loop mLoops[kNumLoops];
  underheard::CaptureBuffer mCapture[kNumSources];
  double mCaptureRate = 0.;
  std::string mTapeDir;

  std::atomic<bool> mResyncTransport{true};
  double mMixSmoothed = 0.5, mSmoothCoef = 1., mInputLevelSmoothed = 0., mInputSendSmoothed = 0.;

  WDL_String mDialogFile, mDialogPath;

  IPeakAvgSender<4> mInputPeakSender;
  IPeakAvgSender<2> mOutputPeakSender;
};
