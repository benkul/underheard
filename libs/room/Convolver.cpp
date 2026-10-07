#include "Convolver.h"

#include "convoengine.h" // WDL

#include <algorithm>
#include <cstring>

namespace underheard::room {

Convolver::Convolver() = default;
Convolver::~Convolver() = default;

std::unique_ptr<Convolver> Convolver::Make(const std::vector<std::vector<float>>& irs, double sampleRate)
{
  if (irs.empty() || irs[0].empty())
    return nullptr;
  std::unique_ptr<Convolver> c(new Convolver());
  c->mChannels = std::min<int>(2, (int)irs.size());
  c->mSampleRate = sampleRate;

  WDL_ImpulseBuffer impulse;
  impulse.samplerate = sampleRate;
  impulse.SetNumChannels(c->mChannels);
  const int len = (int)irs[0].size();
  for (int ch = 0; ch < c->mChannels; ch++)
  {
    WDL_FFT_REAL* dst = impulse.impulses[ch].Resize(len);
    for (int i = 0; i < len; i++)
      dst[i] = i < (int)irs[(size_t)ch].size() ? irs[(size_t)ch][(size_t)i] : 0.f;
  }
  c->mEngine = std::make_unique<WDL_ConvolutionEngine_Div>();
  c->mEngine->SetImpulse(&impulse); // no latency allowed: the first partition is direct
  for (auto& b : c->mIn)
    b.assign(kMaxBlock, 0.f);
  return c;
}

void Convolver::Process(const float* in, float* const* out, int n)
{
  WDL_FFT_REAL* bufs[2] = {mIn[0].data(), mIn[1].data()};
  for (int ch = 0; ch < mChannels; ch++)
    std::memcpy(bufs[ch], in, sizeof(float) * (size_t)n);
  mEngine->Add(bufs, n, mChannels);

  // The engine can lag at the very start; anything it hasn't produced yet is silence.
  const int avail = std::min(mEngine->Avail(n), n);
  const int lead = n - avail;
  WDL_FFT_REAL** got = avail > 0 ? mEngine->Get() : nullptr;
  for (int ch = 0; ch < mChannels; ch++)
  {
    std::fill(out[ch], out[ch] + lead, 0.f);
    if (got)
      std::memcpy(out[ch] + lead, got[ch], sizeof(float) * (size_t)avail);
  }
  if (avail > 0)
    mEngine->Advance(avail);
}

ConvolverSwitch::~ConvolverSwitch()
{
  // The audio thread has stopped by now.
  delete mCurrent;
  delete mFading;
  delete mOffered.load();
  delete mRetired.load();
}

std::unique_ptr<Convolver> ConvolverSwitch::Offer(std::unique_ptr<Convolver> next)
{
  return std::unique_ptr<Convolver>(mOffered.exchange(next.release()));
}

void ConvolverSwitch::Process(const float* in, float* outL, float* outR, int n)
{
  // Long host blocks are taken in pieces the convolvers can handle.
  while (n > Convolver::kMaxBlock)
  {
    Process(in, outL, outR, Convolver::kMaxBlock);
    in += Convolver::kMaxBlock;
    outL += Convolver::kMaxBlock;
    outR += Convolver::kMaxBlock;
    n -= Convolver::kMaxBlock;
  }

  // Claim a new convolver when not already fading (and the retired mailbox has room).
  if (!mFading && mRetired.load() == nullptr)
  {
    if (Convolver* next = mOffered.exchange(nullptr))
    {
      mFading = mCurrent;
      mCurrent = next;
      mFadePos = 0;
    }
  }

  float* a[2] = {mA[0], mA[1]};
  if (mCurrent)
  {
    mCurrent->Process(in, a, n);
    if (mCurrent->Channels() == 1)
      std::memcpy(mA[1], mA[0], sizeof(float) * (size_t)n);
  }
  else
  {
    std::fill(mA[0], mA[0] + n, 0.f);
    std::fill(mA[1], mA[1] + n, 0.f);
  }

  if (mFading)
  {
    float* b[2] = {mB[0], mB[1]};
    mFading->Process(in, b, n);
    if (mFading->Channels() == 1)
      std::memcpy(mB[1], mB[0], sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++)
    {
      const float w = std::min(1.f, (float)(mFadePos + i) / (float)kFadeSamples);
      for (int ch = 0; ch < 2; ch++)
        mA[ch][i] = mA[ch][i] * w + mB[ch][i] * (1.f - w);
    }
    mFadePos += n;
    if (mFadePos >= kFadeSamples)
    {
      mRetired.store(mFading);
      mFading = nullptr;
    }
  }
  else if (mCurrent && mFadePos < kFadeSamples)
  {
    // The very first convolver fades in from silence.
    for (int i = 0; i < n; i++)
    {
      const float w = std::min(1.f, (float)(mFadePos + i) / (float)kFadeSamples);
      mA[0][i] *= w;
      mA[1][i] *= w;
    }
    mFadePos += n;
  }

  std::memcpy(outL, mA[0], sizeof(float) * (size_t)n);
  std::memcpy(outR, mA[1], sizeof(float) * (size_t)n);
}

} // namespace underheard::room
