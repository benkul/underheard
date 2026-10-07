#include "ReverbCore.h"

namespace underheard::fx {

namespace {
constexpr double kTankFeedSeconds = 0.3; // how long the wet sound feeds the freeze tank
constexpr double kTankGain = 3.2;        // the held level about matches the wet level before
} // namespace

void ReverbCore::Filter::Set(bool high, double hz, double fs)
{
  const double w = 2. * kPi * std::min(hz, 0.45 * fs) / fs, c = std::cos(w), al = std::sin(w) / (2. * 0.7071), a0 = 1. + al;
  b0 = (high ? (1. + c) : (1. - c)) / 2. / a0;
  b1 = (high ? -(1. + c) : (1. - c)) / a0;
  b2 = b0;
  a1 = -2. * c / a0;
  a2 = (1. - al) / a0;
}

void ReverbCore::Prepare(double fs, int maxBlock)
{
  mFs = fs;
  mMaxBlock = std::max(16, maxBlock);
  for (auto* v : {&mPreL, &mPreR, &mMono, &mWetL, &mWetR})
    v->assign((size_t)mMaxBlock, 0.f);
  mPreDelayL.Allocate((int)(kMaxPreDelay * fs) + 8);
  mPreDelayR.Allocate((int)(kMaxPreDelay * fs) + 8);
  mAlgo.Prepare(fs);
  mTank.SetEngine(AlgoReverb::kHall);
  mTank.SetBalance(1.);
  mTank.SetModulation(0.2);
  mTank.SetDecay(60.);
  mTank.Prepare(fs);
  mPreCoef = OnePoleCoef(0.05, fs);
  mGateCoef = OnePoleCoef(0.02, fs);
  mSmooth = OnePoleCoef(0.02, fs);
  mDuckAttack = OnePoleCoef(0.01, fs);
  mDuckRelease = OnePoleCoef(0.3, fs);
  mFadeStep = 1. / (0.03 * fs);
  mWarmth.Prepare(fs);
  mToneDirty = true;
  mStarting = true;
}

void ReverbCore::Reset()
{
  mPreDelayL.Clear();
  mPreDelayR.Clear();
  mAlgo.Reset();
  mTank.Reset();
  mTankActive = mTankHolding = false;
  mTankMix = 0.;
  for (auto& f : mHp) f.z1 = f.z2 = 0.;
  for (auto& f : mLp) f.z1 = f.z2 = 0.;
  mSoft[0] = mSoft[1] = 0.;
  mDuckEnv = 0.;
  mWarmth.Reset();
}

void ReverbCore::SetLowCut(double hz)
{
  hz = std::clamp(hz, 20., 2000.);
  if (hz != mLowCut) { mLowCut = hz; mToneDirty = true; }
}

void ReverbCore::SetHighCut(double hz)
{
  hz = std::clamp(hz, 1000., 20000.);
  if (hz != mHighCut) { mHighCut = hz; mToneDirty = true; }
}

void ReverbCore::SetAge(double amount)
{
  amount = std::clamp(amount, 0., 1.);
  if (amount != mAge) { mAge = amount; mToneDirty = true; }
}

void ReverbCore::UpdateTone()
{
  mToneDirty = false;
  for (auto& f : mHp) f.Set(true, mLowCut, mFs);
  for (auto& f : mLp) f.Set(false, mHighCut, mFs);
  // Age: a softer top on the way out, and the algorithmic tanks darken as they decay.
  mSoftK = mAge > 0. ? 1. - std::exp(-2. * kPi * std::min(20000. * std::pow(0.15, mAge), 0.45 * mFs) / mFs) : 1.; // 0: off
  mAlgo.SetDamping(9000. * std::pow(0.3, mAge));
}

void ReverbCore::Front(const float* inL, const float* inR, int n)
{
  if (mToneDirty)
    UpdateTone();
  if (mStarting)
  {
    mStarting = false;
    mPre = mPreTarget;
    mMix = mMixTarget;
    mEngine = mEngineTarget;
    mConvGate = mFreeze ? 0. : 1.;
  }
  mAlgo.SetFreeze(mFreeze);
  for (int i = 0; i < n; i++)
  {
    // Engine change: fade the wet out, switch, fade back in.
    if (mEngineTarget != mEngine)
    {
      mEngineGain -= mFadeStep;
      if (mEngineGain <= 0.)
      {
        mEngineGain = 0.;
        mEngine = mEngineTarget;
        mAlgo.SetEngine(mEngine == kPlate ? AlgoReverb::kPlate : AlgoReverb::kHall);
        mAlgo.Reset();
        mTank.Reset();
        mTankActive = mTankHolding = false;
        mTankMix = 0.;
      }
    }
    else if (mEngineGain < 1.)
      mEngineGain = std::min(1., mEngineGain + mFadeStep);

    mPre += (mPreTarget - mPre) * mPreCoef;
    mPreDelayL.Write(inL[i]);
    mPreDelayR.Write(inR[i]);
    const double d = 1. + mPre * mFs; // Read(1) is the sample just written
    mPreL[(size_t)i] = mPreDelayL.Read(d);
    mPreR[(size_t)i] = mPreDelayR.Read(d);
    // The convolver's input closes while frozen.
    mConvGate += ((mFreeze ? 0. : 1.) - mConvGate) * mGateCoef;
    if (mFreeze && mConvGate < 1e-4)
      mConvGate = 0.;
    mMono[(size_t)i] = (float)(0.5 * (mPreL[(size_t)i] + mPreR[(size_t)i]) * mConvGate);
  }
}

void ReverbCore::FreezeTank(int n)
{
  if (mFreeze && !mTankHolding)
  {
    // Freezing: feed the tank (long, not yet lossless) for a moment, then hold.
    if (!mTankActive)
    {
      mTank.Reset();
      mTank.SetDecay(60.);
      mTank.SetFreeze(false);
      mTankActive = true;
      mTankFeedLeft = kTankFeedSeconds * mFs;
    }
  }
  if (!mFreeze && mTankHolding)
  {
    // Released: the held sound fades out over a couple of seconds.
    mTankHolding = false;
    mTank.SetFreeze(false);
    mTank.SetDecay(2.);
    mTankIdle = 0.;
  }
  if (!mTankActive)
    return;
  double level = 0.;
  for (int i = 0; i < n; i++)
  {
    const bool feeding = mFreeze && mTankFeedLeft > 0.;
    float l, r;
    mTank.Process(feeding ? mWetL[(size_t)i] : 0.f, feeding ? mWetR[(size_t)i] : 0.f, l, r);
    if (feeding && --mTankFeedLeft <= 0.)
    {
      mTank.SetFreeze(true);
      mTankHolding = true;
    }
    // Crossfade the wet toward the tank while frozen (the convolver's tail is dying anyway).
    mTankMix += ((mFreeze ? 1. : 0.) - mTankMix) * mGateCoef * 0.25;
    const double a = mTankMix * kPi / 2.;
    mWetL[(size_t)i] = (float)(mWetL[(size_t)i] * std::cos(a) + l * kTankGain * (mFreeze ? std::sin(a) : 1.));
    mWetR[(size_t)i] = (float)(mWetR[(size_t)i] * std::cos(a) + r * kTankGain * (mFreeze ? std::sin(a) : 1.));
    level = std::max(level, (double)std::fabs(l) + std::fabs(r));
  }
  // Released and silent for a while: stop running the tank.
  if (!mFreeze)
  {
    mTankIdle = level < 1e-5 ? mTankIdle + n : 0.;
    if (mTankIdle > mFs)
    {
      mTankActive = false;
      mTank.Reset();
    }
  }
}

void ReverbCore::Back(const float* inL, const float* inR, float* outL, float* outR, int n)
{
  const double g = 1. + 2. * mAge;
  for (int i = 0; i < n; i++)
  {
    double w[2] = {mWetL[(size_t)i] * mEngineGain, mWetR[(size_t)i] * mEngineGain};
    for (int c = 0; c < 2; c++)
    {
      double x = w[c];
      if (mLowCut > 20.5)
        x = mHp[c].Run(x);
      if (mHighCut < 19999.)
        x = mLp[c].Run(x); // at the ends of their ranges, the cuts are off
      x = mSoft[c] += (x - mSoft[c]) * mSoftK;
      w[c] = mAge > 0. ? std::tanh(g * x) / g : x;
    }
    w[0] = mWarmth.Process(w[0], 0);
    w[1] = mWarmth.Process(w[1], 1);
    // Width: 0 mono, 1 as the engine made it, 1.5 wider.
    const double mid = 0.5 * (w[0] + w[1]), side = 0.5 * (w[0] - w[1]) * mWidth;
    // Duck: the reverb dips while you play.
    const double lvl = std::max(std::fabs(inL[i]), std::fabs(inR[i]));
    mDuckEnv += (lvl - mDuckEnv) * (lvl > mDuckEnv ? mDuckAttack : mDuckRelease);
    const double duck = 1. - mDuck * std::min(0.9, mDuckEnv * 4.);
    mMix += (mMixTarget - mMix) * mSmooth;
    const double a = mMix * kPi / 2.;
    outL[i] = (float)(inL[i] * std::cos(a) + (mid + side) * duck * std::sin(a));
    outR[i] = (float)(inR[i] * std::cos(a) + (mid - side) * duck * std::sin(a));
  }
}

} // namespace underheard::fx
