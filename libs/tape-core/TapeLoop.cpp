#include "TapeLoop.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace underheard {

namespace {

// Windowed-sinc kernel for band-limited reads when the tape plays faster than the host rate.
constexpr int kKernelZeros = 6;
constexpr int kKernelRes = 256;

struct Kernel
{
  std::array<float, kKernelZeros * kKernelRes + 2> t{};
  Kernel()
  {
    for (size_t i = 0; i < t.size(); i++)
    {
      const double x = (double)i / kKernelRes;
      const double sinc = x == 0. ? 1. : std::sin(kPi * x) / (kPi * x);
      const double w = x >= kKernelZeros ? 0. : 0.42 + 0.5 * std::cos(kPi * x / kKernelZeros) + 0.08 * std::cos(2. * kPi * x / kKernelZeros);
      t[i] = (float)(sinc * w);
    }
  }
};
const Kernel kKernel; // built at load time, so the audio thread never allocates it

} // namespace

TapeLoop::TapeLoop(TapeStorage* storage)
: mTape(storage)
{
  const size_t segments = (size_t)(storage->MaxFrames() / kSegmentFrames + 2);
  mAge.assign(segments, 0.f);
  mShed.assign(segments, 0.f);
  SetHostRate(48000.);
}

void TapeLoop::SetHostRate(double sampleRate)
{
  mHostRate = sampleRate;
  mSpeedCoef = OnePoleCoef(0.06, sampleRate);   // speed changes glide, like a capstan
  mSmoothCoef = OnePoleCoef(0.02, sampleRate);
  mRecEnvStep = 1. / (0.005 * sampleRate);      // punch in/out ramp
  mPlayEnvStep = 1. / (0.003 * sampleRate);
  mClickDecay = std::exp(-1. / (0.0005 * sampleRate));
  mPopDecay = std::exp(-1. / (0.0015 * sampleRate));
  mPopFilter.SetLowPass(1500., 0.9, sampleRate);
  mWowCoef = OnePoleCoef(0.25, sampleRate);
  mFlutterCoef = OnePoleCoef(0.01, sampleRate);
  mDcL.Set(3., sampleRate);
  mDcR.Set(3., sampleRate);
  mAaCounter = 0;
  mToneCounter = 0;
  SetMotorTime(mMotorTime);
}

// ---- Transport -------------------------------------------------------------------------

void TapeLoop::SetRecord(bool on)
{
  if (on)
  {
    if (mState == State::Closing)
      FinishClose(State::Playing);
    FinishPending();

    // Recording always starts with the tape at speed: a take shouldn't begin with the motor's
    // spin-up baked in. (Motor still shapes starting and stopping with Play.) A standing tape
    // also takes the set speed directly.
    if (mMotor <= 0.)
      mSpeedSmoothed = mSpeedRatio * (mReverse ? -1. : 1.);
    mMotor = 1.;

    switch (mState)
    {
      case State::Empty:
        mState = State::Recording;
        mTakeFrames = 0;
        mCloseAt = 0;
        mLength = 0;
        mWritePos = 0.;
        mDir = 1;
        mNumSplices = 0;
        ResetWear();
        break;
      case State::Playing:
      case State::Stopped:
        mState = State::Overdubbing;
        break;
      default:
        break;
    }
  }
  else
  {
    if (mState == State::Recording && mCloseAt == 0)
      BeginClose(mTakeFrames);
    else if (mState == State::Overdubbing)
      mState = State::Playing;
  }
}

void TapeLoop::SetPlay(bool on)
{
  FinishPending();
  if (on)
  {
    if (mState == State::Stopped)
      mState = State::Playing;
    return;
  }

  // Stopping during the first take closes the loop with a raw seam: there's no post-roll.
  if (mState == State::Recording)
    BeginClose(mCloseAt > 0 ? std::min(mCloseAt, mTakeFrames) : mTakeFrames);
  if (mState == State::Closing)
    FinishClose(State::Stopped);
  else if (mState == State::Playing || mState == State::Overdubbing)
    mState = State::Stopped;
}

void TapeLoop::Clear()
{
  if (mState == State::Loading || mState == State::Editing)
    FinishPending();
  if (mState == State::Recording || mState == State::Closing)
  {
    mState = State::Empty;
    mLength = 0;
    mTakeFrames = 0;
    mVersion++;
  }
  else if (mState != State::Empty)
    mState = State::Clearing;
}

