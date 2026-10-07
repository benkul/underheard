// Offline tests for libs/tape-core AudioFile, TapeFiles and CaptureBuffer.
#include "AudioFile.h"
#include "CaptureBuffer.h"
#include "Filters.h"
#include "TapeFiles.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace underheard;
namespace fs = std::filesystem;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> SineStereo(double hz, double fs, int64_t frames)
{
  std::vector<float> v((size_t)(2 * frames));
  for (int64_t i = 0; i < frames; i++)
    v[(size_t)(2 * i)] = v[(size_t)(2 * i + 1)] = 0.5f * (float)std::sin(2. * kPi * hz * (double)i / fs);
  return v;
}

static double FreqL(const std::vector<float>& st, double fs)
{
  double first = -1, last = -1;
  int n = 0;
  for (size_t i = 1; i < st.size() / 2; i++)
  {
    const float a = st[2 * (i - 1)], b = st[2 * i];
    if (a < 0.f && b >= 0.f)
    {
      const double x = (double)(i - 1) + a / (a - b);
      if (first < 0) first = x;
      last = x;
      n++;
    }
  }
  return n > 1 ? (n - 1) * fs / (last - first) : 0.;
}

// Writes a 16-bit or 24-bit PCM file by hand, mono or stereo.
static void WritePcm(const fs::path& p, int bits, int channels, double fs, const std::vector<float>& mono)
{
  std::vector<uint8_t> d;
  for (float x : mono)
    for (int c = 0; c < channels; c++)
    {
      const int32_t v = (int32_t)std::lround(x * (bits == 16 ? 32767. : 8388607.));
      for (int b = 0; b < bits / 8; b++)
        d.push_back((uint8_t)(v >> (8 * b)));
    }
  auto u32 = [](std::ofstream& f, uint32_t x) { f.write((const char*)&x, 4); };
  auto u16 = [](std::ofstream& f, uint16_t x) { f.write((const char*)&x, 2); };
  std::ofstream f(p, std::ios::binary);
  f.write("RIFF", 4); u32(f, (uint32_t)(36 + d.size())); f.write("WAVEfmt ", 8); u32(f, 16);
  u16(f, 1); u16(f, (uint16_t)channels); u32(f, (uint32_t)fs); u32(f, (uint32_t)(fs * channels * bits / 8));
  u16(f, (uint16_t)(channels * bits / 8)); u16(f, (uint16_t)bits);
  f.write("LIST", 4); u32(f, 4); f.write("INFO", 4); // an extra chunk to skip
  f.write("data", 4); u32(f, (uint32_t)d.size()); f.write((const char*)d.data(), (std::streamsize)d.size());
}

