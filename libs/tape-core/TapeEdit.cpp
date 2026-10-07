#include "TapeEdit.h"

#include <algorithm>
#include <cmath>

namespace underheard {

namespace {

int64_t Mod(int64_t x, int64_t m)
{
  x %= m;
  return x < 0 ? x + m : x;
}

double ModD(double x, double m) { return x - m * std::floor(x / m); }

} // namespace

int64_t TapeEdit::NewLength() const
{
  int64_t n = 0;
  for (int i = 0; i < count; i++)
    n += pieces[i].len;
  return n;
}

int64_t TapeEdit::PieceStart(int i) const
{
  int64_t out = 0;
  for (int j = 0; j < i; j++)
    out += pieces[j].len;
  return out;
}

bool TapeEdit::Map(double oldPos, double& newPos) const
{
  const double L = (double)oldLength;
  int64_t out = 0;
  for (int i = 0; i < count; i++)
  {
    const TapePiece& p = pieces[i];
    // Offset into the piece, in its reading direction. A backward piece reads old frames
    // src, src-1, ... so old position x lands at src - x (frame for frame); the fraction just
    // past src counts as its first frame.
    const double off = p.dir > 0 ? ModD(oldPos - (double)p.src, L) : ModD((double)p.src - oldPos + 1., L) - 1.;
    if (off > -1. && off < (double)p.len)
    {
      newPos = (double)out + std::max(0., off);
      return true;
    }
    out += p.len;
  }
  return false;
}

double TapeEdit::MapHead(double oldPos) const
{
  double n = 0.;
  if (Map(oldPos, n))
    return n;
  // Cut away: continue from the join that replaced it. Every edit here puts a join at 0.
  return 0.;
}

int64_t TapeEdit::SourceFrame(int64_t k) const
{
  int64_t out = 0;
  for (int i = 0; i < count; i++)
  {
    const TapePiece& p = pieces[i];
    if (k < out + p.len)
      return Mod(p.src + p.dir * (k - out), oldLength);
    out += p.len;
  }
  return 0;
}

bool TapeEdit::StartsJoin(int i) const
{
  const TapePiece& p = pieces[i];
  const TapePiece& prev = pieces[(i + count - 1) % count];
  const int64_t prevLast = Mod(prev.src + prev.dir * (prev.len - 1), oldLength);
  return !(prev.dir == p.dir && Mod(prevLast + prev.dir, oldLength) == p.src);
}

TapeEdit MakeRemove(int64_t L, int64_t start, int64_t len, int64_t minLength)
{
  TapeEdit e;
  e.oldLength = L;
  if (len <= 0 || L - len < minLength)
    return e;
  // What's left, starting right after the cut: the join lands on the new seam.
  e.pieces[0] = {Mod(start + len, L), L - len, 1};
  e.count = 1;
  return e;
}

TapeEdit MakeIsolate(int64_t L, int64_t start, int64_t len, int64_t minLength)
{
  TapeEdit e;
  e.oldLength = L;
  if (len < minLength || len >= L)
    return e;
  e.pieces[0] = {Mod(start, L), len, 1};
  e.count = 1;
  return e;
}

TapeEdit MakeReverse(int64_t L, int64_t start, int64_t len)
{
  TapeEdit e;
  e.oldLength = L;
  if (len < 2 || len > L)
    return e;
  // The flipped cut first, then the rest of the loop after it.
  e.pieces[0] = {Mod(start + len - 1, L), len, -1};
  e.count = 1;
  if (len < L)
  {
    e.pieces[1] = {Mod(start + len, L), L - len, 1};
    e.count = 2;
  }
  return e;
}

void RenderEdit(const TapeEdit& e, TapeStorage& from, TapeStorage& to, int xfade)
{
  const int64_t n = e.NewLength();
  for (int64_t k = 0; k < n; k++)
  {
    const float* s = from.Frame(e.SourceFrame(k));
    float* d = to.Frame(k);
    d[0] = s[0];
    d[1] = s[1];
  }

  // At each join, let the old tape run on past the end of the previous piece and fade from
  // it into the new piece, so the waveform doesn't jump.
  for (int i = 0; i < e.count; i++)
  {
    if (!e.StartsJoin(i))
      continue;
    const TapePiece& prev = e.pieces[(i + e.count - 1) % e.count];
    const int64_t prevLast = prev.src + prev.dir * (prev.len - 1);
    const int64_t at = e.PieceStart(i);
    const int64_t x = std::min<int64_t>(xfade, e.pieces[i].len);
    for (int64_t j = 0; j < x; j++)
    {
      const float w = (float)(j + 1) / (float)(x + 1);
      const float* run = from.Frame(Mod(prevLast + prev.dir * (j + 1), e.oldLength));
      float* d = to.Frame(at + j);
      d[0] = d[0] * w + run[0] * (1.f - w);
      d[1] = d[1] * w + run[1] * (1.f - w);
    }
  }
}

} // namespace underheard
