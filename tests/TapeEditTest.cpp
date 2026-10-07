// Offline tests for libs/tape-core TapeEdit (razor cuts).
#include "TapeEdit.h"

#include <cmath>
#include <cstdio>

using namespace underheard;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static double MapOr(const TapeEdit& e, double p, double none = -1.)
{
  double n = 0.;
  return e.Map(p, n) ? n : none;
}

int main()
{
  printf("-- remove\n");
  {
    const TapeEdit e = MakeRemove(1000, 200, 100, 10);
    CHECK(e.count == 1 && e.NewLength() == 900, "a 100-frame cut from 1000 leaves 900");
    CHECK(e.SourceFrame(0) == 300 && e.SourceFrame(699) == 999 && e.SourceFrame(700) == 0 && e.SourceFrame(899) == 199,
          "the new loop starts right after the cut and runs round to just before it");
    CHECK(MapOr(e, 250.) < 0. && e.MapHead(250.) == 0., "a head inside the cut jumps to the join");
    CHECK(MapOr(e, 500.) == 200. && MapOr(e, 100.) == 800. && MapOr(e, 300.5) == 0.5, "other positions keep their place in the audio");
    CHECK(MakeRemove(1000, 0, 995, 10).count == 0, "refuses to leave less than the minimum");
  }

  printf("\n-- isolate, across the seam\n");
  {
    const TapeEdit e = MakeIsolate(1000, 900, 200, 10);
    CHECK(e.NewLength() == 200 && e.SourceFrame(0) == 900 && e.SourceFrame(150) == 50, "keeps 900..999 then 0..99");
    CHECK(MapOr(e, 50.) == 150. && MapOr(e, 500.) < 0., "maps inside, drops outside");
    CHECK(MakeIsolate(1000, 0, 5, 10).count == 0 && MakeIsolate(1000, 0, 1000, 10).count == 0, "refuses too short or the whole loop");
  }

  printf("\n-- reverse\n");
  {
    const TapeEdit e = MakeReverse(1000, 200, 100);
    CHECK(e.count == 2 && e.NewLength() == 1000, "same length, two pieces");
    CHECK(e.SourceFrame(0) == 299 && e.SourceFrame(99) == 200 && e.SourceFrame(100) == 300 && e.SourceFrame(999) == 199,
          "the flipped cut, then the rest of the loop");
    CHECK(MapOr(e, 299.) == 0. && MapOr(e, 200.) == 99. && MapOr(e, 299.5) == 0. && MapOr(e, 300.) == 100. && MapOr(e, 100.) == 900.,
          "positions map frame for frame, mirrored inside the cut");
    CHECK(e.StartsJoin(0) && e.StartsJoin(1), "both ends of the flipped cut are joins");
  }

  printf("\n-- rendering with crossfaded joins\n");
  {
    const int64_t L = 48000;
    TapeStorage from(L), to(L);
    from.Reserve(L);
    to.Reserve(L);
    for (int64_t i = 0; i < L; i++)
    {
      float* f = from.Frame(i);
      f[0] = f[1] = 0.5f * (float)std::sin(2. * 3.14159265358979 * 441. * (double)i / 48000.);
    }
    const TapeEdit e = MakeRemove(L, 10000, 7777, 100); // cut lands mid-cycle
    RenderEdit(e, from, to, 96);
    double maxStep = 0.;
    for (int64_t k = 1; k < e.NewLength() + 1; k++)
    {
      const double a = to.Frame((k - 1) % e.NewLength())[0], b = to.Frame(k % e.NewLength())[0];
      maxStep = std::fmax(maxStep, std::fabs(b - a));
    }
    CHECK(maxStep < 0.04, "no jump at the join, including around the new seam (max step %.4f)", maxStep);
    CHECK(to.Frame(500)[0] == from.Frame(10000 + 7777 + 500)[0], "away from the join the audio is copied exactly");

    const TapeEdit r = MakeReverse(L, 12345, 6000);
    RenderEdit(r, from, to, 96);
    maxStep = 0.;
    for (int64_t k = 1; k < L + 1; k++)
      maxStep = std::fmax(maxStep, (double)std::fabs(to.Frame(k % L)[0] - to.Frame((k - 1) % L)[0]));
    CHECK(maxStep < 0.04, "reversing has no jumps at either end (max step %.4f)", maxStep);
  }

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
