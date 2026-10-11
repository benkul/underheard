# Underheard

Audio plugins about tape, rooms and slow change.

- **Splicer**: four tape loops that wear out as they play. Record, catch, cut with a razor, and
  let them degrade.
- **Room Bleed**: hear a sound from somewhere else: another room, through a door, on a
  different mic, in spaces you recorded yourself.
- **Section**: an ensemble of imperfect players: every note is taken up by a small group of
  bowed strings who drift, swell, and tune to each other, seated in a room.
- **Drifter**: an instrument for Ableton Live that loads another synth inside itself and
  slowly drifts several of its parameters while the song plays (VST3 only).
- **underheard-chorus, -delay, -reverb**: the suite's own effects.

Formats are VST3 and AU, built with [iPlug2](https://github.com/iPlug2/iPlug2). Building on
Windows is below. For the Mac build, and for progress, see `docs/STATUS.md`.

## Building on Windows (for Ableton Live 11)

On Windows you build the **VST3** versions. These are the ones Live uses; AU is Mac-only.

> **The first Windows build.** Everything so far was developed and tested on a Mac. The code
> was written to build on Windows, but it hasn't been compiled there yet. If the build stops
> with errors, copy the first few into a Claude session.

### 1. Install the tools (once)

- **Visual Studio 2022** (the free Community edition is fine). In the installer, tick the
  **Desktop development with C++** workload. That brings the compiler, the Windows SDK,
  CMake and Ninja.
- **Git for Windows** (https://git-scm.com/download/win).
- **Ableton Live 11**, to try the plugins.

Then, in any terminal, let Git handle the long file paths inside the SDKs:

```powershell
git config --global core.longpaths true
```

### 2. Get the code

Use **Developer PowerShell for VS 2022** (from the Start menu) for every command below. It's
an ordinary PowerShell with Visual Studio's tools on the path.

Keep the repo at a short path, such as `C:\dev\underheard`, so Windows' path-length limit
isn't a problem:

```powershell
mkdir C:\dev
cd C:\dev
git clone --recurse-submodules https://github.com/benkul/underheard.git
cd underheard
```

`--recurse-submodules` also fetches iPlug2, the plugin framework, into `iPlug2\`. If you
cloned without it, run `git submodule update --init --recursive`.

### 3. Get the VST3 SDK (once)

Steinberg's VST3 SDK isn't in the repo. It goes inside iPlug2, at the version the suite was
built with (`v3.8.1_build_84`):

```powershell
cd C:\dev\underheard\iPlug2\Dependencies\IPlug
Remove-Item -Recurse -Force VST3_SDK
git clone https://github.com/steinbergmedia/vst3sdk.git --branch v3.8.1_build_84 --single-branch --depth=1 VST3_SDK
cd VST3_SDK
git submodule update --init pluginterfaces base public.sdk cmake
cd C:\dev\underheard
```

(The `VST3_SDK` folder only holds a placeholder README until then. Afterwards `git status`
may show `iPlug2` as modified. That's expected; don't commit it.)

### 4. Build

Live 11 looks for VST3 plugins in `C:\Program Files\Common Files\VST3`. iPlug2 would
otherwise install them to `%LOCALAPPDATA%\Programs\Common\VST3`, which Live may not scan. So
point it at the system folder when you configure.

Writing into Program Files needs administrator rights. Open **Developer PowerShell for VS
2022 as administrator** (right-click → Run as administrator), then:

```powershell
cd C:\dev\underheard
cmake --preset windows-vs2022 -DIPLUG_VST3_DEPLOY_PATH="C:/Program Files/Common Files/VST3"
cmake --build --preset windows-vs2022 --target Splicer-vst3 RoomBleed-vst3 UnderheardDelay-vst3 UnderheardChorus-vst3 UnderheardReverb-vst3 Section-vst3 Drifter-vst3
```

- **The first build takes a while.** Later builds only recompile what changed.
- **What you get:** each plugin is built into `build\windows-vs2022\out\<Name>.vst3`, then
  copied into `C:\Program Files\Common Files\VST3`. The copy only happens when a plugin
  actually relinks.
- **To build one plugin,** name only its target, e.g. `--target Section-vst3`.
- **Close Live (or at least remove the plugins from the set) before rebuilding.** Windows
  won't let the build replace a plugin that Live has loaded.
- **Without admin rights:** leave out the `-DIPLUG_VST3_DEPLOY_PATH=…` part. Then copy each
  `<Name>.vst3` folder from `build\windows-vs2022\out\` into
  `C:\Program Files\Common Files\VST3` yourself.
- **To change the install folder later,** run the `cmake --preset …` line again with the new
  path. CMake remembers the last one.

**From inside Visual Studio instead:** do the `cmake --preset …` line once from the terminal
(it sets the install folder). Then open the repo folder in Visual Studio (File → Open →
Folder…), pick the **Windows (VS2022)** preset, and build the `-vst3` targets from there. It
uses the same `build\windows-vs2022` folder.

### 5. Load them in Live

1. Preferences → **Plug-Ins**: turn on **Use VST3 Plug-In System Folders**, then **Rescan**.
2. The plugins appear in the browser under **Plug-Ins**, from **Underheard**.
   - Splicer, Room Bleed and the effects go on **audio tracks**.
   - Section and Drifter go on **MIDI tracks**.
3. **Drifter:** work through `docs/DRIFTER-WINDOWS-CHECKLIST.md`. It covers what the Mac
   couldn't test: the synth's editor inside Drifter, screen scaling, and the slot names in
   Live.

**Where files go:** Splicer's tapes, loaded impulse responses and loaded wavetables are
saved under `%USERPROFILE%\Music\Underheard\`.

### 6. Check a build with the VST3 validator (optional)

The tests in `tests/run-tests.sh` are Mac-only. On Windows, Steinberg's validator checks that
each plugin loads and behaves as a VST3. Build it once from the SDK:

```powershell
cd C:\dev\underheard\iPlug2\Dependencies\IPlug\VST3_SDK
cmake -S . -B build-validator -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF
cmake --build build-validator --config Release --target validator
```

Then run it on a plugin:

```powershell
.\build-validator\bin\Release\validator.exe "C:\Program Files\Common Files\VST3\Section.vst3"
```

It should end with `Result: 47 tests passed, 0 tests failed`, as on the Mac.

### If something goes wrong

- **CMake says it can't find the preset, or that the presets file version is too new:**
  CMake is older than 3.25. Use Developer PowerShell for VS 2022, whose CMake is new enough
  (`cmake --version`).
- **Errors about `iPlug2.cmake`, or about missing `pluginterfaces` / `public.sdk` files:**
  step 2's submodules or step 3's SDK are missing. Redo that step.
- **`Access is denied` while copying a `.vst3`:** either the terminal isn't running as
  administrator, or Live still has the plugin loaded.
- **Filename too long:** step 1's `core.longpaths`, and a short repo path (`C:\dev\…`).
- **A plugin doesn't show up in Live:** check that its `.vst3` folder is in
  `C:\Program Files\Common Files\VST3` and that the system-folder option is on, then Rescan.
- **Ninja instead of Visual Studio:** the `windows-ninja` preset works too. Run it from the
  **x64 Native Tools Command Prompt for VS 2022**: the Developer PowerShell defaults to
  32-bit tools, which would build 32-bit plugins that Live can't load.
