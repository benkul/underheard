#pragma once
// Drifter: slowly moves a set of values away from where they're set, to new places nearby, one
// shift after another.
//
// Each target has an offset from its home (the setting it belongs to), in normalised units
// (-1 .. 1, where 1 is the parameter's whole range). A shift moves every offset from where it
// is to a new random place over Length seconds, following the Curve. Smear staggers each
// target's start and duration, so a shift ripples through the set instead of moving in step.
// Reach sets how far a shift can go; Gravity pulls each new place back toward home.
//
// Real-time safe: fixed storage, no allocation. Framework-free.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace underheard {

class Drifter
{
public:
  static constexpr int kMaxTargets = 32;

  enum ECurve
  {
    kCurveLinear = 0,  // constant speed
    kCurveSmooth,      // eases in and out
    kCurveFastStart,   // moves off quickly, then settles
    kCurveSlowStart,   // creeps, then arrives quickly
    kNumCurves
  };

  explicit Drifter(int numTargets = 0, uint32_t seed = 0x5eed) { Reset(numTargets, seed); }

  void Reset(int numTargets, uint32_t seed = 0x5eed)
  {
    mCount = std::clamp(numTargets, 0, kMaxTargets);
    mRand = seed ? seed : 1;
    for (Target& t : mTargets)
      t = Target{};
    mPhase = 1.;
    mHolding = !mEnabled;
  }

  // Seconds per shift. Takes effect at once: the current shift keeps its progress.
  void SetLength(double seconds)
  {
    mLength = std::max(0.05, seconds);
    if (mShiftFollowsLength)
      mShiftLength = mLength;
  }
  void SetCurve(int curve) { mCurve = std::clamp(curve, 0, kNumCurves - 1); }
  void SetSmear(double amount) { mSmear = std::clamp(amount, 0., 1.); }
  void SetReach(double amount) { mReach = std::clamp(amount, 0., 1.); }
  void SetGravity(double amount) { mGravity = std::clamp(amount, 0., 1.); }

  // Off: glide home (over at most kHomeSeconds) and stay there. On: start drifting.
  void SetEnabled(bool on)
  {
    if (on == mEnabled)
      return;
    mEnabled = on;
    if (on)
    {
      mHolding = false;
      NewShift(false);
    }
    else
      GoHome(std::min(mLength, kHomeSeconds));
  }
  bool Enabled() const { return mEnabled; }

  // Glide home over one Length, then carry on drifting (if enabled).
  void Return() { GoHome(mLength); }

  // The current places become home (the caller has moved the settings there): offsets go to
  // zero without moving anything, and a new shift starts from there.
  void Rebase()
  {
    for (int i = 0; i < mCount; i++)
      mTargets[i] = Target{};
    if (mEnabled)
      NewShift(false);
    else
      mPhase = 1.;
  }

  void Advance(double seconds)
  {
    if (mCount == 0)
      return;
    if (mPhase < 1.)
    {
      mPhase = std::min(1., mPhase + seconds / mShiftLength);
      for (int i = 0; i < mCount; i++)
      {
        Target& t = mTargets[i];
        const double x = std::clamp((mPhase - t.start) / t.dur, 0., 1.);
        t.offset = t.from + (t.to - t.from) * Shape(x);
      }
    }
    if (mPhase >= 1.)
    {
      if (mGoingHome)
      {
        mGoingHome = false;
        mHolding = !mEnabled;
      }
      if (mEnabled && !mHolding)
        NewShift(false);
    }
  }

  int Count() const { return mCount; }
  double Offset(int i) const { return mTargets[i].offset; }
  double Progress() const { return mPhase; }  // of the current shift, 0 .. 1
  bool GoingHome() const { return mGoingHome; }

  double Shape(double x) const
  {
    switch (mCurve)
    {
      case kCurveSmooth: return x * x * (3. - 2. * x);
      case kCurveFastStart: return 1. - (1. - x) * (1. - x) * (1. - x);
      case kCurveSlowStart: return x * x * x;
      default: return x;
    }
  }

private:
  static constexpr double kHomeSeconds = 2.;

  struct Target
  {
    double offset = 0., from = 0., to = 0.;
    double start = 0., dur = 1.; // as fractions of the shift
  };

  double Uniform() // 0 .. 1
  {
    mRand = mRand * 1664525u + 1013904223u;
    return (double)(mRand >> 8) / 16777216.;
  }

  void GoHome(double seconds)
  {
    mGoingHome = true;
    mHolding = false;
    NewShift(true, seconds);
  }

  // A shift of `seconds` (0: follow Length, including later changes to it).
  void NewShift(bool home, double seconds = 0.)
  {
    mShiftFollowsLength = seconds <= 0. || seconds == mLength;
    mShiftLength = mShiftFollowsLength ? mLength : seconds;
    mPhase = 0.;
    for (int i = 0; i < mCount; i++)
    {
      Target& t = mTargets[i];
      t.from = t.offset;
      if (home)
        t.to = 0.;
      else
      {
        // Gravity pulls the new place toward home; Reach is how far a shift can wander.
        const double pulled = t.offset * (1. - mGravity);
        t.to = std::clamp(pulled + mReach * (2. * Uniform() - 1.), -1., 1.);
      }
      // Smear: each target starts somewhere in the first half of the shift (at most) and takes
      // its own time, finishing by the end.
      t.start = mSmear * 0.5 * Uniform();
      t.dur = std::max(0.05, (1. - t.start) * (1. - mSmear * 0.4 * Uniform()));
    }
  }

  Target mTargets[kMaxTargets];
  int mCount = 0;
  uint32_t mRand = 1;
  double mLength = 20., mShiftLength = 20., mSmear = 0.3, mReach = 0.3, mGravity = 0.3;
  int mCurve = kCurveSmooth;
  double mPhase = 1.;
  bool mEnabled = false, mGoingHome = false, mHolding = true, mShiftFollowsLength = true;
};

} // namespace underheard
