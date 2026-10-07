// Offline AU host test (macOS) for Section, played over MIDI: pitch, release and sustain, pure
// tuning, the room, a wavetable loaded from a file (restored by path or by its stored copy), the
// controllers, the output limiter, the template, save and reopen, old projects, and CPU.
#include <AudioToolbox/AudioToolbox.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

enum
{
  kParamOutput = 0, kParamPlayers,
  kParamAPosition, kParamAOctave, kParamASemi, kParamAFine, kParamALevel,
  kParamBPosition, kParamBOctave, kParamBSemi, kParamBFine, kParamBLevel,
  kParamFilterType, kParamCutoff, kParamResonance, kParamKeyTrack, kParamFilterEnvAmount, kParamFilterVelocity,
  kParamFAttack, kParamFDecay, kParamFSustain, kParamFRelease,
  kParamAAttack, kParamADecay, kParamASustain, kParamARelease, kParamAmpVelocity,
  kParamWheelTarget, kParamWheelAmount, kParamTouchTarget, kParamTouchAmount,
  kParamLooseness, kParamSmear, kParamSettle, kParamVibrato, kParamOnset,
  kParamCharPitch, kParamCharPosition, kParamCharCutoff, kParamDriftPitch, kParamDriftPosition, kParamDriftCutoff,
  kParamNoiseAmount, kParamNoiseTone, kParamBody, kParamBodyDepth, kParamWarmth,
  kParamRoomAmount, kParamRoom, kParamSize, kParamSurfaces, kParamDistance, kParamMic, kParamPair, kParamWidth,
};

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double SR = 48000.;
static const UInt32 BS = 512;

struct Inst { AudioUnit au; Float64 sampleTime = 0; };

static void SetParam(Inst& i, int p, float v) { AudioUnitSetParameter(i.au, p, kAudioUnitScope_Global, 0, v, 0); }
static float GetParam(Inst& i, int p)
{
  AudioUnitParameterValue v = 0;
  AudioUnitGetParameter(i.au, p, kAudioUnitScope_Global, 0, &v);
  return v;
}
static void Midi(Inst& i, UInt32 status, UInt32 a, UInt32 b) { MusicDeviceMIDIEvent(i.au, status, a, b, 0); }

