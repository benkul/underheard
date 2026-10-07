#include "ReverbIR.h"

#include <algorithm>
#include <cmath>

namespace underheard::room {

size_t Onset(const Channels& irs)
{
  float peak = 0.f;
  for (const auto& c : irs)
    for (float v : c)
      peak = std::max(peak, std::fabs(v));
  size_t first = SIZE_MAX;
  for (const auto& c : irs)
    for (size_t i = 0; i < c.size() && i < first; i++)
      if (std::fabs(c[i]) >= 0.1f * peak)
      {
        first = i;
        break;
      }
  return first == SIZE_MAX ? 0 : first;
}

void RemoveDirect(Channels& irs, double fs)
{
  const size_t onset = Onset(irs), a = onset + (size_t)(0.002 * fs), b = onset + (size_t)(0.0035 * fs);
  for (auto& c : irs)
    for (size_t i = 0; i < c.size() && i < b; i++)
      c[i] *= i < a ? 0.f : (float)(0.5 - 0.5 * std::cos(3.14159265358979 * (double)(i - a) / (double)(b - a)));
}

double MeasureDecay(const Channels& irs, double fs, size_t from)
{
  if (irs.empty())
    return 0.;
  const size_t n = irs[0].size();
  std::vector<double> e(n, 0.);
  double acc = 0.;
  for (size_t i = n; i-- > from;)
  {
    double s = 0.;
    for (const auto& c : irs)
      s += c[i];
    acc += s * s;
    e[i] = acc;
  }
  if (from >= n || e[from] <= 0.)
    return 0.;
  size_t t5 = 0, t25 = 0;
  for (size_t i = from; i < n; i++)
  {
    const double db = 10. * std::log10(e[i] / e[from] + 1e-300);
    if (!t5 && db < -5.) t5 = i;
    if (!t25 && db < -25.) { t25 = i; break; }
  }
  return t25 > t5 ? 3. * (double)(t25 - t5) / fs : 0.;
}

void StretchDecay(Channels& irs, double fs, double stretch, double mixingTime)
{
  stretch = std::clamp(stretch, 0.25, 4.);
  if (irs.empty() || std::fabs(stretch - 1.) < 1e-3)
    return;
  const size_t onset = Onset(irs), start = onset + (size_t)(mixingTime * fs);
  const double rt = MeasureDecay(irs, fs, onset);
  if (rt <= 0.)
    return;
  // Amplitude falls 60 dB in rt; make it fall 60 dB in rt * stretch from `start` on.
  const double k = 3. * std::log(10.) / rt * (1. - 1. / stretch); // > 0 lengthens
  const size_t n = irs[0].size();
  for (auto& c : irs)
    for (size_t i = start; i < n; i++)
      c[i] *= (float)std::min(std::exp(k * (double)(i - start) / fs), 1000.); // at most +60 dB
  if (stretch > 1.)
  {
    // The recording ends sooner than its stretched tail would: fade the last third.
    const size_t f0 = start + (n - start) * 2 / 3;
    for (auto& c : irs)
      for (size_t i = f0; i < n; i++)
        c[i] *= (float)(0.5 + 0.5 * std::cos(3.14159265358979 * (double)(i - f0) / (double)(n - f0)));
  }
}

void Balance(Channels& irs, double fs, double b, double mixingTime)
{
  b = std::clamp(b, -1., 1.);
  const double early = std::min(1., 1. - b), late = std::min(1., 1. + b);
  if (early == 1. && late == 1.)
    return;
  const double t0 = mixingTime, t1 = mixingTime + 0.02;
  for (auto& c : irs)
    for (size_t i = 0; i < c.size(); i++)
    {
      const double t = (double)i / fs;
      const double w = t <= t0 ? 0. : t >= t1 ? 1. : (t - t0) / (t1 - t0); // 0 early .. 1 late
      const double a = w * 3.14159265358979 / 2.;
      c[i] *= (float)(w <= 0. ? early : w >= 1. ? late : early * std::cos(a) + late * std::sin(a));
    }
}

void FadeEnd(Channels& irs, double fraction)
{
  for (auto& c : irs)
  {
    const size_t n = c.size(), f0 = n - (size_t)((double)n * std::clamp(fraction, 0., 1.));
    for (size_t i = f0; i < n; i++)
      c[i] *= (float)(0.5 + 0.5 * std::cos(3.14159265358979 * (double)(i - f0) / (double)(n - f0)));
  }
}

void NormaliseEnergy(Channels& irs)
{
  double worst = 0.;
  for (const auto& c : irs)
  {
    double e = 0.;
    for (float v : c)
      e += (double)v * v;
    worst = std::max(worst, e);
  }
  if (worst <= 0.)
    return;
  const float g = (float)(1. / std::sqrt(worst));
  for (auto& c : irs)
    for (float& v : c)
      v *= g;
}

} // namespace underheard::room
