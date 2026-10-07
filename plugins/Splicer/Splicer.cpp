#include "Splicer.h"
#include "IPlug_include_in_plug_src.h"

#include "AudioFile.h"
#include "ui/TapeView.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

using underheard::TapeLoop;
using underheard::TapeStorage;

namespace {

constexpr int32_t kStateMagic = 'SPLC';
constexpr int32_t kStateVersion = 3; // 2: splice positions from razor cuts; 3: parameter count up front

const char* StateName(TapeLoop::State s)
{
  switch (s)
  {
    case TapeLoop::State::Empty: return "empty";
    case TapeLoop::State::Recording: return "recording";
    case TapeLoop::State::Closing: return "splicing";
    case TapeLoop::State::Playing: return "playing";
    case TapeLoop::State::Overdubbing: return "overdubbing";
    case TapeLoop::State::Stopped: return "stopped";
    case TapeLoop::State::Clearing: return "clearing";
    case TapeLoop::State::Loading: return "loading";
    case TapeLoop::State::Editing: return "cutting";
  }
  return "";
}

double SpeedFromOctaves(double octaves) { return std::pow(2., octaves); }

// Copies `frames` frames between interleaved memory and chunked tape.
void CopyToTape(TapeStorage& t, const float* src, int64_t frames)
{
  for (int64_t k = 0; k < frames;)
  {
    const int64_t n = std::min(TapeStorage::kChunkFrames - (k & (TapeStorage::kChunkFrames - 1)), frames - k);
    std::memcpy(t.Frame(k), src + 2 * k, sizeof(float) * (size_t)(2 * n));
    k += n;
  }
}

void CopyFromTape(TapeStorage& t, float* dst, int64_t frames)
{
  for (int64_t k = 0; k < frames;)
  {
    const int64_t n = std::min(TapeStorage::kChunkFrames - (k & (TapeStorage::kChunkFrames - 1)), frames - k);
    std::memcpy(dst + 2 * k, t.Frame(k), sizeof(float) * (size_t)(2 * n));
    k += n;
  }
}

} // namespace

Splicer::Splicer(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
, mTapeDir(underheard::TapeDirectory("Splicer"))
{
#if IPLUG_DSP
  SetChannelLabel(ERoute::kInput, 0, "Main L");
  SetChannelLabel(ERoute::kInput, 1, "Main R");
  SetChannelLabel(ERoute::kInput, 2, "SideChain L");
  SetChannelLabel(ERoute::kInput, 3, "SideChain R");
#endif

  GetParam(kParamOutput)->InitGain("Output", 0., -70., 12.);
  GetParam(kParamMix)->InitPercentage("Mix (input-tape)", 50.);
  GetParam(kParamMotor)->InitSeconds("Motor", 0., 0., 2., 0.01);
  GetParam(kParamCatchLength)->InitSeconds("Catch Length", 8., 1., 30., 0.1);
  GetParam(kParamSync)->InitEnum("Sync", kSyncFree, {"Free", "Beats", "Bars"});
  GetParam(kParamHiss)->InitPercentage("Hiss", 10.);

  GetParam(kParamChorusOn)->InitBool("Chorus On", true, "", 0, "Effects");
  GetParam(kParamChorusRate)->InitDouble("Chorus Rate", 0.6, 0.05, 5., 0.01, "Hz", 0, "Effects", IParam::ShapeExp());
  GetParam(kParamChorusDepth)->InitPercentage("Chorus Depth", 40., 0., 100., 0, "Effects");
  GetParam(kParamChorusMix)->InitPercentage("Chorus Mix", 50., 0., 100., 0, "Effects");
  GetParam(kParamDelayOn)->InitBool("Delay On", true, "", 0, "Effects");
  GetParam(kParamDelayTime)->InitDouble("Delay Time", 380., 20., 2000., 1., "ms", 0, "Effects", IParam::ShapeExp());
  GetParam(kParamDelayFeedback)->InitPercentage("Delay Feedback", 35., 0., 95., 0, "Effects");
  GetParam(kParamDelayTone)->InitPercentage("Delay Tone", 50., 0., 100., 0, "Effects");
  GetParam(kParamDelayMix)->InitPercentage("Delay Mix", 40., 0., 100., 0, "Effects");
  GetParam(kParamReverbOn)->InitBool("Reverb On", true, "", 0, "Effects");
  GetParam(kParamReverbSize)->InitPercentage("Reverb Size", 60., 0., 100., 0, "Effects");
  GetParam(kParamReverbDecay)->InitDouble("Reverb Decay", 2.5, 0.2, 12., 0.01, "s", 0, "Effects", IParam::ShapeExp());
  GetParam(kParamReverbTone)->InitPercentage("Reverb Tone", 40., 0., 100., 0, "Effects");
  GetParam(kParamReverbMix)->InitPercentage("Reverb Mix", 50., 0., 100., 0, "Effects");
  GetParam(kParamFxWarmth)->InitPercentage("Effects Warmth", 50., 0., 100., 0, "Effects");
  GetParam(kParamChorusMode)->InitEnum("Chorus Mode", 0, {"Chorus", "Vibrato", "Ensemble"}, 0, "Effects");
  GetParam(kParamChorusVoices)->InitEnum("Chorus Voices", 1, {"1 voice", "2 voices", "3 voices", "4 voices"}, 0, "Effects");
  GetParam(kParamChorusFeedback)->InitDouble("Chorus Feedback", 0., -90., 90., 0.1, "%", 0, "Effects");
  {
    IParam* note = GetParam(kParamDelayNote);
    note->InitEnum("Delay Note", 0, underheard::fx::kNumBarOrLess + 1, "", 0, "Effects");
    note->SetDisplayText(0, "Free");
    for (int i = 0; i < underheard::fx::kNumBarOrLess; i++)
      note->SetDisplayText(i + 1, underheard::fx::NoteValues()[(size_t)i].name);
  }
  GetParam(kParamDelayHeads)->InitEnum("Delay Heads", 0, {"Single", "Ping-pong", "Multi-head"}, 0, "Effects");
  GetParam(kParamReverbEngine)->InitEnum("Reverb Engine", 1, {"Plate", "Hall"}, 0, "Effects");

  GetParam(kParamDriftOn)->InitBool("Drift", false, "", 0, "Drift");
  GetParam(kParamDriftLength)->InitDouble("Drift Length", 20., 1., 600., 0.1, "s", 0, "Drift", IParam::ShapeExp());
  GetParam(kParamDriftCurve)->InitEnum("Drift Curve", underheard::Drifter::kCurveSmooth, {"Linear", "Smooth", "Fast start", "Slow start"}, 0, "Drift");
  GetParam(kParamDriftSmear)->InitPercentage("Drift Smear", 30., 0., 100., 0, "Drift");
  GetParam(kParamDriftReach)->InitPercentage("Drift Reach", 30., 0., 100., 0, "Drift");
  GetParam(kParamDriftGravity)->InitPercentage("Drift Gravity", 30., 0., 100., 0, "Drift");
  GetParam(kParamDriftKeep)->InitBool("Drift Keep", false, "", 0, "Drift");
  GetParam(kParamDriftReturn)->InitBool("Drift Return", false, "", 0, "Drift");
  GetParam(kParamInputLevel)->InitPercentage("Input Level", 0., 0., 100., 0, "Input");
  GetParam(kParamInputSend)->InitPercentage("Input Send", 0., 0., 100., 0, "Input");
  for (int i = 0; i < kNumDriftTargets; i++)
  {
    mDriftHome[i] = 0.;
    mDriftNow[i] = 0.;
    mKeepValue[i] = 0.;
  }
  mFx.Prepare(48000.);

  for (int i = 0; i < kNumLoops; i++)
  {
    WDL_String group;
    group.SetFormatted(32, "Loop %d", i + 1);
    auto name = [i](const char* n) {
      WDL_String s;
      s.SetFormatted(64, "L%d %s", i + 1, n);
      return s;
    };
    const char* g = group.Get();
    GetParam(LoopParam(i, kLoopRecord))->InitBool(name("Record").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopPlay))->InitBool(name("Play").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopClear))->InitBool(name("Clear").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopCatch))->InitBool(name("Catch").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopSpeed))->InitDouble(name("Speed").Get(), 0., -2., 1., 0.001, "", 0, g, IParam::ShapeLinear(), IParam::kUnitCustom,
      [](double v, WDL_String& s) { s.SetFormatted(16, "%.2fx", SpeedFromOctaves(v)); });
    GetParam(LoopParam(i, kLoopReverse))->InitBool(name("Reverse").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopSource))->InitEnum(name("Source").Get(), kSourceMain, {"Main", "Sidechain"}, 0, g);
    GetParam(LoopParam(i, kLoopErase))->InitPercentage(name("Erase").Get(), 0., 0., 100., 0, g);
    GetParam(LoopParam(i, kLoopFeedback))->InitPercentage(name("Feedback").Get(), 100., 0., 100., 0, g);
    GetParam(LoopParam(i, kLoopSplice))->InitPercentage(name("Splice").Get(), 30., 0., 100., 0, g);
    GetParam(LoopParam(i, kLoopDry))->InitPercentage(name("Dry").Get(), 100., 0., 100., 0, g);
    GetParam(LoopParam(i, kLoopPan))->InitDouble(name("Pan").Get(), 0., -100., 100., 1., "", 0, g, IParam::ShapeLinear(), IParam::kUnitPan);
    GetParam(LoopParam(i, kLoopWear))->InitBool(name("Wear").Get(), true, "", 0, g);
    GetParam(LoopParam(i, kLoopWearRate))->InitPercentage(name("Wear Rate").Get(), 25., 0., 100., 0, g);
    GetParam(LoopParam(i, kLoopRestore))->InitBool(name("Restore").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopWow))->InitPercentage(name("Wow").Get(), 10., 0., 100., 0, g);
    GetParam(LoopParam(i, kLoopFlutter))->InitPercentage(name("Flutter").Get(), 10., 0., 100., 0, g);
    GetParam(LoopParam(i, kLoopRazor))->InitBool(name("Razor").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopCutReverse))->InitBool(name("Razor Reverse").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopCutRemove))->InitBool(name("Razor Remove").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopCutIsolate))->InitBool(name("Razor Isolate").Get(), false, "", 0, g);
    GetParam(LoopParam(i, kLoopSend))->InitPercentage(name("Send").Get(), 0., 0., 100., 0, g);
    GetParam(LoopParam(i, kLoopWearLimit))->InitPercentage(name("Wear Limit").Get(), 100., 0., 100., 0, g);
    GetParam(LoopParam(i, kLoopRecover))->InitBool(name("Recover").Get(), false, "", 0, g);

    Loop& l = mLoops[i];
    l.tapes[0] = std::make_unique<TapeStorage>(kMaxTapeFrames);
    l.tapes[1] = std::make_unique<TapeStorage>(kMaxTapeFrames);
    l.tapes[0]->Reserve(kTapeHeadroom);
    l.engine = std::make_unique<TapeLoop>(l.tapes[0].get());
    l.stageAge.assign(l.engine->SegmentCapacity(), 0.f);
    l.stageShed.assign(l.engine->SegmentCapacity(), 0.f);
  }

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) { BuildUI(pGraphics); };
#endif
}

