#pragma once
// Shaping impulse responses for underheard-reverb's convolution engines (Rooms, Recordings).
// Main thread only: these allocate.

#include <vector>

namespace underheard::room {

using Channels = std::vector<std::vector<float>>;

// Where the sound starts: the first sample within 20 dB of the loudest, on any channel.
size_t Onset(const Channels& irs);

// Takes out the direct sound of a recording (the first 2 ms after the onset, fading back in
// by 3.5 ms), so the reverb doesn't repeat the dry sound a moment late.
void RemoveDirect(Channels& irs, double fs);

// The broadband decay time (T20 of the Schroeder integral of the channels' sum, from `from`).
double MeasureDecay(const Channels& irs, double fs, size_t from = 0);

// Stretches a recording's decay by `stretch` (0.25 .. 4): an exponential envelope from the
// mixing time on (relative to the onset). A recording can only be lengthened so far: its end
// fades out.
void StretchDecay(Channels& irs, double fs, double stretch, double mixingTime);

// Early/Late: -1 keeps only what's before the mixing time (absolute, in seconds from the start
// of the impulse), 0 everything, 1 only after it; a 20 ms crossfade.
void Balance(Channels& irs, double fs, double earlyToLate, double mixingTime);

// Fades out the last `fraction` of the impulse (for one cut short of its natural end).
void FadeEnd(Channels& irs, double fraction);

// Scales so the loudest channel has unit energy.
void NormaliseEnergy(Channels& irs);

} // namespace underheard::room
