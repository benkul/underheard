#pragma once
// One loop of tape: a record head and a play head over a TapeStorage, driven by a motor.
//
// Positions are in tape frames at the fixed tape rate (48 kHz). The tape moves
// speed * kTapeRate / hostRate frames per host sample, so speed changes pitch and time
// together, and recording at one speed then playing at another transposes, like real tape.
//
// Wear is kept as damage per 5 ms segment of tape (age and oxide loss) instead of being
// written into the audio. Each pass of the head ages the segments it crosses; playback
// applies the damage (HF loss, saturation, dropouts, crackle, print-through, hiss). The audio
// itself stays clean, which makes Restore instant and means the saved tape is the clean one.
//
// All methods except the constructor run on the audio thread and never allocate.

#include "Filters.h"
#include "TapeEdit.h"
#include "TapeStorage.h"

#include <cstdint>
#include <vector>

namespace underheard {

class TapeLoop
{
public:
  enum class State
  {
    Empty,       // no tape
    Recording,   // first take; the loop length isn't known yet
    Closing,     // first take ended; recording a short post-roll to crossfade the seam
    Playing,
    Overdubbing,
    Stopped,     // motor stopping or stopped
    Clearing,    // fading out before becoming Empty
    Loading,     // fading out before switching to new tape (see RequestLoad)
    Editing      // fading out before switching to a cut version of this tape (see RequestEdit)
  };

  static constexpr double kTapeRate = 48000.;
  static constexpr int kHeadGap = 8;            // tape frames between the record and play heads
  static constexpr int kSeamFrames = 480;       // seam crossfade when a first take closes (10 ms)
  static constexpr int kSpliceFrames = 240;     // width of the splice dropout (5 ms)
  static constexpr int64_t kMinLoopFrames = 2400; // takes shorter than 50 ms are discarded
  static constexpr int kSegmentFrames = 240;    // wear resolution (5 ms)
  static constexpr int kPrintThroughFrames = 1440; // print-through ghost offset (30 ms)
  static constexpr int kJoinFrames = 96;        // crossfade at a razor cut (2 ms)
  static constexpr int kMaxSplices = 64;        // splices from cuts, besides the seam

  // `storage` sets the maximum length; every storage used later must have the same maximum.
  explicit TapeLoop(TapeStorage* storage);

  void SetHostRate(double sampleRate);

  // Transport. Idempotent, so they can be called with the current button state.
  void SetRecord(bool on);
  void SetPlay(bool on);
  void Clear();

  // Ends the first take at exactly `lengthFrames`: now, if that much is already recorded
  // (the extra becomes the seam's post-roll), or once the record head gets there.
  void CloseTakeAt(int64_t lengthFrames);

  // Switches to `storage`, which holds `lengthFrames` of finished tape, with optional splice
  // positions from earlier cuts. The current tape fades out first. The previous storage is no
  // longer touched once Storage() == storage.
  void RequestLoad(TapeStorage* storage, int64_t lengthFrames, bool play, const int64_t* splices = nullptr, int numSplices = 0);

  // Switches to `storage`, which holds this loop after a razor `edit` (see RenderEdit). The
  // play head carries on from the matching spot. `age` and `shed` are the edited damage per
  // segment, read when the switch happens, so they must stay put until Storage() == storage.
  void RequestEdit(TapeStorage* storage, const TapeEdit& edit, const float* age, const float* shed, const int64_t* splices, int numSplices);

  // Removes all wear: the tape plays as recorded again.
  void Restore();

  // Tape controls.
  void SetSpeed(double ratio);        // > 0, e.g. 0.25 .. 2
  void SetReverse(bool on);
  void SetMotorTime(double seconds);  // start/stop ramp; 0 = instant
  void SetErase(double amount);       // 0 .. 1: how much old tape an overdub removes
  void SetFeedback(double amount);    // 0 .. 1: how much of the loop survives each pass
  void SetSplice(double amount);      // 0 .. 1: splice click and dropout at the seam
  void SetWear(bool on);              // whether passes age the tape (damage stays when off)
  void SetWearRate(double amount);    // 0 .. 1
  // The terminal point, as a mean age (0 = new, about 4 = as worn as it gets). When the tape
  // gets there it holds, or with Recover on it heals at the wear rate until it's new again,
  // then wears again.
  void SetWearLimit(double age);
  void SetRecover(bool on);
  void SetWow(double amount);         // 0 .. 1: slow, irregular speed drift
  void SetFlutter(double amount);     // 0 .. 1: fast speed wobble
  void SetHiss(double amount);        // 0 .. 1: tape noise, rising with age and at low speed

  void Process(float inL, float inR, float& outL, float& outR);

