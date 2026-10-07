// Offline tests for libs/room RoomTone.
#include "RoomTone.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

using namespace underheard::room;

static std::atomic<bool> gCountAllocs{false};
static std::atomic<int> gAllocs{0};
void* operator new(size_t n)
{
  if (gCountAllocs) gAllocs++;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void* operator new[](size_t n) { return operator new(n); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

// Runs a preset for `secs` and returns per-second RMS of the left channel, plus the L/R correlation.
static std::vector<double> Run(RoomTone& t, double secs, double* corr = nullptr)
{
  std::vector<float> l(480), r(480);
  std::vector<double> rms;
  double s = 0., cl = 0., cr = 0., clr = 0.;
  gCountAllocs = true;
  for (int b = 0; b < (int)(secs * 100.); b++)
  {
    std::fill(l.begin(), l.end(), 0.f);
    std::fill(r.begin(), r.end(), 0.f);
    t.Process(l.data(), r.data(), 480, 1.f);
    for (int i = 0; i < 480; i++) { s += (double)l[(size_t)i] * l[(size_t)i]; cl += (double)l[(size_t)i] * l[(size_t)i]; cr += (double)r[(size_t)i] * r[(size_t)i]; clr += (double)l[(size_t)i] * r[(size_t)i]; }
    if (b % 100 == 99) { gCountAllocs = false; rms.push_back(std::sqrt(s / 48000.)); gCountAllocs = true; s = 0.; }
  }
  gCountAllocs = false;
  if (corr) *corr = clr / std::sqrt(cl * cr + 1e-30);
  return rms;
}

int main()
{
  const char* names[] = {"none", "apartment", "kitchen", "office", "hallway", "basement"};
  printf("-- every preset, about -10 dBFS RMS\n");
  for (int p = RoomTone::kNone; p <= RoomTone::kBasement; p++)
  {
    RoomTone t;
    t.Prepare(48000.);
    t.SetPreset(p);
    double corr = 0.;
    const auto rms = Run(t, 20., &corr);
    double mean = 0.;
    for (double v : rms) mean += v * v;
    mean = std::sqrt(mean / rms.size());
    const double db = 20. * std::log10(mean + 1e-12);
    if (p == RoomTone::kNone)
      CHECK(mean == 0., "none is silent");
    else
      CHECK(db > -16. && db < -5. && corr < 0.97, "%s: %.1f dBFS, sides correlation %.2f", names[p], db, corr);
  }

  printf("\n-- the kitchen fridge cycles\n");
  {
    RoomTone t;
    t.Prepare(48000.);
    t.SetPreset(RoomTone::kKitchen);
    double onLevel = 0., offLevel = 0.;
    int onN = 0, offN = 0, switches = 0;
    bool last = t.CompressorRunning();
    for (int sec = 0; sec < 300; sec++)
    {
      const auto rms = Run(t, 1.);
      if (t.CompressorRunning() != last) { switches++; last = t.CompressorRunning(); continue; }
      (last ? onLevel : offLevel) += rms[0] * rms[0];
      (last ? onN : offN)++;
    }
    const double diff = 10. * std::log10((onLevel / std::max(onN, 1)) / (offLevel / std::max(offN, 1)));
    CHECK(switches >= 3, "it switches on and off over 5 minutes (%d times)", switches);
    CHECK(diff > 1., "and is audible when it runs (%.1f dB louder)", diff);
  }

  printf("\n-- a loaded recording loops seamlessly\n");
  {
    std::vector<float> st(2 * 48000);
    for (int i = 0; i < 48000; i++) st[(size_t)(2 * i)] = st[(size_t)(2 * i + 1)] = 0.3f * (float)std::sin(2. * 3.14159265 * 100.3 * i / 48000.);
    const ToneLoop loop = MakeToneLoop(st, 0.25, 48000.);
    double jump = std::fabs(loop.l.front() - loop.l.back());
    CHECK(loop.l.size() == 36000 && jump < 0.02, "the end flows into the start (step %.4f across the loop point)", jump);
    RoomTone t;
    t.Prepare(48000.);
    t.SetPreset(RoomTone::kLoaded);
    t.SetLoop(&loop);
    const auto rms = Run(t, 3.);
    CHECK(rms[2] > 0.15, "and plays (%.3f rms)", rms[2]);
  }

  CHECK(gAllocs == 0, "no allocations while playing (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
