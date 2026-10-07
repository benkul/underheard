#pragma once
// Non-real-time audio helpers: WAV read/write, resampling, and content hashing.
// Framework-free. None of this may run on the audio thread.

#include <cstdint>
#include <string>
#include <vector>

namespace underheard {

struct AudioData
{
  double sampleRate = 0.;
  int64_t frames = 0;
  std::vector<float> stereo; // interleaved L/R; mono files are copied to both sides
};

// Reads a WAV file: PCM 8/16/24/32-bit or float 32/64-bit, any channel count (the first two
// channels are used). The path is UTF-8.
bool ReadWav(const std::string& path, AudioData& out, std::string& error, int64_t maxFrames = INT64_MAX);

// Writes 32-bit float stereo. Writes to a temporary file first, then renames it, so a
// half-written file never has the final name.
bool WriteWavFloat(const std::string& path, const float* stereo, int64_t frames, double sampleRate, std::string& error);

// Resamples interleaved stereo by ratio = outputRate / inputRate (windowed sinc).
std::vector<float> ResampleStereo(const float* stereo, int64_t frames, double ratio);

// FNV-1a over the samples' bytes; names saved tape files by their content.
uint64_t HashFrames(const float* stereo, int64_t frames);

} // namespace underheard
