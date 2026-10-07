# Underheard — status

The **Underheard** suite is a set of personal-use audio plugins built around tape, rooms and
slow change:

| Plugin | What it is | Spec | State |
|---|---|---|---|
| **Splicer** | 4-loop tape looper whose loops wear out | `docs/SPLICER-SPEC.md` | stage 4 done: all features, generic UI. **Ready to try** (`docs/TRY-SPLICER.md`) |
| **Room Bleed** | listening position, occlusion, mic type, your own recorded spaces | `docs/ROOM-BLEED-SPEC.md` | PoC complete (R0–R6) |
| **Section** | an ensemble of imperfect players: the suite's sound generator (bowed strings) | `docs/SECTION-SPEC.md` | first playable version done (stages 1–5 together) |
| **Drifter** | a VST3 instrument for Live: hosts one VST3 synth and slowly drifts several of its parameters while the song plays | `docs/DRIFTER-SPEC.md` | stages 1–4 built and tested on the Mac; waiting on the Windows checks (`docs/DRIFTER-WINDOWS-CHECKLIST.md`) |
| `underheard-chorus` / `-delay` / `-reverb` | the suite's own effects, standalone and built into Splicer | `docs/EFFECTS-SPEC.md` | **all three done** (standalone), and Splicer's chain now uses their engines |

- **Target:** VST3 in **Ableton Live 11 on Windows**.
- **Developed on:** macOS (Apple Silicon), tested as an AU in **GarageBand**.
- **Portability matters:** no Max for Live. The DSP cores are framework-free C++.

## Build, install, test (macOS)

```bash
git clone --recurse-submodules <repo>    # or: git submodule update --init
./iPlug2/Dependencies/IPlug/download-vst3-sdk.sh v3.8.1_build_84
cmake --preset macos-ninja
cmake --build build/macos-ninja   # every plugin: app, VST3, AU
./tests/run-tests.sh      # tape-core tests, offline AU test, auval, VST3 validator
```

- **Installing:** the build installs into `~/Library/Audio/Plug-Ins/{Components,VST3}` and
  ad-hoc signs each bundle (`cmake/UnderheardSign.cmake`). Without the signing, GarageBand hides
  the AU.
- **Validator:** `tests/run-tests.sh` expects the VST3 `validator` binary at
  `iPlug2/Dependencies/IPlug/VST3_SDK/validator`. The SDK and validator here were copied from
  horsi-vst3. On a fresh clone, build the validator as described in horsi-vst3's
  `docs/STATUS.md` §4.
- **Mac gotchas** are the same as for Horsi (see horsi-vst3 `docs/STATUS.md` §4):
  - The copy into Plug-Ins only happens when the binary relinks.
  - `killall -9 AudioComponentRegistrar` refreshes the AU cache.
  - Keep user files out of Desktop, Documents and Downloads (macOS privacy).

**Pinned:** `iPlug2` `d54f690` (same as horsi-vst3), VST3 SDK `v3.8.1_build_84`. Toolchain:
Xcode 27.0, CMake 4.4.3, Ninja 1.13.2.

## Splicer

**IDs:** AU `aufx Splc Undh`, version `0.1.0`.

**I/O:** main input (mono or stereo), an optional stereo sidechain, and stereo out
(`PLUG_CHANNEL_IO` `1-1 1-2 1.2-1 1.2-2 2-2 2.2-2`). Channels are laid out per bus at each
bus's full width, so main is always 0-1 and the sidechain is always 2-3.

### Stage 0 (done 2026-10-03)
- What it does now: a pass-through shell with an Output gain, input and sidechain meters, and
  the GarageBand/Logic fix for a fake sidechain.
- Verified:
  - `tests/SplicerAUTest.cpp`: bit-exact pass-through, Output gain, state round-trip;
  - auval: AU VALIDATION SUCCEEDED;
  - VST3 validator: 47/47.
- **Still to check by hand:** load it in GarageBand (Audio FX slot → Audio Units → Underheard →
  Splicer).

### Stage 1 (done 2026-10-03): one loop, the tape engine

**Engine:** `libs/tape-core/`, framework-free.
- `TapeStorage`: chunked stereo tape at 48 kHz. Memory grows with the take, up to the 4-minute
  maximum.
- `TapeLoop`: record and play heads, motor, transport state machine, overdub with
  Erase/Feedback, and the seam.
- `Filters.h`: Hermite interpolation, biquads, DC blocker.

**How the tape works**
- Positions are tape frames at 48 kHz. The tape moves `speed × 48000 / hostRate` frames per host
  sample.
- **Recording at a speed** writes each tape frame the record head crosses, interpolated from the
  input at that moment. When the tape runs slower than the input needs, the input is low-passed
  first so it can't alias.
- **Heads:** the play head runs 8 frames ahead of the record head, so it always reads tape from
  before this pass's overdub.
- **States:** Empty → Recording → Closing (10 ms post-roll crossfaded into the start) → Playing ⇄
  Overdubbing / Stopped. Clear fades out through Clearing.
- **Speed:** glides with a 60 ms time constant, and Motor ramps the tape up and down. Reverse
  glides through zero. Playback level falls with speed, and DC is blocked.
- **Splice:** a dropout of up to 5 ms plus a short noise click each time the seam passes.

**Plugin parameters:** stage 2 replaced this layout; see stage 2.

**Transport buttons are host parameters**
- The audio thread acts on changes.
- When the engine disagrees (Play on an empty loop, a take closing itself at the end of the
  tape, Clear springing back), it asks OnIdle to set the button to match.
- After a state load the buttons are re-synced without acting, so a saved REC doesn't start a
  take.

**Tape memory** grows in OnIdle: the frames in use plus 10 s of headroom.

**Tests (all passing)**
- `tests/TapeLoopTest.cpp`, with ASan/UBSan:
  - 1x round trip, seam continuity, half-speed record → octave up, 2x/0.5x playback, a 44.1 kHz
    host;
  - reverse, overdub with Erase 0 and 1, Feedback 50%, motor stop and restart, the splice dip;
  - anti-aliasing (a 15 kHz tone at 0.25x stays at -49 dB), running out of tape, short takes,
    Clear;
  - **no allocations on the audio path.**
- `tests/SplicerAUTest.cpp`: pass-through, Output, state, a record/play cycle through the
  parameters, Speed, Clear.
- auval passes. VST3 validator: 47/47.

**Still to check by hand in GarageBand**
- the generic test UI (status line, REC/PLAY/CLEAR/REVERSE, knobs);
- how it feels to play.

**Known limits (on purpose for now)**
- Tape memory isn't released on Clear; it's freed when the plugin closes.
- If the host stalls the idle timer during a very long take, the take closes itself when it
  runs out of reserved tape rather than overrunning it.
- The first 10 ms of the first pass after a take is silent while the seam crossfade is
  recorded.

### Stage 2 (done 2026-10-03): four loops, Catch, files, saving

**Parameters** (the order is now meant to stay; per-loop parameters added later go in a new
section after these):
- Master: `0 Output, 1 Mix, 2 Motor, 3 Catch Length, 4 Sync (Free/Beats/Bars)`.
- Then 4 loop blocks of 12, starting at 5: `Record, Play, Clear, Catch, Speed, Reverse, Source
  (Main/Sidechain), Erase, Feedback, Splice, Dry, Pan`.

**New in tape-core**
- `AudioFile`: WAV read (PCM 8/16/24/32, float 32/64, any channel count) and write (float, via
  a temporary file then a rename), a windowed-sinc resampler, and an FNV-1a hash.
- `TapeFiles`: the tape folder (`~/Music/Underheard/Splicer`, overridable with
  `UNDERHEARD_TAPE_DIR`), and saving and finding tape by content hash.
