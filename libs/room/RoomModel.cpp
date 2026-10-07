#include "RoomModel.h"

#include <algorithm>
#include <cmath>

namespace underheard::room {

namespace {

constexpr double kSpeedOfSound = 343.;
constexpr double kPi = 3.14159265358979323846;
// Band edges between the octave centres (125 Hz .. 8 kHz).
constexpr double kCrossovers[kBands - 1] = {177., 354., 707., 1414., 2828., 5657.};
// Air absorption, intensity per metre (about 20 °C, 50% RH).
constexpr Bands kAir{0.0001, 0.0003, 0.0006, 0.001, 0.0024, 0.0072, 0.025};

// A second-order Butterworth section (RBJ), applied in place.
struct Section
{
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  static Section Make(bool high, double f, double fs)
  {
    const double w = 2. * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2. * 0.70710678);
    const double a0 = 1. + al;
    Section s;
    s.b0 = (high ? (1. + c) : (1. - c)) / 2. / a0;
    s.b1 = (high ? -(1. + c) : (1. - c)) / a0;
    s.b2 = s.b0;
    s.a1 = -2. * c / a0;
    s.a2 = (1. - al) / a0;
    return s;
  }
  void Run(std::vector<double>& x) const
  {
    double z1 = 0., z2 = 0.;
    for (double& v : x)
    {
      const double y = b0 * v + z1;
      z1 = b1 * v - a1 * y + z2;
      z2 = b2 * v - a2 * y;
      v = y;
    }
  }
};

// Linkwitz-Riley 4th order = two Butterworth sections.
void LR4(std::vector<double>& x, bool high, double f, double fs)
{
  const Section s = Section::Make(high, f, fs);
  s.Run(x);
  s.Run(x);
}

struct Rng
{
  uint32_t s;
  double Uniform() // 0 .. 1
  {
    s = s * 1664525u + 1013904223u;
    return (double)(s >> 8) / 16777216.;
  }
  double Gauss() // approximately normal, unit variance
  {
    double g = 0.;
    for (int i = 0; i < 4; i++)
      g += Uniform();
    return (g - 2.) * std::sqrt(3.);
  }
};

double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

} // namespace

namespace materials {
const Material kDrywall{"drywall", {0.29, 0.10, 0.05, 0.04, 0.07, 0.09, 0.10}};
const Material kPlaster{"plaster", {0.013, 0.015, 0.02, 0.03, 0.04, 0.05, 0.05}};
const Material kTile{"tile", {0.01, 0.01, 0.01, 0.01, 0.02, 0.02, 0.02}};
const Material kConcrete{"concrete", {0.01, 0.01, 0.02, 0.02, 0.02, 0.02, 0.03}};
const Material kWoodFloor{"wood", {0.15, 0.11, 0.10, 0.07, 0.06, 0.07, 0.07}};
const Material kCarpet{"carpet", {0.08, 0.24, 0.57, 0.69, 0.71, 0.73, 0.73}};
const Material kCurtains{"curtains", {0.14, 0.35, 0.55, 0.72, 0.70, 0.65, 0.65}};
const Material kClothes{"clothes", {0.30, 0.50, 0.70, 0.80, 0.85, 0.85, 0.85}};
const Material kGlass{"glass", {0.35, 0.25, 0.18, 0.12, 0.07, 0.04, 0.03}};
const Material kAcousticTile{"acoustic tile", {0.50, 0.70, 0.60, 0.70, 0.70, 0.50, 0.50}};
// Furnished surfaces stand in for what an empty box lacks: a rug and a sofa, cabinets, shelves.
const Material kFurnishedFloor{"floor with rug and sofa", {0.20, 0.30, 0.45, 0.50, 0.52, 0.55, 0.55}};
const Material kCabinets{"cabinets", {0.28, 0.22, 0.17, 0.09, 0.10, 0.11, 0.11}};
const Material kBookshelf{"bookshelf", {0.30, 0.40, 0.40, 0.30, 0.30, 0.20, 0.20}};
} // namespace materials

