#pragma once
// Where saved tape lives, and saving/finding it by content hash. Non-real-time.
//
// Files are named by a hash of their samples and never overwritten, so two projects (or a
// project and its "Save As" copy) can't change each other's loops.

#include <cstdint>
#include <string>
#include <vector>

namespace underheard {

// ~/Music/Underheard/<plugin> (or %USERPROFILE%\Music\Underheard\<plugin> on Windows).
// The UNDERHEARD_TAPE_DIR environment variable overrides it (the tests use this).
std::string TapeDirectory(const std::string& plugin);

struct SavedTape
{
  std::string path;   // UTF-8
  uint64_t hash = 0;
  int64_t frames = 0;
};

// Writes the tape as <dir>/<hash>.wav at the tape rate, unless that file already exists.
bool SaveTape(const std::string& dir, const float* stereo, int64_t frames, double tapeRate, SavedTape& saved, std::string& error);

// Reads a saved tape back, checking its hash. If `saved.path` is gone, looks for the same
// file name in `dir` (for when the folder has moved).
bool LoadSavedTape(const SavedTape& saved, const std::string& dir, std::vector<float>& stereo, std::string& error);

} // namespace underheard