Splicer::~Splicer() = default;

// ---- State -----------------------------------------------------------------------------

// State format 3 (2026-10-03): magic, version, parameter count, the parameters, then for each
// loop its saved tape (frames, hash, path) and splices. The count means a newer Splicer, with
// more parameters, still reads older projects.
//
// Formats 1 and 2 wrote the parameters first with no count (iPlug2's SerializeParams), then the
// magic and the loops. They're still read: the magic's position gives the count.
bool Splicer::SerializeState(IByteChunk& chunk) const
{
  // Make sure each loop's current tape is on disk; the project stores references to it.
  for (int i = 0; i < kNumLoops; i++)
  {
    const Loop& l = mLoops[i];
    if (l.inFlight < 0 && l.version.load() != l.savedVersion)
      SaveLoop(i);
  }

  chunk.Put(&kStateMagic);
  chunk.Put(&kStateVersion);
  const int32_t numParams = kNumParams;
  chunk.Put(&numParams);
  for (int i = 0; i < kNumParams; i++)
  {
    const double v = GetParam(i)->Value();
    chunk.Put(&v);
  }
  for (int i = 0; i < kNumLoops; i++)
  {
    const underheard::SavedTape& s = mLoops[i].saved;
    const int64_t frames = s.frames;
    const uint64_t hash = s.hash;
    chunk.Put(&frames);
    chunk.Put(&hash);
    chunk.PutStr(s.path.c_str());
    const TapeLoop& e = *mLoops[i].engine;
    const int32_t n = frames > 0 ? e.NumSplices() : 0;
    chunk.Put(&n);
    for (int k = 0; k < n; k++)
      chunk.Put(&e.Splices()[k]);
  }
  return true;
}

int Splicer::UnserializeState(const IByteChunk& chunk, int startPos)
{
  // Saved transport buttons must not start a take on load.
  mResyncTransport = true;

  // Where the parameters are, how many there are, and where the loops start (-1: no loops).
  int paramsPos = startPos, numParams = 0, loopsPos = -1;
  int32_t version = 0, first = 0;
  if (chunk.Get(&first, startPos) >= 0 && first == kStateMagic)
  {
    int p = chunk.Get(&version, startPos + 4);
    int32_t n = 0;
    p = p >= 0 ? chunk.Get(&n, p) : p;
    if (p < 0 || n < 0)
      return -1;
    paramsPos = p;
    numParams = n;
    loopsPos = p + 8 * n;
  }
  else
  {
    numParams = -1;
    for (int k = 0; k <= 4096 && startPos + 8 * k + 8 <= chunk.Size(); k++)
    {
      int32_t m = 0;
      if (chunk.Get(&m, startPos + 8 * k) >= 0 && m == kStateMagic)
      {
        numParams = k;
        break;
      }
    }
    if (numParams >= 0)
    {
      chunk.Get(&version, startPos + 8 * numParams + 4);
      loopsPos = startPos + 8 * numParams + 8;
    }
    else
      numParams = std::min<int>(kNumParams, (chunk.Size() - startPos) / 8); // parameters only
  }

  ENTER_PARAMS_MUTEX
  for (int i = 0; i < numParams && i < kNumParams; i++)
  {
    double v = 0.;
    if (chunk.Get(&v, paramsPos + 8 * i) >= 0)
      GetParam(i)->Set(v);
  }
  // Transport buttons describe what the loops were doing, not settings: reopened loops come
  // back stopped, so the buttons come back off.
  const int momentary[] = {kLoopRecord, kLoopPlay, kLoopClear, kLoopCatch, kLoopRestore, kLoopRazor, kLoopCutReverse, kLoopCutRemove, kLoopCutIsolate};
  for (int i = 0; i < kNumLoops; i++)
    for (int b : momentary)
      GetParam(LoopParam(i, b))->Set(0.);
  GetParam(kParamDriftKeep)->Set(0.);
  GetParam(kParamDriftReturn)->Set(0.);
  OnParamReset(kPresetRecall);
  LEAVE_PARAMS_MUTEX

  if (loopsPos < 0)
    return paramsPos + 8 * numParams;

  int p = loopsPos;
  for (int i = 0; i < kNumLoops && p >= 0; i++)
  {
    Loop& l = mLoops[i];
    underheard::SavedTape s;
    WDL_String path;
    p = chunk.Get(&s.frames, p);
    if (p >= 0) p = chunk.Get(&s.hash, p);
    if (p >= 0) p = chunk.GetStr(path, p);
    int32_t numSplices = 0;
    int64_t splices[TapeLoop::kMaxSplices] = {};
    if (p >= 0 && version >= 2)
    {
      p = chunk.Get(&numSplices, p);
      for (int k = 0; p >= 0 && k < numSplices; k++)
      {
        int64_t v = 0;
        p = chunk.Get(&v, p);
        if (k < TapeLoop::kMaxSplices)
          splices[k] = v;
      }
      numSplices = std::min<int32_t>(numSplices, TapeLoop::kMaxSplices);
    }
    if (p < 0)
    {
      SetMessage(i, "couldn't read this loop from the project", true);
      break;
    }
    s.path = path.Get();

    if (s.frames > 0)
    {
      std::vector<float> data;
      std::string error;
      if (underheard::LoadSavedTape(s, mTapeDir, data, error))
      {
        if (StartLoad(i, data.data(), s.frames, false, true, splices, numSplices))
        {
          l.saved = s;
          SetMessage(i, "restored from the project: press PLAY", true);
        }
      }
      else
      {
        SetMessage(i, "not restored: " + error, true);
        StartLoad(i, nullptr, 0, false, true);
        l.saved = {};
      }
    }
    else if (l.length.load() > 0 || l.inFlight >= 0)
    {
      StartLoad(i, nullptr, 0, false, true); // the project has this loop empty
      l.saved = {};
    }
  }
  return p >= 0 ? p : loopsPos;
}

// ---- Main thread -----------------------------------------------------------------------

void Splicer::SetParamFromPlugin(int paramIdx, double value)
{
  const double norm = GetParam(paramIdx)->ToNormalized(value);
  BeginInformHostOfParamChangeFromUI(paramIdx);
  SendParameterValueFromUI(paramIdx, norm);
  EndInformHostOfParamChangeFromUI(paramIdx);
#if IPLUG_EDITOR
  if (GetUI())
    GetUI()->ForControlWithParam(paramIdx, [norm](IControl* pControl) { pControl->SetValueFromDelegate(norm); });
#endif
}

void Splicer::SetMessage(int i, const std::string& msg, bool sticky)
{
  mLoops[i].message = msg;
  mLoops[i].messageSticky = sticky;
  mLoops[i].messageAt = Clock::now();
}

