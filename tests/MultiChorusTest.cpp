// Offline tests for libs/fx/MultiChorus (underheard-chorus).
#include "MultiChorus.h"


#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <tuple>
#include <utility>
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

struct Stereo
{
  std::vector<float> l, r;
};

static Stereo Run(MultiChorus& c, const std::vector<float>& in)
{
  Stereo s{std::vector<float>(in.size()), std::vector<float>(in.size())};
  gCountAllocs = true;
  for (size_t i = 0; i < in.size(); i++)
  {
    c.Process(in[i], in[i], s.l[i], s.r[i]);
    if (!std::isfinite(s.l[i]) || !std::isfinite(s.r[i]))
    {
      fails++;
      printf("FAIL: non-finite output\n");
      break;
    }
  }
  gCountAllocs = false;
  return s;
}

static std::vector<float> Impulse(double secs, float level = 1.f)
{
  std::vector<float> v((size_t)(secs * FS), 0.f);
  v[0] = level;
  return v;
}
static std::vector<float> Noise(double secs, double level)
{
  Rand r;
  std::vector<float> v((size_t)(secs * FS));
  for (auto& x : v)
    x = (float)(level * r.Bipolar());
  return v;
}
static double Rms(const std::vector<float>& v, size_t from, size_t to)
{
  double e = 0.;
  for (size_t i = from; i < to; i++)
    e += (double)v[i] * v[i];
  return std::sqrt(e / (double)(to - from));
}

// One clean voice, wet only.
static void Clean(MultiChorus& c, double delayMs, double depth)
{
  c.SetVoices(1);
  c.SetMode(MultiChorus::kVibrato);
  c.SetDelay(delayMs);
  c.SetDepth(depth);
  c.SetAge(0.);
  c.SetTone(20000.);
  c.SetFeedback(0.);
  c.SetSpread(1.);
  c.Prepare(FS);
}

// Runs silence for `secs`, calling `each` after every sample.
template <class F>
static void Track(MultiChorus& c, double secs, F each)
{
  for (int i = 0; i < (int)(secs * FS); i++)
  {
    float l, r;
    c.Process(0.f, 0.f, l, r);
    each();
  }
}

