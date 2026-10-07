#pragma once
// underheard-delay: a tape echo that flexes into dub.
//
// One tape loop per side. Playback can be a single head, ping-pong between the sides, or three
// heads at 1/3, 2/3 and the full time (Space Echo-style). Every repeat goes back onto the tape
// through the loop: polarity, saturation (always a little, so feedback past 100% runs away
// musically instead of exploding), low cut, high cut, and Age (each repeat darker and
// grittier). Resonance is a peak at the high cut on the echoes, outside the loop, for dub
// filter sweeps that can't run away. Time changes glide like tape, bending the pitch.
// Throw gates what goes in; Freeze holds the loop unchanged.
//
// Prepare() allocates; everything else is real-time safe.

#include "FxCommon.h"
#include "Warmth.h"

namespace underheard::fx {

class DubDelay
{
public:
  enum Mode { kSingle = 0, kPingPong, kMultiHead };
  static constexpr double kMaxSeconds = 2.0;
  static constexpr int kNumHeadChoices = 7; // 1, 2, 3, 1+2, 1+3, 2+3, 1+2+3

  void Prepare(double sampleRate);
  void Reset();

  void SetTime(double seconds) { mTimeTarget = std::clamp(seconds, 0.001, kMaxSeconds); }
  void SetGlide(double seconds) { mGlideCoef = OnePoleCoef(std::max(0.001, seconds), mFs); }
  void SetFeedback(double amount) { mFeedbackTarget = std::clamp(amount, 0., 1.1); }
  void SetPolarity(bool inverted) { mPolarity = inverted ? -1. : 1.; }
  void SetMode(int mode) { mMode = std::clamp(mode, 0, 2); }
  void SetHeads(int choice) { mHeads = std::clamp(choice, 0, kNumHeadChoices - 1); }
  void SetLowCut(double hz);
  void SetHighCut(double hz);
  void SetResonance(double amount);
  void SetDrive(double amount) { mDrive = std::clamp(amount, 0., 1.); }
  void SetAge(double amount);
  void SetWow(double amount) { mWow = std::clamp(amount, 0., 1.); }
  void SetFlutter(double amount) { mFlutter = std::clamp(amount, 0., 1.); }
  void SetPhase(double degrees) { mPhase = degrees * kPi / 180.; }
  void SetSpread(double amount) { mSpread = std::clamp(amount, 0., 1.); }
  void SetFreeze(bool on) { mFreeze = on; }
  void SetSendOpen(bool open) { mSendOpen = open; } // false: nothing new goes in (Throw released)
  void SetDuck(double amount) { mDuck = std::clamp(amount, 0., 1.); }
  void SetMix(double amount) { mMixTarget = std::clamp(amount, 0., 1.); }
  void SetWarmth(double amount) { mWarmth.SetAmount(amount); } // on the echoes

  void Process(float inL, float inR, float& outL, float& outR);

  double CurrentTime() const { return mTime; } // seconds, gliding toward the target

private:
  struct Biquad
  {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void Set(bool high, double hz, double q, double fs);
    void SetPeak(double hz, double q, double db, double fs);
    double Run(double x)
    {
      const double y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
    }
  };
  double Loop(double x, Biquad& hp, Biquad& lp, double& ageState, SoftSaturator& sat, double& grit);
  void UpdateFilters();

  double mFs = 48000.;
  DelayLine mL, mR;
  double mTime = 0.375, mTimeTarget = 0.375, mGlideCoef = 1.;
  double mFeedback = 0.45, mFeedbackTarget = 0.45, mPolarity = 1.;
  int mMode = kSingle, mHeads = 2;
  double mLowCut = 120., mHighCut = 5000., mResonance = 0.1, mAge = 0.3, mDrive = 0.3;
  double mWow = 0.2, mFlutter = 0.15, mPhase = 0., mSpread = 0.;
  bool mFreeze = false, mSendOpen = true;
  double mSend = 1., mSendCoef = 1., mDuck = 0., mDuckEnv = 0., mDuckAttack = 0., mDuckRelease = 0.;
  double mMix = 0.35, mMixTarget = 0.35, mSmooth = 1.;
  double mWowPhase = 0., mFlutterPhase = 0.;
  Wander mWander;
  Biquad mHpL, mHpR, mLpL, mLpR, mPeakL, mPeakR;
  double mAgeL = 0., mAgeR = 0., mAgeCoef = 1.;
  Rand mRand;
  bool mFiltersDirty = true, mStarting = true;
  SoftSaturator mSatL, mSatR;
  double mGritL = 0., mGritR = 0., mGritK = 1.;
  Warmth mWarmth;
};

} // namespace underheard::fx