// The loop's idle tape, ready to be filled, or -1 if a switch is still under way.
int Splicer::PrepareIdleTape(int i)
{
  Loop& l = mLoops[i];
  if (l.inFlight >= 0)
  {
    if (l.loadTape.exchange(-1) >= 0)
      l.inFlight = -1; // the audio thread never took it: reuse that tape
    else
      FinishLoads(i);
    if (l.inFlight >= 0)
    {
      SetMessage(i, "busy, try again");
      return -1;
    }
  }
  return 1 - l.activeTape.load();
}

// Hands `frames` of finished tape to the audio thread through the loop's idle tape.
bool Splicer::StartLoad(int i, const float* stereo, int64_t frames, bool play, bool fromSave, const int64_t* splices, int numSplices)
{
  Loop& l = mLoops[i];
  const int target = PrepareIdleTape(i);
  if (target < 0)
    return false;
  TapeStorage& t = *l.tapes[target];
  frames = std::min(frames, kMaxTapeFrames);
  t.Reserve(std::max(frames, kTapeHeadroom)); // room for a new take after a Clear
  if (stereo && frames > 0)
    CopyToTape(t, stereo, frames);

  l.loadLength = frames;
  l.loadPlay = play;
  l.loadIsEdit = false;
  l.stageNumSplices = std::min(numSplices, TapeLoop::kMaxSplices);
  for (int k = 0; k < l.stageNumSplices; k++)
    l.stageSplices[k] = splices[k];
  l.inFlight = target;
  l.inFlightIsSaved = fromSave;
  l.loadTape.store(target, std::memory_order_release);
  return true;
}

// Once the audio thread has switched tapes, frees the old one.
void Splicer::FinishLoads(int i)
{
  Loop& l = mLoops[i];
  if (l.inFlight >= 0 && l.loadRefused.exchange(false))
  {
    // The loop changed before the cut arrived (cleared, say); the tape was never used.
    l.tapes[l.inFlight]->Release();
    l.inFlight = -1;
    return;
  }
  if (l.inFlight < 0 || l.loadTape.load() >= 0 || l.activeTape.load() != l.inFlight)
    return;
  if (l.inFlightIsSaved)
    l.savedVersion = l.version.load();
  l.tapes[1 - l.inFlight]->Release();
  l.inFlight = -1;
}

void Splicer::DoCatch(int i, int64_t at, int source, double speed)
{
  const double fs = mCaptureRate;
  const underheard::CaptureBuffer& cap = mCapture[source];
  if (fs <= 0. || cap.Capacity() == 0)
    return;

  // Catch as if recorded at the loop's current speed, so it plays back at the pitch it was
  // heard. Take a little extra from before the start to crossfade the seam.
  const double ratio = kTapeRate * speed / fs; // tape frames per host frame
  const int64_t want = (int64_t)(GetParam(kParamCatchLength)->Value() * fs);
  const int64_t pre = (int64_t)std::ceil(TapeLoop::kSeamFrames / ratio) + 8;
  const int64_t count = std::min({want + pre, at, cap.Capacity()});
  std::vector<float> seg;
  if (count < pre + (int64_t)(0.1 * fs) || !cap.CopyRange(at, count, seg))
  {
    SetMessage(i, "nothing to catch yet");
    return;
  }

  std::vector<float> tape = underheard::ResampleStereo(seg.data(), count, ratio);
  const int64_t total = (int64_t)tape.size() / 2;
  const int64_t preT = std::min<int64_t>(total, (int64_t)std::llround((double)pre * ratio));
  const int64_t len = std::min(total - preT, kMaxTapeFrames);
  if (len < TapeLoop::kMinLoopFrames)
  {
    SetMessage(i, "nothing to catch yet");
    return;
  }

  std::vector<float> loop(tape.begin() + 2 * preT, tape.begin() + 2 * (preT + len));
  // The end fades into what came just before the start, so the seam is continuous.
  const int64_t seam = std::min<int64_t>({TapeLoop::kSeamFrames, preT, len});
  for (int64_t j = 0; j < seam; j++)
  {
    const float w = (float)(j + 1) / (float)seam;
    for (int c = 0; c < 2; c++)
    {
      float& dst = loop[(size_t)(2 * (len - seam + j) + c)];
      dst = dst * (1.f - w) + tape[(size_t)(2 * (preT - seam + j) + c)] * w;
    }
  }

  if (StartLoad(i, loop.data(), len, true, false))
  {
    char msg[64];
    std::snprintf(msg, sizeof msg, "caught %.1f s", (double)len / kTapeRate / speed);
    SetMessage(i, msg);
  }
}

// Builds the cut loop on the idle tape (with wear and splices carried across) and hands it to
// the audio thread.
void Splicer::DoCut(int i, int op, int64_t in, int64_t out, int dir, int64_t length)
{
  Loop& l = mLoops[i];
  const int64_t L = l.length.load();
  if (L <= 0 || L != length)
  {
    SetMessage(i, "the loop changed; cut cancelled");
    return;
  }
  // The cut runs from the first mark to the second in the direction the tape was moving.
  const int64_t start = dir > 0 ? in : out;
  const int64_t len = (((dir > 0 ? out - in : in - out) % L) + L) % L;
  const int64_t minLen = TapeLoop::kMinLoopFrames;
  const underheard::TapeEdit e = op == kCutRemove ? underheard::MakeRemove(L, start, len, minLen)
                               : op == kCutIsolate ? underheard::MakeIsolate(L, start, len, minLen)
                                                   : underheard::MakeReverse(L, start, len);
  if (e.count == 0)
  {
    SetMessage(i, op == kCutRemove && len > 0 ? "can't remove that much" : "cut too short");
    return;
  }
  const int target = PrepareIdleTape(i);
  if (target < 0)
    return;

  const int64_t n = e.NewLength();
  TapeStorage& to = *l.tapes[target];
  to.Reserve(std::max(n, kTapeHeadroom));
  underheard::RenderEdit(e, *l.tapes[l.activeTape.load()], to, TapeLoop::kJoinFrames);

  // Wear damage follows the audio, segment by segment.
  const TapeLoop& engine = *l.engine;
  const int64_t seg = TapeLoop::kSegmentFrames;
  const size_t segments = std::min(l.stageAge.size(), (size_t)((n + seg - 1) / seg));
  for (size_t s = 0; s < segments; s++)
  {
    const int64_t c = std::min<int64_t>((int64_t)s * seg + seg / 2, n - 1);
    const size_t src = std::min(l.stageAge.size() - 1, (size_t)(e.SourceFrame(c) / seg));
    l.stageAge[s] = engine.AgeData()[src];
    l.stageShed[s] = engine.ShedData()[src];
  }

  // Splices: the old seam and earlier cuts that survive, plus the new joins (the join at 0 is
  // the new seam).
  std::vector<int64_t> sp;
  auto add = [&](double p) {
    if (p > TapeLoop::kSpliceFrames && p < (double)(n - TapeLoop::kSpliceFrames))
      sp.push_back((int64_t)std::llround(p));
  };
  double np = 0.;
  if (e.Map(0., np))
    add(np);
  for (int k = 0; k < engine.NumSplices(); k++)
    if (e.Map((double)engine.Splices()[k], np))
      add(np);
  for (int k = 0; k < e.count; k++)
    if (e.StartsJoin(k) && e.PieceStart(k) > 0)
      add((double)e.PieceStart(k));
  std::sort(sp.begin(), sp.end());
  l.stageNumSplices = 0;
  for (int64_t p : sp)
    if (l.stageNumSplices < TapeLoop::kMaxSplices && (l.stageNumSplices == 0 || p - l.stageSplices[l.stageNumSplices - 1] > TapeLoop::kSpliceFrames))
      l.stageSplices[l.stageNumSplices++] = p;

  l.stageEdit = e;
  l.loadIsEdit = true;
  l.inFlight = target;
  l.inFlightIsSaved = false;
  l.loadTape.store(target, std::memory_order_release);

  char msg[64];
  const char* what = op == kCutRemove ? "removed" : op == kCutIsolate ? "isolated" : "flipped";
  std::snprintf(msg, sizeof msg, "%s %.2f s", what, (double)len / kTapeRate);
  SetMessage(i, msg);
}

void Splicer::LoadWavDialog(int i)
{
#if IPLUG_EDITOR
  if (!GetUI())
    return;
  mDialogFile.Set("");
  GetUI()->PromptForFile(mDialogFile, mDialogPath, EFileAction::Open, "wav", [this, i](const WDL_String& file, const WDL_String&) {
    if (file.GetLength())
      LoadWavFile(i, file.Get());
  });
#endif
}

void Splicer::LoadWavFile(int i, const std::string& path)
{
  underheard::AudioData a;
  std::string error;
  if (!underheard::ReadWav(path, a, error))
  {
    SetMessage(i, "can't load: " + error);
    return;
  }
  std::vector<float> tape = underheard::ResampleStereo(a.stereo.data(), a.frames, kTapeRate / a.sampleRate);
  const int64_t total = (int64_t)tape.size() / 2;
  const int64_t frames = std::min(total, kMaxTapeFrames);
  if (frames < TapeLoop::kMinLoopFrames)
  {
    SetMessage(i, "can't load: too short");
    return;
  }
  if (StartLoad(i, tape.data(), frames, true, false))
  {
    const std::string name = std::filesystem::u8path(path).filename().u8string();
    SetMessage(i, (frames < total ? "loaded (cut to 4:00) " : "loaded ") + name);
  }
}

