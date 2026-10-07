// Offline tests for libs/room RoomModel: does the generated room behave like a room?
#include "RoomModel.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace underheard::room;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const RoomPreset& Preset(const char* name)
{
  for (const auto& p : RoomPresets())
    if (std::string(p.name) == name)
      return p;
  return RoomPresets()[0];
}

static RoomSpec Spec(const char* preset, double distance, double aim = 0., double a = 0.5, bool stereo = false, int pair = 0,
                     double size = 0., double surfaces = 0.)
{
  RoomSpec s;
  s.shape = MakeShape(Preset(preset), size, surfaces);
  const Placement p = PlaceMics(s.shape, distance, aim, stereo, pair);
  s.source = p.source;
  s.mics = p.mics;
  s.tailCorrelation = p.tailCorrelation;
  s.pickup.a.fill(a);
  return s;
}

// Energy within `ms` of the direct sound (which arrives at `distance` metres), and after that.
static void Split(const std::vector<float>& h, double distance, double ms, double& direct, double& rest)
{
  const size_t arrive = (size_t)(distance / 343. * 48000.);
  const size_t from = arrive > 24 ? arrive - 24 : 0, to = arrive + (size_t)(ms * 48.);
  direct = rest = 0.;
  for (size_t i = 0; i < h.size(); i++)
    (i >= from && i < to ? direct : rest) += (double)h[i] * h[i];
}

// Decay time of the 1 kHz octave, from the Schroeder integral of the tail (after `start`
// seconds), T20 extrapolated to 60 dB.
static double MeasuredRT1k(const std::vector<float>& h, double start)
{
  // An octave band-pass around 1 kHz (two RBJ band-pass sections).
  const double w = 2. * 3.14159265358979 * 1000. / 48000., al = std::sin(w) / (2. * 1.41), c = std::cos(w);
  const double b0 = al / (1. + al), a1 = -2. * c / (1. + al), a2 = (1. - al) / (1. + al);
  std::vector<double> x(h.begin(), h.end());
  for (int pass = 0; pass < 2; pass++)
  {
    double z1 = 0., z2 = 0.;
    for (double& v : x)
    {
      const double y = b0 * v + z1;
      z1 = -a1 * y + z2;
      z2 = -b0 * v - a2 * y;
      v = y;
    }
  }
  const size_t s0 = (size_t)(start * 48000.);
  std::vector<double> e(x.size(), 0.);
  double acc = 0.;
  for (size_t i = x.size(); i-- > s0;) { acc += x[i] * x[i]; e[i] = acc; }
  const double total = e[s0];
  size_t t5 = 0, t25 = 0;
  for (size_t i = s0; i < e.size(); i++)
  {
    const double db = 10. * std::log10(e[i] / total);
    if (!t5 && db < -5.) t5 = i;
    if (!t25 && db < -25.) { t25 = i; break; }
  }
  return t25 > t5 ? 3. * (double)(t25 - t5) / 48000. : 0.;
}

