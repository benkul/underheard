// Offline AU host test (macOS) for Room Bleed: pass-through with no room, convolution with a
// room restored from a project, and the room surviving save and reopen.
#include "TapeFiles.h"

#include <AudioToolbox/AudioToolbox.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

enum { kParamOutput = 0, kParamMix, kParamRoom, kParamSize, kParamSurfaces, kParamDistance, kParamAim, kParamPattern, kParamStereo, kParamPair, kParamMic, kParamWhere, kParamDoor, kParamTone, kParamToneLevel, kParamSourceX, kParamSourceY, kParamBearing, kParamWarmth };
static const float kIdealMic = 14.f, kContactMic = 11.f, kBathroom = 1.f;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double SR = 48000.;
static const UInt32 BS = 512;
static float Signal(int ch, Float64 n) { return 0.5f * (float)std::sin(2. * M_PI * (ch ? 330. : 220.) * n / SR); }
// What the test room does to the summed-mono source: half now, half 100 ms later, times 1/sqrt(0.5).
static float Expected(Float64 n)
{
  auto mono = [](Float64 t) { return t < 0 ? 0.f : 0.5f * (Signal(0, t) + Signal(1, t)); };
  return (float)(std::sqrt(0.5) * (mono(n) + mono(n - 4800)));
}

static OSStatus InputCallback(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts, UInt32, UInt32 nFrames, AudioBufferList* io)
{
  for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
    for (UInt32 k = 0; k < nFrames; ++k)
      ((float*)io->mBuffers[b].mData)[k] = Signal((int)b, ts->mSampleTime + k);
  return noErr;
}

struct Inst { AudioUnit au; Float64 sampleTime = 0; };

static Inst Make()
{
  AudioComponentDescription d = {kAudioUnitType_Effect, 'RmBl', 'Undh', 0, 0};
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  if (!c) { printf("RoomBleed component not found (build RoomBleed-au first)\n"); exit(2); }
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

static void SetParam(Inst& i, int p, float v) { AudioUnitSetParameter(i.au, p, kAudioUnitScope_Global, 0, v, 0); }

// Renders `secs` and returns the largest error against `want(sampleTime)` on the left channel.
template <class F>
static float Render(Inst& i, double secs, F want)
{
  std::vector<float> L(BS), R(BS);
  float err = 0.f;
  for (int b = 0; b < (int)(secs * SR / BS); ++b)
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
    for (UInt32 k = 0; k < BS; ++k)
      err = std::fmax(err, std::fabs(L[k] - want(i.sampleTime + k)));
    i.sampleTime += BS;
  }
  return err;
}

static void Idle() { CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.15, false); }

static CFPropertyListRef GetState(Inst& i)
{
  CFPropertyListRef s = nullptr;
  UInt32 sz = sizeof s;
  AudioUnitGetProperty(i.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s, &sz);
  return s;
}

