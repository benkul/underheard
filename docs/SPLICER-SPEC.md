# Splicer — design spec (draft 1)

Part of the **Underheard** suite: Splicer, Room Bleed, Drifter, and the `underheard-*` effects
(`underheard-delay`, `underheard-reverb`, `underheard-chorus`, ...).

Status: agreed 2026-10-03. Build progress is tracked in `docs/STATUS.md`.

---

## 1. What it is

Splicer is a looper in which each loop is a **physical length of tape**. The tape has a splice, a
speed and a motor, and it wears out as it plays. Splicer has four loops. It is designed to be
played live: record, catch, cut, and let things degrade.

Design principles:

- **Objects, not editors.** You act on tape: record onto it, cut it, slow it down. There is no
  waveform editing and no undo stack.
- **Degradation is the default direction.** Each loop's wear can be switched on or off, but
  Splicer is designed around letting it run.
- **Restrictions are good.** Four loops and one play head per loop in v1 (more heads come in a
  later stage). The loop length limit is fixed.
- **Every control is a host parameter**, so anything can be MIDI-mapped in Live, automated, or
  moved by Splicer's drift.

## 2. Signal flow

```
 Main in ───────┬──────────────────────────────────────────────┐ (dry)
                │                                              │
 Sidechain in ──┤──► Catch buffer (rolling, last 30 s)         │
                │            │                                 │
 WAV file ──────┤            ▼                                 │
                ▼      ┌─────────────── Loop 1..4 ───────────┐ │
             source    │ record head ─► TAPE ─► play head    │ │
             select ──►│   (rec sat,      ▲       │          │ │
             per loop  │    erase)        │       ▼          │ │
                       │            wear process  playback   │ │
                       │            (on the tape) (wow/flutter,│ │
                       │                           hiss, HF) │ │
                       └──────────────────────────┬──────────┘ │
                                                  ▼            │
                                             pan          │
                                   ┌──────────┴──────────┐     │
                                   ▼                     ▼     │
                              DRY knob              SEND knob  │
                                   │                     ▼     │
                                   │   shared FX bus: chorus ─►│
                                   │          delay ─► reverb  │
                                   ▼                     ▼     │
                              TAPE = loop drys + FX return     │ INPUT
                                                  ▼            ▼
                                  Mix (input ◄──────► tape) ─► Out
```

## 3. The tape model

### 3.1 The tape
- Each loop is a buffer indexed by **tape position**, not by time. The tape's duration in time
  depends on the speed.
- **Length** is set by the first recording, by a loaded WAV, or by hand. It can be shown in
  seconds (at 1x) or in bars.
- **Maximum length**: 4 minutes of tape at 1x per loop. At 0.5x a full loop lasts 8 minutes.
- **Tape is stored at a fixed internal rate of 48 kHz**, whatever the host rate. Varispeed
  already reads through an interpolator, so host-rate conversion comes for free, and memory
  doesn't double at 96 kHz.
- **Memory is allocated to the recorded length**, not the maximum. See section 8.1 for the
  budget.
- **Sync**: `Free` (the default; loops phase against each other and the song) or `Bars` (the
  length snaps to whole beats or bars of the host tempo when recording ends).

### 3.2 Speed and motor
- **Speed**: 0.25x to 2x, continuous, per loop. It changes pitch and time together, like real
  tape.
- **Records at the current speed.** Recording at 0.5x and playing back at 1x gives an octave up,
  twice as fast. This needs fractional writing ("splatting" samples into the tape at a
  non-integer rate). It is the main technical risk in stage 1.
- **Direction**: forward or reverse.
- **Motor**: play and stop don't happen instantly. The tape spins up and slows down over a
  `Motor` time (0 to 2 s), with the pitch bend that comes with it. It's one of the most
  important controls for live feel.
- **Wow** (slow, about 0.5 to 2 Hz, irregular) and **Flutter** (fast, about 6 to 12 Hz), each
  with its own amount. These are random-modulated, not plain LFOs.