void TapeLoop::CloseTakeAt(int64_t lengthFrames)
{
  if (mState != State::Recording)
    return;
  if (lengthFrames <= mTakeFrames)
    BeginClose(lengthFrames);
  else
    mCloseAt = lengthFrames;
}

void TapeLoop::SetPendingSplices(const int64_t* splices, int n)
{
  mPendingNumSplices = 0;
  for (int i = 0; i < n && i < kMaxSplices; i++)
    mPendingSplices[mPendingNumSplices++] = splices[i];
}

void TapeLoop::RequestLoad(TapeStorage* storage, int64_t lengthFrames, bool play, const int64_t* splices, int numSplices)
{
  FinishPending();
  mPendingTape = storage;
  mPendingLength = lengthFrames;
  mPendingPlay = play;
  SetPendingSplices(splices, numSplices);
  // Nothing audible to fade: switch now.
  if (!Closed() || mPlayEnv <= 0.)
    ApplyLoad();
  else
    mState = State::Loading;
}

void TapeLoop::ApplyLoad()
{
  if (!mPendingTape)
    return;
  mTape = mPendingTape;
  mPendingTape = nullptr;
  mLength = mPendingLength >= kMinLoopFrames ? std::min(mPendingLength, mTape->MaxFrames()) : 0;
  mTakeFrames = 0;
  mCloseAt = 0;
  mWritePos = 0.;
  mPlayEnv = 0.;
  mState = mLength == 0 ? State::Empty : (mPendingPlay ? State::Playing : State::Stopped);
  ResetWear();
  mNumSplices = 0;
  for (int i = 0; i < mPendingNumSplices; i++)
    if (mPendingSplices[i] > 0 && mPendingSplices[i] < mLength)
      mSplices[mNumSplices++] = mPendingSplices[i];
  mVersion++;
}

void TapeLoop::RequestEdit(TapeStorage* storage, const TapeEdit& edit, const float* age, const float* shed, const int64_t* splices, int numSplices)
{
  FinishPending();
  if (!Closed() || edit.count == 0 || edit.oldLength != mLength)
    return;
  mPendingTape = storage;
  mPendingEdit = edit;
  mPendingAge = age;
  mPendingShed = shed;
  SetPendingSplices(splices, numSplices);
  // Fade out first if anything can be heard; a stopped tape switches at once.
  if (mPlayEnv <= 0. || mMotor <= 0.)
    ApplyEdit();
  else
  {
    mStateBeforeEdit = mState;
    mState = State::Editing;
  }
}

void TapeLoop::ApplyEdit()
{
  if (!mPendingTape)
    return;
  const double head = mPendingEdit.MapHead(mWritePos);
  mTape = mPendingTape;
  mPendingTape = nullptr;
  mLength = mPendingEdit.NewLength();
  mWritePos = Wrap(head);
  if (mState == State::Editing)
    mState = mStateBeforeEdit;

  const size_t segments = std::min(mAge.size(), (size_t)((mLength + kSegmentFrames - 1) / kSegmentFrames));
  std::copy(mPendingAge, mPendingAge + segments, mAge.begin());
  std::copy(mPendingShed, mPendingShed + segments, mShed.begin());
  mLastWriteSeg = -1;

  mNumSplices = 0;
  for (int i = 0; i < mPendingNumSplices; i++)
    if (mPendingSplices[i] > 0 && mPendingSplices[i] < mLength)
      mSplices[mNumSplices++] = mPendingSplices[i];

  mPlayEnv = 0.;
  mLastReadPos = PlayPosition();
  mVersion++;
}

void TapeLoop::Restore() { ResetWear(); }

void TapeLoop::ResetWear()
{
  std::fill(mAge.begin(), mAge.end(), 0.f);
  std::fill(mShed.begin(), mShed.end(), 0.f);
  mLastWriteSeg = -1;
  mPasses = 0;
  mWearPhase = WearPhase::Wearing;
}

void TapeLoop::BeginClose(int64_t length)
{
  mCloseAt = 0;
  if (length < kMinLoopFrames)
  {
    mState = State::Empty;
    mLength = 0;
    mVersion++;
    return;
  }
  mLength = length;
  mState = State::Closing; // the record head keeps going to capture the seam crossfade

  // Anything already recorded past the end is post-roll: blend it in now.
  const int64_t have = std::min<int64_t>(mTakeFrames - length, kSeamFrames);
  for (int64_t j = 0; j < have; j++)
  {
    const float* f = mTape->Frame(length + j);
    BlendSeam(j, f[0], f[1]);
  }
  if (have >= kSeamFrames)
    FinishClose(State::Playing);
}