// Writes the loop's current tape to the tape folder (if it isn't there already).
bool Splicer::SaveLoop(int i) const
{
  Loop& l = mLoops[i];
  const uint64_t v = l.version.load();
  const int64_t len = l.length.load();
  if (len <= 0)
  {
    l.saved = {};
    l.savedVersion = v;
    return true;
  }
  TapeStorage& t = *l.tapes[l.activeTape.load()];
  if (t.CapacityFrames() < len)
    return false;
  std::vector<float> data((size_t)(2 * len));
  CopyFromTape(t, data.data(), len);

  underheard::SavedTape s;
  std::string error;
  if (!underheard::SaveTape(mTapeDir, data.data(), len, kTapeRate, s, error))
  {
    l.message = "couldn't save: " + error;
    l.messageAt = Clock::now();
    return false;
  }
  l.saved = s;
  l.savedVersion = v;
  return true;
}

void Splicer::SyncFixes(int i)
{
  // Show what the engine did on its own (a take closing at the end of the tape, Play pressed
  // on an empty loop, Clear and Catch springing back).
  Loop& l = mLoops[i];
  std::atomic<int>* fixes[] = {&l.fixRecord, &l.fixPlay, &l.fixClear, &l.fixCatch, &l.fixRestore,
                               &l.fixRazor, &l.fixCut[kCutReverse], &l.fixCut[kCutRemove], &l.fixCut[kCutIsolate]};
  const int params[] = {kLoopRecord, kLoopPlay, kLoopClear, kLoopCatch, kLoopRestore,
                        kLoopRazor, kLoopCutReverse, kLoopCutRemove, kLoopCutIsolate};
  for (int f = 0; f < 9; f++)
  {
    const int v = fixes[f]->exchange(-1);
    if (v >= 0 && GetParam(LoopParam(i, params[f]))->Bool() != (v != 0))
      SetParamFromPlugin(LoopParam(i, params[f]), v);
  }
}

void Splicer::OnIdle()
{
  mInputPeakSender.TransmitData(*this);
  mOutputPeakSender.TransmitData(*this);

  for (int i = 0; i < kNumLoops; i++)
  {
    Loop& l = mLoops[i];
    SyncFixes(i);
    FinishLoads(i);

    // Keep tape allocated ahead of the record head.
    if (l.inFlight < 0)
      l.tapes[l.activeTape.load()]->Reserve(l.framesInUse.load() + kTapeHeadroom);

    const int64_t at = l.catchAt.exchange(-1);
    if (at >= 0)
      DoCatch(i, at, l.catchSource.load(), l.catchSpeed.load());

    const int op = l.cutOp.load(std::memory_order_acquire);
    if (op != kCutNone)
    {
      DoCut(i, op, l.cutIn.load(), l.cutOut.load(), l.cutDir.load(), l.cutLength.load());
      l.cutOp = kCutNone;
    }
    if (l.cutNeedsMarks.exchange(false))
      SetMessage(i, "mark the cut with RAZOR first (start, then end)");
  }

  // Drifter's Keep: move the settings to where they've drifted (the audio thread holds those
  // values until the host has them).
  if (mKeepPending.exchange(false))
    for (int i = 0; i < kNumDriftTargets; i++)
      SetParamFromPlugin(kDriftTargets[i], GetParam(kDriftTargets[i])->FromNormalized(mKeepValue[i].load()));
  const int keepFix = mFixDriftKeep.exchange(-1), returnFix = mFixDriftReturn.exchange(-1);
  if (keepFix >= 0 && GetParam(kParamDriftKeep)->Bool())
    SetParamFromPlugin(kParamDriftKeep, 0.);
  if (returnFix >= 0 && GetParam(kParamDriftReturn)->Bool())
    SetParamFromPlugin(kParamDriftReturn, 0.);

  UpdateStatus();
}

