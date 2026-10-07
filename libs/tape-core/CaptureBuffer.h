#pragma once
// Always-on rolling recording of an input, for Catch ("that was good, keep it").
//
// Threading: Allocate() is non-real-time. Write() and Publish() run on the audio thread.
// CopyLast() runs on another thread and reads only frames the audio thread has published and
// won't overwrite for a while (the buffer holds a little more than the longest catch).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace underheard {

class CaptureBuffer
{
public:
  // Non-real-time. Clears the buffer.
  void Allocate(int64_t frames)
  {
    mData.assign((size_t)(2 * frames), 0.f);
    mSize = frames;
    mWrite = 0;
    mPublished.store(0, std::memory_order_release);
  }

  int64_t Capacity() const { return mSize; }

  void Write(float l, float r)
  {
    if (mSize == 0)
      return;
    const size_t i = (size_t)(2 * (mWrite % mSize));
    mData[i] = l;
    mData[i + 1] = r;
    mWrite++;
  }

  // Audio thread, once per block: makes the frames written so far visible to CopyLast().
  void Publish() { mPublished.store(mWrite, std::memory_order_release); }

  // Total frames published so far (a timeline position).
  int64_t Published() const { return mPublished.load(std::memory_order_acquire); }

  // Copies the `count` frames ending at timeline position `end` (exclusive), interleaved.
  // Returns false if they aren't all still in the buffer.
  bool CopyRange(int64_t end, int64_t count, std::vector<float>& out) const
  {
    const int64_t published = Published();
    if (count <= 0 || end > published || end - count < 0 || published - (end - count) > mSize)
      return false;
    out.resize((size_t)(2 * count));
    for (int64_t n = 0; n < count; n++)
    {
      const size_t i = (size_t)(2 * ((end - count + n) % mSize));
      out[(size_t)(2 * n)] = mData[i];
      out[(size_t)(2 * n + 1)] = mData[i + 1];
    }
    return true;
  }

private:
  std::vector<float> mData;
  int64_t mSize = 0;
  int64_t mWrite = 0; // audio thread only
  std::atomic<int64_t> mPublished{0};
};

} // namespace underheard