// Crossfades post-roll frame j (what follows the loop's end) into the loop's start.
void TapeLoop::BlendSeam(int64_t j, float l, float r)
{
  if (j < 0 || j >= kSeamFrames || j >= mLength)
    return;
  const float w = (float)j / (float)kSeamFrames;
  float* f = mTape->Frame(j);
  f[0] = l * (1.f - w) + f[0] * w;
  f[1] = r * (1.f - w) + f[1] * w;
}

void TapeLoop::FinishClose(State next)
{
  mWritePos = Wrap(mWritePos);
  mState = next;
  mLastWriteSeg = -1;
  mVersion++;
}

void TapeLoop::FinishPending()
{
  if (mState == State::Clearing)
  {
    mState = State::Empty;
    mLength = 0;
    mNumSplices = 0;
    ResetWear();
    mVersion++;
  }
  else if (mState == State::Loading)
    ApplyLoad();
  else if (mState == State::Editing)
    ApplyEdit();
}

bool TapeLoop::IsTransportRunning() const
{
  if (mState == State::Editing)
    return mStateBeforeEdit != State::Stopped;
  return mState == State::Recording || mState == State::Closing || mState == State::Playing
      || mState == State::Overdubbing || mState == State::Clearing || mState == State::Loading;
}

// ---- Controls --------------------------------------------------------------------------

void TapeLoop::SetSpeed(double ratio) { mSpeedRatio = std::max(0.01, ratio); }
void TapeLoop::SetReverse(bool on) { mReverse = on; }
void TapeLoop::SetErase(double amount) { mErase = std::clamp(amount, 0., 1.); }
void TapeLoop::SetFeedback(double amount) { mFeedback = std::clamp(amount, 0., 1.); }
void TapeLoop::SetSplice(double amount) { mSplice = std::clamp(amount, 0., 1.); }
void TapeLoop::SetWear(bool on) { mWearOn = on; }
void TapeLoop::SetWearRate(double amount) { mWearRate = std::clamp(amount, 0., 1.); }
void TapeLoop::SetWearLimit(double age) { mWearLimit = std::max(0., age); }
void TapeLoop::SetRecover(bool on) { mRecover = on; }
void TapeLoop::SetWow(double amount) { mWow = std::clamp(amount, 0., 1.); }
void TapeLoop::SetFlutter(double amount) { mFlutter = std::clamp(amount, 0., 1.); }
void TapeLoop::SetHiss(double amount) { mHiss = std::clamp(amount, 0., 1.); }

void TapeLoop::SetMotorTime(double seconds)
{
  mMotorTime = std::max(0., seconds);
  mMotorStep = mMotorTime < 0.001 ? 1. : 1. / (mMotorTime * mHostRate);
}

// ---- Status ----------------------------------------------------------------------------

double TapeLoop::PlayPosition() const
{
  return Closed() ? Wrap(mWritePos + kHeadGap * mDir) : mWritePos;
}

int64_t TapeLoop::FramesInUse() const
{
  return mState == State::Recording ? (int64_t)mWritePos + 2 : mLength;
}

double TapeLoop::MeanAge() const
{
  if (mLength <= 0)
    return 0.;
  const int64_t n = (mLength + kSegmentFrames - 1) / kSegmentFrames;
  double sum = 0.;
  for (int64_t s = 0; s < n; s++)
    sum += mAge[(size_t)s];
  return sum / (double)n;
}

// ---- Audio -----------------------------------------------------------------------------

double TapeLoop::Wrap(double p) const
{
  return mLength > 0 ? p - (double)mLength * std::floor(p / (double)mLength) : p;
}

int64_t TapeLoop::WrapIndex(int64_t i) const
{
  i %= mLength;
  return i < 0 ? i + mLength : i;
}

float TapeLoop::Noise()
{
  mNoise = mNoise * 1664525u + 1013904223u;
  return (float)((double)(int32_t)mNoise * (1. / 2147483648.));
}

