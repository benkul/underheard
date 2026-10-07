#pragma once
// Tempo-synced note values, shared by the delay and the chorus.

#include <array>

namespace underheard::fx {

struct NoteValue
{
  const char* name;
  double beats; // in quarter notes
};

// From short to long: straight, triplet and dotted values from 1/32 to a whole note, then 2
// and 4 bars (for slow modulation). The delay offers only the first kNumBarOrLess.
constexpr int kNumBarOrLess = 16;
inline const std::array<NoteValue, 18>& NoteValues()
{
  static const std::array<NoteValue, 18> v = {{
    {"1/32", 0.125}, {"1/16T", 0.25 * 2. / 3.}, {"1/32D", 0.125 * 1.5}, {"1/16", 0.25},
    {"1/8T", 0.5 * 2. / 3.}, {"1/16D", 0.25 * 1.5}, {"1/8", 0.5}, {"1/4T", 2. / 3.},
    {"1/8D", 0.75}, {"1/4", 1.}, {"1/2T", 4. / 3.}, {"1/4D", 1.5},
    {"1/2", 2.}, {"1/1T", 8. / 3.}, {"1/2D", 3.}, {"1/1", 4.},
    {"2 bars", 8.}, {"4 bars", 16.},
  }};
  return v;
}

inline double NoteSeconds(int index, double bpm)
{
  const auto& v = NoteValues();
  const int i = index < 0 ? 0 : (index >= (int)v.size() ? (int)v.size() - 1 : index);
  return v[(size_t)i].beats * 60. / (bpm > 0. ? bpm : 120.);
}

} // namespace underheard::fx