- `CaptureBuffer`: the always-on recording for Catch.
- `TapeLoop`:
  - `RequestLoad()` fades out and switches to a different `TapeStorage`;
  - `CloseTakeAt()` ends a take at an exact length (for Sync);
  - `Version()` changes whenever the content does.

**Two tapes per loop.** Load WAV, Catch and project reopen fill the loop's idle tape on the main
thread and hand it over through `loadTape`. The audio thread switches with a 3 ms fade. Once
`activeTape` shows the switch, the old tape is released.

**How each feature works**
- **Catch:**
  - The audio thread notes the capture position when Catch is pressed. OnIdle cuts the last
    Catch Length seconds (plus a few ms before them) from that loop's source.
  - It's resampled as if recorded at the loop's current speed, so it plays back at the pitch
    you heard.
  - The few extra ms are crossfaded into the end, so the seam is continuous.
- **Load WAV:** the LOAD button opens a file dialog. Any sample rate works (it's resampled to
  48 kHz), and the file is cut to 4 minutes.
- **Sync:** releasing REC on a first take rounds it to the nearest whole number of beats or bars
  at the host tempo and the loop's speed. It either ends there at once (the extra becomes the
  seam's post-roll) or keeps recording until it gets there.
- **Saving:**
  - `SerializeState` writes each changed loop's tape as `<hash>.wav` (48 kHz float) and stores
    the frame count, hash and path.
  - `UnserializeState` reads them back, checks the hash, looks by file name in the tape folder
    if the path is stale, and loads them **stopped**.
  - A missing file shows a message on that loop.
- **Sidechain:** each loop picks Main or Sidechain as its record source (also used for Catch).
- **Button corrections:** a pending correction is replaced or dropped every block, so a stale
  one can't relight a button.

**Tests (all passing)**
- `TapeLoopTest`: adds CloseTakeAt (shorter and longer) and RequestLoad (fade, switch, no clicks).
- `AudioFileTest`: float round trip, 16/24-bit mono/stereo PCM with an extra chunk, rejecting a
  non-WAV file, 44.1→48 kHz resampling (pitch and level), downsampling without aliasing,
  content-named saving (no rewrite of identical content), finding a moved file, a missing file,
  and the capture buffer.
- `SplicerAUTest` covers:
  - several loops at once, pan, Catch through the idle timer, and the sidechain as a source;
  - Sync snapping a 1.7 s take to 3 beats (1.5 s);
  - **saving a project and reopening it with its loop audio** (in a temp tape folder), and
    buttons springing back.
- auval passes. VST3 validator: 47/47.

**Known limits**
- Saving a 4-minute loop takes a moment on the main thread when the host saves.
- Each saved version of a loop stays in the tape folder. A cleanup tool comes later.
- Loading or catching a long file resamples on the main thread, so the UI can pause for about a
  second.
- The UI is still the generic test layout (stage 5 is the real one).

### Stage 3 (done 2026-10-03): wear and tape character

**Wear is damage, not audio.** `TapeLoop` keeps an age and an oxide-loss amount per 5 ms
segment (`mAge`, `mShed`, allocated in the constructor from the storage's maximum).
- **Aging:** when the record head leaves a segment, that's one pass, and `WearSegment()` ages
  it (unevenly) and may start or spread shedding.
- **Playback** interpolates the damage at the play head and applies, in order: print-through
  (two extra reads, ±30 ms), generation loss (a low-pass whose cutoff falls with age),
  saturation, level loss, then hiss, dropout gain, and crackle.
- **Crackle** (revised 2026-10-03 after the user found it harsh, and still audible on stopped
  loops):
  - Each crackle is a soft pop: a decaying 1.5 ms burst through a slightly resonant low-pass at
    a random 0.8–2.5 kHz. It goes through the head filter, so slow tape dulls it.
  - How often pops happen scales with tape speed, and their level follows the tape. A stopped
    loop is silent.
- **Overdubbing** with Erase scales the age of the tape it writes over.
- **Restore** clears the damage. Wear off stops aging but keeps the damage. Damage is reset by a
  new take, a load and Clear, and it's never saved.

**Other tape character**
- **Wow:** a random target every 0.4–1.2 s, smoothed twice, up to ±1.2%.
- **Flutter:** about 6–11 Hz with a drifting rate plus a little noise, up to ±0.2%. It moves the
  whole transport, so it's recorded into takes too, like a real capstan.
- **Head gap loss:** a low-pass at 18 kHz × |speed| (at least 600 Hz), applied after the hiss.
- **Fast playback:** when the tape moves more than one tape frame per host sample, it's read
  with a stretched windowed-sinc kernel, so it no longer aliases. A 15 kHz tone at 2x now comes
  out at -47 dB instead of folding to 18 kHz.

**Parameters** (appended as section B, so nothing earlier moved)
- Master: `53 Hiss`.
- Then 4 loop blocks of 5, starting at 54: `Wear, Wear Rate, Restore, Wow, Flutter`.
- **Defaults:** Wear on, Wear Rate 25%, Wow 10%, Flutter 10%, Hiss 10%.
- `LoopParam(loop, p)` maps across the sections.

**The status line** shows the passes worn.

**CPU:** four loops at 2x with wear, wow, flutter, hiss and feedback use 1.7% of one core
(Apple Silicon, offline benchmark).

**Tests (all passing)**
- `TapeLoopTest` adds:
  - wear (highs go first: 6 kHz ×0.002 vs 400 Hz ×0.34 after 48 passes at full rate);
  - Restore (back to ×1.000), wear off (no change), dropouts, and the print-through ghost;
  - hiss (off, on, rising with age), wow and flutter (pitch spread), and no aliasing at 2x;
  - an overdub with Erase renewing the tape.
- `SplicerAUTest` adds wear at full rate plus Restore through the parameters. The existing
  level checks run with wear, wow, flutter and hiss off.
- auval passes. VST3 validator: 47/47.

### Stage 4 (done 2026-10-03): Razor

**`TapeEdit`** (tape-core) describes a cut loop as up to two pieces of the old one, each read
forward or backward, wrapping. It covers:
- `MakeRemove` (what's left, starting right after the cut, so the join is the new seam);
- `MakeIsolate`, and `MakeReverse` (the flipped cut first, then the rest);
- `Map` / `MapHead` (old position → new, so playback carries on from the matching audio);
- `RenderEdit` (copies the audio and crossfades 2 ms at every join by letting the old tape run
  on past the cut).

**`TapeLoop`**
- `RequestEdit()` fades out (state `Editing`), switches tape, maps the head, and copies in the
  edited damage and splice list.
- **Splices:** up to 64 positions, each with the same dropout and click as the seam.
- `RequestLoad()` also takes splices, so they come back when a project reopens.

**Plugin**
- **Marking:** RAZOR marks the cut's start, then its end, at the play head (in tape positions, so
  marks stay on the same audio). A third press clears them. The cut runs from the first mark to
  the second in the direction the tape was moving.
- **Cutting:** FLIP, REMOVE and ISOLATE ask OnIdle to build the cut on the idle tape (`DoCut`),
  carrying the wear damage across segment by segment. The new splices are the old seam and
  earlier cuts that survive, plus the new joins. It's handed over like a load (`loadIsEdit`).
- **If the loop changed** before the cut arrived, the audio thread refuses it (`loadRefused`)
  and the idle tape is freed.
- **Saved state is version 2,** which adds splice positions per loop.

**Parameters** (section C)
- 4 loop blocks of 4, starting at 74: `Razor, Razor Reverse, Razor Remove, Razor Isolate`.
- The UI labels are RAZOR, FLIP, REMOVE, ISOLATE.

**Fixed along the way:** a reopened project restored Play as on while its loops come back
stopped, so pressing Play did nothing until the idle tick corrected the button. Transport
buttons (Record, Play, Clear, Catch, Restore, Razor and the cuts) are now reset to off when
state loads.

**Tests (all passing)**
- `TapeEditTest`: remove, isolate across the seam, reverse, position mapping, the minimum-length
  refusals, and no jumps at joins.
- `TapeLoopTest`:
  - Remove, Isolate and Reverse on a playing loop (content, length, head position, no clicks);
  - wear following the audio, and a cut's splice dipping like the seam.
- `SplicerAUTest`: mark, mark, Remove through the parameters (the loop shortens by exactly the
  marked 47 blocks), with the cut surviving a save and reopen.
- auval passes. VST3 validator: 47/47.

### Stage 5 (in progress): performance UI

**Chosen layout (2026-10-03): strips + focus.**
- **Four strips, always visible.** Each has a tape drawing, REC/PLAY/CATCH and a Level (Dry)
  knob.
  - `ui/TapeView.h`: the tape is drawn darker as it ages and thinner where oxide has shed, with
    splices, the Razor marks, the play head, and the status and message text.
- **Clicking a strip** selects its loop. The focus panel then shows that loop's controls in rows:
  - **TAPE:** Speed, FWD/REV, Erase, Feedback, Splice;
  - **WEAR:** on/off, Rate, Wow, Flutter, RESTORE;
  - **RAZOR:** MARK, FLIP, REMOVE, ISOLATE;
  - **OUT:** Pan, Main/Sidechain, LOAD, CLEAR.

  Every loop's controls exist in groups `loop0`–`loop3`; `SelectLoop()` hides all but one.
- **Master row:** Input/Tape, Motor, Catch, Hiss, Output, Sync, and the meters.
- **Look:** a dark, warm theme (`splicer_ui::theme`). The window is 1100×790 and scales from
  the corner.
- **Marking a cut on the tape:** drag across a loop's tape to mark a cut, drag either edge to
  adjust it, and double-click to clear. The marks go to the audio thread through
  `uiMarkIn/uiMarkOut/uiMarkSeq` and replace any play-head marks. They always run forward.
  MARK still marks at the play head (for MIDI). A plain click selects the loop.
- **Knobs** show short labels. Parameter names keep their "L1" prefix for host automation lists.
- **Not checked visually by Claude** (it has no screen capture), so feedback from the user
  drives the next pass.

### Stage 6, part 1 (done 2026-10-03): the built-in effects

**`libs/fx`** (framework-free, header-only; uses `Filters.h` from tape-core)
- `Chorus`: two modulated voices (sine plus a slow random wander), 12 ms base, up to 8 ms of
  swing, with a 9 kHz low-pass on the wet signal.
- `TapeDelay`: up to 2 s.
  - Changing the time glides (150 ms, so the pitch bends).
  - The read point wobbles ±0.2%.
  - Repeats are saturated, low-passed by Tone (1–14 kHz) and high-passed at 80 Hz.
- `Reverb`: 4 diffusing all-passes per side into an 8-line FDN with Householder mixing,
  damping in each line by Tone (1.5–12 kHz), and modulation on two lines.
  - Size scales the lines 0.5–2×, and Decay is RT60.
  - **The output is level-normalised** from a fitted model, so Decay and Size change the space,
    not the loudness (wet/in 0.35–0.47 across all settings).
- `FxChain`: chorus → delay → reverb, each with on/off.
- **All three start exactly at their settings** when prepared, with no initial glide.

**In Splicer**
- **Each loop gets a Send** (after pan, independent of Dry/Level) into one shared chain; the
  chain's output joins the tape side of the Mix.
- **Strips** now show Level and Send.
- **The EFFECTS button** in the focus panel title shows the chain's controls (rows CHORUS, DELAY,
  REVERB) in place of a loop's; clicking a strip goes back.
