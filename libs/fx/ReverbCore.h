#pragma once
// underheard-reverb's signal path, around whichever engine is chosen:
//
//   input -> pre-delay -> engine -> low cut, high cut -> Age (softer top, saturation)
//         -> width -> duck -> mix with the dry signal
//
// Engines: Rooms and Recordings are convolution (the caller does it: Process() takes a
// function that convolves a mono block into a stereo one), Plate and Hall are AlgoReverb.
// Changing engine fades the wet out and back in.
//
// Freeze: the algorithmic engines hold their own tanks. A convolver can't, so for Rooms and
// Recordings the wet sound is fed into a lossless hall tank for a moment and held there, while
// the convolver's input closes; on release the tank fades out.
//
// Prepare() allocates; everything else is real-time safe.

#include "AlgoReverb.h"
#include "Warmth.h"

namespace underheard::fx {

class ReverbCore
{
public:
  enum Engine { kRooms = 0, kRecordings, kPlate, kHall };
  static constexpr double kMaxPreDelay = 1.0;

  void Prepare(double sampleRate, int maxBlock);
  void Reset();

  void SetEngine(int engine) { mEngineTarget = std::clamp(engine, 0, 3); }
  void SetPreDelay(double seconds) { mPreTarget = std::clamp(seconds, 0., kMaxPreDelay); }
  void SetDecay(double seconds) { mAlgo.SetDecay(seconds); }       // Plate and Hall
  void SetSize(double scale) { mAlgo.SetSize(scale); }             // Plate and Hall
  void SetBalance(double earlyToLate) { mAlgo.SetBalance(earlyToLate); }
  void SetModulation(double amount) { mAlgo.SetModulation(amount); }
  void SetLowCut(double hz);
  void SetHighCut(double hz);
  void SetAge(double amount);
  void SetWidth(double amount) { mWidth = std::clamp(amount, 0., 1.5); }
  void SetFreeze(bool on) { mFreeze = on; }
  void SetDuck(double amount) { mDuck = std::clamp(amount, 0., 1.); }
  void SetMix(double amount) { mMixTarget = std::clamp(amount, 0., 1.); }
  void SetWarmth(double amount) { mWarmth.SetAmount(amount); } // on the reverb

  // `convolve(const float* mono, float* l, float* r, int n)` runs the convolution engines.
  template <class Convolve>
  void Process(const float* inL, const float* inR, float* outL, float* outR, int n, Convolve&& convolve)
  {
    for (int start = 0; start < n;)
    {
      const int m = std::min(n - start, mMaxBlock);
      Front(inL + start, inR + start, m);
      if (IsConvolution(mEngine))
      {
        convolve(mMono.data(), mWetL.data(), mWetR.data(), m);
        FreezeTank(m);
      }
      else
        for (int i = 0; i < m; i++)
          mAlgo.Process(mPreL[(size_t)i], mPreR[(size_t)i], mWetL[(size_t)i], mWetR[(size_t)i]);
      Back(inL + start, inR + start, outL + start, outR + start, m);
      start += m;
    }
  }

  int Engine() const { return mEngine; }
  static bool IsConvolution(int engine) { return engine == kRooms || engine == kRecordings; }

private:
  void Front(const float* inL, const float* inR, int n); // pre-delay, engine switching, the convolver's input
  void FreezeTank(int n);                                // Freeze for the convolution engines
  void Back(const float* inL, const float* inR, float* outL, float* outR, int n);
  void UpdateTone();

  struct Filter
  {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void Set(bool high, double hz, double fs);
    double Run(double x)
    {
      const double y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
    }
  };

  double mFs = 48000.;
  int mMaxBlock = 512;
  int mEngine = kHall, mEngineTarget = kHall;
  double mEngineGain = 1., mFadeStep = 0.;
  AlgoReverb mAlgo, mTank;
  DelayLine mPreDelayL, mPreDelayR;
  double mPre = 0.02, mPreTarget = 0.02, mPreCoef = 1.;
  std::vector<float> mPreL, mPreR, mMono, mWetL, mWetR;

  // Freeze
  bool mFreeze = false, mTankActive = false, mTankHolding = false;
  double mConvGate = 1., mGateCoef = 1., mTankMix = 0., mTankFeedLeft = 0., mTankIdle = 0.;

  // After the engine
  double mLowCut = 80., mHighCut = 16000., mAge = 0.2, mWidth = 1., mDuck = 0.;
  bool mToneDirty = true;
  Filter mHp[2], mLp[2];
  double mSoftK = 1., mSoft[2] = {0., 0.};
  double mDuckEnv = 0., mDuckAttack = 0., mDuckRelease = 0.;
  double mMix = 0.3, mMixTarget = 0.3, mSmooth = 1.;
  bool mStarting = true;
  Warmth mWarmth;
};

} // namespace underheard::fx