void TapeLoop::ReadTape(double pos, float& l, float& r)
{
  const int64_t i = (int64_t)std::floor(pos);
  const float t = (float)(pos - (double)i);
  const float* f[4];
  for (int n = 0; n < 4; n++)
    f[n] = mTape->Frame(WrapIndex(i - 1 + n));
  l = Hermite(f[0][0], f[1][0], f[2][0], f[3][0], t);
  r = Hermite(f[0][1], f[1][1], f[2][1], f[3][1], t);
}

// Reads with a low-pass stretched by `stretch` (tape frames per host sample, > 1), so tape
// played faster than the host rate can't alias.
void TapeLoop::ReadTapeBandLimited(double pos, double stretch, float& l, float& r)
{
  const double c = 0.92 / stretch; // cutoff, relative to the tape's Nyquist
  const double half = kKernelZeros / c;
  const int64_t i0 = (int64_t)std::ceil(pos - half), i1 = (int64_t)std::floor(pos + half);
  double sl = 0., sr = 0., norm = 0.;
  int64_t idx = WrapIndex(i0);
  for (int64_t i = i0; i <= i1; i++)
  {
    const double d = std::fabs((double)i - pos) * c * kKernelRes;
    const size_t k = (size_t)d;
    if (k + 1 < kKernel.t.size())
    {
      const double w = kKernel.t[k] + (kKernel.t[k + 1] - kKernel.t[k]) * (d - (double)k);
      const float* f = mTape->Frame(idx);
      sl += w * f[0];
      sr += w * f[1];
      norm += w;
    }
    if (++idx == mLength)
      idx = 0;
  }
  const double g = norm != 0. ? 1. / norm : 0.;
  l = (float)(sl * g);
  r = (float)(sr * g);
}

// One pass of the head over segment `seg`: it ages, and oxide may start to shed.
void TapeLoop::WearSegment(int64_t seg)
{
  const int64_t n = (mLength + kSegmentFrames - 1) / kSegmentFrames;
  if (seg < 0 || seg >= n)
    return;
  auto u = [this] { return 0.5 * (Noise() + 1.f); };
  const double r = mWearRate;
  const double step = r * r * 0.06 * (0.6 + 0.8 * u()); // uneven, like real tape
  if (mWearPhase == WearPhase::Holding)
    return;
  if (mWearPhase == WearPhase::Recovering)
  {
    // Healing runs the wear backwards at the same rate: age falls, and shed oxide closes up in
    // proportion, so the tape is fully clean when its age reaches zero.
    const double old = mAge[(size_t)seg];
    const double now = std::max(0., old - step);
    mAge[(size_t)seg] = (float)now;
    mShed[(size_t)seg] = old > 0. ? (float)(mShed[(size_t)seg] * now / old) : 0.f;
    return;
  }
  const double age = mAge[(size_t)seg] + step;
  double shed = mShed[(size_t)seg];
  if (u() < r * 0.004 * (1. + 2. * age))
    shed += 0.15 + 0.45 * u(); // a new spot
  const float neighbour = std::max(mShed[(size_t)((seg + n - 1) % n)], mShed[(size_t)((seg + 1) % n)]);
  if (neighbour > 0.3f)
    shed += r * 0.03 * u() * neighbour; // damage spreads
  mAge[(size_t)seg] = (float)age;
  mShed[(size_t)seg] = (float)std::min(1., shed);
}

// Checks the tape against the Wear Limit (every few hundred milliseconds of travel).
void TapeLoop::UpdateWearPhase()
{
  const double mean = MeanAge();
  switch (mWearPhase)
  {
    case WearPhase::Wearing:
      if (mean >= mWearLimit)
        mWearPhase = mRecover ? WearPhase::Recovering : WearPhase::Holding;
      break;
    case WearPhase::Holding:
      if (mRecover)
        mWearPhase = WearPhase::Recovering;
      else if (mean < mWearLimit - 0.05)
        mWearPhase = WearPhase::Wearing; // the limit was raised, or an overdub renewed the tape
      break;
    case WearPhase::Recovering:
      if (!mRecover)
        mWearPhase = mean >= mWearLimit ? WearPhase::Holding : WearPhase::Wearing;
      else if (mean <= 0.002)
        mWearPhase = WearPhase::Wearing; // new again: start over
      break;
  }
}

