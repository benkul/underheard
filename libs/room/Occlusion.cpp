#include "Occlusion.h"

#include "MicModel.h"

#include <algorithm>
#include <cmath>

namespace underheard::room {

namespace {

// Transmission through a single-stud drywall partition (about STC 33): the loss rises with
// frequency, with a dip near the coincidence frequency (~2.5 kHz) and a little mass-air-mass
// resonance in the lows. Shifted so the lows come through at about -6 dB: the shape is what
// reads as "next door"; Output sets the level.
const std::vector<ResponsePoint> kWall = {{40, -5}, {90, -3}, {125, -6}, {250, -14}, {500, -21}, {1000, -26},
                                          {2000, -28}, {2500, -25}, {4000, -31}, {8000, -36}, {16000, -42}};

// A wooden floor and ceiling between storeys: mostly the boom gets through.
const std::vector<ResponsePoint> kFloor = {{40, -4}, {63, -3}, {100, -6}, {160, -12}, {250, -20}, {500, -30},
                                           {1000, -38}, {2000, -45}, {4000, -50}, {16000, -60}};

// The doorway as `door` opens: ajar it's a narrow, midrange leak; open it's broad and only
// gently dulled by diffraction.
std::vector<ResponsePoint> DoorPath(double door)
{
  const double lo = 300. * std::pow(60. / 300., door);     // high-pass corner: 300 Hz -> 60 Hz
  const double hi = 3000. * std::pow(12000. / 3000., door); // low-pass corner: 3 kHz -> 12 kHz
  return {{20, -30}, {lo * 0.5, -12}, {lo, -3}, {lo * 2., 0}, {hi * 0.5, 0}, {hi, -3}, {hi * 2., -12}, {22000, -30}};
}

} // namespace

std::vector<float> Convolve(const std::vector<float>& a, const std::vector<float>& b)
{
  if (a.empty() || b.empty())
    return {};
  std::vector<float> out(a);
  out.resize(a.size() + b.size() - 1, 0.f);
  ApplyFilter(out, b);
  return out;
}

std::vector<float> OcclusionFilter(Where where, double door, double fs)
{
  if (where == Where::SameRoom)
    return {1.f};
  if (where == Where::FloorBelow)
    return MicFilter(kFloor, fs, 2048, true);

  door = std::clamp(door, 0., 1.);
  // The wall always carries some sound; the doorway takes over as it opens.
  std::vector<float> wall = MicFilter(kWall, fs, 2048, true);
  const float wallGain = (float)std::sqrt(1. - 0.8 * door);
  for (float& v : wall)
    v *= wallGain;
  if (door <= 0.)
    return wall;

  std::vector<float> doorway = MicFilter(DoorPath(door), fs, 2048, true);
  // The doorway is a little further round (about 3 ms), and through a narrow gap its own
  // reflections comb it (a 0.6 ms echo that fades as the door opens).
  const size_t delay = (size_t)(0.003 * fs), comb = (size_t)(0.0006 * fs);
  const float doorGain = (float)(0.7 * std::pow(door, 0.7)), combGain = (float)(0.45 * (1. - door));
  std::vector<float> out(std::max(wall.size(), delay + comb + doorway.size()), 0.f);
  for (size_t i = 0; i < wall.size(); i++)
    out[i] += wall[i];
  for (size_t i = 0; i < doorway.size(); i++)
  {
    out[delay + i] += doorGain * doorway[i];
    out[delay + comb + i] += doorGain * combGain * doorway[i];
  }
  return out;
}

std::vector<std::vector<float>> Occlude(const std::vector<std::vector<float>>& sourceRoom, Where where, double door,
                                        const std::vector<std::vector<float>>& listenerRoom, double fs, double maxSeconds)
{
  if (where == Where::SameRoom || sourceRoom.empty())
    return sourceRoom;

  // The wall (or floor) radiates the whole source room as one: sum its channels.
  std::vector<float> mono = sourceRoom[0];
  if (sourceRoom.size() > 1)
    for (size_t i = 0; i < mono.size() && i < sourceRoom[1].size(); i++)
      mono[i] = 0.5f * (mono[i] + sourceRoom[1][i]);
  const std::vector<float> through = Convolve(mono, OcclusionFilter(where, door, fs));

  // The listener's room only colours the sound: scale it to unit energy (averaged over its
  // channels), so the partition sets how loud next door is.
  double e = 0.;
  for (const auto& l : listenerRoom)
    for (float v : l)
      e += (double)v * v;
  e /= std::max<size_t>(1, listenerRoom.size());
  const float norm = e > 0. ? (float)(1. / std::sqrt(e)) : 1.f;

  std::vector<std::vector<float>> out;
  const size_t maxLen = (size_t)(maxSeconds * fs);
  for (const auto& l : listenerRoom)
  {
    std::vector<float> c = Convolve(through, l);
    for (float& v : c)
      v *= norm;
    if (c.size() > maxLen)
    {
      // Fade the last 50 ms before cutting.
      const size_t fade = (size_t)(0.05 * fs);
      for (size_t i = 0; i < fade; i++)
        c[maxLen - fade + i] *= (float)(1. - (double)i / (double)fade);
      c.resize(maxLen);
    }
    out.push_back(std::move(c));
  }
  return out;
}

} // namespace underheard::room
