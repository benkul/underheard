# Drifter: a wrapper that slowly drifts a synth's parameters (spec)

**Status:** design agreed with the user 2026-10-07. Built and tested on the Mac:
- stage 1, `libs/vst3host`;
- stage 2, `libs/drifter-core/DriftLanes.h`;
- stage 3, `plugins/Drifter`: hosting, 128 slots, latency and saving;
- stage 4, the UI:
  - the strip, with lane rows that can be hidden;
  - the synth's editor embedded under the strip, with Drifter's window sized around it.

Next: the user checks it in Live on Windows (`docs/DRIFTER-WINDOWS-CHECKLIST.md`); stage 5 is
the Windows build notes.

## What it is
A VST3 instrument for **Ableton Live (11, Windows)** that loads **one other VST3 instrument**
inside itself and slowly drifts several of that instrument's parameters, the way Splicer's
drift moves its effects (the same engine). Drifter's own UI is the smallest possible strip around the
hosted synth's editor.

**Agreed scope (2026-10-07):**
- Ableton only; VST3 only (hosting and being hosted).
- MIDI tracks only: Drifter goes in the instrument slot, the track's MIDI plays the hosted
  synth, and Drifter drifts the hosted synth's **plugin parameters**.
- One hosted plugin at a time; several of its parameters drifting.
- **Pass-through slots**, so the hosted synth's parameters stay automatable from Live.
- **Grabbing a drifting control makes that its new home** (the drift carries on from there).
- Not tested in Live on the Mac (no Live there for now): the core is tested on the Mac without
  Live (see Testing); the UI and Live behaviour are checked by the user on Windows.

## How it works
- **Loading:** LOAD opens a file dialog for a `.vst3` (starting in `C:\Program Files\Common
  Files\VST3`). If the bundle holds several instruments, a menu picks one. Only instruments
  (VST3 "Instrument" category) are offered.
- **Audio and MIDI:** the track's MIDI goes to the hosted synth; its stereo output is
  Drifter's output. Drifter reports the hosted synth's latency to Live. (Multi-output synths
  and sidechains: later.)
- **The UI:** a slim strip across the top (LOAD, the synth's name, the lanes, the drift
  controls, and a toggle to hide the lanes), and the hosted synth's own editor directly under
  it. Drifter's window is the hosted editor's size plus the strip, and follows it if the
  hosted editor resizes.
- **Lanes:** up to 8, one per drifting parameter, shown as rows (two columns of four). Each row
  shows on/off, the parameter's name, a bar for its range (drag either end; double-click for
  the whole range) with home (a line) and where it is now (a dot), its value, and remove (x).
  - **Adding:** "+ lane", then move any control in the hosted synth's own editor: Drifter catches
    that edit and makes it a lane (learn). Or pick from a menu of the synth's parameter names.
  - **Each lane:** its parameter, its **range** (the bounds it can drift within), on/off. The
    drift moves a lane from its home toward the bottom of its range or the top, each side
    scaled to its own room, so a lane whose home is near one end doesn't sit pinned against it
    (found in stage 2). A home moved outside the range widens the range.
- **Timing:** **Length** in seconds, or synced to the song in **bars** (a switch). Drift moves
  **only while Live is playing**: stopped, the lanes hold where they are.
- **Drift:** the drift engine (`libs/drifter-core`): **Length**, **Curve**, **Smear**
  (staggers the lanes), **Reach**, **Gravity**, **KEEP**, **RETURN**, and **Drift** on/off. Drifted
  values go to the hosted synth's audio processing every block (sample-accurate parameter
  changes) and to its editor (so its knobs visibly move), a few times a second.
- **Grabbing:** when you move a drifting parameter in the hosted editor, or Live's automation
  moves it through its slot, that value becomes the lane's new home; the drift carries on
  from there.
- **Pass-through slots:** Drifter exposes **128** slots that map to
  the hosted synth's parameters in order, so Live can automate them, map them to macros, and
  show them in Configure. Each slot is renamed to its parameter's name when a synth loads.
- **Saving:** with the Live set: which plugin (its class ID and path), its complete state (the
  synth's own preset data, component and controller), the lanes, and the drift settings.
  If the plugin is missing when the set opens, Drifter keeps its state and says what's missing.

## Under the hood
- **Hosting** with the VST3 SDK's own hosting code (module loading, the component and edit
  controller, `ProcessData` with event and parameter-change queues), in a new framework-free
  library `libs/vst3host`.
- **Threads:** the hosted processor runs on Live's audio thread inside Drifter's; the hosted
  controller and editor on the UI thread. Parameter changes cross between them through
  lock-free queues; edits from the hosted editor (`beginEdit`/`performEdit`/`endEdit`) drive
  learn and grabbing.
- **The editor:** the hosted `IPlugView` is attached to Drifter's window (a child window on
  Windows); `IPlugFrame::resizeView` resizes Drifter's window around it.
- **Drifter itself** is an iPlug2 VST3 instrument (like Section).

## Testing
- On the Mac, without Live: unit tests of the drift lanes, and a test that loads a real VST3
  instrument (Section) through `libs/vst3host`, plays MIDI through it, drifts parameters,
  checks they reach its processing, saves and restores its state, and checks grabbing.
- The VST3 validator on Drifter itself.
- The editor embedding and the Live behaviour (slots, automation, saving the set) can only be
  checked on Windows: a checklist for the user.

## Decided (2026-10-07)
1. **128 pass-through slots** (the hosted synth's first 128 parameters, in order). Whether
   Live shows the renamed slots reliably is on the Windows checklist.
2. **Tempo:** Length in seconds or in bars (optional sync); drift only while Live is playing.
3. **The name is Drifter**, for this plugin. The drift inside Splicer is a component of Splicer
   (the same engine), not Drifter.
4. **Windows:** the user will have the PC and Visual Studio in a few days. Stages 1-3 can be
   built and tested on the Mac before then; stage 4 (the UI) needs the PC to check.

## Build stages (after the go-ahead)
1. `libs/vst3host`: load a VST3 instrument, run it (MIDI in, audio out), list and set its
   parameters, save and restore its state; tests on the Mac with Section.
2. Lanes and drift: the drift engine driving hosted parameters (sample-accurate), learn and
   grabbing from edits; tests.
3. The Drifter plugin: hosting inside an iPlug2 VST3 instrument, pass-through slots,
   latency, saving; the VST3 validator.
4. The UI: the strip, the embedded editor, resizing, learn; a Windows checklist.
5. Windows build notes and docs.

I'll report back after each stage. No git commits at any point.
