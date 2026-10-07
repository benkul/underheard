// Offline AU host test (macOS) for underheard-chorus: dry and wet paths, the LFO locked to the
// song position when synced, the templates, and the settings surviving save and reopen.
#include <AudioToolbox/AudioToolbox.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

enum { kParamOutput = 0, kParamMix, kParamMode, kParamVoices, kParamSync, kParamRate, kParamNote, kParamDepth, kParamDelay, kParamShape,
       kParamFeedback, kParamPhase, kParamSpread, kParamTone, kParamAge, kParamWarmth };

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double SR = 48000.;
static const UInt32 BS = 512;
static const double kTempo = 120.;
static bool gPlaying = true;
static Float64 gNow = 0.; // the sample time being rendered (the song position)

static float Signal(Float64 n) { return 0.4f * (float)std::sin(2. * M_PI * 220. * n / SR) + 0.2f * (float)std::sin(2. * M_PI * 1375. * n / SR); }

static OSStatus InputCallback(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts, UInt32, UInt32 nFrames, AudioBufferList* io)
{
  for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
    for (UInt32 k = 0; k < nFrames; ++k)
      ((float*)io->mBuffers[b].mData)[k] = Signal(ts->mSampleTime + k);
  return noErr;
}
static OSStatus BeatAndTempo(void*, Float64* beat, Float64* tempo)
{
  if (beat) *beat = gNow / SR * kTempo / 60.;
  if (tempo) *tempo = kTempo;
  return noErr;
}
static OSStatus TransportState(void*, Boolean* playing, Boolean* changed, Float64* samplePos, Boolean* looping, Float64* loopStart, Float64* loopEnd)
{
  if (playing) *playing = gPlaying;
  if (changed) *changed = false;
  if (samplePos) *samplePos = gNow;
  if (looping) *looping = false;
  if (loopStart) *loopStart = 0.;
  if (loopEnd) *loopEnd = 0.;
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
  AudioComponentDescription d = {kAudioUnitType_Effect, 'UCho', 'Undh', 0, 0};
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  if (!c) { printf("underheard-chorus component not found (build UnderheardChorus-au first)\n"); exit(2); }
  Inst i;
  AudioComponentInstanceNew(c, &i.au);
  AudioStreamBasicDescription f = {SR, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
  AudioUnitSetProperty(i.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
  AudioUnitSetProperty(i.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  UInt32 mf = BS;
  AudioUnitSetProperty(i.au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mf, sizeof mf);
  AURenderCallbackStruct cb = {InputCallback, nullptr};
  AudioUnitSetProperty(i.au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
  HostCallbackInfo host = {};
  host.beatAndTempoProc = BeatAndTempo;
  host.transportStateProc = TransportState;
  AudioUnitSetProperty(i.au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &host, sizeof host);
  if (AudioUnitInitialize(i.au)) { printf("init failed\n"); exit(2); }
  return i;
}

// Renders `secs` from the instance's current sample time; returns the left channel.
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
    gNow = i.sampleTime;
    AudioUnitRenderActionFlags fl = 0;
    if (AudioUnitRender(i.au, &fl, &ts, 0, BS, &abl.l)) { printf("render error\n"); exit(2); }
    out.insert(out.end(), L.begin(), L.end());
    i.sampleTime += BS;
  }
  return out;
}

static double MaxDiff(const std::vector<float>& a, const std::vector<float>& b)
{
  double m = 0.;
  for (size_t k = 0; k < a.size() && k < b.size(); k++)
    m = std::fmax(m, std::fabs(a[k] - b[k]));
  return m;
}

static void Idle() { CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false); }

// Two choruses, synced, joining the song 0.37 s apart: do they move together?
static double LockedDifference()
{
  Inst a = Make(), b = Make();
  for (Inst* i : {&a, &b})
  {
    SetParam(*i, kParamSync, 1.f);
    SetParam(*i, kParamNote, 9.f); // 1/4: 2 Hz at 120 BPM
    SetParam(*i, kParamDepth, 100.f);
    SetParam(*i, kParamMix, 100.f);
    SetParam(*i, kParamAge, 0.f); // Age's warble is random, not locked
  }
  b.sampleTime = 35 * BS; // 0.37 s later in the song
  Render(a, 100. * BS / SR);
  Render(b, 65. * BS / SR);
  // Now both are at the same song position.
  const auto x = Render(a, 0.5), y = Render(b, 0.5);
  AudioComponentInstanceDispose(a.au);
  AudioComponentInstanceDispose(b.au);
  return MaxDiff(x, y);
}

int main()
{
  printf("-- dry and wet\n");
  {
    Inst a = Make();
    Render(a, 0.3);
    auto out = Render(a, 0.5);
    std::vector<float> dry(out.size());
    for (size_t k = 0; k < dry.size(); k++)
      dry[k] = Signal(a.sampleTime - out.size() + k);
    CHECK(MaxDiff(out, dry) > 0.05, "a new chorus changes the sound (%.3f from dry)", MaxDiff(out, dry));
    SetParam(a, kParamMix, 0.f);
    Render(a, 0.2);
    out = Render(a, 0.5);
    for (size_t k = 0; k < dry.size(); k++)
      dry[k] = Signal(a.sampleTime - out.size() + k);
    CHECK(MaxDiff(out, dry) < 1e-5, "Mix at dry is the dry signal");

    SetParam(a, kParamMode, 1.f); // vibrato: voices only
    SetParam(a, kParamVoices, 0.f);
    SetParam(a, kParamDepth, 0.f);
    SetParam(a, kParamDelay, 5.f);
    SetParam(a, kParamTone, 20000.f);
    SetParam(a, kParamAge, 0.f);
    SetParam(a, kParamWarmth, 0.f);
    Render(a, 0.5);
    out = Render(a, 0.5);
    for (size_t k = 0; k < dry.size(); k++)
      dry[k] = Signal(a.sampleTime - out.size() + k - 240.5); // the tone filter adds about half a sample
    CHECK(MaxDiff(out, dry) < 0.02, "vibrato at no depth is the sound 5 ms later, with no dry (%.4f)", MaxDiff(out, dry));
    SetParam(a, kParamWarmth, 50.f);
    Render(a, 0.3);
    out = Render(a, 0.5);
    CHECK(MaxDiff(out, dry) > 0.03, "Warmth colours the voices (%.3f)", MaxDiff(out, dry));
    AudioComponentInstanceDispose(a.au);
  }

  printf("\n-- synced to the song\n");
  gPlaying = true;
  const double locked = LockedDifference();
  gPlaying = false;
  const double free = LockedDifference();
  gPlaying = true;
  CHECK(locked < 1e-4 && free > 0.01, "playing, two choruses that joined apart move together (%.2g); stopped, they don't (%.2g)", locked, free);

  printf("\n-- templates (the host's factory presets) and saving\n");
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
    CHECK(n == 7, "seven templates listed");
    CHECK(choose("Vibrato") && GetParam(a, kParamMode) == 1.f && GetParam(a, kParamVoices) == 0.f && GetParam(a, kParamRate) > 5.f, "Vibrato: one voice, fast");
    CHECK(choose("Jet flanger") && GetParam(a, kParamFeedback) == 70.f && GetParam(a, kParamSync) == 1.f && GetParam(a, kParamDelay) < 2.f,
          "Jet flanger: short, fed back, synced");
    CHECK(choose("Ensemble strings") && GetParam(a, kParamMode) == 2.f && GetParam(a, kParamVoices) == 2.f && GetParam(a, kParamFeedback) == 0.f,
          "Ensemble strings: three voices, the rest back to defaults");
    Render(a, 0.5);
    for (int k = 0; k < 2000; k++)
    {
      auto out = Render(a, 0.0107);
      if (!std::isfinite(out[0])) { CHECK(false, "non-finite output"); break; }
    }

    SetParam(a, kParamDelay, 17.5f);
    SetParam(a, kParamShape, 2.f);
    Idle();
    CFPropertyListRef s = nullptr;
    sz = sizeof s;
    AudioUnitGetProperty(a.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s, &sz);
    Inst b = Make();
    AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s, sizeof s);
    Idle();
    CHECK(std::fabs(GetParam(b, kParamDelay) - 17.5f) < 0.01f && GetParam(b, kParamShape) == 2.f && GetParam(b, kParamMode) == 2.f,
          "settings come back (delay %.1f ms)", GetParam(b, kParamDelay));
    CFRelease(s);
    if (presets) CFRelease(presets);
    AudioComponentInstanceDispose(a.au);
    AudioComponentInstanceDispose(b.au);
  }
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
