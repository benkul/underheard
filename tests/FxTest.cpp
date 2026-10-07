// Offline tests for libs/fx/FxChain: Splicer's effects chain (the standalone plugins' engines).
#include "FxChain.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
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

struct Out
{
  std::vector<float> l, r;
};

// Runs `in` (on both sides) through the chain, counting allocations.
static Out Run(FxChain& fx, const std::vector<float>& in)
{
  Out o{std::vector<float>(in.size()), std::vector<float>(in.size())};
  gCountAllocs = true;
  for (size_t i = 0; i < in.size(); i++)
  {
    fx.Process(in[i], in[i], o.l[i], o.r[i]);
    if (!std::isfinite(o.l[i]) || !std::isfinite(o.r[i]))
    {
      fails++;
      printf("FAIL: non-finite output\n");
      break;
    }
  }
  gCountAllocs = false;
  return o;
}

static std::vector<float> Noise(double secs, double level)
{
  Rand r;
  std::vector<float> v((size_t)(secs * FS));
  for (auto& x : v)
    x = (float)(level * r.Bipolar());
  return v;
}
static std::vector<float> Click(double secs)
{
  std::vector<float> v((size_t)(secs * FS), 0.f);
  v[0] = 1.f;
  return v;
}
static double Rms(const std::vector<float>& v, double from, double to)
{
  double e = 0.;
  const size_t a = (size_t)(from * FS), b = std::min(v.size(), (size_t)(to * FS));
  for (size_t i = a; i < b; i++)
    e += (double)v[i] * v[i];
  return std::sqrt(e / (double)(b - a));
}
static double MaxDiff(const std::vector<float>& a, const std::vector<float>& b)
{
  double m = 0.;
  for (size_t i = 0; i < a.size(); i++)
    m = std::max(m, (double)std::fabs(a[i] - b[i]));
  return m;
}

// A chain with only the given stages on, Warmth as given.
static FxChain Make(bool chorus, bool delay, bool reverb, double warmth = 0.)
{
  FxChain fx;
  fx.chorusOn = chorus;
  fx.delayOn = delay;
  fx.reverbOn = reverb;
  fx.warmth.SetAmount(warmth);
  fx.Prepare(FS);
  return fx;
}

int main()
{
  const auto noise = Noise(1., 0.3);
  {
    FxChain fx = Make(false, false, false, 1.);
    CHECK(MaxDiff(Run(fx, noise).l, noise) == 0., "every stage off: a straight bypass (no Warmth either)");
  }
  {
    FxChain fx = Make(true, false, false);
    fx.chorus.SetMix(0.);
    CHECK(MaxDiff(Run(fx, noise).l, noise) < 1e-6, "chorus at Mix 0: dry");
    FxChain wet = Make(true, false, false);
    CHECK(MaxDiff(Run(wet, noise).l, noise) > 0.05, "the chorus changes the sound");
  }
  {
    FxChain fx = Make(false, true, false);
    fx.delay.SetTime(0.25);
    fx.delay.SetFeedback(0.);
    fx.delay.SetMix(1.);
    fx.delay.SetWow(0.);
    fx.delay.SetFlutter(0.);
    fx.Prepare(FS);
    const auto o = Run(fx, Click(0.4));
    size_t peak = 0;
    for (size_t i = 0; i < o.l.size(); i++)
      if (std::fabs(o.l[i]) > std::fabs(o.l[peak])) peak = i;
    CHECK(peak == 12000, "the delay's echo lands at 250 ms (%zu)", peak);
  }
  {
    FxChain fx = Make(false, false, true);
    fx.SetReverbMix(1.);
    fx.reverb.SetDecay(4.);
    fx.Prepare(FS);
    auto in = Noise(0.5, 0.3);
    in.resize((size_t)(3. * FS), 0.f);
    const auto o = Run(fx, in);
    CHECK(Rms(o.l, 1.5, 2.) > 0.005, "the reverb's tail rings on after the sound stops (%.3f)", Rms(o.l, 1.5, 2.));
    FxChain dry = Make(false, false, true);
    dry.SetReverbMix(0.);
    dry.Prepare(FS);
    CHECK(MaxDiff(Run(dry, noise).l, noise) < 1e-6, "reverb at Mix 0: dry");
    double lo = 1e9, hi = 0.;
    for (int engine : {0, 1})
      for (double decay : {1., 6.})
        for (double size : {0.5, 2.})
        {
          FxChain f = Make(false, false, true);
          f.SetReverbMix(1.);
          f.reverb.SetEngine(engine);
          f.reverb.SetDecay(decay);
          f.reverb.SetSize(size);
          f.Prepare(FS);
          const double level = Rms(Run(f, Noise(3., 0.3)).l, 2., 3.) / 0.3 * 1.7320508;
          lo = std::min(lo, level);
          hi = std::max(hi, level);
        }
    CHECK(20. * std::log10(hi / lo) < 8., "plate or hall, any decay and size: the level stays close (within %.1f dB)", 20. * std::log10(hi / lo));
  }
  {
    FxChain cold = Make(true, true, true, 0.), warm = Make(true, true, true, 0.5);
    const auto a = Run(cold, noise), b = Run(warm, noise);
    CHECK(MaxDiff(a.l, b.l) > 0.01, "Warmth colours the chain's output");
  }
  {
    FxChain fx = Make(true, true, true, 1.);
    fx.chorus.SetFeedback(0.9);
    fx.delay.SetFeedback(1.1);
    fx.reverb.SetDecay(60.);
    fx.SetReverbMix(1.);
    fx.Prepare(FS);
    double peak = 0.;
    for (float x : Run(fx, Noise(10., 0.9)).l)
      peak = std::max(peak, (double)std::fabs(x));
    CHECK(peak < 4., "everything at its most extreme stays bounded (peak %.2f)", peak);
  }
  CHECK(gAllocs == 0, "no allocations while processing (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
