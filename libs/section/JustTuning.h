#pragma once
// Section's Settle: where each held note would sit in pure (just) intonation.
//
// The held notes are tuned over a root, as 5-limit ratios (the 3rds and 6ths of the harmonic
// series: a major 3rd 14 cents flat of equal temperament, a minor 3rd 16 sharp, 5ths 2 sharp).
// The root is the held note that makes all the ratios simplest (lowest Tenney height), so a
// minor chord settles over its own root, not just the lowest note. The root itself stays where
// equal temperament puts it, so the section doesn't wander off pitch.
//
// Framework-free; no allocation.

#include <cmath>
#include <cstdint>

namespace underheard::section {

struct Ratio
{
  int num, den;
};

// The pure ratio for each interval class above the root (0 = unison .. 11 = major 7th).
inline const Ratio* JustRatios()
{
  static const Ratio r[12] = {{1, 1}, {16, 15}, {9, 8}, {6, 5}, {5, 4}, {4, 3}, {45, 32}, {3, 2}, {8, 5}, {5, 3}, {9, 5}, {15, 8}};
  return r;
}

// Cents away from equal temperament of each interval class, in pure tuning.
inline double JustOffsetCents(int intervalClass)
{
  const Ratio& r = JustRatios()[((intervalClass % 12) + 12) % 12];
  return 1200. * std::log2((double)r.num / r.den) - 100. * (((intervalClass % 12) + 12) % 12);
}

// How complicated an interval is (Tenney height: log2(num * den)).
inline double Complexity(int intervalClass)
{
  const Ratio& r = JustRatios()[((intervalClass % 12) + 12) % 12];
  return std::log2((double)r.num * r.den);
}

// The root for a set of held notes (MIDI numbers): the one giving the simplest ratios to all
// the others. Ties go to the lowest note. Returns -1 for no notes.
inline int ChooseRoot(const int* notes, int n)
{
  int best = -1;
  double bestScore = 1e9;
  for (int i = 0; i < n; i++)
  {
    double score = 0.;
    for (int j = 0; j < n; j++)
      score += Complexity(notes[j] - notes[i]);
    if (score < bestScore - 1e-9 || (std::fabs(score - bestScore) < 1e-9 && notes[i] < notes[best]))
    {
      bestScore = score;
      best = i;
    }
  }
  return best < 0 ? -1 : notes[best];
}

// Each held note's pure-tuning offset from equal temperament, in cents, written to `cents`.
inline void JustOffsets(const int* notes, int n, double* cents)
{
  const int root = ChooseRoot(notes, n);
  for (int i = 0; i < n; i++)
    cents[i] = root < 0 ? 0. : JustOffsetCents(notes[i] - root);
}

} // namespace underheard::section
