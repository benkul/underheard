// Offline tests for libs/room Occlusion: next door and downstairs sound like it.
#include "MicModel.h"
#include "Occlusion.h"
#include "RoomModel.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace underheard::room;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<std::vector<float>> Room(const char* name, double distance)
{
  RoomSpec s;
  for (const auto& p : RoomPresets()) if (std::string(p.name) == name) s.shape = MakeShape(p, 0., 0.);
  const Placement p = PlaceMics(s.shape, distance, 0., false, 0);
  s.source = p.source;
  s.mics = p.mics;
  s.pickup.a.fill(1.);
  return GenerateRoom(s).channels;
}

// Average power (dB) of an impulse response across an octave around `hz`.
static double BandDb(const std::vector<float>& h, double hz)
{
  double p = 0.;
  const int n = 24;
  for (int i = 0; i < n; i++)
  {
    const double f = hz * std::pow(2., (i + 0.5) / n - 0.5);
    p += std::pow(10., FilterGainDb(h, f, 48000.) / 10.);
  }
  return 10. * std::log10(p / n);
}


int main()
{
  const auto source = Room("Living room", 2.0);
  const auto listener = Room("Bedroom", 1.5);
  const auto same = Occlude(source, Where::SameRoom, 0., listener, 48000.);
  const auto closed = Occlude(source, Where::NextRoom, 0., listener, 48000.);
  const auto ajar = Occlude(source, Where::NextRoom, 0.3, listener, 48000.);
  const auto open = Occlude(source, Where::NextRoom, 1., listener, 48000.);
  const auto below = Occlude(source, Where::FloorBelow, 0., listener, 48000.);

  auto tilt = [](const std::vector<float>& h) { return BandDb(h, 4000.) - BandDb(h, 125.); };
  printf("4 kHz relative to 125 Hz: same room %.1f, next door closed %.1f, ajar %.1f, open %.1f, below %.1f dB\n",
         tilt(same[0]), tilt(closed[0]), tilt(ajar[0]), tilt(open[0]), tilt(below[0]));
  CHECK(same == source, "the same room is left as it is");
  CHECK(tilt(closed[0]) < tilt(same[0]) - 15., "next door with the door shut loses the highs");
  CHECK(tilt(ajar[0]) > tilt(closed[0]) + 5., "ajar lets some through");
  CHECK(tilt(open[0]) > tilt(ajar[0]) + 3. && tilt(open[0]) > tilt(same[0]) - 12., "open lets most back");
  CHECK(tilt(below[0]) < tilt(closed[0]) - 5., "downstairs is boomier still");
  // Loudness where the ear is most sensitive (250 Hz - 4 kHz). A plain energy sum would be
  // dominated by the lowest band, where early reflections add up in phase (the boom that small
  // rooms have) and which walls let through.
  auto mids = [](const std::vector<float>& h) {
    double p = 0.;
    for (double f : {250., 500., 1000., 2000., 4000.}) p += std::pow(10., BandDb(h, f) / 10.);
    return 10. * std::log10(p / 5.);
  };
  const double level = mids(closed[0]) - mids(same[0]);
  CHECK(level > -45. && level < -10., "next door is much quieter in the mids, but not lost (%.1f dB)", level);
  const auto stereo = Occlude(source, Where::NextRoom, 0., {listener[0], listener[0]}, 48000.);
  CHECK(stereo.size() == 2, "a stereo pair in the listener's room gives two channels");

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
