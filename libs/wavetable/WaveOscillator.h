#pragma once
// A wavetable oscillator: a smooth Position that morphs between frames, and band-limited
// playback (it picks the table's band-limited copy for the note, so bright tables don't alias).
//
// Trimmed from horsi-vst3's WaveOscillator (HorsiWave/dsp): only its Smooth + Clean mode, with
// a short crossfade when the table is swapped. The hardware-step and raw modes are gone.
//
// Tables are shared, read-only, by every oscillator. No allocation; real-time safe.

#include "WavetableData.h"

#include <algorithm>
#include <cmath>

namespace underheard::wavetable
{

class WaveOscillator
{
public:
  static constexpr int kSize = WavetableData::kFrameSize;
  static constexpr int kFadeLength = 480; // a table swap crossfades over 10 ms at 48 kHz

  void Prepare(double sampleRate)
  {
    mFs = sampleRate;
    mPosCoef = 1. - std::exp(-1. / (0.01 * sampleRate)); // Position slews over ~10 ms
  }

  // Plays `table` from now on (nullptr: silence). A table already playing fades out.
  void SetTable(const WavetableData* table)
  {
    if (table == mTable)
      return;
    if (mTable && table)
    {
      mLast = mTable;
      mFade = 0;
    }
    else
      mLast = nullptr;
    mTable = table;
  }

  // Forgets a table about to be freed.
  void ReleaseTable(const WavetableData* table)
  {
    if (mLast == table)
      mLast = nullptr;
    if (mTable == table)
      mTable = nullptr;
  }

  const WavetableData* Table() const { return mTable; }

  void SetFrequency(double hz) { mInc = std::max(0., hz) * kSize / mFs; }
  void SetPhase(double phase01) { mPhase = (phase01 - std::floor(phase01)) * kSize; }
  // 0..1 across the table's frames, slewed. `jump` skips the slew (a note's start).
  void SetPosition(double pos01, bool jump = false)
  {
    mTargetPos = std::clamp(pos01, 0., 1.);
    if (jump)
      mPos = mTargetPos;
  }

  float Process()
  {
    mPos += (mTargetPos - mPos) * mPosCoef;
    UpdateLevel();
    float out = mTable ? Sample(mTable, mPos) : 0.f;
    if (mLast)
    {
      const float fade = (float)mFade / kFadeLength;
      out = Sample(mLast, mPos) * (1.f - fade) + out * fade;
      if (++mFade >= kFadeLength)
        mLast = nullptr;
    }
    mPhase += mInc;
    while (mPhase >= kSize)
      mPhase -= kSize;
    return out;
  }

private:
  // The band-limited level for the current pitch. The richer level used always has its top
  // harmonic below Nyquist; as that harmonic passes 70% of Nyquist the next level (half the
  // harmonics) fades in, so the timbre doesn't jump as the pitch moves.
  void UpdateLevel()
  {
    if (mInc == mLevelForInc)
      return;
    mLevelForInc = mInc;
    auto top = [&](int level) { return (double)((kSize / 2) >> level) * mInc / kSize; }; // as a fraction of the sample rate
    int level = 0;
    while (level < WavetableData::kNumLevels - 1 && top(level) > 0.5)
      level++;
    mLevel0 = level;
    mLevel1 = std::min(level + 1, WavetableData::kNumLevels - 1);
    mLevelMix = mLevel1 == mLevel0 ? 0.f : (float)std::clamp((top(level) - 0.35) / 0.15, 0., 1.);
  }

  float SampleLevel(const WavetableData* t, int level, int frame) const
  {
    if (level == 0)
    {
      const float* f = t->RawFrame(frame);
      const int i = std::min((int)mPhase, kSize - 1);
      const float frac = (float)(mPhase - i);
      return f[i] + frac * (f[(i + 1) & (kSize - 1)] - f[i]);
    }
    const int len = t->LevelLength(level);
    const double pos = mPhase * ((double)len / kSize);
    const int i = std::min((int)pos, len - 1);
    const float frac = (float)(pos - i);
    const float* f = t->LevelFrame(level, frame);
    return f[i] + frac * (f[i + 1] - f[i]); // guard sample at [len]
  }

  float Sample(const WavetableData* t, double pos01) const
  {
    const double framePos = pos01 * (t->NumFrames() - 1);
    const int a = (int)framePos;
    const int b = std::min(a + 1, t->NumFrames() - 1);
    const float mix = (float)(framePos - a);
    auto atFrame = [&](int frame) {
      const float s0 = SampleLevel(t, mLevel0, frame);
      return mLevelMix > 0.f ? s0 + mLevelMix * (SampleLevel(t, mLevel1, frame) - s0) : s0;
    };
    const float sa = atFrame(a);
    return mix > 0.f ? sa + mix * (atFrame(b) - sa) : sa;
  }

  double mFs = 48000., mInc = 0., mPhase = 0.;
  double mPos = 0., mTargetPos = 0., mPosCoef = 1.;
  const WavetableData* mTable = nullptr;
  const WavetableData* mLast = nullptr;
  int mFade = 0;
  double mLevelForInc = -1.;
  int mLevel0 = 0, mLevel1 = 0;
  float mLevelMix = 0.f;
};

} // namespace underheard::wavetable