#if IPLUG_EDITOR
// Strips across the top (one per loop, always visible), the selected loop's full controls
// below them, and the master row at the bottom.
void Splicer::BuildUI(IGraphics* g)
{
  using namespace splicer_ui;
  g->AttachCornerResizer(EUIResizerMode::Scale, false);
  g->AttachPanelBackground(theme::kBackground);
  g->LoadFont("Roboto-Regular", ROBOTO_FN);

  const IVStyle style = DEFAULT_STYLE.WithColor(kBG, COLOR_TRANSPARENT)
                                     .WithColor(kFG, IColor(255, 70, 64, 58))
                                     .WithColor(kPR, theme::kAccent)
                                     .WithColor(kFR, IColor(255, 110, 100, 90))
                                     .WithColor(kHL, IColor(40, 255, 255, 255))
                                     .WithColor(kSH, IColor(60, 0, 0, 0))
                                     .WithColor(kX1, theme::kAccent)
                                     .WithLabelText(IText(13.f, theme::kText, "Roboto-Regular"))
                                     .WithValueText(IText(12.f, theme::kTextDim, "Roboto-Regular"))
                                     .WithDrawShadows(false)
                                     .WithRoundness(0.2f);
  const IVStyle recStyle = style.WithColor(kPR, theme::kRecord);
  const IVStyle playStyle = style.WithColor(kPR, theme::kPlay);
  const IText rowLabel(13.f, theme::kTextDim, "Roboto-Regular", EAlign::Near);
  const IText hint(12.f, theme::kTextDim, "Roboto-Regular", EAlign::Near);

  IRECT b = g->GetBounds().GetPadded(-12.f);
  IRECT strips = b.ReduceFromTop(4 * 78.f);
  b.ReduceFromTop(3.f);
  const IRECT inputStrip = b.ReduceFromTop(56.f);
  b.ReduceFromTop(10.f);
  IRECT focus = b.ReduceFromTop(304.f);
  b.ReduceFromTop(10.f);
  IRECT master = b;

  // ---- Strips
  for (int i = 0; i < kNumLoops; i++)
  {
    const IRECT strip = strips.GetGridCell(i, 0, kNumLoops, 1).GetPadded(0.f, -3.f, 0.f, -3.f);
    g->AttachControl(new StripPanel(strip, [this, i] { return mSelectedLoop == i; }, [this, i] { SelectLoop(i); }));
    IRECT inner = strip.GetPadded(-6.f);
    const IRECT knobs = inner.ReduceFromRight(128.f);
    const IRECT buttons = inner.ReduceFromRight(3 * 78.f);
    inner.ReduceFromRight(8.f);
    g->AttachControl(new TapeView(inner, [this, i] { SelectLoop(i); }, [this, i](double in, double out) { SetMarksFromUI(i, in, out); }), kCtrlTagTape + i);
    g->AttachControl(new IVToggleControl(buttons.GetGridCell(0, 1, 3).GetPadded(-4.f), LoopParam(i, kLoopRecord), "", recStyle, "REC", "REC"));
    g->AttachControl(new IVToggleControl(buttons.GetGridCell(1, 1, 3).GetPadded(-4.f), LoopParam(i, kLoopPlay), "", playStyle, "PLAY", "PLAY"));
    g->AttachControl(new IVToggleControl(buttons.GetGridCell(2, 1, 3).GetPadded(-4.f), LoopParam(i, kLoopCatch), "", style, "CATCH", "CATCH"));
    g->AttachControl(new IVKnobControl(knobs.GetGridCell(0, 0, 1, 2), LoopParam(i, kLoopDry), "Level", style));
    g->AttachControl(new IVKnobControl(knobs.GetGridCell(0, 1, 1, 2), LoopParam(i, kLoopSend), "Send", style));
  }

  // ---- Input strip: the live input as a source, with the same Level and Send as a loop.
  {
    g->AttachControl(new IPanelControl(inputStrip, theme::kPanel));
    IRECT inner = inputStrip.GetPadded(-6.f);
    const IRECT knobs = inner.ReduceFromRight(128.f);
    g->AttachControl(new IVKnobControl(knobs.GetGridCell(0, 0, 1, 2), kParamInputLevel, "Level", style));
    g->AttachControl(new IVKnobControl(knobs.GetGridCell(0, 1, 1, 2), kParamInputSend, "Send", style));
    g->AttachControl(new ITextControl(inner.GetFromLeft(70.f), "INPUT", IText(15.f, theme::kText, "Roboto-Regular", EAlign::Near)));
    g->AttachControl(new ITextControl(inner.GetReducedFromLeft(76.f), "the live input: Level puts it on the Tape side with the loops, Send feeds the effects", hint));
  }

  // ---- Focus panel: every loop's controls are here, and only the selected loop's are shown.
  g->AttachControl(new IPanelControl(focus, theme::kPanel));
  IRECT f = focus.GetPadded(-10.f);
  IRECT titleRow = f.ReduceFromTop(26.f);
  g->AttachControl(new IVButtonControl(titleRow.ReduceFromRight(120.f).GetPadded(0.f, 0.f, 0.f, -2.f), [this](IControl* c) {
    SplashClickActionFunc(c);
    SelectLoop(kFocusDrifter);
  }, "DRIFT", style));
  titleRow.ReduceFromRight(8.f);
  g->AttachControl(new IVButtonControl(titleRow.ReduceFromRight(120.f).GetPadded(0.f, 0.f, 0.f, -2.f), [this](IControl* c) {
    SplashClickActionFunc(c);
    SelectLoop(kFocusEffects);
  }, "EFFECTS", style));
  g->AttachControl(new ITextControl(titleRow, "", IText(16.f, theme::kText, "Roboto-Regular", EAlign::Near)), kCtrlTagFocusTitle);
  IRECT rows[4];
  for (int r = 0; r < 4; r++)
  {
    rows[r] = f.GetGridCell(r, 0, 4, 1);
    g->AttachControl(new ITextControl(rows[r].GetFromLeft(70.f), "", rowLabel), kCtrlTagRowLabel + r);
    rows[r].ReduceFromLeft(76.f);
  }
  auto cell = [&](int row, int col, int span = 1) {
    IRECT c = rows[row].GetGridCell(0, col, 1, 7);
    for (int k = 1; k < span; k++)
      c = c.Union(rows[row].GetGridCell(0, col + k, 1, 7));
    return c.GetPadded(-4.f);
  };
  auto button = [&](const IRECT& r) { return r.GetMidVPadded(18.f); };

  for (int i = 0; i < kNumLoops; i++)
  {
    WDL_String grp;
    grp.SetFormatted(16, "loop%d", i);
    const char* gr = grp.Get();
    auto P = [i](int p) { return LoopParam(i, p); };

    g->AttachControl(new IVKnobControl(cell(0, 0), P(kLoopSpeed), "Speed", style), kNoTag, gr);
    g->AttachControl(new IVToggleControl(button(cell(0, 1)), P(kLoopReverse), "", style, "FWD", "REV"), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 2), P(kLoopErase), "Erase", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 3), P(kLoopFeedback), "Feedback", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 4), P(kLoopSplice), "Splice", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 5), P(kLoopWow), "Wow", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 6), P(kLoopFlutter), "Flutter", style), kNoTag, gr);

    g->AttachControl(new IVToggleControl(button(cell(1, 0)), P(kLoopWear), "", style, "WEAR OFF", "WEAR ON"), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(1, 1), P(kLoopWearRate), "Rate", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(1, 2), P(kLoopWearLimit), "Limit", style), kNoTag, gr);
    g->AttachControl(new IVToggleControl(button(cell(1, 3)), P(kLoopRecover), "", style, "HOLD", "RECOVER"), kNoTag, gr);
    g->AttachControl(new IVToggleControl(button(cell(1, 4)), P(kLoopRestore), "", style, "RESTORE", "RESTORE"), kNoTag, gr);
    g->AttachControl(new ITextControl(cell(1, 5, 2), "at the Limit: HOLD there, or RECOVER back to new and wear again", hint), kNoTag, gr);

    g->AttachControl(new IVToggleControl(button(cell(2, 0)), P(kLoopRazor), "", style, "MARK", "MARK"), kNoTag, gr);
    g->AttachControl(new IVToggleControl(button(cell(2, 1)), P(kLoopCutReverse), "", style, "FLIP", "FLIP"), kNoTag, gr);
    g->AttachControl(new IVToggleControl(button(cell(2, 2)), P(kLoopCutRemove), "", style, "REMOVE", "REMOVE"), kNoTag, gr);
    g->AttachControl(new IVToggleControl(button(cell(2, 3)), P(kLoopCutIsolate), "", style, "ISOLATE", "ISOLATE"), kNoTag, gr);
    g->AttachControl(new ITextControl(cell(2, 4, 3), "drag on the tape to mark a cut (double-click clears), or MARK at the play head", hint), kNoTag, gr);

    g->AttachControl(new IVKnobControl(cell(3, 0), P(kLoopPan), "Pan", style), kNoTag, gr);
    g->AttachControl(new IVTabSwitchControl(button(cell(3, 1, 2)), P(kLoopSource), {"Main", "Sidechain"}, "", style), kNoTag, gr);
    g->AttachControl(new IVButtonControl(button(cell(3, 3)), [this, i](IControl* c) {
      SplashClickActionFunc(c);
      LoadWavDialog(i);
    }, "LOAD", style), kNoTag, gr);
    g->AttachControl(new IVToggleControl(button(cell(3, 4)), P(kLoopClear), "", style, "CLEAR", "CLEAR"), kNoTag, gr);
  }

  // ---- The effects chain, shown in the focus panel instead of a loop.
  {
    const char* gr = "fx";
    // Chorus (underheard-chorus's engine)
    g->AttachControl(new IVToggleControl(button(cell(0, 0)), kParamChorusOn, "", style, "CHORUS", "CHORUS"), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 1), kParamChorusRate, "Rate", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 2), kParamChorusDepth, "Depth", style), kNoTag, gr);
    g->AttachControl(new IVMenuButtonControl(button(cell(0, 3)), kParamChorusMode, "", style), kNoTag, gr);
    g->AttachControl(new IVMenuButtonControl(button(cell(0, 4)), kParamChorusVoices, "", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 5), kParamChorusFeedback, "Feedback", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 6), kParamChorusMix, "Mix", style), kNoTag, gr);

    // Delay (underheard-delay's engine)
    g->AttachControl(new IVToggleControl(button(cell(1, 0)), kParamDelayOn, "", style, "DELAY", "DELAY"), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(1, 1), kParamDelayTime, "Time", style), kNoTag, gr);
    g->AttachControl(new IVMenuButtonControl(button(cell(1, 2)), kParamDelayNote, "", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(1, 3), kParamDelayFeedback, "Feedback", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(1, 4), kParamDelayTone, "Tone", style), kNoTag, gr);
    g->AttachControl(new IVMenuButtonControl(button(cell(1, 5)), kParamDelayHeads, "", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(1, 6), kParamDelayMix, "Mix", style), kNoTag, gr);

    // Reverb (underheard-reverb's Plate and Hall)
    g->AttachControl(new IVToggleControl(button(cell(2, 0)), kParamReverbOn, "", style, "REVERB", "REVERB"), kNoTag, gr);
    g->AttachControl(new IVTabSwitchControl(button(cell(2, 1)), kParamReverbEngine, {"Plate", "Hall"}, "", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(2, 2), kParamReverbSize, "Size", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(2, 3), kParamReverbDecay, "Decay", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(2, 4), kParamReverbTone, "Tone", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(2, 6), kParamReverbMix, "Mix", style), kNoTag, gr);

    g->AttachControl(new CheckboxControl(cell(3, 0).GetMidVPadded(12.f), kParamDriftOn, "Drift"), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(3, 1), kParamFxWarmth, "Warmth", style), kNoTag, gr);
    g->AttachControl(new ITextControl(cell(3, 2, 5), "The Sends (loops and input) feed this chain; it's heard on the Tape side of Mix. Drift slowly moves these settings (shape it in DRIFT).", hint), kNoTag, gr);
  }

  // ---- Drifter, also shown in the focus panel.
  {
    const char* gr = "drift";
    g->AttachControl(new IVKnobControl(cell(0, 0), kParamDriftLength, "Length", style), kNoTag, gr);
    g->AttachControl(new IVTabSwitchControl(button(cell(0, 1, 3)), kParamDriftCurve, {"Linear", "Smooth", "Fast start", "Slow start"}, "", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(0, 4), kParamDriftSmear, "Smear", style), kNoTag, gr);
    g->AttachControl(new CheckboxControl(cell(0, 5).GetMidVPadded(12.f), kParamDriftOn, "Drift"), kNoTag, gr);

    g->AttachControl(new IVKnobControl(cell(1, 0), kParamDriftReach, "Reach", style), kNoTag, gr);
    g->AttachControl(new IVKnobControl(cell(1, 1), kParamDriftGravity, "Gravity", style), kNoTag, gr);
    g->AttachControl(new IVToggleControl(button(cell(1, 2)), kParamDriftKeep, "", style, "KEEP", "KEEP"), kNoTag, gr);
    g->AttachControl(new IVToggleControl(button(cell(1, 3)), kParamDriftReturn, "", style, "RETURN", "RETURN"), kNoTag, gr);
    g->AttachControl(new ITextControl(cell(1, 4, 2), "KEEP: where it has drifted becomes your settings. RETURN: glide home.", hint), kNoTag, gr);

    g->AttachControl(new DriftView(rows[2].Union(rows[3]).GetPadded(-4.f)), kCtrlTagDriftView, gr);
  }

  // ---- Master row
  const IVStyle meterStyle = style.WithColor(kFG, IColor(255, 70, 64, 58)).WithColor(kX1, theme::kAccent);
  IRECT meters = master.ReduceFromRight(240.f);
  g->AttachControl(new IVPeakAvgMeterControl<4>(meters.FracRectVertical(0.5, true).GetPadded(-2.f), "In / SC", meterStyle, EDirection::Horizontal, {"L", "R", "SC L", "SC R"}), kCtrlTagInputMeter);
  g->AttachControl(new IVPeakAvgMeterControl<2>(meters.FracRectVertical(0.5, false).GetPadded(-2.f), "Out", meterStyle, EDirection::Horizontal, {"L", "R"}), kCtrlTagOutputMeter);
  const int masterKnobs[] = {kParamMix, kParamMotor, kParamCatchLength, kParamHiss, kParamOutput};
  const char* masterLabels[] = {"Input / Tape", "Motor", "Catch", "Hiss", "Output"};
  for (int k = 0; k < 5; k++)
    g->AttachControl(new IVKnobControl(master.GetGridCell(0, k, 1, 7).GetPadded(-2.f), masterKnobs[k], masterLabels[k], style));
  g->AttachControl(new IVTabSwitchControl(master.GetGridCell(0, 5, 1, 7).Union(master.GetGridCell(0, 6, 1, 7)).GetMidVPadded(24.f).GetPadded(-4.f),
                                          kParamSync, {"Free", "Beats", "Bars"}, "Sync", style));

  SelectLoop(mSelectedLoop);
}

