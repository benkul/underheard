#pragma once
// underheard-reverb's algorithmic engines.
//
// - Plate: Dattorro's plate (input diffusion, then a figure-of-eight tank of two halves with
//   modulated allpasses and damping), scaled to the sample rate and Size.
// - Hall: an 8-line feedback delay network with a Householder matrix, per-line damping and
//   slowly modulated lines, after a stereo input diffuser.
//
// Both have early reflections (a multi-tap line: short and dense for the plate, wider for the
// hall) and Early/Late balance. Output is level-normalised: the impulse response has about unit
// energy whatever the decay and size, like the convolution engines' prepared impulses.
//
// Freeze makes the tank lossless (no decay, no damping) and closes its input, so it holds.
// Prepare() allocates; everything else is real-time safe.

#include "FxCommon.h"

#include <array>

namespace underheard::fx {

class AlgoReverb
{
public:
  enum Engine { kPlate = 0, kHall };

  void Prepare(double sampleRate);
  void Reset();

  void SetEngine(int engine);
  void SetDecay(double seconds) { mDecay = std::clamp(seconds, 0.1, 60.); mDirty = true; }
  void SetSize(double scale);                 // 0.5 .. 2 (applied on Reset/Prepare and engine change)
  void SetDamping(double hz) { mDampHz = std::clamp(hz, 500., 20000.); mDirty = true; }
  void SetModulation(double amount) { mMod = std::clamp(amount, 0., 1.); }
  void SetBalance(double earlyToLate);        // -1 early only .. 0 both .. 1 late only
  void SetFreeze(bool on) { mFreeze = on; mDirty = true; }

  void Process(float inL, float inR, float& outL, float& outR);

  double Decay() const { return mDecay; }

private:
  struct Line // a delay with fractional, modulated reads
  {
    DelayLine d;
    double length = 0.;
  };
  struct Allpass
  {
    DelayLine d;
    int length = 1;
    double g = 0.5;
    double Run(double x)
    {
      const double v = d.ReadInt(length);
      const double w = x + g * v;
      d.Write((float)w);
      return v - g * w;
    }
    // Modulated: the read point moves by `offset` samples.
    double RunMod(double x, double offset)
    {
      const double v = d.Read(std::max(1., length + offset));
      const double w = x - g * v; // Dattorro's tank allpasses use the opposite sign
      d.Write((float)w);
      return v + g * w;
    }
  };
  struct OnePole
  {
    double z = 0., k = 0.;
    double Run(double x) { return z += (x - z) * k; }
  };

  void Build();      // delay lengths for the engine, size and rate
  void UpdateGains();
  void ProcessPlate(double inL, double inR, double& lateL, double& lateR);
  void ProcessHall(double inL, double inR, double& lateL, double& lateR);

  double mFs = 48000.;
  int mEngine = kHall;
  double mDecay = 2.5, mSize = 1., mDampHz = 7000., mMod = 0.3, mModNow = 0.3;
  double mEarly = 1., mLate = 1., mBalanceGain = 0.7071;
  bool mFreeze = false, mDirty = true;
  double mInGate = 1., mGateCoef = 1.;
  double mLateNorm = 1.;

  // Early reflections
  DelayLine mEr;
  static constexpr int kTaps = 12;
  std::array<double, kTaps> mTapTime{}, mTapGainL{}, mTapGainR{};

  // Plate
  OnePole mBandwidth;
  std::array<Allpass, 4> mInDiff;
  Allpass mModAp[2], mDecayAp[2];
  DelayLine mTankDelay1[2], mTankDelay2[2];
  int mTankLen1[2] = {}, mTankLen2[2] = {};
  OnePole mPlateDamp[2];
  double mPlateFeed[2] = {};
  double mPlateGain = 0.5;
  double mScale = 1.; // plate: samples per Dattorro sample (29761 Hz) times size

  // Hall
  static constexpr int kLines = 8;
  std::array<Line, kLines> mLines;
  std::array<OnePole, kLines> mHallDamp;
  std::array<double, kLines> mLineGain{}, mLineState{};
  std::array<Allpass, 4> mDiffL, mDiffR;

  // Modulation
  std::array<double, kLines> mModPhase{}, mModRate{};
  Wander mWander;
};

} // namespace underheard::fx