- **Parameters**, appended:
  - `90–103`: Chorus On/Rate/Depth/Mix; Delay On/Time/Feedback/Tone/Mix; Reverb
    On/Size/Decay/Tone/Mix.
  - Then a per-loop section D, `104–107`: `Send`.
  - Defaults: all stages on, Sends at 0.

**Also in this round**
- Takes start at full speed whatever the Motor time; Motor defaults to 0.
- Loop settings are applied before transport each block, so a take started with a Speed change
  in the same block starts at the new speed.

**CPU:** the chain uses 0.6% of one core.

**Tests**
- `FxTest`:
  - chorus dry at Mix 0 and moving pitch at full depth;
  - the delay's echo time (250.1 ms for 250), its feedback decay, and staying bounded at maximum
    feedback;
  - the reverb's RT60, its stability at 20 s decay, and its level staying flat across settings;
  - the chain passing through with everything off, and no allocations.
- `SplicerAUTest`: Send alone through a bypassed chain equals the loop, and the reverb tail
  rings after the loop stops.

### Fixed 2026-10-03: projects saved by an older build lost their loops

**Cause.** State formats 1–2 wrote the parameters with iPlug2's `SerializeParams`, which has no
count. Once the effects added parameters (90 → 108), reading an older project consumed the loop
data as parameters, missed the loop section, and silently restored no loops. The user's
`looper` project hit this.

**Fix**
- **State format 3:** magic, version and a parameter count first, then the parameters, then the
  loops. Newer builds read older counts, and parameters an old project didn't have keep their
  defaults.
- **Formats 1–2 are still read:** the plugin finds the magic after the parameters (on 8-byte
  steps) to learn the count.
- **Restoring is now visible:** each restored loop says "restored from the project: press
  PLAY". A loop that can't be restored says why (a missing or changed file, or unreadable
  state). These messages stay until the loop is played or recorded, instead of fading after
  8 s.

**Test:** `SplicerAUTest` turns a current project into the old count-less format with only 90
parameters, and checks that the settings, the defaults for the new parameters, and the loop all
come back.

**Rule from now on:** parameters can be appended freely; the count in the state handles it.

### Splicer's drift (done 2026-10-04)
A drift component inside Splicer. Its engine, `libs/drifter-core`, is also the one under the
Drifter plugin (see the Drifter section below).

**`libs/drifter-core/Drifter.h`** (framework-free, real-time safe, header-only)
- **What it moves:** up to 32 targets. Each holds an *offset* from its home setting, in
  normalised units.
- **A shift** moves every offset to a new random place over Length seconds, following the Curve
  (Linear, Smooth, Fast start, Slow start).
  - **Smear** staggers each target's start and duration within the shift.
  - **Reach** bounds each shift's step, and **Gravity** pulls new places toward home.
  - **Length** changes apply to the shift in progress.
- **Off** glides home within 2 s and holds. **Return** glides home over one Length, then drifts
  on. **Rebase** zeroes the offsets (used by Keep).

**In Splicer**
- **Targets:** it drifts the 11 continuous effect settings (chorus rate/depth/mix; delay
  time/feedback/tone/mix; reverb size/decay/tone/mix). The effects get setting + offset each
  block, while **the host parameters are never touched**, so turning a knob moves the drift's
  centre. Drifting Delay Time gives tape-style pitch bends, through the delay's own time glide.
- **Keep:** the audio thread snapshots the drifted values, rebases, and holds those values while
  OnIdle writes them to the host parameters (up to 1 s, until the host catches up), so nothing
  jumps.
- **UI:**
  - a **Drift** checkbox (`CheckboxControl`) in the EFFECTS view;
  - a **DRIFT** button opening the drift view. The view has MOTION
    (Length, Curve, Smear, Drift) and RANGE (Reach, Gravity, KEEP, RETURN), plus `DriftView`:
    - each target shows a track with its home mark and a dot where it has drifted;
    - a bar shows the shift's progress.
- **Parameters**, appended after the Sends: `Drift, Drift Length (1–600 s, default 20), Drift
  Curve, Drift Smear, Drift Reach, Drift Gravity (30% each), Drift Keep, Drift Return`. Off by
  default.

