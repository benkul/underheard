// Offline AU host test (macOS) for underheard-reverb: every engine makes a tail, Decay
// lengthens it, a recording restored from a project convolves exactly, Freeze holds, the
// templates load, and the settings survive save and reopen.
#include "TapeFiles.h"

#include <AudioToolbox/AudioToolbox.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

enum { kParamOutput = 0, kParamMix, kParamEngine, kParamPreSync, kParamPreDelay, kParamPreNote, kParamDecay, kParamBalance, kParamLowCut, kParamHighCut,
       kParamWidth, kParamModulation, kParamAge, kParamFreeze, kParamDuck, kParamRoom };
enum { kRooms = 0, kRecordings, kPlate, kHall };

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double SR = 48000.;
static const UInt32 BS = 512;
enum Source { kSilence, kClick, kSine, kNoise };
static Source gSource = kSilence;
static Float64 gClickAt = 0.;

static float Input(Float64 n)
{
  switch (gSource)
  {
    case kClick: return n == gClickAt ? 1.f : 0.f;
    case kSine: return 0.4f * (float)std::sin(2. * M_PI * 220. * n / SR) + 0.2f * (float)std::sin(2. * M_PI * 1375. * n / SR);
    case kNoise: // white noise, a hash of the sample time
    {
      uint32_t x = (uint32_t)(int64_t)n * 2654435761u;
      x ^= x >> 15;
      x *= 2246822519u;
      x ^= x >> 13;
      return (float)(0.6 * ((double)x / 4294967296. - 0.5));
    }
    default: return 0.f;
  }
}

static OSStatus InputCallback(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts, UInt32, UInt32 nFrames, AudioBufferList* io)
{
  for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
    for (UInt32 k = 0; k < nFrames; ++k)
      ((float*)io->mBuffers[b].mData)[k] = Input(ts->mSampleTime + k);
  return noErr;
}

struct Inst { AudioUnit au; Float64 sampleTime = 0; };

static void SetParam(Inst& i, int p, float v) { AudioUnitSetParameter(i.au, p, kAudioUnitScope_Global, 0, v, 0); }
static float GetParam(Inst& i, int p)
{
  AudioUnitParameterValue v = 0;
  AudioUnitGetParameter(i.au, p, kAudioUnitScope_Global, 0, &v);
  return v;
}

