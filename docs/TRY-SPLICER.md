# Trying Splicer (GarageBand, Mac)

Splicer has all its features now, behind a plain test layout (the real performance UI is
stage 5). Everything here is also a host parameter, so in Live you can MIDI-map any button.

## Load it

1. Build and install it (see `STATUS.md`). If GarageBand was open, quit it first.
2. In GarageBand, select an audio track (or a software instrument track, to loop an
   instrument).
3. Press **B** for Smart Controls, then the ⓘ inspector, then **Plug-ins**. Splicer is an
   **audio effect**: click an empty slot in the Audio FX list, then **Audio Units → Underheard
   → Splicer**. On a software instrument track the top slot is the instrument, and the Audio FX
   slots are below it. Splicer never appears in the instrument slot or under MIDI FX.
4. If it isn't listed: `killall -9 AudioComponentRegistrar`, then restart GarageBand.

## The layout

- **Four columns, one per loop.** The top line shows what the loop is doing: its position and
  length in seconds, speed, passes worn, and Razor marks.
- **Per loop:**
  - **REC PLAY CATCH** / **CLEAR LOAD FWD-REV**
  - **Main | Sidechain**: where the loop records from.
  - **Knobs:** Speed, Erase, Feedback, Splice, Dry, Pan, Wear Rate, Wow, Flutter.
  - **WEAR ON/OFF, RESTORE**
  - **RAZOR FLIP REMOVE ISOLATE**
- **Bottom row:** Mix (input ⟷ tape), Motor, Catch Length, Hiss, Output, and Sync
  (Free / Beats / Bars).

## Things to try

1. **First loop.** Play something into the track, press **REC**, play a phrase, then press
   **REC** again. It loops. Overdub with **REC** while it plays.
2. **Tape speed.** Turn **Speed** while it plays. Pitch and time move together, with a short
   glide. Record a take at a slow speed, then turn it back up.
3. **Motor.** Press **PLAY** to stop and start: the tape slows down and spins up. Set
   **Motor** (bottom row) long for a dramatic stop.
4. **Let it wear.** Turn **Wear Rate** up on a short loop and leave it. The highs go first, then
   it saturates, ghosts appear 30 ms either side, and spots drop out and crackle. **RESTORE**
   brings it back. **WEAR OFF** freezes it where it is. **Limit** sets how far it can go: at
   the limit it **HOLD**s, or with **RECOVER** it heals back to new and wears again, over and
   over.
5. **Catch.** Play something *without* recording, then press **CATCH**: the last Catch Length
   seconds become the loop.
6. **Razor.** Drag across a loop's tape to mark a stretch (drag an edge to adjust it,
   double-click to clear), or press **MARK** at two points while it plays. Then:
   - **REMOVE** cuts that stretch out;
   - **ISOLATE** keeps only it;
   - **FLIP** reverses it.

   Turn **Splice** up to hear the joins.
7. **Several loops.** Use different lengths and leave them in Free sync so they drift against
   each other.
8. **Effects.** Turn up a loop's **Send** (in its strip), then click **EFFECTS** to shape the
   chain: chorus, then tape delay, then reverb. Turn **Level** to 0 for effects only. Tails keep
   going after a loop stops.
9. **Input.** The **INPUT** strip treats the live input like a loop. **Level** puts it with
   the loops, and **Send** runs it through the effects (and Drift). Keep it low for a subtle
   wash.
10. **Drift.** In EFFECTS, tick **Drift**: the effect settings start wandering slowly around
   where you set them. Open **DRIFT** to see them move and to shape it (Length, Curve, Smear,
   Reach, Gravity). **KEEP** makes the current drifted sound your settings; **RETURN** glides
   home.
11. **Save and reopen** the GarageBand project. The loops come back stopped; press PLAY. Their
   audio is in `~/Music/Underheard/Splicer/`.

## Worth noticing and reporting

- The default feel settings: Motor 0 (instant), Splice 30%, Wear Rate 25%, Wow and Flutter 10%,
  Hiss 10%. Too much, too little?
- Speed glide time (60 ms): too slidey, or not enough?
- How fast wear should go at 100%, and whether the order (highs, then grit, then dropouts)
  sounds right.
- Anything that clicks where it shouldn't, buttons that don't light correctly, or a loop that
  doesn't come back after reopening.

Known: the layout is generic, a 4-minute loop takes a moment to save, and loading a long file
can pause the window for about a second.
