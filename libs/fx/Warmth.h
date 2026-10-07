#pragma once
// Warmth: the suite's shared analog colour (the user's goal: "a rich analog warmth suffusing
// everything"). Per channel, at Amount 0 .. 1 (50%, the default, is where the user liked it):
//
//   head bump (+2.5 dB at 110 Hz at 50%, +5 at 100%) -> high shelf down (-6 dB above ~4.5 kHz
//   at 50%, -9 at 100%, so the highs are saturated less) -> soft saturation (slightly
//   asymmetric, for even harmonics; unity gain for quiet signals) -> DC blocker -> a top
//   roll-off (12 kHz at 50%, 9 kHz at 100%)
//
// The saturation uses first-order antiderivative anti-aliasing (ADAA), so driving it doesn't
// fold harsh aliases back down. Amount 0 is an exact bypass. Real-time safe; no allocation.

#include "FxCommon.h"

namespace underheard::fx {

// tanh-based soft clipper with first-order antiderivative anti-aliasing. `drive` >= 1 sets how
// hard it bends; `bias` tilts it for even harmonics. Unity gain for small signals, 0 in, 0 out.
// The anti-aliasing averages adjacent samples a little, so it adds half a sample of delay and
// a gentle top roll-off (warm, not neutral: inside a feedback loop each pass softens the top).
struct SoftSaturator
{
  double drive = 1., bias = 0., offset = 0., slope = 1., x1 = 0., F1 = 0.;

  void Set(double newDrive, double newBias)
  {
    drive = std::max(1e-3, newDrive);
    bias = newBias;
    offset = std::tanh(drive * bias);
    slope = drive * (1. - offset * offset); // f'(0)
    F1 = F(x1);
  }
  void Reset() { x1 = 0.; F1 = F(0.); }

  double Run(double x)
  {
    const double Fx = F(x), dx = x - x1;
    const double y = std::fabs(dx) > 1e-6 ? (Fx - F1) / dx : f(0.5 * (x + x1));
    x1 = x;
    F1 = Fx;
    return y / slope;
  }

  double f(double x) const { return std::tanh(drive * (x + bias)) - offset; }

private:
  static double LogCosh(double z)
  {
    const double a = std::fabs(z);
    return a + std::log1p(std::exp(-2. * a)) - 0.6931471805599453;
  }
  // Antiderivative of f.
  double F(double x) const { return LogCosh(drive * (x + bias)) / drive - offset * x; }
};

class Warmth
{
public:
  void Prepare(double sampleRate)
  {
    mFs = sampleRate;
    mDcCoef = 1. - std::exp(-2. * kPi * 10. / sampleRate);
    Update();
    Reset();
  }
  void Reset()
  {
    for (auto& c : mCh)
    {
      c.bump.Reset();
      c.shelf.Reset();
      c.sat.Reset();
      c.dc = 0.;
      c.lp = 0.;
    }
  }
  void SetAmount(double amount)
  {
    amount = std::clamp(amount, 0., 1.);
    if (amount != mAmount)
    {
      mAmount = amount;
      Update();
    }
  }
  double Amount() const { return mAmount; }

  double Process(double x, int channel)
  {
    if (mAmount <= 0.)
      return x;
    Channel& c = mCh[channel & 1];
    x = c.bump.Process(x);
    x = c.shelf.Process(x);
    x = c.sat.Run(x);
    c.dc += (x - c.dc) * mDcCoef; // the asymmetry makes a little DC
    x -= c.dc;
    c.lp += (x - c.lp) * mLpK;
    return c.lp;
  }

  void Process(float& l, float& r)
  {
    l = (float)Process((double)l, 0);
    r = (float)Process((double)r, 1);
  }

private:
  struct Filter // RBJ peaking and high-shelf biquads
  {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void Peak(double hz, double q, double db, double fs)
    {
      const double A = std::pow(10., db / 40.), w = 2. * kPi * hz / fs, c = std::cos(w), al = std::sin(w) / (2. * q), a0 = 1. + al / A;
      b0 = (1. + al * A) / a0;
      b1 = -2. * c / a0;
      b2 = (1. - al * A) / a0;
      a1 = b1;
      a2 = (1. - al / A) / a0;
    }
    void HighShelf(double hz, double db, double fs)
    {
      const double A = std::pow(10., db / 40.), w = 2. * kPi * std::min(hz, 0.45 * fs) / fs, c = std::cos(w), s = std::sin(w);
      const double al = s / 2. * std::sqrt(2.), sq = 2. * std::sqrt(A) * al; // slope 1
      const double a0 = (A + 1.) - (A - 1.) * c + sq;
      b0 = A * ((A + 1.) + (A - 1.) * c + sq) / a0;
      b1 = -2. * A * ((A - 1.) + (A + 1.) * c) / a0;
      b2 = A * ((A + 1.) + (A - 1.) * c - sq) / a0;
      a1 = 2. * ((A - 1.) - (A + 1.) * c) / a0;
      a2 = ((A + 1.) - (A - 1.) * c - sq) / a0;
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
  struct Channel
  {
    Filter bump, shelf;
    SoftSaturator sat;
    double dc = 0., lp = 0.;
  };

  // Intensity k = 2 x Amount. Up to k = 1 (Amount 50%, the default) is the original curve,
  // which the user liked best at full; past it, more of the same: more body, more saturation,
  // a darker top.
  void Update()
  {
    const double k = 2. * mAmount, over = std::max(0., k - 1.);
    for (auto& c : mCh)
    {
      c.bump.Peak(110., 0.8, 2.5 * k, mFs);                         // +2.5 dB at 50%, +5 at 100%
      c.shelf.HighShelf(4500., -6. * std::min(k, 1.) - 3. * over, mFs); // -6 dB at 50%, -9 at 100%
      // About 2.6% THD on a -6 dBFS note at 50%, 6% at 100% (the bigger bump drives it harder too).
      c.sat.Set(0.3 + 0.5 * std::min(k, 1.) + 0.2 * over, 0.12 * std::min(k, 1.) + 0.04 * over);
    }
    const double lp = 20000. * std::pow(0.6, std::min(k, 1.)) * std::pow(0.75, over); // 12 kHz at 50%, 9 kHz at 100%
    mLpK = 1. - std::exp(-2. * kPi * std::min(lp, 0.45 * mFs) / mFs);
  }

  double mFs = 48000., mAmount = 0., mDcCoef = 0., mLpK = 1.;
  Channel mCh[2];
};

} // namespace underheard::fx