// Damage at a tape position, interpolated between segment centres.
void TapeLoop::SegmentDamage(double pos, double& age, double& shed) const
{
  const int64_t n = (mLength + kSegmentFrames - 1) / kSegmentFrames;
  const double x = pos / kSegmentFrames - 0.5;
  const double fl = std::floor(x);
  const double f = x - fl;
  int64_t s0 = (int64_t)fl % n;
  if (s0 < 0)
    s0 += n;
  const int64_t s1 = (s0 + 1) % n;
  age = mAge[(size_t)s0] + (mAge[(size_t)s1] - mAge[(size_t)s0]) * f;
  shed = mShed[(size_t)s0] + (mShed[(size_t)s1] - mShed[(size_t)s0]) * f;
}

void TapeLoop::Process(float inL, float inR, float& outL, float& outR)
{
  // Motor and speed. The motor ramps the tape up and down; the speed glides toward its
  // target. A standing tape takes the set speed directly, so a take starts at the right speed.
  const double target = mSpeedRatio * (mReverse ? -1. : 1.);
  if (mMotor <= 0.)
    mSpeedSmoothed = target;
  else
    mSpeedSmoothed += (target - mSpeedSmoothed) * mSpeedCoef;
  const bool running = IsTransportRunning();
  mMotor = running ? std::min(1., mMotor + mMotorStep) : std::max(0., mMotor - mMotorStep);

  double v = mSpeedSmoothed * SmoothStep(mMotor);
  if (mState == State::Recording || mState == State::Closing)
    v = std::fabs(v); // a first take always runs forward

  // Wow: a slow wander toward a new random target every half second or so. Flutter: a fast
  // wobble around 8 Hz whose rate drifts, with a little noise.
  mWowTimer -= 1. / mHostRate;
  if (mWowTimer <= 0.)
  {
    mWowTarget = Noise();
    mWowTimer = 0.4 + 0.4 * (Noise() + 1.f);
  }
  mWow1 += (mWowTarget - mWow1) * mWowCoef;
  mWow2 += (mWow1 - mWow2) * mWowCoef;
  mFlutterPhase += 2. * kPi * mFlutterRate / mHostRate;
  if (mFlutterPhase > 2. * kPi)
  {
    mFlutterPhase -= 2. * kPi;
    mFlutterRate = std::clamp(mFlutterRate + 0.4 * Noise(), 6., 11.);
  }
  mFlutterNoise += (Noise() - mFlutterNoise) * mFlutterCoef;
  v *= 1. + mWow * 0.012 * mWow2 + mFlutter * 0.002 * (std::sin(mFlutterPhase) + 0.5 * mFlutterNoise);

  mSpeed = v;
  if (v > 0.)
    mDir = 1;
  else if (v < 0.)
    mDir = -1;
  const double delta = v * kTapeRate / mHostRate; // tape frames per host sample

  mEraseSmoothed += (mErase - mEraseSmoothed) * mSmoothCoef;

  // ---- Playback: the play head sits kHeadGap frames ahead of the record head, so it always
  // reads tape from before this pass's overdub.
  const bool switching = mState == State::Clearing || mState == State::Loading || mState == State::Editing;
  const double playTarget = (Closed() && !switching) ? 1. : 0.;
  mPlayEnv = playTarget > mPlayEnv ? std::min(playTarget, mPlayEnv + mPlayEnvStep) : std::max(playTarget, mPlayEnv - mPlayEnvStep);

  double age = 0., shed = 0.;
  float yl = 0.f, yr = 0.f;
  const bool updateTone = --mToneCounter <= 0; // filter coefficients follow every 32 samples
  if (updateTone)
    mToneCounter = 32;
  if (Closed())
  {
    const double pos = Wrap(mWritePos + kHeadGap * mDir);
    if (mPlayEnv > 0.)
    {
      if (std::fabs(delta) > 1.)
        ReadTapeBandLimited(pos, std::fabs(delta), yl, yr);
      else
        ReadTape(pos, yl, yr);

      // Playback level falls with tape speed, as the head's output does.
      double gain = mPlayEnv * SmoothStep(std::fabs(v) / 0.12);
      if (mSplice > 0.)
      {
        // The seam and every razor cut are splices: a dropout around each, and a click as it
        // passes the head.
        const double L = (double)mLength;
        const bool jumped = std::fabs(pos - mLastReadPos) > 0.5 * L;
        double d = std::min(pos, L - pos);
        bool crossed = jumped;
        for (int i = 0; i < mNumSplices; i++)
        {
          const double sp = (double)mSplices[i];
          const double ds = std::fabs(pos - sp);
          d = std::min(d, std::min(ds, L - ds));
          if (!jumped && (mLastReadPos < sp) != (pos < sp))
            crossed = true;
        }
        if (d < kSpliceFrames)
        {
          const double u = 1. - d / kSpliceFrames;
          gain *= 1. - 0.8 * mSplice * u * u;
        }
        if (crossed)
          mClickEnv = 1.;
      }

      // Wear on the recording: print-through ghosts, generation loss, saturation, level loss.
      SegmentDamage(pos, age, shed);
      if (age > 1e-3)
      {
        const float pt = (float)std::min(0.06, 0.015 * age);
        float al, ar, bl, br;
        ReadTape(Wrap(pos + kPrintThroughFrames), al, ar);
        ReadTape(Wrap(pos - kPrintThroughFrames), bl, br);
        yl += pt * (al + bl);
        yr += pt * (ar + br);
      }
      if (updateTone)
      {
        const double ageCut = std::min(0.45 * mHostRate, std::max(400., 18000. * std::exp(-1.2 * age)));
        mAgeL.SetLowPass(ageCut, 0.7071, mHostRate);
        mAgeR.SetLowPass(ageCut, 0.7071, mHostRate);
      }
      yl = (float)mAgeL.Process(yl);
      yr = (float)mAgeR.Process(yr);
      if (age > 1e-3)
      {
        const double d = 1. + 2. * age;
        yl = (float)(std::tanh(d * yl) / d);
        yr = (float)(std::tanh(d * yr) / d);
        gain /= 1. + 0.15 * age;
      }

      // Hiss comes from the tape at playback, so the recording's age dulls it less; it rises
      // with age and at low speed.
      if (mHiss > 0.)
      {
        const double h = mHiss * (0.004 + 0.008 * age) / std::sqrt(std::max(std::fabs(v), 0.25));
        yl += (float)(h * Noise());
        yr += (float)(h * Noise());
      }
      if (shed > 1e-3)
      {
        gain *= 1. - 0.9 * shed;
        // Crackle happens as shed tape passes the head, so it follows the tape's speed.
        const double moving = std::min(1., std::fabs(delta));
        if (0.5 * (Noise() + 1.f) < shed * 0.002 * moving)
        {
          mPopAmp = (0.04 + 0.04 * (Noise() + 1.f)) * shed * (Noise() < 0.f ? -1. : 1.);
          mPopEnv = 1.;
          mPopFilter.SetLowPass(800. + 850. * (Noise() + 1.f), 0.9, mHostRate); // 0.8 - 2.5 kHz
        }
      }
      yl *= (float)gain;
      yr *= (float)gain;
    }
    mLastReadPos = pos;
  }

  // The head loses highs at low speed (gap loss).
  if (updateTone)
  {
    const double headCut = std::min(0.45 * mHostRate, std::max(600., 18000. * std::fabs(v)));
    mHeadL.SetLowPass(headCut, 0.7071, mHostRate);
    mHeadR.SetLowPass(headCut, 0.7071, mHostRate);
  }
  // Pops fade with the tape like everything else it plays, and the head dulls them at low speed.
  const double popIn = mPopEnv > 1e-4 ? mPopAmp * mPopEnv * (0.5 + 0.5 * Noise()) : 0.;
  mPopEnv *= mPopDecay;
  const float pop = (float)(mPopFilter.Process(popIn) * mPlayEnv * SmoothStep(std::fabs(v) / 0.12));
  yl = (float)mHeadL.Process(yl + pop);
  yr = (float)mHeadR.Process(yr + pop);

  if (mClickEnv > 1e-4)
  {
    const float c = (float)(Noise() * mClickEnv * mSplice * 0.25);
    yl += c;
    yr += c;
    mClickEnv *= mClickDecay;
  }

  outL = (float)mDcL.Process(yl);
  outR = (float)mDcR.Process(yr);

  if (switching && mPlayEnv <= 0.)
    FinishPending();

  // ---- Record path. When the tape runs slower than the input needs, low-pass the input
  // so it can't alias on the tape (slow tape is duller, too).
  if (--mAaCounter <= 0)
  {
    mAaCounter = 16;
    const double ad = std::fabs(delta);
    mAaBypass = ad >= 0.999;
    if (!mAaBypass)
    {
      const double fc = std::max(20., 0.45 * mHostRate * ad);
      mAaL.Set(fc, mHostRate);
      mAaR.Set(fc, mHostRate);
    }
  }
  const double fl = mAaL.Process(inL), fr = mAaR.Process(inR);
  for (int n = 0; n < 3; n++)
  {
    mHistL[n] = mHistL[n + 1];
    mHistR[n] = mHistR[n + 1];
  }
  mHistL[3] = mAaBypass ? inL : (float)fl;
  mHistR[3] = mAaBypass ? inR : (float)fr;

  const double recTarget = mState == State::Overdubbing ? 1. : 0.;
  mRecEnv = recTarget > mRecEnv ? std::min(1., mRecEnv + mRecEnvStep) : std::max(0., mRecEnv - mRecEnvStep);

  const double a = mWritePos, b = mWritePos + delta;
  const bool firstTake = mState == State::Recording || mState == State::Closing;
  const bool closed = Closed() && !switching;
  const bool rewrite = closed && (mRecEnv > 0. || mFeedback < 1.);
  const bool wearing = closed && (mWearOn || mRecEnv > 0.);

  if (delta != 0. && (firstTake || rewrite || wearing))
  {
    const double retain = mFeedback * (1. - mEraseSmoothed * mRecEnv);
    if (rewrite)
      mVersion++;

    // Writes tape frame k, which the record head crosses at fraction t of this sample.
    // Returns false to stop writing for this sample.
    auto write = [&](int64_t k, double t) -> bool {
      if (mState == State::Recording || mState == State::Closing || rewrite)
      {
        const float wl = Hermite(mHistL[0], mHistL[1], mHistL[2], mHistL[3], (float)t);
        const float wr = Hermite(mHistR[0], mHistR[1], mHistR[2], mHistR[3], (float)t);

        if (mState == State::Recording)
        {
          if (k >= mTape->CapacityFrames())
          {
            BeginClose(k); // out of tape: close the loop here
            return false;
          }
          float* f = mTape->Frame(k);
          f[0] = wl;
          f[1] = wr;
          mTakeFrames = k + 1;
          if (mCloseAt > 0 && mTakeFrames >= mCloseAt)
          {
            BeginClose(mCloseAt);
            return mState == State::Closing;
          }
          return true;
        }

        if (mState == State::Closing)
        {
          // Post-roll: crossfade what follows the loop's end into its start, so the seam is
          // continuous. Playback starts once this is done.
          const int64_t j = k - mLength;
          BlendSeam(j, wl, wr);
          if (j + 1 >= kSeamFrames)
          {
            FinishClose(State::Playing);
            return false;
          }
          return true;
        }

        float* f = mTape->Frame(WrapIndex(k));
        f[0] = (float)(f[0] * retain + wl * mRecEnv);
        f[1] = (float)(f[1] * retain + wr * mRecEnv);
      }

      // The head has left a segment: that's one pass over it.
      const int64_t seg = WrapIndex(k) / kSegmentFrames;
      if (seg != mLastWriteSeg)
      {
        if (mLastWriteSeg >= 0)
        {
          if (mRecEnv > 0.)
            mAge[(size_t)mLastWriteSeg] *= (float)(1. - mEraseSmoothed * mRecEnv); // erased tape is new again
          if (mWearOn)
          {
            WearSegment(mLastWriteSeg);
            if (++mSegmentsSincePhaseCheck >= 64)
            {
              mSegmentsSincePhaseCheck = 0;
              UpdateWearPhase();
            }
          }
        }
        mLastWriteSeg = seg;
      }
      return true;
    };

    if (delta > 0.)
    {
      for (int64_t k = (int64_t)std::ceil(a); (double)k < b; k++)
        if (!write(k, ((double)k - a) / delta))
          break;
    }
    else
    {
      for (int64_t k = (int64_t)std::floor(a); (double)k > b; k--)
        if (!write(k, ((double)k - a) / delta))
          break;
    }
  }

  mWritePos = b;
  if (Closed())
  {
    if (mWearOn && std::floor(a / (double)mLength) != std::floor(b / (double)mLength))
      mPasses++;
    mWritePos = Wrap(mWritePos);
  }
}

} // namespace underheard
