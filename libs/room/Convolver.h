#pragma once
// Room Bleed's convolution: one mono source convolved with one impulse response per output
// channel (two for a stereo mic pair). Built on WDL's low-latency partitioned engine.
//
// Ownership: convolvers are created and freed on the main thread. The audio thread borrows
// them through ConvolverSwitch:
//   main  Offer(c)       hands a new one over (an unclaimed earlier offer comes back to free)
//   audio Process()      claims the offer and crossfades to it; when the fade ends, the old
//                        one goes into a one-slot "retired" mailbox
//   main  TakeRetired()  collects it to free

#include <atomic>
#include <memory>
#include <vector>

class WDL_ConvolutionEngine_Div;

namespace underheard::room {

class Convolver
{
public:
  // `irs` holds 1 or 2 channels of equal length at `sampleRate`.
  static std::unique_ptr<Convolver> Make(const std::vector<std::vector<float>>& irs, double sampleRate);
  ~Convolver();

  // Convolves `n` (<= kMaxBlock) mono samples into out[0..Channels()).
  void Process(const float* in, float* const* out, int n);
  int Channels() const { return mChannels; }
  double SampleRate() const { return mSampleRate; }

  static constexpr int kMaxBlock = 4096;

private:
  Convolver();
  std::unique_ptr<WDL_ConvolutionEngine_Div> mEngine;
  int mChannels = 1;
  double mSampleRate = 48000.;
  std::vector<float> mIn[2]; // per-channel copies of the input (the engine takes writable buffers)
};

class ConvolverSwitch
{
public:
  ~ConvolverSwitch();

  // Main thread. Returns an unclaimed earlier offer, if any, for the caller to free.
  std::unique_ptr<Convolver> Offer(std::unique_ptr<Convolver> next);
  // Main thread: a convolver the audio thread has finished with, to free.
  std::unique_ptr<Convolver> TakeRetired() { return std::unique_ptr<Convolver>(mRetired.exchange(nullptr)); }

  // Audio thread: stereo out (a mono convolver fills both). Silent with no convolver yet.
  void Process(const float* in, float* outL, float* outR, int n);
  bool HasConvolver() const { return mCurrent != nullptr; }

  static constexpr int kFadeSamples = 2048;

private:
  Convolver* mCurrent = nullptr; // audio thread
  Convolver* mFading = nullptr;  // audio thread: being faded out
  int mFadePos = 0;
  std::atomic<Convolver*> mOffered{nullptr};
  std::atomic<Convolver*> mRetired{nullptr};
  float mA[2][Convolver::kMaxBlock] = {}, mB[2][Convolver::kMaxBlock] = {};
};

} // namespace underheard::room
