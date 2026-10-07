#pragma once
// Hearing a room from somewhere else: through a wall (with a door that can open), or through
// the floor from below. Linear, so it's baked into the impulse response. Non-real-time.
//
// The source room is collected at the wall as one radiator (its channels summed), filtered by
// the partition (and the door), then heard in the listener's room through the listener's mic.

#include <vector>

namespace underheard::room {

enum class Where { SameRoom = 0, NextRoom, FloorBelow };

// The partition's filter: mono FIR at `sampleRate`. `door` (0 closed .. 1 open) only matters
// next door.
std::vector<float> OcclusionFilter(Where where, double door, double sampleRate);

// Full linear convolution (length a + b - 1). Non-real-time.
std::vector<float> Convolve(const std::vector<float>& a, const std::vector<float>& b);

// The source room heard from `where`: sourceRoom (1-2 channels) summed, through the
// partition, then through each channel of listenerRoom. Returns one channel per listener
// channel, at most `maxSeconds` long. SameRoom returns sourceRoom unchanged.
std::vector<std::vector<float>> Occlude(const std::vector<std::vector<float>>& sourceRoom, Where where, double door,
                                        const std::vector<std::vector<float>>& listenerRoom, double sampleRate, double maxSeconds = 6.5);

} // namespace underheard::room
