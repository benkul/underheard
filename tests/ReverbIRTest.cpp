// Offline tests for libs/room/ReverbIR: shaping impulses for underheard-reverb.
#include "ReverbIR.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace underheard::room;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double FS = 48000.;

// A recording-like impulse: silence, a direct click at 10 ms, then an exponential noise tail
// from 15 ms with the given decay time.
static Channels Recording(double rt, double secs, int channels = 2)
{
  Channels irs((size_t)channels, std::vector<float>((size_t)(secs * FS), 0.f));
  uint32_t s = 1;
  for (size_t ch = 0; ch < irs.size(); ch++)
  {
    irs[ch][480] = 1.f;
    for (size_t i = 720; i < irs[ch].size(); i++)
    {
      s = s * 1664525u + 1013904223u;
      const double noise = (double)(int32_t)s / 2147483648.;
      irs[ch][i] = (float)(0.3 * noise * std::exp(-3. * std::log(10.) * (double)(i - 720) / FS / rt));
    }
  }
  return irs;
}
static double Energy(const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
  double e = 0.;
  for (size_t i = from; i < std::min(to, v.size()); i++)
    e += (double)v[i] * v[i];
  return e;
}

int main()
{
  {
    Channels irs = Recording(1., 3.);
    CHECK(Onset(irs) == 480, "the onset is the direct click (%zu)", Onset(irs));
    const double tail = Energy(irs[0], 720);
    RemoveDirect(irs, FS);
    CHECK(irs[0][480] == 0.f && std::fabs(Energy(irs[0], 720) / tail - 1.) < 1e-6, "direct sound removed, the tail untouched");
  }
  {
    Channels irs = Recording(1., 4.);
    CHECK(std::fabs(MeasureDecay(irs, FS, 720) - 1.) < 0.05, "measures a 1 s decay (%.3f)", MeasureDecay(irs, FS, 720));
    for (double stretch : {0.5, 1.5, 2.})
    {
      Channels s = Recording(1., 6.);
      StretchDecay(s, FS, stretch, 0.08);
      const double rt = MeasureDecay(s, FS, 480 + (size_t)(0.1 * FS));
      CHECK(std::fabs(rt / stretch - 1.) < 0.15, "stretch x%.1f: decay %.2f s", stretch, rt);
    }
    Channels s = Recording(1., 3.);
    StretchDecay(s, FS, 4., 0.08);
    double peak = 0.;
    for (float v : s[0]) peak = std::max(peak, (double)std::fabs(v));
    CHECK(std::isfinite(peak) && std::fabs(s[0].back()) < 1e-6, "stretched past the recording, it fades out at the end");
  }
  {
    const double mix = 0.08;
    const size_t t0 = (size_t)(mix * FS), t1 = (size_t)((mix + 0.02) * FS);
    Channels early = Recording(1., 2.), late = Recording(1., 2.), both = Recording(1., 2.);
    const double whole = Energy(both[0]);
    Balance(early, FS, -1., mix);
    Balance(late, FS, 1., mix);
    Balance(both, FS, 0., mix);
    CHECK(Energy(early[0], t1) == 0. && Energy(early[0], 0, t0) > 0., "early only: nothing after the mixing time");
    CHECK(Energy(late[0], 0, t0) == 0. && Energy(late[0], t1) > 0., "late only: nothing before it");
    CHECK(Energy(both[0]) == whole, "both: unchanged");
    Channels half = Recording(1., 2.);
    Balance(half, FS, 0.5, mix);
    CHECK(std::fabs(Energy(half[0], 0, t0) / Energy(Recording(1., 2.)[0], 0, t0) - 0.25) < 1e-6, "halfway to late: the early part at half level");
    NormaliseEnergy(half);
    CHECK(std::fabs(std::max(Energy(half[0]), Energy(half[1])) - 1.) < 1e-4, "normalised: the louder channel has unit energy");
  }
  {
    Channels irs = Recording(10., 1.);
    const double mid = std::fabs(irs[0][24000]);
    FadeEnd(irs, 0.3);
    CHECK(std::fabs(irs[0].back()) < 1e-4 * mid && std::fabs(irs[0][24000]) == (float)mid && std::fabs(irs[0][40000]) < std::fabs(Recording(10., 1.)[0][40000]),
          "a cut-short impulse fades out over its last 30%%");
  }
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