**Tests**
- `DrifterTest` (the engine): off means still, Reach bounds, full Gravity, Smear 0 in step versus
  Smear 1 staggered, curve shapes, a Length change applying mid-shift, Return, off gliding home,
  Rebase.
- `SplicerAUTest`: the parameters are untouched while drifting; Keep changes all 11 settings and
  springs back; switching off afterwards keeps them.

**Not yet** (from the spec):
- choosing which settings drift (including loop settings);
- Trigger modes (by bars, by chance, by note);
- Path (wandering on the way);
- Audio Smear, for settings that can't glide.

### Input strip (done 2026-10-04)
- **An INPUT strip** under the loop strips treats the live (main) input like a loop:
  - **Level** puts it on the Tape side with the loops;
  - **Send** feeds the effects chain, so the drift applies to it too.
- **Both default to 0.** Mix still crossfades the untouched dry input against the Tape side
  (loops, input Level, effects).
- **Parameters**, appended: `Input Level, Input Send`. The window is now 1100×850.
- **Test:** `SplicerAUTest` checks Level, Send through a bypassed chain, Send through the delay,
  and that Mix fully to Input is dry.

### Wear Limit and Recover (done 2026-10-04)
- **`TapeLoop`** gains `SetWearLimit(age)` and `SetRecover()`. The wear phase (Wearing, Holding,
  Recovering) is checked against the tape's mean age every 64 segments crossed.
  - **At the limit** it holds (no more aging or shedding), or with Recover heals at the wear
    rate. Shed oxide closes up in proportion to age, so at mean age ≤ 0.002 the tape is clean,
    and it starts wearing again: a breathing cycle.
  - **Raising the limit** or an Erase overdub lets a holding loop wear again.
- **Plugin:**
  - per-loop **Limit** (0–100% → mean age 0–4; the default 100% keeps the old behaviour) and
    **HOLD/RECOVER**, in the WEAR row;
  - Wow and Flutter moved to the TAPE row, and the focus grid is now 7 columns;
  - the status line adds ", holding" or ", recovering".
