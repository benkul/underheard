// Offline AU host test (macOS): loads the installed Splicer component and drives it like a
// DAW would, with no GUI: pass-through, Output, record/play through the transport
// parameters, Speed, Clear, several loops with pan, Catch, and saving and reopening a project
// with its loop audio. Run with tests/run-tests.sh.
#include <AudioToolbox/AudioToolbox.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// Must match EParams / ELoopParam in plugins/Splicer/Splicer.h.
enum { kRecord = 0, kPlay, kClear, kCatch, kSpeed, kReverse, kSource, kErase, kFeedback, kSplice, kDry, kPan, kNumA,
       kWear = kNumA, kWearRate, kRestore, kWow, kFlutter, kNumAB,
       kRazor = kNumAB, kCutReverse, kCutRemove, kCutIsolate, kNumABC,
       kSend = kNumABC, kNumABCD, kWearLimit = kNumABCD, kRecover, kNumLoopParams };
enum { kParamOutput = 0, kParamMix, kParamMotor, kParamCatchLength, kParamSync, kParamLoops,
       kParamHiss = kParamLoops + 4 * kNumA, kParamLoopsB, kParamLoopsC = kParamLoopsB + 4 * (kNumAB - kNumA),
       kParamChorusOn = kParamLoopsC + 4 * (kNumABC - kNumAB), kParamChorusRate, kParamChorusDepth, kParamChorusMix,
       kParamDelayOn, kParamDelayTime, kParamDelayFeedback, kParamDelayTone, kParamDelayMix,
       kParamReverbOn, kParamReverbSize, kParamReverbDecay, kParamReverbTone, kParamReverbMix, kParamLoopsD,
       kParamDriftOn = kParamLoopsD + 4, kParamDriftLength, kParamDriftCurve, kParamDriftSmear, kParamDriftReach, kParamDriftGravity,
       kParamDriftKeep, kParamDriftReturn, kParamInputLevel, kParamInputSend, kParamLoopsE };
// The fuller effects, after the loop blocks.
enum { kParamFxWarmth = kParamLoopsE + 4 * (kNumLoopParams - kNumABCD), kParamChorusMode, kParamChorusVoices, kParamChorusFeedback, kParamDelayNote,
       kParamDelayHeads, kParamReverbEngine };
static int LoopParam(int loop, int p)
{
  return p < kNumA ? kParamLoops + loop * kNumA + p
       : p < kNumAB ? kParamLoopsB + loop * (kNumAB - kNumA) + (p - kNumA)
       : p < kNumABC ? kParamLoopsC + loop * (kNumABC - kNumAB) + (p - kNumAB)
       : p < kNumABCD ? kParamLoopsD + loop * (kNumABCD - kNumABC) + (p - kNumABC)
                      : kParamLoopsE + loop * (kNumLoopParams - kNumABCD) + (p - kNumABCD);
}

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double SR = 48000.;
static const UInt32 BS = 512;

// The test signal: a different tone on each channel, so a channel swap would be caught.
static float Signal(int ch, Float64 n) { return 0.5f * (float)std::sin(2. * M_PI * (ch ? 330. : 220.) * n / SR); }

static OSStatus InputCallback(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts, UInt32, UInt32 nFrames, AudioBufferList* io)
{
  for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
  {
    float* d = (float*)io->mBuffers[b].mData;
    for (UInt32 k = 0; k < nFrames; ++k)
      d[k] = Signal((int)b, ts->mSampleTime + k);
  }
  return noErr;
}

// The sidechain carries its own tone.
static OSStatus SidechainCallback(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts, UInt32, UInt32 nFrames, AudioBufferList* io)
{
  for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
  {
    float* d = (float*)io->mBuffers[b].mData;
    for (UInt32 k = 0; k < nFrames; ++k)
      d[k] = 0.5f * (float)std::sin(2. * M_PI * 550. * (ts->mSampleTime + k) / SR);
  }
  return noErr;
}

struct Inst { AudioUnit au; Float64 sampleTime = 0; };

