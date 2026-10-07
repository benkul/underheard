// From horsi-vst3 (HorsiWave/dsp/WavetableData.cpp), unchanged but for the namespace.
#include "WavetableData.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>

namespace underheard::wavetable
{
namespace
{
using cplx = std::complex<double>;
constexpr double kPi = 3.14159265358979323846;

bool IsPow2(int n) { return n > 0 && (n & (n - 1)) == 0; }

/** In-place iterative radix-2 FFT. inverse = true computes the unscaled inverse. */
void Fft(std::vector<cplx>& a, bool inverse)
{
  const int n = (int) a.size();
  for (int i = 1, j = 0; i < n; i++)
  {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(a[i], a[j]);
  }
  for (int len = 2; len <= n; len <<= 1)
  {
    const double ang = 2. * kPi / len * (inverse ? 1. : -1.);
    const cplx wl(std::cos(ang), std::sin(ang));
    for (int i = 0; i < n; i += len)
    {
      cplx w(1.);
      for (int k = 0; k < len / 2; k++)
      {
        const cplx u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

/** Harmonics 0..maxHarmonic of one periodic cycle of length n (any n). */
std::vector<cplx> Spectrum(const float* x, int n, int maxHarmonic)
{
  std::vector<cplx> out(maxHarmonic + 1);
  if (IsPow2(n))
  {
    std::vector<cplx> a(x, x + n);
    Fft(a, false);
    for (int k = 0; k <= maxHarmonic && k < n; k++)
      out[k] = a[k];
    return out;
  }
  // Odd lengths only come from single-cycle files, so a plain DFT is fine.
  for (int k = 0; k <= maxHarmonic; k++)
  {
    cplx s = 0.;
    for (int i = 0; i < n; i++)
      s += (double) x[i] * std::polar(1., -2. * kPi * k * i / n);
    out[k] = s;
  }
  return out;
}

/** One cycle of `length` samples from harmonics 0..harmonics of a spectrum
 *  taken over `sourceLength` samples (the scale keeps the amplitude). */
void Synthesize(const std::vector<cplx>& spec, int harmonics, int sourceLength, float* out, int length)
{
  std::vector<cplx> a(length, 0.);
  const double scale = (double) length / sourceLength;
  const int h = std::min({harmonics, (int) spec.size() - 1, length / 2 - 1}); // skip the Nyquist bin
  a[0] = spec[0] * scale;
  for (int k = 1; k <= h; k++)
  {
    a[k] = spec[k] * scale;
    a[length - k] = std::conj(spec[k]) * scale;
  }
  Fft(a, true);
  for (int i = 0; i < length; i++)
    out[i] = (float) (a[i].real() / length);
}

uint16_t Le16(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
} // namespace

std::unique_ptr<WavetableData> WavetableData::FromFrames(const float* frames, int numFrames, std::string name)
{
  auto t = std::unique_ptr<WavetableData>(new WavetableData());
  t->mNumFrames = std::clamp(numFrames, 1, kMaxFrames);
  t->mName = std::move(name);
  t->mRaw.assign((size_t) (t->mNumFrames + 1) * kFrameSize, 0.f); // + the zero guard frame
  std::memcpy(t->mRaw.data(), frames, (size_t) t->mNumFrames * kFrameSize * sizeof(float));

  for (int level = 1; level < kNumLevels; level++)
  {
    Level& l = t->mLevels[level];
    // At least 4x oversampled, so linear interpolation adds little of its own
    l.length = std::max(256, 4 * ((kFrameSize / 2) >> level));
    l.data.assign((size_t) t->mNumFrames * (l.length + 1), 0.f);
  }
  for (int f = 0; f < t->mNumFrames; f++)
  {
    const std::vector<cplx> spec = Spectrum(t->RawFrame(f), kFrameSize, kFrameSize / 2);
    for (int level = 1; level < kNumLevels; level++)
    {
      Level& l = t->mLevels[level];
      float* dst = l.data.data() + (size_t) f * (l.length + 1);
      Synthesize(spec, (kFrameSize / 2) >> level, kFrameSize, dst, l.length);
      dst[l.length] = dst[0]; // guard sample
    }
  }
  return t;
}

size_t WavetableData::MemoryBytes() const
{
  size_t n = mRaw.size();
  for (const Level& l : mLevels)
    n += l.data.size();
  return n * sizeof(float);
}

DecodedWavetable DecodeWavetable(const void* bytes, size_t numBytes, const std::string& name)
{
  DecodedWavetable out;
  const uint8_t* p = static_cast<const uint8_t*>(bytes);
  const uint8_t* end = p + numBytes;
  if (!p || numBytes < 12 || std::memcmp(p, "RIFF", 4) || std::memcmp(p + 8, "WAVE", 4))
  {
    out.error = "Not a WAV file";
    return out;
  }

  uint16_t format = 0, channels = 0, bits = 0, blockAlign = 0;
  const uint8_t* data = nullptr;
  uint32_t dataSize = 0;
  int clmCycle = 0;
  for (const uint8_t* c = p + 12; c + 8 <= end;)
  {
    const uint32_t size = Le32(c + 4);
    const uint8_t* body = c + 8;
    if (size > (uint32_t) (end - body))
      break;
    if (!std::memcmp(c, "fmt ", 4) && size >= 16)
    {
      format = Le16(body);
      channels = Le16(body + 2);
      blockAlign = Le16(body + 12);
      bits = Le16(body + 14);
      if (format == 0xFFFE && size >= 26)
        format = Le16(body + 24); // WAVE_FORMAT_EXTENSIBLE: the sub-format GUID starts with the format code
    }
    else if (!std::memcmp(c, "data", 4))
    {
      data = body;
      dataSize = size;
    }
    else if (!std::memcmp(c, "clm ", 4) && size >= 4 && !std::memcmp(body, "<!>", 3))
    {
      clmCycle = std::atoi(std::string((const char*) body + 3, std::min<uint32_t>(size - 3, 8)).c_str());
    }
    c = body + size + (size & 1);
  }

  const bool isFloat = format == 3 && (bits == 32 || bits == 64);
  const bool isPcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
  if (!data || channels == 0 || blockAlign == 0 || (!isFloat && !isPcm))
  {
    out.error = "Unsupported WAV format (needs PCM or float)";
    return out;
  }

  // Read and mix down to mono
  const int bytesPerSample = bits / 8;
  const size_t total = dataSize / blockAlign;
  std::vector<float> mono(total);
  for (size_t i = 0; i < total; i++)
  {
    double sum = 0.;
    for (int ch = 0; ch < channels; ch++)
    {
      const uint8_t* s = data + i * blockAlign + ch * bytesPerSample;
      double v = 0.;
      if (isFloat && bits == 32) { float f; std::memcpy(&f, s, 4); v = f; }
      else if (isFloat) { double d; std::memcpy(&d, s, 8); v = d; }
      else if (bits == 8) v = (s[0] - 128) / 128.;
      else if (bits == 16) v = (int16_t) Le16(s) / 32768.;
      else if (bits == 24) v = (int32_t) ((uint32_t) (s[0] << 8 | s[1] << 16 | s[2] << 24)) / 2147483648.;
      else v = (int32_t) Le32(s) / 2147483648.;
      sum += v;
    }
    mono[i] = (float) (sum / channels);
  }

  // Cycle length
  int cycle = clmCycle;
  if (cycle <= 0)
  {
    if (total == 64 * 256)
      cycle = 256; // WaveEdit's fixed 64 x 256 (Serum files always carry "clm ")
    else if (total >= (size_t) WavetableData::kFrameSize && total % WavetableData::kFrameSize == 0)
      cycle = WavetableData::kFrameSize;
    else if (total >= 16 && total <= 8192)
      cycle = (int) total; // one single-cycle waveform
    else if (total % 256 == 0)
      cycle = 256;         // other 256-sample-cycle tables
  }
  if (cycle < 16 || (size_t) cycle > total)
  {
    out.error = "Can't tell the cycle length (no Serum metadata)";
    return out;
  }
  const int sourceFrames = (int) (total / cycle);
  out.sourceCycle = cycle;
  out.sourceFrames = sourceFrames;

  // Thin long tables evenly to the frame limit, and bring every frame to 2048.
  const int frames = std::min(sourceFrames, WavetableData::kMaxFrames);
  std::vector<float> resampled((size_t) frames * WavetableData::kFrameSize);
  for (int f = 0; f < frames; f++)
  {
    const int src = frames == sourceFrames ? f : (int) std::lround((double) f * (sourceFrames - 1) / (frames - 1));
    const float* in = mono.data() + (size_t) src * cycle;
    float* dst = resampled.data() + (size_t) f * WavetableData::kFrameSize;
    if (cycle == WavetableData::kFrameSize)
      std::memcpy(dst, in, WavetableData::kFrameSize * sizeof(float));
    else
    {
      const int harmonics = std::min(cycle / 2, WavetableData::kFrameSize / 2);
      Synthesize(Spectrum(in, cycle, harmonics), harmonics, cycle, dst, WavetableData::kFrameSize);
    }
  }
  out.table = WavetableData::FromFrames(resampled.data(), frames, name);
  return out;
}

} // namespace underheard::wavetable
