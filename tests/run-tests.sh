#!/bin/sh
# Runs the Underheard checks. Build and install the plugins first:
#   cmake --preset macos-ninja && cmake --build build/macos-ninja
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/tests"
mkdir -p "$OUT"

echo "== tape-core: TapeLoop tests"
c++ -std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" \
  "$ROOT/libs/tape-core/TapeLoop.cpp" "$ROOT/libs/tape-core/TapeEdit.cpp" "$ROOT/tests/TapeLoopTest.cpp" -o "$OUT/TapeLoopTest"
"$OUT/TapeLoopTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/TapeLoopTest" >/dev/null

echo "== room: convolver and impulse tests"
W="$ROOT/iPlug2/WDL"
cc -O2 -c "$W/fft.c" -o "$OUT/wdl_fft.o"
c++ -std=c++17 -O1 -w -I"$W" -c "$W/convoengine.cpp" -o "$OUT/wdl_convo.o"
c++ -std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I"$ROOT/libs/room" -I"$W" \
  "$ROOT/libs/room/Convolver.cpp" "$ROOT/tests/ConvolverTest.cpp" "$OUT/wdl_convo.o" "$OUT/wdl_fft.o" -o "$OUT/ConvolverTest" 2>&1 | grep -v 'wdl_log\|heapbuf\|convoengine.h\|queue.h\|^ *[0-9]* |\|^ *|\|warnings\? generated' || true
"$OUT/ConvolverTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/ConvolverTest" >/dev/null

echo "== room: generated room tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/room" \
  "$ROOT/libs/room/RoomModel.cpp" "$ROOT/tests/RoomModelTest.cpp" -o "$OUT/RoomModelTest"
"$OUT/RoomModelTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/RoomModelTest" >/dev/null

echo "== room: mic model tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/room" \
  "$ROOT/libs/room/RoomModel.cpp" "$ROOT/libs/room/MicModel.cpp" "$ROOT/tests/MicModelTest.cpp" -o "$OUT/MicModelTest"
"$OUT/MicModelTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/MicModelTest" >/dev/null

echo "== room: occlusion tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/room" \
  "$ROOT/libs/room/RoomModel.cpp" "$ROOT/libs/room/MicModel.cpp" "$ROOT/libs/room/Occlusion.cpp" "$ROOT/tests/OcclusionTest.cpp" -o "$OUT/OcclusionTest"
"$OUT/OcclusionTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/OcclusionTest" >/dev/null

echo "== room: room tone tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/room" \
  "$ROOT/libs/room/RoomTone.cpp" "$ROOT/tests/RoomToneTest.cpp" -o "$OUT/RoomToneTest"
"$OUT/RoomToneTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/RoomToneTest" >/dev/null

echo "== drifter-core tests"
c++ -std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I"$ROOT/libs/drifter-core" \
  "$ROOT/tests/DrifterTest.cpp" -o "$OUT/DrifterTest"
"$OUT/DrifterTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/DrifterTest" >/dev/null

echo "== drifter-core: Drifter's lanes"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/drifter-core" "$ROOT/tests/DriftLanesTest.cpp" -o "$OUT/DriftLanesTest"
"$OUT/DriftLanesTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/DriftLanesTest" >/dev/null

echo "== fx: Splicer's effects chain tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" -I"$ROOT/libs/fx" \
  "$ROOT/libs/fx/MultiChorus.cpp" "$ROOT/libs/fx/DubDelay.cpp" "$ROOT/libs/fx/AlgoReverb.cpp" "$ROOT/tests/FxTest.cpp" -o "$OUT/FxTest"
"$OUT/FxTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/FxTest" >/dev/null

echo "== fx: underheard-delay tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" -I"$ROOT/libs/fx" \
  "$ROOT/libs/fx/DubDelay.cpp" "$ROOT/tests/DubDelayTest.cpp" -o "$OUT/DubDelayTest"
"$OUT/DubDelayTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/DubDelayTest" >/dev/null

echo "== fx: underheard-chorus tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" -I"$ROOT/libs/fx" \
  "$ROOT/libs/fx/MultiChorus.cpp" "$ROOT/tests/MultiChorusTest.cpp" -o "$OUT/MultiChorusTest"
"$OUT/MultiChorusTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/MultiChorusTest" >/dev/null

echo "== fx: underheard-reverb tests (plate and hall, signal path)"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" -I"$ROOT/libs/fx" \
  "$ROOT/libs/fx/AlgoReverb.cpp" "$ROOT/tests/AlgoReverbTest.cpp" -o "$OUT/AlgoReverbTest"