static Inst Make(bool sidechain = false)
{
  AudioComponentDescription d = {kAudioUnitType_Effect, 'Splc', 'Undh', 0, 0};
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  if (!c) { printf("Splicer component not found (build Splicer-au first)\n"); exit(2); }
  Inst i;
  AudioComponentInstanceNew(c, &i.au);
  AudioStreamBasicDescription f = {SR, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
  AudioUnitSetProperty(i.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
  AudioUnitSetProperty(i.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  UInt32 mf = BS;
  AudioUnitSetProperty(i.au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mf, sizeof mf);
  AURenderCallbackStruct cb = {InputCallback, nullptr};
  AudioUnitSetProperty(i.au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
  if (sidechain)
  {
    AudioUnitSetProperty(i.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &f, sizeof f);
    AURenderCallbackStruct sc = {SidechainCallback, nullptr};
    AudioUnitSetProperty(i.au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 1, &sc, sizeof sc);
  }
  OSStatus e = AudioUnitInitialize(i.au);
  if (e) { printf("init error %d\n", (int)e); exit(2); }
  return i;
}

static void SetParam(Inst& i, int p, float v) { AudioUnitSetParameter(i.au, p, kAudioUnitScope_Global, 0, v, 0); }
static float GetParam(Inst& i, int p) { AudioUnitParameterValue v = 0; AudioUnitGetParameter(i.au, p, kAudioUnitScope_Global, 0, &v); return v; }

// Lets the plugin's idle timer run (catches, loads, button fixes happen there).
static void Idle(double secs = 0.15) { CFRunLoopRunInMode(kCFRunLoopDefaultMode, secs, false); }

// Renders `secs` seconds and returns the largest |out - expectedGain * in| over both channels.
// The outputs are appended to `left` / `right` if given.
static float Render(Inst& i, double secs, float expectedGain = 0.f, std::vector<float>* left = nullptr, std::vector<float>* right = nullptr)
{
  std::vector<float> L(BS), R(BS);
  float maxErr = 0;
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
    OSStatus e = AudioUnitRender(i.au, &fl, &ts, 0, BS, &abl.l);
    if (e) { printf("render error %d\n", (int)e); exit(2); }
    if (left) left->insert(left->end(), L.begin(), L.end());
    if (right) right->insert(right->end(), R.begin(), R.end());
    for (UInt32 k = 0; k < BS; ++k)
    {
      maxErr = std::fmax(maxErr, std::fabs(L[k] - expectedGain * Signal(0, i.sampleTime + k)));
      maxErr = std::fmax(maxErr, std::fabs(R[k] - expectedGain * Signal(1, i.sampleTime + k)));
    }
    i.sampleTime += BS;
  }
  return maxErr;
}

// Amplitude of one frequency (Goertzel).
static double ToneAmp(const std::vector<float>& v, double hz)
{
  const double w = 2. * M_PI * hz / SR, c = 2. * std::cos(w);
  double s1 = 0, s2 = 0;
  for (float x : v)
  {
    const double s0 = x + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
  return 2. * std::sqrt(re * re + im * im) / (double)v.size();
}

static double Rms(const std::vector<float>& v)
{
  double s = 0;
  for (float x : v)
    s += (double)x * x;
  return v.empty() ? 0. : std::sqrt(s / (double)v.size());
}

// A fresh instance set up to hear only the tape, with instant motor, and no splice, wear,
// wow, flutter or hiss, so levels and pitches can be checked exactly.
static Inst MakeTapeOnly(bool sidechain = false)
{
  Inst c = Make(sidechain);
  SetParam(c, kParamMix, 100.f);
  SetParam(c, kParamMotor, 0.f);
  SetParam(c, kParamHiss, 0.f);
  for (int l = 0; l < 4; l++)
  {
    SetParam(c, LoopParam(l, kSplice), 0.f);
    SetParam(c, LoopParam(l, kWear), 0.f);
    SetParam(c, LoopParam(l, kWow), 0.f);
    SetParam(c, LoopParam(l, kFlutter), 0.f);
  }
  Render(c, 0.2);
  return c;
}

static void Press(Inst& c, int param, double holdSecs)
{
  SetParam(c, param, 1.f);
  Render(c, holdSecs);
  SetParam(c, param, 0.f);
}

// Takes are whole 512-sample blocks, so not whole numbers of cycles: the seam shifts phase,
// and the checks use level plus "mostly this tone" instead of exact tone amplitude.
static void TestRecordAndPlay()
{
  printf("\n-- record and play\n");
  Inst c = MakeTapeOnly();
  Press(c, LoopParam(0, kRecord), 1.0);
  Render(c, 0.1);

  std::vector<float> out;
  Render(c, 1.0, 0.f, &out);
  CHECK(std::fabs(Rms(out) - 0.3536) < 0.015 && ToneAmp(out, 220.) > 0.4,
        "Record on/off makes a loop that plays the take back (rms %.3f, 220 Hz at %.3f)", Rms(out), ToneAmp(out, 220.));

  SetParam(c, LoopParam(0, kSpeed), -1.f); // one octave down
  Render(c, 0.5);
  out.clear();
  Render(c, 2.0, 0.f, &out);
  CHECK(std::fabs(Rms(out) - 0.3536) < 0.015 && ToneAmp(out, 110.) > 0.3 && ToneAmp(out, 220.) < 0.05,
        "Speed 0.5x plays it an octave down (rms %.3f, 110 Hz at %.3f)", Rms(out), ToneAmp(out, 110.));

  SetParam(c, LoopParam(0, kClear), 1.f);
  Render(c, 0.1);
  out.clear();
  Render(c, 0.5, 0.f, &out);
  CHECK(Rms(out) < 1e-3, "Clear empties the loop");
  Idle();
  CHECK(GetParam(c, LoopParam(0, kClear)) == 0.f && GetParam(c, LoopParam(0, kPlay)) == 0.f, "Clear and Play buttons spring back (clear %.0f, play %.0f)",
        GetParam(c, LoopParam(0, kClear)), GetParam(c, LoopParam(0, kPlay)));
  AudioComponentInstanceDispose(c.au);
}

static void TestLoopsAndPan()
{
  printf("\n-- several loops, pan\n");
  Inst c = MakeTapeOnly();
  SetParam(c, LoopParam(2, kPan), -100.f); // loop 3 hard left
  Press(c, LoopParam(2, kRecord), 1.0);
  Render(c, 0.1);
  std::vector<float> L, R;
  Render(c, 1.0, 0.f, &L, &R);
  CHECK(ToneAmp(L, 220.) > 0.4 && Rms(R) < 1e-3, "loop 3 plays, panned hard left (L 220 Hz %.3f, R rms %.4f)", ToneAmp(L, 220.), Rms(R));

  SetParam(c, LoopParam(1, kSpeed), -1.f);
  Press(c, LoopParam(1, kRecord), 1.0); // loop 2, at half speed: plays back at pitch
  Render(c, 0.1);
  L.clear();
  R.clear();
  Render(c, 2.0, 0.f, &L, &R);
  // Loop 2 is the only thing on the right; on the left the two loops' phases are arbitrary.
  CHECK(std::fabs(Rms(R) - 0.3536) < 0.02 && Rms(L) > 0.1, "loops 2 and 3 play together (R rms %.3f, L rms %.3f)", Rms(R), Rms(L));
  Idle();
  CHECK(GetParam(c, LoopParam(1, kPlay)) == 1.f && GetParam(c, LoopParam(2, kPlay)) == 1.f && GetParam(c, LoopParam(0, kPlay)) == 0.f,
        "Play buttons light for the loops that are running");
  AudioComponentInstanceDispose(c.au);
}

static void TestCatch()
{
  printf("\n-- catch\n");
  Inst c = MakeTapeOnly();
  SetParam(c, kParamCatchLength, 2.f);
  Render(c, 3.0); // three seconds go by without recording
  SetParam(c, LoopParam(3, kCatch), 1.f);
  Render(c, 0.02);
  Idle(); // the catch is cut from the capture buffer on the main thread
  Render(c, 0.1);
  std::vector<float> L, R;
  Render(c, 2.0, 0.f, &L, &R);
  CHECK(ToneAmp(L, 220.) > 0.4 && ToneAmp(R, 330.) > 0.4, "Catch on loop 4 plays back what was just heard (220 Hz %.3f, 330 Hz %.3f)",
        ToneAmp(L, 220.), ToneAmp(R, 330.));
  Idle();
  CHECK(GetParam(c, LoopParam(3, kCatch)) == 0.f && GetParam(c, LoopParam(3, kPlay)) == 1.f, "Catch springs back and Play lights");
  AudioComponentInstanceDispose(c.au);
}

static void TestSaveAndReopen(const std::filesystem::path& dir)
{
  printf("\n-- save and reopen with loop audio\n");
  Inst a = MakeTapeOnly();
  SetParam(a, LoopParam(1, kSpeed), 0.5f); // loop 2 at 1.41x: a non-trivial setting to restore
  Press(a, LoopParam(1, kRecord), 1.0);
  Render(a, 0.3);

  CFPropertyListRef state = nullptr;
  UInt32 sz = sizeof state;
  CHECK(AudioUnitGetProperty(a.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &sz) == noErr && state, "project saves");
  size_t files = 0;
  for (auto& e : std::filesystem::directory_iterator(dir / "Splicer"))
    files += e.path().extension() == ".wav";
  CHECK(files == 1, "the loop's tape is written as one WAV file (%zu)", files);

  Inst b = Make();
  CHECK(AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof state) == noErr, "project reopens");
  Render(b, 0.1);
  std::vector<float> L;
  Render(b, 0.5, 0.f, &L);
  CHECK(Rms(L) < 1e-3, "reopened loops wait, stopped");
  SetParam(b, LoopParam(1, kPlay), 1.f);
  Render(b, 0.3);
  L.clear();
  Render(b, 2.0, 0.f, &L);
  // Recorded at 1.41x and played back at the restored 1.41x: the original pitch.
  CHECK(std::fabs(Rms(L) - 0.3536) < 0.02 && ToneAmp(L, 220.) > 0.25, "Play brings back the saved loop at its saved speed (rms %.3f, 220 Hz at %.3f)",
        Rms(L), ToneAmp(L, 220.));
  if (state) CFRelease(state);
  AudioComponentInstanceDispose(a.au);
  AudioComponentInstanceDispose(b.au);
}

static void TestSidechain()
{
  printf("\n-- sidechain as a loop's source\n");
  Inst c = MakeTapeOnly(true);
  SetParam(c, LoopParam(0, kSource), 1.f);
  Press(c, LoopParam(0, kRecord), 1.0);
  Render(c, 0.1);
  std::vector<float> L;
  Render(c, 1.0, 0.f, &L);
  CHECK(ToneAmp(L, 550.) > 0.3 && ToneAmp(L, 220.) < 0.02, "loop 1 records the sidechain, not the main input (550 Hz %.3f, 220 Hz %.3f)",
        ToneAmp(L, 550.), ToneAmp(L, 220.));
  AudioComponentInstanceDispose(c.au);
}

// Mean difference between the output and itself `period` samples later: near zero when the
// loop repeats with that period.
static double PeriodError(const std::vector<float>& v, size_t period)
{
  double e = 0;
  size_t n = 0;
  for (size_t i = 0; i + period < v.size(); i++, n++)
    e += std::fabs(v[i] - v[i + period]);
  return n ? e / (double)n : 1.;
}

static void TestSync()
{
  printf("\n-- tempo sync\n");
  // No host tempo here, so the plugin uses iPlug's default of 120 BPM: a beat is 0.5 s.
  Inst c = MakeTapeOnly();
  SetParam(c, kParamSync, 1.f); // Beats
  Press(c, LoopParam(0, kRecord), 1.7);
  Render(c, 0.2);
  std::vector<float> L;
  Render(c, 4.0, 0.f, &L);
  const double beats3 = PeriodError(L, 72000), free = PeriodError(L, (size_t)(1.7 * SR) / BS * BS);
  CHECK(beats3 < 0.01 && free > 0.1, "a 1.7 s take snaps to 3 beats, 1.5 s (repeat error %.4f at 1.5 s, %.4f at 1.7 s)", beats3, free);
  AudioComponentInstanceDispose(c.au);
}

static void TestWearAndRestore()
{
  printf("\n-- wear and restore\n");
  Inst c = MakeTapeOnly();
  Press(c, LoopParam(0, kRecord), 0.25);
  Render(c, 0.1);
  std::vector<float> L;
  Render(c, 1.0, 0.f, &L);
  const double fresh = Rms(L);
  SetParam(c, LoopParam(0, kWear), 1.f);
  SetParam(c, LoopParam(0, kWearRate), 100.f);
  Render(c, 10.0);
  SetParam(c, LoopParam(0, kWear), 0.f);
  L.clear();
  Render(c, 1.0, 0.f, &L);
  const double worn = Rms(L);
  CHECK(worn < 0.7 * fresh, "a 1/4 s loop at full Wear Rate wears down in 10 s (rms %.3f -> %.3f)", fresh, worn);
  SetParam(c, LoopParam(0, kRestore), 1.f);
  Render(c, 0.1);
  L.clear();
  Render(c, 1.0, 0.f, &L);
  CHECK(std::fabs(Rms(L) / fresh - 1.) < 0.03, "Restore brings it back (rms %.3f)", Rms(L));
  Idle();
  CHECK(GetParam(c, LoopParam(0, kRestore)) == 0.f, "Restore springs back");
  AudioComponentInstanceDispose(c.au);
}

static void TestRecover()
{
  printf("\n-- wear limit and recover\n");
  Inst c = MakeTapeOnly();
  Press(c, LoopParam(0, kRecord), 0.25);
  Render(c, 0.1);
  std::vector<float> L;
  Render(c, 1.0, 0.f, &L);
  const double fresh = Rms(L);
  SetParam(c, LoopParam(0, kWear), 1.f);
  SetParam(c, LoopParam(0, kWearRate), 100.f);
  SetParam(c, LoopParam(0, kWearLimit), 25.f);
  SetParam(c, LoopParam(0, kRecover), 1.f);
  double lowest = 1e9, highestAfter = 0.;
  for (int s = 0; s < 60; s++)
  {
    L.clear();
    Render(c, 0.5, 0.f, &L);
    const double r = Rms(L);
    if (s < 30) lowest = std::min(lowest, r);
    else highestAfter = std::max(highestAfter, r);
  }
  CHECK(lowest < 0.9 * fresh && highestAfter > 0.97 * fresh, "with Recover the loop wears down, then comes back (rms %.3f -> %.3f -> %.3f)",
        fresh, lowest, highestAfter);
  AudioComponentInstanceDispose(c.au);
}

static void TestRazor()
{
  printf("\n-- razor through the parameters\n");
  Inst c = MakeTapeOnly();
  Press(c, LoopParam(0, kRecord), 1.0); // 93 blocks: 47616 frames
  Render(c, 0.1);
  Press(c, LoopParam(0, kRazor), 0.02); // mark the start
  Render(c, 0.5);
  Press(c, LoopParam(0, kRazor), 0.02); // mark the end, 47 blocks later
  Press(c, LoopParam(0, kCutRemove), 0.02);
  Idle(); // the cut is built on the main thread
  Render(c, 0.2);
  const size_t expected = 47616 - 47 * 512;
  std::vector<float> L;
  Render(c, 2.0, 0.f, &L);
  CHECK(PeriodError(L, expected) < 0.01 && PeriodError(L, 47616) > 0.05, "Remove shortens the loop to %zu frames (repeat error %.4f)",
        expected, PeriodError(L, expected));
  Idle();
  CHECK(GetParam(c, LoopParam(0, kRazor)) == 0.f && GetParam(c, LoopParam(0, kCutRemove)) == 0.f, "Razor buttons spring back");

  CFPropertyListRef state = nullptr;
  UInt32 sz = sizeof state;
  AudioUnitGetProperty(c.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &sz);
  Inst b = Make();
  AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof state);
  Render(b, 0.1);
  SetParam(b, LoopParam(0, kPlay), 1.f);
  Render(b, 0.3);
  L.clear();
  Render(b, 2.0, 0.f, &L);
  CHECK(PeriodError(L, expected) < 0.01 && Rms(L) > 0.3, "the cut is saved with the project (repeat error %.4f, rms %.3f)", PeriodError(L, expected), Rms(L));
  if (state) CFRelease(state);
  AudioComponentInstanceDispose(b.au);
  AudioComponentInstanceDispose(c.au);
}

static void TestSend()
{
  printf("\n-- send into the effects\n");
  Inst c = MakeTapeOnly();
  SetParam(c, kParamChorusOn, 0.f);
  SetParam(c, kParamDelayOn, 0.f);
  SetParam(c, kParamReverbOn, 0.f);
  Press(c, LoopParam(0, kRecord), 1.0);
  Render(c, 0.1);
  SetParam(c, LoopParam(0, kDry), 0.f);
  Render(c, 0.2);
  std::vector<float> L;
  Render(c, 1.0, 0.f, &L);
  CHECK(Rms(L) < 1e-3, "Dry 0 and Send 0: silent (rms %.4f)", Rms(L));
  SetParam(c, LoopParam(0, kSend), 100.f);
  Render(c, 0.2);
  L.clear();
  Render(c, 1.0, 0.f, &L);
  CHECK(std::fabs(Rms(L) - 0.3536) < 0.015, "Send alone, every stage off: the loop comes through the chain unchanged (rms %.3f)", Rms(L));

  SetParam(c, kParamReverbOn, 1.f);
  SetParam(c, kParamReverbMix, 100.f);
  SetParam(c, kParamReverbDecay, 4.f);
  Render(c, 1.0);
  SetParam(c, LoopParam(0, kPlay), 0.f);
  Render(c, 0.2);
  L.clear();
  Render(c, 0.5, 0.f, &L);
  CHECK(Rms(L) > 0.01, "the reverb tail rings on after the loop stops (rms %.3f)", Rms(L));
  AudioComponentInstanceDispose(c.au);
}

// Rewrites a current (format 3) state into the old format: no header, only the first
// `keepParams` parameters, then the magic, version 2 and the loops.
static CFPropertyListRef ToLegacyState(CFPropertyListRef state, int keepParams)
{
  CFDataRef data = (CFDataRef)CFDictionaryGetValue((CFDictionaryRef)state, CFSTR("data"));
  const UInt8* b = CFDataGetBytePtr(data);
  const CFIndex size = CFDataGetLength(data);
  int32_t magic, version, count;
  std::memcpy(&magic, b, 4);
  std::memcpy(&version, b + 4, 4);
  std::memcpy(&count, b + 8, 4);
  std::vector<UInt8> out(b + 12, b + 12 + 8 * keepParams);           // parameters, no count
  const int32_t v2 = 2;
  out.insert(out.end(), (const UInt8*)&magic, (const UInt8*)&magic + 4);
  out.insert(out.end(), (const UInt8*)&v2, (const UInt8*)&v2 + 4);
  out.insert(out.end(), b + 12 + 8 * count, b + size);              // the loops
  CFMutableDictionaryRef d = CFDictionaryCreateMutableCopy(nullptr, 0, (CFDictionaryRef)state);
  CFDataRef nd = CFDataCreate(nullptr, out.data(), (CFIndex)out.size());
  CFDictionarySetValue(d, CFSTR("data"), nd);
  CFRelease(nd);
  return d;
}

static void TestOldProjects()
{
  printf("\n-- projects saved by an older Splicer, with fewer parameters\n");
  Inst a = MakeTapeOnly();
  Press(a, LoopParam(2, kRecord), 1.0);
  Render(a, 0.3);
  CFPropertyListRef state = nullptr;
  UInt32 sz = sizeof state;
  AudioUnitGetProperty(a.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &sz);
  CFPropertyListRef old = ToLegacyState(state, 90); // the build before the effects had 90

  Inst b = Make();
  CHECK(AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &old, sizeof old) == noErr, "an old-format project opens");
  CHECK(GetParam(b, kParamMix) == 100.f && GetParam(b, kParamHiss) == 0.f, "its settings come back");
  CHECK(GetParam(b, LoopParam(2, kSend)) == 0.f && GetParam(b, kParamReverbOn) == 1.f, "parameters it didn't have get their defaults");
  Render(b, 0.1);
  SetParam(b, LoopParam(2, kPlay), 1.f);
  Render(b, 0.3);
  std::vector<float> L;
  Render(b, 1.0, 0.f, &L);
  CHECK(std::fabs(Rms(L) - 0.3536) < 0.02, "and so does its loop (rms %.3f)", Rms(L));
  CFRelease(old);
  CFRelease(state);
  AudioComponentInstanceDispose(a.au);
  AudioComponentInstanceDispose(b.au);
}

static void TestDrifter()
{
  printf("\n-- Drifter\n");
  const int targets[] = {kParamChorusRate, kParamChorusDepth, kParamChorusMix, kParamDelayTime, kParamDelayFeedback, kParamDelayTone,
                         kParamDelayMix, kParamReverbSize, kParamReverbDecay, kParamReverbTone, kParamReverbMix};
  Inst c = Make();
  float before[11];
  for (int i = 0; i < 11; i++) before[i] = GetParam(c, targets[i]);
  Render(c, 2.0);
  SetParam(c, kParamDriftKeep, 1.f);
  Render(c, 0.02);
  Idle();
  int changed = 0;
  for (int i = 0; i < 11; i++) changed += GetParam(c, targets[i]) != before[i];
  CHECK(changed == 0, "off by default: Keep changes nothing");

  SetParam(c, kParamDriftLength, 1.f);
  SetParam(c, kParamDriftReach, 100.f);
  SetParam(c, kParamDriftGravity, 0.f);
  SetParam(c, kParamDriftOn, 1.f);
  Render(c, 3.0);
  for (int i = 0; i < 11; i++) CHECK(GetParam(c, targets[i]) == before[i], "drifting leaves setting %d alone", i);
  SetParam(c, kParamDriftKeep, 1.f);
  Render(c, 0.02);
  Idle();
  changed = 0;
  for (int i = 0; i < 11; i++) changed += std::fabs(GetParam(c, targets[i]) - before[i]) > 1e-3f * (1.f + std::fabs(before[i]));
  CHECK(changed >= 8, "Keep moves the settings to where they drifted (%d of 11 changed)", changed);
  CHECK(GetParam(c, kParamDriftKeep) == 0.f, "Keep springs back");
  float kept[11];
  for (int i = 0; i < 11; i++) kept[i] = GetParam(c, targets[i]);
  SetParam(c, kParamDriftOn, 0.f);
  Render(c, 3.0);
  int same = 0;
  for (int i = 0; i < 11; i++) same += GetParam(c, targets[i]) == kept[i];
  CHECK(same == 11, "turning Drift off afterwards leaves the kept settings");
  AudioComponentInstanceDispose(c.au);
}

static void TestInputSend()
{
  printf("\n-- the input strip\n");
  Inst c = MakeTapeOnly(); // Mix fully to Tape, no loops
  SetParam(c, kParamChorusOn, 0.f);
  SetParam(c, kParamDelayOn, 0.f);
  SetParam(c, kParamReverbOn, 0.f);
  Render(c, 0.2);
  CHECK(Render(c, 0.5, 0.f) < 1e-4f, "Level and Send 0: nothing from the input on the Tape side");
  SetParam(c, kParamInputLevel, 100.f);
  Render(c, 0.2);
  CHECK(Render(c, 0.5, 1.f) < 1e-3f, "Level 100: the input on the Tape side, like a loop");
  SetParam(c, kParamInputLevel, 0.f);
  SetParam(c, kParamInputSend, 100.f);
  Render(c, 0.2);
  CHECK(Render(c, 0.5, 1.f) < 1e-3f, "Send 100 through a bypassed chain: the input itself");
  SetParam(c, kParamDelayOn, 1.f);
  SetParam(c, kParamDelayMix, 100.f);
  SetParam(c, kParamDelayTime, 250.f);
  SetParam(c, kParamDelayFeedback, 0.f);
  Render(c, 2.0); // the delay time glides to 250 ms like tape (bending the pitch on the way)
  std::vector<float> L;
  Render(c, 0.5, 0.f, &L);
  // Fully wet: the input's tone comes out at full level but no longer lined up with the input.
  // (Echo timing is measured exactly with an impulse in FxTest; a steady tone can't show it.)
  double aligned = 0.;
  for (size_t k = 0; k < L.size(); k++)
    aligned = std::max(aligned, (double)std::fabs(L[k] - Signal(0, c.sampleTime - (double)L.size() + (double)k)));
  CHECK(std::fabs(Rms(L) - 0.3536) < 0.03 && aligned > 0.1, "with the delay on, the input goes through it (rms %.3f, delayed)", Rms(L));
  SetParam(c, kParamMix, 0.f);
  Render(c, 0.3);
  CHECK(Render(c, 0.5, 1.f) < 1e-3f, "Mix fully to Input: the dry input only, no effects");
  AudioComponentInstanceDispose(c.au);
}

static void TestFullerEffects()
{
  printf("\n-- the fuller effects (the standalone plugins' engines)\n");
  Inst c = MakeTapeOnly();
  SetParam(c, kParamChorusOn, 0.f);
  SetParam(c, kParamReverbOn, 0.f);
  SetParam(c, kParamFxWarmth, 0.f);
  SetParam(c, kParamInputSend, 100.f);
  SetParam(c, kParamDelayOn, 1.f);
  SetParam(c, kParamDelayMix, 100.f);
  SetParam(c, kParamDelayFeedback, 0.f);
  SetParam(c, kParamDelayTime, 250.f);
  // The right input is 330 Hz: 250 ms later it's inverted (82.5 cycles), 500 ms later in phase.
  auto echoPhase = [&]() {
    Render(c, 2.0); // let the time glide there
    std::vector<float> L, R;
    Render(c, 0.5, 0.f, &L, &R);
    double xy = 0., xx = 0., yy = 0.;
    for (size_t k = 0; k < R.size(); k++)
    {
      const double x = Signal(1, c.sampleTime - (double)R.size() + (double)k);
      xy += x * R[k];
      xx += x * x;
      yy += (double)R[k] * R[k];
    }
    return xy / std::sqrt(xx * yy);
  };
  const double free = echoPhase();
  SetParam(c, kParamDelayNote, 10.f); // 1/4 (Free is 0)
  const double synced = echoPhase();
  CHECK(free < -0.5 && synced > 0.5, "Delay Note 1/4 at 120 BPM: 500 ms instead of Time's 250 (%.2f, then %.2f)", free, synced);
  std::vector<float> a, b;
  Render(c, 0.5, 0.f, &a);
  SetParam(c, kParamFxWarmth, 100.f);
  Render(c, 0.3);
  Render(c, 0.5, 0.f, &b);
  CHECK(Rms(a) > 0.1 && std::fabs(Rms(a) - Rms(b)) > 0.002, "Warmth changes the chain's sound (rms %.3f -> %.3f)", Rms(a), Rms(b));
  SetParam(c, kParamReverbOn, 1.f);
  SetParam(c, kParamReverbMix, 100.f);
  for (int engine : {0, 1})
  {
    SetParam(c, kParamReverbEngine, (float)engine);
    SetParam(c, kParamChorusOn, 1.f);
    SetParam(c, kParamChorusMode, 2.f);
    SetParam(c, kParamChorusVoices, 3.f);
    SetParam(c, kParamDelayHeads, 2.f);
    std::vector<float> o;
    Render(c, 1.0, 0.f, &o);
    bool finite = true;
    for (float x : o) finite = finite && std::isfinite(x);
    CHECK(finite && Rms(o) > 0.02, "%s, ensemble chorus, multi-head delay: all running (rms %.3f)", engine ? "hall" : "plate", Rms(o));
  }
  AudioComponentInstanceDispose(c.au);
}

int main()
{
  // Keep test tape out of ~/Music.
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "underheard-au-test";
  std::filesystem::remove_all(dir);
  setenv("UNDERHEARD_TAPE_DIR", dir.c_str(), 1);

  Inst a = Make();
  CHECK(Render(a, 0.2, 1.f) < 0.6f, "renders with default settings");
  SetParam(a, kParamMix, 0.f); // input only
  Render(a, 0.5, 1.f);
  CHECK(Render(a, 1.0, 1.f) < 1e-6f, "Mix at input passes audio through unchanged");

  SetParam(a, kParamOutput, -6.f);
  const float g = std::pow(10.f, -6.f / 20.f);
  Render(a, 0.1, g);
  CHECK(Render(a, 0.5, g) < 1e-6f, "Output -6 dB scales the signal");

  CFPropertyListRef state = nullptr;
  UInt32 sz = sizeof state;
  CHECK(AudioUnitGetProperty(a.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &sz) == noErr && state, "state saves");
  Inst b = Make();
  CHECK(AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof state) == noErr, "state restores");
  CHECK(std::fabs(GetParam(b, kParamOutput) + 6.f) < 0.01f, "Output survives the round-trip (%.2f dB)", GetParam(b, kParamOutput));
  if (state) CFRelease(state);
  AudioComponentInstanceDispose(a.au);
  AudioComponentInstanceDispose(b.au);

  TestRecordAndPlay();
  TestLoopsAndPan();
  TestCatch();
  TestSaveAndReopen(dir);
  TestSidechain();
  TestSync();
  TestWearAndRestore();
  TestRecover();
  TestRazor();
  TestSend();
  TestOldProjects();
  TestDrifter();
  TestInputSend();
  TestFullerEffects();

  std::filesystem::remove_all(dir);
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
