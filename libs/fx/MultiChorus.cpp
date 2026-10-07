#include "MultiChorus.h"

namespace underheard::fx {

void MultiChorus::Prepare(double fs)
{
  mFs = fs;
  for (auto& l : mLine)
    l.Allocate((int)((kMaxDelayMs + kMaxSwingMs + 2.) / 1000. * fs) + 8);
  for (int s = 0; s < 2; s++)
  {
    for (int v = 0; v < kMaxVoices; v++)
      mWander[s][v].Set(fs, 0.3, 1000u + 77u * (uint32_t)(s * kMaxVoices + v));
    mWarble[s].Set(fs, 0.12, 5150u + (uint32_t)s);
  }
  mSmooth = OnePoleCoef(0.02, fs);
  mDelaySmooth = OnePoleCoef(0.08, fs);
  SetRate(mRate);
  mWarmth.Prepare(fs);
  mToneDirty = true;
  mStarting = true;
}

void MultiChorus::Reset()
{
  for (auto& l : mLine)
    l.Clear();
  for (auto& f : mToneF)
    f.Reset();
  mFbState[0] = mFbState[1] = 0.;
  mWarmth.Reset();
}

void MultiChorus::SetRate(double hz)
{
  mRate = std::clamp(hz, 0.01, 20.);
  // Wander: a new target about twice a cycle, smoothed over a third of a cycle.
  const double c = OnePoleCoef(std::clamp(0.33 / mRate, 0.01, 10.), mFs);
  for (auto& side : mWander)
    for (auto& w : side)
      w.coef = c;
}

void MultiChorus::SetTone(double hz)
{
  hz = std::clamp(hz, 500., 20000.);
  if (hz != mTone) { mTone = hz; mToneDirty = true; }
}

void MultiChorus::SetAge(double amount)
{
  amount = std::clamp(amount, 0., 1.);
  if (amount != mAge) { mAge = amount; mToneDirty = true; }
}

void MultiChorus::UpdateTone()
{
  mToneDirty = false;
  const double hz = std::min(mTone * std::pow(0.4, mAge), 0.45 * mFs); // Age softens the top
  for (auto& f : mToneF)
    f.SetLowPass(hz, 0.7071, mFs);
}

// The LFO for one voice, -1 .. 1.
double MultiChorus::Shape(double cycles, int side, int voice)
{
  cycles -= std::floor(cycles);
  switch (mShape)
  {
    case kTriangle: // in step with the sine: 0 at 0, peak at a quarter
    {
      const double c = cycles + 0.25 - std::floor(cycles + 0.25);
      return 1. - 4. * std::fabs(c - 0.5);
    }
    case kWander: return std::clamp(1.6 * mWander[side][voice].Next(0.5 / mRate), -1., 1.);
    default: return std::sin(2. * kPi * cycles);
  }
}

void MultiChorus::Process(float inL, float inR, float& outL, float& outR)
{
  if (mToneDirty)
    UpdateTone();
  if (mStarting)
  {
    mStarting = false;
    mDepth = mDepthTarget;
    mDelay = mDelayTarget;
    mFeedback = mFeedbackTarget;
    mMix = mMixTarget;
  }
  mDepth += (mDepthTarget - mDepth) * mSmooth;
  mDelay += (mDelayTarget - mDelay) * mDelaySmooth;
  mFeedback += (mFeedbackTarget - mFeedback) * mSmooth;
  mMix += (mMixTarget - mMix) * mSmooth;
  mLfo += mRate / mFs;
  if (mLfo >= 1.) mLfo -= 1.;
  mFast += 6.1 / mFs; // ensemble's fast shimmer
  if (mFast >= 1.) mFast -= 1.;

  // The swing can't take a voice below zero delay: it's at most 90% of the centre time.
  const double swing = mDepth * std::min(kMaxSwingMs, 0.9 * mDelay) / 1000. * mFs;
  const double centre = mDelay / 1000. * mFs;
  const float in[2] = {inL, inR};
  double wet[2];
  for (int s = 0; s < 2; s++)
  {
    const double warble = mAge > 0. ? mAge * 0.0004 * mFs * mWarble[s].Next(0.4) : 0.;
    double sum = 0.;
    for (int v = 0; v < mVoices; v++)
    {
      const double at = mLfo + (double)v / mVoices + (s ? mPhase : 0.);
      double mod = Shape(at, s, v);
      if (mMode == kEnsemble)
        mod = 0.7 * mod + 0.3 * std::sin(2. * kPi * (mFast + (double)v / mVoices + (s ? mPhase : 0.)));
      const double d = std::max(2., centre + 0.5 * swing * mod + warble);
      mLastDelay[s][v] = d;
      sum += mLine[s].Read(d);
    }
    // Heard at 1/sqrt(n) for even loudness; fed back as the average (never more than Feedback).
    const double toned = mToneF[s].Process(sum / std::sqrt((double)mVoices));
    double w = toned;
    if (mAge > 0.)
    {
      const double g = 1. + 2. * mAge;
      w = std::tanh(g * w) / g;
    }
    wet[s] = w;
    mFbState[s] = toned / std::sqrt((double)mVoices); // = the voices' average, toned
    mLine[s].Write((float)(in[s] + mFeedback * std::tanh(mFbState[s])));
  }

  {
    float a = (float)wet[0], b = (float)wet[1];
    mWarmth.Process(a, b);
    wet[0] = a;
    wet[1] = b;
  }

  // Spread: 0 puts the voices in the middle, 1 keeps the sides apart.
  const double k = 0.5 + 0.5 * mSpread;
  const double wl = wet[0] * k + wet[1] * (1. - k), wr = wet[1] * k + wet[0] * (1. - k);
  if (mMode == kVibrato)
  {
    outL = (float)wl;
    outR = (float)wr;
    return;
  }
  const double a = mMix * kPi / 2.;
  outL = (float)(inL * std::cos(a) + wl * std::sin(a));
  outR = (float)(inR * std::cos(a) + wr * std::sin(a));
}

} // namespace underheard::fx
