#include "DubDelay.h"

namespace underheard::fx {

namespace {
// Which heads each choice plays (bits: head 1 at 1/3, head 2 at 2/3, head 3 at the full time).
constexpr int kHeadMasks[DubDelay::kNumHeadChoices] = {1, 2, 4, 3, 5, 6, 7};
constexpr double kHeadRatio[3] = {1. / 3., 2. / 3., 1.};
} // namespace

void DubDelay::Biquad::Set(bool high, double hz, double q, double fs)
{
  const double w = 2. * kPi * std::min(hz, 0.45 * fs) / fs, c = std::cos(w), al = std::sin(w) / (2. * q), a0 = 1. + al;
  b0 = (high ? (1. + c) : (1. - c)) / 2. / a0;
  b1 = (high ? -(1. + c) : (1. - c)) / a0;
  b2 = b0;
  a1 = -2. * c / a0;
  a2 = (1. - al) / a0;
}

void DubDelay::Biquad::SetPeak(double hz, double q, double db, double fs)
{
  const double A = std::pow(10., db / 40.), w = 2. * kPi * std::min(hz, 0.45 * fs) / fs, c = std::cos(w), al = std::sin(w) / (2. * q);
  const double a0 = 1. + al / A;
  b0 = (1. + al * A) / a0;
  b1 = -2. * c / a0;
  b2 = (1. - al * A) / a0;
  a1 = b1;
  a2 = (1. - al / A) / a0;
}

void DubDelay::Prepare(double fs)
{
  mFs = fs;
  mL.Allocate((int)((kMaxSeconds * 1.02 + 0.05) * fs));
  mR.Allocate((int)((kMaxSeconds * 1.02 + 0.05) * fs));
  mSmooth = OnePoleCoef(0.02, fs);
  mSendCoef = OnePoleCoef(0.004, fs);
  mDuckAttack = OnePoleCoef(0.01, fs);
  mDuckRelease = OnePoleCoef(0.25, fs);
  if (mGlideCoef >= 1.)
    mGlideCoef = OnePoleCoef(0.15, fs);
  mWander.Set(fs, 0.3, 909);
  mGritK = 1. - std::exp(-2. * kPi * 2500. / fs);
  mWarmth.Prepare(fs);
  mStarting = true; // the first sample jumps to the settings (no glide or fade from defaults)
  mFiltersDirty = true;
}

void DubDelay::Reset()
{
  mL.Clear();
  mR.Clear();
  for (Biquad* f : {&mHpL, &mHpR, &mLpL, &mLpR, &mPeakL, &mPeakR})
    f->z1 = f->z2 = 0.;
  mSatL.Reset();
  mSatR.Reset();
  mWarmth.Reset();
  mAgeL = mAgeR = 0.;
}

void DubDelay::SetLowCut(double hz)
{
  hz = std::clamp(hz, 20., 2000.);
  if (hz != mLowCut) { mLowCut = hz; mFiltersDirty = true; }
}

void DubDelay::SetHighCut(double hz)
{
  hz = std::clamp(hz, 300., 20000.);
  if (hz != mHighCut) { mHighCut = hz; mFiltersDirty = true; }
}

void DubDelay::SetResonance(double amount)
{
  amount = std::clamp(amount, 0., 1.);
  if (amount != mResonance) { mResonance = amount; mFiltersDirty = true; }
}

void DubDelay::SetAge(double amount)
{
  amount = std::clamp(amount, 0., 1.);
  if (amount != mAge) { mAge = amount; mFiltersDirty = true; }
}

void DubDelay::UpdateFilters()
{
  mFiltersDirty = false;
  mHpL.Set(true, mLowCut, 0.7071, mFs);
  mHpR.Set(true, mLowCut, 0.7071, mFs);
  mLpL.Set(false, mHighCut, 0.7071, mFs);
  mLpR.Set(false, mHighCut, 0.7071, mFs);
  // Resonance is a peak at the high cut on the echoes, outside the loop: inside, the peak
  // would multiply the loop gain (+14 dB at 88% feedback runs away), and keeping it at unity
  // there would thin every repeat to a whistle. Out here it sweeps with High cut on every
  // repeat and can't feed on itself.
  mPeakL.SetPeak(mHighCut, 1.5 + 2.5 * mResonance, 12. * mResonance, mFs);
  mPeakR.SetPeak(mHighCut, 1.5 + 2.5 * mResonance, 12. * mResonance, mFs);
  // Age: a further gentle low-pass in the loop, so every repeat comes back a little darker.
  mAgeCoef = mAge > 0. ? 1. - std::exp(-2. * kPi * (16000. * std::pow(0.08, mAge)) / mFs) : 1.;
}

// One trip round the loop: polarity, saturation, filters, age.
double DubDelay::Loop(double x, Biquad& hp, Biquad& lp, double& ageState, SoftSaturator& sat, double& grit)
{
  x *= mPolarity;
  // Always some saturation, so runaway feedback stays bounded; anti-aliased, so driving it
  // doesn't fold harsh aliases into the repeats.
  const double g = 1. + 3. * mDrive;
  if (sat.drive != g)
    sat.Set(g, 0.);
  x = sat.Run(x); // tanh(g x) / g: unity for small signals, bounded at +/- 1/g
  x = lp.Run(hp.Run(x));
  ageState += (x - ageState) * mAgeCoef;
  grit += (mRand.Bipolar() - grit) * mGritK; // a little grit, low-passed: tape noise, not white hiss
  return ageState + mAge * 0.001 * grit;
}

void DubDelay::Process(float inL, float inR, float& outL, float& outR)
{
  if (mFiltersDirty)
    UpdateFilters();
  if (mStarting)
  {
    mStarting = false;
    mTime = mTimeTarget;
    mFeedback = mFeedbackTarget;
    mMix = mMixTarget;
    mSend = mSendOpen && !mFreeze ? 1. : 0.;
  }
  mTime += (mTimeTarget - mTime) * mGlideCoef;
  mFeedback += (mFeedbackTarget - mFeedback) * mSmooth;
  mMix += (mMixTarget - mMix) * mSmooth;
  mSend += ((mSendOpen && !mFreeze ? 1. : 0.) - mSend) * mSendCoef;

  // Tape wobble: a slow wow (sine plus wander) and a fast flutter; the right side runs
  // `Phase` ahead.
  mWowPhase += 2. * kPi * 0.55 / mFs;
  mFlutterPhase += 2. * kPi * 6.5 / mFs;
  if (mWowPhase > 2. * kPi) mWowPhase -= 2. * kPi;
  if (mFlutterPhase > 2. * kPi) mFlutterPhase -= 2. * kPi;
  const double wander = mWander.Next(1.5);
  auto wobble = [&](double offset) {
    return mWow * 0.004 * (0.7 * std::sin(mWowPhase + offset) + 0.3 * wander) + mFlutter * 0.0008 * std::sin(mFlutterPhase + offset);
  };
  const bool hold = mFreeze;
  const double tL = mTime * (hold ? 1. : 1. + wobble(0.));
  double tR = mTime * (hold ? 1. : 1. + wobble(mPhase));
  if (mMode != kPingPong)
    tR += mSpread * 0.02; // spread: the right side up to 20 ms later

  // Read the heads.
  double rL = 0., rR = 0., fbL = 0., fbR = 0.;
  if (mMode == kMultiHead)
  {
    const int mask = kHeadMasks[mHeads];
    int n = 0;
    for (int h = 0; h < 3; h++)
      if (mask & (1 << h))
      {
        rL += mL.Read(std::max(1., tL * kHeadRatio[h] * mFs));
        rR += mR.Read(std::max(1., tR * kHeadRatio[h] * mFs));
        n++;
      }
    // Heard at 1/sqrt(n) (about equal loudness); fed back as the average, because where the
    // heads line up in phase they add, and 1/sqrt(n) would let n heads feed back sqrt(n) times
    // the Feedback setting.
    fbL = rL / n;
    fbR = rR / n;
    const double norm = 1. / std::sqrt((double)n);
    rL *= norm;
    rR *= norm;
    if (hold)
    {
      // Frozen: only the full-length head recirculates, so the loop holds steady.
      fbL = mL.Read(tL * mFs);
      fbR = mR.Read(tR * mFs);
    }
  }
  else
  {
    rL = mL.Read(std::max(1., tL * mFs));
    rR = mR.Read(std::max(1., tR * mFs));
    // Ping-pong: each side's repeats feed the other.
    fbL = mMode == kPingPong ? rR : rL;
    fbR = mMode == kPingPong ? rL : rR;
  }

  // Write: new input (if the send is open) plus the repeats, through the loop.
  double wL, wR;
  if (hold)
  {
    wL = fbL;
    wR = fbR;
  }
  else
  {
    const double lL = Loop(fbL, mHpL, mLpL, mAgeL, mSatL, mGritL) * mFeedback;
    const double lR = Loop(fbR, mHpR, mLpR, mAgeR, mSatR, mGritR) * mFeedback;
    if (mMode == kPingPong)
    {
      wL = 0.5 * (inL + inR) * mSend + lL; // the input starts on one side and bounces
      wR = lR;
    }
    else
    {
      wL = inL * mSend + lL;
      wR = inR * mSend + lR;
    }
  }
  mL.Write((float)wL);
  mR.Write((float)wR);

  rL = mPeakL.Run(rL);
  rR = mPeakR.Run(rR);

  // Ping-pong width: at 0 the bounce is narrow, at 1 hard left and right.
  if (mMode == kPingPong)
  {
    const double w = 0.5 + 0.5 * mSpread;
    const double l = rL * w + rR * (1. - w), r = rR * w + rL * (1. - w);
    rL = l;
    rR = r;
  }

  // Warmth on the echoes (not the dry).
  {
    float wl = (float)rL, wr = (float)rR;
    mWarmth.Process(wl, wr);
    rL = wl;
    rR = wr;
  }

  // Duck: the repeats dip while you play.
  const double level = std::max(std::fabs(inL), std::fabs(inR));
  mDuckEnv += (level - mDuckEnv) * (level > mDuckEnv ? mDuckAttack : mDuckRelease);
  const double duck = 1. - mDuck * std::min(0.9, mDuckEnv * 4.);

  const double a = mMix * kPi / 2.;
  outL = (float)(inL * std::cos(a) + rL * duck * std::sin(a));
  outR = (float)(inR * std::cos(a) + rR * duck * std::sin(a));
}

} // namespace underheard::fx
