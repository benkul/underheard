#pragma once
// Generated rooms: a shoebox room with real dimensions and surface materials, a source, and one
// or two mics, turned into impulse responses (one per mic). Non-real-time.
//
// The early part comes from image sources: every reflection path up to the mixing time, with
// its delay, 1/r spreading, per-band absorption at each bounce, air absorption, a little random
// scattering, and the mic's pickup for the direction it arrives from. After that, a decaying
// noise tail per octave band at the room's Eyring decay time, level-matched to the reflections
// where they hand over. Bands are separated with Linkwitz-Riley crossovers and summed.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace underheard::room {

constexpr int kBands = 7; // octave bands centred 125, 250, 500, 1k, 2k, 4k, 8k Hz
using Bands = std::array<double, kBands>;

struct Vec3
{
  double x = 0., y = 0., z = 0.;
};

// Absorption coefficients per band.
struct Material
{
  const char* name;
  Bands alpha;
};

namespace materials {
extern const Material kDrywall, kPlaster, kTile, kConcrete, kWoodFloor, kCarpet, kCurtains, kClothes, kGlass, kAcousticTile,
  kFurnishedFloor, kCabinets, kBookshelf;
}

// The six surfaces: walls at x = 0, x = W, y = 0, y = D; floor (z = 0); ceiling (z = H).
struct RoomShape
{
  double width = 4., depth = 5., height = 2.5; // metres
  std::array<const Material*, 6> surfaces{};
  double scattering = 0.3;   // 0..1: how much furniture and clutter break up reflections
  double surfaceScale = 1.;  // multiplies every absorption coefficient (the Surfaces control)
};

// A mic's directivity per band: gain(θ) = a + (1 - a)·cos θ (1 omni, 0.5 cardioid,
// 0.25 hypercardioid, 0 figure-8).
struct Pickup
{
  Bands a{1., 1., 1., 1., 1., 1., 1.};
  double proximity = 0.; // 0 .. 1: how much of the near-field bass boost the mic has
  double roomGain = 1.;  // below 1 for a mic that barely hears the air (a contact pickup)
  // Diffuse-field energy response: how much of a reverberant room the pattern hears.
  double DiffuseEnergy(int band) const { return a[band] * a[band] + (1. - a[band]) * (1. - a[band]) / 3.; }
  double Gain(int band, double cosTheta) const { return a[band] + (1. - a[band]) * cosTheta; }
};

struct MicPlacement
{
  Vec3 position;
  Vec3 aim; // unit vector the mic points along
};

struct RoomSpec
{
  RoomShape shape;
  Vec3 source;
  std::vector<MicPlacement> mics; // 1 or 2
  Pickup pickup;
  double sampleRate = 48000.;
  double maxSeconds = 6.;
  double tailCorrelation = 0.; // between two mics' tails: about 0.7 for XY (one point), 0 spaced
  uint32_t seed = 1;
  double decayScale = 1.;      // stretches the room's decay times (the reverb's Decay)
  bool direct = true;          // false: leave out the direct sound (reflections and tail only)
};

struct RoomIR
{
  std::vector<std::vector<float>> channels; // one per mic
  Bands rt60{};                             // predicted decay time per band (s)
  double mixingTime = 0.;                   // where the tail takes over (s)
  int imageCount = 0;
};

// Eyring decay time per band, including air absorption.
Bands DecayTimes(const RoomShape& shape);

RoomIR GenerateRoom(const RoomSpec& spec);

// Bass boost of a first-order pattern's near field, per band, for a source r metres away: the
// pressure-gradient part of a + (1 - a)·cos θ rises as sqrt(1 + 1/(k r)²). Capped at +20 dB.
double ProximityFactor(int band, double r);

// ---- Presets ----

struct RoomPreset
{
  const char* name;
  double width, depth, height;
  std::array<const Material*, 6> surfaces; // x0, xW, y0, yD, floor, ceiling
  double scattering;
};

const std::vector<RoomPreset>& RoomPresets();

// A preset scaled by `size` (-1 .. 1: half to double) with surfaces made softer or harder
// (`surfaces` -1 .. 1: absorption ×2.8 .. ×0.35).
RoomShape MakeShape(const RoomPreset& preset, double size, double surfaces);

// Placement for the PoC: the source is fixed 30% across and 25% into the room; the mic sits
// `distance` metres from it toward the far corner, pointing at it, turned by `aimDegrees`. For
// stereo, `pair` is 0 XY (±45° at one point), 1 ORTF (17 cm apart, ±55°), 2 spaced (60 cm
// apart, both aimed at the source).
struct Placement
{
  Vec3 source;
  std::vector<MicPlacement> mics;
  double maxDistance = 0.;
  double tailCorrelation = 0.;
};
Placement PlaceMics(const RoomShape& shape, double distance, double aimDegrees, bool stereo, int pair);

// The general form: the source at (sourceX, sourceY) as fractions of the width and depth (kept
// 0.3 m from the walls), and the mic `distance` metres from it in the direction `bearingDegrees`
// (in plan: 0 points toward the wall at x = W, 90 toward y = D).
Placement PlaceMics(const RoomShape& shape, double sourceX, double sourceY, double bearingDegrees, double distance, double aimDegrees, bool stereo,
                    int pair);

// The longest usable distance (the mic stays 0.3 m from the walls): toward the far corner from
// the default source, or along a bearing from a given source.
double MaxDistance(const RoomShape& shape);
double MaxDistance(const RoomShape& shape, double sourceX, double sourceY, double bearingDegrees);

// The source's position for the given fractions (kept 0.3 m from the walls), and the bearing
// from the default source toward the far corner (what PlaceMics' short form uses).
Vec3 SourcePosition(const RoomShape& shape, double sourceX, double sourceY);
double DefaultBearing(const RoomShape& shape);

} // namespace underheard::room