const std::vector<RoomPreset>& RoomPresets()
{
  using namespace materials;
  static const std::vector<RoomPreset> presets = {
    {"Closet", 1.0, 1.5, 2.4, {&kClothes, &kClothes, &kDrywall, &kClothes, &kCarpet, &kDrywall}, 0.7},
    // Tile, but with towels and a shower curtain along one side.
    {"Bathroom", 2.0, 2.5, 2.4, {&kTile, &kTile, &kTile, &kCurtains, &kTile, &kDrywall}, 0.3},
    {"Bedroom", 3.5, 4.0, 2.5, {&kDrywall, &kCurtains, &kDrywall, &kDrywall, &kCarpet, &kDrywall}, 0.5},
    {"Kitchen", 3.0, 4.0, 2.5, {&kCabinets, &kCabinets, &kGlass, &kCabinets, &kWoodFloor, &kDrywall}, 0.4},
    {"Living room", 5.0, 6.0, 2.6, {&kBookshelf, &kGlass, &kDrywall, &kDrywall, &kFurnishedFloor, &kDrywall}, 0.5},
    {"Hallway", 1.2, 8.0, 2.5, {&kDrywall, &kDrywall, &kDrywall, &kDrywall, &kWoodFloor, &kPlaster}, 0.15},
    {"Stairwell", 2.5, 3.0, 8.0, {&kConcrete, &kConcrete, &kConcrete, &kConcrete, &kConcrete, &kConcrete}, 0.3},
    // Concrete, with a finished wall, a drywall ceiling and the usual clutter.
    {"Garage", 6.0, 6.0, 3.0, {&kConcrete, &kConcrete, &kDrywall, &kConcrete, &kConcrete, &kDrywall}, 0.5},
    {"Church hall", 12.0, 25.0, 9.0, {&kPlaster, &kGlass, &kPlaster, &kPlaster, &kWoodFloor, &kWoodFloor}, 0.4},
  };
  return presets;
}

RoomShape MakeShape(const RoomPreset& p, double size, double surfaces)
{
  RoomShape s;
  const double k = std::pow(2., std::clamp(size, -1., 1.));
  s.width = p.width * k;
  s.depth = p.depth * k;
  s.height = p.height * std::sqrt(k); // ceilings grow less than floor plans
  s.surfaces = p.surfaces;
  s.scattering = p.scattering;
  s.surfaceScale = std::pow(2., -1.5 * std::clamp(surfaces, -1., 1.)); // softer = more absorption
  return s;
}

namespace {
// Absorption of surface i in band b, with the Surfaces control applied.
double Alpha(const RoomShape& s, int i, int b)
{
  return std::clamp(s.surfaces[(size_t)i]->alpha[(size_t)b] * s.surfaceScale, 0.002, 0.98);
}
} // namespace

Bands DecayTimes(const RoomShape& s)
{
  const double W = s.width, D = s.depth, H = s.height;
  const double areas[6] = {D * H, D * H, W * H, W * H, W * D, W * D};
  const double V = W * D * H, S = 2. * (W * D + W * H + D * H);
  Bands t{};
  for (int b = 0; b < kBands; b++)
  {
    double sa = 0.;
    for (int i = 0; i < 6; i++)
      sa += areas[i] * Alpha(s, i, b);
    const double mean = std::min(0.98, sa / S);
    t[(size_t)b] = 0.161 * V / (-S * std::log(1. - mean) + 4. * kAir[(size_t)b] * V);
  }
  return t;
}

double ProximityFactor(int band, double r)
{
  const double hz = 125. * std::pow(2., band);
  const double kr = 2. * kPi * hz / kSpeedOfSound * std::max(r, 0.01);
  return std::min(10., std::sqrt(1. + 1. / (kr * kr)));
}

Vec3 SourcePosition(const RoomShape& s, double fx, double fy)
{
  return {std::clamp(fx * s.width, 0.3, std::max(0.3, s.width - 0.3)), std::clamp(fy * s.depth, 0.3, std::max(0.3, s.depth - 0.3)),
          std::min(1.5, s.height * 0.55)};
}

