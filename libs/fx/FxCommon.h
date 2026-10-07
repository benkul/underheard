#pragma once
// Shared pieces for the underheard effects. Framework-free.

#include "Filters.h" // from libs/tape-core

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace underheard::fx {

// A power-of-two ring buffer with fractional (Hermite) reads. Allocate() is non-real-time.
class DelayLine
{
public:
  void Allocate(int minSamples)
  {
    int n = 1;
    while (n < minSamples + 4)
      n <<= 1;
    mBuf.assign((size_t)n, 0.f);
    mMask = n - 1;
    mWrite = 0;
  }

  void Clear() { std::fill(mBuf.begin(), mBuf.end(), 0.f); }

  void Write(float x)
  {
    mBuf[(size_t)mWrite] = x;
    mWrite = (mWrite + 1) & mMask;
  }

  // The sample written `delay` samples ago (delay >= 1).
  float Read(double delay) const
  {
    const double pos = (double)mWrite - delay;
    const int i = (int)std::floor(pos);
    const float t = (float)(pos - (double)i);
    auto at = [&](int k) { return mBuf[(size_t)(k & mMask)]; };
    return Hermite(at(i - 1), at(i), at(i + 1), at(i + 2), t);
  }

  float ReadInt(int delay) const { return mBuf[(size_t)((mWrite - delay) & mMask)]; }

  int Capacity() const { return mMask; }

private:
  std::vector<float> mBuf;
  int mMask = 0;
  int mWrite = 0;
};

// Small, cheap random source for modulation.
struct Rand
{
  uint32_t s = 12345;
  float Bipolar()
  {
    s = s * 1664525u + 1013904223u;
    return (float)((double)(int32_t)s * (1. / 2147483648.));
  }
};

// Slow random wander (smoothed steps toward a new target every so often), -1 .. 1.
struct Wander
{
  Rand rng;
  double target = 0., a = 0., b = 0., coef = 0., timer = 0., fs = 48000.;
  void Set(double sampleRate, double smoothSeconds, uint32_t seed)
  {
    fs = sampleRate;
    coef = OnePoleCoef(smoothSeconds, sampleRate);
    rng.s = seed;
  }
  double Next(double meanInterval)
  {
    timer -= 1. / fs;
    if (timer <= 0.)
    {
      target = rng.Bipolar();
      timer = meanInterval * (0.5 + 0.5 * (rng.Bipolar() + 1.f));
    }
    a += (target - a) * coef;
    b += (a - b) * coef;
    return b;
  }
};

} // namespace underheard::fx
