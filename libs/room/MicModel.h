#pragma once
// Emulations of specific microphones, from published data (docs/MIC-DATA.md).
//
// A mic is a frequency response (on axis, per pattern for multi-pattern mics), a pickup per
// octave band (a + (1 - a)·cos θ), how much proximity effect it has, its noise floor, and
// extras: transformer saturation, a phone's voice processing, a cassette recorder's auto-level
// and tape, or a contact pickup's deafness to the room.
//
// The response becomes a minimum-phase filter that's baked into the room's impulse response
// (MicFilter + ApplyFilter, non-real-time); the pickup and proximity go into the room
// generation (per reflection); MicPost does the rest in real time after the convolution.

#include "RoomModel.h"

#include <cstdint>
#include <vector>

namespace underheard::room {

struct ResponsePoint
{
  double hz, db;
};

enum class MicPattern { Omni = 0, Cardioid, Hyper, Figure8 };

struct MicVariant
{
  MicPattern pattern;
  Bands a;                               // pickup per band
  std::vector<ResponsePoint> response;   // on axis, relative to 1 kHz
  double noiseDbSpl;                     // equivalent noise (self-noise, or preamp hiss)
};

enum class MicKind { Normal, Contact, Phone, Cassette };

struct MicModel
{
  const char* name;
  std::vector<MicVariant> variants;      // one per available pattern
  double proximity = 1.;                 // 1 = full near-field bass boost, 0 = none
  double saturation = 0.;                // 0 .. 1: transformer or tape drive
  MicKind kind = MicKind::Normal;

  bool MultiPattern() const { return variants.size() > 1; }
  // The variant for a requested pattern (the closest available one).
  const MicVariant& Variant(MicPattern p) const;
};

const std::vector<MicModel>& MicModels();
int IdealMicIndex();

// A response's level at `hz` (interpolated in log frequency, held flat beyond its ends).
double ResponseDb(const std::vector<ResponsePoint>& response, double hz);

// A minimum-phase FIR (`taps` long) with the given magnitude response at `sampleRate`,
// normalised to 0 dB at 1 kHz unless `absolute` (then the response's levels are kept).
std::vector<float> MicFilter(const std::vector<ResponsePoint>& response, double sampleRate, int taps = 1024, bool absolute = false);

// Convolves `signal` with `fir` in place (the result keeps signal's length). Non-real-time.
void ApplyFilter(std::vector<float>& signal, const std::vector<float>& fir);

// The magnitude (dB) of `fir` at `hz`, for tests.
double FilterGainDb(const std::vector<float>& fir, double hz, double sampleRate);

// The real-time part of a mic, after the room: noise floor, saturation, and the phone's or the
// cassette recorder's processing. Stereo in place.
class MicPost
{
public:
  void Prepare(double sampleRate);
  void Configure(const MicModel& model, const MicVariant& variant);
  void Process(float* l, float* r, int n);

  // The level the phone's AGC or the cassette's ALC is currently adding, in dB (for tests and UI).
  double AutoGainDb() const;

private:
  double mFs = 48000.;
  MicKind mKind = MicKind::Normal;
  double mNoise = 0.;          // linear, relative to full scale (0 dBFS = 100 dB SPL)
  double mDrive = 0.;
  // Auto gain (phone AGC / cassette ALC)
  double mEnv = 0., mGain = 1., mAttack = 0., mRelease = 0., mGainCoef = 0.;
  // Phone noise suppression (a sluggish downward expander)
  double mGateEnv = 0., mGate = 1., mGateCoef = 0.;
  // Cassette: wow and flutter (a short modulated delay), hiss
  std::vector<float> mDelL, mDelR;
  int mDelPos = 0;
  double mWowPhase = 0., mFlutterPhase = 0.;
  uint32_t mRand = 1;
  float Noise();
};

} // namespace underheard::room
