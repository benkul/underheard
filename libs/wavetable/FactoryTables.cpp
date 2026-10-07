#include "FactoryTables.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <vector>

namespace underheard::wavetable
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int N = WavetableData::kFrameSize;
constexpr int kMaxHarmonic = N / 2 - 1;

// One frame from harmonic amplitudes (sine phase; amplitude[k] for harmonic k, index 0 unused),
// by inverse FFT, scaled to RMS 0.5.
void FrameFromHarmonics(const std::vector<double>& amp, float* out)
{
  std::vector<std::complex<double>> a(N, 0.);
  for (int k = 1; k < (int)amp.size() && k <= kMaxHarmonic; k++)
  {
    // sin(2 pi k n / N) = (e^{i..} - e^{-i..}) / 2i
    a[k] = std::complex<double>(0., -0.5 * amp[k]);
    a[N - k] = std::complex<double>(0., 0.5 * amp[k]);
  }
  // In-place iterative inverse FFT (unscaled: x[n] = sum a[k] e^{+i 2 pi k n / N}).
  for (int i = 1, j = 0; i < N; i++)
  {
    int bit = N >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(a[i], a[j]);
  }
  for (int len = 2; len <= N; len <<= 1)
  {
    const double ang = 2. * kPi / len;
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (int i = 0; i < N; i += len)
    {
      std::complex<double> w(1.);
      for (int k = 0; k < len / 2; k++)
      {
        const auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
  double e = 0.;
  for (int n = 0; n < N; n++)
    e += a[n].real() * a[n].real();
  const double g = e > 0. ? 0.5 / std::sqrt(e / N) : 0.;
  for (int n = 0; n < N; n++)
    out[n] = (float)(a[n].real() * g);
}

std::unique_ptr<WavetableData> Build(const char* name, int frames, const std::function<void(int frame, double t, std::vector<double>& amp)>& spectrum)
{
  std::vector<float> data((size_t)frames * N);
  std::vector<double> amp(kMaxHarmonic + 1);
  for (int f = 0; f < frames; f++)
  {
    std::fill(amp.begin(), amp.end(), 0.);
    spectrum(f, frames > 1 ? (double)f / (frames - 1) : 0., amp);
    FrameFromHarmonics(amp, data.data() + (size_t)f * N);
  }
  return WavetableData::FromFrames(data.data(), frames, name);
}

// A formant's response at frequency f (a resonance at `centre` with bandwidth `bw`).
double Formant(double f, double centre, double bw)
{
  const double r = f / centre, q = centre / bw;
  return 1. / std::sqrt((1. - r * r) * (1. - r * r) + (r / q) * (r / q));
}
} // namespace

namespace
{
const EmbeddedTable* gEmbedded = nullptr;
int gNumEmbedded = 0;
} // namespace

void RegisterEmbeddedTables(const EmbeddedTable* tables, int count)
{
  gEmbedded = tables;
  gNumEmbedded = tables ? std::max(0, count) : 0;
}

int NumFactoryTables() { return kNumGeneratedTables + gNumEmbedded; }

const char* FactoryTableName(int index)
{
  static const char* names[kNumGeneratedTables] = {"Sine", "Triangle", "Saw", "Square", "Bowed string", "Vowels", "Organ"};
  if (index >= kNumGeneratedTables && index < NumFactoryTables())
    return gEmbedded[index - kNumGeneratedTables].name;
  return names[std::clamp(index, 0, kNumGeneratedTables - 1)];
}

std::unique_ptr<WavetableData> MakeFactoryTable(int index)
{
  if (index >= kNumGeneratedTables && index < NumFactoryTables())
  {
    const EmbeddedTable& e = gEmbedded[index - kNumGeneratedTables];
    return DecodeWavetable(e.data, e.size, e.name).table;
  }
  switch (index)
  {
    case kSine:
      return Build("Sine", 1, [](int, double, std::vector<double>& a) { a[1] = 1.; });
    case kTriangle:
      return Build("Triangle", 1, [](int, double, std::vector<double>& a) {
        for (int k = 1; k <= kMaxHarmonic; k += 2)
          a[k] = (((k - 1) / 2) % 2 ? -1. : 1.) / (double)(k * k);
      });
    case kSaw:
      return Build("Saw", 1, [](int, double, std::vector<double>& a) {
        for (int k = 1; k <= kMaxHarmonic; k++)
          a[k] = 1. / k;
      });
    case kSquare:
      return Build("Square", 1, [](int, double, std::vector<double>& a) {
        for (int k = 1; k <= kMaxHarmonic; k += 2)
          a[k] = 1. / k;
      });
    case kBowedString:
      // A bowed string's 1/k (Helmholtz) spectrum, its top rounded off: light bowing (frame 0)
      // rolls off from the 2nd or 3rd harmonic, heavy bowing (the last frame) keeps ~40.
      return Build("Bowed string", 64, [](int, double t, std::vector<double>& a) {
        const double corner = 2.5 * std::pow(45. / 2.5, t);
        for (int k = 1; k <= kMaxHarmonic; k++)
          a[k] = (1. / k) / std::sqrt(1. + std::pow(k / corner, 4.));
      });
    case kVowels:
    {
      // A voice-like source (1/k) through three formants, morphing ah -> eh -> ee -> oh -> oo.
      // The formants are placed for a G3 (196 Hz); like any wavetable they move with pitch.
      struct V { double f1, f2, f3; };
      static const V vowels[5] = {{730., 1090., 2440.}, {530., 1840., 2480.}, {270., 2290., 3010.}, {570., 840., 2410.}, {300., 870., 2240.}};
      return Build("Vowels", 64, [](int, double t, std::vector<double>& a) {
        const double x = t * 4.;
        const int i = std::min(3, (int)x);
        const double m = x - i;
        const V v{vowels[i].f1 + m * (vowels[i + 1].f1 - vowels[i].f1), vowels[i].f2 + m * (vowels[i + 1].f2 - vowels[i].f2),
                  vowels[i].f3 + m * (vowels[i + 1].f3 - vowels[i].f3)};
        for (int k = 1; k <= kMaxHarmonic; k++)
        {
          const double hz = 196. * k;
          const double env = Formant(hz, v.f1, 80.) + 0.6 * Formant(hz, v.f2, 100.) + 0.3 * Formant(hz, v.f3, 120.);
          a[k] = env / k;
        }
      });
    }
    case kOrgan:
    {
      // Drawbars 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' (harmonics 1, 2, 3, 4, 5, 6, 8), each
      // step 3 dB; five registrations from a lone flute to everything out.
      static const int harmonic[7] = {1, 2, 3, 4, 5, 6, 8};
      static const int reg[5][7] = {{8, 0, 0, 0, 0, 0, 0}, {8, 6, 0, 0, 0, 0, 0}, {8, 8, 6, 4, 0, 0, 0}, {8, 8, 8, 8, 6, 4, 0}, {8, 8, 8, 8, 8, 8, 8}};
      return Build("Organ", 64, [](int, double t, std::vector<double>& a) {
        const double x = t * 4.;
        const int i = std::min(3, (int)x);
        const double m = x - i;
        auto level = [](int v) { return v <= 0 ? 0. : std::pow(10., -(8 - v) * 3. / 20.); };
        for (int d = 0; d < 7; d++)
          a[(size_t)harmonic[d]] = level(reg[i][d]) * (1. - m) + level(reg[i + 1][d]) * m;
      });
    }
    default: return nullptr;
  }
}

} // namespace underheard::wavetable
