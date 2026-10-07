#include "RoomTone.h"

#include <algorithm>
#include <cmath>

namespace underheard::room {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

ToneLoop MakeToneLoop(const std::vector<float>& stereo, double fadeSeconds, double fs)
{
  ToneLoop t;
  const size_t frames = stereo.size() / 2;
  size_t fade = std::min((size_t)(fadeSeconds * fs), frames / 3);
  const size_t len = frames - fade;
  t.l.resize(len);
  t.r.resize(len);
  for (size_t i = 0; i < len; i++)
  {
    t.l[i] = stereo[2 * i];
    t.r[i] = stereo[2 * i + 1];
  }
  // The last `fade` frames fade into the first `fade` (equal power), so the end flows into the
  // start.
  for (size_t i = 0; i < fade; i++)
  {
    const double w = (double)i / (double)fade;
    const float a = (float)std::sin(w * kPi / 2.), b = (float)std::cos(w * kPi / 2.);
    t.l[i] = t.l[i] * a + stereo[2 * (len + i)] * b;
    t.r[i] = t.r[i] * a + stereo[2 * (len + i) + 1] * b;
  }
  return t;
}

void RoomTone::Filter::Set(int type, double hz, double q, double fs)
{
  const double w = 2. * kPi * std::min(hz, 0.45 * fs) / fs, c = std::cos(w), al = std::sin(w) / (2. * q), a0 = 1. + al;
  if (type == 0)
  {
    b0 = (1. - c) / 2. / a0; b1 = (1. - c) / a0; b2 = b0;
  }
  else if (type == 1)
  {
    b0 = (1. + c) / 2. / a0; b1 = -(1. + c) / a0; b2 = b0;
  }
  else
  {
    b0 = al / a0; b1 = 0.; b2 = -al / a0;
  }
  a1 = -2. * c / a0;
  a2 = (1. - al) / a0;
}

double RoomTone::Filter::Run(double x)
{
  const double y = b0 * x + z1;
  z1 = b1 * x - a1 * y + z2;
  z2 = b2 * x - a2 * y;
  return y;
}

// Paul Kellet's pinking filter.
double RoomTone::Pink::Run(double w)
{
  b0 = 0.99886 * b0 + w * 0.0555179;
  b1 = 0.99332 * b1 + w * 0.0750759;
  b2 = 0.96900 * b2 + w * 0.1538520;
  b3 = 0.86650 * b3 + w * 0.3104856;
  b4 = 0.55000 * b4 + w * 0.5329522;
  b5 = -0.7616 * b5 - w * 0.0168980;
  const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
  b6 = w * 0.115926;
  return p * 0.11;
}

double RoomTone::White()
{
  mRand = mRand * 1664525u + 1013904223u;
  return (double)(int32_t)mRand * (1. / 2147483648.);
}

// A slow random wander between -1 and 1: a new target every `interval` seconds or so.
double RoomTone::Wander(double& state, double& target, double& timer, double rate, double interval)
{
  timer -= 1. / mFs;
  if (timer <= 0.)
  {
    target = White();
    timer = interval * (0.5 + 0.5 * (White() + 1.));
  }
  state += (target - state) * rate;
  return state;
}

void RoomTone::Prepare(double fs, uint32_t seed)
{
  mFs = fs;
  mRand = seed ? seed : 1;
  mTraffic.Set(0, 120., 0.7, fs);
  mHvacL.Set(2, 400., 0.6, fs);
  mHvacR.Set(2, 430., 0.6, fs);
  mDuct.Set(2, 120., 6., fs);
  mAirL.Set(1, 700., 0.7, fs);
  mAirR.Set(1, 650., 0.7, fs);
  mBoiler.Set(0, 70., 0.8, fs);
  mTick.Set(2, 3200., 12., fs);
}

void RoomTone::SetPreset(int preset) { mPreset = std::clamp(preset, 0, kNumPresets - 1); }

void RoomTone::Process(float* l, float* r, int n, float gain)
{
  if (mPreset == kNone || gain <= 0.f)
    return;
  if (mPreset == kLoaded)
  {
    if (!mLoop || mLoop->l.empty())
      return;
    for (int i = 0; i < n; i++)
    {
      if (mLoopPos >= mLoop->l.size())
        mLoopPos = 0;
      l[i] += gain * mLoop->l[mLoopPos];
      r[i] += gain * mLoop->r[mLoopPos];
      mLoopPos++;
    }
    return;
  }

  const double slow = 1. - std::exp(-1. / (2.0 * mFs));
  for (int i = 0; i < n; i++)
  {
    const double wl = White(), wr = White();
    const double pl = mPinkL.Run(wl), pr = mPinkR.Run(wr);
    mBrown = 0.997 * mBrown + 0.03 * White();
    const double drift = Wander(mDrift, mDriftTarget, mDriftTimer, slow, 6.);
    double L = 0., R = 0.;

    // Mains hum, drifting a little in level.
    mHumPhase += 2. * kPi * 60. / mFs;
    if (mHumPhase > 2. * kPi)
      mHumPhase -= 2. * kPi;
    const double hum = (std::sin(mHumPhase) * 0.05 + std::sin(2. * mHumPhase) * 0.03 + std::sin(3. * mHumPhase) * 0.015 + std::sin(4. * mHumPhase) * 0.008) * (1. + 0.2 * drift);

    switch (mPreset)
    {
      case kApartment:
      case kKitchen:
      {
        // Soft air, hum, and traffic rumble that swells as cars pass.
        const double swell = 0.5 + 0.5 * Wander(mSwell, mSwellTarget, mSwellTimer, 1. - std::exp(-1. / (1.5 * mFs)), 8.);
        const double traffic = mTraffic.Run(mBrown) * 3.5 * swell * swell;
        L = 0.35 * pl + hum + traffic;
        R = 0.35 * pr + hum + traffic;
        if (mPreset == kKitchen)
        {
          // The fridge compressor cycles: on for 40-90 s, off for 30-60 s, with a soft start,
          // a buzzing hum, and a clunk when it starts and stops.
          mFridgeTimer -= 1. / mFs;
          if (mFridgeTimer <= 0.)
          {
            mFridgeOn = !mFridgeOn;
            mFridgeTimer = mFridgeOn ? 40. + 50. * (0.5 * (White() + 1.)) : 30. + 30. * (0.5 * (White() + 1.));
            mClunk = 1.;
            mClunkPhase = 0.;
          }
          mFridgeEnv += ((mFridgeOn ? 1. : 0.) - mFridgeEnv) * (1. - std::exp(-1. / (1.2 * mFs)));
          mFridgePhase += 2. * kPi * (50. + 0.3 * drift) / mFs;
          if (mFridgePhase > 2. * kPi)
            mFridgePhase -= 2. * kPi;
          const double buzz = std::sin(mFridgePhase) * 0.12 + std::sin(2. * mFridgePhase) * 0.08 + std::sin(3. * mFridgePhase) * 0.04 + std::sin(5. * mFridgePhase) * 0.02;
          mClunkPhase += 2. * kPi * 45. / mFs;
          const double clunk = mClunk * std::sin(mClunkPhase) * 0.4;
          mClunk *= std::exp(-1. / (0.08 * mFs));
          L += buzz * mFridgeEnv + clunk;
          R += buzz * mFridgeEnv * 0.9 + clunk;
        }
        break;
      }
      case kOffice:
      {
        // HVAC: broadband air with a duct resonance and a slow drift.
        const double duct = mDuct.Run(0.5 * (pl + pr)) * 2.5;
        const double rumble = mTraffic.Run(mBrown) * 0.8;
        L = (mHvacL.Run(pl) * 2.2 + duct + rumble) * (1. + 0.15 * drift) + hum * 0.5;
        R = (mHvacR.Run(pr) * 2.2 + duct + rumble) * (1. + 0.15 * drift) + hum * 0.5;
        break;
      }
      case kHallway:
      {
        // Thin and airy, with a distant building hum.
        L = mAirL.Run(pl) * 2.6 + hum * 0.6;
        R = mAirR.Run(pr) * 2.6 + hum * 0.6;
        break;
      }
      case kBasement:
      {
        // A boiler's low rumble, pulsing slowly, and the odd tick of a pipe expanding.
        mBoilerPhase += 2. * kPi * 0.4 / mFs;
        if (mBoilerPhase > 2. * kPi)
          mBoilerPhase -= 2. * kPi;
        const double pulse = 1. + 0.3 * std::sin(mBoilerPhase);
        const double rumble = mBoiler.Run(mBrown) * 1.3 * pulse;
        if (0.5 * (White() + 1.) < 0.25 / mFs)
          mTickEnv = 0.6 + 0.4 * (0.5 * (White() + 1.));
        const double tick = mTick.Run(mTickEnv * White()) * 3.;
        mTickEnv *= std::exp(-1. / (0.004 * mFs));
        L = rumble + 0.6 * pl + tick + hum * 0.6;
        R = rumble + 0.6 * pr + tick * 0.7 + hum * 0.6;
        break;
      }
      default:
        break;
    }
    l[i] += (float)(gain * L);
    r[i] += (float)(gain * R);
  }
}

} // namespace underheard::room