"$OUT/AlgoReverbTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/AlgoReverbTest" >/dev/null
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" -I"$ROOT/libs/fx" \
  "$ROOT/libs/fx/AlgoReverb.cpp" "$ROOT/libs/fx/ReverbCore.cpp" "$ROOT/tests/ReverbCoreTest.cpp" -o "$OUT/ReverbCoreTest"
"$OUT/ReverbCoreTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/ReverbCoreTest" >/dev/null

echo "== room: reverb impulse shaping tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/room" \
  "$ROOT/libs/room/ReverbIR.cpp" "$ROOT/tests/ReverbIRTest.cpp" -o "$OUT/ReverbIRTest"
"$OUT/ReverbIRTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/ReverbIRTest" >/dev/null

echo "== fx: warmth tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" -I"$ROOT/libs/fx" \
  "$ROOT/tests/WarmthTest.cpp" -o "$OUT/WarmthTest"
"$OUT/WarmthTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/WarmthTest" >/dev/null

echo "== wavetable: decoding, oscillator, factory tables"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/wavetable" -DUNDERHEARD_ROOT="\"$ROOT\"" \
  "$ROOT/libs/wavetable/WavetableData.cpp" "$ROOT/libs/wavetable/FactoryTables.cpp" "$ROOT/tests/WavetableTest.cpp" -o "$OUT/WavetableTest"
"$OUT/WavetableTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/WavetableTest" >/dev/null

echo "== voicefx: noise layer, resonator"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/voicefx" \
  "$ROOT/libs/voicefx/Resonator.cpp" "$ROOT/tests/VoiceFxTest.cpp" -o "$OUT/VoiceFxTest"
"$OUT/VoiceFxTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/VoiceFxTest" >/dev/null

echo "== section: ensemble tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" -I"$ROOT/libs/fx" -I"$ROOT/libs/section" -I"$ROOT/libs/wavetable" -I"$ROOT/libs/voicefx" \
  "$ROOT/libs/section/SectionEngine.cpp" "$ROOT/libs/voicefx/Resonator.cpp" "$ROOT/libs/wavetable/WavetableData.cpp" "$ROOT/libs/wavetable/FactoryTables.cpp" "$ROOT/tests/SectionTest.cpp" -o "$OUT/SectionTest"
"$OUT/SectionTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/SectionTest" >/dev/null

echo "== tape-core: razor edit tests"
c++ -std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" \
  "$ROOT/libs/tape-core/TapeEdit.cpp" "$ROOT/tests/TapeEditTest.cpp" -o "$OUT/TapeEditTest"
"$OUT/TapeEditTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/TapeEditTest" >/dev/null

echo "== tape-core: audio file, saved tape and capture tests"
c++ -std=c++17 -Wall -Wextra -O2 -g -fsanitize=address,undefined -I"$ROOT/libs/tape-core" \
  "$ROOT/libs/tape-core/AudioFile.cpp" "$ROOT/libs/tape-core/TapeFiles.cpp" "$ROOT/tests/AudioFileTest.cpp" -o "$OUT/AudioFileTest"
"$OUT/AudioFileTest" | grep -v '^ok' | grep -v '^$' || true
"$OUT/AudioFileTest" >/dev/null