double DefaultBearing(const RoomShape& s)
{
  const Vec3 src = SourcePosition(s, 0.3, 0.25);
  return std::atan2(s.depth - 0.3 - src.y, s.width - 0.3 - src.x) * 180. / kPi;
}

double MaxDistance(const RoomShape& s, double fx, double fy, double bearingDegrees)
{
  const Vec3 src = SourcePosition(s, fx, fy);
  const double b = bearingDegrees * kPi / 180., dx = std::cos(b), dy = std::sin(b);
  auto reach = [](double from, double d, double lo, double hi) {
    if (d > 1e-9) return (hi - from) / d;
    if (d < -1e-9) return (lo - from) / d;
    return 1e9;
  };
  const double t = std::min(reach(src.x, dx, 0.3, s.width - 0.3), reach(src.y, dy, 0.3, s.depth - 0.3));
  return std::max(0.1, t);
}

double MaxDistance(const RoomShape& s) { return MaxDistance(s, 0.3, 0.25, DefaultBearing(s)); }

Placement PlaceMics(const RoomShape& s, double distance, double aimDegrees, bool stereo, int pair)
{
  return PlaceMics(s, 0.3, 0.25, DefaultBearing(s), distance, aimDegrees, stereo, pair);
}

Placement PlaceMics(const RoomShape& s, double fx, double fy, double bearingDegrees, double distance, double aimDegrees, bool stereo, int pair)
{
  Placement p;
  p.source = SourcePosition(s, fx, fy);
  p.maxDistance = MaxDistance(s, fx, fy, bearingDegrees);
  const double d = std::clamp(distance, 0.05, p.maxDistance);
  // The mic is `d` metres from the source along the bearing, at the same height.
  const double b = bearingDegrees * kPi / 180.;
  const Vec3 dir{std::cos(b), std::sin(b), 0.};
  const Vec3 centre{p.source.x + dir.x * d, p.source.y + dir.y * d, p.source.z};
  const Vec3 toSource{-dir.x, -dir.y, 0.};
  const Vec3 side{-dir.y, dir.x, 0.}; // horizontal, perpendicular

  auto rotated = [&](double degrees) {
    const double r = degrees * kPi / 180.;
    return Vec3{toSource.x * std::cos(r) - toSource.y * std::sin(r), toSource.x * std::sin(r) + toSource.y * std::cos(r), 0.};
  };
  auto inside = [&](Vec3 v) {
    v.x = std::clamp(v.x, 0.05, s.width - 0.05);
    v.y = std::clamp(v.y, 0.05, s.depth - 0.05);
    return v;
  };

  if (!stereo)
  {
    p.mics.push_back({inside(centre), rotated(aimDegrees)});
    return p;
  }
  const double spread = pair == 0 ? 0. : pair == 1 ? 0.085 : 0.30;
  const double angle = pair == 0 ? 45. : pair == 1 ? 55. : 0.;
  p.tailCorrelation = pair == 0 ? 0.7 : pair == 1 ? 0.3 : 0.;
  const Vec3 l{centre.x + side.x * spread, centre.y + side.y * spread, centre.z};
  const Vec3 r{centre.x - side.x * spread, centre.y - side.y * spread, centre.z};
  p.mics.push_back({inside(l), rotated(aimDegrees - angle)});
  p.mics.push_back({inside(r), rotated(aimDegrees + angle)});
  return p;
}