int main()
{
  printf("-- the direct sound\n");
  {
    RoomSpec s = Spec("Living room", 3.43);
    const RoomIR r = GenerateRoom(s);
    size_t first = 0;
    while (std::fabs(r.channels[0][first]) < 1e-3f) first++;
    CHECK(std::abs((int)first - 480) <= 3, "3.43 m away arrives 10 ms later (sample %zu)", first);
    printf("   (%d image sources, mixing time %.0f ms)\n", r.imageCount, r.mixingTime * 1000.);
  }

  printf("\n-- decay times (1 kHz, measured on the tail)\n");
  for (const auto& p : RoomPresets())
  {
    RoomSpec s = Spec(p.name, 1.5, 0., 1.);
    const RoomIR r = GenerateRoom(s);
    const double predicted = r.rt60[3];
    const double measured = MeasuredRT1k(r.channels[0], r.mixingTime + 0.03);
    if (predicted < 0.3)
      // Very dead rooms: past -25 dB the slower low bands leak through the test's filter, so
      // just check the room really is dead.
      CHECK(measured < 0.4, "%s: dead (measured %.2f s, predicted %.2f s)", p.name, measured, predicted);
    else
      CHECK(measured > 0.75 * predicted && measured < 1.3 * predicted, "%s: measured %.2f s, predicted %.2f s", p.name, measured, predicted);
  }
  {
    auto rt = [](double size, double surfaces) {
      const RoomIR r = GenerateRoom(Spec("Living room", 1.5, 0., 1., false, 0, size, surfaces));
      return MeasuredRT1k(r.channels[0], r.mixingTime + 0.03);
    };
    const double small = rt(-1., 0.), big = rt(1., 0.), soft = rt(0., -1.), hard = rt(0., 1.);
    CHECK(big > 1.4 * small, "a bigger room rings longer (%.2f s vs %.2f s)", big, small);
    CHECK(hard > 2. * soft, "harder surfaces ring longer (%.2f s vs %.2f s)", hard, soft);
  }

  printf("\n-- distance\n");
  {
    double d1, r1, d2, r2;
    Split(GenerateRoom(Spec("Living room", 0.5)).channels[0], 0.5, 2.5, d1, r1);
    Split(GenerateRoom(Spec("Living room", 4.0)).channels[0], 4.0, 2.5, d2, r2);
    const double near = 10. * std::log10(d1 / r1), far = 10. * std::log10(d2 / r2);
    CHECK(near - far > 8., "moving away from 0.5 m to 4 m lowers the direct-to-room ratio (%.1f dB -> %.1f dB)", near, far);
  }

  printf("\n-- mic patterns\n");
  {
    double dOn, rOn, dRear, rRear, dSide, rSide;
    Split(GenerateRoom(Spec("Living room", 2.0, 0., 0.5)).channels[0], 2.0, 1.0, dOn, rOn);
    Split(GenerateRoom(Spec("Living room", 2.0, 180., 0.5)).channels[0], 2.0, 1.0, dRear, rRear);
    CHECK(10. * std::log10(dRear / dOn) < -15., "a cardioid turned away loses the direct sound (%.1f dB)", 10. * std::log10(dRear / dOn));
    CHECK(rRear > 0.1 * rOn, "but still hears the room");
    Split(GenerateRoom(Spec("Living room", 2.0, 90., 0.)).channels[0], 2.0, 1.0, dSide, rSide);
    Split(GenerateRoom(Spec("Living room", 2.0, 0., 0.)).channels[0], 2.0, 1.0, dOn, rOn);
    CHECK(10. * std::log10(dSide / dOn) < -15., "a figure-8 side-on nulls the direct sound (%.1f dB)", 10. * std::log10(dSide / dOn));
    double dOmni, rOmni, dCard, rCard;
    Split(GenerateRoom(Spec("Living room", 2.0, 0., 1.)).channels[0], 2.0, 2.5, dOmni, rOmni);
    Split(GenerateRoom(Spec("Living room", 2.0, 0., 0.5)).channels[0], 2.0, 2.5, dCard, rCard);
    CHECK(10. * std::log10((dOmni / rOmni) / (dCard / rCard)) < -2., "an omni hears more room than a cardioid (%.1f dB)",
          10. * std::log10((dOmni / rOmni) / (dCard / rCard)));
  }

  printf("\n-- stereo pairs\n");
  {
    const RoomIR xy = GenerateRoom(Spec("Living room", 2.0, 0., 0.5, true, 0));
    const RoomIR sp = GenerateRoom(Spec("Living room", 2.0, 0., 1., true, 2));
    auto firstArrival = [](const std::vector<float>& h) { size_t i = 0; while (std::fabs(h[i]) < 1e-3f) i++; return i; };
    double diff = 0.;
    for (size_t i = 0; i < xy.channels[0].size(); i++) diff += std::fabs(xy.channels[0][i] - xy.channels[1][i]);
    CHECK(xy.channels.size() == 2 && firstArrival(xy.channels[0]) == firstArrival(xy.channels[1]) && diff > 1.,
          "XY: both mics hear the direct sound at once, but the room differently");
    double cxy = 0., nxy0 = 0., nxy1 = 0., csp = 0., nsp0 = 0., nsp1 = 0.;
    const size_t from = (size_t)(0.15 * 48000), to = (size_t)(0.6 * 48000);
    for (size_t i = from; i < to; i++)
    {
      cxy += xy.channels[0][i] * xy.channels[1][i]; nxy0 += xy.channels[0][i] * xy.channels[0][i]; nxy1 += xy.channels[1][i] * xy.channels[1][i];
      csp += sp.channels[0][i] * sp.channels[1][i]; nsp0 += sp.channels[0][i] * sp.channels[0][i]; nsp1 += sp.channels[1][i] * sp.channels[1][i];
    }
    const double rxy = cxy / std::sqrt(nxy0 * nxy1), rsp = csp / std::sqrt(nsp0 * nsp1);
    CHECK(rxy > rsp + 0.3, "the tail is more alike in XY (correlation %.2f) than spaced (%.2f)", rxy, rsp);
  }

  printf("\n-- moving the source\n");
  {
    // Energy arriving 0.3-3 ms after the direct sound, relative to it: near a wall the wall's
    // reflection lands there (with the mic 1 m away along the wall, it's 0.5 ms behind); in the
    // middle of the room nothing does.
    auto earlyEcho = [](double fx, double fy) {
      RoomSpec s;
      for (const auto& p : RoomPresets()) if (std::string(p.name) == "Living room") s.shape = MakeShape(p, 0., 0.);
      s.shape.scattering = 0.;
      const Placement p = PlaceMics(s.shape, fx, fy, 0., 1.0, 0., false, 0); // mic 1 m away along +x
      s.source = p.source;
      s.mics = p.mics;
      s.pickup.a.fill(1.);
      const RoomIR room = GenerateRoom(s);
      const auto& h = room.channels[0];
      const size_t direct = (size_t)(1.0 / 343. * 48000.);
      double d = 0., e = 0.;
      for (size_t i = direct - 6; i < direct + 14; i++) d += (double)h[i] * h[i];
      for (size_t i = direct + 14; i < direct + 144; i++) e += (double)h[i] * h[i];
      return 10. * std::log10(e / d);
    };
    const double middle = earlyEcho(0.3, 0.5), wall = earlyEcho(0.3, 0.06);
    CHECK(wall > middle + 6., "a source near a wall gets a strong reflection right behind the direct sound (%.1f dB vs %.1f dB)", wall, middle);
    const Placement corner = PlaceMics(MakeShape(RoomPresets()[4], 0., 0.), 0.95, 0.95, 0., 3.0, 0., false, 0);
    CHECK(corner.maxDistance <= 0.11 && corner.mics[0].position.x <= 4.95, "the mic can't go through a wall (max %.2f m)", corner.maxDistance);
  }

  printf("\n-- level and speed\n");
  {
    double worst = 0., slowest = 0.;
    for (const auto& p : RoomPresets())
      for (double d : {0.1, 1.0, 100.})
      {
        RoomSpec s = Spec(p.name, d, 0., 1., true, 2);
        const auto t0 = std::chrono::steady_clock::now();
        const RoomIR r = GenerateRoom(s);
        slowest = std::max(slowest, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        for (const auto& c : r.channels)
        {
          double e = 0.;
          for (float v : c) e += (double)v * v;
          worst = std::max(worst, e);
        }
      }
    CHECK(worst <= 4.0001, "no room is more than +6 dB (largest energy %.2f)", worst);
    CHECK(slowest < 1.0, "every preset generates in under a second (slowest %.2f s, stereo)", slowest);
    const RoomIR a = GenerateRoom(Spec("Kitchen", 1.2)), b = GenerateRoom(Spec("Kitchen", 1.2));
    CHECK(a.channels[0] == b.channels[0], "the same settings give the same room");
  }

  {
    // For underheard-reverb: Decay stretches the room; and the direct sound can be left out.
    RoomSpec s = Spec("Living room", 2.);
    s.maxSeconds = 12.;
    const double rt1 = MeasuredRT1k(GenerateRoom(s).channels[0], 0.1);
    s.decayScale = 2.;
    const double rt2 = MeasuredRT1k(GenerateRoom(s).channels[0], 0.1);
    CHECK(rt2 / rt1 > 1.8 && rt2 / rt1 < 2.2, "decay scale 2 doubles the decay (%.2f s -> %.2f s)", rt1, rt2);
    s.decayScale = 1.;
    double withD = 0., withRest = 0., noD = 0., noRest = 0.;
    Split(GenerateRoom(s).channels[0], 2., 1., withD, withRest);
    s.direct = false;
    Split(GenerateRoom(s).channels[0], 2., 1., noD, noRest);
    CHECK(noD < withD * 0.01 && std::fabs(noRest / withRest - 1.) < 0.05, "without the direct sound, only the reflections and tail are left");
  }

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