static Inst Make()
{
  AudioComponentDescription d = {kAudioUnitType_MusicDevice, 'Sctn', 'Undh', 0, 0};
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  if (!c) { printf("Section component not found (build Section-au first)\n"); exit(2); }
  Inst i;
  AudioComponentInstanceNew(c, &i.au);
  AudioStreamBasicDescription f = {SR, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
  AudioUnitSetProperty(i.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  UInt32 mf = BS;
  AudioUnitSetProperty(i.au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mf, sizeof mf);
  if (AudioUnitInitialize(i.au)) { printf("init failed\n"); exit(2); }
  return i;
}

static std::vector<float> Render(Inst& i, double secs)
{
  std::vector<float> L(BS), R(BS), out;
  for (int b = 0; b < (int)std::lround(secs * SR / BS); ++b)
  {
    struct { AudioBufferList l; AudioBuffer b2; } abl;
    abl.l.mNumberBuffers = 2;
    abl.l.mBuffers[0] = {1, BS * 4, L.data()};
    abl.l.mBuffers[1] = {1, BS * 4, R.data()};
    AudioTimeStamp ts = {};
    ts.mSampleTime = i.sampleTime;
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    AudioUnitRenderActionFlags fl = 0;
    if (AudioUnitRender(i.au, &fl, &ts, 0, BS, &abl.l)) { printf("render error\n"); exit(2); }
    out.insert(out.end(), L.begin(), L.end());
    i.sampleTime += BS;
  }
  return out;
}

static double Rms(const std::vector<float>& v, double from = 0., double to = 1e9)
{
  double e = 0.;
  const size_t a = (size_t)(from * SR), b = std::min(v.size(), (size_t)(to * SR));
  for (size_t k = a; k < b; k++)
    e += (double)v[k] * v[k];
  return b > a ? std::sqrt(e / (double)(b - a)) : 0.;
}
static double Level(const std::vector<float>& v, double hz)
{
  const double w = 2. * M_PI * hz / SR, k = 2. * std::cos(w);
  double s1 = 0., s2 = 0., wsum = 0.;
  const size_t n = v.size();
  for (size_t i = 0; i < n; i++)
  {
    const double win = 0.5 - 0.5 * std::cos(2. * M_PI * (double)i / (double)(n - 1));
    wsum += win;
    const double s = v[i] * win + k * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return std::sqrt(std::fmax(0., s1 * s1 + s2 * s2 - k * s1 * s2)) * 2. / wsum;
}
static double PeakHz(const std::vector<float>& v, double lo, double hi)
{
  double best = lo, bestMag = -1.;
  for (double hz = lo; hz <= hi; hz += 0.05)
  {
    const double m = Level(v, hz);
    if (m > bestMag) { bestMag = m; best = hz; }
  }
  return best;
}
static double Db(double x) { return 20. * std::log10(std::fmax(x, 1e-15)); }

static void Idle(double secs = 0.3)
{
  const auto end = CFAbsoluteTimeGetCurrent() + secs;
  while (CFAbsoluteTimeGetCurrent() < end)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
}

// One player, no randomness, vibrato or tuning glide; the filter open; close and dry, no warmth,
// noise or body.
static void Plain(Inst& i)
{
  SetParam(i, kParamPlayers, 1.f);
  for (int p : {kParamLooseness, kParamSmear, kParamSettle, kParamVibrato, kParamCharPitch, kParamCharPosition, kParamCharCutoff, kParamDriftPitch,
                kParamDriftPosition, kParamDriftCutoff, kParamNoiseAmount, kParamRoomAmount, kParamWarmth, kParamKeyTrack, kParamFilterEnvAmount,
                kParamFilterVelocity, kParamAmpVelocity, kParamResonance})
    SetParam(i, p, 0.f);
  SetParam(i, kParamBody, 5.f); // Off
  SetParam(i, kParamCutoff, 20000.f);
}

static CFPropertyListRef GetState(Inst& i)
{
  CFPropertyListRef s = nullptr;
  UInt32 sz = sizeof s;
  AudioUnitGetProperty(i.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s, &sz);
  return s;
}
static void SetState(Inst& i, CFPropertyListRef s) { AudioUnitSetProperty(i.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s, sizeof s); }

// A project state: version `version`, only Output saved (everything else default), then both
// oscillators' tables (A as given, B the factory saw).
static CFPropertyListRef StateWith(CFPropertyListRef base, int version, int factoryA, const std::string& pathA, uint64_t hashA, const std::string& nameA)
{
  std::vector<UInt8> d;
  auto put = [&](const void* p, size_t n) { d.insert(d.end(), (const UInt8*)p, (const UInt8*)p + n); };
  auto putStr = [&](const std::string& s) { const int32_t len = (int32_t)s.size(); put(&len, 4); put(s.data(), s.size()); };
  const int32_t magic = 'SCTN', count = 1;
  const double output = 0.;
  put(&magic, 4); put(&version, 4); put(&count, 4); put(&output, 8);
  if (version >= 2)
  {
    const int32_t fa = factoryA, fb = 2;
    const uint64_t hb = 0;
    put(&fa, 4); putStr(pathA); put(&hashA, 8); putStr(nameA);
    put(&fb, 4); putStr(""); put(&hb, 8); putStr("Saw");
  }
  CFMutableDictionaryRef dict = CFDictionaryCreateMutableCopy(nullptr, 0, (CFDictionaryRef)base);
  CFDataRef data = CFDataCreate(nullptr, d.data(), (CFIndex)d.size());
  CFDictionarySetValue(dict, CFSTR("data"), data);
  CFRelease(data);
  return dict;
}

// A single-cycle sine as a 16-bit WAV file (a wavetable of one frame); returns its bytes.
static std::vector<uint8_t> SineWav()
{
  std::vector<uint8_t> b;
  auto p16 = [&](uint16_t v) { b.push_back(v & 255); b.push_back(v >> 8); };
  auto p32 = [&](uint32_t v) { for (int k = 0; k < 4; k++) b.push_back((v >> (8 * k)) & 255); };
  auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
  const int n = 2048;
  tag("RIFF"); p32(36 + 2 * n); tag("WAVE"); tag("fmt "); p32(16); p16(1); p16(1); p32(48000); p32(96000); p16(2); p16(16);
  tag("data"); p32(2 * n);
  for (int i = 0; i < n; i++)
    p16((uint16_t)(int16_t)std::lround(30000. * std::sin(2. * M_PI * i / n)));
  return b;
}
static uint64_t Fnv1a(const std::vector<uint8_t>& bytes)
{
  uint64_t h = 1469598103934665603ull;
  for (uint8_t c : bytes) { h ^= c; h *= 1099511628211ull; }
  return h;
}
static void Write(const std::filesystem::path& p, const std::vector<uint8_t>& b)
{
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f.write((const char*)b.data(), (std::streamsize)b.size());
}

// How pure A4 comes out: the 2nd and 3rd harmonics against the fundamental (dB).
static double Impurity(Inst& i)
{
  Plain(i);
  Render(i, 0.2);
  Midi(i, 0x90, 69, 100);
  Render(i, 0.5);
  const auto out = Render(i, 1.);
  Midi(i, 0x80, 69, 0);
  Render(i, 1.);
  return Db(std::fmax(Level(out, 880.), Level(out, 1320.)) / Level(out, 440.));
}

int main()
{
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "underheard-section-test";
  std::filesystem::remove_all(dir);
  setenv("UNDERHEARD_TAPE_DIR", dir.c_str(), 1);

  printf("-- playing\n");
  {
    Inst a = Make();
    Plain(a);
    CHECK(Rms(Render(a, 0.3)) == 0., "silent until a note comes");
    Midi(a, 0x90, 69, 100);
    const auto out = Render(a, 2.);
    std::vector<float> tail(out.begin() + (long)SR, out.end());
    CHECK(std::fabs(PeakHz(tail, 435., 445.) - 440.) < 0.2, "A4 at 440 Hz on the default bowed-string table (%.2f)", PeakHz(tail, 435., 445.));
    Midi(a, 0x80, 69, 0);
    Render(a, 3.);
    CHECK(Rms(Render(a, 0.5)) < 1e-4, "and quiet after it's released");
    Midi(a, 0xB0, 64, 127);
    Midi(a, 0x90, 69, 100);
    Render(a, 0.3);
    Midi(a, 0x80, 69, 0);
    Render(a, 1.5);
    const double held = Rms(Render(a, 0.3));
    Midi(a, 0xB0, 64, 0);
    Render(a, 3.);
    CHECK(held > 0.01 && Rms(Render(a, 0.3)) < 1e-4, "the sustain pedal holds a note, and lifting it lets go");

    // The mod wheel (default: cutoff, +50%) opens a closed filter.
    SetParam(a, kParamCutoff, 300.f);
    SetParam(a, kParamWheelAmount, 100.f);
    Midi(a, 0x90, 57, 100);
    Render(a, 0.5);
    const auto closed = Render(a, 0.5);
    Midi(a, 0xB0, 1, 127);
    Render(a, 0.3);
    const auto open = Render(a, 0.5);
    Midi(a, 0x80, 57, 0);
    CHECK(Db(Level(open, 2200.) / Level(closed, 2200.)) > 20., "the mod wheel opens the filter (10th harmonic %+.0f dB)", Db(Level(open, 2200.) / Level(closed, 2200.)));
    AudioComponentInstanceDispose(a.au);
  }

  printf("\n-- settling into pure tuning\n");
  auto heldE = [](float settle) {
    Inst a = Make();
    Plain(a);
    SetParam(a, kParamSettle, settle);
    for (int n : {60, 64, 67})
      Midi(a, 0x90, n, 100);
    Render(a, 3.);
    const auto out = Render(a, 2.);
    AudioComponentInstanceDispose(a.au);
    return PeakHz(out, 324., 333.);
  };
  const double et = heldE(0.f), pure = heldE(100.f);
  CHECK(std::fabs(et - 329.63) < 0.15 && std::fabs(pure - 327.03) < 0.15, "a held C major chord: its E moves from %.2f Hz (equal) to %.2f Hz (pure)", et, pure);

  printf("\n-- the room\n");
  {
    auto tailAfter = [](float room) {
      Inst a = Make();
      Plain(a);
      SetParam(a, kParamRoomAmount, room);
      Idle();
      Render(a, 0.2);
      Midi(a, 0x90, 55, 110);
      Render(a, 1.5);
      Midi(a, 0x80, 55, 0);
      const auto out = Render(a, 2.5);
      AudioComponentInstanceDispose(a.au);
      return Rms(out, 1.6, 2.2);
    };
    const double dry = tailAfter(0.f), room = tailAfter(100.f);
    CHECK(room > 1e-3 && room > dry * 10., "through the room (a church hall), the section rings on after release (%.4f against %.5f close)", room, dry);
  }

  printf("\n-- wavetables from files\n");
  {
    const auto bytes = SineWav();
    const uint64_t hash = Fnv1a(bytes);
    const std::filesystem::path original = dir / "elsewhere" / "My sine.wav";
    Write(original, bytes);
    Inst a = Make();
    CFPropertyListRef base = GetState(a);
    const double bowed = Impurity(a);
    CFPropertyListRef byPath = StateWith(base, 2, -1, original.string(), hash, "My sine");
    Inst b = Make();
    SetState(b, byPath);
    const double sine = Impurity(b);
    CHECK(bowed > -30. && sine < -60., "a project with a table loaded from a file plays it: a sine table is pure (%.0f dB, against %.0f on the bowed string)", sine, bowed);

    // Saved and reopened, the table comes back.
    CFPropertyListRef saved = GetState(b);
    Inst c = Make();
    SetState(c, saved);
    CHECK(Impurity(c) < -60., "saved and reopened: the same table");

    // The original gone, but its copy in the tables folder (by hash): still found.
    std::filesystem::remove(original);
    char copyName[32];
    std::snprintf(copyName, sizeof copyName, "%016llx.wav", (unsigned long long)hash);
    Write(dir / "Section" / copyName, bytes);
    Inst e = Make();
    SetState(e, byPath);
    CHECK(Impurity(e) < -60., "the original moved: found again by its copy in the tables folder");

    // Neither there: the oscillator keeps its table (and says so on the panel).
    std::filesystem::remove(dir / "Section" / copyName);
    Inst f = Make();
    SetState(f, byPath);
    CHECK(Impurity(f) > -30., "the table nowhere to be found: the oscillator stays on the bowed string");

    // A horse table (the first one after Section's seven generated tables) plays, at pitch.
    CFPropertyListRef horse = StateWith(base, 2, 7, "", 0, "Horse: Whinny");
    Inst h = Make();
    SetState(h, horse);
    Plain(h);
    Midi(h, 0x90, 69, 100);
    Render(h, 0.5);
    const auto whinny = Render(h, 1.);
    Midi(h, 0x80, 69, 0);
    CFPropertyListRef horseSaved = GetState(h);
    Inst h2 = Make();
    SetState(h2, horseSaved);
    Plain(h2);
    Midi(h2, 0x90, 69, 100);
    Render(h2, 0.5);
    const auto again = Render(h2, 1.);
    double diff = 0.;
    for (size_t k = 0; k < again.size(); k++)
      diff = std::fmax(diff, std::fabs(again[k] - whinny[k]));
    CHECK(Rms(whinny) > 0.01 && std::fabs(PeakHz(whinny, 430., 450.) - 440.) < 0.5 && Impurity(b) < -60. && diff < 1e-4,
          "a horse table (Whinny) plays at pitch, and comes back the same after saving");
    CFRelease(horse);
    CFRelease(horseSaved);
    AudioComponentInstanceDispose(h.au);
    AudioComponentInstanceDispose(h2.au);

    // A project from before the redesign opens with the defaults.
    SetParam(f, kParamPlayers, 2.f);
    CFPropertyListRef old = StateWith(base, 1, 0, "", 0, "");
    SetState(f, old);
    Inst g = Make();
    SetState(g, old);
    CHECK(GetParam(g, kParamPlayers) == 4.f && GetParam(g, kParamCutoff) == 3000.f, "a project from before the redesign opens with the defaults");
    for (CFPropertyListRef r : {base, byPath, saved, old})
      CFRelease(r);
    for (Inst* i : {&a, &b, &c, &e, &f, &g})
      AudioComponentInstanceDispose(i->au);
  }

  printf("\n-- template, limiter, CPU\n");
  {
    Inst a = Make();
    CFArrayRef presets = nullptr;
    UInt32 sz = sizeof presets;
    AudioUnitGetProperty(a.au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &presets, &sz);
    const CFIndex n = presets ? CFArrayGetCount(presets) : 0;
    char name[64] = {};
    if (n)
      CFStringGetCString(((const AUPreset*)CFArrayGetValueAtIndex(presets, 0))->presetName, name, sizeof name, kCFStringEncodingUTF8);
    SetParam(a, kParamPlayers, 2.f);
    if (n)
    {
      AUPreset p = *(const AUPreset*)CFArrayGetValueAtIndex(presets, 0);
      AudioUnitSetProperty(a.au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, sizeof p);
    }
    CHECK(n == 1 && std::string(name) == "String section" && GetParam(a, kParamPlayers) == 4.f, "one template, the default string section, and choosing it restores it");
    if (presets) CFRelease(presets);

    // Everything as loud as it goes: the limiter keeps it under full scale.
    SetParam(a, kParamPlayers, 6.f);
    SetParam(a, kParamOutput, 12.f);
    SetParam(a, kParamResonance, 100.f);
    SetParam(a, kParamCutoff, 1500.f);
    SetParam(a, kParamBLevel, 100.f);
    SetParam(a, kParamAmpVelocity, 0.f);
    Idle();
    for (int k = 0; k < 8; k++)
      Midi(a, 0x90, 40 + 5 * k, 127);
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = Render(a, 10.);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double peak = 0.;
    bool finite = true;
    for (float x : out)
    {
      finite = finite && std::isfinite(x);
      peak = std::fmax(peak, std::fabs(x));
    }
    CHECK(finite && peak <= 1.0 && peak > 0.8, "8 notes x 6 players, full resonance, +12 dB: limited under full scale (peak %.3f)", peak);
    CHECK(secs < 3.5, "and in the room, both oscillators: 10 s rendered in %.2f s (%.0f%% of one core)", secs, secs * 10.);
    AudioComponentInstanceDispose(a.au);
  }
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