### 3.3 Record path
- **Record saturation**: soft tape compression on the way onto the tape.
- **Erase** (sound-on-sound) controls how much of the existing tape survives each overdub pass:
  `0` adds the new layer on top of everything, `1` replaces the old material.
- **Feedback** sets how much of the loop survives each pass, whether or not you're overdubbing.
  At `100%` the loop lasts forever. Lower values make it fade. The record head rewrites the tape
  as it passes, which is also where wear (stage 3) is applied. *(Changed 2026-10-03 from "a
  playback-to-record path": with the play and record heads 8 frames apart, that would have done
  the same thing as Erase.)*
- **Seam**: when a first take ends, the record head keeps going for 10 ms and crossfades that
  post-roll into the start of the loop, so the seam is continuous. The splice click and dropout
  are a separate control, layered on top.

### 3.4 Playback path
- HF response that depends on speed: slower tape is duller.
- **Hiss**: a noise floor that rises with wear and with lower speeds.
- **Splice**: a click plus a short dropout each time the splice passes the head, with an
  `amount` control. Every Razor cut adds another splice.

## 4. Wear

Wear can be switched on or off **per loop**. Each pass of the head ages the tape it crosses,
and the damage compounds pass after pass, the way a real loop disintegrates. Switching Wear off
stops the aging. The damage already done stays until **Restore**.

**How it's stored** *(changed 2026-10-03 from "two tapes, clean and worn")*: wear is kept as
damage per 5 ms segment of tape (an **age** and an **oxide-loss** amount), not written into the
audio. Playback applies the damage. This sounds the same, and:
- the audio underneath stays clean, so the saved tape is the clean one with no extra copy;
- Restore is instant (it clears the damage);
- age resets on project open automatically;
- memory is half what two tapes would need (see 8.1).

Per pass, scaled by **Wear Rate** (at 100%, a 4 s loop is heavily worn after about 3 minutes):

| Process | Behavior |
|---|---|
| Generation loss | the recording's highs roll off as the segment ages |
| Saturation | soft clipping that grows with age |
| Level loss | worn tape plays a little quieter |
| Oxide shedding | spots appear at random (more often on old tape), grow, and spread to neighboring segments. Shed areas drop out and crackle. |
| Print-through | a ghost of the tape 30 ms either side, growing with age |
| Hiss | the tape noise floor (master **Hiss**) rises with age and at low speed |

- Wear is **uneven**: each segment ages at a slightly different rate.
- **Overdubbing with Erase** makes the tape under the record head new again, in proportion to
  Erase. The oxide damage stays, because that's physical.
- **Age** is shown per loop as the number of passes worn since the last Restore or new take.
  Stage 5 draws the tape patchier as it wears.
- **Age resets when the project opens.** Wear is never saved.
- **Long-term persistent degradation** is deferred and noted as a future option.

## 5. Capture and editing

- **Catch**: Splicer always keeps the last 30 s of the main (or sidechain) input. Pressing
  Catch on a loop drops the last N seconds onto its tape (`Catch Length`, 1 to 30 s). It's
  "that was good, keep it" after the fact.
- **Load WAV** into a loop: the file sets the length, is trimmed to the maximum, and is
  resampled to the host rate.
- **Razor** works on the playing loop:
  1. press **Razor** to mark the cut in at the playhead, and press it again to mark the cut
     out;
  2. then choose **Reverse** (flip the segment), **Remove** (shorten the loop), or **Isolate**
     (the segment becomes the whole loop).
  3. Each cut adds a splice.
  4. **Razor edits are permanent** and are saved with the project.
- **Restore** (the escape hatch): copies the clean tape over the worn tape. It removes wear and
  keeps cuts and overdubs. There is no step-by-step undo.

## 6. Built-in effects and drift

- **The FX chain** is a fixed serial order: `underheard-chorus → underheard-delay →
  underheard-reverb`. It's the same DSP as the standalone plugins. More stages, such as a
  filter or drive, can be added to the order later.
