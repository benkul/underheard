#pragma once
// Drifter's lanes (docs/DRIFTER-SPEC.md): up to 8 of a hosted synth's parameters, each drifting
// slowly around its home within its range, driven by the drift engine (Drifter.h).
//
// - A lane: a parameter (by ID), its home (normalised 0..1), its range (lo..hi, normalised), on
//   or off. The engine's offset (-1..1) moves it from home toward the bottom of the range or the
//   top, each side scaled to its own room (so a lane near one end doesn't pin against it).
// - Timing: Length in seconds or in bars (at the song's tempo and meter). The drift moves only
//   while the song plays; stopped, the lanes hold where they are.
// - Grabbing: a begin-edit holds the lane at the user's value; each perform-edit makes that value
//   its home; the end-edit makes it the new home with no offset (no jump), and the drift
//   carries on from there. A change with no begin (automation through a slot) moves the home
//   and the drift rides on top. A home outside the range widens the range to include it.
// - Learn: the next edit to a parameter that isn't already a lane adds it.
//
// Single-threaded (the plugin calls it on the audio thread). No allocation. Framework-free.

#include "Drifter.h"

#include <cstdint>

namespace underheard {

class DriftLanes
{
public:
  static constexpr int kMaxLanes = 8;
  enum class Edit { kBegin, kPerform, kEnd };

  struct Lane
  {
    uint32_t id = 0;
    double home = 0.5, lo = 0., hi = 1.;
    bool on = true, held = false;
  };

  // A parameter change for the hosted synth.
  struct Change
  {
    uint32_t id;
    double value;
  };

  DriftLanes() { mDrift.Reset(0); }

  Drifter& Engine() { return mDrift; }
  const Drifter& Engine() const { return mDrift; }

  // ---- Lanes
  int Count() const { return mCount; }
  const Lane& LaneAt(int i) const { return mLanes[i]; }
  int Find(uint32_t id) const
  {
    for (int i = 0; i < mCount; i++)
      if (mLanes[i].id == id)
        return i;
    return -1;
  }
  // Adds a lane at `home` (whole range by default). Returns its index, or -1 (full, or a lane).
  int Add(uint32_t id, double home, double lo = 0., double hi = 1.)
  {
    if (mCount >= kMaxLanes || Find(id) >= 0)
      return -1;
    Lane& l = mLanes[mCount];
    l = Lane{};
    l.id = id;
    l.lo = std::clamp(std::min(lo, hi), 0., 1.);
    l.hi = std::clamp(std::max(lo, hi), 0., 1.);
    SetHome(l, home);
    mCount++;
    mDrift.SetCount(mCount);
    return mCount - 1;
  }
  void Remove(int i)
  {
    if (i < 0 || i >= mCount)
      return;
    for (int k = i; k + 1 < mCount; k++)
      mLanes[k] = mLanes[k + 1];
    mCount--;
    mDrift.RemoveTarget(i);
  }
  void SetRange(int i, double lo, double hi)
  {
    if (i < 0 || i >= mCount)
      return;
    mLanes[i].lo = std::clamp(std::min(lo, hi), 0., 1.);
    mLanes[i].hi = std::clamp(std::max(lo, hi), 0., 1.);
    SetHome(mLanes[i], mLanes[i].home);
  }
  void SetOn(int i, bool on)
  {
    if (i >= 0 && i < mCount)
      mLanes[i].on = on;
  }
  void Clear()
  {
    mCount = 0;
    mDrift.SetCount(0);
  }

  // ---- Timing
  void SetLengthSeconds(double s) { mBars = false; mSeconds = std::max(0.05, s); }
  void SetLengthBars(double bars) { mBars = true; mLengthBars = std::max(0.25, bars); }
  // Moves the drift on by one block (only while playing). tempo in BPM, beatsPerBar from the
  // song's meter.
  void Advance(double blockSeconds, bool playing, double tempo, double beatsPerBar)
  {
    mDrift.SetLength(mBars ? mLengthBars * beatsPerBar * 60. / std::max(1., tempo) : mSeconds);
    if (playing)
      mDrift.Advance(blockSeconds);
  }

  // ---- What the synth should hear: each lane's value now.
  double Value(int i) const
  {
    const Lane& l = mLanes[i];
    if (!l.on || l.held)
      return l.home;
    // Each side of home scaled to its own room, so a lane near one end of its range doesn't sit
    // pinned against it: -1 reaches the bottom of the range, +1 the top.
    const double off = mDrift.Offset(i);
    return std::clamp(l.home + off * (off < 0. ? l.home - l.lo : l.hi - l.home), l.lo, l.hi);
  }
  // Writes the lanes' values (all of them, every block; at most kMaxLanes). Returns how many.
  int Changes(Change* out) const
  {
    for (int i = 0; i < mCount; i++)
      out[i] = {mLanes[i].id, Value(i)};
    return mCount;
  }

  // ---- Edits from the synth's editor, or the host's automation through a slot.
  // Returns the lane the edit touched (-1 if none; learn may add one).
  int OnEdit(Edit e, uint32_t id, double value)
  {
    int i = Find(id);
    if (i < 0 && mLearning && e != Edit::kEnd)
    {
      i = Add(id, value);
      mLearning = false;
      mLearned = i;
    }
    if (i < 0)
      return -1;
    Lane& l = mLanes[i];
    switch (e)
    {
      case Edit::kBegin: l.held = true; break;
      case Edit::kPerform:
        SetHome(l, value);
        if (l.held)
          mDrift.RebaseTarget(i); // while held, the user's value is the value
        break;
      case Edit::kEnd:
        l.held = false;
        mDrift.RebaseTarget(i); // the new home, no jump; the drift carries on from there
        break;
    }
    return i;
  }
  // Automation through a slot: the home moves, the drift rides on top.
  int OnAutomation(uint32_t id, double value)
  {
    const int i = Find(id);
    if (i >= 0 && !mLanes[i].held)
      SetHome(mLanes[i], value);
    return i;
  }

  // ---- Learn: the next edit to a parameter that isn't a lane adds it.
  void StartLearn() { mLearning = true; mLearned = -1; }
  void CancelLearn() { mLearning = false; }
  bool Learning() const { return mLearning; }
  int Learned() const { return mLearned; } // the lane learn added, or -1

  // ---- KEEP: where the lanes have drifted becomes their homes. RETURN: glide home.
  void Keep()
  {
    for (int i = 0; i < mCount; i++)
      mLanes[i].home = Value(i);
    mDrift.Rebase();
  }
  void Return() { mDrift.Return(); }

private:
  static void SetHome(Lane& l, double home)
  {
    l.home = std::clamp(home, 0., 1.);
    l.lo = std::min(l.lo, l.home); // a home outside the range widens it
    l.hi = std::max(l.hi, l.home);
  }

  Drifter mDrift;
  Lane mLanes[kMaxLanes];
  int mCount = 0;
  bool mBars = false, mLearning = false;
  double mSeconds = 20., mLengthBars = 8.;
  int mLearned = -1;
};

} // namespace underheard
