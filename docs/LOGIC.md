# Logic Pro: build and install

Logic Pro loads Audio Units (AUv2), and the macOS build already makes one per plugin. These
steps haven't been tried in Logic yet. GarageBand uses the same AU host, so they should carry
over.

## Requirements

- macOS 11 or newer (the deployment target). Apple Silicon is the tested setup.
- Xcode or its command line tools. Pinned toolchain: Xcode 27.0, CMake 4.4.3, Ninja 1.13.2.
- Logic Pro running natively, not under Rosetta (see **Architecture** below).
- Submodules checked out: `git clone --recurse-submodules`, or `git submodule update --init`.

## Build

From the repo root:

```bash
./iPlug2/Dependencies/IPlug/download-vst3-sdk.sh v3.8.1_build_84   # once per clone
cmake --preset macos-ninja
cmake --build build/macos-ninja   # every plugin: app, VST3, AU
```

To build only the AUs:

```bash
cmake --build build/macos-ninja --target Splicer-au RoomBleed-au Drifter-au Section-au \
  UnderheardDelay-au UnderheardChorus-au UnderheardReverb-au
```

**Architecture:** the build makes arm64 binaries only. For an Intel Mac, or for Logic under
Rosetta, configure a universal build:

```bash
cmake --preset macos-ninja -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

## Install

The build installs for you. Each AU is copied to `~/Library/Audio/Plug-Ins/Components` and
ad-hoc signed (`cmake/UnderheardSign.cmake`). Without the signature, Apple's hosts hide it.

1. Refresh the AU cache: `killall -9 AudioComponentRegistrar`.
2. Optionally run the check Logic runs: `auval -v aufx Splc Undh` (Splicer).
   `auval -a | grep Undh` lists every Underheard AU with its codes.
3. Quit and reopen Logic. It scans new AUs at startup.

The copy into Plug-Ins only happens when the binary relinks. If a change doesn't show up,
touch a source file and build again.

## Where they show up

| Plugin | Kind | Where in Logic |
| --- | --- | --- |
| Splicer, Room Bleed, underheard-delay, underheard-chorus, underheard-reverb | Effect | Audio FX slot → Audio Units → Underheard |
| Drifter, Section | Instrument | Software instrument track → Instrument slot → AU Instruments → Underheard |

Splicer's sidechain is set from the **Side Chain** menu at the top of its plugin window.
GarageBand has no such menu.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| Plugin missing or marked failed | Logic Pro → Settings → Plug-in Manager, select it, **Reset & Rescan Selection**. The result names the failure. |
| An old version still loads | `killall -9 AudioComponentRegistrar`, then restart Logic. |
| Blocked on another Mac | Ad-hoc signing only suits the Mac that built it. Run `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/<Plugin>.component`, or sign with a Developer ID. |
| Missing under Rosetta or on Intel | Rebuild universal (see **Architecture**). |
| Saved tape files don't load | Keep them out of Desktop, Documents and Downloads, which macOS privacy blocks. |

Logic can't load the VST3 builds. Those are for Ableton.