void Splicer::SetMarksFromUI(int i, double in, double out)
{
  Loop& l = mLoops[i];
  l.uiMarkIn = in < 0. ? -1 : (int64_t)std::llround(in);
  l.uiMarkOut = out < 0. ? -1 : (int64_t)std::llround(out);
  l.uiMarkSeq.fetch_add(1, std::memory_order_release);
}

void Splicer::SelectLoop(int loop)
{
  mSelectedLoop = loop;
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  for (int i = 0; i < kNumLoops; i++)
  {
    WDL_String grp;
    grp.SetFormatted(16, "loop%d", i);
    ui->ForControlInGroup(grp.Get(), [hide = i != loop](IControl* c) { c->Hide(hide); });
  }
  const bool fx = loop == kFocusEffects, drift = loop == kFocusDrifter;
  ui->ForControlInGroup("fx", [fx](IControl* c) { c->Hide(!fx); });
  ui->ForControlInGroup("drift", [drift](IControl* c) { c->Hide(!drift); });
  if (auto* title = static_cast<ITextControl*>(ui->GetControlWithTag(kCtrlTagFocusTitle)))
  {
    WDL_String t;
    if (fx)
      t.Set("EFFECTS");
    else if (drift)
      t.Set("DRIFT  (moves the effects)");
    else
      t.SetFormatted(16, "LOOP %d", loop + 1);
    title->SetStr(t.Get());
  }
  const char* loopRows[] = {"TAPE", "WEAR", "RAZOR", "OUT"};
  const char* fxRows[] = {"CHORUS", "DELAY", "REVERB", ""};
  const char* driftRows[] = {"MOTION", "RANGE", "WHERE", ""};
  for (int r = 0; r < 4; r++)
    if (auto* label = static_cast<ITextControl*>(ui->GetControlWithTag(kCtrlTagRowLabel + r)))
      label->SetStr(fx ? fxRows[r] : drift ? driftRows[r] : loopRows[r]);
  ui->SetAllControlsDirty();
}
#endif

void Splicer::UpdateStatus()
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  if (auto* dv = static_cast<splicer_ui::DriftView*>(ui->GetControlWithTag(kCtrlTagDriftView)); dv && mSelectedLoop == kFocusDrifter)
  {
    static const char* names[kNumDriftTargets] = {"Rate", "Depth", "Mix", "Time", "Feedback", "Tone", "Mix", "Size", "Decay", "Tone", "Mix"};
    splicer_ui::DriftViewData d;
    d.enabled = GetParam(kParamDriftOn)->Bool();
    d.progress = mDriftProgress.load();
    d.groups = {{0, 3, "CHORUS"}, {3, 4, "DELAY"}, {7, 4, "REVERB"}};
    for (int i = 0; i < kNumDriftTargets; i++)
    {
      d.home.push_back(mDriftHome[i].load());
      d.now.push_back(mDriftNow[i].load());
      d.names.push_back(names[i]);
    }
    dv->SetData(d);
  }

  constexpr int kBins = 160;
  const auto now = Clock::now();
  for (int i = 0; i < kNumLoops; i++)
  {
    Loop& l = mLoops[i];
    auto* view = static_cast<splicer_ui::TapeView*>(ui->GetControlWithTag(kCtrlTagTape + i));
    if (!view)
      continue;

    const auto st = (TapeLoop::State)l.state.load();
    splicer_ui::TapeViewData d;
    d.length = (double)l.length.load();
    d.empty = st == TapeLoop::State::Empty || d.length <= 0.;
    d.recording = st == TapeLoop::State::Recording || st == TapeLoop::State::Closing || st == TapeLoop::State::Overdubbing;
    d.playing = st == TapeLoop::State::Playing;
    d.position = l.position.load();
    d.markIn = (double)l.markInShown.load();
    d.markOut = (double)l.markOutShown.load();

    WDL_String s;
    if (st == TapeLoop::State::Empty)
      s.SetFormatted(64, "%d   empty: REC, CATCH or LOAD", i + 1);
    else if (st == TapeLoop::State::Recording)
      s.SetFormatted(64, "%d   recording  %.1f s", i + 1, d.position / kTapeRate);
    else
    {
      const auto phase = (TapeLoop::WearPhase)l.wearPhase.load();
      s.SetFormatted(128, "%d   %s   %.1f / %.1f s   %.2fx   worn %d%s", i + 1, StateName(st), d.position / kTapeRate, d.length / kTapeRate,
                     std::fabs(l.speed.load()), l.passes.load(),
                     phase == TapeLoop::WearPhase::Recovering ? ", recovering" : phase == TapeLoop::WearPhase::Holding ? ", holding" : "");
    }
    d.status = s.Get();
    // Most messages fade after 8 s. Sticky ones (about restoring a project) stay until the loop
    // is played or recorded, so they're there whenever the window is opened.
    const bool engaged = st == TapeLoop::State::Playing || st == TapeLoop::State::Recording || st == TapeLoop::State::Overdubbing;
    if (!l.message.empty() && (l.messageSticky ? engaged : now - l.messageAt > std::chrono::seconds(8)))
      l.message.clear();
    d.message = l.message;

    // Damage along the tape, read straight from the engine (a slightly stale view is fine).
    if (!d.empty && l.inFlight < 0)
    {
      const TapeLoop& e = *l.engine;
      const int64_t segs = std::min<int64_t>((int64_t)e.SegmentCapacity(), ((int64_t)d.length + TapeLoop::kSegmentFrames - 1) / TapeLoop::kSegmentFrames);
      d.age.assign(kBins, 0.f);
      d.shed.assign(kBins, 0.f);
      for (int bin = 0; bin < kBins; bin++)
      {
        const int64_t s0 = segs * bin / kBins, s1 = std::max(s0 + 1, segs * (bin + 1) / kBins);
        float a = 0.f, sh = 0.f;
        for (int64_t k = s0; k < s1 && k < segs; k++)
        {
          a += e.AgeData()[k];
          sh = std::max(sh, e.ShedData()[k]);
        }
        d.age[(size_t)bin] = a / (float)(s1 - s0);
        d.shed[(size_t)bin] = sh;
      }
      for (int k = 0; k < e.NumSplices(); k++)
        d.splices.push_back((double)e.Splices()[k]);
    }
    view->SetData(d);
  }
#endif
}

// ---- Audio thread ----------------------------------------------------------------------

#if IPLUG_DSP
void Splicer::OnReset()
{
  const double fs = GetSampleRate();
  mInputPeakSender.Reset(fs);
  mOutputPeakSender.Reset(fs);
  mSmoothCoef = underheard::OnePoleCoef(0.02, fs);
  for (Loop& l : mLoops)
    l.engine->SetHostRate(fs);
  mFx.Prepare(fs);
  if (fs != mCaptureRate)
  {
    for (auto& c : mCapture)
      c.Allocate((int64_t)(kCaptureSeconds * fs));
    mCaptureRate = fs;
  }
}

void Splicer::GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const
{
  if (direction == ERoute::kInput)
    str.Set(busIdx == 0 ? "Main Input" : "SideChain");
  else
    str.Set("Output");
}

