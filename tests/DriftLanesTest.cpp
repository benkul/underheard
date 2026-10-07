// Offline tests for libs/fx/libs/drifter-core/DriftLanes.h (Drifter's lanes).
#include "DriftLanes.h"


#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

using namespace underheard;


static std::atomic<bool> gCountAllocs{false};
static std::atomic<int> gAllocs{0};
void* operator new(size_t n)
{
  if (gCountAllocs)
    gAllocs++;
  if (void* p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void* operator new[](size_t n) { return operator new(n); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double kBlock = 512. / 48000.;

// Runs `secs` of blocks (playing or not), calling each(lanes) after every block.
template <class F>
static void Run(DriftLanes& d, double secs, bool playing, F each, double tempo = 120., double beats = 4.)
{
  for (int b = 0; b < (int)(secs / kBlock); b++)
  {
    gCountAllocs = true;
    d.Advance(kBlock, playing, tempo, beats);
    gCountAllocs = false;
    each(d);
  }
}
static void Run(DriftLanes& d, double secs, bool playing = true) { Run(d, secs, playing, [](DriftLanes&) {}); }

static DriftLanes Make(double length = 2.)
{
  DriftLanes d;
  d.SetLengthSeconds(length);
  d.Engine().SetReach(1.);
  d.Engine().SetSmear(0.5);
  d.Engine().SetEnabled(true);
  return d;
}

int main()
{
  {
    DriftLanes d = Make();
    d.Add(10, 0.5, 0.3, 0.7);
    d.Add(20, 0.2);
    CHECK(d.Count() == 2 && d.Add(10, 0.9) == -1, "two lanes; the same parameter can't be added twice");
    double lo = 1., hi = 0., lo1 = 1., hi1 = 0., atEdge = 0., n = 0.;
    bool differ = false;
    Run(d, 30., true, [&](DriftLanes& x) {
      lo = std::min(lo, x.Value(0));
      hi = std::max(hi, x.Value(0));
      lo1 = std::min(lo1, x.Value(1));
      hi1 = std::max(hi1, x.Value(1));
      atEdge += x.Value(1) <= 1e-6 ? 1. : 0.;
      n += 1.;
      differ = differ || std::fabs(x.Engine().Offset(0) - x.Engine().Offset(1)) > 0.1;
    });
    CHECK(lo >= 0.3 - 1e-9 && hi <= 0.7 + 1e-9 && hi - lo > 0.2, "a lane drifts within its range (0.3 .. 0.7: went %.2f .. %.2f)", lo, hi);
    CHECK(differ && lo1 < 0.15 && hi1 > 0.25, "another lane drifts on its own path, both ways from a home near the bottom (0.2: went %.2f .. %.2f)", lo1, hi1);
    CHECK(atEdge / n < 0.05, "and doesn't sit pinned at the edge (%.0f%% of the time)", 100. * atEdge / n);
    const double v0 = d.Value(0), v1 = d.Value(1);
    Run(d, 10., false);
    CHECK(d.Value(0) == v0 && d.Value(1) == v1, "stopped: the lanes hold where they are");
  }
  {
    // Timing in bars: 2 bars of 4/4 at 120 BPM is 4 s a shift.
    DriftLanes d = Make();
    d.SetLengthBars(2.);
    d.Add(1, 0.5);
    Run(d, 0.01, true);
    const double p0 = d.Engine().Progress();
    Run(d, 2., true);
    const double half = d.Engine().Progress() - p0;
    Run(d, 0.01, true, [](DriftLanes&) {}, 60., 3.); // 60 BPM in 3/4: 2 bars is 6 s
    const double q0 = d.Engine().Progress();
    Run(d, 1.5, true, [](DriftLanes&) {}, 60., 3.);
    CHECK(std::fabs(half - 0.5) < 0.02 && std::fabs(d.Engine().Progress() - q0 - 0.25) < 0.02, "Length in bars follows the tempo and meter (2 bars: 4 s at 120 in 4/4, 6 s at 60 in 3/4)");
  }
  {
    // Grabbing: held at the user's value; let go, that's home, no jump; then it drifts on.
    DriftLanes d = Make();
    d.Add(7, 0.5);
    Run(d, 3., true);
    const double drifted = d.Value(0);
    d.OnEdit(DriftLanes::Edit::kBegin, 7, 0.);
    d.OnEdit(DriftLanes::Edit::kPerform, 7, 0.8);
    Run(d, 1., true);
    const double held = d.Value(0);
    d.OnEdit(DriftLanes::Edit::kPerform, 7, 0.85);
    d.OnEdit(DriftLanes::Edit::kEnd, 7, 0.);
    const double released = d.Value(0);
    Run(d, 3., true);
    CHECK(std::fabs(drifted - 0.5) > 0.01 && held == 0.8 && released == 0.85 && d.LaneAt(0).home == 0.85 && std::fabs(d.Value(0) - 0.85) > 0.01,
          "grabbed: held at the user's value (0.80), released as the new home (0.85, no jump), then drifting from there");
  }
  {
    // Automation through a slot moves the home; the drift rides on top.
    DriftLanes d = Make();
    d.SetRange(-1, 0., 1.);
    d.Add(3, 0.4, 0., 1.);
    Run(d, 3., true);
    const double offset = d.Engine().Offset(0);
    d.OnAutomation(3, 0.6);
    const double room = offset < 0. ? 0.6 : 0.4;
    CHECK(d.LaneAt(0).home == 0.6 && d.Engine().Offset(0) == offset && std::fabs(d.Value(0) - (0.6 + offset * room)) < 1e-9 && d.Value(0) != 0.6,
          "automation moves the home; the drift carries on around it");
    d.OnAutomation(3, 0.95);
    CHECK(d.LaneAt(0).home == 0.95 && d.Value(0) <= 1., "a home stays within 0..1");
  }
  {
    // A home outside the range widens it.
    DriftLanes d = Make();
    d.Add(5, 0.5, 0.4, 0.6);
    d.OnAutomation(5, 0.9);
    CHECK(d.LaneAt(0).hi == 0.9 && d.LaneAt(0).lo == 0.4, "moving the home past the range widens the range");
  }
  {
    // Learn: the next edit to a parameter that isn't a lane adds it.
    DriftLanes d = Make();
    d.Add(1, 0.5);
    d.StartLearn();
    d.OnEdit(DriftLanes::Edit::kBegin, 1, 0.); // already a lane: just a grab
    d.OnEdit(DriftLanes::Edit::kEnd, 1, 0.);
    CHECK(d.Learning() && d.Count() == 1, "learning ignores parameters that are already lanes");
    d.OnEdit(DriftLanes::Edit::kBegin, 42, 0.);
    d.OnEdit(DriftLanes::Edit::kPerform, 42, 0.33);
    d.OnEdit(DriftLanes::Edit::kEnd, 42, 0.);
    CHECK(!d.Learning() && d.Count() == 2 && d.Learned() == 1 && d.LaneAt(1).id == 42 && d.LaneAt(1).home == 0.33,
          "the next one moved becomes a lane, at the value it was moved to");
  }
  {
    // Removing a lane leaves the others where they are.
    DriftLanes d = Make();
    d.Add(1, 0.5);
    d.Add(2, 0.5);
    d.Add(3, 0.5);
    Run(d, 3., true);
    const double third = d.Value(2);
    d.Remove(1);
    CHECK(d.Count() == 2 && d.LaneAt(1).id == 3 && d.Value(1) == third, "removing a lane leaves the others exactly where they were");
    for (int i = 0; i < 6; i++)
      d.Add(100 + (uint32_t)i, 0.5);
    CHECK(d.Count() == DriftLanes::kMaxLanes && d.Add(999, 0.5) == -1, "8 lanes at most");
  }
  {
    // KEEP, RETURN, a lane off, drift off.
    DriftLanes d = Make(1.);
    d.Add(1, 0.5);
    d.Add(2, 0.5);
    Run(d, 2.5, true);
    const double a = d.Value(0);
    d.Keep();
    CHECK(d.LaneAt(0).home == a && d.Value(0) == a, "KEEP: where it drifted is its home now (no jump)");
    Run(d, 2.5, true);
    d.Return();
    Run(d, 1.05, true);
    const double back = d.Value(0);
    CHECK(std::fabs(back - a) < 0.02, "RETURN: it glides home over one Length (%.3f, home %.3f)", back, a);
    d.SetOn(1, false);
    CHECK(d.Value(1) == d.LaneAt(1).home, "a lane switched off sits at its home");
    d.Engine().SetEnabled(false);
    Run(d, 3., true);
    CHECK(std::fabs(d.Value(0) - d.LaneAt(0).home) < 1e-9, "Drift off: everything glides home and stays");
    DriftLanes::Change c[DriftLanes::kMaxLanes];
    CHECK(d.Changes(c) == 2 && c[0].id == 1 && c[0].value == d.Value(0), "every lane's value goes to the synth each block");
  }
  CHECK(gAllocs == 0, "no allocations while drifting (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
