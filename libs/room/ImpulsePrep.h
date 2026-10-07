#pragma once
// Turning a room recording (a clap, a balloon pop, or a ready-made impulse response) into an
// impulse response Room Bleed can use. Non-real-time.

#include <cmath>
#include <cstdint>
#include <vector>

namespace underheard::room {

struct Impulse
{
  double sampleRate = 48000.;
  std::vector<std::vector<float>> channels; // 1 (mono) or 2
  int64_t Frames() const { return channels.empty() ? 0 : (int64_t)channels[0].size(); }
};

// Prepares an impulse from interleaved stereo audio (a mono file has identical sides):
//   - starts just before the onset (1 ms before the first sample within 20 dB of the peak);
//   - ends where the tail has fallen 70 dB below the peak, at most maxSeconds later, and fades
//     its last 20% with a raised cosine;
//   - is scaled so each channel's energy (sum of squares) averages 1, so a broadband sound
//     comes out about as loud as it went in;
//   - keeps one channel when the sides are identical.
inline Impulse PrepareImpulse(const std::vector<float>& stereo, double sampleRate, double maxSeconds = 4.)
{
  Impulse imp;
  imp.sampleRate = sampleRate;
  const int64_t frames = (int64_t)stereo.size() / 2;
  if (frames < 2)
    return imp;

  bool identical = true;
  float peak = 0.f;
  for (int64_t i = 0; i < frames; i++)
  {
    const float l = stereo[(size_t)(2 * i)], r = stereo[(size_t)(2 * i + 1)];
    identical = identical && l == r;
    peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
  }
  if (peak <= 0.f)
    return imp;

  auto level = [&](int64_t i) { return std::max(std::fabs(stereo[(size_t)(2 * i)]), std::fabs(stereo[(size_t)(2 * i + 1)])); };
  int64_t onset = 0;
  while (onset < frames && level(onset) < peak * 0.1f)
    onset++;
  onset = std::max<int64_t>(0, onset - (int64_t)(0.001 * sampleRate));

  const float floor = peak * std::pow(10.f, -70.f / 20.f);
  int64_t end = frames;
  while (end > onset + 1 && level(end - 1) < floor)
    end--;
  end = std::min(end, onset + (int64_t)(maxSeconds * sampleRate));
  const int64_t len = std::max<int64_t>(1, end - onset);

  const int nch = identical ? 1 : 2;
  imp.channels.assign((size_t)nch, std::vector<float>((size_t)len));
  const int64_t fadeStart = len - len / 5;
  for (int ch = 0; ch < nch; ch++)
    for (int64_t i = 0; i < len; i++)
    {
      float x = stereo[(size_t)(2 * (onset + i) + ch)];
      if (i >= fadeStart && len > 5)
        x *= 0.5f * (1.f + std::cos(3.14159265f * (float)(i - fadeStart) / (float)(len - fadeStart)));
      imp.channels[(size_t)ch][(size_t)i] = x;
    }

  double energy = 0.;
  for (const auto& c : imp.channels)
    for (float x : c)
      energy += (double)x * x;
  energy /= nch;
  const float g = energy > 0. ? (float)(1. / std::sqrt(energy)) : 1.f;
  for (auto& c : imp.channels)
    for (float& x : c)
      x *= g;
  return imp;
}

} // namespace underheard::room