int main()
{
  const fs::path dir = fs::temp_directory_path() / "underheard-audiofile-test";
  fs::remove_all(dir);
  fs::create_directories(dir);

  printf("-- float WAV round trip\n");
  {
    auto v = SineStereo(440., 48000., 48000);
    std::string err;
    CHECK(WriteWavFloat((dir / "a.wav").string(), v.data(), 48000, 48000., err), "writes");
    AudioData a;
    CHECK(ReadWav((dir / "a.wav").string(), a, err) && a.frames == 48000 && a.sampleRate == 48000. && a.stereo == v, "reads back identical");
    CHECK(!fs::exists(dir / "a.wav.part"), "no temporary file left behind");
  }

  printf("\n-- PCM formats\n");
  {
    std::vector<float> mono(4410);
    for (size_t i = 0; i < mono.size(); i++)
      mono[i] = 0.5f * (float)std::sin(2. * kPi * 441. * (double)i / 44100.);
    for (int bits : {16, 24})
      for (int ch : {1, 2})
      {
        const fs::path p = dir / ("pcm" + std::to_string(bits) + "_" + std::to_string(ch) + ".wav");
        WritePcm(p, bits, ch, 44100., mono);
        AudioData a;
        std::string err;
        const bool ok = ReadWav(p.string(), a, err);
        double maxErr = 0;
        for (size_t i = 0; ok && i < mono.size(); i++)
          maxErr = std::max(maxErr, (double)std::fabs(a.stereo[2 * i] - mono[i]) + std::fabs(a.stereo[2 * i + 1] - mono[i]));
        CHECK(ok && a.frames == 4410 && a.sampleRate == 44100. && maxErr < (bits == 16 ? 1e-4 : 1e-6),
              "%d-bit %s reads correctly (max error %.2g)", bits, ch == 1 ? "mono" : "stereo", maxErr);
      }
    AudioData a;
    std::string err;
    std::ofstream(dir / "junk.wav") << "not audio";
    CHECK(!ReadWav((dir / "junk.wav").string(), a, err) && !err.empty(), "rejects a non-WAV file (%s)", err.c_str());
  }

  printf("\n-- resampling\n");
  {
    auto v = SineStereo(1000., 44100., 44100);
    auto up = ResampleStereo(v.data(), 44100, 48000. / 44100.);
    CHECK(up.size() == 2 * 48000, "44.1 -> 48 kHz gives 48000 frames (%zu)", up.size() / 2);
    CHECK(std::fabs(FreqL(up, 48000.) - 1000.) < 0.5, "pitch preserved (%.2f Hz)", FreqL(up, 48000.));
    double peak = 0;
    for (size_t i = 2 * 1000; i < up.size() - 2 * 1000; i++) peak = std::max(peak, (double)std::fabs(up[i]));
    CHECK(std::fabs(peak - 0.5) < 0.005, "level preserved (peak %.4f)", peak);

    auto hi = SineStereo(20000., 48000., 48000);
    auto down = ResampleStereo(hi.data(), 48000, 0.5); // to 24 kHz: 20 kHz must be removed
    double rms = 0;
    for (size_t i = 2000; i < down.size() - 2000; i++) rms += (double)down[i] * down[i];
    rms = std::sqrt(rms / (double)(down.size() - 4000));
    CHECK(rms < 0.01, "downsampling removes what can't fit (rms %.4f)", rms);
  }

  printf("\n-- saved tape\n");
  {
    auto v = SineStereo(440., 48000., 24000);
    SavedTape s;
    std::string err;
    CHECK(SaveTape(dir.string(), v.data(), 24000, 48000., s, err), "saves");
    const auto stamp = fs::last_write_time(fs::u8path(s.path));
    SavedTape again;
    CHECK(SaveTape(dir.string(), v.data(), 24000, 48000., again, err) && again.path == s.path && fs::last_write_time(fs::u8path(s.path)) == stamp,
          "same content isn't written twice");
    std::vector<float> back;
    CHECK(LoadSavedTape(s, dir.string(), back, err) && back == v, "loads back identical");
    SavedTape moved = s;
    moved.path = (dir / "elsewhere" / "gone.wav").string();
    CHECK(LoadSavedTape(moved, dir.string(), back, err), "finds it by name when the path is stale");
    v[100] += 0.25f;
    SavedTape other;
    SaveTape(dir.string(), v.data(), 24000, 48000., other, err);
    CHECK(other.path != s.path, "different content gets a different file");
    SavedTape missing = s;
    missing.hash ^= 1;
    missing.path = (dir / "nope.wav").string();
    CHECK(!LoadSavedTape(missing, dir.string(), back, err) && !err.empty(), "reports a missing tape (%s)", err.c_str());

    setenv("UNDERHEARD_TAPE_DIR", dir.string().c_str(), 1);
    CHECK(TapeDirectory("Splicer") == (dir / "Splicer").string(), "UNDERHEARD_TAPE_DIR overrides the folder");
    unsetenv("UNDERHEARD_TAPE_DIR");
    CHECK(TapeDirectory("Splicer").find("Music") != std::string::npos, "default folder is under Music (%s)", TapeDirectory("Splicer").c_str());
  }

  printf("\n-- capture buffer\n");
  {
    CaptureBuffer c;
    c.Allocate(1000);
    for (int i = 0; i < 2500; i++)
      c.Write((float)i, (float)-i);
    c.Publish();
    std::vector<float> out;
    CHECK(c.CopyRange(2500, 300, out) && out[0] == 2200.f && out[598] == 2499.f && out[599] == -2499.f, "copies the most recent frames");
    CHECK(!c.CopyRange(2500, 1001, out), "refuses more than it holds");
    CHECK(!c.CopyRange(2600, 10, out), "refuses frames not published yet");
    CHECK(c.CopyRange(2000, 500, out) && out[0] == 1500.f, "copies an earlier range still held");
  }

  fs::remove_all(dir);
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