- **Each loop has two output knobs**, independent of each other:
  - **Dry**: the loop's level straight to the output;
  - **Send**: the loop's level into the FX chain.
  So a loop can be all effects (Dry 0, Send up), all dry (Send 0), or any mix. The Send is
  pre-Dry, so turning Dry down never starves the effects.
- **One shared FX bus.** All sends sum into a single chain, with one set of knobs per stage.
  Each stage also has its own bypass, so you can skip chorus for the whole chain.
- **Tails continue** after a loop stops or its Send drops.
- **Drift** is built in, with its targets marked per parameter: Speed, Wow, Flutter, Wear Rate,
  Erase, Feedback, Dry, Send, Pan, and the FX parameters. Splicer is the first place the
  drift engine (Length, Smear, Reach, Gravity, Path, Audio Smear, Keep/Return) gets proven.
  Audio Smear matters most for speed and delay-time moves. (The same engine also runs
  **Drifter**, a separate plugin.)
- Both arrive after the core looper works (see the stages in section 10).

## 7. Performance UI

- **Four vertical loop strips.** Each has:
  - **the tape**: a loop drawing showing the playhead, splices, Razor marks, and wear (patchy,
    darker oxide);
  - **big buttons**: REC, PLAY, CATCH, RAZOR;
  - a **Speed** fader with detents at 0.5x, 1x and 2x, a reverse toggle, and a Wear toggle with
    the Age readout;
  - **DRY** and **SEND** knobs, then Pan, Erase, Feedback.
