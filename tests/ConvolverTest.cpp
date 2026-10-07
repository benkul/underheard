// Offline tests for libs/room Convolver and ConvolverSwitch.
#include "Convolver.h"
#include "ImpulsePrep.h"

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

static float Noise(uint32_t& s) { s = s * 1664525u + 1013904223u; return (float)((double)(int32_t)s / 2147483648.); }

int main()
{
  printf("-- exact convolution, no latency\n");
  {
    // A sparse IR plus a long decaying tail, so several partition sizes are used.
    std::vector<float> ir(48000, 0.f);
    ir[0] = 0.5f;
    ir[37] = -0.25f;
    uint32_t s = 7;
    for (size_t i = 100; i < ir.size(); i++) ir[i] = 0.1f * Noise(s) * std::exp(-(float)i / 8000.f);
    auto c = Convolver::Make({ir}, 48000.);
    const int N = 48000 * 2, B = 256;
    std::vector<float> in(N), out(N);
    for (float& x : in) x = Noise(s);
    for (int p = 0; p < N; p += B)
    {
      float* o[1] = {out.data() + p};
      c->Process(in.data() + p, o, B);
    }
    double err = 0.;
    for (int n = 0; n < N; n += 97)
    {
      double y = 0.;
      for (int k = 0; k < (int)ir.size() && k <= n; k++) y += (double)ir[(size_t)k] * in[(size_t)(n - k)];
      err = std::max(err, std::fabs(y - out[(size_t)n]));
    }
    CHECK(err < 1e-3, "matches direct convolution, sample-aligned (max error %.2g)", err);
  }

  printf("\n-- stereo, odd block sizes, real-time safety\n");
  {
    std::vector<float> l(24000, 0.f), r(24000, 0.f);
    l[0] = 1.f;
    r[480] = 1.f; // right channel: 10 ms later
    auto c = Convolver::Make({l, r}, 48000.);
    ConvolverSwitch sw;
    sw.Offer(std::move(c));
    std::vector<float> outL(20000), outR(20000), in(20000, 0.f);
    uint32_t s = 3;
    for (float& x : in) x = Noise(s);
    int p = 0, block = 1;
    int allocsAfterWarmup = 0;
    for (int i = 0; p < 20000; i++)
    {
      const int n = std::min(20000 - p, block);
      if (i == 40) { gAllocs = 0; gCountAllocs = true; }
      sw.Process(in.data() + p, outL.data() + p, outR.data() + p, n);
      p += n;
      block = block % 700 + 37; // irregular host blocks
    }
    gCountAllocs = false;
    allocsAfterWarmup = gAllocs;
    double err = 0.;
    for (int n = 4096; n < 20000; n++)
      err = std::max(err, (double)std::fabs(outL[n] - in[n]) + std::fabs(outR[n] - in[n - 480]));
    CHECK(err < 1e-4, "left is the input, right is it 10 ms later (max error %.2g)", err);
    CHECK(allocsAfterWarmup == 0, "no allocations once running (%d)", allocsAfterWarmup);
  }

  printf("\n-- switching impulse responses\n");
  {
    std::vector<float> a(4800, 0.f), b(4800, 0.f);
    a[0] = 1.f;
    b[0] = -1.f; // the opposite polarity: an abrupt switch would jump by twice the signal
    ConvolverSwitch sw;
    sw.Offer(Convolver::Make({a}, 48000.));
    const int B = 128;
    std::vector<float> in(B), L(B), R(B);
    double phase = 0., maxStep = 0., last = 0.;
    bool offered = false;
    std::vector<std::unique_ptr<Convolver>> freed;
    for (int blk = 0; blk < 400; blk++)
    {
      for (int i = 0; i < B; i++) { in[(size_t)i] = 0.5f * (float)std::sin(phase); phase += 2. * 3.14159265 * 220. / 48000.; }
      if (blk == 100 && !offered) { sw.Offer(Convolver::Make({b}, 48000.)); offered = true; }
      sw.Process(in.data(), L.data(), R.data(), B);
      for (int i = 0; i < B; i++)
      {
        if (blk > 20) maxStep = std::max(maxStep, std::fabs(L[(size_t)i] - last));
        last = L[(size_t)i];
      }
      if (auto old = sw.TakeRetired()) freed.push_back(std::move(old));
    }
    CHECK(maxStep < 0.05, "crossfades to the new IR without a click (max step %.4f)", maxStep);
    CHECK(freed.size() == 1, "the old convolver comes back to be freed");
    CHECK(L[(size_t)(B - 1)] * in[(size_t)(B - 1)] <= 0.f, "and the new IR is the one playing");
  }

  printf("\n-- preparing a room recording\n");
  {
    // Half a second of near-silence, a clap with a 0.4 s decaying tail, then silence. Mono.
    std::vector<float> st(2 * 96000, 0.f);
    uint32_t s = 11;
    for (int i = 0; i < 24000; i++) st[(size_t)(2 * i)] = st[(size_t)(2 * i + 1)] = 1e-5f * Noise(s);
    for (int i = 0; i < 19200; i++)
    {
      const float x = 0.8f * Noise(s) * std::exp(-(float)i / 2400.f);
      st[(size_t)(2 * (24000 + i))] = st[(size_t)(2 * (24000 + i) + 1)] = x;
    }
    const underheard::room::Impulse imp = underheard::room::PrepareImpulse(st, 48000.);
    double e = 0.;
    for (float x : imp.channels[0]) e += (double)x * x;
    CHECK(imp.channels.size() == 1, "identical sides make a mono impulse");
    float peak = 0.f;
    for (float x : imp.channels[0]) peak = std::max(peak, std::fabs(x));
    // The onset (within 20 dB of the peak) is 1 ms (48 samples) in.
    CHECK(imp.Frames() < 24000 && std::fabs(imp.channels[0][48]) >= 0.1f * peak && std::fabs(imp.channels[0][20]) < 0.1f * peak,
          "starts 1 ms before the clap, not in the silence before it (%lld frames)", (long long)imp.Frames());
    CHECK(std::fabs(e - 1.) < 1e-3, "scaled to unit energy (%.4f)", e);
    CHECK(std::fabs(imp.channels[0].back()) < 1e-4f, "the tail is faded to nothing");
  }

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
