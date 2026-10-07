// Offline tests for libs/fx/DubDelay (underheard-delay).
#include "DubDelay.h"
#include "NoteValues.h"

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

struct Stereo
{
  std::vector<float> l, r;
};

// Runs the delay on `in` (fed to both sides), counting allocations.
static Stereo Run(DubDelay& d, const std::vector<float>& in)
{
  Stereo s{std::vector<float>(in.size()), std::vector<float>(in.size())};
  gCountAllocs = true;
  for (size_t i = 0; i < in.size(); i++)
  {
    d.Process(in[i], in[i], s.l[i], s.r[i]);
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

static std::vector<float> Silence(double secs) { return std::vector<float>((size_t)(secs * FS), 0.f); }
static std::vector<float> Impulse(double secs)
{
  auto v = Silence(secs);
  v[0] = 1.f;
  return v;
}
static std::vector<float> Noise(double secs, double level, uint32_t seed = 7)
{
  Rand r;
  r.s = seed;
  auto v = Silence(secs);
  for (auto& x : v)
    x = (float)(level * r.Bipolar());
  return v;
}

static size_t PeakIndex(const std::vector<float>& v, size_t from, size_t to)
{
  size_t best = from;
  for (size_t i = from; i < to && i < v.size(); i++)
    if (std::fabs(v[i]) > std::fabs(v[best]))
      best = i;
  return best;
}
static double Rms(const std::vector<float>& v, size_t from, size_t to)
{
  double e = 0.;
  for (size_t i = from; i < to; i++)
    e += (double)v[i] * v[i];
  return std::sqrt(e / (double)(to - from));
}
static double MaxAbs(const std::vector<float>& v)
{
  double m = 0.;
  for (float x : v)
    m = std::max(m, (double)std::fabs(x));
  return m;
}

// A clean, wet-only delay: no wobble, no age, open filters, no feedback.
static void Clean(DubDelay& d, double time)
{
  d.SetTime(time);
  d.SetFeedback(0.);
  d.SetWow(0.);
  d.SetFlutter(0.);
  d.SetAge(0.);
  d.SetDrive(0.);
  d.SetLowCut(20.);
  d.SetHighCut(20000.);
  d.SetResonance(0.);
  d.SetMix(1.);
  d.Prepare(FS);
}

int main()
{
  {
    DubDelay d;
    Clean(d, 0.25);
    auto s = Run(d, Impulse(0.5));
    const size_t p = PeakIndex(s.l, 1, s.l.size());
    CHECK(p == 12000 && std::fabs(s.l[p] - 1.f) < 0.01f, "single echo at 250 ms (peak %zu, %.3f)", p, s.l[p]);
    CHECK(MaxAbs(std::vector<float>(s.l.begin() + 12001, s.l.end())) < 0.01, "no feedback: one echo only");
  }
  {
    CHECK(std::fabs(NoteSeconds(9, 120.) - 0.5) < 1e-9, "1/4 at 120 BPM is 500 ms");
    CHECK(std::fabs(NoteSeconds(8, 120.) - 0.375) < 1e-9, "1/8 dotted at 120 BPM is 375 ms");
    CHECK(std::fabs(NoteSeconds(4, 100.) - 0.2) < 1e-9, "1/8 triplet at 100 BPM is 200 ms");
    CHECK(std::string(NoteValues()[kNumBarOrLess - 1].name) == "1/1" && NoteValues().back().beats == 16., "the delay's longest value is a whole note; 4 bars for modulation");
  }
  {
    DubDelay d;
    d.SetMode(DubDelay::kPingPong);
    d.SetSpread(1.);
    Clean(d, 0.2);
    d.SetFeedback(0.8);
    auto s = Run(d, Impulse(0.7));
    const size_t t = 9600;
    CHECK(std::fabs(s.l[t]) > 0.4 && std::fabs(s.r[t]) < 0.01, "ping-pong: first repeat left (L %.3f, R %.3f)", s.l[t], s.r[t]);
    const size_t p2 = PeakIndex(s.r, t + 100, 3 * t - 100);
    CHECK(p2 > 2 * t - 50 && p2 < 2 * t + 50 && std::fabs(s.l[p2]) < 0.01 && std::fabs(s.r[p2]) > 0.2,
          "ping-pong: second repeat right (at %zu, R %.3f)", p2, s.r[p2]);
    const size_t p3 = PeakIndex(s.l, 2 * t + 100, 4 * t - 100);
    CHECK(p3 > 3 * t - 50 && p3 < 3 * t + 50, "ping-pong: third repeat back left (at %zu)", p3);
  }
  {
    DubDelay d;
    d.SetMode(DubDelay::kMultiHead);
    d.SetHeads(6); // 1+2+3
    Clean(d, 0.3);
    auto s = Run(d, Impulse(0.4));
    const double g = 1. / std::sqrt(3.);
    bool all = true;
    for (size_t at : {4800u, 9600u, 14400u})
      all = all && std::fabs(s.l[at] - g) < 0.01;
    CHECK(all, "three heads at 1/3, 2/3 and 1x (%.3f %.3f %.3f)", s.l[4800], s.l[9600], s.l[14400]);
    DubDelay d2;
    d2.SetMode(DubDelay::kMultiHead);
    d2.SetHeads(4); // 1+3
    Clean(d2, 0.3);
    auto s2 = Run(d2, Impulse(0.4));
    CHECK(std::fabs(s2.l[4800]) > 0.6 && std::fabs(s2.l[9600]) < 0.01 && std::fabs(s2.l[14400]) > 0.6, "heads 1+3 skip head 2");
  }
  {
    DubDelay d;
    Clean(d, 0.15);
    d.SetFeedback(1.1);
    d.SetDrive(0.);
    auto in = Noise(0.5, 0.8);
    in.resize((size_t)(12 * FS), 0.f);
    auto s = Run(d, in);
    const double late = Rms(s.l, (size_t)(10 * FS), (size_t)(12 * FS));
    CHECK(MaxAbs(s.l) < 3. && late > 0.05, "110%% feedback runs away but stays bounded (max %.2f, late rms %.3f)", MaxAbs(s.l), late);
  }
  {
    DubDelay d;
    Clean(d, 0.25);
    d.SetFeedback(0.7);
    d.SetWow(1.); // frozen loops don't wobble
    d.SetAge(1.);
    Run(d, Noise(1., 0.3));
    d.SetFreeze(true);
    auto s = Run(d, Noise(4., 0.5, 99)); // new input must not get in
    const double a = Rms(s.l, (size_t)(0.5 * FS), (size_t)(1.5 * FS)), b = Rms(s.l, (size_t)(2.5 * FS), (size_t)(3.5 * FS));
    CHECK(a > 0.02 && std::fabs(20. * std::log10(b / a)) < 0.5, "freeze holds the loop steady (%.4f -> %.4f)", a, b);
    d.SetFreeze(false);
    d.SetFeedback(0.);
    auto s2 = Run(d, Silence(1.));
    CHECK(Rms(s2.l, (size_t)(0.5 * FS), s2.l.size()) < 1e-3, "unfreezing with no feedback empties the loop");
  }
  {
    DubDelay d;
    Clean(d, 0.1); // prepared with the send open, as a host would before the first block ...
    d.SetSendOpen(false); // ... then the project's settings arrive
    auto s = Run(d, Noise(0.5, 0.5));
    CHECK(MaxAbs(s.l) < 1e-6, "throw closed: nothing goes in, even in the first block");
    d.SetSendOpen(true);
    auto s2 = Run(d, Noise(0.5, 0.5));
    CHECK(Rms(s2.l, (size_t)(0.2 * FS), s2.l.size()) > 0.1, "throw open: the input echoes");
  }
  {
    auto secondEcho = [](bool inverted) {
      DubDelay d;
      d.SetPolarity(inverted);
      Clean(d, 0.1);
      d.SetFeedback(0.5);
      auto s = Run(d, Impulse(0.25));
      return s.l[PeakIndex(s.l, 9500, 9700)];
    };
    const float n = secondEcho(false), i = secondEcho(true);
    CHECK(n > 0.2f && i < -0.2f, "polarity flips the repeats (%.3f vs %.3f)", n, i);
  }
  {
    DubDelay d;
    d.SetGlide(0.2);
    Clean(d, 0.375);
    d.SetTime(0.1);
    Run(d, Silence(0.1));
    CHECK(std::fabs(d.CurrentTime() - 0.1) < 1e-9, "the first block starts at its time, without gliding from the last");
    d.SetTime(0.5);
    Run(d, Silence(0.1));
    const double mid = d.CurrentTime();
    Run(d, Silence(1.5));
    CHECK(mid > 0.15 && mid < 0.45 && std::fabs(d.CurrentTime() - 0.5) < 0.001, "time glides (%.3f after 100 ms, then %.4f)", mid, d.CurrentTime());
  }
  {
    auto wetWhilePlaying = [](double duck) {
      DubDelay d;
      d.SetDuck(duck);
      Clean(d, 0.1);
      d.SetFeedback(0.5);
      auto s = Run(d, Noise(1., 0.4));
      return Rms(s.l, (size_t)(0.5 * FS), s.l.size());
    };
    const double off = wetWhilePlaying(0.), on = wetWhilePlaying(1.);
    CHECK(on < off * 0.3, "duck dips the repeats while playing (%.3f vs %.3f)", on, off);
  }
  {
    auto sideDifference = [](double phase) {
      DubDelay d;
      Clean(d, 0.3);
      d.SetWow(1.);
      d.SetFlutter(1.);
      d.SetPhase(phase);
      std::vector<float> in((size_t)(3 * FS));
      for (size_t i = 0; i < in.size(); i++)
        in[i] = (float)(0.5 * std::sin(2. * kPi * 440. * (double)i / FS));
      auto s = Run(d, in);
      double e = 0.;
      for (size_t i = (size_t)FS; i < s.l.size(); i++)
        e += std::pow(s.l[i] - s.r[i], 2.);
      return e;
    };
    const double same = sideDifference(0.), opposed = sideDifference(180.);
    CHECK(same < 1e-6 && opposed > 1., "phase offsets the right side's wobble (%.2g vs %.2g)", same, opposed);
  }
  {
    DubDelay d;
    d.SetSpread(1.);
    Clean(d, 0.2);
    auto s = Run(d, Impulse(0.3));
    CHECK(PeakIndex(s.r, 1, s.r.size()) == 9600 + 960, "spread: right side 20 ms later");
  }
  {
    // Below 100% the loop must die away whatever the filters and heads do. A small click keeps
    // the saturation out of it (that only bounds a runaway; it shouldn't be what stops one).
    struct Case { const char* name; int mode, heads; double fb, res, cut; };
    const Case cases[] = {
      {"single, full resonance at 1 kHz", DubDelay::kSingle, 0, 0.95, 1., 1000.},
      {"single, full resonance at 300 Hz", DubDelay::kSingle, 0, 0.95, 1., 300.},
      {"single, half resonance at 3 kHz", DubDelay::kSingle, 0, 0.95, 0.5, 3000.},
      {"ping-pong, full resonance", DubDelay::kPingPong, 0, 0.95, 1., 1500.},
      {"heads 2+3, no resonance", DubDelay::kMultiHead, 5, 0.9, 0., 20000.},
      {"all heads, no resonance", DubDelay::kMultiHead, 6, 0.9, 0., 20000.},
      {"Dub echo template", DubDelay::kMultiHead, 5, 0.88, 0.45, 1800.},
    };
    for (const Case& c : cases)
    {
      DubDelay d;
      d.SetMode(c.mode);
      d.SetHeads(c.heads);
      d.SetResonance(c.res);
      Clean(d, 0.2);
      d.SetHighCut(c.cut);
      d.SetResonance(c.res);
      d.SetLowCut(c.name[0] == 'D' ? 200. : 20.);
      d.SetFeedback(c.fb);
      auto in = Impulse(8.);
      in[0] = 0.01f;
      auto s = Run(d, in);
      const double early = Rms(s.l, (size_t)(0.5 * FS), (size_t)(1.5 * FS)), late = Rms(s.l, (size_t)(6.5 * FS), (size_t)(7.5 * FS));
      CHECK(late < early, "%s at %.0f%% feedback dies away (%.1f dB over 6 s)", c.name, c.fb * 100., 20. * std::log10(late / early));
    }
  }
  {
    // Resonance: a peak at the high cut on the echoes.
    auto echoAt = [](double hz, double res) {
      DubDelay d;
      Clean(d, 0.1);
      d.SetHighCut(1000.);
      d.SetResonance(res);
      std::vector<float> in((size_t)FS);
      for (size_t i = 0; i < in.size(); i++)
        in[i] = (float)(0.1 * std::sin(2. * kPi * hz * (double)i / FS));
      auto s = Run(d, in);
      return Rms(s.l, (size_t)(0.5 * FS), s.l.size());
    };
    const double flat = 20. * std::log10(echoAt(1000., 0.) / echoAt(250., 0.)), peaked = 20. * std::log10(echoAt(1000., 1.) / echoAt(250., 1.));
    CHECK(std::fabs(flat) < 0.5 && peaked > 10., "resonance peaks the echoes at the high cut (%+.1f dB vs %+.1f dB without)", peaked, flat);
  }
  {
    // Warmth on the echoes: a softer top, mids about as they were.
    auto echo = [](double hz, double warmth) {
      DubDelay d;
      d.SetWarmth(warmth);
      Clean(d, 0.1);
      std::vector<float> in((size_t)FS);
      for (size_t i = 0; i < in.size(); i++)
        in[i] = (float)(0.05 * std::sin(2. * kPi * hz * (double)i / FS));
      auto s = Run(d, in);
      return Rms(s.l, (size_t)(0.5 * FS), s.l.size());
    };
    const double top = 20. * std::log10(echo(10000., 0.5) / echo(10000., 0.)), mid = 20. * std::log10(echo(1000., 0.5) / echo(1000., 0.));
    CHECK(top < -3. && std::fabs(mid) < 1., "Warmth 50%%: echoes' top %+.1f dB, mids %+.1f dB", top, mid);
  }
  CHECK(gAllocs == 0, "no allocations while processing (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
