#pragma once
// Room tone: the background sound a room has with nobody playing. Generated live from seeded
// noise (no files), so it never audibly repeats, or played from a loaded recording.
// Real-time safe after Prepare().

#include <cstdint>
#include <vector>

namespace underheard::room {

// A loaded recording, made seamless for looping (non-real-time; see MakeToneLoop).
struct ToneLoop
{
  std::vector<float> l, r;
};

// Crossfades the end of a recording into its start (over `fadeSeconds`) so it loops without a
// seam. Interleaved stereo in.
ToneLoop MakeToneLoop(const std::vector<float>& stereo, double fadeSeconds, double sampleRate);

class RoomTone
{
public:
  enum Preset { kNone = 0, kApartment, kKitchen, kOffice, kHallway, kBasement, kLoaded, kNumPresets };

  void Prepare(double sampleRate, uint32_t seed = 1);
  void SetPreset(int preset);
  // The recording to play for kLoaded (owned by the caller; may be null).
  void SetLoop(const ToneLoop* loop) { mLoop = loop; }

  // Adds `gain` × the tone into l and r. A preset's level is about -10 dBFS RMS before gain.
  void Process(float* l, float* r, int n, float gain);

  bool CompressorRunning() const { return mFridgeOn; } // the kitchen fridge (for tests)

private:
  struct Filter
  {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void Set(int type, double hz, double q, double fs); // 0 low-pass, 1 high-pass, 2 band-pass
    double Run(double x);
  };
  struct Pink
  {
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    double Run(double white);
  };

  double White();
  double Wander(double& state, double& target, double& timer, double rate, double interval);

  double mFs = 48000.;
  int mPreset = kApartment;
  uint32_t mRand = 1;
  Pink mPinkL, mPinkR;
  double mBrown = 0.;
  Filter mTraffic, mHvacL, mHvacR, mDuct, mAirL, mAirR, mBoiler, mTick;
  double mHumPhase = 0.;
  double mSwell = 0., mSwellTarget = 0., mSwellTimer = 0.;
  double mDrift = 0., mDriftTarget = 0., mDriftTimer = 0.;
  // Kitchen fridge
  bool mFridgeOn = false;
  double mFridgeTimer = 20., mFridgeEnv = 0., mFridgePhase = 0., mClunk = 0., mClunkPhase = 0.;
  // Basement
  double mTickEnv = 0., mBoilerPhase = 0.;
  // Loaded recording
  const ToneLoop* mLoop = nullptr;
  size_t mLoopPos = 0;
};

} // namespace underheard::room
