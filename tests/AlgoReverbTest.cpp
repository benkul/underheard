// Offline tests for libs/fx/AlgoReverb (underheard-reverb's Plate and Hall).
#include "AlgoReverb.h"


#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <cstdint>
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
static const char* kName[2] = {"plate", "hall"};

struct Response
{
  std::vector<double> l, r;
};

// The impulse response (a click on both inputs), counting allocations.
static Response Impulse(AlgoReverb& rv, double secs)
{
  Response o{std::vector<double>((size_t)(secs * FS)), std::vector<double>((size_t)(secs * FS))};
  gCountAllocs = true;
  for (size_t i = 0; i < o.l.size(); i++)
  {
    float a, b;
    rv.Process(i == 0 ? 1.f : 0.f, i == 0 ? 1.f : 0.f, a, b);
    o.l[i] = a;
    o.r[i] = b;
  }
  gCountAllocs = false;
  return o;
}
static double Energy(const std::vector<double>& v, size_t from = 0, size_t to = SIZE_MAX)
{
  double e = 0.;
  for (size_t i = from; i < std::min(to, v.size()); i++)
    e += v[i] * v[i];
  return e;
}
// Broadband decay time: T20 of the Schroeder integral, extrapolated to 60 dB.
static double Rt(const std::vector<double>& h)
{
  std::vector<double> e(h.size());
  double acc = 0.;
  for (size_t i = h.size(); i-- > 0;) { acc += h[i] * h[i]; e[i] = acc; }
  size_t t5 = 0, t25 = 0;
  for (size_t i = 0; i < e.size(); i++)
  {
    const double db = 10. * std::log10(e[i] / e[0]);
    if (!t5 && db < -5.) t5 = i;
    if (!t25 && db < -25.) { t25 = i; break; }
  }
  return 3. * (double)(t25 - t5) / FS;
}
static AlgoReverb Make(int engine, double decay, double size = 1., double damp = 7000., double balance = 1.)
{
  AlgoReverb rv;
  rv.SetEngine(engine);
  rv.SetDecay(decay);
  rv.SetSize(size);
  rv.SetDamping(damp);
  rv.SetBalance(balance);
  rv.Prepare(FS);
  return rv;
}

int main()
{
  for (int eng : {0, 1})
  {
    double lo = 1e9, hi = -1e9, worstRt = 0.;
    for (double decay : {0.5, 2., 8.})
      for (double size : {0.5, 1., 2.})
        for (double damp : {2500., 12000.})
        {
          AlgoReverb rv = Make(eng, decay, size, damp);
          const Response h = Impulse(rv, decay * 2.2 + 0.3);
          const double db = 10. * std::log10(Energy(h.l));
          lo = std::min(lo, db);
          hi = std::max(hi, db);
          worstRt = std::max(worstRt, std::fabs(Rt(h.l) / decay - 1.));
        }
    CHECK(lo > -3. && hi < 3., "%s: about unit energy at any decay, size and damping (%+.1f .. %+.1f dB)", kName[eng], lo, hi);
    CHECK(worstRt < 0.3, "%s: decays in about the time set (worst %.0f%% off)", kName[eng], worstRt * 100.);
  }
  for (int eng : {0, 1})
  {
    AlgoReverb rv = Make(eng, 2.5);
    const Response h = Impulse(rv, 3.);
    const size_t a = (size_t)(0.3 * FS), b = (size_t)(1.3 * FS);
    double lr = 0.;
    for (size_t i = a; i < b; i++)
      lr += h.l[i] * h.r[i];
    const double corr = lr / std::sqrt(Energy(h.l, a, b) * Energy(h.r, a, b));
    CHECK(std::fabs(corr) < 0.3, "%s: the sides' tails are decorrelated (%.2f)", kName[eng], corr);
  }
  for (int eng : {0, 1})
  {
    AlgoReverb early = Make(eng, 2.5, 1., 7000., -1.), late = Make(eng, 2.5, 1., 7000., 1.), both = Make(eng, 2.5, 1., 7000., 0.);
    const Response e = Impulse(early, 1.), l = Impulse(late, 3.), b = Impulse(both, 3.);
    CHECK(Energy(e.l, (size_t)(0.25 * FS)) < 1e-12 && std::fabs(10. * std::log10(Energy(e.l))) < 0.5,
          "%s: early only is a short burst of unit energy", kName[eng]);
    CHECK(std::fabs(10. * std::log10(Energy(b.l))) < 2., "%s: early and late together stay about unit energy (%+.1f dB)", kName[eng],
          10. * std::log10(Energy(b.l)));
    (void)l;
  }
  for (int eng : {0, 1})
  {
    // Freeze: holds steady, and nothing new gets in.
    auto frozen = [&](uint32_t seed) {
      AlgoReverb rv = Make(eng, 2.5, 1., 7000., 0.);
      rv.SetModulation(1.);
      Rand r;
      std::vector<double> out;
      for (int i = 0; i < (int)(1. * FS); i++)
      {
        float a, b;
        rv.Process(0.3f * r.Bipolar(), 0.3f * r.Bipolar(), a, b);
      }
      rv.SetFreeze(true);
      out.resize((size_t)(10. * FS));
      gCountAllocs = true;
      for (size_t i = 0; i < out.size(); i++)
      {
        if (i == (size_t)(0.3 * FS))
          r.s = seed; // different input once the gate has closed
        float a, b;
        rv.Process(0.3f * r.Bipolar(), 0.3f * r.Bipolar(), a, b);
        out[i] = a;
      }
      gCountAllocs = false;
      return out;
    };
    const auto x = frozen(1), y = frozen(2);
    std::vector<double> diff(x.size());
    for (size_t i = 0; i < x.size(); i++)
      diff[i] = x[i] - y[i];
    const double a = Energy(x, (size_t)(1. * FS), (size_t)(2. * FS)), b = Energy(x, (size_t)(9. * FS), (size_t)(10. * FS));
    CHECK(a > 1e-3 && std::fabs(10. * std::log10(b / a)) < 0.3, "%s: frozen, it holds (%+.2f dB over 8 s)", kName[eng], 10. * std::log10(b / a));
    CHECK(Energy(diff) < 1e-12 * Energy(x), "%s: frozen, new input doesn't get in", kName[eng]);
  }
  for (int eng : {0, 1})
  {
    // The longest decay, full modulation, loud input: bounded, and it dies away.
    AlgoReverb rv = Make(eng, 60., 2., 20000., 0.);
    rv.SetModulation(1.);
    Rand r;
    double peak = 0., first = 0., last = 0.;
    for (int i = 0; i < (int)(30. * FS); i++)
    {
      float a, b;
      rv.Process(i < (int)(2. * FS) ? r.Bipolar() : 0.f, i < (int)(2. * FS) ? r.Bipolar() : 0.f, a, b);
      if (!std::isfinite(a)) { peak = 1e9; break; }
      peak = std::max(peak, (double)std::fabs(a));
      if (i >= (int)(3. * FS) && i < (int)(5. * FS)) first += (double)a * a;
      if (i >= (int)(28. * FS)) last += (double)a * a;
    }
    CHECK(peak < 4. && last < first, "%s: 60 s decay at full modulation stays bounded (peak %.2f) and fades (%+.1f dB over 25 s)", kName[eng], peak,
          10. * std::log10(last / first));
  }
  CHECK(gAllocs == 0, "no allocations while processing (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