  // Status, for the plugin and UI.
  State GetState() const { return mState; }
  // True while the record button should show as on (a scheduled close counts as off).
  bool IsRecordingState() const { return (mState == State::Recording && mCloseAt == 0) || mState == State::Overdubbing; }
  bool IsTransportRunning() const;
  int64_t LengthFrames() const { return mLength; }
  int64_t TakeFrames() const { return mTakeFrames; }
  double PlayPosition() const;        // tape frames from the seam
  double TapeSpeed() const { return mSpeed; } // signed, including the motor
  int64_t FramesInUse() const;        // for growing the storage ahead of a take
  TapeStorage* Storage() const { return mTape; }
  uint64_t Version() const { return mVersion; } // changes whenever the tape's audio does
  int Passes() const { return mPasses; }  // passes worn since the last restore
  int Direction() const { return mDir; }  // +1 forward, -1 backward (the last way it moved)
  const float* AgeData() const { return mAge.data(); }   // per segment
  const float* ShedData() const { return mShed.data(); } // per segment
  size_t SegmentCapacity() const { return mAge.size(); }
  int NumSplices() const { return mNumSplices; }
  const int64_t* Splices() const { return mSplices; }
  double MeanAge() const;             // average segment age (0 = new)
  enum class WearPhase { Wearing, Holding, Recovering };
  WearPhase GetWearPhase() const { return mWearPhase; }

private:
  bool Closed() const { return mLength > 0 && mState != State::Recording && mState != State::Closing; }
  void BeginClose(int64_t length);
  void BlendSeam(int64_t j, float l, float r);
  void FinishClose(State next);
  void FinishPending();
  void ApplyLoad();
  void ApplyEdit();
  void SetPendingSplices(const int64_t* splices, int n);
  void ResetWear();
  double Wrap(double p) const;
  int64_t WrapIndex(int64_t i) const;
  void ReadTape(double pos, float& l, float& r);
  void ReadTapeBandLimited(double pos, double stretch, float& l, float& r);
  void WearSegment(int64_t seg);
  void UpdateWearPhase();
  void SegmentDamage(double pos, double& age, double& shed) const;
  float Noise();

  TapeStorage* mTape;
  double mHostRate = 48000.;

  State mState = State::Empty;
  int64_t mLength = 0;      // loop length in frames, once closed
  int64_t mTakeFrames = 0;  // frames written during the first take
  int64_t mCloseAt = 0;     // scheduled end of the first take (0 = none)
  double mWritePos = 0.;    // record head, tape frames
  int mDir = 1;             // last direction of travel
  double mLastReadPos = 0.;
  uint64_t mVersion = 0;

  TapeStorage* mPendingTape = nullptr;
  int64_t mPendingLength = 0;
  bool mPendingPlay = false;
  TapeEdit mPendingEdit;
  const float* mPendingAge = nullptr;
  const float* mPendingShed = nullptr;
  State mStateBeforeEdit = State::Playing;
  int64_t mPendingSplices[kMaxSplices] = {};
  int mPendingNumSplices = 0;
  int64_t mSplices[kMaxSplices] = {};
  int mNumSplices = 0;

  // Controls
  double mSpeedRatio = 1., mMotorTime = 0.4, mErase = 0., mFeedback = 1., mSplice = 0.3;
  double mWearRate = 0.25, mWow = 0., mFlutter = 0., mHiss = 0., mWearLimit = 1e9;
  bool mReverse = false, mWearOn = false, mRecover = false;
  WearPhase mWearPhase = WearPhase::Wearing;
  int mSegmentsSincePhaseCheck = 0;

  // Motor and smoothing
  double mMotor = 0., mMotorStep = 1.;
  double mSpeedSmoothed = 1., mSpeedCoef = 1., mSpeed = 0.;
  double mEraseSmoothed = 0., mSmoothCoef = 1.;
  double mRecEnv = 0., mRecEnvStep = 1.;
  double mPlayEnv = 0., mPlayEnvStep = 1.;

  // Wow and flutter
  double mWowTarget = 0., mWow1 = 0., mWow2 = 0., mWowCoef = 0., mWowTimer = 0.;
  double mFlutterPhase = 0., mFlutterRate = 8., mFlutterNoise = 0., mFlutterCoef = 0.;

  // Wear: per segment of tape
  std::vector<float> mAge, mShed;
  int64_t mLastWriteSeg = -1;
  int mPasses = 0;

  // Record path: anti-alias filter (for slow tape) and input history for interpolated writes.
  LowPass4 mAaL, mAaR;
  bool mAaBypass = true;
  int mAaCounter = 0;
  float mHistL[4] = {}, mHistR[4] = {};

  // Playback path
  Biquad mAgeL, mAgeR;      // generation loss: the recording dulls as the tape ages
  Biquad mHeadL, mHeadR;    // head gap loss: everything dulls at low speed
  int mToneCounter = 0;
  DcBlocker mDcL, mDcR;
  double mClickEnv = 0., mClickDecay = 0.;
  // Crackle: soft pops (a short burst through a gently resonant low-pass) as shed tape passes.
  double mPopEnv = 0., mPopAmp = 0., mPopDecay = 0.;
  Biquad mPopFilter;
  uint32_t mNoise = 22222;
};

} // namespace underheard