static Inst Make()
{
  AudioComponentDescription d = {kAudioUnitType_Effect, 'URvb', 'Undh', 0, 0};
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  if (!c) { printf("underheard-reverb component not found (build UnderheardReverb-au first)\n"); exit(2); }
  Inst i;
  AudioComponentInstanceNew(c, &i.au);
  AudioStreamBasicDescription f = {SR, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
  AudioUnitSetProperty(i.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
  AudioUnitSetProperty(i.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  UInt32 mf = BS;
  AudioUnitSetProperty(i.au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mf, sizeof mf);
  AURenderCallbackStruct cb = {InputCallback, nullptr};
  AudioUnitSetProperty(i.au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
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

static double Rms(const std::vector<float>& v, double from, double to)
{
  double e = 0.;
  const size_t a = (size_t)(from * SR), b = std::min(v.size(), (size_t)(to * SR));
  for (size_t k = a; k < b; k++)
    e += (double)v[k] * v[k];
  return std::sqrt(e / (double)std::max<size_t>(1, b - a));
}

// Lets OnIdle run (it builds the convolver after settings settle for 60 ms).
static void Idle(double secs = 0.3)
{
  const auto end = CFAbsoluteTimeGetCurrent() + secs;
  while (CFAbsoluteTimeGetCurrent() < end)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
}

// The reverb of a click (all wet, no pre-delay), from the click on.
static std::vector<float> ClickResponse(Inst& i, double secs)
{
  gSource = kSilence;
  Render(i, 0.2);
  gSource = kClick;
  gClickAt = i.sampleTime;
  auto out = Render(i, secs);
  gSource = kSilence;
  return out;
}

static CFPropertyListRef GetState(Inst& i)
{
  CFPropertyListRef s = nullptr;
  UInt32 sz = sizeof s;
  AudioUnitGetProperty(i.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s, &sz);
  return s;
}

// A project state with the given recording: Output 0 dB, Mix 100%, the Recordings engine.
static CFPropertyListRef StateWithRecording(CFPropertyListRef base, const underheard::SavedTape& t)
{
  std::vector<UInt8> d;
  auto put = [&](const void* p, size_t n) { d.insert(d.end(), (const UInt8*)p, (const UInt8*)p + n); };
  const int32_t magic = 'URVB', version = 1, count = 3, channels = 1;
  const double output = 0., mix = 100., engine = kRecordings, rate = 48000.;
  put(&magic, 4); put(&version, 4); put(&count, 4); put(&output, 8); put(&mix, 8); put(&engine, 8);
  put(&t.frames, 8); put(&t.hash, 8);
  const int32_t len = (int32_t)t.path.size();
  put(&len, 4); put(t.path.data(), t.path.size());
  put(&channels, 4); put(&rate, 8);
  CFMutableDictionaryRef dict = CFDictionaryCreateMutableCopy(nullptr, 0, (CFDictionaryRef)base);
  CFDataRef data = CFDataCreate(nullptr, d.data(), (CFIndex)d.size());
  CFDictionarySetValue(dict, CFSTR("data"), data);
  CFRelease(data);
  return dict;
}

// How well `out` matches the input `lag` samples earlier (normalised correlation), its level
// against that, and how much it correlates with the input now (the direct sound).
struct Match { double corr, levelDb, direct; };
static Match Compare(const std::vector<float>& out, Float64 endTime, double lag)
{
  double xy = 0., xx = 0., yy = 0., d = 0., dd = 0.;
  for (size_t k = 0; k < out.size(); k++)
  {
    const Float64 n = endTime - out.size() + k;
    const double x = Input(n - lag), y = out[k], now = Input(n);
    xy += x * y;
    xx += x * x;
    yy += y * y;
    d += now * y;
    dd += now * now;
  }
  return {xy / std::sqrt(xx * yy), 10. * std::log10(yy / xx), d / std::sqrt(dd * yy)};
}

static void Plain(Inst& i)
{
  SetParam(i, kParamMix, 100.f);
  SetParam(i, kParamPreDelay, 0.f);
  SetParam(i, kParamLowCut, 20.f);
  SetParam(i, kParamHighCut, 20000.f);
  SetParam(i, kParamAge, 0.f);
  SetParam(i, 27, 0.f); // Warmth off: the wet sound exactly as the engine makes it
}

int main()
{
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "underheard-reverb-test";
  std::filesystem::remove_all(dir);
  setenv("UNDERHEARD_TAPE_DIR", dir.c_str(), 1);

  printf("-- the engines\n");
  {
    Inst a = Make();
    gSource = kSine;
    Render(a, 0.5);
    auto out = Render(a, 0.5);
    double diff = 0.;
    for (size_t k = 0; k < out.size(); k++)
      diff = std::fmax(diff, std::fabs(out[k] - Input(a.sampleTime - out.size() + k)));
    CHECK(diff > 0.02, "a new reverb (a hall) adds to the sound (%.3f from dry)", diff);
    SetParam(a, kParamMix, 0.f);
    Render(a, 0.3);
    out = Render(a, 0.5);
    diff = 0.;
    for (size_t k = 0; k < out.size(); k++)
      diff = std::fmax(diff, std::fabs(out[k] - Input(a.sampleTime - out.size() + k)));
    CHECK(diff < 1e-5, "Mix at dry is the dry signal");
    Plain(a);
    const char* names[4] = {"Rooms", "Recordings (none loaded)", "Plate", "Hall"};
    for (int e : {kRooms, kRecordings, kPlate, kHall})
    {
      SetParam(a, kParamEngine, (float)e);
      Idle();
      Render(a, 0.5);
      const auto h = ClickResponse(a, 1.5);
      const double tail = Rms(h, 0.05, 0.5);
      CHECK(e == kRecordings ? tail < 1e-6 : tail > 1e-3, "%s: %s (tail %.2g)", names[e], e == kRecordings ? "silent" : "a tail", tail);
    }
    SetParam(a, kParamEngine, kHall);
    SetParam(a, kParamDecay, 0.5f);
    Render(a, 4.);
    const double shortTail = Rms(ClickResponse(a, 3.), 1.5, 2.5);
    SetParam(a, kParamDecay, 2.f);
    Render(a, 4.);
    const double longTail = Rms(ClickResponse(a, 3.), 1.5, 2.5);
    CHECK(longTail > shortTail * 10., "Decay x2 rings on much longer than x0.5 (%+.0f dB at 2 s)", 20. * std::log10(longTail / shortTail));
    SetParam(a, kParamEngine, kRooms);
    SetParam(a, kParamDecay, 0.5f);
    Idle();
    Render(a, 3.);
    const double shortRoom = Rms(ClickResponse(a, 2.), 0.3, 0.6);
    SetParam(a, kParamDecay, 3.f);
    Idle(1.5);
    Render(a, 1.);
    const double longRoom = Rms(ClickResponse(a, 2.), 0.3, 0.6);
    CHECK(longRoom > shortRoom * 3., "Rooms: Decay stretches the room too (%+.0f dB at 0.45 s)", 20. * std::log10(longRoom / shortRoom));

    printf("\n-- freeze\n");
    SetParam(a, kParamEngine, kHall);
    SetParam(a, kParamDecay, 1.f);
    gSource = kNoise;
    Render(a, 2.);
    SetParam(a, kParamFreeze, 1.f);
    Render(a, 0.5);
    gSource = kSilence;
    const auto held = Render(a, 4.);
    CHECK(Rms(held, 3., 4.) > 0.01 && std::fabs(20. * std::log10(Rms(held, 3., 4.) / Rms(held, 0.5, 1.5))) < 1., "Hall: frozen, it holds (%.3f)", Rms(held, 3., 4.));
    SetParam(a, kParamFreeze, 0.f);
    SetParam(a, kParamEngine, kRooms);
    Idle();
    gSource = kNoise;
    Render(a, 2.);
    SetParam(a, kParamFreeze, 1.f);
    Render(a, 0.5);
    gSource = kSilence;
    const auto heldRoom = Render(a, 4.);
    CHECK(Rms(heldRoom, 3., 4.) > 0.01 && std::fabs(20. * std::log10(Rms(heldRoom, 3., 4.) / Rms(heldRoom, 0.5, 1.5))) < 1., "Rooms: frozen, it holds (%.3f)",
          Rms(heldRoom, 3., 4.));
    SetParam(a, kParamFreeze, 0.f);
    AudioComponentInstanceDispose(a.au);
  }

  printf("\n-- a recording restored from a project\n");
  {
    // The recording: a direct click (taken out as the direct sound), then one reflection
    // 100 ms later. After shaping, the reverb is that reflection alone, at unit energy.
    std::vector<float> ir(2 * 9600, 0.f);
    ir[0] = ir[1] = 1.f;
    ir[2 * 4800] = ir[2 * 4800 + 1] = 0.5f;
    underheard::SavedTape t;
    std::string err;
    underheard::SaveTape(underheard::TapeDirectory("UnderheardReverb"), ir.data(), 9600, 48000., t, err);
    Inst a = Make();
    CFPropertyListRef base = GetState(a);
    CFPropertyListRef state = StateWithRecording(base, t);
    Inst b = Make();
    AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof state);
    Plain(b);
    Idle();
    CHECK(GetParam(b, kParamEngine) == kRecordings, "the project's engine is Recordings");
    gSource = kNoise; // not periodic, so 100 ms ago and now don't look alike
    Render(b, 0.5);
    const auto out = Render(b, 0.5);
    const Match m = Compare(out, b.sampleTime, 4800.);
    CHECK(m.corr > 0.99 && std::fabs(m.levelDb) < 0.5 && m.direct < 0.5,
          "it convolves with the recording: the sound 100 ms later (match %.4f, %+.2f dB), not the direct sound", m.corr, m.levelDb);

    // Save and reopen: the recording and settings come back.
    SetParam(b, kParamDecay, 1.7f);
    Idle();
    CFPropertyListRef saved = GetState(b);
    Inst c = Make();
    AudioUnitSetProperty(c.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &saved, sizeof saved);
    Idle();
    CHECK(GetParam(c, kParamEngine) == kRecordings && std::fabs(GetParam(c, kParamDecay) - 1.7f) < 0.01f, "settings come back");
    SetParam(c, kParamDecay, 1.f);
    Plain(c);
    Idle();
    Render(c, 0.5);
    const auto again = Render(c, 0.5);
    const Match m2 = Compare(again, c.sampleTime, 4800.);
    CHECK(m2.corr > 0.99 && std::fabs(m2.levelDb) < 0.5, "and the recording with them (match %.4f)", m2.corr);
    SetParam(c, 27, 50.f);
    Render(c, 0.3);
    const Match warm = Compare(Render(c, 0.5), c.sampleTime, 4800.);
    CHECK(warm.corr < 0.95 && warm.corr > 0.3 && warm.levelDb < -1., "Warmth colours the reverb: darker (%+.1f dB on white noise), still the same room (%.2f)",
          warm.levelDb, warm.corr);
    gSource = kSilence;
    CFRelease(base);
    CFRelease(state);
    CFRelease(saved);
    for (Inst* i : {&a, &b, &c})
      AudioComponentInstanceDispose(i->au);
  }

  printf("\n-- templates (the host's factory presets)\n");
  {
    Inst a = Make();
    CFArrayRef presets = nullptr;
    UInt32 sz = sizeof presets;
    AudioUnitGetProperty(a.au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &presets, &sz);
    const CFIndex n = presets ? CFArrayGetCount(presets) : 0;
    auto choose = [&](const char* name) {
      for (CFIndex k = 0; k < n; k++)
      {
        AUPreset p = *(const AUPreset*)CFArrayGetValueAtIndex(presets, k);
        char buf[64] = {};
        CFStringGetCString(p.presetName, buf, sizeof buf, kCFStringEncodingUTF8);
        if (std::string(buf) == name)
          return AudioUnitSetProperty(a.au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, sizeof p) == noErr;
      }
      return false;
    };
    CHECK(n == 8, "eight templates listed");
    CHECK(choose("Church hall") && GetParam(a, kParamEngine) == kRooms && GetParam(a, kParamRoom) == 8.f, "Church hall: the Rooms engine, the church hall");
    CHECK(choose("Bright plate") && GetParam(a, kParamEngine) == kPlate && GetParam(a, kParamHighCut) == 14000.f, "Bright plate: the plate, open top");
    CHECK(choose("Frozen pad") && GetParam(a, kParamDecay) == 4.f && GetParam(a, kParamPreSync) == 1.f, "Frozen pad: longest decay, synced pre-delay");
    Idle();
    gSource = kNoise;
    const auto out = Render(a, 2.);
    gSource = kSilence;
    bool finite = true;
    for (float x : out) finite = finite && std::isfinite(x);
    CHECK(finite && Rms(out, 1., 2.) > 0.01, "and it plays");
    if (presets) CFRelease(presets);
    AudioComponentInstanceDispose(a.au);
  }
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
