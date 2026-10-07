#include "Resonator.h"

#include <algorithm>
#include <cmath>

namespace underheard::voicefx
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
// A violin body's main resonances (Hz, dB, Q).
struct Res { double hz, db, q; };
constexpr Res kViolinBody[6] = {{280., 6., 8.}, {460., 7., 10.}, {550., 6., 10.}, {1100., 3., 4.}, {2500., 2., 1.5}, {4500., -4., 1.5}};
// How each body scales the violin's resonances (Off: unused).
constexpr double kScale[Resonator::kNumBodies] = {1., 1., 0.82, 0.42, 0.25};
constexpr double kLevel = 0.35; // Section's original body level
} // namespace

const char* Resonator::BodyName(int body)
{
  static const char* names[kNumBodies] = {"Off", "Violin", "Viola", "Cello", "Double bass"};
  return names[std::clamp(body, 0, kNumBodies - 1)];
}

void Resonator::Prepare(double sampleRate)
{
  mFs = sampleRate;
  mBody = -1; // re-voice on the next Set
  Reset();
}

void Resonator::Reset()
{
  for (auto& p : mPeaks)
    p.z1 = p.z2 = 0.;
}

// The six peaks for a body at a depth (Off: flat).
void Resonator::Voice(std::array<Peak, 6>& peaks, int body, double depth) const
{
  const double s = kScale[std::clamp(body, 0, kNumBodies - 1)];
  const double dbScale = body == kOff ? 0. : depth;
  for (int i = 0; i < 6; i++)
  {
    const Res& r = kViolinBody[i];
    // The bridge hill and the dip above it move less with size than the wood modes.
    const double hz = std::min(r.hz * (i >= 4 ? std::sqrt(s) : s), 0.45 * mFs);
    const double A = std::pow(10., r.db * dbScale / 40.), w = 2. * kPi * hz / mFs, c = std::cos(w), al = std::sin(w) / (2. * r.q), a0 = 1. + al / A;
    Peak& p = peaks[(size_t)i];
    p.b0 = (1. + al * A) / a0;
    p.b1 = -2. * c / a0;
    p.b2 = (1. - al * A) / a0;
    p.a1 = p.b1;
    p.a2 = (1. - al / A) / a0;
  }
}

// The peaks' power on white noise (the energy of their impulse response).
double Resonator::Power(std::array<Peak, 6> peaks)
{
  for (auto& p : peaks)
    p.z1 = p.z2 = 0.;
  double e = 0.;
  for (int i = 0; i < 8192; i++)
  {
    double x = i == 0 ? 1. : 0.;
    for (auto& p : peaks)
      x = p.Run(x);
    e += x * x;
  }
  return e;
}

void Resonator::Set(int body, double depth)
{
  body = std::clamp(body, 0, kNumBodies - 1);
  depth = std::clamp(depth, 0., 2.);
  if (body == mBody && depth == mDepth)
    return;
  mBody = body;
  mDepth = depth;
  // The level: equal power to this body (the violin's, for Off) at depth 1.
  std::array<Peak, 6> reference;
  Voice(reference, body == kOff ? kViolin : body, 1.);
  std::array<Peak, 6> next = mPeaks; // keeps the running state
  Voice(next, body, depth);
  mGain = kLevel * std::sqrt(Power(reference) / Power(next));
  mPeaks = next;
}

} // namespace underheard::voicefx
