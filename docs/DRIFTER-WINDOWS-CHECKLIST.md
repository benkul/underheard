# Drifter on Windows: what to check in Live 11

These are the things the Mac tests can't reach: Live itself, the Windows window code
(`plugins/Drifter/EditorContainer_win.cpp`), and screen scaling. The Windows build steps are
stage 5. Each item below says what should happen. Note anything that doesn't, with the
synth's name.

Use two synths if you can:
- **Section**: our own, and the one the Mac tests use.
- **A third-party synth with a resizable editor** (e.g. Vital or Surge XT).

## Loading
1. Put Drifter on a MIDI track's instrument slot. Its window shows the strip and the
   placeholder "LOAD a VST3 instrument…", 900 wide.
2. Press LOAD, then pick a `.vst3` from `C:\Program Files\Common Files\VST3`. When the synth
   loads:
   - its name appears in the strip;
   - its own editor appears under the strip;
   - Drifter's window grows to fit both.
3. Play MIDI on the track. The synth plays.
4. Load a different synth. The old editor goes, the new one appears, and the window resizes.
   The lanes are cleared.

## The synth's editor inside Drifter
5. Click, drag and type in the synth's editor: knobs, menus, text fields (e.g. preset names).
   Keyboard focus inside embedded editors is a known trouble spot on Windows.
6. Resize the synth's editor from its own size setting or corner, if it has one. Drifter's
   window should follow.
7. Close and reopen Drifter's window. The synth's editor comes back at the same size.
8. **Screen scaling:** test at 100% Windows display scale, then at 150% if you can
   (Settings → Display). Check that:
   - the editor sits exactly under the strip, with no gap and no overlap;
   - it isn't cut off;
   - the strip text is sharp.

## Slots (Live's side)
9. Open Drifter's device and use Configure, or show its automation parameters. The slots
   should carry the synth's parameter names (Output, Players, …), not "Slot 1". If Live
   still shows "Slot N", try removing and re-adding the device, then saving and reopening
   the set, and note which of those fixes it. Whether Live reads renamed parameters live is
   the open question here.
10. Draw automation on a slot. The synth's control moves while the set plays.
11. Move a synth control in its own editor while recording automation. Live records the
    slot.

## Lanes and drift
12. Press **+ LANE (LEARN)**. The button reads CANCEL LEARN and the status line says to move a
    control. Move one in the synth's editor: it becomes a lane row.
13. Use **+ FROM LIST** to pick a parameter. It adds a row.
14. Press play in Live. The lane's dot drifts and the synth's knob visibly moves. Stop Live:
    the drift holds.
15. While it plays, drag the drifting knob in the synth's editor. Where you let go becomes
    its new home (the line on the row's bar), and the drift carries on from there.
16. Drag the ends of a row's bar to narrow its range: the drift stays inside it.
    Double-click the bar to get the full range back.
17. Click a row's box to turn the lane off; it stops drifting. Click × to remove it.
18. Try KEEP, RETURN, Length in seconds and in bars, and HIDE LANES. Hiding the lanes moves
    the editor up and shrinks the window.

## Saving
19. Save the set with a synth inside, a few lanes and lanes hidden, then close and reopen it.
    Everything should come back: the synth with its sound, the lanes, the drift settings,
    and the lanes hidden.
20. Rename the synth's `.vst3` so it can't be found, then reopen the set. You should see
    "missing synth" and the placeholder naming the path. Save, restore the file's name, and
    reopen: the synth comes back with its settings.

## Things to watch for
- Crashes or hangs when closing Live with Drifter's window open.
- Clicks or dropouts when a lane moves a parameter that the synth smooths badly. That's the
  synth's own handling, but note which parameters do it.
