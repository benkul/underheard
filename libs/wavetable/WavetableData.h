/** @file WavetableData.h
 *  @brief (From horsi-vst3's HorsiWave/dsp, unchanged but for the namespace.) One loaded wavetable: its frames at the oscillator's 2048-sample
 *  cycle length, plus band-limited copies for alias-free playback.
 *
 *  Immutable once built. Built off the audio thread, then shared read-only
 *  by every voice.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace underheard::wavetable
{

class WavetableData
{
public:
  static constexpr int kFrameSize = 2048; // the firmware's MAX_SAMPLES_PER_CYCLE
  static constexpr int kMaxFrames = 256;  // Serum/Vital's limit; larger files are thinned
  /** Level 0 is the raw frames (up to 1024 harmonics). Level L keeps only
   *  harmonics 1..(1024 >> L), for notes high enough that more would alias,
   *  stored at least 4x oversampled. */
  static constexpr int kNumLevels = 11;

  /** Builds a table from frames that are already kFrameSize samples long. The
   *  raw level is an exact copy, so firmware-format tables play bit-identically. */
  static std::unique_ptr<WavetableData> FromFrames(const float* frames, int numFrames, std::string name);

  int NumFrames() const { return mNumFrames; }
  const std::string& Name() const { return mName; }

  /** Raw frame f. Frames are contiguous and followed by one zero frame, so
   *  reading one sample past a frame is safe (the firmware's PopSample can). */
  const float* RawFrame(int f) const { return mRaw.data() + (size_t) f * kFrameSize; }

  /** Band-limited frame f at level 1..kNumLevels-1. It has LevelLength(level)
   *  samples plus one guard sample equal to the first, for interpolation. */
  const float* LevelFrame(int level, int f) const
  {
    const Level& l = mLevels[level];
    return l.data.data() + (size_t) f * (l.length + 1);
  }
  int LevelLength(int level) const { return level == 0 ? kFrameSize : mLevels[level].length; }

  size_t MemoryBytes() const;

private:
  struct Level
  {
    int length = 0;
    std::vector<float> data;
  };

  int mNumFrames = 0;
  std::string mName;
  std::vector<float> mRaw;
  Level mLevels[kNumLevels]; // [0] unused: level 0 is mRaw
};

struct DecodedWavetable
{
  std::unique_ptr<WavetableData> table;
  std::string error;   // set when table is null
  int sourceCycle = 0; // cycle length found in the file, before resampling
  int sourceFrames = 0;
};

/** Decodes a .wav into a wavetable. Handles:
 *  - 8/16/24/32-bit PCM and 32/64-bit float, including WAVE_FORMAT_EXTENSIBLE
 *  - mono, or more channels mixed down
 *  - cycle length from Serum's "clm " chunk ("<!>2048 ..."), otherwise:
 *    WaveEdit's 64 x 256, multiples of 2048 (CHOMPI, Serum, Vital), a single
 *    cycle of up to 8192 samples (AKWF-style), or other 256-sample cycles
 *  Other cycle lengths are resampled to 2048 in the frequency domain, which
 *  also band-limits them. Tables longer than 256 frames are thinned evenly. */
DecodedWavetable DecodeWavetable(const void* bytes, size_t numBytes, const std::string& name);

} // namespace underheard::wavetable
