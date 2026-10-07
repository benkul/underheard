// Offline tests for libs/fx/ReverbCore (underheard-reverb's signal path).
#include "ReverbCore.h"


#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

using namespace underheard;
using namespace underheard::fx;

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

static const double FS = 48000.;
static const int BLOCK = 256;

// A stand-in convolver: a feedback comb per side (a crude, long-ish "room"), or a plain copy.
struct FakeRoom
{
  std::vector<double> l = std::vector<double>(4096, 0.), r = std::vector<double>(4096, 0.);
  size_t pos = 0;
  bool comb = true;
  void operator()(const float* mono, float* outL, float* outR, int n)
  {
    for (int i = 0; i < n; i++)
    {
      if (!comb)
      {
        outL[i] = outR[i] = mono[i];
        continue;
      }
      const double yl = mono[i] + 0.9 * l[(pos + 4096 - 1500) % 4096], yr = mono[i] + 0.9 * r[(pos + 4096 - 1733) % 4096];
      l[pos] = yl;
      r[pos] = yr;
      pos = (pos + 1) % 4096;
      outL[i] = (float)(0.4 * yl);
      outR[i] = (float)(0.4 * yr);
    }
  }
};

struct Out
{
  std::vector<float> l, r;
};

// Runs `secs` of input from `in(sample)` through the core in blocks.
template <class In>
static Out Run(ReverbCore& c, FakeRoom& room, double secs, In in)
{
  const size_t n = (size_t)(secs * FS);
  Out o{std::vector<float>(n), std::vector<float>(n)};
  std::vector<float> a(BLOCK), b(BLOCK);
  for (size_t s = 0; s < n; s += BLOCK)
  {
    const int m = (int)std::min((size_t)BLOCK, n - s);
    for (int i = 0; i < m; i++)
      a[(size_t)i] = b[(size_t)i] = in(s + (size_t)i);
    gCountAllocs = true;
    c.Process(a.data(), b.data(), o.l.data() + s, o.r.data() + s, m, room);
    gCountAllocs = false;
  }
  for (float x : o.l)
    if (!std::isfinite(x)) { fails++; printf("FAIL: non-finite output\n"); break; }
  return o;
}

static double Rms(const std::vector<float>& v, double from, double to)
{
  double e = 0.;
  const size_t a = (size_t)(from * FS), b = std::min(v.size(), (size_t)(to * FS));
  for (size_t i = a; i < b; i++)
    e += (double)v[i] * v[i];
  return std::sqrt(e / (double)(b - a));
}
static double Db(double x) { return 20. * std::log10(x); }

static ReverbCore Make(int engine, double mix = 1.)
{
  ReverbCore c;
  c.SetEngine(engine);
  c.SetMix(mix);
  c.SetAge(0.);
  c.SetLowCut(20.);
  c.SetHighCut(20000.);
  c.SetPreDelay(0.);
  c.Prepare(FS, BLOCK);
  return c;
}

static Rand gRand;
static float Noise(size_t) { return 0.3f * gRand.Bipolar(); }

