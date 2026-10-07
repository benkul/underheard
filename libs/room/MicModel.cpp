#include "MicModel.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace underheard::room {

namespace {

constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

// In-place iterative radix-2 FFT (n a power of two). Offline use only.
void Fft(std::vector<cd>& a, bool inverse)
{
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; i++)
  {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1)
  {
    const double ang = 2. * kPi / (double)len * (inverse ? 1. : -1.);
    const cd wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len)
    {
      cd w(1.);
      for (size_t k = 0; k < len / 2; k++)
      {
        const cd u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
  if (inverse)
    for (cd& x : a)
      x /= (double)n;
}

size_t NextPow2(size_t n)
{
  size_t p = 1;
  while (p < n)
    p <<= 1;
  return p;
}

} // namespace

double ResponseDb(const std::vector<ResponsePoint>& pts, double hz)
{
  if (pts.empty())
    return 0.;
  if (hz <= pts.front().hz)
    return pts.front().db;
  if (hz >= pts.back().hz)
    return pts.back().db;
  for (size_t i = 1; i < pts.size(); i++)
    if (hz <= pts[i].hz)
    {
      const double t = std::log(hz / pts[i - 1].hz) / std::log(pts[i].hz / pts[i - 1].hz);
      return pts[i - 1].db + (pts[i].db - pts[i - 1].db) * t;
    }
  return pts.back().db;
}

namespace {

// Equivalent noise of a passive mic into a good preamp (EIN -128 dBu), from its sensitivity.
double PreampNoise(double mvPerPa)
{
  return 94. + (-128. - 20. * std::log10(mvPerPa / 1000. / 0.775));
}

Bands Same(double a) { Bands b; b.fill(a); return b; }

} // namespace

const MicVariant& MicModel::Variant(MicPattern p) const
{
  for (const MicVariant& v : variants)
    if (v.pattern == p)
      return v;
  // Closest available: by how directional it is.
  auto order = [](MicPattern q) { return (int)q; };
  const MicVariant* best = &variants[0];
  for (const MicVariant& v : variants)
    if (std::abs(order(v.pattern) - order(p)) < std::abs(order(best->pattern) - order(p)))
      best = &v;
  return *best;
}

const std::vector<MicModel>& MicModels()
{
  using P = MicPattern;
  static const std::vector<MicModel> mics = [] {
    std::vector<MicModel> m;
    // Dynamics
    m.push_back({"Shure SM57", {{P::Cardioid, {0.5, 0.5, 0.5, 0.5, 0.45, 0.4, 0.4},
      {{20, -20}, {50, -9}, {100, -3}, {200, 0}, {500, -1}, {1000, 0}, {2000, 1}, {4000, 3}, {6000, 6}, {8000, 4}, {10000, 3}, {12000, 0}, {15000, -8}, {20000, -20}},
      PreampNoise(1.9)}}, 1., 0.25, MicKind::Normal});
    m.push_back({"Shure SM7B", {{P::Cardioid, {0.5, 0.5, 0.5, 0.5, 0.45, 0.42, 0.42},
      {{20, -14}, {50, -5}, {100, -3}, {200, -2}, {500, -1}, {1000, 0}, {2000, 0}, {4000, 1}, {6000, 1}, {8000, 0}, {10000, 0}, {12000, -1}, {15000, -2}, {19500, -13}, {20000, -15}},
      PreampNoise(1.12)}}, 1., 0.2, MicKind::Normal});
    m.push_back({"Electro-Voice RE20", {{P::Cardioid, Same(0.5),
      {{20, -12}, {45, -3}, {80, 0}, {1000, 0}, {6000, 0}, {8000, 1.5}, {10000, 2.5}, {12000, 2.5}, {15000, 1}, {18000, -3}, {20000, -6}},
      PreampNoise(1.5)}}, 0.15, 0.15, MicKind::Normal}); // Variable-D: almost no proximity effect
    m.push_back({"Sennheiser MD 421 II", {{P::Cardioid, {0.5, 0.5, 0.5, 0.5, 0.45, 0.4, 0.35},
      {{20, -16}, {40, -8}, {50, -6}, {70, -2.5}, {100, 0}, {1000, 0}, {2000, 2.5}, {3000, 4.5}, {4000, 7}, {5000, 7.5}, {6000, 7}, {8000, 4.5}, {10000, 4.5},
       {12000, 5}, {15000, 4}, {17000, -3}, {20000, -11}},
      PreampNoise(2.0)}}, 1., 0.2, MicKind::Normal});
    // Condensers
    m.push_back({"Neumann U 87 Ai", {
      {P::Omni, {1, 1, 1, 1, 0.95, 0.85, 0.75},
       {{20, -4}, {50, -1}, {100, 0}, {5000, 0}, {8000, 4}, {9500, 4.5}, {15000, 2}, {20000, -6}}, 15},
      {P::Cardioid, {0.5, 0.5, 0.5, 0.5, 0.48, 0.42, 0.38},
       {{20, -6}, {50, -2}, {100, 0}, {5000, 0}, {8000, 2}, {10000, 3}, {12000, 2}, {15000, -2}, {20000, -9}}, 12},
      {P::Figure8, Same(0.),
       {{20, -9}, {50, -5}, {100, -2}, {150, 0}, {2000, 0}, {5500, 3}, {10000, 0}, {15000, -6}, {20000, -12}}, 14}},
      1., 0.15, MicKind::Normal});
    const std::vector<ResponsePoint> xls = {{20, -2}, {30, 0}, {1500, -1}, {4000, 1}, {13000, 1}, {20000, -2}};
    const std::vector<ResponsePoint> xlii = {{20, -2}, {30, 0}, {1500, -1}, {3000, 0}, {5000, 2}, {8000, 3.5}, {10000, 4}, {12000, 3}, {15000, 0}, {20000, -3.5}};
    m.push_back({"AKG C414 XLS", {{P::Omni, Same(1.), xls, 6}, {P::Cardioid, Same(0.5), xls, 6}, {P::Hyper, Same(0.25), xls, 6}, {P::Figure8, Same(0.), xls, 6}}, 1., 0., MicKind::Normal});
    m.push_back({"AKG C414 XLII", {{P::Omni, Same(1.), xlii, 6}, {P::Cardioid, Same(0.5), xlii, 6}, {P::Hyper, Same(0.25), xlii, 6}, {P::Figure8, Same(0.), xlii, 6}}, 1., 0., MicKind::Normal});
    m.push_back({"DPA 4006 (omni)", {{P::Omni, {1, 1, 1, 1, 0.95, 0.85, 0.7}, {{20, 0}, {15000, 0}, {20000, -1.5}}, 15}}, 1., 0., MicKind::Normal});
    // Ribbons
    m.push_back({"Royer R-121", {{P::Figure8, Same(0.),
      {{20, -4}, {30, -2}, {50, 1}, {100, 0}, {1000, 0}, {2000, 1}, {4000, 1}, {6000, 0.5}, {8000, 0}, {10000, -0.5}, {12000, -1}, {15000, -1.5}, {18000, -2}, {20000, -3}},
      PreampNoise(3.5)}}, 1., 0.2, MicKind::Normal});
    m.push_back({"Coles 4038", {{P::Figure8, Same(0.),
      {{20, -6}, {45, 0}, {60, -1}, {100, 1}, {200, 1}, {500, 0}, {1000, 0}, {2000, -0.3}, {8000, -0.3}, {10000, -0.5}, {15000, -2}, {20000, -4}},
      PreampNoise(0.56)}}, 1., 0.25, MicKind::Normal});
    // Others
    m.push_back({"DPA 4060 lavalier (on a chest)", {{P::Omni, {1, 1, 1, 1, 1, 0.95, 0.9},
      {{100, 0}, {750, 3}, {1500, 0}, {2500, -7}, {3500, -7}, {5000, -1}, {8000, 2}, {12000, 3}, {15000, 3}, {20000, 0}}, 23}}, 0., 0., MicKind::Normal});
    m.push_back({"Contact pickup (piezo)", {{P::Omni, Same(1.),
      {{20, -30}, {80, -3}, {150, 0}, {1000, 0}, {2000, 3}, {3500, 8}, {5000, 2}, {8000, -6}, {12000, -20}, {20000, -35}}, 25}}, 0., 0.1, MicKind::Contact});
    m.push_back({"Phone (iPhone, voice processing)", {{P::Omni, {1, 1, 1, 1, 0.95, 0.8, 0.6},
      {{20, -30}, {100, -12}, {200, -3}, {300, 0}, {1000, 0}, {8000, 0}, {11500, -3}, {13000, 0}, {16000, -1}, {18000, -6}, {20000, -40}}, 30}}, 0., 0., MicKind::Phone});
    m.push_back({"Portable cassette recorder", {{P::Omni, {1, 1, 1, 1, 0.95, 0.85, 0.75},
      {{20, -30}, {80, -12}, {150, -3}, {300, 0}, {1000, 0}, {3000, 3}, {5000, 1}, {8000, -4}, {10000, -10}, {12000, -20}, {16000, -35}}, 32}}, 0., 0.4, MicKind::Cassette});
    // A perfect mic of each pattern: flat, noiseless.
    const std::vector<ResponsePoint> flat = {{20, 0}, {20000, 0}};
    m.push_back({"Ideal (flat)", {{P::Omni, Same(1.), flat, -100}, {P::Cardioid, Same(0.5), flat, -100}, {P::Hyper, Same(0.25), flat, -100}, {P::Figure8, Same(0.), flat, -100}},
      1., 0., MicKind::Normal});
    return m;
  }();
  return mics;
}

int IdealMicIndex() { return (int)MicModels().size() - 1; }

std::vector<float> MicFilter(const std::vector<ResponsePoint>& response, double fs, int taps, bool absolute)
{
  // Minimum phase by the real cepstrum: fold the log-magnitude's cepstrum onto positive time.
  const size_t n = NextPow2((size_t)taps * 8);
  const double ref = absolute ? 0. : ResponseDb(response, 1000.);
  std::vector<cd> x(n);
  for (size_t k = 0; k <= n / 2; k++)
  {
    const double hz = std::max(1., (double)k * fs / (double)n);
    const double mag = std::pow(10., (ResponseDb(response, hz) - ref) / 20.);
    x[k] = std::log(std::max(mag, 1e-6));
    if (k > 0 && k < n / 2)
      x[n - k] = x[k];
  }
  Fft(x, true); // the real cepstrum
  for (size_t i = 1; i < n / 2; i++)
    x[i] *= 2.;
  for (size_t i = n / 2 + 1; i < n; i++)
    x[i] = 0.;
  Fft(x, false);
  for (cd& v : x)
    v = std::exp(v);
  Fft(x, true);
  std::vector<float> h((size_t)taps);
  const size_t fade = (size_t)taps / 4;
  for (size_t i = 0; i < (size_t)taps; i++)
  {
    double v = x[i].real();
    if (i >= (size_t)taps - fade)
      v *= 0.5 * (1. + std::cos(kPi * (double)(i - ((size_t)taps - fade)) / (double)fade));
    h[i] = (float)v;
  }
  return h;
}

void ApplyFilter(std::vector<float>& signal, const std::vector<float>& fir)
{
  if (signal.empty() || fir.empty())
    return;
  const size_t n = NextPow2(signal.size() + fir.size());
  std::vector<cd> a(n), b(n);
  for (size_t i = 0; i < signal.size(); i++)
    a[i] = signal[i];
  for (size_t i = 0; i < fir.size(); i++)
    b[i] = fir[i];
  Fft(a, false);
  Fft(b, false);
  for (size_t i = 0; i < n; i++)
    a[i] *= b[i];
  Fft(a, true);
  for (size_t i = 0; i < signal.size(); i++)
    signal[i] = (float)a[i].real();
}

double FilterGainDb(const std::vector<float>& fir, double hz, double fs)
{
  cd s = 0.;
  const double w = 2. * kPi * hz / fs;
  for (size_t i = 0; i < fir.size(); i++)
    s += (double)fir[i] * std::polar(1., -w * (double)i);
  return 20. * std::log10(std::abs(s) + 1e-12);
}

// ---- MicPost -----------------------------------------------------------------------------

float MicPost::Noise()
{
  mRand = mRand * 1664525u + 1013904223u;
  return (float)((double)(int32_t)mRand * (1. / 2147483648.));
}

void MicPost::Prepare(double fs)
{
  mFs = fs;
  mDelL.assign((size_t)(0.02 * fs) + 8, 0.f);
  mDelR.assign(mDelL.size(), 0.f);
  mDelPos = 0;
  mGainCoef = 1. - std::exp(-1. / (0.02 * fs));
  mGateCoef = 1. - std::exp(-1. / (0.15 * fs));
}

void MicPost::Configure(const MicModel& model, const MicVariant& variant)
{
  mKind = model.kind;
  // 0 dBFS = 100 dB SPL, so a quiet condenser's noise is far down and only the auto-gain
  // devices bring noise up to where it's heard.
  mNoise = std::pow(10., (variant.noiseDbSpl - 100.) / 20.);
  mDrive = model.saturation * 2.;
  if (mKind == MicKind::Phone)
  {
    mAttack = 1. - std::exp(-1. / (0.05 * mFs));
    mRelease = 1. - std::exp(-1. / (2.0 * mFs));
  }
  else if (mKind == MicKind::Cassette)
  {
    mAttack = 1. - std::exp(-1. / (0.02 * mFs));
    mRelease = 1. - std::exp(-1. / (3.0 * mFs));
  }
}

double MicPost::AutoGainDb() const { return 20. * std::log10(mGain * mGate); }

void MicPost::Process(float* l, float* r, int n)
{
  for (int i = 0; i < n; i++)
  {
    double x[2] = {l[i], r[i]};
    for (double& v : x)
    {
      v += mNoise * Noise(); // the mic's own noise floor (or its preamp's)
      if (mDrive > 0.)
        v = std::tanh(v * (1. + mDrive)) / (1. + mDrive); // unity gain for small signals
    }

    if (mKind == MicKind::Phone || mKind == MicKind::Cassette)
    {
      // Auto gain: the phone follows loudness (RMS) slowly toward -20 dBFS, up to +20 dB; the
      // cassette's ALC follows peaks fast and lets go slowly, toward -12 dBFS, up to +30 dB.
      const double level = mKind == MicKind::Phone ? (x[0] * x[0] + x[1] * x[1]) * 0.5 : std::max(std::fabs(x[0]), std::fabs(x[1]));
      mEnv += (level - mEnv) * (level > mEnv ? mAttack : mRelease);
      const double env = mKind == MicKind::Phone ? std::sqrt(mEnv) : mEnv;
      const double target = mKind == MicKind::Phone ? 0.1 : 0.25;
      const double maxGain = mKind == MicKind::Phone ? 10. : 31.6;
      const double want = std::clamp(target / std::max(env, 1e-6), 0.5, maxGain);
      mGain += (want - mGain) * mGainCoef;
      if (mKind == MicKind::Phone)
      {
        // Noise suppression: below about -55 dBFS the phone ducks the signal by up to 15 dB,
        // sluggishly, so quiet passages pump.
        mGateEnv += (env - mGateEnv) * mGateCoef;
        const double gateWant = mGateEnv < 0.0018 ? 0.18 : 1.;
        mGate += (gateWant - mGate) * mGateCoef;
      }
      for (double& v : x)
        v *= mGain * (mKind == MicKind::Phone ? mGate : 1.);
    }

    if (mKind == MicKind::Cassette)
    {
      // Tape: hiss, then a little wow and flutter (a gently modulated delay).
      for (double& v : x)
        v += 0.0008 * Noise();
      mWowPhase += 2. * kPi * 0.6 / mFs;
      mFlutterPhase += 2. * kPi * 7.0 / mFs;
      const double delay = 0.005 * mFs + 0.00021 * mFs * std::sin(mWowPhase) + 0.000011 * mFs * std::sin(mFlutterPhase);
      const int size = (int)mDelL.size();
      mDelL[(size_t)mDelPos] = (float)x[0];
      mDelR[(size_t)mDelPos] = (float)x[1];
      const double rp = (double)mDelPos - delay;
      const int i0 = (int)std::floor(rp);
      const double f = rp - (double)i0;
      auto at = [&](const std::vector<float>& d, int k) { return d[(size_t)((k % size + size) % size)]; };
      x[0] = at(mDelL, i0) * (1. - f) + at(mDelL, i0 + 1) * f;
      x[1] = at(mDelR, i0) * (1. - f) + at(mDelR, i0 + 1) * f;
      mDelPos = (mDelPos + 1) % size;
    }
    l[i] = (float)x[0];
    r[i] = (float)x[1];
  }
}

} // namespace underheard::room
