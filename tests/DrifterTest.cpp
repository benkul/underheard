// Offline tests for libs/drifter-core Drifter.
#include "Drifter.h"

#include <cmath>
#include <cstdio>

using namespace underheard;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double kBlock = 512. / 48000.;

static void Run(Drifter& d, double seconds)
{
  for (double t = 0.; t < seconds; t += kBlock)
    d.Advance(kBlock);
}

static double MaxAbs(const Drifter& d)
{
  double m = 0.;
  for (int i = 0; i < d.Count(); i++)
    m = std::max(m, std::fabs(d.Offset(i)));
  return m;
}

int main()
{
  printf("-- off by default, nothing moves\n");
  {
    Drifter d(11);
    Run(d, 30.);
    CHECK(MaxAbs(d) == 0., "offsets stay at home");
  }

  printf("\n-- drifting within Reach\n");
  {
    Drifter d(11);
    d.SetLength(1.);
    d.SetReach(0.2);
    d.SetGravity(0.);
    d.SetEnabled(true);
    double most = 0.;
    bool moved = false;
    for (int s = 0; s < 200; s++)
    {
      Run(d, 0.25);
      most = std::max(most, MaxAbs(d));
      moved = moved || MaxAbs(d) > 0.02;
    }
    CHECK(moved, "it moves");
    // With no gravity a target can random-walk; each shift adds at most Reach.
    CHECK(most <= 1., "offsets stay inside the parameter range (largest %.2f)", most);
    Drifter g(11);
    g.SetLength(1.);
    g.SetReach(0.2);
    g.SetGravity(1.);
    g.SetEnabled(true);
    double gm = 0.;
    for (int s = 0; s < 200; s++)
    {
      Run(g, 0.25);
      gm = std::max(gm, MaxAbs(g));
    }
    CHECK(gm <= 0.2 + 1e-9, "full Gravity keeps every shift within Reach of home (largest %.3f)", gm);
    Drifter z(11);
    z.SetReach(0.);
    z.SetEnabled(true);
    Run(z, 60.);
    CHECK(MaxAbs(z) == 0., "Reach 0 goes nowhere");
  }

  printf("\n-- Smear\n");
  {
    double spread = 0.;
    double first = -1.;
    // The first shift starts at 0 for all: fraction along = offset / final offset.
    Drifter f(8);
    f.SetLength(2.);
    f.SetSmear(0.);
    f.SetCurve(Drifter::kCurveLinear);
    f.SetReach(0.5);
    f.SetEnabled(true);
    Drifter fEnd = f;
    Run(fEnd, 2.0 - kBlock);
    Run(f, 1.0);
    for (int i = 0; i < 8; i++)
    {
      const double frac = f.Offset(i) / fEnd.Offset(i);
      if (first < 0.) first = frac;
      spread = std::max(spread, std::fabs(frac - first));
    }
    CHECK(spread < 1e-6, "Smear 0: all targets move in step (spread %.2g)", spread);

    Drifter s(8);
    s.SetLength(2.);
    s.SetSmear(1.);
    s.SetCurve(Drifter::kCurveLinear);
    s.SetReach(0.5);
    s.SetEnabled(true);
    Drifter sEnd = s;
    Run(sEnd, 2.0 - kBlock);
    Run(s, 0.6);
    double lo = 2., hi = -1.;
    for (int i = 0; i < 8; i++)
    {
      const double frac = s.Offset(i) / sEnd.Offset(i);
      lo = std::min(lo, frac);
      hi = std::max(hi, frac);
    }
    CHECK(hi - lo > 0.2, "Smear 1: targets are at different points of the shift (%.2f to %.2f along)", lo, hi);
  }

  printf("\n-- curves\n");
  {
    Drifter d(1);
    const char* names[] = {"linear", "smooth", "fast start", "slow start"};
    const double expect[] = {0.25, 0.15625, 0.578125, 0.015625};
    for (int c = 0; c < Drifter::kNumCurves; c++)
    {
      d.SetCurve(c);
      CHECK(d.Shape(0.) == 0. && d.Shape(1.) == 1. && std::fabs(d.Shape(0.25) - expect[c]) < 1e-9, "%s: 0 -> 1, and %.3f a quarter of the way", names[c], d.Shape(0.25));
    }
  }

  printf("\n-- Length takes effect at once\n");
  {
    Drifter d(4);
    d.SetLength(100.);
    d.SetReach(0.5);
    d.SetEnabled(true);
    Run(d, 10.);
    const double p = d.Progress();
    d.SetLength(1.);
    Run(d, 0.5);
    CHECK(p < 0.2 && d.Progress() > p + 0.4, "a long shift speeds up when Length drops (%.2f -> %.2f)", p, d.Progress());
  }

  printf("\n-- Return, switching off, Rebase\n");
  {
    Drifter d(6);
    d.SetLength(2.);
    d.SetReach(0.6);
    d.SetGravity(0.);
    d.SetEnabled(true);
    Run(d, 3.);
    CHECK(MaxAbs(d) > 0.05, "drifted away (%.2f)", MaxAbs(d));
    d.Return();
    Run(d, 2.0 + kBlock);
    CHECK(MaxAbs(d) < 1e-9, "Return arrives home after one Length");
    Run(d, 3.);
    CHECK(MaxAbs(d) > 0.01, "then drifts on");

    d.SetLength(30.);
    d.SetEnabled(false);
    Run(d, 2.0 + kBlock);
    CHECK(MaxAbs(d) < 1e-9, "switching off glides home within 2 s, even with a long Length");
    Run(d, 30.);
    CHECK(MaxAbs(d) == 0., "and stays there");

    Drifter r(6);
    r.SetLength(2.);
    r.SetReach(0.6);
    r.SetEnabled(true);
    Run(r, 3.);
    r.Rebase();
    CHECK(MaxAbs(r) == 0., "Rebase: offsets become zero");
    Run(r, 1.);
    CHECK(MaxAbs(r) > 0.001, "and drifting continues from there");
  }

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