// Acts on transport button changes, then asks OnIdle to correct any button that no longer
// matches the engine.
void Splicer::SyncTransport(int i, ESync sync)
{
  Loop& l = mLoops[i];
  TapeLoop& e = *l.engine;
  const bool rec = GetParam(LoopParam(i, kLoopRecord))->Bool();
  const bool play = GetParam(LoopParam(i, kLoopPlay))->Bool();
  const bool clear = GetParam(LoopParam(i, kLoopClear))->Bool();
  const bool caught = GetParam(LoopParam(i, kLoopCatch))->Bool();
  const bool restore = GetParam(LoopParam(i, kLoopRestore))->Bool();
  const bool razor = GetParam(LoopParam(i, kLoopRazor))->Bool();
  bool cut[4] = {false, GetParam(LoopParam(i, kLoopCutReverse))->Bool(), GetParam(LoopParam(i, kLoopCutRemove))->Bool(),
                 GetParam(LoopParam(i, kLoopCutIsolate))->Bool()};

  if (!mResyncTransport)
  {
    if (clear && !l.lastClear)
      e.Clear();
    if (play != l.lastPlay)
      e.SetPlay(play);
    if (rec != l.lastRecord)
    {
      if (!rec && sync != kSyncFree && e.GetState() == TapeLoop::State::Recording)
      {
        // Round the take to the nearest whole number of beats or bars at the set speed.
        const double bpm = GetTempo() > 0. ? GetTempo() : 120.;
        int num = 4, den = 4;
        GetTimeSig(num, den);
        num = num > 0 ? num : 4;
        den = den > 0 ? den : 4;
        const double quarters = (sync == kSyncBars ? num : 1) * 4. / den;
        const double speed = SpeedFromOctaves(GetParam(LoopParam(i, kLoopSpeed))->Value());
        const double unit = quarters * 60. / bpm * kTapeRate * speed;
        const double n = std::max(1., std::round((double)e.TakeFrames() / unit));
        e.CloseTakeAt((int64_t)std::llround(n * unit));
      }
      else
        e.SetRecord(rec);
    }
    if (restore && !l.lastRestore)
      e.Restore();

    // Razor marks are tape positions, so they stay on the same audio as the loop goes round.
    const TapeLoop::State st = e.GetState();
    const bool cuttable = st == TapeLoop::State::Playing || st == TapeLoop::State::Stopped || st == TapeLoop::State::Overdubbing;
    if (e.LengthFrames() != l.markLength)
    {
      l.markIn = l.markOut = -1;
      l.markLength = e.LengthFrames();
    }
    // Marks drawn on the tape replace any marks made at the play head. They run forward.
    const uint32_t markSeq = l.uiMarkSeq.load(std::memory_order_acquire);
    if (markSeq != l.seenMarkSeq)
    {
      l.seenMarkSeq = markSeq;
      l.markIn = l.uiMarkIn.load();
      l.markOut = l.uiMarkOut.load();
      l.markDir = 1;
    }
    if (razor && !l.lastRazor && cuttable)
    {
      const int64_t pos = (int64_t)e.PlayPosition();
      if (l.markIn < 0)
        l.markIn = pos;
      else if (l.markOut < 0)
      {
        l.markOut = pos;
        l.markDir = e.Direction();
      }
      else
        l.markIn = l.markOut = -1;
    }
    for (int op = kCutReverse; op <= kCutIsolate; op++)
    {
      if (!cut[op] || l.lastCut[op])
        continue;
      if (l.markIn < 0 || l.markOut < 0)
        l.cutNeedsMarks = true;
      else if (l.cutOp.load() == kCutNone && cuttable)
      {
        l.cutIn = l.markIn;
        l.cutOut = l.markOut;
        l.cutDir = l.markDir;
        l.cutLength = e.LengthFrames();
        l.cutOp.store(op, std::memory_order_release);
        l.markIn = l.markOut = -1;
      }
      break;
    }
    if (caught && !l.lastCatch)
    {
      const int src = GetParam(LoopParam(i, kLoopSource))->Int();
      l.catchSource = src;
      l.catchSpeed = SpeedFromOctaves(GetParam(LoopParam(i, kLoopSpeed))->Value());
      l.catchAt = mCapture[src].Published();
    }
  }
  l.lastRecord = rec;
  l.lastPlay = play;
  l.lastClear = clear;
  l.lastCatch = caught;
  l.lastRestore = restore;
  l.lastRazor = razor;
  for (int op = 0; op < 4; op++)
    l.lastCut[op] = cut[op];
  l.markInShown = l.markIn;
  l.markOutShown = l.markOut;

  if (razor)
    l.fixRazor = 0;
  for (int op = kCutReverse; op <= kCutIsolate; op++)
    if (cut[op])
      l.fixCut[op] = 0;

  const TapeLoop::State s = e.GetState();
  if (s == TapeLoop::State::Loading || s == TapeLoop::State::Editing)
    return; // settles within a few milliseconds
  const bool wantRec = e.IsRecordingState();
  const bool wantPlay = s == TapeLoop::State::Recording || s == TapeLoop::State::Closing || s == TapeLoop::State::Playing
                     || s == TapeLoop::State::Overdubbing;
  // A correction still pending from an earlier block may be stale: replace or drop it.
  l.fixRecord = rec != wantRec ? (wantRec ? 1 : 0) : -1;
  l.fixPlay = play != wantPlay ? (wantPlay ? 1 : 0) : -1;
  if (clear)
    l.fixClear = 0;
  if (caught)
    l.fixCatch = 0;
  if (restore)
    l.fixRestore = 0;
}

void Splicer::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const double gain = GetParam(kParamOutput)->DBToAmp();
  const double mixTarget = GetParam(kParamMix)->Value() / 100.;
  const double motor = GetParam(kParamMotor)->Value();
  const ESync sync = (ESync)GetParam(kParamSync)->Int();
  const double hiss = GetParam(kParamHiss)->Value() / 100.;
  const int nOut = NOutChansConnected();

  // Channels are laid out per bus at each bus's maximum width: main is 0-1 and the sidechain
  // is 2-3, even when the main input is mono. Mono inputs feed both sides.
  const sample* mainIn[2] = {inputs[0], IsChannelConnected(ERoute::kInput, 1) ? inputs[1] : inputs[0]};
  const bool scConnected = IsChannelConnected(ERoute::kInput, 2);
  const sample* scIn[2] = {inputs[2], IsChannelConnected(ERoute::kInput, 3) ? inputs[3] : inputs[2]};

  // Logic and GarageBand send the main input to the sidechain bus when no sidechain is
  // selected. Treat an identical sidechain as silent (from iPlug2's IPlugSideChain example).
#if defined OS_MAC && defined AU_API
  if (scConnected && (GetHost() == kHostLogic || GetHost() == kHostGarageBand))
  {
    const size_t sz = nFrames * sizeof(sample);
    for (int c = 0; c < 2; c++)
      if (!memcmp(mainIn[c], inputs[c + 2], sz))
        memset(inputs[c + 2], 0, sz);
  }