- **A master section**:
  - **Mix**: a single fader labeled **INPUT ◄► TAPE**. Fully left is 100% live input. Fully right
    is 100% tape (each loop's Dry plus the FX return). It's an equal-power crossfade, so the
    midpoint doesn't dip. It's labeled Input and Tape, not dry and wet, so it doesn't clash with
    each loop's Dry knob.
  - the FX stage settings and bypasses, Output, Catch Length, Motor, master Hiss, Sync mode, and the input
    source for each loop (Main or Sidechain).
- **Controls**: all of them are host parameters. The suggested Live workflow is to MIDI-map the
  big buttons to a pad controller. (Live 11 makes it awkward to send MIDI notes into audio
  effects; parameter mapping avoids the problem and works in every DAW.)
- **The visual feel** is reels and tape, not a DAW. It gets designed properly in the UI stage.
  Until then the stages use iPlug2 generic controls.

## 8. State and files

- Parameters and loop settings are stored in the plugin state.
- **Loop audio** is saved as the **clean tape** (recordings, overdubs and Razor cuts, no wear).
  Reopening a project brings every loop back with its cuts but no age.
- **Audio is stored as WAV files**, not inside the Live set. Four 4-minute stereo loops are far
  too heavy for plugin state.
  - Location: `~/Music/Underheard/Splicer/`. Use `~/Music`, not Desktop or Downloads. The Horsi
    work found that macOS privacy (TCC) blocks those for plugins.
  - **Files are named by content hash and never overwritten.** The plugin state stores the hash
    and the path. This keeps "Save As" and duplicated projects safe: two projects never share a
    file that one of them later changes. Unused files pile up, so a cleanup tool comes later.
  - **Saving happens when the host saves the project.** Only loops that changed since the
    last save are written, and a file whose content already exists isn't written again.
    *(Changed 2026-10-03 from "in the background after each take settles": that would leave a
    file for every intermediate version of every loop.)* A cleanup tool for files no project
    uses comes later.
  - **Reopened loops come back stopped**, at their saved settings. Press Play to hear them.

### 8.1 Memory budget

At 48 kHz stereo, one minute of float tape is about 23 MB. Wear damage is about 0.4 MB per loop.

| Per loop, at the 4-minute maximum | |
|---|---|
| Tape (float) | 92 MB |
| A second tape, only briefly, while Load WAV, Catch or reopening a project swaps it in | up to 92 MB |
| **Worst case for 4 full loops** | **about 370 MB per instance** |

Memory is allocated to the recorded length, so typical use is much smaller.

## 9. Code layout (Underheard suite repo)

```
underheard/
  libs/tape-core/      framework-free C++: tape buffer, varispeed read/write, motor,
                       wow/flutter, wear, splice
  libs/drifter-core/   framework-free C++: drift engine (Splicer's drift and Drifter)
  libs/vst3host/       framework-free C++: hosting a VST3 instrument (for Drifter)
  libs/fx/             underheard-delay, -reverb, -chorus DSP (shared by all plugins)
  plugins/Splicer/     iPlug2 plugin (VST3 + AU)
  plugins/RoomBleed/   later
  plugins/Drifter/     iPlug2 plugin (VST3): hosts a VST3 instrument and drifts it
  docs/
```

- Use iPlug2 and reuse Horsi's CMake presets, signing (`HorsiSign.cmake`), and validator
  setup.
- The core libraries have their own unit tests: render offline and check behavior with no host.

## 10. Build stages

| Stage | Scope | Done when |
|---|---|---|
| 0 | Repo scaffold, iPlug2 submodule, pass-through Splicer builds VST3 + AU, signed | It passes auval and the VST3 validator, and it loads in GarageBand |
| 1 | One loop: record, play, overdub, Erase, Feedback; free length; **varispeed record and play**; motor ramps; splice click | It records and plays at mismatched speeds with correct pitch and no clicks except the splice |
| 2 | Four loops, sidechain source, Load WAV, Catch, Sync, state and WAV storage | A saved project reopens with its loops |
| 3 | Wear model, per-loop toggle, Age, Restore; wow, flutter and hiss; playback tone by speed | A loop audibly disintegrates over a few minutes at a high Wear Rate |
| 4 | Razor (Reverse, Remove, Isolate) | Cuts land at the playhead and add splices |
| 5 | Performance UI (tape drawing, big buttons) | Playable live with a pad controller |
| 6 | underheard-delay and underheard-reverb built in; the drift engine integrated | The drift moves Splicer parameters with Smear and Audio Smear |

Development happens on the Mac in GarageBand (AU). Windows and Live 11 testing start once the PC
is available.

## 11. Decisions log

- 2026-10-03: maximum loop length is 4 min at 1x (memory permitting; see 8.1).
- 2026-10-03: Razor edits are saved with the project. Wear is not.
- 2026-10-03: each loop has a Dry knob and a Send knob into one shared FX chain (this replaced
  per-stage toggles).
- 2026-10-03: Mix is a single fader from fully dry to fully wet (labeled Input and Tape).
- 2026-10-03: FX order is chorus → delay → reverb.
- 2026-10-03: there's no restore past a cut. Once a cut is made, it is made.
- 2026-10-03: Feedback means "how much survives each pass" (see 3.3).
- 2026-10-03: Motor is a master control shared by all loops.
- 2026-10-03: loop audio is written when the project is saved, not after every take. Restore
  moved to stage 3, because it needs the clean/worn split that comes with wear.
- 2026-10-03: Razor's cut buttons are labeled FLIP, REMOVE and ISOLATE (FLIP so it doesn't clash
  with the loop's Reverse). A cut runs from the first mark to the second in the direction the
  tape was moving. Joins get a 2 ms crossfade, so the audible splice is the Splice control's,
  not a raw waveform jump. Splice positions are saved with the project.
- 2026-10-03: the UI is strips + focus: four always-visible loop strips (tape drawing, REC,
  PLAY, CATCH, Level), and below them one panel with the selected loop's full controls. This
  replaces "four vertical loop strips with everything" from §7, which was too crowded.
- 2026-10-03: the effects chain's controls live in an EFFECTS view of the focus panel. Each
  loop's Send sits in its strip next to Level. Takes always start at full speed (Motor only
  shapes Play), and Motor defaults to 0.
- 2026-10-03: wear is stored as damage per 5 ms segment and applied at playback, not written
  into the audio. There's no clean/worn tape pair. Wear off freezes the damage. Wow, Flutter,
  Wear and Wear Rate are per loop; Hiss is master.

## 12. Open questions

1. Do the names need checking? They're fine for personal use, but should be checked before
   sharing publicly.
