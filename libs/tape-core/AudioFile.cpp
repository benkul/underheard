#include "AudioFile.h"

#include "Filters.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

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

uint32_t U32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void PutU32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
void PutU16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }

} // namespace

bool ReadWav(const std::string& path, AudioData& out, std::string& error, int64_t maxFrames)
{
  std::ifstream f(PathFromUtf8(path), std::ios::binary);
  if (!f)
  {
    error = "can't open the file";
    return false;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
  {
    error = "not a WAV file";
    return false;
  }

  int format = 0, channels = 0, bits = 0;
  double rate = 0.;
  const uint8_t* data = nullptr;
  size_t dataSize = 0;
  size_t pos = 12;
  while (pos + 8 <= bytes.size())
  {
    const uint8_t* ck = bytes.data() + pos;
    size_t size = U32(ck + 4);
    const size_t body = pos + 8;
    if (std::memcmp(ck, "fmt ", 4) == 0 && size >= 16 && body + 16 <= bytes.size())
    {
      format = U16(ck + 8);
      channels = U16(ck + 10);
      rate = (double)U32(ck + 12);
      bits = U16(ck + 22);
      if (format == 0xFFFE && size >= 40 && body + 40 <= bytes.size())
        format = U16(ck + 8 + 24); // WAVE_FORMAT_EXTENSIBLE: the subformat's first two bytes
    }
    else if (std::memcmp(ck, "data", 4) == 0)
    {
      data = ck + 8;
      dataSize = std::min(size, bytes.size() - body); // also handles streamed files (size 0xFFFFFFFF)
      break;
    }
    pos = body + size + (size & 1);
  }

  if (!data || channels < 1 || rate <= 0.)
  {
    error = "no audio found in the file";
    return false;
  }
  const bool isFloat = format == 3 && (bits == 32 || bits == 64);
  const bool isPcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
  if (!isFloat && !isPcm)
  {
    error = "unsupported WAV format (use PCM or float)";
    return false;
  }

  const int bytesPer = bits / 8;
  const size_t frameBytes = (size_t)bytesPer * (size_t)channels;
  const int64_t frames = std::min<int64_t>((int64_t)(dataSize / frameBytes), maxFrames);

  auto sample = [&](const uint8_t* p) -> float {
    if (isFloat)
    {
      if (bits == 32) { float x; std::memcpy(&x, p, 4); return x; }
      double x; std::memcpy(&x, p, 8); return (float)x;
    }
    switch (bits)
    {
      case 8: return ((float)p[0] - 128.f) / 128.f;
      case 16: return (float)(int16_t)U16(p) / 32768.f;
      case 24: return (float)((int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24)) >> 8) / 8388608.f;
      default: return (float)((double)(int32_t)U32(p) / 2147483648.);
    }
  };

  out.sampleRate = rate;
  out.frames = frames;
  out.stereo.assign((size_t)(2 * frames), 0.f);
  for (int64_t i = 0; i < frames; i++)
  {
    const uint8_t* fr = data + (size_t)i * frameBytes;
    const float l = sample(fr);
    const float r = channels > 1 ? sample(fr + bytesPer) : l;
    out.stereo[(size_t)(2 * i)] = l;
    out.stereo[(size_t)(2 * i + 1)] = r;
  }
  return true;
}

bool WriteWavFloat(const std::string& path, const float* stereo, int64_t frames, double sampleRate, std::string& error)
{
  const uint64_t dataBytes = (uint64_t)frames * 8;
  if (dataBytes > 0xFFFFFFFFull - 64)
  {
    error = "too long for a WAV file";
    return false;
  }

  std::vector<uint8_t> h;
  h.insert(h.end(), {'R', 'I', 'F', 'F'});
  PutU32(h, (uint32_t)(4 + 26 + 8 + dataBytes));
  h.insert(h.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  PutU32(h, 18);
  PutU16(h, 3);                    // IEEE float
  PutU16(h, 2);                    // channels
  PutU32(h, (uint32_t)sampleRate);
  PutU32(h, (uint32_t)sampleRate * 8);
  PutU16(h, 8);                    // block align
  PutU16(h, 32);
  PutU16(h, 0);                    // cbSize
  h.insert(h.end(), {'d', 'a', 't', 'a'});
  PutU32(h, (uint32_t)dataBytes);

  const fs::path final = PathFromUtf8(path);
  fs::path tmp = final;
  tmp += ".part";
  std::error_code ec;
  fs::create_directories(final.parent_path(), ec);
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f)
    {
      error = "can't write " + path;
      return false;
    }
    f.write((const char*)h.data(), (std::streamsize)h.size());
    f.write((const char*)stereo, (std::streamsize)dataBytes);
    if (!f)
    {
      error = "write failed for " + path;
      return false;
    }
  }
  fs::rename(tmp, final, ec);
  if (ec)
  {
    fs::remove(tmp, ec);
    error = "can't rename into " + path;
    return false;
  }
  return true;
}

std::vector<float> ResampleStereo(const float* in, int64_t frames, double ratio)
{
  if (frames <= 0 || ratio <= 0.)
    return {};
  if (std::fabs(ratio - 1.) < 1e-9)
    return std::vector<float>(in, in + 2 * frames);

  // Windowed sinc with a tabulated kernel. When downsampling, the cutoff drops below the
  // output Nyquist so nothing folds back.
  constexpr int kZeroCrossings = 16;
  constexpr int kTableRes = 512;
  const double cutoff = 0.97 * std::min(1., ratio);
  static std::vector<float> table = [] {
    std::vector<float> t((size_t)(kZeroCrossings * kTableRes + 2));
    for (size_t i = 0; i < t.size(); i++)
    {
      const double x = (double)i / kTableRes; // in zero crossings
      const double sinc = x == 0. ? 1. : std::sin(kPi * x) / (kPi * x);
      const double w = x >= kZeroCrossings ? 0. : 0.42 + 0.5 * std::cos(kPi * x / kZeroCrossings) + 0.08 * std::cos(2. * kPi * x / kZeroCrossings);
      t[i] = (float)(sinc * w);
    }
    return t;
  }();

  const int64_t outFrames = (int64_t)std::floor((double)frames * ratio);
  std::vector<float> out((size_t)(2 * outFrames));
  const double halfWidth = kZeroCrossings / cutoff; // in input samples
  for (int64_t n = 0; n < outFrames; n++)
  {
    const double x = (double)n / ratio;
    const int64_t lo = std::max<int64_t>(0, (int64_t)std::ceil(x - halfWidth));
    const int64_t hi = std::min<int64_t>(frames - 1, (int64_t)std::floor(x + halfWidth));
    double l = 0., r = 0., norm = 0.;
    for (int64_t i = lo; i <= hi; i++)
    {
      const double d = std::fabs((double)i - x) * cutoff * kTableRes;
      const size_t k = (size_t)d;
      if (k + 1 >= table.size())
        continue;
      const double frac = d - (double)k;
      const double w = table[k] + (table[k + 1] - table[k]) * frac;
      l += w * in[2 * i];
      r += w * in[2 * i + 1];
      norm += w;
    }
    const double g = norm != 0. ? 1. / norm : 0.;
    out[(size_t)(2 * n)] = (float)(l * g);
    out[(size_t)(2 * n + 1)] = (float)(r * g);
  }
  return out;
}

uint64_t HashFrames(const float* stereo, int64_t frames)
{
  uint64_t h = 1469598103934665603ull;
  const uint8_t* p = (const uint8_t*)stereo;
  const size_t n = (size_t)frames * 2 * sizeof(float);
  for (size_t i = 0; i < n; i++)
  {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

} // namespace underheard
