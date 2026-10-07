#pragma once
// underheard-chorus: up to four modulated delay voices per side.
//
// The voices share one LFO, spread evenly round its cycle; the right side runs `Phase` ahead of
// the left. The LFO can be a sine, a triangle, or Wander (a smoothed random walk, like worn
// tape). Modes: chorus (dry plus voices), vibrato (voices only), and ensemble (a slow and a fast
// motion together, string-machine style). Feedback (either polarity) pushes it toward a
// flanger; it feeds back the voices' average through Tone, so the loop never gains more than
// the Feedback setting. Age adds a tape warble, a softer top and a little saturation.
//
// For tempo sync the host sets the rate and, while the song plays, locks the LFO to the song
// position with SyncPhase(). Prepare() allocates; everything else is real-time safe.

#include "FxCommon.h"
#include "Warmth.h"

namespace underheard::fx {

class MultiChorus
{
public:
  enum Shape { kSine = 0, kTriangle, kWander };
  enum Mode { kChorus = 0, kVibrato, kEnsemble };
  static constexpr int kMaxVoices = 4;
  static constexpr double kMaxDelayMs = 25., kMaxSwingMs = 8.;

  void Prepare(double sampleRate);
  void Reset();

  void SetVoices(int n) { mVoices = std::clamp(n, 1, kMaxVoices); }
  void SetRate(double hz);
  void SyncPhase(double cycles) { mLfo = cycles - std::floor(cycles); } // the LFO's position, in cycles
  void SetDepth(double amount) { mDepthTarget = std::clamp(amount, 0., 1.); }
  void SetDelay(double ms) { mDelayTarget = std::clamp(ms, 0.3, kMaxDelayMs); }
  void SetSpread(double amount) { mSpread = std::clamp(amount, 0., 1.); }
  void SetShape(int shape) { mShape = std::clamp(shape, 0, 2); }
  void SetMode(int mode) { mMode = std::clamp(mode, 0, 2); }
  void SetFeedback(double amount) { mFeedbackTarget = std::clamp(amount, -0.9, 0.9); }
  void SetPhase(double degrees) { mPhase = degrees / 360.; }
  void SetTone(double hz);
  void SetAge(double amount);
  void SetMix(double amount) { mMixTarget = std::clamp(amount, 0., 1.); }
  void SetWarmth(double amount) { mWarmth.SetAmount(amount); } // on the voices

  void Process(float inL, float inR, float& outL, float& outR);

  double LfoCycles() const { return mLfo; }
  // The delay (ms) voice v of a side is reading at, for tests and drawing.
  double VoiceDelayMs(int side, int voice) const { return mLastDelay[side][voice] / mFs * 1000.; }

private:
  double Shape(double cycles, int side, int voice);
  void UpdateTone();

  double mFs = 48000.;
  DelayLine mLine[2];
  int mVoices = 2, mShape = kSine, mMode = kChorus;
  double mRate = 0.5, mLfo = 0., mFast = 0.;
  double mDepth = 0.5, mDepthTarget = 0.5, mDelay = 10., mDelayTarget = 10., mSpread = 0.7, mPhase = 0.25;
  double mFeedback = 0., mFeedbackTarget = 0., mMix = 0.5, mMixTarget = 0.5, mSmooth = 1., mDelaySmooth = 1.;
  double mTone = 9000., mAge = 0.2;
  double mFbState[2] = {0., 0.};
  double mLastDelay[2][kMaxVoices] = {};
  Wander mWander[2][kMaxVoices], mWarble[2];
  Biquad mToneF[2];
  bool mToneDirty = true, mStarting = true;
  Warmth mWarmth;
};

} // namespace underheard::fx
