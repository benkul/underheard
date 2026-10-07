#include "AlgoReverb.h"

namespace underheard::fx {

namespace {
constexpr double kDattorroRate = 29761.;
constexpr double kMaxSize = 2.;
// Dattorro's plate, in samples at 29761 Hz.
constexpr int kInDiffLen[4] = {142, 107, 379, 277};
constexpr double kInDiffG[4] = {0.75, 0.75, 0.625, 0.625};
constexpr int kModApLen[2] = {672, 908}, kDelay1Len[2] = {4453, 4217}, kDecayApLen[2] = {1800, 2656}, kDelay2Len[2] = {3720, 3163};
constexpr double kModExcursion = 16.;
// The hall's lines (ms at size 1) and its input diffusers.
constexpr double kHallMs[8] = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 79.3};
constexpr double kHallDiffMs[4] = {4.7, 3.6, 12.7, 9.3};
constexpr double kHallDiffG[4] = {0.7, 0.7, 0.6, 0.6};
// Output signs (orthogonal-ish, so the sides decorrelate).
constexpr double kHallOutL[8] = {1, -1, 1, 1, -1, 1, -1, 1}, kHallOutR[8] = {1, 1, -1, 1, 1, -1, -1, 1};
// Early reflections (ms at size 1).
constexpr double kHallEr[12] = {11, 17, 23, 29, 37, 43, 51, 59, 67, 73, 83, 97};
constexpr double kPlateEr[12] = {3.1, 4.7, 6.3, 7.9, 9.6, 11.3, 13.1, 14.9, 16.7, 18.9, 21.3, 23.9};
// Damping takes some energy out with the highs, so the broadband decay comes out a little
// short of the setting; these correct it (measured in AlgoReverbTest).
constexpr double kPlateDecayFix = 1.1, kHallDecayFix = 1.15;
} // namespace

void AlgoReverb::Prepare(double fs)
{
  mFs = fs;
  const double plateMax = fs / kDattorroRate * kMaxSize;
  for (int i = 0; i < 4; i++)
    mInDiff[(size_t)i].d.Allocate((int)(kInDiffLen[i] * plateMax) + 4);
  for (int h = 0; h < 2; h++)
  {
    mModAp[h].d.Allocate((int)((kModApLen[h] + kModExcursion * 3.) * plateMax) + 8);
    mDecayAp[h].d.Allocate((int)(kDecayApLen[h] * plateMax) + 4);
    mTankDelay1[h].Allocate((int)(kDelay1Len[h] * plateMax) + 4);
    mTankDelay2[h].Allocate((int)(kDelay2Len[h] * plateMax) + 4);
  }
  for (int i = 0; i < kLines; i++)
    mLines[(size_t)i].d.Allocate((int)((kHallMs[i] * kMaxSize + 3.) / 1000. * fs) + 8);
  for (int i = 0; i < 4; i++)
  {
    mDiffL[(size_t)i].d.Allocate((int)(kHallDiffMs[i] * kMaxSize / 1000. * fs) + 4);
    mDiffR[(size_t)i].d.Allocate((int)(kHallDiffMs[i] * kMaxSize / 1000. * fs) + 4);
  }
  mEr.Allocate((int)(100. * kMaxSize / 1000. * fs) + 8);
  for (int i = 0; i < kLines; i++)
  {
    mModRate[(size_t)i] = 0.11 + 0.07 * i; // slow, all different
    mModPhase[(size_t)i] = 0.37 * i;
  }
  mWander.Set(fs, 0.5, 4242);
  mGateCoef = OnePoleCoef(0.02, fs);
  mInGate = mFreeze ? 0. : 1.;
  mModNow = mFreeze ? 0. : mMod;
  Build();
}

