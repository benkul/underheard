#pragma once
// A body resonance for a voice (or a group of voices): the main resonances of a bowed
// instrument's body - the air mode, the two main wood modes, a higher wood mode, the bridge
// hill and a dip above it - as six peaking filters in series. Larger instruments scale the
// resonances down.
//
// Depth scales the resonances' dB (0 = flat, 1 = as measured, up to 2). The level is kept where
// the body sits at depth 1 (equal power on white noise), so Depth and the body choice change
// the character, not the loudness. Off is flat at that same level.
//
// Changing the body or depth re-voices the filters without clearing them (no click).
// Framework-free; Set() does a little work (not every sample), Process() is real-time safe.

#include <array>

namespace underheard::voicefx
{

class Resonator
{
public:
  enum Body { kOff = 0, kViolin, kViola, kCello, kDoubleBass, kNumBodies };
  static const char* BodyName(int body);

  void Prepare(double sampleRate);
  void Reset();
  void Set(int body, double depth); // depth 0..2
  int CurrentBody() const { return mBody; }
  double Process(double x)
  {
    for (auto& p : mPeaks)
      x = p.Run(x);
    return x * mGain;
  }

private:
  struct Peak
  {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double Run(double x)
    {
      const double y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
    }
  };
  void Voice(std::array<Peak, 6>& peaks, int body, double depth) const;
  static double Power(std::array<Peak, 6> peaks);

  double mFs = 48000.;
  int mBody = -1;
  double mDepth = -1.;
  std::array<Peak, 6> mPeaks;
  double mGain = 1.;
};

} // namespace underheard::voicefx
