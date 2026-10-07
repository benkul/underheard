# Section: an ensemble of imperfect players (spec)

**Status:** redesign agreed with the user 2026-10-07. Built through stage 4 (2026-10-07): the
plugin is playable with all of this; stage 5 (docs) and the user's listening are next.

## What Section is
A MIDI instrument where every note is taken up by a small group of players who behave like
people: they come in a little apart, swell, add vibrato late, drift, and on a held chord tune to
each other until it rings pure. The players are what make Section unique (the user owns plenty
of conventional synths), so the voice they play is a capable, familiar synth voice, and the
players reach inside it, so each one sounds slightly different.

**Kept from the first version:** the players and their behaviour, Settle (pure intervals over
the chord's root), Smear, the desks by register, Room Bleed's rooms and mics, Warmth, 8 notes x
up to 6 players, and the MIDI handling.

**Replaced:** the fixed string voice (a sawtooth through a hidden "bow" low-pass), the Bow
knob, and the VOICE page built on 2026-10-07 (Wave, Pulse Width, Bowing). The user judged that
page wrong: it only swapped the oscillator shape in front of a chain tuned for strings.

## How the sound is made (new)
Per player: **wavetable oscillators A + B → multimode filter → amp envelope → noise layer**.
Then per desk: **resonator (body)**. Then: **room** (or close panning) → **Warmth** → Output.

## The panel: three tabs
Always visible: title, TEMPLATES, Output and meter, the keyboard.

### VOICES: the synth voice every player plays
- **Players** per note, 1–6.
- **Oscillator A** and **Oscillator B**, the same controls each:
  - **Table:** a menu with the factory tables, recently loaded tables, and **Load…** (a file
    dialog for one .wav). Each oscillator plays one table.
  - **Position:** morphs smoothly through the table's frames.
  - **Octave**, **Semi**, **Fine**.
  - **Level** (0 = off).
- **Filter:** **Type** (low-pass, band-pass, high-pass), **Cutoff**, **Resonance**, **Key track**,
  **Env amount** (from the filter envelope), **Velocity** (velocity → cutoff).
- **Filter envelope:** Attack, Decay, Sustain, Release.
- **Amp envelope:** Attack, Decay, Sustain, Release, and **Velocity** (how much velocity sets
  loudness).
- **Controllers:** **Mod wheel** and **Aftertouch**, each with a **Target** (Off, Cutoff,
  Resonance, Position, Vibrato depth, Level, Noise amount) and a bipolar **Amount**. Defaults:
  mod wheel → Cutoff, aftertouch → Vibrato depth. They feed the same per-player target table as
  Character and Drift.

### EFFECTS: what's done to the voices
- **Ensemble** (the players' behaviour): **Looseness**, **Smear**, **Settle**, **Vibrato** and
  **Onset**.
- **Character and Drift**, with a depth per target, laid out as a grid:

  |           | Pitch | Position | Cutoff |
  |-----------|-------|----------|--------|
  | Character | depth | depth    | depth  |
  | Drift     | depth | depth    | depth  |

  **Character** gives each player fixed offsets (decided at note-on); **Drift** makes them
  wander slowly. **Position** moves both oscillators' positions together.
- **Colour:**
  - **Noise layer** (was Bow Noise): pitch-synced noise bursts per player, **after the
    filter**. **Amount** and **Tone**.
  - **Resonator** (was Body): **Body** = By register (default: violin, viola, cello or bass
    body depending on the desk, as now), Violin, Viola, Cello, Double bass, or Off; **Depth**.
  - **Warmth**.

### ROOM
The room drawing at full size (room, desks, mics), **Room** preset, **Size**, **Surfaces**,
**Distance**, **Mics**, **Pair**, **Close / Room**, **Width**.

## Wavetables
- **What a table is:** a .wav of single-cycle frames back to back (typically 256 frames of
  2048 samples). Position picks and morphs between frames.
- **Reused from HORSI** (`horsi-vst3/HorsiWave/dsp`), copied into `libs/wavetable` and trimmed:
  - the **decoder**: Serum (`clm` chunk), Vital and CHOMPI multiples of 2048, WaveEdit 64 x 256,
    single cycles (AKWF-style), other cycle lengths resampled; any bit depth; mono mixdown;
    tables over 256 frames thinned;
  - the **band-limited copies** (11 levels) so bright tables don't alias on high notes;
  - the oscillator's **smooth Position** morph and clean (band-limited) playback.
- **Not reused:** HORSI's library folder, index and browser, the 8 slots, fingerprints and
  in-project fallback copies, the hardware-step and raw modes.
- **Loading and storing:** Load… copies the chosen file into `~/Music/Underheard/Section/` by
  content (the same convention as Splicer and Room Bleed), and the project saves that copy's
  path and hash. Decoding happens off the audio thread; a new table is handed to the voices
  the same way Room Bleed hands over a new convolver. (Keep packs out of Desktop, Documents and
  Downloads: inside GarageBand, macOS privacy blocks reading their subfolders.)
- **Factory tables:** Sine, Triangle, Saw, Square; and tables generated from spectra:
  **Bowed string** (light to heavy bowing across the frames), **Vowels** (ah → eh → ee → oh →
  oo), **Organ** (drawbar registrations); then, added 2026-10-07 at the user's request, horsi-vst3's
  eight **horse tables** (Whinny, Stallion, Call, Breath, Huff, Rasp, Whinny to Breath, Human),
  compiled in from `assets/wavetables/horsi` (credits there: CC BY 4.0 research recordings,
  one public domain, one CC0) and shown as "Horse: …" after the generated tables.

## Per-player modulation (code)
Each player carries one table of modulation offsets, one entry per target (pitch, position,
cutoff, resonance, vibrato depth, level, noise amount). Character, Drift, Smear, Settle, vibrato and the two controllers each add their contribution to the targets
they affect, and the voice reads the totals once per sample. Adding a target later (level,
pan, resonance, envelope times) means one new entry and the code that reads it, not a
restructure.

## Saving
A new state version. Section projects and presets from before the redesign won't load their
settings (agreed with the user).

## Decided (2026-10-07)
1. **Controllers:** the mod wheel and aftertouch are assignable (see VOICES).
2. **Filter:** a 12 dB/oct state-variable filter (low, band, high pass from one design); easy
   to change later.
3. **Default sound:** Oscillator A on Bowed string, B off, a gentle low-pass: a new Section
   starts as a string section.
4. **Templates:** only the default is rebuilt; the TEMPLATES menu holds just that for now.

## Build stages (after the go-ahead)
1. `libs/wavetable`: the decoder, the table data and the oscillator from HORSI, trimmed; tests
   (formats, band-limiting, Position morph).
2. The voice: two oscillators, the filter, two envelopes, the per-player modulation table with
   Character and Drift targets; tests.
3. The colour effects as standalone modules (noise layer, resonator with body choice); tests.
4. The plugin: parameters, the three tabs, table menus and loading, storage, the controllers,
   the default template, state;
   the AU host test, auval, the VST3 validator.
5. Docs.

I'll report back after each stage. No git commits at any point.