void AlgoReverb::Reset()
{
  for (auto& a : mInDiff) a.d.Clear();
  for (int h = 0; h < 2; h++)
  {
    mModAp[h].d.Clear();
    mDecayAp[h].d.Clear();
    mTankDelay1[h].Clear();
    mTankDelay2[h].Clear();
    mPlateDamp[h].z = 0.;
    mPlateFeed[h] = 0.;
  }
  mBandwidth.z = 0.;
  for (auto& l : mLines) l.d.Clear();
  for (auto& f : mHallDamp) f.z = 0.;
  for (auto& a : mDiffL) a.d.Clear();
  for (auto& a : mDiffR) a.d.Clear();
  mLineState.fill(0.);
  mEr.Clear();
}

void AlgoReverb::SetEngine(int engine)
{
  engine = std::clamp(engine, 0, 1);
  if (engine != mEngine)
  {
    mEngine = engine;
    mDirty = true;
  }
}

void AlgoReverb::SetSize(double scale)
{
  scale = std::clamp(scale, 0.5, kMaxSize);
  if (scale != mSize)
  {
    mSize = scale;
    mDirty = true;
  }
}

void AlgoReverb::SetBalance(double b)
{
  b = std::clamp(b, -1., 1.);
  mEarly = std::min(1., 1. - b);
  mLate = std::min(1., 1. + b);
  mBalanceGain = 1. / std::sqrt(mEarly * mEarly + mLate * mLate);
}

void AlgoReverb::Build()
{
  mScale = mFs / kDattorroRate * mSize;
  for (int i = 0; i < 4; i++)
  {
    mInDiff[(size_t)i].length = std::max(1, (int)(kInDiffLen[i] * mScale));
    mInDiff[(size_t)i].g = kInDiffG[i];
  }
  for (int h = 0; h < 2; h++)
  {
    mModAp[h].length = std::max(1, (int)(kModApLen[h] * mScale));
    mDecayAp[h].length = std::max(1, (int)(kDecayApLen[h] * mScale));
    mTankLen1[h] = std::max(1, (int)(kDelay1Len[h] * mScale));
    mTankLen2[h] = std::max(1, (int)(kDelay2Len[h] * mScale));
  }
  for (int i = 0; i < kLines; i++)
    mLines[(size_t)i].length = std::round(kHallMs[i] * mSize / 1000. * mFs); // whole samples: lossless when frozen
  for (int i = 0; i < 4; i++)
  {
    mDiffL[(size_t)i].length = std::max(1, (int)(kHallDiffMs[i] * mSize / 1000. * mFs));
    mDiffR[(size_t)i].length = std::max(1, (int)(kHallDiffMs[i] * mSize * 1.13 / 1000. * mFs)); // a little different per side
    mDiffL[(size_t)i].g = mDiffR[(size_t)i].g = kHallDiffG[i];
  }
  // Early reflections: decaying, alternately panned, each side normalised to unit energy.
  const double* er = mEngine == kPlate ? kPlateEr : kHallEr;
  const double tau = mEngine == kPlate ? 0.012 : 0.04;
  double eL = 0., eR = 0.;
  for (int i = 0; i < kTaps; i++)
  {
    const double t = er[i] * mSize / 1000.;
    const double g = std::exp(-t / (tau * mSize)), pan = (i % 2 ? -0.7 : 0.7) * (1. - 0.04 * i);
    mTapTime[(size_t)i] = t * mFs;
    mTapGainL[(size_t)i] = g * std::sqrt(0.5 + 0.5 * pan);
    mTapGainR[(size_t)i] = g * std::sqrt(0.5 - 0.5 * pan);
    eL += mTapGainL[(size_t)i] * mTapGainL[(size_t)i];
    eR += mTapGainR[(size_t)i] * mTapGainR[(size_t)i];
  }
  for (int i = 0; i < kTaps; i++)
  {
    mTapGainL[(size_t)i] /= std::sqrt(eL);
    mTapGainR[(size_t)i] /= std::sqrt(eR);
  }
  UpdateGains();
}

