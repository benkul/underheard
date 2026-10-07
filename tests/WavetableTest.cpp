// Offline tests for libs/wavetable: decoding the wavetable formats (code from horsi-vst3),
// the oscillator (smooth Position, band-limited playback, table swaps) and the factory tables.
#include "FactoryTables.h"
#include "WaveOscillator.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace underheard::wavetable;

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
static const double kPi = 3.14159265358979323846;

// ---- WAV files in memory ----
static void Put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 255); b.push_back(v >> 8); }
static void Put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 255); }
static void Tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A WAV of `samples` (interleaved by channel), as 16-bit PCM, 24-bit PCM or 32-bit float, with an
// optional Serum "clm " chunk.
static std::vector<uint8_t> Wav(const std::vector<double>& samples, int channels, int bits, bool isFloat, int clm = 0)
{
  std::vector<uint8_t> data;
  for (double x : samples)
  {
    if (isFloat) { float f = (float)x; uint32_t u; std::memcpy(&u, &f, 4); Put32(data, u); }
    else if (bits == 16) Put16(data, (uint16_t)(int16_t)std::lround(std::clamp(x, -1., 1.) * 32767.));
    else { const int32_t v = (int32_t)std::lround(std::clamp(x, -1., 1.) * 8388607.); data.push_back(v & 255); data.push_back((v >> 8) & 255); data.push_back((v >> 16) & 255); }
  }
  std::vector<uint8_t> b;
  Tag(b, "RIFF");
  Put32(b, 0);
  Tag(b, "WAVE");
  Tag(b, "fmt ");
  Put32(b, 16);
  Put16(b, isFloat ? 3 : 1);
  Put16(b, (uint16_t)channels);
  Put32(b, 48000);
  Put32(b, 48000 * channels * bits / 8);
  Put16(b, (uint16_t)(channels * bits / 8));
  Put16(b, (uint16_t)bits);
  if (clm)
  {
    const std::string s = "<!>" + std::to_string(clm) + " 10000000 wwwww";
    Tag(b, "clm ");
    Put32(b, (uint32_t)s.size());
    b.insert(b.end(), s.begin(), s.end());
    if (s.size() & 1) b.push_back(0);
  }
  Tag(b, "data");
  Put32(b, (uint32_t)data.size());
  b.insert(b.end(), data.begin(), data.end());
  const uint32_t riff = (uint32_t)b.size() - 8;
  std::memcpy(b.data() + 4, &riff, 4);
  return b;
}

// Frames of `cycle` samples; frame f is harmonic (f + 1) at amplitude 0.8.
static std::vector<double> HarmonicFrames(int frames, int cycle)
{
  std::vector<double> v;
  for (int f = 0; f < frames; f++)
    for (int n = 0; n < cycle; n++)
      v.push_back(0.8 * std::sin(2. * kPi * (f + 1) * n / cycle));
  return v;
}