RoomIR GenerateRoom(const RoomSpec& spec)
{
  RoomIR out;
  const RoomShape& s = spec.shape;
  const double fs = spec.sampleRate;
  const double W = s.width, D = s.depth, H = s.height;
  const double V = W * D * H;
  out.rt60 = DecayTimes(s);
  for (double& t : out.rt60)
    t *= std::max(0.05, spec.decayScale);
  const double maxRt = *std::max_element(out.rt60.begin(), out.rt60.end());

  // The early part runs to the mixing time (where reflections are dense enough to treat as a
  // diffuse tail), plus a 20 ms crossfade.
  const double tMix = std::clamp(std::sqrt(V) * 0.001, 0.02, 0.08), xfade = 0.02;
  const double tImages = tMix + xfade;
  out.mixingTime = tMix;
  const int64_t len = (int64_t)(std::min(spec.maxSeconds, tImages + 1.1 * maxRt + 0.05) * fs);

  std::array<Bands, 6> refl{}; // per surface, per band: pressure reflection coefficients
  for (int i = 0; i < 6; i++)
    for (int b = 0; b < kBands; b++)
      refl[(size_t)i][(size_t)b] = std::sqrt(1. - Alpha(s, i, b));

  const int nMics = (int)spec.mics.size();
  Rng shared{spec.seed * 2654435761u + 1u};
  std::vector<double> sharedNoise((size_t)len);
  for (double& v : sharedNoise)
    v = shared.Gauss();

  for (int m = 0; m < nMics; m++)
  {
    const MicPlacement& mic = spec.mics[(size_t)m];
    std::array<std::vector<double>, kBands> band;
    for (auto& v : band)
      v.assign((size_t)len, 0.);
    Rng rng{spec.seed * 97u + 13u}; // the same scattering for every mic: they're in one room

    // ---- Image sources
    const double maxDist = tImages * kSpeedOfSound;
    const int nx = (int)std::ceil(maxDist / (2. * W)) + 1, ny = (int)std::ceil(maxDist / (2. * D)) + 1, nz = (int)std::ceil(maxDist / (2. * H)) + 1;
    int count = 0;
    for (int lx = -nx; lx <= nx; lx++)
      for (int ux = 0; ux < 2; ux++)
        for (int ly = -ny; ly <= ny; ly++)
          for (int uy = 0; uy < 2; uy++)
            for (int lz = -nz; lz <= nz; lz++)
              for (int uz = 0; uz < 2; uz++)
              {
                const Vec3 img{(1 - 2 * ux) * spec.source.x + 2. * lx * W, (1 - 2 * uy) * spec.source.y + 2. * ly * D, (1 - 2 * uz) * spec.source.z + 2. * lz * H};
                const Vec3 v{img.x - mic.position.x, img.y - mic.position.y, img.z - mic.position.z};
                const double dist = std::sqrt(Dot(v, v));
                if (dist > maxDist)
                  continue;
                // Bounces off each surface: x = 0, x = W, y = 0, y = D, floor, ceiling.
                const int hits[6] = {std::abs(lx - ux), std::abs(lx), std::abs(ly - uy), std::abs(ly), std::abs(lz - uz), std::abs(lz)};
                const int order = hits[0] + hits[1] + hits[2] + hits[3] + hits[4] + hits[5];
                if (order == 0 && !spec.direct)
                  continue;
                // Scattering: furniture and clutter nudge the later reflections in time and level.
                double t = dist / kSpeedOfSound;
                double jitter = 1.;
                if (order > 0 && s.scattering > 0.)
                {
                  t += (rng.Uniform() - 0.5) * s.scattering * 0.0006 * std::min(order, 4);
                  jitter = 1. + (rng.Uniform() - 0.5) * s.scattering * 0.8;
                }
                const double pos = t * fs;
                const int64_t i0 = (int64_t)pos;
                if (i0 + 1 >= len)
                  continue;
                const double frac = pos - (double)i0;
                const double cosTheta = dist > 1e-9 ? Dot(v, mic.aim) / dist : 1.;
                const double spread = 1. / std::max(dist, 0.5); // the direct sound is capped at +6 dB
                for (int b = 0; b < kBands; b++)
                {
                  // The pattern, with the pressure-gradient part lifted in the near field.
                  const Pickup& pk = spec.pickup;
                  const double near = 1. + pk.proximity * (ProximityFactor(b, dist) - 1.);
                  const double pattern = pk.a[(size_t)b] + (1. - pk.a[(size_t)b]) * cosTheta * near;
                  double a = spread * jitter * pattern * std::exp(-kAir[(size_t)b] * dist / 2.) * (order > 0 ? pk.roomGain : 1.);
                  for (int k = 0; k < 6; k++)
                    if (hits[k])
                      a *= std::pow(refl[(size_t)k][(size_t)b], hits[k]);
                  band[(size_t)b][(size_t)i0] += a * (1. - frac);
                  band[(size_t)b][(size_t)i0 + 1] += a * frac;
                }
                count++;
              }
    out.imageCount = count;

    // ---- Tail: noise per band at the band's decay time, matched to the reflections' level
    // around the mixing time, crossfaded in as the reflections fade out.
    const int64_t mix0 = (int64_t)(tMix * fs), mix1 = (int64_t)(tImages * fs);
    const int64_t win0 = std::max<int64_t>(0, mix0 - (int64_t)(0.01 * fs));
    // XY pairs (one point) share most of their tail; spaced mics hear it independently.
    const double corr = spec.mics.size() > 1 ? spec.tailCorrelation : 1.;
    Rng own{spec.seed * 7919u + (uint32_t)m * 104729u + 5u};
    for (int b = 0; b < kBands; b++)
    {
      std::vector<double>& x = band[(size_t)b];
      const double decay = 3. * std::log(10.) / out.rt60[(size_t)b]; // amplitude: -60 dB at RT60
      double eEarly = 0., eTail = 0.;
      for (int64_t i = win0; i < mix1; i++)
      {
        eEarly += x[(size_t)i] * x[(size_t)i];
        const double env = std::exp(-decay * (double)i / fs);
        eTail += env * env;
      }
      const double gain = eTail > 0. ? std::sqrt(eEarly / eTail) : 0.;
      const double ws = std::sqrt(std::max(0., corr)), wo = std::sqrt(std::max(0., 1. - corr));
      for (int64_t i = mix0; i < len; i++)
      {
        const double tail = gain * std::exp(-decay * (double)i / fs) * (ws * sharedNoise[(size_t)i] + wo * own.Gauss());
        if (i < mix1)
        {
          // Equal-power crossfade: reflections out, tail in.
          const double w = (double)(i - mix0) / (double)(mix1 - mix0);
          x[(size_t)i] = x[(size_t)i] * std::cos(w * kPi / 2.) + tail * std::sin(w * kPi / 2.);
        }
        else
          x[(size_t)i] = tail;
      }
    }

    // ---- Bands: each band keeps its octave (Linkwitz-Riley crossovers), then they're summed.
    std::vector<double> sum((size_t)len, 0.);
    for (int b = 0; b < kBands; b++)
    {
      std::vector<double>& x = band[(size_t)b];
      if (b > 0)
        LR4(x, true, kCrossovers[b - 1], fs);
      if (b < kBands - 1)
        LR4(x, false, kCrossovers[b], fs);
      for (int64_t i = 0; i < len; i++)
        sum[(size_t)i] += x[(size_t)i];
    }

    // Fade the last 10% so the tail ends without a step.
    const int64_t fade0 = len - len / 10;
    std::vector<float> ch((size_t)len);
    for (int64_t i = 0; i < len; i++)
    {
      double v = sum[(size_t)i];
      if (i >= fade0)
        v *= 0.5 * (1. + std::cos(kPi * (double)(i - fade0) / (double)(len - fade0)));
      ch[(size_t)i] = (float)v;
    }
    out.channels.push_back(std::move(ch));
  }

  // Very live rooms (a tiled bathroom) can be far louder than the direct sound: cap the total
  // energy of the louder channel at +6 dB.
  double e = 0.;
  for (const auto& c : out.channels)
  {
    double ce = 0.;
    for (float v : c)
      ce += (double)v * v;
    e = std::max(e, ce);
  }
  if (e > 4.)
  {
    const float g = (float)(2. / std::sqrt(e));
    for (auto& c : out.channels)
      for (float& v : c)
        v *= g;
  }
  return out;
}

} // namespace underheard::room