void AlgoReverb::UpdateGains()
{
  mDirty = false;
  const double hold = 1.; // frozen: lossless (the reads sit on whole samples)
  const double damp = mFreeze ? 1. : 1. - std::exp(-2. * kPi * std::min(mDampHz, 0.45 * mFs) / mFs);
  // Plate: two decay multiplies per half of the figure of eight.
  const double halfLoop = (kModApLen[0] + kDelay1Len[0] + kDecayApLen[0] + kDelay2Len[0] + kModApLen[1] + kDelay1Len[1] + kDecayApLen[1] + kDelay2Len[1]) / 2.
                          * mScale / mFs;
  mPlateGain = mFreeze ? hold : std::pow(10., -3. * (halfLoop / 2.) / (mDecay * kPlateDecayFix));
  // The tank's allpasses ring on their own; keep that under half the decay, or short decays on
  // a big plate hang on (Dattorro's 0.7 and 0.5 otherwise).
  for (int h = 0; h < 2; h++)
  {
    const double cap = [&](int len) { return std::pow(10., -3. * (len * mScale / mFs) / (0.5 * mDecay)); }(kModApLen[h]);
    const double cap2 = std::pow(10., -3. * (kDecayApLen[h] * mScale / mFs) / (0.5 * mDecay));
    mModAp[h].g = std::min(0.7, cap);
    mDecayAp[h].g = std::min(0.5, cap2);
  }
  for (auto& d : mPlateDamp)
    d.k = damp;
  mBandwidth.k = 1. - std::exp(-2. * kPi * std::min(14000., 0.45 * mFs) / mFs);
  double meanLen = 0.;
  for (int i = 0; i < kLines; i++)
  {
    const double secs = mLines[(size_t)i].length / mFs;
    meanLen += secs / kLines;
    mLineGain[(size_t)i] = mFreeze ? hold : std::pow(10., -3. * secs / (mDecay * kHallDecayFix));
    mHallDamp[(size_t)i].k = damp;
  }
  (void)meanLen;
  // Level: about unit energy whatever the decay, size and damping (not while frozen: it holds
  // what's there). The late part's raw energy, fitted to measurements (AlgoReverbTest checks
  // the result stays within a couple of dB).
  if (!mFreeze)
  {
    const double dampRatio = std::min(mDampHz, 0.45 * mFs) / 7000.;
    const double raw = mEngine == kPlate ? (1.9 + 0.31 * mDecay / mSize) * std::pow(dampRatio, 0.18)
                                         : 0.6 * std::pow(mDecay, mDecay < 1. ? 1.25 : 0.75) * std::pow(mSize, -0.7) * std::pow(dampRatio, 0.9);
    mLateNorm = 1. / std::sqrt(raw);
  }
}

void AlgoReverb::ProcessPlate(double inL, double inR, double& lateL, double& lateR)
{
  double x = mBandwidth.Run(0.5 * (inL + inR));
  for (auto& a : mInDiff)
    x = a.Run(x);
  // The tank: two halves, each feeding the other.
  const double wob = mWander.Next(1.);
  const double exc = kModExcursion * mScale / mSize * mModNow * 2.;
  const double feedL = mPlateFeed[1], feedR = mPlateFeed[0];
  for (int h = 0; h < 2; h++)
  {
    const double mod = exc * (0.6 * std::sin(2. * kPi * mModPhase[(size_t)h]) + 0.4 * (h ? -wob : wob));
    double v = mModAp[h].RunMod(x + mPlateGain * (h ? feedR : feedL), mod);
    mTankDelay1[h].Write((float)v);
    v = mTankDelay1[h].ReadInt(mTankLen1[h]);
    v = mPlateDamp[h].Run(v) * mPlateGain;
    v = mDecayAp[h].Run(v);
    mTankDelay2[h].Write((float)v);
    mPlateFeed[h] = mTankDelay2[h].ReadInt(mTankLen2[h]);
  }
  for (int h = 0; h < 2; h++)
  {
    mModPhase[(size_t)h] += (h ? 0.87 : 1.03) / mFs;
    if (mModPhase[(size_t)h] >= 1.) mModPhase[(size_t)h] -= 1.;
  }
  // Dattorro's output taps (scaled).
  auto tap = [&](const DelayLine& d, int n) { return (double)d.ReadInt(std::max(1, (int)(n * mScale))); };
  lateL = tap(mTankDelay1[1], 266) + tap(mTankDelay1[1], 2974) - tap(mDecayAp[1].d, 1913) + tap(mTankDelay2[1], 1996) - tap(mTankDelay1[0], 1990)
          - tap(mDecayAp[0].d, 187) - tap(mTankDelay2[0], 1066);
  lateR = tap(mTankDelay1[0], 353) + tap(mTankDelay1[0], 3627) - tap(mDecayAp[0].d, 1228) + tap(mTankDelay2[0], 2673) - tap(mTankDelay1[1], 2111)
          - tap(mDecayAp[1].d, 335) - tap(mTankDelay2[1], 121);
}

