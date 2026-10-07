// Offline tests for libs/voicefx: effect modules any Underheard instrument can add to its voices
// (the noise layer and the body resonator).
#include "NoiseLayer.h"
#include "Resonator.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace underheard::voicefx;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double FS = 48000., kPi = 3.14159265358979323846;
static double Db(double x) { return 20. * std::log10(std::max(x, 1e-15)); }

// The resonator's gain at `hz`: a steady sine through it, its level after settling.
static double GainAt(int body, double depth, double hz)
{
  Resonator r;
  r.Prepare(FS);
  r.Set(body, depth);
  double e = 0.;
  const int n = (int)FS;
  for (int i = 0; i < n; i++)
  {
    const double y = r.Process(std::sin(2. * kPi * hz * i / FS));
    if (i >= n / 2)
      e += y * y;
  }
  return std::sqrt(2. * e / (n / 2));
}
static double NoisePower(int body, double depth)
{
  Resonator r;
  r.Prepare(FS);
  r.Set(body, depth);
  uint32_t s = 7;
  double e = 0.;
  for (int i = 0; i < (int)(4. * FS); i++)
  {
    s = s * 1664525u + 1013904223u;
    const double y = r.Process((double)(int32_t)s / 2147483648.);
    e += y * y;
  }
  return e;
}

int main()
{
  // ---- Noise layer
  {
    auto render = [](double amount, double tone, double hz) {
      NoiseLayer n;
      n.Prepare(FS);
      n.Seed(99);
      n.SetTone(tone);
      std::vector<double> v((size_t)FS);
      for (auto& x : v)
        x = n.Next(hz, amount);
      return v;
    };
    auto rms = [](const std::vector<double>& v) { double e = 0.; for (double x : v) e += x * x; return std::sqrt(e / v.size()); };
    const auto a = render(1., 0.5, 220.), b = render(0.5, 0.5, 220.), z = render(0., 0.5, 220.);
    CHECK(rms(z) == 0. && std::fabs(rms(b) / rms(a) - 0.5) < 1e-9, "Amount: 0 is silent, half is half");
    // Bursts once per cycle: the start of each cycle much louder than its second half.
    const int period = (int)(FS / 220.);
    double head = 0., tail = 0.;
    for (size_t i = 0; i + (size_t)period < a.size(); i++)
    {
      const double ph = std::fmod((double)(i + 1) * 220. / FS, 1.); // the layer steps its phase before each sample
      if (ph < 0.1) head += a[i] * a[i];
      if (ph > 0.5) tail += a[i] * a[i];
    }
    CHECK(Db(std::sqrt(head / tail)) > 20., "noise bursts at the start of each cycle (%.0f dB over the rest)", Db(std::sqrt(head / tail)));
    // Tone: the share of energy above 8 kHz grows from dark to bright.
    auto above8k = [](const std::vector<double>& v) {
      double lp = 0., e = 0., all = 0.;
      const double k = 1. - std::exp(-2. * kPi * 8000. / FS);
      for (double x : v)
      {
        lp += (x - lp) * k;
        e += (x - lp) * (x - lp);
        all += x * x;
      }
      return e / all;
    };
    const double dark = above8k(render(1., 0., 220.)), mid = above8k(render(1., 0.5, 220.)), bright = above8k(render(1., 1., 220.));
    CHECK(mid > 1.4 * dark && bright > 1.4 * mid, "Tone: dark %.3f < middle %.3f < bright %.3f of the energy above 8 kHz", dark, mid, bright);
  }

  // ---- Resonator
  {
    CHECK(std::fabs(Db(GainAt(Resonator::kOff, 1., 200.) / GainAt(Resonator::kOff, 1., 3000.))) < 0.1 &&
              std::fabs(Db(GainAt(Resonator::kViolin, 0., 460.) / GainAt(Resonator::kViolin, 0., 3000.))) < 0.1,
          "Off, and any body at depth 0, is flat");
    const double violin = Db(GainAt(Resonator::kViolin, 1., 460.) / GainAt(Resonator::kViolin, 1., 380.));
    const double viola = Db(GainAt(Resonator::kViola, 1., 377.) / GainAt(Resonator::kViola, 1., 312.));
    CHECK(violin > 4. && viola > 4., "violin's main wood mode at 460 Hz (+%.0f dB); the viola's lower, at 377 Hz (+%.0f dB)", violin, viola);
    CHECK(GainAt(Resonator::kCello, 1., 193.) > GainAt(Resonator::kCello, 1., 460.) && GainAt(Resonator::kDoubleBass, 1., 115.) > GainAt(Resonator::kDoubleBass, 1., 193.),
          "the cello's and double bass's modes sit lower still");
    const double deeper = Db(GainAt(Resonator::kViolin, 2., 460.) / GainAt(Resonator::kViolin, 2., 380.));
    CHECK(deeper > violin + 3., "Depth 2 deepens the resonances (+%.0f dB)", deeper);
    const double ref = NoisePower(Resonator::kViolin, 1.);
    double worst = 0.;
    for (int body = 0; body < Resonator::kNumBodies; body++)
      for (double depth : {0., 0.5, 1., 2.})
        worst = std::max(worst, std::fabs(10. * std::log10(NoisePower(body, depth) / ref)));
    CHECK(worst < 0.5, "every body at every depth (and Off) at the same level on noise (within %.2f dB)", worst);

    // Changing body while it's sounding re-voices without clearing: no click.
    Resonator r;
    r.Prepare(FS);
    r.Set(Resonator::kViolin, 1.);
    double last = 0., jump = 0.;
    for (int i = 0; i < (int)FS; i++)
    {
      if (i == (int)(FS / 2))
        r.Set(Resonator::kCello, 1.5);
      const double y = r.Process(0.5 * std::sin(2. * kPi * 110. * i / FS));
      if (i > 1000)
        jump = std::max(jump, std::fabs(y - last));
      last = y;
    }
    CHECK(jump < 0.05, "switching violin to cello mid-note: no click (largest step %.3f)", jump);
    CHECK(std::string(Resonator::BodyName(Resonator::kDoubleBass)) == "Double bass", "bodies are named for a menu");
  }
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
