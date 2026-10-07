#pragma once
// Razor cuts: a new loop described as pieces of the old one.
//
// A TapeEdit lists up to two pieces of the old loop (each read forward or backward, wrapping
// around the old loop) that are laid end to end to make the new loop. It can map positions
// from the old loop to the new one, so playback carries on from the matching spot, and build
// the new tape with short crossfades at every join.
//
// MakeX() and the mapping functions are real-time safe. RenderEdit() isn't (it copies a whole
// loop) and runs on the main thread.

#include "TapeStorage.h"

#include <cstdint>

namespace underheard {

struct TapePiece
{
  int64_t src = 0;   // first old frame read
  int64_t len = 0;   // frames
  int dir = 1;       // +1 forward, -1 backward
};

struct TapeEdit
{
  static constexpr int kMaxPieces = 2;
  TapePiece pieces[kMaxPieces];
  int count = 0;
  int64_t oldLength = 0;

  int64_t NewLength() const;
  // Whether `oldPos` survives the edit; if so, `newPos` is where it ends up.
  bool Map(double oldPos, double& newPos) const;
  // Where the head goes: surviving positions map directly, cut ones to the join after them.
  double MapHead(double oldPos) const;
  // The old frame that new frame k comes from.
  int64_t SourceFrame(int64_t k) const;
  // Whether piece i starts a join (its first frame doesn't follow on from the frame before it).
  bool StartsJoin(int i) const;
  int64_t PieceStart(int i) const; // new-tape position of piece i
};

// Each takes the old loop length and the cut: `len` frames starting at `start`, going forward
// (wrapping). Returns count == 0 if the result would be too short.
TapeEdit MakeRemove(int64_t loopLength, int64_t start, int64_t len, int64_t minLength);
TapeEdit MakeIsolate(int64_t loopLength, int64_t start, int64_t len, int64_t minLength);
TapeEdit MakeReverse(int64_t loopLength, int64_t start, int64_t len);

// Builds the new tape into `to` (which must have room for NewLength() frames), crossfading
// over `xfade` frames at every join by letting the old tape run on past the cut and fading
// into the next piece.
void RenderEdit(const TapeEdit& edit, TapeStorage& from, TapeStorage& to, int xfade);

} // namespace underheard
