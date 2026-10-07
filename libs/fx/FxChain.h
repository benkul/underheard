#pragma once
// Splicer's effects chain: the suite's own effects in series, chorus -> delay -> reverb, each
// with its own on/off, then Warmth on whatever the chain did. Splicer feeds it from every
// loop's Send (and the input's).
//
// The same engines as the standalone plugins: MultiChorus (underheard-chorus), DubDelay
// (underheard-delay) and AlgoReverb (underheard-reverb's Plate and Hall). Rooms and
// Recordings stay in the standalone reverb. With every stage off the chain is a straight
// bypass (no Warmth either).
//
// Prepare() allocates; everything else is real-time safe.

#include "AlgoReverb.h"
#include "DubDelay.h"
#include "MultiChorus.h"
#include "Warmth.h"

namespace underheard::fx {

struct FxChain
{
  MultiChorus chorus;
  DubDelay delay;
  AlgoReverb reverb;
  Warmth warmth;
  bool chorusOn = true, delayOn = true, reverbOn = true;

  void Prepare(double sampleRate)
  {
    mFs = sampleRate;
    chorus.Prepare(sampleRate);
    delay.Prepare(sampleRate);
    reverb.Prepare(sampleRate);
    warmth.Prepare(sampleRate);
    mSmooth = OnePoleCoef(0.02, sampleRate);
    mReverbMix = mReverbMixTarget;
  }

  void Reset()
  {
    delay.Reset();
    chorus.Reset();
    reverb.Reset();
    warmth.Reset();
  }

  void SetReverbMix(double amount) { mReverbMixTarget = std::clamp(amount, 0., 1.); }

  void Process(float inL, float inR, float& outL, float& outR)
  {
    float l = inL, r = inR;
    if (chorusOn)
      chorus.Process(l, r, l, r);
    if (delayOn)
      delay.Process(l, r, l, r);
    mReverbMix += (mReverbMixTarget - mReverbMix) * mSmooth;
    if (reverbOn)
    {
      float wl, wr;
      reverb.Process(l, r, wl, wr);
      const double a = mReverbMix * kPi / 2.;
      l = (float)(l * std::cos(a) + wl * std::sin(a));
      r = (float)(r * std::cos(a) + wr * std::sin(a));
    }
    if (chorusOn || delayOn || reverbOn)
      warmth.Process(l, r);
    outL = l;
    outR = r;
  }

private:
  double mFs = 48000., mSmooth = 1., mReverbMix = 0.5, mReverbMixTarget = 0.5;
};

} // namespace underheard::fx