// ---- Measuring ----
// Amplitude of `hz` in v (Goertzel with a Hann window).
static double Level(const std::vector<float>& v, double hz)
{
  const size_t n = v.size();
  const double w = 2. * kPi * hz / FS, k = 2. * std::cos(w);
  double s1 = 0., s2 = 0., wsum = 0.;
  for (size_t i = 0; i < n; i++)
  {
    const double win = 0.5 - 0.5 * std::cos(2. * kPi * (double)i / (double)(n - 1));
    wsum += win;
    const double s = v[i] * win + k * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return std::sqrt(std::max(0., s1 * s1 + s2 * s2 - k * s1 * s2)) * 2. / wsum;
}
static double Db(double x) { return 20. * std::log10(std::max(x, 1e-15)); }
// A frame's harmonic k amplitude (DFT bin of one 2048-sample frame).
static double FrameHarmonic(const WavetableData& t, int frame, int k)
{
  const float* f = t.RawFrame(frame);
  double re = 0., im = 0.;
  for (int n = 0; n < WavetableData::kFrameSize; n++)
  {
    re += f[n] * std::cos(2. * kPi * k * n / WavetableData::kFrameSize);
    im -= f[n] * std::sin(2. * kPi * k * n / WavetableData::kFrameSize);
  }
  return 2. * std::sqrt(re * re + im * im) / WavetableData::kFrameSize;
}
static double FrameRms(const WavetableData& t, int frame)
{
  const float* f = t.RawFrame(frame);
  double e = 0.;
  for (int n = 0; n < WavetableData::kFrameSize; n++)
    e += (double)f[n] * f[n];
  return std::sqrt(e / WavetableData::kFrameSize);
}

static std::vector<float> Play(WaveOscillator& o, double secs)
{
  std::vector<float> out((size_t)(secs * FS));
  gCountAllocs = true;
  for (auto& x : out)
    x = o.Process();
  gCountAllocs = false;
  return out;
}

int main()
{
  // ---- Decoding
  {
    auto bytes = Wav(HarmonicFrames(4, 2048), 1, 16, false, 2048);
    auto d = DecodeWavetable(bytes.data(), bytes.size(), "serum");
    CHECK(d.table && d.table->NumFrames() == 4 && std::fabs(FrameHarmonic(*d.table, 2, 3) - 0.8) < 0.01, "Serum (clm chunk, 16-bit): 4 frames, frame 3 is the 3rd harmonic");
    bytes = Wav(HarmonicFrames(64, 256), 1, 32, true);
    d = DecodeWavetable(bytes.data(), bytes.size(), "waveedit");
    CHECK(d.table && d.table->NumFrames() == 64 && d.sourceCycle == 256 && std::fabs(FrameHarmonic(*d.table, 9, 10) - 0.8) < 0.01,
          "WaveEdit 64 x 256 (float): resampled to 2048, frame 10 still the 10th harmonic");
    bytes = Wav(HarmonicFrames(3, 2048), 1, 24, false);
    d = DecodeWavetable(bytes.data(), bytes.size(), "multiple");
    CHECK(d.table && d.table->NumFrames() == 3, "3 x 2048 with no metadata (24-bit): 3 frames");
    std::vector<double> stereo;
    for (int n = 0; n < 600; n++)
    {
      const double s = 0.6 * std::sin(2. * kPi * n / 600.);
      stereo.push_back(s);
      stereo.push_back(s);
    }
    bytes = Wav(stereo, 2, 24, false);
    d = DecodeWavetable(bytes.data(), bytes.size(), "single");
    CHECK(d.table && d.table->NumFrames() == 1 && d.sourceCycle == 600 && std::fabs(FrameHarmonic(*d.table, 0, 1) - 0.6) < 0.01,
          "a single 600-sample cycle (stereo, 24-bit): one frame, mixed down");
    bytes = Wav(HarmonicFrames(300, 2048), 1, 16, false, 2048);
    d = DecodeWavetable(bytes.data(), bytes.size(), "long");
    CHECK(d.table && d.table->NumFrames() == 256 && d.sourceFrames == 300, "300 frames are thinned to 256");
    const char junk[] = "this is not a wav file at all";
    d = DecodeWavetable(junk, sizeof junk, "junk");
    CHECK(!d.table && !d.error.empty(), "not a WAV: an error, not a crash (\"%s\")", d.error.c_str());
  }

  // ---- The oscillator
  {
    auto saw = MakeFactoryTable(kSaw), sine = MakeFactoryTable(kSine);
    WaveOscillator o;
    o.Prepare(FS);
    o.SetTable(sine.get());
    o.SetFrequency(440.);
    auto out = Play(o, 1.);
    CHECK(Db(Level(out, 440.) / (0.5 * std::sqrt(2.))) > -0.2 && Db(Level(out, 880.) / Level(out, 440.)) < -80., "a sine table plays a clean 440 Hz");

    // Band-limited: a saw at 5 kHz has harmonics at 5, 10, 15, 20 kHz; anything at 25 kHz and
    // up would fold back (25 -> 23, 30 -> 18, 35 -> 13 kHz). Those must be absent.
    o.SetTable(saw.get());
    o.SetFrequency(5000.);
    Play(o, 0.05);
    out = Play(o, 1.);
    const double fund = Level(out, 5000.);
    double alias = 0.;
    for (double hz : {23000., 18000., 13000., 8000., 3000.})
      alias = std::max(alias, Level(out, hz));
    CHECK(Db(Level(out, 20000.) / fund) > -25. && Db(alias / fund) < -60., "a bright saw at 5 kHz: harmonics up to 20 kHz, aliases %.0f dB down", Db(alias / fund));

    // Position morphs between frames: a two-frame table (1st harmonic, then 2nd).
    auto bytes = Wav(HarmonicFrames(2, 2048), 1, 32, true, 2048);
    auto two = DecodeWavetable(bytes.data(), bytes.size(), "two").table;
    WaveOscillator m;
    m.Prepare(FS);
    m.SetTable(two.get());
    m.SetFrequency(200.);
    auto at = [&](double pos) {
      m.SetPosition(pos, true);
      Play(m, 0.05);
      const auto v = Play(m, 0.5);
      return std::make_pair(Level(v, 200.), Level(v, 400.));
    };
    const auto p0 = at(0.), p1 = at(1.), ph = at(0.5);
    CHECK(Db(p0.second / p0.first) < -60. && Db(p1.first / p1.second) < -60. && std::fabs(Db(ph.first / ph.second)) < 0.2,
          "Position 0 is the first frame, 1 the last, 0.5 an even blend");

    // A table swap crossfades over 10 ms: at the swap it's still the old table, 10 ms later
    // it's the new one (checked against a sine-only and a saw-only oscillator in step with it).
    WaveOscillator s, onlySine, onlySaw;
    for (WaveOscillator* x : {&s, &onlySine, &onlySaw})
    {
      x->Prepare(FS);
      x->SetFrequency(110.);
    }
    s.SetTable(sine.get());
    onlySine.SetTable(sine.get());
    onlySaw.SetTable(saw.get());
    Play(s, 0.1);
    Play(onlySine, 0.1);
    Play(onlySaw, 0.1);
    s.SetTable(saw.get());
    const auto b = Play(s, 0.02), bs = Play(onlySine, 0.02), bw = Play(onlySaw, 0.02);
    double atSwap = std::fabs(b[0] - bs[0]), after = 0., during = 0.;
    for (size_t i = WaveOscillator::kFadeLength; i < b.size(); i++)
      after = std::max(after, (double)std::fabs(b[i] - bw[i]));
    for (size_t i = 0; i < (size_t)WaveOscillator::kFadeLength; i++)
      during = std::max(during, (double)std::fabs(b[i] - bw[i]));
    CHECK(atSwap < 1e-6 && after < 1e-6 && during > 0.05, "swapping tables crossfades over 10 ms (no click)");
    s.ReleaseTable(saw.get());
    CHECK(s.Table() == nullptr && Play(s, 0.02).back() == 0.f, "a released table is forgotten (silence, no dangling pointer)");
  }

  // ---- Factory tables
  {
    double lo = 1e9, hi = 0.;
    bool built = true;
    const auto t0 = std::chrono::steady_clock::now();
    std::unique_ptr<WavetableData> t[kNumGeneratedTables];
    for (int i = 0; i < kNumGeneratedTables; i++)
    {
      t[i] = MakeFactoryTable(i);
      built = built && t[i] && t[i]->Name() == FactoryTableName(i);
      for (int f = 0; t[i] && f < t[i]->NumFrames(); f++)
      {
        lo = std::min(lo, FrameRms(*t[i], f));
        hi = std::max(hi, FrameRms(*t[i], f));
      }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(built && Db(hi / lo) < 0.01, "all %d factory tables build, every frame at the same loudness, in %.2f s", kNumGeneratedTables, secs);
    CHECK(std::fabs(Db(FrameHarmonic(*t[kTriangle], 0, 3) / FrameHarmonic(*t[kTriangle], 0, 1)) + 19.08) < 0.05 &&
              Db(FrameHarmonic(*t[kTriangle], 0, 2) / FrameHarmonic(*t[kTriangle], 0, 1)) < -100.,
          "Triangle: odd harmonics at 1/k^2");
    CHECK(std::fabs(Db(FrameHarmonic(*t[kSaw], 0, 2) / FrameHarmonic(*t[kSaw], 0, 1)) + 6.02) < 0.05, "Saw: 1/k");
    CHECK(std::fabs(Db(FrameHarmonic(*t[kSquare], 0, 3) / FrameHarmonic(*t[kSquare], 0, 1)) + 9.54) < 0.05 &&
              Db(FrameHarmonic(*t[kSquare], 0, 2) / FrameHarmonic(*t[kSquare], 0, 1)) < -100.,
          "Square: odd harmonics at 1/k");
    const auto& bow = *t[kBowedString];
    const double light = Db(FrameHarmonic(bow, 0, 10) / FrameHarmonic(bow, 0, 1)), heavy = Db(FrameHarmonic(bow, 63, 10) / FrameHarmonic(bow, 63, 1));
    CHECK(bow.NumFrames() == 64 && light < -40. && std::fabs(heavy + 20.) < 1., "Bowed string: light bowing dark (10th %.0f dB), heavy bowing a full 1/k (%.0f dB)",
          light, heavy);
    const auto& vow = *t[kVowels];
    // ah (frame 0) is strong at the 5th harmonic (980 Hz, between its formants at 730 and 1090)
    // and weak at the 12th; ee (frame 32) the other way round (its formants at 270 and 2290).
    CHECK(vow.NumFrames() == 64 && FrameHarmonic(vow, 0, 5) > 2. * FrameHarmonic(vow, 0, 12) && FrameHarmonic(vow, 32, 12) > 2. * FrameHarmonic(vow, 32, 5),
          "Vowels: ah strong in the low mids, ee strong up at its high second formant");
    const auto& org = *t[kOrgan];
    CHECK(org.NumFrames() == 64 && FrameHarmonic(org, 0, 2) < 1e-6 && FrameHarmonic(org, 63, 8) > 0.1 && FrameHarmonic(org, 63, 7) < 1e-6,
          "Organ: a lone 8' flute, to every drawbar (the 8th harmonic, never the 7th)");
  }
  // ---- Embedded tables (the plugin registers horsi-vst3's horse tables; here read from assets)
  {
    const char* files[] = {"Horsi-Whinny.wav", "Horsi-Breath.wav"};
    static std::vector<std::vector<unsigned char>> bytes;
    std::vector<EmbeddedTable> tables;
    for (const char* f : files)
    {
      FILE* fp = std::fopen((std::string(UNDERHEARD_ROOT "/assets/wavetables/horsi/") + f).c_str(), "rb");
      std::vector<unsigned char> b;
      if (fp)
      {
        int c;
        while ((c = std::fgetc(fp)) != EOF)
          b.push_back((unsigned char)c);
        std::fclose(fp);
      }
      bytes.push_back(b);
    }
    for (size_t i = 0; i < bytes.size(); i++)
      tables.push_back({i == 0 ? "Horse: Whinny" : "Horse: Breath", bytes[i].data(), bytes[i].size()});
    RegisterEmbeddedTables(tables.data(), (int)tables.size());
    auto whinny = MakeFactoryTable(kNumGeneratedTables), breath = MakeFactoryTable(kNumGeneratedTables + 1);
    CHECK(NumFactoryTables() == kNumGeneratedTables + 2 && std::string(FactoryTableName(kNumGeneratedTables)) == "Horse: Whinny" && whinny &&
              whinny->NumFrames() == 64 && breath && breath->NumFrames() == 32,
          "embedded tables follow the generated ones: the whinny (64 frames), the breath (32)");
    WaveOscillator o;
    o.Prepare(FS);
    o.SetTable(whinny.get());
    o.SetFrequency(220.);
    o.SetPosition(0.5, true);
    const auto out = Play(o, 0.5);
    double e = 0.;
    for (float x : out) e += (double)x * x;
    CHECK(std::sqrt(e / out.size()) > 0.05, "and play");
    RegisterEmbeddedTables(nullptr, 0);
  }
  CHECK(gAllocs == 0, "no allocations while playing (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