// A project state (format 1) with the given room file.
static CFPropertyListRef StateWithRoom(CFPropertyListRef base, const underheard::SavedTape& t)
{
  std::vector<UInt8> d;
  auto put = [&](const void* p, size_t n) { d.insert(d.end(), (const UInt8*)p, (const UInt8*)p + n); };
  const int32_t magic = 'RMBL', version = 1, count = 2, channels = 1;
  const double output = 0., mix = 100., rate = 48000.;
  put(&magic, 4); put(&version, 4); put(&count, 4); put(&output, 8); put(&mix, 8);
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

int main()
{
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "underheard-roombleed-test";
  std::filesystem::remove_all(dir);
  setenv("UNDERHEARD_TAPE_DIR", dir.c_str(), 1);

  printf("-- the default room\n");
  Inst a = Make();
  Idle();
  Render(a, 0.5, [](Float64) { return 0.f; });
  const float vsSilence = Render(a, 0.5, [](Float64) { return 0.f; });
  const float vsDry = Render(a, 0.5, [](Float64 n) { return Signal(0, n); });
  CHECK(vsSilence > 0.05f && vsDry > 0.05f, "a new Room Bleed puts the sound in a generated room (not silent, not dry)");
  SetParam(a, kParamMix, 0.f);
  Render(a, 0.3, [](Float64) { return 0.f; });
  CHECK(Render(a, 0.5, [](Float64 n) { return Signal(0, n); }) < 1e-4f, "Mix at dry is the dry signal");
  SetParam(a, kParamMix, 100.f);

  printf("\n-- a room restored from a project\n");
  // The room: half the sound now, half 100 ms later (already energy-normalised).
  std::vector<float> ir(2 * 9600, 0.f);
  ir[0] = ir[1] = (float)std::sqrt(0.5);
  ir[2 * 4800] = ir[2 * 4800 + 1] = (float)std::sqrt(0.5);
  underheard::SavedTape t;
  std::string err;
  underheard::SaveTape(underheard::TapeDirectory("RoomBleed"), ir.data(), 9600, 48000., t, err);
  CFPropertyListRef base = GetState(a);
  CFPropertyListRef withRoom = StateWithRoom(base, t);
  Inst b = Make();
  CHECK(AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &withRoom, sizeof withRoom) == noErr, "opens");
  float roomChoice = 0.f;
  AudioUnitGetParameter(b.au, kParamRoom, kAudioUnitScope_Global, 0, &roomChoice);
  CHECK(roomChoice == 9.f, "a project from before the Room choice opens on its recording");
  // At 1 m (the recording's own distance) the recording is used as it is. In the living room,
  // from the default source along the default bearing (53°), the Distance scale runs
  // 0.1 m .. 5.257 m logarithmically: 1 m is 58.10%.
  SetParam(b, kParamDistance, 58.10f);
  SetParam(b, kParamMic, kIdealMic); // a flat, noiseless mic leaves the recording exactly as it is
  SetParam(b, kParamTone, 0.f);      // and no room tone
  SetParam(b, kParamWarmth, 0.f);    // and no added warmth
  Idle();
  Render(b, 0.5, [](Float64) { return 0.f; }); // the switch crossfades
  const float e = Render(b, 1.0, [](Float64 n) { return Expected(n); });
  CHECK(e < 1e-3f, "the output is the source through the room: now plus 100 ms later (max error %.2g)", e);

  printf("\n-- save and reopen\n");
  CFPropertyListRef saved = GetState(b);
  Inst c = Make();
  AudioUnitSetProperty(c.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &saved, sizeof saved);
  Idle();
  Render(c, 0.5, [](Float64) { return 0.f; });
  CHECK(Render(c, 1.0, [](Float64 n) { return Expected(n); }) < 1e-3f, "the recording comes back with the project, with its settings");
  SetParam(c, kParamWarmth, 50.f);
  Render(c, 0.3, [](Float64) { return 0.f; });
  const float warm = Render(c, 1.0, [](Float64 n) { return Expected(n); });
  CHECK(warm > 0.01f && warm < 0.3f, "Warmth colours the bled sound (%.3f from the exact room)", warm);

  printf("\n-- mics\n");
  {
    // In a live bathroom most of what an omni hears is the room. A contact pickup hears the
    // surface it's on, not the air, so its output is far quieter.
    auto level = [&](float mic) {
      Inst m = Make();
      SetParam(m, kParamRoom, kBathroom);
      SetParam(m, kParamMic, mic);
      SetParam(m, kParamPattern, 0.f);
      Idle();
      Render(m, 1.0, [](Float64) { return 0.f; });
      std::vector<float> L(BS), R(BS);
      double total = 0.;
      for (int b = 0; b < (int)(1.0 * SR / BS); ++b)
      {
        struct { AudioBufferList l; AudioBuffer b2; } abl;
        abl.l.mNumberBuffers = 2;
        abl.l.mBuffers[0] = {1, BS * 4, L.data()};
        abl.l.mBuffers[1] = {1, BS * 4, R.data()};
        AudioTimeStamp ts = {};
        ts.mSampleTime = m.sampleTime;
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags fl = 0;
        AudioUnitRender(m.au, &fl, &ts, 0, BS, &abl.l);
        m.sampleTime += BS;
        for (float v : L) total += (double)v * v;
      }
      AudioComponentInstanceDispose(m.au);
      return 10. * std::log10(total);
    };
    const double ideal = level(kIdealMic), contact = level(kContactMic);
    CHECK(ideal - contact > 6., "in the bathroom, a contact pickup hears much less of the room than an ideal omni (%.1f dB less)", ideal - contact);
  }

  printf("\n-- room tone\n");
  {
    auto level = [&](float tone, float db) {
      Inst m = Make();
      SetParam(m, kParamTone, tone);
      SetParam(m, kParamToneLevel, db);
      Idle();
      Render(m, 0.5, [](Float64) { return 0.f; });
      std::vector<float> L(BS), R(BS);
      double total = 0.;
      for (int b = 0; b < (int)(2.0 * SR / BS); ++b)
      {
        struct { AudioBufferList l; AudioBuffer b2; } abl;
        abl.l.mNumberBuffers = 2;
        abl.l.mBuffers[0] = {1, BS * 4, L.data()};
        abl.l.mBuffers[1] = {1, BS * 4, R.data()};
        AudioTimeStamp ts = {};
        ts.mSampleTime = m.sampleTime;
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags fl = 0;
        AudioUnitRender(m.au, &fl, &ts, 0, BS, &abl.l);
        m.sampleTime += BS;
        for (float v : L) total += (double)v * v;
      }
      AudioComponentInstanceDispose(m.au);
      return 10. * std::log10(total);
    };
    const double none = level(0.f, -20.f), office = level(3.f, -20.f);
    CHECK(office - none > 0.5, "an office's room tone turned up is heard under the sound (%.1f dB more)", office - none);
  }

  printf("\n-- next door\n");
  {
    auto level = [&](float where, float door) {
      Inst m = Make();
      SetParam(m, kParamMic, kIdealMic);
      SetParam(m, kParamPattern, 0.f);
      SetParam(m, kParamWhere, where);
      SetParam(m, kParamDoor, door);
      Idle();
      Render(m, 1.0, [](Float64) { return 0.f; });
      std::vector<float> L(BS), R(BS);
      double total = 0.;
      for (int b = 0; b < (int)(1.0 * SR / BS); ++b)
      {
        struct { AudioBufferList l; AudioBuffer b2; } abl;
        abl.l.mNumberBuffers = 2;
        abl.l.mBuffers[0] = {1, BS * 4, L.data()};
        abl.l.mBuffers[1] = {1, BS * 4, R.data()};
        AudioTimeStamp ts = {};
        ts.mSampleTime = m.sampleTime;
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags fl = 0;
        AudioUnitRender(m.au, &fl, &ts, 0, BS, &abl.l);
        m.sampleTime += BS;
        for (float v : L) total += (double)v * v;
      }
      AudioComponentInstanceDispose(m.au);
      return 10. * std::log10(total);
    };
    const double same = level(0.f, 0.f), shut = level(1.f, 0.f), open = level(1.f, 100.f);
    CHECK(same - shut > 6., "next door with the door shut is much quieter (%.1f dB)", same - shut);
    CHECK(open - shut > 3., "opening the door lets it back in (%.1f dB louder)", open - shut);
  }

  CFRelease(base);
  CFRelease(withRoom);
  CFRelease(saved);
  AudioComponentInstanceDispose(a.au);
  AudioComponentInstanceDispose(b.au);
  AudioComponentInstanceDispose(c.au);
  std::filesystem::remove_all(dir);
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
