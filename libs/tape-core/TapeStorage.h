#pragma once
// Stereo tape at the fixed tape rate, stored in chunks so memory follows the recorded length
// instead of the 4-minute maximum.
//
// Threading: Reserve() allocates and must only be called from one non-audio thread (the
// plugin calls it from OnIdle). The audio thread may touch frames below CapacityFrames().

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace underheard {

class TapeStorage
{
public:
  static constexpr int kChunkBits = 16;
  static constexpr int64_t kChunkFrames = int64_t(1) << kChunkBits; // ~1.4 s at 48 kHz

  explicit TapeStorage(int64_t maxFrames)
  : mMaxFrames(maxFrames)
  , mChunks((size_t)((maxFrames + kChunkFrames - 1) / kChunkFrames), nullptr)
  {
  }

  ~TapeStorage()
  {
    for (float* c : mChunks)
      delete[] c;
  }

  TapeStorage(const TapeStorage&) = delete;
  TapeStorage& operator=(const TapeStorage&) = delete;

  int64_t MaxFrames() const { return mMaxFrames; }

  // Non-audio thread only. Makes sure at least `frames` frames (capped at the maximum) exist.
  void Reserve(int64_t frames)
  {
    frames = std::min(frames, mMaxFrames);
    const int needed = (int)((frames + kChunkFrames - 1) / kChunkFrames);
    int have = mNumChunks.load(std::memory_order_relaxed);
    for (; have < needed; have++)
    {
      mChunks[(size_t)have] = new float[(size_t)(2 * kChunkFrames)];
      std::memset(mChunks[(size_t)have], 0, sizeof(float) * (size_t)(2 * kChunkFrames));
      mNumChunks.store(have + 1, std::memory_order_release);
    }
  }

  // Non-audio thread only, and only while the audio thread isn't using this storage.
  void Release()
  {
    const int have = mNumChunks.load(std::memory_order_relaxed);
    mNumChunks.store(0, std::memory_order_release);
    for (int i = 0; i < have; i++)
    {
      delete[] mChunks[(size_t)i];
      mChunks[(size_t)i] = nullptr;
    }
  }

  // Frames the audio thread may use.
  int64_t CapacityFrames() const
  {
    return std::min(mMaxFrames, (int64_t)mNumChunks.load(std::memory_order_acquire) << kChunkBits);
  }

  // Interleaved L/R pair for frame i (i < CapacityFrames()).
  float* Frame(int64_t i) { return mChunks[(size_t)(i >> kChunkBits)] + 2 * (i & (kChunkFrames - 1)); }

private:
  const int64_t mMaxFrames;
  std::vector<float*> mChunks; // fixed size; entries below mNumChunks are valid
  std::atomic<int> mNumChunks{0};
};

} // namespace underheard
