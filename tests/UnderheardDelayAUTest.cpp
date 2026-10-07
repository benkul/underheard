// Offline AU host test (macOS) for underheard-delay: echo timing (free and synced to the host
// tempo), Throw, Mix, and the settings surviving save and reopen.
#include <AudioToolbox/AudioToolbox.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

enum { kParamOutput = 0, kParamMix, kParamSync, kParamTime, kParamNote, kParamGlide, kParamFeedback, kParamPolarity, kParamMode, kParamHeads,
       kParamLowCut, kParamHighCut, kParamResonance, kParamDrive, kParamAge, kParamWow, kParamFlutter, kParamPhase, kParamSpread,
       kParamSend, kParamThrow, kParamFreeze, kParamDuck, kParamWarmth };

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double SR = 48000.;
static const UInt32 BS = 512;
static double gTempo = 120.;
static double gImpulseAt = -1.; // the input: one click at this sample time ...
static bool gNoise = false;     // ... or noise

static float Input(Float64 n)
{
  if (gNoise)
    return 0.3f * (float)std::sin(n * 12.9898) * (float)std::sin(n * 0.0137 + 1.);
  return n == gImpulseAt ? 1.f : 0.f;
}

static OSStatus InputCallback(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts, UInt32, UInt32 nFrames, AudioBufferList* io)
{
  for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
    for (UInt32 k = 0; k < nFrames; ++k)
      ((float*)io->mBuffers[b].mData)[k] = Input(ts->mSampleTime + k);
  return noErr;
}

static OSStatus BeatAndTempo(void*, Float64* beat, Float64* tempo)
{
  if (beat) *beat = 0.;
  if (tempo) *tempo = gTempo;
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
  AudioComponentDescription d = {kAudioUnitType_Effect, 'UDly', 'Undh', 0, 0};
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  if (!c) { printf("underheard-delay component not found (build UnderheardDelay-au first)\n"); exit(2); }
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
  AudioUnitSetProperty(i.au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &host, sizeof host);
  if (AudioUnitInitialize(i.au)) { printf("init failed\n"); exit(2); }
  return i;
}

// A clean echo: all wet, one repeat, no wobble or colour.
static Inst MakeClean()
{
  Inst i = Make();
  SetParam(i, kParamMix, 100.f);
  SetParam(i, kParamFeedback, 0.f);
  SetParam(i, kParamWow, 0.f);
  SetParam(i, kParamFlutter, 0.f);
  SetParam(i, kParamAge, 0.f);
  SetParam(i, kParamDrive, 0.f);
  SetParam(i, kParamLowCut, 20.f);
  SetParam(i, kParamHighCut, 20000.f);
  SetParam(i, kParamSpread, 0.f);
  SetParam(i, kParamGlide, 5.f);
  return i;
}

// Renders `secs`; returns the left channel.
static std::vector<float> Render(Inst& i, double secs)
{
  std::vector<float> L(BS), R(BS), out;
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
    out.insert(out.end(), L.begin(), L.end());
    i.sampleTime += BS;
  }
  return out;
}

static size_t Peak(const std::vector<float>& v)
{
  size_t best = 0;
  for (size_t k = 0; k < v.size(); k++)
    if (std::fabs(v[k]) > std::fabs(v[best]))
      best = k;
  return best;
}
static double Rms(const std::vector<float>& v)
{
  double e = 0.;
  for (float x : v)
    e += (double)x * x;
  return std::sqrt(e / (double)v.size());
}

// Where the echo of a click lands, in ms after the click.
static double EchoMs(Inst& i)
{
  Render(i, 0.1); // settle
  gImpulseAt = i.sampleTime;
  const double at = i.sampleTime;
  auto out = Render(i, 1.);
  gImpulseAt = -1.;
  return (Peak(out) - (at - (i.sampleTime - out.size()))) / SR * 1000.;
}

static void Idle() { CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false); }