int main()
{
  {
    MultiChorus c;
    Clean(c, 10., 0.);
    auto s = Run(c, Impulse(0.05));
    size_t p = 0;
    for (size_t i = 0; i < s.l.size(); i++)
      if (std::fabs(s.l[i]) > std::fabs(s.l[p])) p = i;
    CHECK(p == 480 && std::fabs(s.l[0]) < 1e-6, "no depth: the voice is the input 10 ms later, no dry in vibrato (peak %zu)", p);
  }
  {
    MultiChorus c;
    Clean(c, 10., 1.);
    c.SetRate(1.);
    double lo = 1e9, hi = 0.;
    Track(c, 2., [&] { lo = std::min(lo, c.VoiceDelayMs(0, 0)); hi = std::max(hi, c.VoiceDelayMs(0, 0)); });
    CHECK(std::fabs(lo - 6.) < 0.01 && std::fabs(hi - 14.) < 0.01, "full depth swings 8 ms round the centre (%.2f .. %.2f ms)", lo, hi);
    MultiChorus c2;
    Clean(c2, 2., 1.);
    lo = 1e9;
    Track(c2, 2., [&] { lo = std::min(lo, c2.VoiceDelayMs(0, 0)); });
    CHECK(lo > 1., "a short centre time keeps the swing above zero (lowest %.2f ms)", lo);
  }
  {
    MultiChorus c;
    Clean(c, 10., 1.);
    c.SetPhase(180.);
    double worst = 0.;
    Track(c, 1., [&] { worst = std::max(worst, std::fabs(c.VoiceDelayMs(0, 0) + c.VoiceDelayMs(1, 0) - 20.)); });
    MultiChorus c2;
    Clean(c2, 10., 1.);
    c2.SetPhase(0.);
    double apart = 0.;
    Track(c2, 1., [&] { apart = std::max(apart, std::fabs(c2.VoiceDelayMs(0, 0) - c2.VoiceDelayMs(1, 0))); });
    CHECK(worst < 1e-6 && apart < 1e-9, "phase 180: the sides move opposite; phase 0: together");
  }
  {
    MultiChorus c;
    Clean(c, 10., 1.);
    c.SetVoices(4);
    double worst = 0., spread = 0.;
    Track(c, 1., [&] {
      double sum = 0.;
      for (int v = 0; v < 4; v++)
        sum += c.VoiceDelayMs(0, v);
      worst = std::max(worst, std::fabs(sum / 4. - 10.));
      spread = std::max(spread, std::fabs(c.VoiceDelayMs(0, 0) - c.VoiceDelayMs(0, 2)));
    });
    CHECK(worst < 1e-6 && spread > 7.9, "four voices spread evenly round the cycle (opposite voices %.2f ms apart)", spread);
  }
  {
    auto at = [](int shape) {
      MultiChorus c;
      Clean(c, 10., 1.);
      c.SetShape(shape);
      c.SetRate(0.01);
      c.SyncPhase(0.125);
      Track(c, 1. / FS, [] {});
      return (c.VoiceDelayMs(0, 0) - 10.) / 4.;
    };
    CHECK(std::fabs(at(MultiChorus::kTriangle) - 0.5) < 0.01 && std::fabs(at(MultiChorus::kSine) - std::sqrt(0.5)) < 0.01,
          "an eighth of a cycle in: triangle halfway up (%.3f), sine at 0.707 (%.3f)", at(MultiChorus::kTriangle), at(MultiChorus::kSine));
  }
  {
    MultiChorus a, b;
    Clean(a, 10., 1.);
    Clean(b, 10., 1.);
    a.SetRate(2.);
    b.SetRate(2.);
    Track(a, 0.37, [] {});
    a.SyncPhase(3.6);
    b.SyncPhase(3.6);
    double worst = 0.;
    for (int i = 0; i < 4800; i++)
    {
      float l, r;
      a.Process(0.f, 0.f, l, r);
      b.Process(0.f, 0.f, l, r);
      worst = std::max(worst, std::fabs(a.VoiceDelayMs(0, 0) - b.VoiceDelayMs(0, 0)));
    }
    CHECK(worst < 1e-9, "locked to the song position: two choruses started apart move together");
  }
  {
    auto turns = [](int shape, int mode) {
      MultiChorus c;
      Clean(c, 10., 1.);
      c.SetShape(shape);
      c.SetMode(mode);
      c.SetRate(0.5);
      double lo = 1e9, hi = 0., last = 0., dir = 0.;
      int n = 0;
      Track(c, 4., [&] {
        const double d = c.VoiceDelayMs(0, 0);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
        const double nd = d > last ? 1. : d < last ? -1. : dir;
        if (dir != 0. && nd != dir) n++;
        dir = nd;
        last = d;
      });
      return std::make_tuple(n, lo, hi);
    };
    const auto [sineTurns, sl, sh] = turns(MultiChorus::kSine, MultiChorus::kChorus);
    const auto [ensTurns, el, eh] = turns(MultiChorus::kSine, MultiChorus::kEnsemble);
    const auto [wTurns, wl, wh] = turns(MultiChorus::kWander, MultiChorus::kChorus);
    CHECK(sineTurns <= 5 && ensTurns > 30, "ensemble adds a fast shimmer (%d turns in 4 s against %d)", ensTurns, sineTurns);
    CHECK(wl >= 6. - 1e-6 && wh <= 14. + 1e-6 && wh - wl > 3., "wander moves within the depth (%.2f .. %.2f ms, %d turns)", wl, wh, wTurns);
    (void)sl; (void)sh; (void)el; (void)eh;
  }
  {
    auto comb = [](double fb) {
      MultiChorus c;
      Clean(c, 2., 0.);
      c.SetFeedback(fb);
      auto s = Run(c, Impulse(0.02, 0.05f));
      // Each repeat's size (the tone filter spreads a click over a few samples) and sign.
      auto repeat = [&](size_t at) {
        double e = 0., sum = 0.;
        for (size_t i = at - 20; i < at + 20; i++)
        {
          e += (double)s.l[i] * s.l[i];
          sum += s.l[i];
        }
        return std::copysign(std::sqrt(e), sum);
      };
      return std::make_pair(repeat(96), repeat(192));
    };
    const auto [p1, p2] = comb(0.7);
    const auto [n1, n2] = comb(-0.7);
    CHECK(p2 / p1 > 0.6 && p2 / p1 < 0.75 && n2 / n1 < -0.6 && n2 / n1 > -0.75, "feedback makes flanger repeats (%.2f, inverted %.2f)", p2 / p1, n2 / n1);
  }
  {
    int bad = 0;
    double worstGrowth = -1e9;
    for (int mode : {0, 1, 2})
      for (int voices : {1, 2, 4})
        for (double delay : {0.5, 3., 20.})
          for (double fb : {0.9, -0.9})
          {
            MultiChorus c;
            Clean(c, delay, 1.);
            c.SetMode(mode);
            c.SetVoices(voices);
            c.SetFeedback(fb);
            c.SetRate(3.);
            auto s = Run(c, Impulse(1., 0.01f));
            const double early = Rms(s.l, 0, 2400), late = Rms(s.l, 24000, 26400);
            const double g = 20. * std::log10((late + 1e-30) / early);
            worstGrowth = std::max(worstGrowth, g);
            if (!(late < early)) bad++;
          }
    CHECK(bad == 0, "90%% feedback dies away in every mode, voice count and delay (worst %.0f dB after 0.5 s)", worstGrowth);
  }
  {
    MultiChorus c;
    c.SetMode(MultiChorus::kChorus);
    c.SetMix(0.);
    c.Prepare(FS);
    auto in = Noise(0.2, 0.3);
    auto s = Run(c, in);
    double err = 0.;
    for (size_t i = 0; i < in.size(); i++)
      err = std::max(err, (double)std::fabs(s.l[i] - in[i]));
    CHECK(err < 1e-6, "chorus at Mix 0 is the dry signal");
  }
  {
    auto level = [](int voices) {
      MultiChorus c;
      Clean(c, 12., 0.6);
      c.SetVoices(voices);
      auto s = Run(c, Noise(1., 0.3));
      return Rms(s.l, 4800, s.l.size());
    };
    const double d = 20. * std::log10(level(4) / level(1));
    CHECK(std::fabs(d) < 3., "four voices are about as loud as one (%+.1f dB)", d);
  }
  CHECK(gAllocs == 0, "no allocations while processing (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