#endif

  // ---- Drifter: each target's setting plus its drift offset (in normalised units).
  mDrifter.SetLength(GetParam(kParamDriftLength)->Value());
  mDrifter.SetCurve(GetParam(kParamDriftCurve)->Int());
  mDrifter.SetSmear(GetParam(kParamDriftSmear)->Value() / 100.);
  mDrifter.SetReach(GetParam(kParamDriftReach)->Value() / 100.);
  mDrifter.SetGravity(GetParam(kParamDriftGravity)->Value() / 100.);
  mDrifter.SetEnabled(GetParam(kParamDriftOn)->Bool());
  const bool driftKeep = GetParam(kParamDriftKeep)->Bool(), driftReturn = GetParam(kParamDriftReturn)->Bool();
  double driftHome[kNumDriftTargets], driftNow[kNumDriftTargets];
  for (int i = 0; i < kNumDriftTargets; i++)
  {
    const double setting = GetParam(kDriftTargets[i])->GetNormalized();
    if (mKeepOverrideBlocks[i] > 0 && std::fabs(setting - mKeepOverride[i]) > 1e-4)
      mKeepOverrideBlocks[i]--; // the host hasn't caught up yet: the kept value stands in
    else
      mKeepOverrideBlocks[i] = 0;
    driftHome[i] = mKeepOverrideBlocks[i] > 0 ? mKeepOverride[i] : setting;
    driftNow[i] = std::clamp(driftHome[i] + mDrifter.Offset(i), 0., 1.);
  }
  if (driftKeep && !mLastDriftKeep)
  {
    for (int i = 0; i < kNumDriftTargets; i++)
    {
      mKeepValue[i] = driftNow[i];
      mKeepOverride[i] = driftNow[i];
      mKeepOverrideBlocks[i] = (int)(GetSampleRate() / std::max(1, nFrames)); // up to a second
      driftHome[i] = driftNow[i];
    }
    mDrifter.Rebase();
    mKeepPending = true;
  }
  if (driftReturn && !mLastDriftReturn)
    mDrifter.Return();
  mLastDriftKeep = driftKeep;
  mLastDriftReturn = driftReturn;
  if (driftKeep)
    mFixDriftKeep = 0;
  if (driftReturn)
    mFixDriftReturn = 0;
  mDrifter.Advance(nFrames / GetSampleRate());
  for (int i = 0; i < kNumDriftTargets; i++)
  {
    mDriftHome[i] = driftHome[i];
    mDriftNow[i] = driftNow[i];
  }
  mDriftProgress = mDrifter.Progress();
  auto drifted = [&](int param) {
    for (int i = 0; i < kNumDriftTargets; i++)
      if (kDriftTargets[i] == param)
        return GetParam(param)->FromNormalized(driftNow[i]);
    return GetParam(param)->Value();
  };

  // The chain: the standalone plugins' engines behind Splicer's controls (the old controls keep
  // their meaning, so saved projects and Drifter carry over).
  mFx.chorusOn = GetParam(kParamChorusOn)->Bool();
  mFx.chorus.SetRate(drifted(kParamChorusRate));
  mFx.chorus.SetDepth(drifted(kParamChorusDepth) / 100.);
  mFx.chorus.SetMix(drifted(kParamChorusMix) / 100.);
  mFx.chorus.SetMode(GetParam(kParamChorusMode)->Int());
  mFx.chorus.SetVoices(GetParam(kParamChorusVoices)->Int() + 1);
  mFx.chorus.SetFeedback(GetParam(kParamChorusFeedback)->Value() / 100.);
  mFx.delayOn = GetParam(kParamDelayOn)->Bool();
  {
    const int note = GetParam(kParamDelayNote)->Int(); // 0: Free
    mFx.delay.SetTime(note > 0 ? underheard::fx::NoteSeconds(note - 1, GetTempo() > 0. ? GetTempo() : 120.) : drifted(kParamDelayTime) / 1000.);
  }
  mFx.delay.SetFeedback(drifted(kParamDelayFeedback) / 100.);
  mFx.delay.SetHighCut(1000. * std::pow(14., drifted(kParamDelayTone) / 100.)); // Tone: 1 kHz dark .. 14 kHz bright
  mFx.delay.SetMix(drifted(kParamDelayMix) / 100.);
  mFx.delay.SetMode(GetParam(kParamDelayHeads)->Int());
  mFx.delay.SetHeads(6); // multi-head: all three
  mFx.reverbOn = GetParam(kParamReverbOn)->Bool();
  mFx.reverb.SetEngine(GetParam(kParamReverbEngine)->Int());
  mFx.reverb.SetSize(std::pow(2., (drifted(kParamReverbSize) - 50.) / 50.)); // 0 .. 100%: x0.5 .. x2
  mFx.reverb.SetDecay(drifted(kParamReverbDecay));
  mFx.reverb.SetDamping(1500. * std::pow(12., drifted(kParamReverbTone) / 100.)); // Tone: 1.5 kHz dark .. 18 kHz bright
  mFx.SetReverbMix(drifted(kParamReverbMix) / 100.);
  mFx.warmth.SetAmount(GetParam(kParamFxWarmth)->Value() / 100.);

  const double inputLevelTarget = GetParam(kParamInputLevel)->Value() / 100.;
  const double inputSendTarget = GetParam(kParamInputSend)->Value() / 100.;
  double dryTarget[kNumLoops], panTarget[kNumLoops], sendTarget[kNumLoops];
  int source[kNumLoops];
  for (int i = 0; i < kNumLoops; i++)
  {
    Loop& l = mLoops[i];
    TapeLoop& e = *l.engine;

    const int load = l.loadTape.exchange(-1, std::memory_order_acquire);
    if (load >= 0)
    {
      if (l.loadIsEdit)
      {
        e.RequestEdit(l.tapes[load].get(), l.stageEdit, l.stageAge.data(), l.stageShed.data(), l.stageSplices, l.stageNumSplices);
        if (e.GetState() != TapeLoop::State::Editing && e.Storage() != l.tapes[load].get())
          l.loadRefused = true;
      }
      else
        e.RequestLoad(l.tapes[load].get(), l.loadLength.load(), l.loadPlay.load(), l.stageSplices, l.stageNumSplices);
    }

    // Settings first, so a take started in this block starts at this block's speed.
    e.SetMotorTime(motor);
    e.SetSpeed(SpeedFromOctaves(GetParam(LoopParam(i, kLoopSpeed))->Value()));
    e.SetReverse(GetParam(LoopParam(i, kLoopReverse))->Bool());
    e.SetErase(GetParam(LoopParam(i, kLoopErase))->Value() / 100.);
    e.SetFeedback(GetParam(LoopParam(i, kLoopFeedback))->Value() / 100.);
    e.SetSplice(GetParam(LoopParam(i, kLoopSplice))->Value() / 100.);
    e.SetWear(GetParam(LoopParam(i, kLoopWear))->Bool());
    e.SetWearRate(GetParam(LoopParam(i, kLoopWearRate))->Value() / 100.);
    e.SetWearLimit(4. * GetParam(LoopParam(i, kLoopWearLimit))->Value() / 100.); // 100% = as worn as it gets
    e.SetRecover(GetParam(LoopParam(i, kLoopRecover))->Bool());
    e.SetWow(GetParam(LoopParam(i, kLoopWow))->Value() / 100.);
    e.SetFlutter(GetParam(LoopParam(i, kLoopFlutter))->Value() / 100.);
    e.SetHiss(hiss);
    SyncTransport(i, sync);
    dryTarget[i] = GetParam(LoopParam(i, kLoopDry))->Value() / 100.;
    panTarget[i] = GetParam(LoopParam(i, kLoopPan))->Value() / 100.;
    sendTarget[i] = GetParam(LoopParam(i, kLoopSend))->Value() / 100.;
    source[i] = GetParam(LoopParam(i, kLoopSource))->Int();
  }
  mResyncTransport = false;

  for (int s = 0; s < nFrames; s++)
  {
    const float in[kNumSources][2] = {{(float)mainIn[0][s], (float)mainIn[1][s]},
                                      {scConnected ? (float)scIn[0][s] : 0.f, scConnected ? (float)scIn[1][s] : 0.f}};
    for (int c = 0; c < kNumSources; c++)
      mCapture[c].Write(in[c][0], in[c][1]);

    double tapeL = 0., tapeR = 0., sendL = 0., sendR = 0.;
    for (int i = 0; i < kNumLoops; i++)
    {
      Loop& l = mLoops[i];
      l.drySmoothed += (dryTarget[i] - l.drySmoothed) * mSmoothCoef;
      l.panSmoothed += (panTarget[i] - l.panSmoothed) * mSmoothCoef;
      l.sendSmoothed += (sendTarget[i] - l.sendSmoothed) * mSmoothCoef;
      float ol, orr;
      l.engine->Process(in[source[i]][0], in[source[i]][1], ol, orr);
      // Balance-style pan: the far side turns down, the near side stays at unity.
      const double pl = ol * std::min(1., 1. - l.panSmoothed), pr = orr * std::min(1., 1. + l.panSmoothed);
      tapeL += pl * l.drySmoothed;
      tapeR += pr * l.drySmoothed;
      // The send is independent of Dry, so a loop can go only to the effects.
      sendL += pl * l.sendSmoothed;
      sendR += pr * l.sendSmoothed;
    }
    // The live input as a source of its own: onto the Tape side, and into the chain.
    mInputLevelSmoothed += (inputLevelTarget - mInputLevelSmoothed) * mSmoothCoef;
    mInputSendSmoothed += (inputSendTarget - mInputSendSmoothed) * mSmoothCoef;
    tapeL += in[kSourceMain][0] * mInputLevelSmoothed;
    tapeR += in[kSourceMain][1] * mInputLevelSmoothed;
    sendL += in[kSourceMain][0] * mInputSendSmoothed;
    sendR += in[kSourceMain][1] * mInputSendSmoothed;
    float fxL, fxR;
    mFx.Process((float)sendL, (float)sendR, fxL, fxR);
    tapeL += fxL;
    tapeR += fxR;

    // Equal-power crossfade between the live input and the tape.
    mMixSmoothed += (mixTarget - mMixSmoothed) * mSmoothCoef;
    const double a = mMixSmoothed * 0.5 * underheard::kPi;
    const double inGain = std::cos(a), tapeGain = std::sin(a);
    const double outs[2] = {(mainIn[0][s] * inGain + tapeL * tapeGain) * gain, (mainIn[1][s] * inGain + tapeR * tapeGain) * gain};
    for (int c = 0; c < nOut; c++)
      outputs[c][s] = outs[std::min(c, 1)];
  }

  for (auto& c : mCapture)
    c.Publish();

  for (int i = 0; i < kNumLoops; i++)
  {
    Loop& l = mLoops[i];
    const TapeLoop& e = *l.engine;
    l.state = (int)e.GetState();
    l.length = e.LengthFrames();
    l.position = e.PlayPosition();
    l.speed = e.TapeSpeed();
    l.framesInUse = e.FramesInUse();
    l.activeTape = e.Storage() == l.tapes[1].get() ? 1 : 0;
    l.version = e.Version();
    l.passes = e.Passes();
    l.wearPhase = (int)e.GetWearPhase();
  }

  mInputPeakSender.ProcessBlock(inputs, nFrames, kCtrlTagInputMeter, 4, 0);
  mOutputPeakSender.ProcessBlock(outputs, nFrames, kCtrlTagOutputMeter, nOut, 0);
}
#endif // IPLUG_DSP
