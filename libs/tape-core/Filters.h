#pragma once
// Small DSP building blocks shared by the tape engine. Framework-free.

#include <cmath>

namespace underheard {

constexpr double kPi = 3.14159265358979323846;

// 4-point, 3rd-order Hermite interpolation between y1 (t = 0) and y2 (t = 1).
inline float Hermite(float y0, float y1, float y2, float y3, float t)
{
  const float c1 = 0.5f * (y2 - y0);
  const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
  const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
  return ((c3 * t + c2) * t + c1) * t + y1;
}

inline double SmoothStep(double x)
{
  x = x < 0. ? 0. : (x > 1. ? 1. : x);
  return x * x * (3. - 2. * x);
}

// One-pole smoothing coefficient for a time constant in seconds.
inline double OnePoleCoef(double seconds, double sampleRate)
{
  return seconds <= 0. ? 1. : 1. - std::exp(-1. / (seconds * sampleRate));
}

// RBJ low-pass biquad, transposed direct form II.
struct Biquad
{
  double b0 = 1., b1 = 0., b2 = 0., a1 = 0., a2 = 0.;
  double z1 = 0., z2 = 0.;

  void SetLowPass(double freq, double q, double sampleRate)
  {
    const double w0 = 2. * kPi * freq / sampleRate;
    const double cw = std::cos(w0), alpha = std::sin(w0) / (2. * q);
    const double a0 = 1. + alpha;
    b0 = (1. - cw) * 0.5 / a0;
    b1 = (1. - cw) / a0;
    b2 = b0;
    a1 = -2. * cw / a0;
    a2 = (1. - alpha) / a0;
  }

  double Process(double x)
  {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }

  void Reset() { z1 = z2 = 0.; }
};

// 4th-order Butterworth low-pass (two biquads).
struct LowPass4
{
  Biquad s1, s2;

  void Set(double freq, double sampleRate)
  {
    s1.SetLowPass(freq, 0.54119610, sampleRate);
    s2.SetLowPass(freq, 1.30656296, sampleRate);
  }

  double Process(double x) { return s2.Process(s1.Process(x)); }
  void Reset() { s1.Reset(); s2.Reset(); }
};

// Removes DC. Tape heads don't reproduce it, and a stopped tape would otherwise hold a level.
struct DcBlocker
{
  double r = 0.9996, x1 = 0., y1 = 0.;

  void Set(double cutoffHz, double sampleRate) { r = std::exp(-2. * kPi * cutoffHz / sampleRate); }

  double Process(double x)
  {
    const double y = x - x1 + r * y1;
    x1 = x;
    y1 = y;
    return y;
  }

  void Reset() { x1 = y1 = 0.; }
};

} // namespace underheard