int main()
{
  printf("-- timing\n");
  {
    Inst a = MakeClean();
    gTempo = 120.;
    const double ms = EchoMs(a);
    CHECK(std::fabs(ms - 375.) < 0.1, "default: synced to 1/8 dotted, 375 ms at 120 BPM (%.2f)", ms);
    AudioComponentInstanceDispose(a.au);
  }
  {
    gTempo = 90.;
    Inst a = MakeClean();
    SetParam(a, kParamNote, 9.f); // 1/4
    const double ms = EchoMs(a);
    CHECK(std::fabs(ms - 666.67) < 0.1, "1/4 at 90 BPM is 666.7 ms (%.2f)", ms);
    gTempo = 120.;
    AudioComponentInstanceDispose(a.au);
  }
  {
    Inst a = MakeClean();
    SetParam(a, kParamSync, 0.f);
    SetParam(a, kParamTime, 250.f);
    const double ms = EchoMs(a);
    CHECK(std::fabs(ms - 250.) < 0.1, "free: 250 ms (%.2f)", ms);

    printf("\n-- mix and throw\n");
    gNoise = true;
    SetParam(a, kParamMix, 0.f);
    Render(a, 0.2);
    auto dry = Render(a, 0.2);
    double err = 0.;
    for (size_t k = 0; k < dry.size(); k++)
      err = std::fmax(err, std::fabs(dry[k] - Input(a.sampleTime - dry.size() + k)));
    CHECK(err < 1e-5, "Mix at dry is the dry signal (%.2g)", err);
    SetParam(a, kParamMix, 100.f);
    SetParam(a, kParamSend, 1.f); // throw only
    gNoise = false;
    Render(a, 0.5); // let what was in there pass
    gNoise = true;
    const double closed = Rms(Render(a, 0.5));
    SetParam(a, kParamThrow, 1.f);
    const double open = Rms(Render(a, 0.5));
    SetParam(a, kParamThrow, 0.f);
    CHECK(closed < 1e-6 && open > 0.05, "only what's thrown goes in (%.2g held off, %.3f thrown)", closed, open);
    gNoise = false;

    printf("\n-- save and reopen\n");
    SetParam(a, kParamTime, 123.f);
    SetParam(a, kParamMode, 2.f);
    SetParam(a, kParamThrow, 1.f); // held while saving
    Idle();
    CFPropertyListRef s = nullptr;
    UInt32 sz = sizeof s;
    AudioUnitGetProperty(a.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s, &sz);
    Inst b = Make();
    AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s, sizeof s);
    Idle();
    CHECK(std::fabs(GetParam(b, kParamTime) - 123.f) < 0.01f && GetParam(b, kParamMode) == 2.f && GetParam(b, kParamSend) == 1.f,
          "settings come back (time %.1f, mode %.0f)", GetParam(b, kParamTime), GetParam(b, kParamMode));
    CHECK(GetParam(b, kParamThrow) == 0.f, "Throw doesn't come back held");
    CFRelease(s);

    // A project saved by version 1 (Warmth ran half as strong): its 100% opens as 50%.
    SetParam(a, kParamWarmth, 100.f);
    Idle();
    CFPropertyListRef s1 = nullptr;
    sz = sizeof s1;
    AudioUnitGetProperty(a.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s1, &sz);
    CFMutableDictionaryRef old = CFDictionaryCreateMutableCopy(nullptr, 0, (CFDictionaryRef)s1);
    CFMutableDataRef data = CFDataCreateMutableCopy(nullptr, 0, (CFDataRef)CFDictionaryGetValue(old, CFSTR("data")));
    const int32_t v1 = 1;
    CFDataReplaceBytes(data, CFRangeMake(4, 4), (const UInt8*)&v1, 4); // after the magic
    CFDictionarySetValue(old, CFSTR("data"), data);
    Inst c = Make();
    CFPropertyListRef oldState = old;
    AudioUnitSetProperty(c.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &oldState, sizeof oldState);
    Idle();
    Inst d = Make();
    AudioUnitSetProperty(d.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &s1, sizeof s1);
    Idle();
    CHECK(std::fabs(GetParam(c, kParamWarmth) - 50.f) < 0.01f && std::fabs(GetParam(d, kParamWarmth) - 100.f) < 0.01f,
          "an older project's Warmth 100%% opens as 50%% (%.0f%%); a current one stays at 100%%", GetParam(c, kParamWarmth));
    CFRelease(data);
    CFRelease(old);
    CFRelease(s1);
    AudioComponentInstanceDispose(c.au);
    AudioComponentInstanceDispose(d.au);
    AudioComponentInstanceDispose(a.au);
    AudioComponentInstanceDispose(b.au);
  }
  printf("\n-- templates (the host's factory presets)\n");
  {
    Inst a = MakeClean();
    CFArrayRef presets = nullptr;
    UInt32 sz = sizeof presets;
    AudioUnitGetProperty(a.au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &presets, &sz);
    const CFIndex n = presets ? CFArrayGetCount(presets) : 0;
    std::vector<std::string> names;
    for (CFIndex k = 0; k < n; k++)
    {
      const AUPreset* p = (const AUPreset*)CFArrayGetValueAtIndex(presets, k);
      char buf[64] = {};
      CFStringGetCString(p->presetName, buf, sizeof buf, kCFStringEncodingUTF8);
      names.push_back(buf);
    }
    auto find = [&](const char* name) { for (CFIndex k = 0; k < n; k++) if (names[(size_t)k] == name) return (int)k; return -1; };
    CHECK(find("Dub echo") >= 0 && find("Slapback") >= 0 && find("Large atmosphere") >= 0, "the templates are listed (%d of them)", (int)n);
    auto choose = [&](const char* name) {
      AUPreset p = *(const AUPreset*)CFArrayGetValueAtIndex(presets, find(name));
      AudioUnitSetProperty(a.au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, sizeof p);
    };
    choose("Slapback");
    CHECK(GetParam(a, kParamSync) == 0.f && std::fabs(GetParam(a, kParamTime) - 95.f) < 0.01f && GetParam(a, kParamFeedback) < 10.f,
          "Slapback: one short repeat (%.0f ms, feedback %.0f%%)", GetParam(a, kParamTime), GetParam(a, kParamFeedback));
    SetParam(a, kParamMix, 100.f);
    SetParam(a, kParamWow, 0.f);
    SetParam(a, kParamFlutter, 0.f);
    Render(a, 0.5); // let the time finish gliding from the last setting
    const double ms = EchoMs(a);
    CHECK(std::fabs(ms - 95.) < 0.1, "and it echoes at 95 ms (%.2f)", ms);
    choose("Dub echo");
    CHECK(GetParam(a, kParamMode) == 2.f && GetParam(a, kParamHeads) == 5.f && GetParam(a, kParamSync) == 1.f && GetParam(a, kParamFeedback) > 80.f,
          "Dub echo: heads 2+3, synced, high feedback");
    choose("Large atmosphere");
    CHECK(GetParam(a, kParamMode) == 1.f && GetParam(a, kParamSpread) == 100.f && GetParam(a, kParamDuck) > 0.f && GetParam(a, kParamMix) == 40.f,
          "Large atmosphere: wide ping-pong, ducked (and Mix back to the template's)");
    AudioComponentInstanceDispose(a.au);
    a = Make(); // a fresh, empty delay: the last template's repeats would take a while to die
    choose("Dub throw");
    const bool sendIsThrow = GetParam(a, kParamSend) == 1.f;
    SetParam(a, kParamMix, 100.f); // listen to the echoes only
    gNoise = true;
    Render(a, 0.3);
    const double closed = Rms(Render(a, 0.5));
    gNoise = false;
    CHECK(sendIsThrow && GetParam(a, kParamThrow) == 0.f && closed < 1e-3, "Dub throw: waits for THROW (%.2g: only Age's tape hiss)", closed);
    if (presets) CFRelease(presets);
    AudioComponentInstanceDispose(a.au);
  }
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
