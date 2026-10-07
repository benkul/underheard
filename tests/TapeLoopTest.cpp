// Offline tests for libs/tape-core TapeLoop. No host or framework needed.
#include "TapeLoop.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <vector>

using namespace underheard;
using State = TapeLoop::State;

// ---- Allocation counter: the audio path must never allocate ------------------------------
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

// ---- Harness --------------------------------------------------------------------------
struct Rig
{
  double fs;
  TapeStorage tape;
  TapeLoop loop;
  double t = 0; // host samples processed
  std::function<float(double)> input = [](double) { return 0.f; }; // by time in seconds

  explicit Rig(double sampleRate, double maxSeconds = 20.)
  : fs(sampleRate), tape((int64_t)(maxSeconds * TapeLoop::kTapeRate)), loop(&tape)
  {
    tape.Reserve(tape.MaxFrames());
    loop.SetHostRate(fs);
    loop.SetMotorTime(0.);
    loop.SetSplice(0.);
  }

  // Runs for `secs` and returns the left output.
  std::vector<float> Run(double secs)
  {
    const int n = (int)std::lround(secs * fs);
    std::vector<float> out((size_t)n);
    gCountAllocs = true;
    for (int i = 0; i < n; i++)
    {
      const float x = input(t / fs);
      float l, r;
      loop.Process(x, x, l, r);
      if (!std::isfinite(l) || !std::isfinite(r))
      {
        fails++;
        printf("FAIL: non-finite output\n");
        gCountAllocs = false;
        return out;
      }
      out[(size_t)i] = l;
      t += 1.;
    }
    gCountAllocs = false;
    return out;
  }
};

static std::function<float(double)> Sine(double hz, float amp = 0.5f)
{
  return [hz, amp](double s) { return amp * (float)std::sin(2. * kPi * hz * s); };
}

static double Rms(const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
  to = std::min(to, v.size());
  double s = 0;
  for (size_t i = from; i < to; i++)
    s += (double)v[i] * v[i];
  return to > from ? std::sqrt(s / (double)(to - from)) : 0.;
}