if [ "$(uname)" = "Darwin" ]; then
  echo "== Splicer: offline AU host test"
  c++ -std=c++17 -Wall -O1 "$ROOT/tests/SplicerAUTest.cpp" -framework AudioToolbox -framework CoreFoundation -o "$OUT/SplicerAUTest"
  "$OUT/SplicerAUTest"

  echo "== Room Bleed: offline AU host test"
  c++ -std=c++17 -O1 -I"$ROOT/libs/tape-core" "$ROOT/tests/RoomBleedAUTest.cpp" "$ROOT/libs/tape-core/AudioFile.cpp" "$ROOT/libs/tape-core/TapeFiles.cpp" \
    -framework AudioToolbox -framework CoreFoundation -o "$OUT/RoomBleedAUTest"
  "$OUT/RoomBleedAUTest"

  echo "== underheard-delay: offline AU host test"
  c++ -std=c++17 -Wall -O1 "$ROOT/tests/UnderheardDelayAUTest.cpp" -framework AudioToolbox -framework CoreFoundation -o "$OUT/UnderheardDelayAUTest"
  "$OUT/UnderheardDelayAUTest"

  echo "== underheard-chorus: offline AU host test"
  c++ -std=c++17 -Wall -O1 "$ROOT/tests/UnderheardChorusAUTest.cpp" -framework AudioToolbox -framework CoreFoundation -o "$OUT/UnderheardChorusAUTest"
  "$OUT/UnderheardChorusAUTest"

  echo "== underheard-reverb: offline AU host test"
  c++ -std=c++17 -O1 -I"$ROOT/libs/tape-core" "$ROOT/tests/UnderheardReverbAUTest.cpp" "$ROOT/libs/tape-core/AudioFile.cpp" "$ROOT/libs/tape-core/TapeFiles.cpp" \
    -framework AudioToolbox -framework CoreFoundation -o "$OUT/UnderheardReverbAUTest"
  "$OUT/UnderheardReverbAUTest"

  echo "== Section: offline AU host test (MIDI)"
  c++ -std=c++17 -Wall -O1 "$ROOT/tests/SectionAUTest.cpp" -framework AudioToolbox -framework CoreFoundation -o "$OUT/SectionAUTest"
  "$OUT/SectionAUTest"

  echo "== vst3host: hosting a VST3 instrument (Section) for Drifter"
  SDK="$ROOT/iPlug2/Dependencies/IPlug/VST3_SDK"
  HOSTOBJ="$OUT/vst3host-objs" # (only what this script compiles)
  mkdir -p "$HOSTOBJ"
  for f in $(cat "$ROOT/tests/vst3host-sources.txt"); do
    c++ -std=c++17 -O1 -w -DRELEASE=1 -I"$SDK" -c "$SDK/$f" -o "$HOSTOBJ/$(echo "$f" | tr / _).o"
  done
  for f in public.sdk/source/vst/hosting/module_mac.mm public.sdk/source/common/threadchecker_mac.mm; do
    c++ -std=c++17 -fobjc-arc -O1 -w -DRELEASE=1 -I"$SDK" -c "$SDK/$f" -o "$HOSTOBJ/$(echo "$f" | tr / _).o"
  done
  c++ -std=c++17 -O1 -Wall -Wextra -DRELEASE=1 -isystem "$SDK" -I"$ROOT/libs/vst3host" -c "$ROOT/libs/vst3host/HostedInstrument.cpp" -o "$HOSTOBJ/HostedInstrument.o"
  c++ -std=c++17 -O1 -Wall -Wextra -I"$ROOT/libs/vst3host" -I"$ROOT/libs/drifter-core" "$ROOT/tests/Vst3HostTest.cpp" "$HOSTOBJ"/*.o -framework CoreFoundation -framework Foundation -o "$OUT/Vst3HostTest"
  # (Loading two iPlug2 bundles into one process prints objc duplicate-class notes on macOS: harmless here.)
  ( set -o pipefail; "$OUT/Vst3HostTest" 2>&1 | grep -v '^objc' )

  echo "== Drifter: the plugin hosting Section (slots, drift, saving)"
  c++ -std=c++17 -O1 -Wall -Wextra -I"$ROOT/libs/vst3host" "$ROOT/tests/DrifterPluginTest.cpp" "$HOSTOBJ"/*.o -framework CoreFoundation -framework Foundation -o "$OUT/DrifterPluginTest"
  ( set -o pipefail; "$OUT/DrifterPluginTest" 2>&1 | grep -v '^objc' )

  echo "== Drifter: its window, with Section's editor inside (opens a window briefly)"
  c++ -std=c++17 -fobjc-arc -O1 -Wall -Wextra -I"$ROOT/libs/vst3host" "$ROOT/tests/DrifterEditorTest.mm" "$HOSTOBJ"/*.o -framework Cocoa -framework CoreFoundation -o "$OUT/DrifterEditorTest"
  ( set -o pipefail; "$OUT/DrifterEditorTest" 2>&1 | grep -v '^objc' )

  echo "== Section: auval (instrument)"
  auval -v aumu Sctn Undh > "$OUT/auval-Section.txt" || { cat "$OUT/auval-Section.txt"; exit 1; }
  grep "VALIDATION" "$OUT/auval-Section.txt"

  for au in "Splicer:Splc" "RoomBleed:RmBl" "UnderheardDelay:UDly" "UnderheardChorus:UCho" "UnderheardReverb:URvb"; do
    name=${au%%:*}; code=${au##*:}
    echo "== $name: auval"
    auval -v aufx "$code" Undh > "$OUT/auval-$name.txt" || { cat "$OUT/auval-$name.txt"; exit 1; }
    grep "VALIDATION" "$OUT/auval-$name.txt"
  done
fi

VALIDATOR="$ROOT/iPlug2/Dependencies/IPlug/VST3_SDK/validator"
for name in Splicer RoomBleed UnderheardDelay UnderheardChorus UnderheardReverb Section Drifter; do
  VST3="$HOME/Library/Audio/Plug-Ins/VST3/$name.vst3"
  if [ -x "$VALIDATOR" ] && [ -d "$VST3" ]; then
    echo "== $name: VST3 validator"
    "$VALIDATOR" "$VST3" | grep 'Result'
    "$VALIDATOR" "$VST3" >/dev/null
  else
    echo "== $name: VST3 validator (skipped: validator or plugin missing)"
  fi
done
