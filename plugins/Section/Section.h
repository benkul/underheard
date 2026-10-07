#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "ISender.h"
#include "IPlugQueue.h"

#include "Convolver.h"
#include "SectionEngine.h"
#include "Warmth.h"
#include "WavetableData.h"
#if IPLUG_EDITOR
#include "RoomView.h"
#endif

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

const int kNumPresets = 1; // the default string section (docs/SECTION-SPEC.md: only the default for now)

// Order is saved state, but the state stores a count, so parameters can be appended.
// (Version 2: the redesign. Version 1 projects open with defaults.)
enum EParams
{
  kParamOutput = 0,
  // VOICES
  kParamPlayers,
  kParamAPosition, kParamAOctave, kParamASemi, kParamAFine, kParamALevel,
  kParamBPosition, kParamBOctave, kParamBSemi, kParamBFine, kParamBLevel,
  kParamFilterType, kParamCutoff, kParamResonance, kParamKeyTrack, kParamFilterEnvAmount, kParamFilterVelocity,
  kParamFAttack, kParamFDecay, kParamFSustain, kParamFRelease,
  kParamAAttack, kParamADecay, kParamASustain, kParamARelease, kParamAmpVelocity,
  kParamWheelTarget, kParamWheelAmount, kParamTouchTarget, kParamTouchAmount,
  // EFFECTS
  kParamLooseness, kParamSmear, kParamSettle, kParamVibrato, kParamOnset,
  kParamCharPitch, kParamCharPosition, kParamCharCutoff,
  kParamDriftPitch, kParamDriftPosition, kParamDriftCutoff,
  kParamNoiseAmount, kParamNoiseTone, kParamBody, kParamBodyDepth, kParamWarmth,
  // ROOM
  kParamRoomAmount, kParamRoom, kParamSize, kParamSurfaces, kParamDistance, kParamMic, kParamPair, kParamWidth,
  kNumParams
};

enum ECtrlTags
{
  kCtrlTagOutputMeter = 0,
  kCtrlTagStatus,
  kCtrlTagInfo,
  kCtrlTagRoomView,
  kCtrlTagKeyboard,
  kCtrlTagTableA,
  kCtrlTagTableB,
  kNumCtrlTags
};

using namespace iplug;
using namespace igraphics;

// See docs/SECTION-SPEC.md.
class Section final : public Plugin
{
public:
  Section(const InstanceInfo& info);
  ~Section() override;

  void OnIdle() override;
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;
#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
#endif

  // An oscillator's table: a factory table, or a file the user loaded (kept as a copy in
  // ~/Music/Underheard/Section/, found by path, then by its content hash).
  struct TableSource
  {
    int factory = -1;   // a factory table, or -1 for a file
    std::string path;   // the copy's path
    uint64_t hash = 0;  // of the file's bytes
    std::string name;   // what to show (the original file's name, or the factory table's)
  };

private:
  using Clock = std::chrono::steady_clock;
  struct LoadedTable
  {
    TableSource source;
    std::unique_ptr<underheard::wavetable::WavetableData> table;
  };

  // Main thread
  void RebuildRoom();
  bool RoomSettingsChanged();
  double DistanceMetres() const;
  void ApplyTemplate(int index);
  void UpdateStatus();
  void SetMessage(const std::string& msg, bool sticky = false);
  void LoadTableDialog(int osc);
  bool LoadTableFile(int osc, const std::string& path);           // the user's file: copied, then used
  bool UseTableSource(int osc, const TableSource& source);         // a factory table or a stored copy
  void ChooseTable(int osc, int menuIndex);                        // from the oscillator's table menu
  std::vector<std::string> TableMenuItems() const;
  void RetireUnusedTables();
#if IPLUG_EDITOR
  roombleed_ui::RoomViewData MakeRoomView() const;
#endif

  underheard::room::ConvolverSwitch mDeskRooms[underheard::section::kNumDesks];
  std::vector<double> mBuiltSettings;
  Clock::time_point mSettingsChangedAt{}, mMessageAt{};
  bool mRebuildPending = true, mMessageSticky = false;
  std::string mInfo, mMessage, mTapeDir;
  WDL_String mDialogFile, mDialogPath;

  // Tables: the factory ones (built once), and files loaded into this instance (newest last).
  std::vector<std::unique_ptr<underheard::wavetable::WavetableData>> mFactory;
  std::vector<LoadedTable> mLoaded;
  TableSource mSource[2];
  // Handing tables to the audio thread: the table each oscillator should play, and a table the
  // main thread wants to free (the audio thread lets go of it, then says so).
  std::atomic<const underheard::wavetable::WavetableData*> mOscTable[2]{{nullptr}, {nullptr}};
  std::atomic<const underheard::wavetable::WavetableData*> mRetireRequest{nullptr}, mRetired{nullptr};

  // Audio thread
  void ApplySettings();
  void HandleMidi(const IMidiMsg& msg);
  underheard::section::Engine mEngine;
  underheard::fx::Warmth mWarmth;
  IMidiQueue mMidiQueue;
  std::vector<float> mDesk[underheard::section::kNumDesks], mRoomL, mRoomR, mTmpL, mTmpR;
  double mRoomSmoothed = 0.5, mSmoothCoef = 1.;

  IPeakAvgSender<2> mOutputPeakSender;
};