int main()
{
  {
    // Pre-delay, through a plain-copy "room": the first sound arrives exactly then.
    ReverbCore c = Make(ReverbCore::kRecordings);
    c.SetPreDelay(0.05);
    FakeRoom room;
    room.comb = false;
    auto o = Run(c, room, 0.2, [](size_t i) { return i == 0 ? 1.f : 0.f; });
    size_t first = 0;
    while (first < o.l.size() && std::fabs(o.l[first]) < 1e-3f) first++;
    CHECK(first == 2400, "pre-delay: the reverb starts 50 ms late (%zu samples)", first);
  }
  {
    // The engines: Rooms and Recordings go through the convolver, Plate and Hall don't.
    for (int e : {0, 1, 2, 3})
    {
      ReverbCore c = Make(e);
      FakeRoom room;
      room.comb = false;
      auto o = Run(c, room, 0.3, [](size_t i) { return i == 0 ? 1.f : 0.f; });
      const bool conv = std::fabs(o.l[0]) > 0.1f; // the plain copy answers at once
      CHECK(conv == ReverbCore::IsConvolution(e), "engine %d %s the convolver", e, conv ? "uses" : "doesn't use");
    }
  }
  {
    // Switching engine fades the wet out and back in, rather than cutting.
    ReverbCore c = Make(ReverbCore::kHall);
    FakeRoom room;
    gRand.s = 3;
    auto before = Run(c, room, 1., Noise);
    c.SetEngine(ReverbCore::kRooms);
    auto o = Run(c, room, 0.5, Noise);
    const double steady = Rms(before.l, 0.5, 1.);
    double lowest = 1e9, highest = 0.;
    for (double t = 0.; t < 0.1; t += 0.005)
    {
      lowest = std::min(lowest, Rms(o.l, t, t + 0.005));
      highest = std::max(highest, Rms(o.l, t, t + 0.005));
    }
    CHECK(c.Engine() == ReverbCore::kRooms && lowest < 0.3 * steady && Rms(o.l, 0.3, 0.5) > 0.3 * steady,
          "switching engine dips the wet (to %.0f%%) and brings it back", 100. * lowest / steady);
    (void)highest;
  }
  for (int e : {ReverbCore::kRooms, ReverbCore::kHall})
  {
    // Freeze holds about the level it had, nothing new gets in, and release lets it go.
    auto frozen = [&](uint32_t seed, double& before, Out& o) {
      ReverbCore c = Make(e);
      FakeRoom room;
      gRand.s = 5;
      auto pre = Run(c, room, 1.5, Noise);
      before = Rms(pre.l, 1., 1.5);
      c.SetFreeze(true);
      gRand.s = 6;
      auto a = Run(c, room, 0.5, Noise);
      gRand.s = seed; // different input from here
      o = Run(c, room, 6., Noise);
      c.SetFreeze(false);
      return Run(c, room, 8., [](size_t) { return 0.f; });
    };
    double before1 = 0., before2 = 0.;
    Out x, y;
    const Out released = frozen(1, before1, x);
    frozen(2, before2, y);
    const double held1 = Rms(x.l, 0.5, 1.5), held2 = Rms(x.l, 5., 6.);
    double diff = 0.;
    for (size_t i = 0; i < x.l.size(); i++)
      diff = std::max(diff, (double)std::fabs(x.l[i] - y.l[i]));
    const char* name = e == ReverbCore::kRooms ? "rooms" : "hall";
    CHECK(std::fabs(Db(held2 / held1)) < 0.5, "%s: frozen, it holds (%+.2f dB over 4.5 s)", name, Db(held2 / held1));
    CHECK(std::fabs(Db(held1 / before1)) < 4., "%s: at about the level before (%+.1f dB)", name, Db(held1 / before1));
    CHECK(diff < 1e-6, "%s: frozen, new input doesn't get in (%.2g)", name, diff);
    CHECK(Rms(released.l, 7., 8.) < held1 * 0.01, "%s: released (in silence), it fades away", name);
  }
  {
    ReverbCore c = Make(ReverbCore::kHall, 0.);
    FakeRoom room;
    gRand.s = 9;
    std::vector<float> in;
    auto o = Run(c, room, 0.5, [&](size_t) { in.push_back(Noise(0)); return in.back(); });
    double err = 0.;
    for (size_t i = 0; i < in.size(); i++)
      err = std::max(err, (double)std::fabs(o.l[i] - in[i]));
    CHECK(err < 1e-6, "Mix at dry is the dry signal");
  }
  {
    auto wet = [](double width, double highCut, double duck, double age, double& lrDiff) {
      ReverbCore c = Make(ReverbCore::kHall);
      c.SetWidth(width);
      c.SetHighCut(highCut);
      c.SetDuck(duck);
      c.SetAge(age);
      c.Prepare(FS, BLOCK);
      FakeRoom room;
      gRand.s = 11;
      auto o = Run(c, room, 2., Noise);
      lrDiff = 0.;
      for (size_t i = 0; i < o.l.size(); i++)
        lrDiff = std::max(lrDiff, (double)std::fabs(o.l[i] - o.r[i]));
      return Rms(o.l, 1., 2.);
    };
    double d0 = 0., d1 = 0., dd = 0.;
    const double plain = wet(1., 20000., 0., 0., d1);
    wet(0., 20000., 0., 0., d0);
    const double dark = wet(1., 1000., 0., 0., dd), ducked = wet(1., 20000., 1., 0., dd), aged = wet(1., 20000., 0., 1., dd);
    CHECK(d0 < 1e-6 && d1 > 0.01, "width 0 is mono, 1 keeps the sides");
    CHECK(Db(dark / plain) < -3., "high cut takes out the highs (%+.1f dB)", Db(dark / plain));
    CHECK(Db(ducked / plain) < -6., "duck dips the reverb while playing (%+.1f dB)", Db(ducked / plain));
    CHECK(Db(aged / plain) < -1., "age softens it (%+.1f dB)", Db(aged / plain));
  }
  CHECK(gAllocs == 0, "no allocations while processing (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