void AlgoReverb::ProcessHall(double inL, double inR, double& lateL, double& lateR)
{
  double dl = inL, dr = inR;
  for (int i = 0; i < 4; i++)
  {
    dl = mDiffL[(size_t)i].Run(dl);
    dr = mDiffR[(size_t)i].Run(dr);
  }
  const double depth = mModNow * 0.0008 * mFs * mSize;
  std::array<double, kLines> y{};
  double sum = 0.;
  for (int i = 0; i < kLines; i++)
  {
    Line& l = mLines[(size_t)i];
    const double mod = depth * std::sin(2. * kPi * mModPhase[(size_t)i]);
    mModPhase[(size_t)i] += mModRate[(size_t)i] / mFs;
    if (mModPhase[(size_t)i] >= 1.) mModPhase[(size_t)i] -= 1.;
    y[(size_t)i] = mHallDamp[(size_t)i].Run(l.d.Read(std::max(2., l.length + mod))) * mLineGain[(size_t)i];
    sum += y[(size_t)i];
  }
  // Householder: lossless, every line feeds every other.
  const double k = 2. / kLines * sum;
  lateL = lateR = 0.;
  for (int i = 0; i < kLines; i++)
  {
    mLines[(size_t)i].d.Write((float)(y[(size_t)i] - k + 0.5 * (i % 2 ? dr : dl)));
    lateL += kHallOutL[i] * y[(size_t)i];
    lateR += kHallOutR[i] * y[(size_t)i];
  }
}

void AlgoReverb::Process(float inL, float inR, float& outL, float& outR)
{
  if (mDirty)
    Build();
  mInGate += ((mFreeze ? 0. : 1.) - mInGate) * mGateCoef;
  if (mFreeze && mInGate < 1e-4)
    mInGate = 0.; // shut: nothing more gets in
  // Frozen, the modulation fades out: reads on whole samples lose nothing, so the hold is exact.
  mModNow += ((mFreeze ? 0. : mMod) - mModNow) * mGateCoef * 0.1;
  const double l = inL * mInGate, r = inR * mInGate;

  // Early reflections from the (gated) input.
  mEr.Write((float)(0.5 * (l + r)));
  double erL = 0., erR = 0.;
  for (int i = 0; i < kTaps; i++)
  {
    const double v = mEr.ReadInt(std::max(1, (int)mTapTime[(size_t)i]));
    erL += mTapGainL[(size_t)i] * v;
    erR += mTapGainR[(size_t)i] * v;
  }

  double lateL, lateR;
  if (mEngine == kPlate)
    ProcessPlate(l, r, lateL, lateR);
  else
    ProcessHall(l, r, lateL, lateR);
  lateL *= mLateNorm;
  lateR *= mLateNorm;
  outL = (float)((mEarly * erL + mLate * lateL) * mBalanceGain);
  outR = (float)((mEarly * erR + mLate * lateR) * mBalanceGain);
}

} // namespace underheard::fx
