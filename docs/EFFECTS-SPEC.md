# underheard-delay, -chorus, -reverb — standalone effects (spec, 2026-10-04)

Three full plugins with Room Bleed's look. They're warm by design (see the warmth goal in
STATUS): saturation and softened highs are part of each effect's character, not add-ons. The
DSP lives in `libs/fx` as new, fuller classes, and Splicer's built-in chain uses them too
(Plate and Hall only; see STATUS).

**Build order:** delay → chorus → reverb.

## underheard-delay: a tape echo that flexes into dub
- **Time:** in ms (up to 2 s), or **synced** to note values (1/32 to 1/1, straight, dotted or
  triplet). Changing it glides like tape. **Glide** sets how fast: a quick slur, or a long wind.
- **Feedback** from 0 to 110%: past 100% it runs away, held musical by the loop's saturation.
  **Polarity** (normal or inverted) flips the repeats for a hollow, out-of-phase sound.
- **Heads:**
  - **Single;**
  - **Ping-pong:** repeats alternate sides;
  - **Multi-head:** three playback heads at ⅓, ⅔ and 1× the time, Space Echo-style, chosen by a
    head selector (1, 2, 3, 1+2, 1+3, 2+3, 1+2+3).
- **In the loop:**
  - **Low cut** and **High cut**, with **Resonance** on the high cut, for dub filter sweeps;
  - **Drive** (saturation);
  - **Age** (each repeat a little darker and grittier than the last).
- **Tape:** Wow and Flutter amounts. **Phase** (0–360°) offsets the right side's tape
  wobble from the left's.
- **Stereo:** **Spread** (a left/right time offset, or the width of the ping-pong).
- **Performance:**
  - **Throw:** with Send set to Throw, only what's played while THROW is held goes into the
    delay;
  - **Freeze:** holds the loop as it is, unchanging;
  - **Duck:** the repeats dip while you play.
- **Output:** Mix (dry ⟷ wet) and Output.
- **Templates:** Tape echo, Dub echo, Dub throw, Slapback, Large atmosphere, Ping-pong eighths,
  Space echo (in the host's preset list and the panel's TEMPLATES menu).

## underheard-chorus
- **Voices** (1–4), **Rate** (Hz, or synced to note values), **Depth**, **Delay** (the
  centre time), **Spread** (stereo).
- **Shape:** sine, triangle, or **Wander** (random, tape-like).
- **Mode:** chorus, vibrato (wet only), ensemble (slow and fast motion together).
  **Feedback** pushes it toward a flanger.
- **Phase** (0–360°): the right side's offset from the left. When synced, the movement locks to
  the song position.
- **Tone, Age** (tape warble and a softened top), Mix, Output.
- **Templates:** Gentle chorus, Wide stereo, Vibrato, Ensemble strings, Tape warble, Jet
  flanger, Dimension.

## underheard-reverb
- **Engines:**
  - **Rooms:** Room Bleed's room engine, with presets, Size, Surfaces, source and mic placement
    (the drawing), and mic choice;
  - **Recordings:** loaded IRs;
  - **Plate** and **Hall:** algorithmic, long, smooth and modulated.
- **Shaping:** pre-delay (ms or synced), decay stretch, early/late balance, low and high cut,
  width.
- **Character:** tail modulation (Plate/Hall), tape-style softening and saturation on the tail,
  **Freeze**, **Duck**.
- **Output:** Mix, Output.
- **Templates:** Hall, Large hall, Bright plate, Dark plate, Living room, Tiled bathroom, Church
  hall, Frozen pad.
