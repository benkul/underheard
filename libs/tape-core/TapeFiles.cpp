#include "TapeFiles.h"

#include "AudioFile.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace underheard {

namespace fs = std::filesystem;

namespace {

fs::path PathFromUtf8(const std::string& s)
{
#if defined(__cpp_char8_t)
  return fs::path(std::u8string(s.begin(), s.end()));
#else
  return fs::u8path(s);
#endif
}

std::string Utf8(const fs::path& p)
{
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

fs::path HomeMusic()
{
#ifdef _WIN32
  if (const wchar_t* profile = _wgetenv(L"USERPROFILE"))
    return fs::path(profile) / L"Music";
#else
  if (const char* home = std::getenv("HOME"))
    return fs::path(home) / "Music";
#endif
  return fs::temp_directory_path();
}

std::string FileName(uint64_t hash)
{
  char name[32];
  std::snprintf(name, sizeof name, "%016llx.wav", (unsigned long long)hash);
  return name;
}

} // namespace

std::string TapeDirectory(const std::string& plugin)
{
  if (const char* over = std::getenv("UNDERHEARD_TAPE_DIR"))
    if (*over)
      return Utf8(PathFromUtf8(over) / PathFromUtf8(plugin));
  return Utf8(HomeMusic() / "Underheard" / PathFromUtf8(plugin));
}

bool SaveTape(const std::string& dir, const float* stereo, int64_t frames, double tapeRate, SavedTape& saved, std::string& error)
{
  saved.hash = HashFrames(stereo, frames);
  saved.frames = frames;
  const fs::path p = PathFromUtf8(dir) / FileName(saved.hash);
  saved.path = Utf8(p);
  std::error_code ec;
  if (fs::exists(p, ec) && fs::file_size(p, ec) == (uintmax_t)(46 + 8 * frames))
    return true; // same content, already saved
  return WriteWavFloat(saved.path, stereo, frames, tapeRate, error);
}

bool LoadSavedTape(const SavedTape& saved, const std::string& dir, std::vector<float>& stereo, std::string& error)
{
  std::vector<fs::path> candidates = {PathFromUtf8(saved.path), PathFromUtf8(dir) / FileName(saved.hash)};
  for (const fs::path& p : candidates)
  {
    std::error_code ec;
    if (!fs::exists(p, ec))
      continue;
    AudioData a;
    if (!ReadWav(Utf8(p), a, error))
      continue;
    if (a.frames != saved.frames || HashFrames(a.stereo.data(), a.frames) != saved.hash)
    {
      error = "saved tape " + Utf8(p.filename()) + " has changed on disk";
      continue;
    }
    stereo = std::move(a.stereo);
    return true;
  }
  if (error.empty())
    error = "saved tape " + FileName(saved.hash) + " is missing";
  return false;
}

} // namespace underheard