// Amplitude of one frequency (Goertzel).
static double ToneAmp(const std::vector<float>& v, double hz, double fs)
{
  const double w = 2. * kPi * hz / fs, c = 2. * std::cos(w);
  double s1 = 0, s2 = 0;
  for (float x : v)
  {
    const double s0 = x + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
  return 2. * std::sqrt(re * re + im * im) / (double)v.size();
}

// Frequency from rising zero crossings.
static double Freq(const std::vector<float>& v, double fs)
{
  double first = -1, last = -1;
  int n = 0;
  for (size_t i = 1; i < v.size(); i++)
    if (v[i - 1] < 0.f && v[i] >= 0.f)
    {
      const double x = (double)(i - 1) + v[i - 1] / (v[i - 1] - v[i]);
      if (first < 0)
        first = x;
      last = x;
      n++;
    }
  return n > 1 ? (double)(n - 1) * fs / (last - first) : 0.;
}

static double MaxStep(const std::vector<float>& v, size_t from = 1)
{
  double m = 0;
  for (size_t i = std::max<size_t>(from, 1); i < v.size(); i++)
    m = std::max(m, (double)std::fabs(v[i] - v[i - 1]));
  return m;
}

// Records a first take of `secs` seconds at the current speed.
static void RecordTake(Rig& r, double secs)
{
  r.loop.SetRecord(true);
  r.Run(secs);
  r.loop.SetRecord(false);
}

// ---- Tests ----------------------------------------------------------------------------

static void TestPlainLoop()
{
  printf("\n-- 1x record and play\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 1.0);
  CHECK(r.loop.GetState() == State::Closing, "releasing record closes the take");
  CHECK(std::llabs(r.loop.LengthFrames() - 48000) <= 2, "loop is 1 s of tape (%lld frames)", (long long)r.loop.LengthFrames());
  r.input = Sine(0.);
  r.Run(0.05);
  CHECK(r.loop.GetState() == State::Playing, "post-roll done, loop playing");
  auto out = r.Run(2.0);
  CHECK(std::fabs(Freq(out, r.fs) - 440.) < 1., "plays back at 440 Hz (%.2f)", Freq(out, r.fs));
  CHECK(std::fabs(Rms(out) - 0.3536) < 0.01, "level preserved (rms %.4f)", Rms(out));
  CHECK(MaxStep(out) < 0.035, "seam is continuous (max step %.4f)", MaxStep(out));
}

static void TestSeamCrossfade()
{
  printf("\n-- seam crossfade on a loop that isn't a whole number of cycles\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 1.0011); // ends mid-cycle
  r.Run(0.05);           // post-roll still sees the sine continue
  r.input = Sine(0.);
  auto out = r.Run(2.0);
  CHECK(MaxStep(out) < 0.045, "no click at the seam (max step %.4f)", MaxStep(out));
}

static void TestVarispeedRecord()
{
  printf("\n-- record at 0.5x, play at 1x\n");
  Rig r(48000.);
  r.loop.SetSpeed(0.5);
  r.input = Sine(440.);
  RecordTake(r, 1.0);
  CHECK(std::llabs(r.loop.LengthFrames() - 24000) <= 2, "half-speed take uses half the tape (%lld frames)", (long long)r.loop.LengthFrames());
  r.Run(0.05);
  r.input = Sine(0.);
  r.loop.SetSpeed(1.0);
  r.Run(0.5); // speed glide
  auto out = r.Run(1.0);
  CHECK(std::fabs(Freq(out, r.fs) - 880.) < 2., "plays an octave up (%.2f Hz)", Freq(out, r.fs));
}

static void TestVarispeedPlay()
{
  printf("\n-- record at 1x, play at 2x and 0.5x\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.input = Sine(0.);
  r.loop.SetSpeed(2.0);
  r.Run(0.5);
  auto fast = r.Run(1.0);
  CHECK(std::fabs(Freq(fast, r.fs) - 880.) < 2., "2x plays at 880 Hz (%.2f)", Freq(fast, r.fs));
  r.loop.SetSpeed(0.5);
  r.Run(0.5);
  auto slow = r.Run(1.0);
  CHECK(std::fabs(Freq(slow, r.fs) - 220.) < 1., "0.5x plays at 220 Hz (%.2f)", Freq(slow, r.fs));
}

static void TestHostRate()
{
  printf("\n-- 44.1 kHz host\n");
  Rig r(44100.);
  r.input = Sine(440.);
  RecordTake(r, 1.0);
  CHECK(std::llabs(r.loop.LengthFrames() - 48000) <= 3, "1 s is still 48000 tape frames (%lld)", (long long)r.loop.LengthFrames());
  r.Run(0.05);
  r.input = Sine(0.);
  auto out = r.Run(2.0);
  CHECK(std::fabs(Freq(out, r.fs) - 440.) < 1., "pitch preserved (%.2f Hz)", Freq(out, r.fs));
}

static void TestReverse()
{
  printf("\n-- reverse\n");
  Rig r(48000.);
  // First half 300 Hz, second half 600 Hz.
  r.input = [](double s) { return 0.5f * (float)std::sin(2. * kPi * (s < 0.5 ? 300. : 600.) * s); };
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.input = Sine(0.);
  r.loop.SetReverse(true);
  r.Run(0.6); // glide through zero
  const double p0 = r.loop.PlayPosition();
  r.Run(0.01);
  const double p1 = r.loop.PlayPosition();
  CHECK(p1 < p0 || p0 < 1000., "play position runs backward (%.0f -> %.0f)", p0, p1);
  CHECK(r.loop.TapeSpeed() < -0.99, "tape speed is -1 (%.3f)", r.loop.TapeSpeed());

  // Collect output by position to check the content is intact.
  std::vector<float> low, high;
  for (int i = 0; i < 48000; i++)
  {
    const double pos = r.loop.PlayPosition();
    auto o = r.Run(1. / r.fs);
    if (pos > 4000 && pos < 20000)
      low.push_back(o[0]);
    else if (pos > 28000 && pos < 44000)
      high.push_back(o[0]);
  }
  std::vector<float> lowRev(low.rbegin(), low.rend()), highRev(high.rbegin(), high.rend());
  CHECK(std::fabs(Freq(lowRev, r.fs) - 300.) < 2., "first half still 300 Hz backward (%.1f)", Freq(lowRev, r.fs));
  CHECK(std::fabs(Freq(highRev, r.fs) - 600.) < 3., "second half still 600 Hz backward (%.1f)", Freq(highRev, r.fs));
}

static void TestOverdub(double erase)
{
  printf("\n-- overdub with Erase %.0f%%\n", erase * 100.);
  Rig r(48000.);
  r.input = Sine(300.);
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.loop.SetErase(erase);
  r.input = Sine(600.);
  // Erase 1 dubs past a full pass to show the old layer is gone everywhere. Erase 0 dubs 90%
  // of a pass so no part of the loop gets the new layer twice.
  r.loop.SetRecord(true);
  r.Run(erase >= 1. ? 1.2 : 0.9);
  r.loop.SetRecord(false);
  r.input = Sine(0.);
  r.Run(1.0); // let the dub's last pass go by
  auto out = r.Run(1.0);
  const double a300 = ToneAmp(out, 300., r.fs), a600 = ToneAmp(out, 600., r.fs);
  if (erase >= 1.)
    CHECK(a300 < 0.01 && std::fabs(a600 - 0.5) < 0.03, "new layer replaces the old (300: %.3f, 600: %.3f)", a300, a600);
  else
    CHECK(std::fabs(a300 - 0.5) < 0.03 && std::fabs(a600 - 0.45) < 0.03, "layers add (300: %.3f, 600: %.3f)", a300, a600);
}

static void TestFeedback()
{
  printf("\n-- feedback\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.input = Sine(0.);
  r.loop.SetFeedback(0.5);
  r.Run(0.5);
  auto p1 = r.Run(1.0);
  auto p2 = r.Run(1.0);
  const double ratio = Rms(p2) / Rms(p1);
  CHECK(std::fabs(ratio - 0.5) < 0.05, "each pass is half as loud (%.3f)", ratio);
}

static void TestMotor()
{
  printf("\n-- motor\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.input = Sine(0.);
  r.loop.SetMotorTime(0.3);
  r.loop.SetPlay(false);
  CHECK(r.loop.GetState() == State::Stopped, "stop");
  auto ramp = r.Run(0.15);
  CHECK(r.loop.TapeSpeed() > 0.05 && r.loop.TapeSpeed() < 0.95, "tape is slowing, not stopped (%.2f)", r.loop.TapeSpeed());
  r.Run(0.3);
  auto stopped = r.Run(0.5);
  CHECK(r.loop.TapeSpeed() == 0. && Rms(stopped) < 1e-3, "stopped and silent (rms %.5f)", Rms(stopped));
  r.loop.SetPlay(true);
  r.Run(0.6);
  auto again = r.Run(1.0);
  CHECK(std::fabs(Freq(again, r.fs) - 440.) < 1., "restarts at 440 Hz (%.2f)", Freq(again, r.fs));
}

static void TestTakeStartsAtSpeed()
{
  printf("\n-- takes start at speed, whatever the motor time\n");
  Rig r(48000.);
  r.loop.SetMotorTime(1.0);
  r.input = Sine(440.);
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.input = Sine(0.);
  // Listen to the start of the loop: with a spin-up baked in it would be low and swooping.
  std::vector<float> start;
  bool wrapped = false;
  double last = r.loop.PlayPosition();
  for (int i = 0; i < 2 * 48000; i++)
  {
    const double pos = r.loop.PlayPosition();
    wrapped = wrapped || pos < last;
    last = pos;
    const float o = r.Run(1. / r.fs)[0];
    if (wrapped && pos > 600 && pos < 9600)
      start.push_back(o);
    if (wrapped && pos >= 9600)
      break;
  }
  CHECK(std::fabs(Freq(start, r.fs) - 440.) < 1., "the first 200 ms of the loop are at pitch (%.2f Hz)", Freq(start, r.fs));
  r.loop.SetPlay(false);
  r.Run(0.5);
  CHECK(r.loop.TapeSpeed() > 0.1 && r.loop.TapeSpeed() < 0.95, "stopping still ramps down (%.2f)", r.loop.TapeSpeed());
}

static void TestSplice()
{
  printf("\n-- splice\n");
  Rig r(48000.);
  r.loop.SetSplice(1.);
  r.input = Sine(440.);
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.input = Sine(0.);
  // Measure level around the seam vs. away from it.
  double nearSeam = 0, away = 0;
  int nNear = 0, nAway = 0;
  for (int i = 0; i < 96000; i++)
  {
    const double pos = r.loop.PlayPosition();
    const float o = r.Run(1. / r.fs)[0];
    const double d = std::min(pos, (double)r.loop.LengthFrames() - pos);
    if (d < 60) { nearSeam += (double)o * o; nNear++; }
    else if (d > 4000) { away += (double)o * o; nAway++; }
  }
  nearSeam = std::sqrt(nearSeam / nNear);
  away = std::sqrt(away / nAway);
  CHECK(nearSeam < 0.5 * away, "level dips at the splice (%.3f vs %.3f)", nearSeam, away);
}

static void TestAntiAlias()
{
  printf("\n-- slow tape doesn't alias\n");
  Rig r(48000.);
  r.loop.SetSpeed(0.25);
  r.input = Sine(15000.);
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.input = Sine(0.);
  auto out = r.Run(1.0);
  CHECK(Rms(out) < 0.02, "a 15 kHz tone recorded at 0.25x is filtered, not folded down (rms %.4f)", Rms(out));
}

static void TestOutOfTape()
{
  printf("\n-- running out of tape\n");
  Rig r(48000., 1.0);
  r.input = Sine(440.);
  r.loop.SetRecord(true);
  r.Run(2.0);
  CHECK(r.loop.GetState() == State::Playing, "take closes itself at the end of the tape");
  CHECK(r.loop.LengthFrames() == 48000, "loop uses all of it (%lld)", (long long)r.loop.LengthFrames());

  // Only part of the storage reserved: the take must stop at the capacity, not overrun it.
  TapeStorage partial(10 * 48000);
  partial.Reserve(70000);
  TapeLoop loop(&partial);
  loop.SetMotorTime(0.);
  loop.SetRecord(true);
  float l, rr;
  for (int i = 0; i < 4 * 48000; i++) // longer than the 131072 frames reserved
    loop.Process(0.1f, 0.1f, l, rr);
  CHECK(loop.LengthFrames() == partial.CapacityFrames(), "stops at the reserved capacity (%lld of %lld)", (long long)loop.LengthFrames(), (long long)partial.CapacityFrames());
}

static void TestShortTakeAndClear()
{
  printf("\n-- short takes and clear\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 0.01);
  CHECK(r.loop.GetState() == State::Empty, "a 10 ms take is discarded");
  RecordTake(r, 1.0);
  r.Run(0.1);
  r.loop.Clear();
  CHECK(r.loop.GetState() == State::Clearing, "clear fades first");
  auto out = r.Run(0.02);
  CHECK(r.loop.GetState() == State::Empty, "then empties");
  CHECK(MaxStep(out) < 0.035, "without a click (max step %.4f)", MaxStep(out));
}

static void TestCloseTakeAt()
{
  printf("\n-- closing a take at a set length (tempo sync)\n");
  {
    Rig r(48000.);
    r.input = Sine(440.);
    r.loop.SetRecord(true);
    r.Run(1.0);
    r.loop.CloseTakeAt(36000); // shorter than recorded: closes now, extra is post-roll
    CHECK(r.loop.GetState() == State::Playing && r.loop.LengthFrames() == 36000, "shorter: closes at once (%lld frames)", (long long)r.loop.LengthFrames());
    CHECK(!r.loop.IsRecordingState(), "record button goes off");
    auto out = r.Run(1.0);
    CHECK(std::fabs(Freq(out, r.fs) - 440.) < 1. && MaxStep(out) < 0.035, "plays cleanly (%.2f Hz, max step %.4f)", Freq(out, r.fs), MaxStep(out));
  }
  {
    Rig r(48000.);
    r.input = Sine(440.);
    r.loop.SetRecord(true);
    r.Run(1.0);
    r.loop.CloseTakeAt(72000); // longer: keeps recording until then
    CHECK(r.loop.GetState() == State::Recording && !r.loop.IsRecordingState(), "longer: still taking, but the button shows off");
    r.Run(0.6);
    CHECK(r.loop.GetState() == State::Playing && r.loop.LengthFrames() == 72000, "closes at the set length (%lld frames)", (long long)r.loop.LengthFrames());
  }
}

static void TestLoad()
{
  printf("\n-- loading new tape\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 1.0);
  r.Run(0.5);
  r.input = Sine(0.);

  TapeStorage other(48000);
  other.Reserve(48000);
  for (int64_t i = 0; i < 48000; i++)
  {
    float* f = other.Frame(i);
    f[0] = f[1] = 0.5f * (float)std::sin(2. * kPi * 330. * (double)i / 48000.);
  }
  const uint64_t v0 = r.loop.Version();
  r.loop.RequestLoad(&other, 48000, true);
  CHECK(r.loop.GetState() == State::Loading && r.loop.Storage() == &r.tape, "fades out the old tape first");
  auto fade = r.Run(0.01);
  CHECK(r.loop.Storage() == &other && r.loop.GetState() == State::Playing, "then switches to the new tape");
  CHECK(r.loop.Version() != v0, "content version changes");
  auto out = r.Run(1.0);
  CHECK(std::fabs(Freq(out, r.fs) - 330.) < 1., "plays the new tape (%.2f Hz)", Freq(out, r.fs));
  CHECK(MaxStep(fade) < 0.035 && MaxStep(out) < 0.035, "no clicks across the switch");

  TapeStorage third(48000);
  third.Reserve(48000);
  Rig e(48000.);
  e.loop.RequestLoad(&third, 48000, false);
  CHECK(e.loop.Storage() == &third && e.loop.GetState() == State::Stopped, "an empty loop switches at once, stopped when asked");
}

// Amplitude of `hz` over the next `secs`.
static double AmpOver(Rig& r, double hz, double secs)
{
  return ToneAmp(r.Run(secs), hz, r.fs);
}

static void TestWear()
{
  printf("\n-- wear\n");
  Rig r(48000.);
  r.input = [](double t) { return 0.3f * (float)std::sin(2. * kPi * 400. * t) + 0.15f * (float)std::sin(2. * kPi * 6000. * t); };
  RecordTake(r, 0.25); // 1/4 s loop: 4 passes a second
  r.Run(0.05);
  r.input = Sine(0.);
  const double lo0 = AmpOver(r, 400., 1.0), hi0 = AmpOver(r, 6000., 1.0);
  r.loop.SetWear(true);
  r.loop.SetWearRate(1.);
  r.Run(10.);
  const double lo1 = AmpOver(r, 400., 1.0), hi1 = AmpOver(r, 6000., 1.0);
  CHECK(r.loop.Passes() >= 40 && r.loop.MeanAge() > 1., "the tape ages with each pass (%d passes, mean age %.2f)", r.loop.Passes(), r.loop.MeanAge());
  CHECK(hi1 / hi0 < 0.25 && lo1 / lo0 > 0.3, "highs wear away first (6 kHz x%.3f, 400 Hz x%.3f)", hi1 / hi0, lo1 / lo0);
  r.loop.SetWear(false); // measure the restored tape without it aging again
  r.loop.Restore();
  const double lo2 = AmpOver(r, 400., 1.0), hi2 = AmpOver(r, 6000., 1.0);
  CHECK(std::fabs(lo2 / lo0 - 1.) < 0.05 && std::fabs(hi2 / hi0 - 1.) < 0.1 && r.loop.Passes() <= 5,
        "Restore brings it back (400 Hz x%.3f, 6 kHz x%.3f)", lo2 / lo0, hi2 / hi0);
}

static void TestWearLimit()
{
  printf("\n-- wear limit and recovery\n");
  {
    Rig r(48000.);
    r.input = Sine(440.);
    RecordTake(r, 0.25);
    r.Run(0.05);
    r.loop.SetWear(true);
    r.loop.SetWearRate(1.);
    r.loop.SetWearLimit(1.0);
    r.Run(30.);
    CHECK(r.loop.GetWearPhase() == TapeLoop::WearPhase::Holding && r.loop.MeanAge() < 1.15, "Recover off: wear stops at the limit (mean age %.2f)",
          r.loop.MeanAge());
  }
  {
    Rig r(48000.);
    r.input = Sine(440.);
    RecordTake(r, 0.25);
    r.Run(0.05);
    r.loop.SetWear(true);
    r.loop.SetWearRate(1.);
    r.loop.SetWearLimit(1.0);
    r.loop.SetRecover(true);
    // Phases: wear up to the limit, heal to new, wear again.
    double peak = 0., trough = 1e9, peakAgain = 0., shedAtTrough = 1.;
    int stage = 0; // 0 wearing, 1 recovering, 2 wearing again
    for (int s = 0; s < 240; s++)
    {
      r.Run(0.25);
      const double m = r.loop.MeanAge();
      const auto phase = r.loop.GetWearPhase();
      if (stage == 0 && phase == TapeLoop::WearPhase::Recovering) stage = 1;
      if (stage == 1 && phase == TapeLoop::WearPhase::Wearing) stage = 2;
      if (stage == 0) peak = std::max(peak, m);
      if (stage >= 1 && m < trough)
      {
        trough = m;
        double sh = 0.;
        for (int k = 0; k < 50; k++) sh = std::max(sh, (double)r.loop.ShedData()[k]);
        shedAtTrough = sh;
      }
      if (stage == 2) peakAgain = std::max(peakAgain, m);
    }
    const bool recovered = stage == 2;
    CHECK(peak > 0.95 && peak < 1.2, "it wears up to the limit (peak mean age %.2f)", peak);
    CHECK(recovered && trough < 0.005 && shedAtTrough < 0.005, "then heals back to new, dropouts closed to inaudible (age %.3f, shed %.2g)", trough, shedAtTrough);
    CHECK(peakAgain > 0.5, "and wears again (%.2f)", peakAgain);
  }
}

static void TestWearOff()
{
  printf("\n-- wear switched off\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 0.25);
  r.Run(0.05);
  r.input = Sine(0.);
  r.loop.SetWearRate(1.);
  const double a0 = Rms(r.Run(1.0));
  r.Run(10.);
  const double a1 = Rms(r.Run(1.0));
  CHECK(std::fabs(a1 / a0 - 1.) < 0.01 && r.loop.MeanAge() == 0., "nothing changes (x%.4f)", a1 / a0);
}

static void TestDropouts()
{
  printf("\n-- oxide shedding\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 0.5);
  r.Run(0.05);
  r.input = Sine(0.);
  r.loop.SetWear(true);
  r.loop.SetWearRate(1.);
  r.Run(30.);
  auto pass = r.Run(0.5);
  double lo = 1e9, hi = 0;
  for (size_t i = 0; i + 480 <= pass.size(); i += 480)
  {
    const double v = Rms(pass, i, i + 480);
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  CHECK(lo < 0.5 * hi, "some stretches drop out (quietest 10 ms %.3f vs loudest %.3f)", lo, hi);
}

static void TestPrintThrough()
{
  printf("\n-- print-through\n");
  // A 20 ms burst in the middle of half a second of silence.
  auto burst = [](double t) { return (t > 0.25 && t < 0.27) ? 0.5f * (float)std::sin(2. * kPi * 1000. * t) : 0.f; };
  auto ghostLevel = [&](bool wear) {
    Rig r(48000.);
    r.input = burst;
    RecordTake(r, 0.5);
    r.Run(0.05);
    r.input = Sine(0.);
    if (wear)
    {
      r.loop.SetWear(true);
      r.loop.SetWearRate(0.8);
      r.Run(20.);
      r.loop.SetWear(false); // freeze the damage so the pass measured is like the last
    }
    // Collect one pass by tape position: the ghost before the burst plays 30 ms ahead of it.
    double e = 0;
    int n = 0;
    for (int i = 0; i < 24000; i++)
    {
      const double pos = r.loop.PlayPosition() / 48000.;
      const float o = r.Run(1. / r.fs)[0];
      if (pos > 0.225 && pos < 0.24)
      {
        e += (double)o * o;
        n++;
      }
    }
    return std::sqrt(e / std::max(n, 1));
  };
  const double clean = ghostLevel(false), worn = ghostLevel(true);
  CHECK(clean < 1e-3 && worn > 0.003, "a worn loop has a ghost 30 ms before the sound (%.4f, clean %.5f)", worn, clean);
}

static void TestCrackle()
{
  printf("\n-- crackle\n");
  Rig r(48000.);
  RecordTake(r, 0.5); // silence: anything heard is crackle
  r.Run(0.05);
  r.loop.SetWear(true);
  r.loop.SetWearRate(1.);
  r.Run(40.);
  r.loop.SetWear(false);
  auto out = r.Run(4.0);
  double e = 0., d = 0.;
  for (size_t i = 1; i < out.size(); i++)
  {
    e += (double)out[i] * out[i];
    d += (double)(out[i] - out[i - 1]) * (out[i] - out[i - 1]);
  }
  CHECK(e > 1e-6, "worn tape crackles (energy %.2g)", e);
  CHECK(d / e < 0.3, "the crackle is soft, not spiky (difference/energy %.3f; white noise is 2)", d / e);
  r.loop.SetMotorTime(0.2);
  r.loop.SetPlay(false);
  r.Run(0.5);
  auto stopped = r.Run(1.0);
  double peak = 0.;
  for (float x : stopped)
    peak = std::max(peak, (double)std::fabs(x));
  CHECK(peak < 1e-5, "a stopped loop is silent, crackle included (peak %.2g)", peak);
}

static void TestHiss()
{
  printf("\n-- hiss\n");
  Rig r(48000.);
  RecordTake(r, 0.5); // silence
  r.Run(0.05);
  const double none = Rms(r.Run(0.5));
  r.loop.SetHiss(1.);
  const double fresh = Rms(r.Run(0.5));
  r.loop.SetWear(true);
  r.loop.SetWearRate(1.);
  r.Run(15.);
  r.loop.SetWear(false);
  const double old = Rms(r.Run(0.5));
  CHECK(none < 1e-6 && fresh > 0.001 && old > 1.5 * fresh, "hiss is off at 0, there at 1, and grows with age (%.5f -> %.5f)", fresh, old);
}

static void TestWowFlutter()
{
  printf("\n-- wow and flutter\n");
  auto spread = [](double wow, double flutter) {
    Rig r(48000.);
    r.input = Sine(1000.);
    RecordTake(r, 1.0);
    r.Run(0.05);
    r.input = Sine(0.);
    r.loop.SetWow(wow);
    r.loop.SetFlutter(flutter);
    r.Run(0.5);
    std::vector<double> f;
    for (int w = 0; w < 80; w++)
      f.push_back(Freq(r.Run(0.05), r.fs));
    double mean = 0, var = 0;
    for (double x : f) mean += x;
    mean /= f.size();
    for (double x : f) var += (x - mean) * (x - mean);
    return std::sqrt(var / f.size());
  };
  const double steady = spread(0., 0.), wow = spread(1., 0.), flutter = spread(0., 1.);
  CHECK(steady < 0.5 && wow > 2. && flutter > 0.5, "pitch wanders with wow and flutter (steady %.2f Hz, wow %.2f Hz, flutter %.2f Hz)", steady, wow, flutter);
}

static void TestFastPlaybackAlias()
{
  printf("\n-- fast playback doesn't alias\n");
  Rig r(48000.);
  r.input = Sine(15000.);
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.input = Sine(0.);
  r.loop.SetSpeed(2.);
  r.Run(0.5);
  auto out = r.Run(1.0);
  CHECK(Rms(out) < 0.01, "15 kHz played at 2x is removed, not folded down to 18 kHz (rms %.4f)", Rms(out));
}

static void TestOverdubRenews()
{
  printf("\n-- an overdub with Erase makes tape new again\n");
  Rig r(48000.);
  r.input = Sine(440.);
  RecordTake(r, 0.25);
  r.Run(0.05);
  r.loop.SetWear(true);
  r.loop.SetWearRate(1.);
  r.Run(8.);
  const double before = r.loop.MeanAge();
  r.loop.SetErase(1.);
  r.loop.SetRecord(true);
  r.Run(0.3);
  r.loop.SetRecord(false);
  CHECK(r.loop.MeanAge() < 0.2 * before, "mean age %.2f -> %.2f", before, r.loop.MeanAge());
}

// Records two halves (300 Hz then 600 Hz) and returns once the loop is playing.
static void TwoToneLoop(Rig& r)
{
  r.input = [](double t) { return 0.5f * (float)std::sin(2. * kPi * (t < 0.5 ? 300. : 600.) * t); };
  RecordTake(r, 1.0);
  r.Run(0.05);
  r.input = Sine(0.);
}

// Cuts the loop on the main thread's behalf: renders into `to` and requests the switch.
static void Cut(Rig& r, TapeStorage& to, const TapeEdit& e, std::vector<float>& age, std::vector<float>& shed)
{
  to.Reserve(e.NewLength());
  RenderEdit(e, r.tape, to, TapeLoop::kJoinFrames);
  const size_t n = (size_t)((e.NewLength() + TapeLoop::kSegmentFrames - 1) / TapeLoop::kSegmentFrames);
  age.assign(r.loop.SegmentCapacity(), 0.f);
  shed.assign(r.loop.SegmentCapacity(), 0.f);
  for (size_t s = 0; s < n; s++)
  {
    const int64_t c = std::min<int64_t>((int64_t)s * TapeLoop::kSegmentFrames + TapeLoop::kSegmentFrames / 2, e.NewLength() - 1);
    const size_t src = (size_t)(e.SourceFrame(c) / TapeLoop::kSegmentFrames);
    age[s] = r.loop.AgeData()[src];
    shed[s] = r.loop.ShedData()[src];
  }
  r.loop.RequestEdit(&to, e, age.data(), shed.data(), nullptr, 0);
}

static void TestRazor()
{
  printf("\n-- razor cuts while playing\n");
  std::vector<float> age, shed;
  {
    Rig r(48000.);
    TwoToneLoop(r);
    TapeStorage to(r.tape.MaxFrames());
    Cut(r, to, MakeRemove(r.loop.LengthFrames(), 24000, 24000, TapeLoop::kMinLoopFrames), age, shed);
    CHECK(r.loop.GetState() == State::Editing, "fades out before switching");
    auto fade = r.Run(0.01);
    CHECK(r.loop.Storage() == &to && r.loop.LengthFrames() == 24000 && r.loop.GetState() == State::Playing, "Remove: half the loop is gone, still playing");
    auto out = r.Run(1.0);
    CHECK(ToneAmp(out, 300., r.fs) > 0.4 && ToneAmp(out, 600., r.fs) < 0.01, "only the 300 Hz half is left (300: %.3f, 600: %.3f)",
          ToneAmp(out, 300., r.fs), ToneAmp(out, 600., r.fs));
    CHECK(MaxStep(fade) < 0.06 && MaxStep(out) < 0.06, "no clicks (max step %.4f / %.4f)", MaxStep(fade), MaxStep(out));
  }
  {
    Rig r(48000.);
    TwoToneLoop(r);
    TapeStorage to(r.tape.MaxFrames());
    Cut(r, to, MakeIsolate(r.loop.LengthFrames(), 24000, 24000, TapeLoop::kMinLoopFrames), age, shed);
    r.Run(0.01);
    auto out = r.Run(1.0);
    CHECK(r.loop.LengthFrames() == 24000 && ToneAmp(out, 600., r.fs) > 0.4 && ToneAmp(out, 300., r.fs) < 0.01,
          "Isolate: only the 600 Hz half plays (600: %.3f)", ToneAmp(out, 600., r.fs));
  }
  {
    Rig r(48000.);
    r.input = Sine(440.);
    RecordTake(r, 1.0);
    r.Run(0.05);
    r.input = Sine(0.);
    r.Run(0.3); // head is in the middle of what gets flipped
    const double before = r.loop.PlayPosition();
    TapeStorage to(r.tape.MaxFrames());
    Cut(r, to, MakeReverse(r.loop.LengthFrames(), 12000, 24000), age, shed);
    r.Run(0.004);
    const double after = r.loop.PlayPosition();
    CHECK(std::fabs(after - (35999. - before)) < 400., "Reverse: the head lands on the mirrored spot (%.0f -> %.0f)", before, after);
    auto out = r.Run(1.0);
    CHECK(r.loop.LengthFrames() == 48000 && std::fabs(Freq(out, r.fs) - 440.) < 1. && MaxStep(out) < 0.06,
          "same length, same pitch, no clicks (%.1f Hz, max step %.4f)", Freq(out, r.fs), MaxStep(out));
  }
  {
    Rig r(48000.);
    r.input = Sine(440.);
    RecordTake(r, 1.0);
    r.Run(0.05);
    r.input = Sine(0.);
    r.loop.SetWear(true);
    r.loop.SetWearRate(1.);
    r.Run(6.);
    r.loop.SetWear(false);
    double firstHalf = 0.;
    for (int s = 0; s < 100; s++)
      firstHalf += r.loop.AgeData()[s];
    firstHalf /= 100.;
    TapeStorage to(r.tape.MaxFrames());
    Cut(r, to, MakeIsolate(r.loop.LengthFrames(), 0, 24000, TapeLoop::kMinLoopFrames), age, shed);
    r.Run(0.01);
    CHECK(std::fabs(r.loop.MeanAge() - firstHalf) < 0.01, "wear moves with the audio (isolated half aged %.3f, was %.3f)", r.loop.MeanAge(), firstHalf);
  }
  {
    Rig r(48000.);
    r.input = Sine(440.);
    RecordTake(r, 1.0);
    r.Run(0.05);
    r.input = Sine(0.);
    TapeStorage to(r.tape.MaxFrames());
    const int64_t splice = 12000;
    to.Reserve(48000);
    const TapeEdit e = MakeReverse(48000, 30000, 6000);
    RenderEdit(e, r.tape, to, TapeLoop::kJoinFrames);
    age.assign(r.loop.SegmentCapacity(), 0.f);
    shed.assign(r.loop.SegmentCapacity(), 0.f);
    r.loop.RequestEdit(&to, e, age.data(), shed.data(), &splice, 1);
    r.Run(0.01);
    r.loop.SetSplice(1.);
    double nearCut = 0, away = 0;
    int nn = 0, na = 0;
    for (int i = 0; i < 48000; i++)
    {
      const double pos = r.loop.PlayPosition();
      const float o = r.Run(1. / r.fs)[0];
      if (std::fabs(pos - splice) < 60) { nearCut += (double)o * o; nn++; }
      else if (std::fabs(pos - splice) > 3000 && std::fabs(pos - splice) < 9000) { away += (double)o * o; na++; }
    }
    CHECK(r.loop.NumSplices() == 1 && std::sqrt(nearCut / nn) < 0.5 * std::sqrt(away / na), "a cut's splice dips like the seam (%.3f vs %.3f)",
          std::sqrt(nearCut / nn), std::sqrt(away / na));
  }
}

int main()
{
  TestPlainLoop();
  TestSeamCrossfade();
  TestVarispeedRecord();
  TestVarispeedPlay();
  TestHostRate();
  TestReverse();
  TestOverdub(1.0);
  TestOverdub(0.0);
  TestFeedback();
  TestMotor();
  TestTakeStartsAtSpeed();
  TestSplice();
  TestAntiAlias();
  TestOutOfTape();
  TestShortTakeAndClear();
  TestCloseTakeAt();
  TestLoad();
  TestWear();
  TestWearLimit();
  TestWearOff();
  TestDropouts();
  TestPrintThrough();
  TestCrackle();
  TestHiss();
  TestWowFlutter();
  TestFastPlaybackAlias();
  TestOverdubRenews();
  TestRazor();

  CHECK(gAllocs == 0, "no allocations while processing (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
