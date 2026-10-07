// Offline tests for libs/fx/Warmth: the suite's shared analog colour.
#include "Warmth.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace underheard;
using namespace underheard::fx;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double FS = 48000.;

static std::vector<double> Sine(double hz, double level, double secs = 1.)
{
  std::vector<double> v((size_t)(secs * FS));
  for (size_t i = 0; i < v.size(); i++)
    v[i] = level * std::sin(2. * kPi * hz * (double)i / FS);
  return v;
}
// Amplitude of the `hz` component over the second half (Goertzel, Hann window).
static double Level(const std::vector<double>& v, double hz)
{
  const size_t a = v.size() / 2, n = v.size() - a;
  const double w = 2. * kPi * hz / FS, k = 2. * std::cos(w);
  double s1 = 0., s2 = 0., wsum = 0.;
  for (size_t i = 0; i < n; i++)
  {
    const double win = 0.5 - 0.5 * std::cos(2. * kPi * (double)i / (double)(n - 1));
    wsum += win;
    const double s = v[a + i] * win + k * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return std::sqrt(s1 * s1 + s2 * s2 - k * s1 * s2) * 2. / wsum;
}
static double Db(double x) { return 20. * std::log10(x); }
static std::vector<double> Warm(const std::vector<double>& in, double amount)
{
  Warmth w;
  w.SetAmount(amount);
  w.Prepare(FS);
  std::vector<double> out(in.size());
  for (size_t i = 0; i < in.size(); i++)
    out[i] = w.Process(in[i], 0);
  return out;
}

int main()
{
  {
    const auto in = Sine(1000., 0.5);
    const auto out = Warm(in, 0.);
    CHECK(out == in, "Warmth 0 is an exact bypass");
  }
  for (double a : {0.5, 1.})
  {
    const double mid = Db(Level(Warm(Sine(1000., 0.01), a), 1000.) / 0.01);
    const double low = Db(Level(Warm(Sine(110., 0.01), a), 110.) / 0.01);
    const double high = Db(Level(Warm(Sine(10000., 0.01), a), 10000.) / 0.01);
    CHECK(std::fabs(mid) < 1., "Warmth %.0f%%: quiet mids about unchanged (%+.1f dB at 1 kHz)", a * 100., mid);
    CHECK(low > 4. * a && low < 6. * a, "Warmth %.0f%%: a low bump (%+.1f dB at 110 Hz)", a * 100., low);
    CHECK(high < -16. * std::min(a, 0.5) && high < -12. * a && high > -18., "Warmth %.0f%%: a softer top (%+.1f dB at 10 kHz)", a * 100., high);
  }
  {
    // Harmonics on a fairly loud low note: present, even ones included, but gentle at the
    // default and fuller at full.
    for (double a : {0.5, 1.})
    {
      const auto o = Warm(Sine(200., 0.5), a);
      const double f1 = Level(o, 200.), a2 = Level(o, 400.), a3 = Level(o, 600.), t = std::sqrt(a2 * a2 + a3 * a3) / f1;
      CHECK(Db(a2 / f1) > -50. && t < (a < 1. ? 0.035 : 0.08), "a -6 dBFS note at Warmth %.0f%%: 2nd harmonic %.0f dB, 3rd %.0f dB, THD %.1f%%", a * 100.,
            Db(a2 / f1), Db(a3 / f1), t * 100.);
    }
    const auto out = Warm(Sine(200., 0.5), 1.);
    const double f = Level(out, 200.), h2 = Level(out, 400.);
    const auto quiet = Warm(Sine(200., 0.05), 1.);
    CHECK(Level(quiet, 400.) / Level(quiet, 200.) < h2 / f / 5., "quiet notes stay much cleaner");
    double mean = 0.;
    for (size_t i = out.size() / 2; i < out.size(); i++)
      mean += out[i];
    mean /= (double)(out.size() / 2);
    CHECK(std::fabs(mean) < 1e-3, "no DC from the asymmetry (%.1g)", mean);
  }
  {
    // Aliasing: a loud 15 kHz tone driven hard. A plain tanh folds its 3rd harmonic (45 kHz)
    // down to 3 kHz; the anti-aliased one much less.
    const auto in = Sine(15000., 0.9);
    SoftSaturator s;
    s.Set(4., 0.);
    std::vector<double> plain(in.size()), adaa(in.size());
    for (size_t i = 0; i < in.size(); i++)
    {
      plain[i] = s.f(in[i]) / s.slope;
      adaa[i] = s.Run(in[i]);
    }
    const double pa = Level(plain, 3000.), aa = Level(adaa, 3000.);
    CHECK(Db(aa / pa) < -6., "anti-aliased saturation: the 3 kHz alias %.0f dB lower than plain tanh", Db(aa / pa));
    SoftSaturator q;
    q.Set(2.5, 0.1);
    double worst = 0.;
    for (double x : {0.001, -0.001, 0.0005})
    {
      q.Reset();
      double y = 0.;
      for (int i = 0; i < 4; i++)
        y = q.Run(x);
      worst = std::max(worst, std::fabs(y / x - 1.));
    }
    CHECK(worst < 0.01, "the saturator is unity gain for small signals (%.2g)", worst);
  }
  {
    Warmth w;
    w.SetAmount(1.);
    w.Prepare(FS);
    double peak = 0.;
    for (int i = 0; i < 48000; i++)
      peak = std::max(peak, std::fabs(w.Process(i % 2 ? 4. : -4., 0)));
    CHECK(std::isfinite(peak) && peak < 4., "very hot input stays bounded (%.2f)", peak);
  }
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
