#pragma once
// Shared by the Drifter tests: where the plugins are, and a Drifter state made by hand.
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

static const int kOwnParams = 8, kSlots = 128; // Drifter: Drift, Timing, Length, Bars, Curve, Smear, Reach, Gravity; then the slots

static std::string Plugin(const char* name) { return std::string(std::getenv("HOME")) + "/Library/Audio/Plug-Ins/VST3/" + name; }

// A Drifter state: its own settings, a synth (path + its state) and lanes (id, home, lo, hi).
struct LaneSpec { uint32_t id; double home, lo, hi; };
static std::vector<uint8_t> DrifterState(const std::string& synth, const std::vector<uint8_t>& comp, const std::vector<uint8_t>& ctrl, const std::vector<LaneSpec>& lanes,
                                         double lengthSeconds, int showLanes = -1)
{
  std::vector<uint8_t> d;
  auto put = [&](const void* p, size_t n) { d.insert(d.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
  auto put32 = [&](int32_t v) { put(&v, 4); };
  auto putD = [&](double v) { put(&v, 8); };
  auto putStr = [&](const std::string& s) { put32((int32_t)s.size()); put(s.data(), s.size()); };
  put32('DRFT'); put32(showLanes < 0 ? 1 : 2); put32(kOwnParams + kSlots); // (version 2 adds the lanes panel's toggle)
  for (double v : {1., 0., lengthSeconds, 4., 1., 30., 100., 30.}) // drift on, seconds, Length, (4 bars), smooth, smear, full reach, gravity
    putD(v);
  for (int i = 0; i < kSlots; i++)
    putD(0.);
  putStr(synth); put32(0); putStr("");
  put32((int32_t)comp.size()); put(comp.data(), comp.size());
  put32((int32_t)ctrl.size()); put(ctrl.data(), ctrl.size());
  put32((int32_t)lanes.size());
  for (const auto& l : lanes)
  {
    put(&l.id, 4); putD(l.home); putD(l.lo); putD(l.hi); put32(1);
  }
  if (showLanes >= 0)
    put32(showLanes);
  put32(0); // iPlug2's bypass flag
  return d;
}