- **Parameters**, appended as per-loop section E: `Wear Limit, Recover`.
- **Tests:** `TapeLoopTest` (hold at the limit; wear up, heal to clean, wear again) and
  `SplicerAUTest` (a loop's level falls then returns with Recover).

### Splicer's effects: the standalone engines (2026-10-04)
The user asked for the new effects and Warmth in Splicer (unparking just this part).
- **`libs/fx/FxChain.h`** now runs `MultiChorus` → `DubDelay` → `AlgoReverb` (Plate/Hall),
  then `Warmth` on the chain's output. With every stage off it's a straight bypass (no
  Warmth). The old `Chorus.h`, `TapeDelay.h` and `Reverb.h` are gone. Rooms and Recordings
  stay in the standalone reverb: they need the room drawing and convolution, which don't fit
  Splicer's panel.
- **The old controls keep their meaning,** so saved projects and the drift's 11 targets carry
  over:
  - Delay Tone 0–100% → high cut 1 kHz .. 14 kHz;
  - Reverb Size 0–100% → x0.5 .. x2;
  - Reverb Tone → damping 1.5 kHz .. 18 kHz;
  - Reverb Mix is the chain's equal-power mix around the reverb.
  Projects will sound different anyway: these are new engines.
- **New parameters** (appended after the loop blocks; older projects get the defaults):
  Effects Warmth (50%), Chorus Mode, Chorus Voices (2), Chorus Feedback, Delay Note (Free, or
  a note synced to the host: Time and its drift apply only when Free), Delay Heads (single,
  ping-pong, multi-head with all three heads), Reverb Engine (Hall). Delay Feedback still
  tops out at 95% here.
- **Panel (EFFECTS):** each effect's row is ON, then its controls, Mix last; the bottom row
  has Drift and Warmth.
- **Tests:** `FxTest` rewritten for the chain (bypass, mixes, echo timing, reverb tail and
  level across engines/decays/sizes, Warmth, everything at its extreme bounded, no
  allocations); `SplicerAUTest` adds the synced delay (1/4 at 120 BPM against Free 250 ms),
  Warmth, and every new mode running.

### Room Bleed
- **R0:** scaffold (AU `aufx RmBl Undh`, VST3); validated in the test runner alongside Splicer.
- **R1 (2026-10-04): convolution with a loaded room recording.**
  - **`libs/room/Convolver`:** wraps WDL's zero-latency partitioned `WDL_ConvolutionEngine_Div`
    (one mono source to 1–2 IR channels).
    - **`ConvolverSwitch`:** the main thread creates and frees convolvers. The audio thread
      claims an offer and crossfades over 2048 samples, then returns the old one through a
      one-slot retired mailbox.
    - The first room fades in from silence. Long host blocks are processed in 4096-sample
      pieces.
  - **`libs/room/ImpulsePrep.h`:** trims to 1 ms before the onset (within 20 dB of the peak),
    ends 70 dB down (4 s maximum) with a 20% raised-cosine fade, scales to unit energy, and
    keeps one channel when the sides are identical.
  - **Plugin:**
    - LOAD IR; the prepared room is saved content-hashed in `~/Music/Underheard/RoomBleed/` and
      rebuilt at the host rate (in OnReset too).
    - Mix (dry ⟷ bled, equal power, default fully bled) and Output. A stereo input is summed to
      a mono source.
    - **With no room loaded,** the sound passes through.
  - **State format 1** has the parameter count from day one, plus the room's
    frames/hash/path/channels/rate, and sticky "restored" or "not restored" messages.
  - **Tests:**
    - `ConvolverTest`: exact against direct convolution with zero latency, a stereo IR with odd
      host blocks, no allocations once running, a click-free switch, and impulse preparation.
    - `RoomBleedAUTest`: pass-through with no room, a room restored from a project matching
      exactly (now plus 100 ms later), save and reopen, Mix at dry.
- **Mic research:** `docs/MIC-DATA.md` (the research is done; models get fitted in R3).
- **R2 (2026-10-04): generated rooms.**
  - **`libs/room/RoomModel`:**
    - **Shoebox rooms:** 10 materials plus 3 "furnished" stand-ins (rug and sofa floor,
      cabinets, bookshelf), with absorption per octave band (125 Hz–8 kHz).
    - **Nine presets,** tuned to realistic 1 kHz decay times:

      | Preset | Decay |
      |---|---|
      | Closet | 0.04 s |
      | Bedroom | 0.26 s |
      | Living room | 0.47 s |
      | Bathroom (with towels and a curtain) | 0.46 s |
      | Kitchen | 0.96 s |
      | Hallway | 1.3 s |
      | Garage | 3.9 s |
      | Stairwell | 4.2 s |
      | Church hall | 4.5 s |

    - **Image sources** (Allen–Berkley) up to the mixing time (√V ms, clamped to 20–80 ms) plus
      20 ms. Per band: reflection coefficients, air absorption, 1/r (capped at +6 dB), the mic
      pattern by arrival direction (a + (1−a)·cos θ), and scattering jitter.
    - **The tail:** Gaussian noise per band at the Eyring RT60 (with air), energy-matched to
      the reflections over a window and equal-power crossfaded. Bands are split with LR4 at the
      octave edges and summed.
    - **Stereo:** the tail's correlation between the mics is 0.7 for XY, 0.3 for ORTF, 0 for
      spaced.
    - **Limits:** the loudest channel is capped at +6 dB of energy; at most 6 s; generated in
      0.07 s or less.
  - **Placement:** the source sits 30% across and 25% deep; the mic sits along the line to the
    far corner. Distance runs logarithmically from 0.1 m to the room's maximum. Aim turns the
    mic away from the source. Pairs: XY (±45°), ORTF (±8.5 cm, ±55°), spaced (±30 cm).
  - **Plugin:**
    - **Parameters:** Room (9 presets plus Loaded recording), Size, Surfaces, Distance (shown in
      metres), Aim, Pattern (until R3), Output (Mono/Stereo, default Stereo), Pair (default
      ORTF).
    - **The room is rebuilt in OnIdle** 60 ms after its settings stop changing, and on
      sample-rate changes. A new instance starts in the living room.
    - **Loaded recordings and Distance:** past 1 m, the direct 2.5 ms falls as 1/r, the sound is
      pre-delayed, and air takes some highs. At 1 m the recording is used as it is.
    - **LOAD IR** selects "Loaded recording". R1 projects open on their recording.
  - **CPU:** a 6 s stereo stairwell convolves at 0.5% of one core.
  - **Tests:**
    - `RoomModelTest`: the direct arrival to the sample; the measured 1 kHz tail RT against
      Eyring for all presets; Size and Surfaces; the direct-to-room ratio falling with distance;
      cardioid rear and figure-8 side nulls; an omni hearing more room; XY versus spaced
      arrival and tail correlation; the level cap, speed, and determinism.
    - `RoomBleedAUTest`: the default generated room, Mix at dry, an R1-format project opening on
      its recording, an exact match at 1 m, save and reopen.
- **Possible later: Auto Level.** Generated rooms keep their physical level, from about +5 dB
  down to −19 dB depending on the room and the distance. The user hears that as "quiet" but
  chose to leave it (2026-10-04). If wanted: an on-by-default switch that holds the output near
  the input level while keeping the direct-to-room balance and tone.
- **R3 (2026-10-04): mic models** (`libs/room/MicModel`).
  - **15 mics:** SM57, SM7B, RE20, MD 421 II, U 87 Ai (omni, cardioid, figure-8), C414 XLS and
    XLII (4 patterns each), DPA 4006, R-121, Coles 4038, DPA 4060 on a chest, a piezo contact
    pickup, a phone with voice processing, a portable cassette recorder, and Ideal (flat, 4
    patterns).
  - **Each mic has:**
    - published response points → a 1024-tap minimum-phase FIR (cepstral), normalised at
      1 kHz and **baked into the impulse response** (offline FFT convolution);
    - a per-band pickup (a per band, so high-frequency narrowing and rear lobes) applied per
      reflection;
    - **proximity** applied per reflection at its own distance (the gradient part times
      √(1 + 1/(kr)²), capped at +20 dB; the RE20 gets 15%);
    - **noise:** self-noise, or for passive mics an EIN −128 dBu preamp at their
      sensitivity; 0 dBFS = 100 dB SPL.
  - **`MicPost`** (real time, after the room):
    - noise and transformer or tape saturation;
    - **phone:** a slow RMS auto-gain toward −20 dBFS (up to +20 dB) plus sluggish
      noise-suppression ducking;
    - **cassette:** a fast-attack, 3 s-release ALC toward −12 dBFS (up to +30 dB), tape hiss,
      and wow and flutter.
  - **Contact pickup:** its reflections and tail are cut to 6%, because it hears the surface,
    not the air.
  - **Plugin:** Mic menu (default U 87). Pattern is enabled only for multi-pattern mics and
    Ideal. Loaded recordings also go through the mic's response.
  - **Tests:**
    - `MicModelTest`: every variant's FIR within 1.5 dB of its curve (in practice ≤ 0.2 dB),
      with its energy in the first 2 ms; proximity against theory and the Royer chart; the
      phone and cassette auto-gain lifting, ducking and recovering slowly.
    - `RoomBleedAUTest`: Ideal leaves a recording exact; a contact pickup in the bathroom is
      8 dB quieter than an omni (less room).
- **R4 (2026-10-04): occlusion** (`libs/room/Occlusion`), baked into the impulse response.
  - **Next door:** the source room, collected at the wall by an omni (mono, summed), goes
    through a drywall partition curve: lows at about −6 dB, falling to −36 dB at 8 kHz, with a
    coincidence dip and a mass-air-mass bump.
    - **Door:** the wall path scales by √(1 − 0.8·door), and a doorway path joins it, 3 ms
      later. Ajar, the doorway is a 300 Hz–3 kHz leak with a 0.6 ms comb; open, it's
      60 Hz–12 kHz.
  - **Below:** a floor/ceiling curve with the boom through (−3 dB at 63 Hz, −38 dB at 1 kHz).
  - **The listener's room** is a bedroom heard through the chosen mic and pair from 1.5 m,
    normalised to unit energy so it only colours. The result is capped at 6.5 s.
  - **`MicFilter` gained `absolute`,** so partition curves keep their levels (mic curves are
    normalised at 1 kHz).
  - **Plugin:** Where (Same room / Next door / Below) and Door (enabled only next door). The
    status line says where you're hearing from.
  - **Tests:**
    - `OcclusionTest`: the 4 kHz-to-125 Hz tilt is −6 dB in the same room, −42 dB next door
      (shut), −24 dB ajar, −16 dB open, −59 dB below; the mids are 16 dB quieter next door.
    - `RoomBleedAUTest`: shut is 16 dB quieter than the same room; open brings back 8.8 dB.
  - **Noticed:** generated rooms have a strong low-end build-up below about 177 Hz, where
    early reflections add in phase, like real small rooms. It's fine for now; revisit if rooms
    sound boomy.
- **R5 (2026-10-04): room tone** (`libs/room/RoomTone`), generated live and allocation-free.
  - **Presets** (each about −10 dBFS RMS before Level):
    - **Apartment:** Kellet pink noise, 60 Hz hum with 3 harmonics drifting, and traffic rumble
      (low-passed brown noise) with swells.
    - **Kitchen:** the apartment, plus a fridge cycling on for 40–90 s and off for 30–60 s
      (50 Hz buzz and harmonics, a 1.2 s soft start, a 45 Hz clunk).
    - **Office:** HVAC band noise, a 120 Hz duct resonance, drift.
    - **Hallway:** high-passed air and hum.
    - **Basement:** a pulsing boiler rumble plus random ringing pipe ticks.
  - **Loaded tone:** `MakeToneLoop` crossfades the end into the start over 1 s. It's saved
    content-hashed (state version 2) and handed over like the rooms (`mToneOffered` /
    `mToneRetired`, with at most two kept alive).
  - **Plugin:** Room Tone (None, 5 presets, Loaded recording; default Apartment), Level
    (−80 to −20 dBFS, default −50), LOAD TONE. The tone is added to the wet signal *before*
    MicPost (so the phone's or cassette's auto-gain lifts it in pauses) and *after* occlusion
    (it belongs to the listener's room). A contact pickup hears 10% of it.
  - **Tests:**
    - `RoomToneTest`: preset levels and stereo width; the fridge cycling over 5 minutes and
      being louder when on; the loop seam; no allocations.
    - `RoomBleedAUTest`: an office tone at −20 dBFS is audible; exact-match checks run with
      tone off.
- **R6 (2026-10-04): the room drawing** (`plugins/RoomBleed/ui/RoomView.h`). It's a top-down
  plan on the right of a 1100×760 window:
  - walls coloured by hardness (the mean absorption, with Surfaces applied);
  - the source, and the dotted line the mic travels along;
  - each mic with its mid-band polar pattern filled around it, rotated to its aim;
  - the distance in metres;
  - next door, a wall with the doorway opening by Door.

  **Interaction:** drag anywhere to set Distance (projected onto the line, log-mapped like the
  knob); drag the dot in front of the mic (or the pair's centre) to set Aim. It drives both
  parameters with host gestures and reads the same `PlaceMics` as the room model.
  **Not seen by Claude** (no screen capture), so it's for the user to check.
- **The proof of concept is complete.** Next comes the user's listening and the warmth pass
  (below).
- **Movable source (2026-10-04, the user's request after trying the PoC).**
  - **New parameters,** appended: `Source X`, `Source Y` (% of the width and depth, default
    30/25), and `Mic Bearing` (degrees in plan, 0 toward x = W; default 53°, toward the far
    corner).
  - **`PlaceMics` long form:** the source is at the fractions (kept 0.3 m from the walls), the
    mic is `distance` along the bearing, and `MaxDistance` is a ray cast to 0.3 m inside the
    walls. The short form (fixed source, toward the far corner) is kept for tests and the
    listener's room.
  - **Drawing:** drag the source (2D); drag the mic or anywhere else (sets Bearing and Distance,
    log-mapped, with the reach worked out along that bearing); drag the dot to aim. It drives
    5 parameters with host gestures.
  - **Aim runs 0–360°** (0 points at the source). The drawing's handle turns the full circle
    (a signed angle, turning the same way as `PlaceMics`). Stored values keep their degrees.
  - **Test:** `RoomModelTest` checks that a source and mic beside a wall get a strong reflection
    0.5 ms behind the direct sound (−5.6 dB against −17.6 dB mid-room), and that the mic can't
    pass through a wall.

### Warmth pass, part 1 (2026-10-04): a shared Warmth stage, to tune by ear
The user's goal is **"a rich analog warmth suffusing everything."** The user asked to start
on the Mac rather than wait for Windows. Claude can't listen, so part 1 builds the means and
fixes the harsh edges it can find in code; the tuning is the user's A/B.

- **`libs/fx/Warmth.h`** (header-only), per channel. Rescaled 2026-10-04 at the user's
  request ("they sound really nice with the warm at 100%… use ~50% as the starting point and
  add more warmth"): intensity k = 2 × Amount, so **50% (the default) is the first version's
  100%** and the top half goes further.
  - head bump at 110 Hz (Q 0.8): +2.5 dB at 50%, +5 at 100%;
  - a high shelf above about 4.5 kHz, before the saturation (so the highs are driven less):
    −6 dB at 50%, −9 at 100%;
  - `SoftSaturator`: tanh with a small bias (even harmonics), unity gain for quiet signals,
    first-order antiderivative anti-aliasing (ADAA: 13 dB less of a 15 kHz tone's 3 kHz alias
    than plain tanh). On a −6 dBFS 200 Hz note: 2.6% THD at 50% (2nd −34 dB, 3rd −36), 5.4%
    at 100% (2nd −27, 3rd −30). Drives of 1.0 and up had measured 6–15%, gritty;
  - a DC blocker, then a one-pole top roll-off: 12 kHz at 50%, 9 kHz at 100%.
  - Amount 0 is an exact bypass. At 10 kHz: −9.6 dB at 50%, −13.7 dB at 100%.
  - **State:** delay, chorus and reverb went to version 2, Room Bleed to 3. A project from an
    earlier version has its Warmth halved on load, so it sounds the same.
- **A Warmth knob (default 50%) on every plugin but Splicer** (parked), appended as the last
  parameter (the user likes the default; more is there to try). It acts on the effect's sound only, never the dry: the echoes, the chorus
  voices, the reverb, and Room Bleed's bled sound. **Projects saved before this open with
  Warmth at 50%**, so they'll sound warmer than when saved; turn it to 0 for the old sound.
- **Harsh edges fixed:**
  - The delay's loop saturation was plain `tanh` (aliasing when driven, especially as feedback
    builds up). It's now the ADAA saturator. Its slight averaging also softens the top a little
    on each pass, like tape.
  - The delay's Age grit was white noise; it's now low-passed (2.5 kHz), more like tape noise.
- **Tests:** `WarmthTest` (bypass, tone shape at 50% and 100%, harmonics, quiet notes cleaner,
  no DC, anti-aliasing, small-signal unity, bounded on hot input); delay, chorus, reverb and
  Room Bleed tests check that Warmth colours the wet sound and that the exact-match tests hold
  with it at 0.
- **Not done yet (part 2, from the user's listening):**
  - the other hypotheses: generated rooms' bright tails and glassy early reflections; the
    bright presence peaks of some mic models (SM57, MD 421, U 87, C414 XLII);
  - Room Bleed's mic noise and the other noise sources (still white);
  - darker defaults, if Warmth alone isn't enough;
  - whether Warmth should also colour the dry sound (it doesn't now).

### PARKED (2026-10-04): Splicer (with its drift) and the effects
The user is happy with how they behave and has ideas for shaping them further, but wants to do
that once they're in their real Windows/Live production workflow. Don't extend them until
then; fixes are fine. Work moves to Room Bleed (`docs/ROOM-BLEED-SPEC.md`).

### Next (when unparked)
- **Splicer's drift, beyond rudimentary**, when the user reaches for it (see "Not yet" above).
- **The standalone effects** are done, and Splicer's chain uses their engines (see "Splicer's
  effects" above).
- **Stage 5:** refine the UI from the user's feedback.
- **Stage 6:** `underheard-chorus/-delay/-reverb` built in (Dry + Send per loop), and the drift.
- **Windows:** build and test in Live 11 once the PC is available. The Horsi Windows notes
  apply.

## Standalone effects (`docs/EFFECTS-SPEC.md`)
Agreed with the user 2026-10-04. Build order: delay → chorus → reverb. Each is its own plugin
(same look as Room Bleed). The DSP is new, fuller classes in `libs/fx`; Splicer's simple ones
stay until Splicer is unparked.

**"Phase", as interpreted (told to the user, open to correction):** the right side's
modulation offset from the left's, 0–360°. In the chorus, a synced LFO also locks to the song
position. The delay also has a feedback Polarity switch.

### underheard-delay (done 2026-10-04)
- **Plugin:** `plugins/UnderheardDelay`, AU `aufx UDly Undh`, shown as "underheard-delay".
  State: magic `'UDLY'`, version 1, a parameter count, then the parameters (append-only).
- **DSP:** `libs/fx/DubDelay.{h,cpp}`, plus `libs/fx/NoteValues.h` (16 note values, 1/32 to
  1/1, straight, triplet and dotted; shared with the chorus).
  - Two tape loops (2 s), Hermite reads. Time glides with a one-pole slew (Glide), so changes
    bend the pitch like tape. Synced time = note value at the host tempo, read every block.
  - **Heads:** Single; Ping-pong (the input, summed, enters left and the sides feed each
    other; Spread is the width); Multi-head (heads at ⅓, ⅔ and 1× the time).
  - **Loop:** polarity → `tanh` saturation (always some, so 110% feedback runs away but stays
    bounded: about 2× full scale at most) → low cut (12 dB/oct) → high cut (12 dB/oct, no
    resonance) → Age (an extra low-pass, 16 kHz down to ~1.3 kHz, and a little grit).
  - **Resonance is outside the loop** (fixed 2026-10-04 after the user found any resonance
    ran away): a peak at the high cut (up to +12 dB, Q 1.5–4) on the echoes. Inside the loop
    the peak multiplied the loop gain; scaling it to unity there instead thinned every repeat to
    a whistle and made Feedback barely matter. `DubDelayTest` checks that below 100% the loop
    dies away for every head mode and filter setting.
  - **Multi-head feedback is the heads' average** (heard at 1/√n): where the heads line up in
    phase they add, so 1/√n let two heads feed back 1.41× the Feedback setting.
  - **Wobble:** wow (0.55 Hz sine plus wander) and flutter (6.5 Hz). Phase offsets the right
    side's. Outside ping-pong, Spread delays the right side by up to 20 ms.
  - **Freeze:** the send closes, the loop recirculates at exactly 1× with no processing or
    wobble, so it holds unchanged (multi-head: only the full-length head recirculates).
  - **Throw:** Send = Throw closes the input except while THROW (a momentary parameter) is held.
    It's never restored held.
  - **Duck:** an input envelope (10 ms attack, 250 ms release) dips the repeats by up to 90%.
- **Tests:** `tests/DubDelayTest.cpp` (timing, sync maths, ping-pong alternation, head
  selection, bounded runaway, Freeze steady to 0.5 dB, Throw, Polarity, Glide, Duck, Phase,
  Spread, no allocations) and `tests/UnderheardDelayAUTest.cpp` (synced timing from the host
  tempo, free timing, Mix, Throw, save and reopen). Plus auval and the VST3 validator.
- **Templates (2026-10-04, the user's request):** Tape echo (the defaults), Dub echo, Dub
  throw, Slapback, Large atmosphere, Ping-pong eighths, Space echo. Each is a list of changes
  from the defaults (`Templates()` in `UnderheardDelay.cpp`). They're the host's factory presets
  (in the `'UDLY'` state format) and the panel's TEMPLATES menu, which sets every parameter with
  host gestures. The AU test loads them through `kAudioUnitProperty_PresentPreset`.
- **Startup:** the first sample after `Prepare` jumps straight to the settings, so a project
  doesn't glide in from the default time or let input through a closed Throw.
- **Age adds hiss:** a little noise in the loop (about −76 dBFS at high feedback).
- **To try:** in GarageBand, Audio FX slot → Audio Units → Underheard → underheard-delay.
  Starting points for dub: Heads Multi-head with heads 2+3, Feedback 95–105%, High cut down to
  1–2 kHz with Resonance up, then sweep High cut; or Send = Throw, hold THROW on a snare hit,
  and ride Feedback and Time (Glide 500 ms+) for the slurs. **Not heard by Claude:** levels and
  tone are for the user's ear (and the warmth pass).

### underheard-chorus (done 2026-10-04)
- **Plugin:** `plugins/UnderheardChorus`, AU `aufx UCho Undh`, shown as "underheard-chorus".
  State: magic `'UCHO'`, version 1, a parameter count, then the parameters (append-only).
- **Shared panel pieces:** `libs/ui/UnderheardUI.h` (colours, `PanelStyle()`, the TEMPLATES
  `MenuButton`, the delay's `HoldButton`), used by the delay and the chorus.
- **DSP:** `libs/fx/MultiChorus.{h,cpp}`.
  - 1–4 voices per side reading one delay line (up to 25 ms centre, 8 ms swing, never more
    than 90% of the centre so it can't reach zero). The voices share one LFO, spread evenly
    round its cycle; the right side runs Phase ahead.
  - **Shapes:** sine, triangle (in step with the sine), Wander (a smoothed random walk, a new
    target about twice a cycle).
  - **Modes:** chorus (dry + voices, equal-power Mix), vibrato (voices only; the panel greys
    out Mix), ensemble (70% the LFO, 30% a 6.1 Hz shimmer, also phase-spread).
  - **Feedback** −90..90%: the voices' average, toned, through `tanh`, so the loop never
    gains more than the setting (the same rule as the delay's resonance fix). Voices are heard
    at 1/√n (four voices are as loud as one).
  - **Tone** is a low-pass on the voices; **Age** darkens it (× 0.4 at full), adds a random
    warble (up to 0.4 ms) and gentle saturation.
  - **Sync:** the rate is one cycle per note value (now including 2 and 4 bars; the delay
    still offers up to 1 bar). While the host plays, the plugin sets the LFO's position from
    the song position every block (`SyncPhase(ppq / beats)`), so the motion lands the same way
    on every pass. Age's warble is random and isn't locked.
- **Templates:** Gentle chorus (the defaults), Wide stereo, Vibrato, Ensemble strings, Tape
  warble, Jet flanger (synced to 2 bars, 70% feedback), Dimension.
- **Tests:** `tests/MultiChorusTest.cpp` (centre time, swing range and floor, phase, voice
  spread, triangle vs sine, song lock, ensemble shimmer, wander bounds, flanger repeats,
  feedback dying away in every mode, voice count and delay, Mix, loudness, no allocations)
  and `tests/UnderheardChorusAUTest.cpp` (dry/wet, vibrato, two instances joining the song
  0.37 s apart moving together while playing but not while stopped, templates, save and
  reopen). Plus auval and the VST3 validator.
- **To try:** Audio FX → Audio Units → Underheard → underheard-chorus. **Not heard by
  Claude.**

### underheard-reverb (done 2026-10-04)
- **Plugin:** `plugins/UnderheardReverb`, AU `aufx URvb Undh`, shown as "underheard-reverb".
  State: magic `'URVB'`, version 1, a parameter count, the parameters, then the loaded
  recording (frames, hash, path, channels, rate; frames 0 for none), like Room Bleed's.
  Recordings are kept in `~/Music/Underheard/UnderheardReverb/`. A template (no recording)
  leaves a loaded recording in place.
- **The room drawing** moved to `libs/ui/RoomView.h`, shared with Room Bleed.
- **Engines** (`ReverbCore` handles the path around them):
  - **Rooms:** Room Bleed's generator, with `RoomSpec::decayScale` = Decay and
    `RoomSpec::direct = false` (a send reverb shouldn't repeat the dry sound a few ms late).
    The mic's response goes in; Room Bleed's real-time mic extras (noise, phone, cassette)
    don't. The default mic is the ideal one, spaced omnis. Rooms up to 20 s; one that's still
    ringing at 20 s fades out over its last 30%. The worst case (a church hall at double size,
    Decay x4) builds in about 0.25 s on the main thread.
  - **Recordings:** LOAD IR (up to 10 s, `PrepareImpulse`), then `RemoveDirect`,
    `StretchDecay` (an exponential envelope from 80 ms after the onset; a stretched recording's
    last third fades, since it can only be lengthened so far), `Balance`, `NormaliseEnergy`.
    With nothing loaded the engine is silent (a silent convolver replaces the last one).
  - **Plate** (Dattorro) and **Hall** (8-line FDN): `libs/fx/AlgoReverb`. Decay x1 is
    2.0 s (plate) and 3.0 s (hall). Size also scales them (x0.5..x2). Their level is
    normalised from a fit to measurements (within about ±1 dB at normal settings, ±3 dB at the
    extremes) and their decay lands within about 25% of the setting. The plate's tank
    allpasses are capped so they never ring longer than half the decay.
  - Every engine ends at about unit energy, so switching engines keeps the level. Switching
    fades the wet out and in (30 ms).
- **Early/Late:** for convolution, the impulse is split at the mixing time with a 20 ms
  crossfade; for Plate and Hall, early reflections (12 taps) against the tank.
- **Freeze:** Plate and Hall hold their own tanks exactly (lossless, modulation fades out so
  the reads sit on whole samples). Rooms and Recordings: the convolver's input closes, the wet
  sound feeds a hall tank for 0.3 s and is held there (level within about 1 dB of before);
  on release the tank fades over about 2 s and then stops running.
- **After the engine:** low cut, high cut (each off at the end of its range), Age (a softer
  top, `tanh` saturation, darker Plate/Hall damping; off at 0), width (M/S, 0–150%), duck,
  equal-power Mix. Pre-delay up to 500 ms or synced (1/32 to 1/4).
- **Templates:** Hall (the defaults), Large hall, Bright plate, Dark plate, Living room,
  Tiled bathroom, Church hall, Frozen pad.
- **Tests:** `AlgoReverbTest` (level, decay, decorrelation, Early/Late, Freeze exact and
  sealed, 60 s decay at full modulation bounded), `ReverbCoreTest` (pre-delay to the sample,
  which engines convolve, switching fades, Freeze for both kinds of engine, Mix, width, high
  cut, duck, Age), `ReverbIRTest` (onset, direct removal, decay measured and stretched,
  Early/Late, end fade, normalising), `RoomModelTest` (decay scale, no direct sound), and
  `UnderheardReverbAUTest` (every engine, Decay in Hall and Rooms, Freeze in Hall and Rooms,
  a recording restored from a project convolving exactly, save and reopen, templates).
  Plus auval and the VST3 validator.
- **To try:** Audio FX → Audio Units → Underheard → underheard-reverb. **Not heard by Claude.**

## Section (2026-10-04)
The user parked the effects work and asked for a sound generator. They have plenty of
conventional synths and found the chain weak on the musical side. From a brainstorm they chose
"Section" (agreed details in `docs/SECTION-SPEC.md`: strings first, pure intervals by default,
seated in Room Bleed's rooms, 6 players x 8 notes).

- **Plugin:** `plugins/Section` (from iPlug2's IPlugInstrument), AU `aumu Sctn Undh`, VST3
  "Instrument|Synth". State: magic `'SCTN'`, version 1, a parameter count, the parameters.
  In GarageBand: a Software Instrument track → the instrument slot → AU Instruments →
  Underheard → Section. A keyboard along the bottom plays it without a controller.
- **`libs/section/JustTuning.h`:** 5-limit ratios over a root chosen as the held note giving
  the simplest ratios (lowest summed Tenney height); the root stays at equal temperament.
  C major: E −13.7, G +2.0 cents; A minor settles over A (C +15.6).
- **`libs/section/SectionEngine`:** 8 notes (a 9th takes a released note first, else the
  oldest) x 1–6 players.
  - Per player at note-on: entry and release delays (Looseness; the leader, player 0, comes in
    most promptly), attack and release multipliers, a swell curve, vibrato rate (half its
    own, half the leader's), depth, onset, brightness, bow noise, level, a starting smear.
  - Per sample: an exponential envelope; tuning glides to the pure target (Settle: time
    constant 4 s at 0+ down to 0.25 s at 100%; at 0 they stay in equal temperament); the smear
    decays (350 ms); vibrato fades in over 0.6 s after its onset; drift is a slow wander (up to
    12 cents); pitch bend ±2.
  - **Tone:** a PolyBLEP sawtooth (Helmholtz motion) through two one-poles whose corner rises
    with bow pressure (velocity, Bow, the mod wheel), plus band-passed bow noise bursting just
    after each slip.
  - **Desks by register:** basses below G2, cellos below G3, violas below E4, violins above.
    Each desk is summed and goes through a body: six peaking resonances from a violin body (air
    mode 280 Hz, wood modes 460/550 Hz, 1.1 kHz, the bridge hill at 2.5 kHz, a dip at
    4.5 kHz), scaled down for larger instruments.
- **The room:** one impulse per desk, generated by Room Bleed's engine with each desk at its
  own seat across the width near one end (violins 22%, violas 40%, cellos 60%, basses 80%),
  all heard by one mic pair placed out in the room facing them (Distance). The default is a
  church hall heard by a Coles 4038 XY pair (a Blumlein pair). The four impulses share one
  gain, about unit energy, so the desks keep the room's balance. **Close / Room** crossfades
  (equal power) from the desks panned to their seats (Width) to the room through the mics.
  The drawing shows the room, the four desks and the mics (a picture only).
- **MIDI:** notes, sustain (CC64), the mod wheel (CC1; it scales Bow by 0.5–1.5),
  aftertouch (more vibrato), pitch bend, all-notes-off (CC123/120). Events are applied at
  their sample offsets.
- **Warmth** (50%) on the output. **Templates:** Chamber strings (the defaults), Quartet,
  Tired orchestra, Lone player, Pure pad, Close and dry.
- **CPU:** 8 notes x 6 players through the room is about 10% of one core (offline AU test).
- **Tests:** `SectionTest` (440 Hz, bend, settling C major and A minor, slow Settle, Settle 0,
  smear resolving, looseness, vibrato onset, desks by register, stealing, sustain, bounded at
  full, no allocations) and `SectionAUTest` over MIDI (pitch, release, sustain pedal, a held
  chord's E moving from 329.65 to 327.05 Hz, the room's tail, templates, save and reopen,
  CPU). Plus auval (`aumu`) and the VST3 validator.
- **User's first listen (2026-10-07,** the Haydn Op. 1 No. 1 Adagio from flat-velocity MIDI in
  `~/Music/Underheard/Test MIDI`): "remarkably expressive despite having no velocity
  information". The sound is "like a string section, but also distinctly not … an interesting
  liminal space", and a little more noise improves it. So:
  - **Bow Noise** (0–100% → x0..x3 the first version's bow noise; default 55% = x1.65,
    +4.3 dB, about 38 dB under the tone on a held C3), and
  - **Body** (0–100% → x0..x2 the resonances' dB; default 70% = x1.4). The body's gain keeps
    its white-noise power where it is at x1, so Body changes character, not level (within
    about 1 dB on a string; 1.7 dB quieter with no body at all).
  - Both are appended parameters, on a new TONE row (with Warmth and Output; Bow moved to
    PLAYING, Smear to ENSEMBLE). Projects from before open with the new defaults.
- **The VOICE page (2026-10-07,** the user's idea: switch the generator under the same players
  to reach further into that liminal space). The panel has two pages (PLAYERS, VOICE; UI-only,
  control groups "players" and "voice"); the room drawing, output and keyboard show on both.
  - **Wave** (appended, 0..3, default 2): sine → triangle → saw (strings) → square,
    crossfading between neighbours. `Wave()` in `SectionEngine.cpp`: every shape's fundamental
    is in phase with the sine (a falling saw, a sine-phase triangle), so blends don't cancel;
    each shape is scaled to RMS 0.577; the saw and pulse are PolyBLEP, the triangle naive (its
    harmonics fall fast enough). The saw is now falling: a polarity flip, inaudible.
  - **Pulse Width** (5–50%, default 50) for the square, loudness kept the same at any width.
  - **Bowing** (default 100%): how much of the bow-pressure corner rounding applies (a
    crossfade from the raw wave). Bow, Bow Noise and Body moved to VOICE and apply to any wave.
    Warmth moved to PLAYING. A drawing shows one cycle of the wave.
  - **Templates** 7–9: Bowed sines, Glass section, Organ loft.
  - **Tests:** each shape's harmonics (sine none; triangle odd at 1/9; saw 1/k; square odd at
    1/3; a 25% pulse with no 4th), equal loudness within 0.11 dB, blends keep their
    fundamental, Bowing 0 is 15 dB brighter at the 24th harmonic than bowed lightly, a plain
    sine section is pure (also through the plugin: 440 Hz, no 2nd or 3rd).
  - **Not seen by Claude:** the tabs and layout (no screen capture). Check that the pages
    switch and nothing overlaps.
- Voices (sung vowels) and phrase-aware players are the candidates for next.

## Drifter (`docs/DRIFTER-SPEC.md`, 2026-10-07)
A VST3 instrument for Ableton Live 11 (Windows), on MIDI tracks: it loads one other VST3
instrument, plays it from the track's MIDI, and slowly drifts up to 8 of its parameters while
the song plays.
- **Built and tested on the Mac** (stages 1–4):
  - `libs/vst3host`: hosting a VST3 instrument, including its editor;
  - `libs/drifter-core/DriftLanes.h`: lanes, learn and grabbing, on the same engine as Splicer's drift;
  - `plugins/Drifter`: 128 pass-through slots, latency, saving, the strip with lane rows, and
    the synth's editor embedded under the strip.
- **Tests:** `DriftLanesTest`, `Vst3HostTest`, `DrifterPluginTest` and `DrifterEditorTest`
  (all with Section as the hosted synth), plus the VST3 validator.
- **Next:**
  - the user checks it in Live on Windows (`docs/DRIFTER-WINDOWS-CHECKLIST.md`);
  - stage 5: the Windows build notes.

## Invariants
- **Parameters:** order is saved state. Append only, once anyone has saved projects.
  Parameters are named `kParam*`, because iPlug2 already defines `kOutput` and similar names.
- **Audio thread:** no locks, allocations, frees or file I/O.
- **Before committing:** run `./tests/run-tests.sh`.
