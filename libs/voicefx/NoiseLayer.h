#pragma once
// A noise layer for a voice: band-limited noise in bursts that start once per cycle of the
// note, the way a bow's scrape follows the string's slip. Added after the voice's filter.
//
// Amount scales it; Tone moves its band from dark (about 240 Hz - 1.5 kHz) through the middle
// (about 1 - 6 kHz, the first version of Section's bow noise) to bright (4 - 24 kHz).
//
// Framework-free, real-time safe, one per voice.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace underheard::voicefx
{

class NoiseLayer
{
public:
  void Prepare(double sampleRate)
  {
    mFs = sampleRate;
    Voice();
  }
  void Reset(double phase01 = 0.) { mPhase = phase01 - std::floor(phase01); mLp = mHp = 0.; }
  void Seed(uint32_t seed) { mRng = seed ? seed : 1u; }

  // Tone 0..1: the band's top runs 1.5 kHz .. 24 kHz (log), its bottom about 2.6 octaves lower.
  void SetTone(double tone)
  {
    tone = std::clamp(tone, 0., 1.);
    if (tone != mTone)
    {
      mTone = tone;
      Voice();
    }
  }
  double Tone() const { return mTone; }

  // One sample for a note at `hz`, at `amount` (the caller's scale: 1 = Section's original).
  double Next(double hz, double amount)
  {
    mPhase += hz / mFs;
    if (mPhase >= 1.)
      mPhase -= std::floor(mPhase);
    mRng = mRng * 1664525u + 1013904223u;
    const double white = (double)(int32_t)mRng * (1. / 2147483648.);
    mLp += (white - mLp) * mLpK;
    mHp += (mLp - mHp) * mHpK;
    return (mLp - mHp) * std::exp(-mPhase * 10.) * amount; // a burst just after each cycle starts
  }

private:
  void Voice()
  {
    const double lpHz = std::min(1500. * std::pow(16., mTone), 0.45 * mFs), hpHz = lpHz / 6.2;
    mLpK = 1. - std::exp(-2. * 3.14159265358979 * lpHz / mFs);
    mHpK = 1. - std::exp(-2. * 3.14159265358979 * hpHz / mFs);
  }

  double mFs = 48000., mTone = 0.5, mLpK = 0.55, mHpK = 0.12;
  double mPhase = 0., mLp = 0., mHp = 0.;
  uint32_t mRng = 1;
};

} // namespace underheard::voicefx
