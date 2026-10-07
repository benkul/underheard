# Room Bleed — design spec (draft 1, proof of concept)

Part of the **Underheard** suite. Status: draft for review, 2026-10-04.

---

## 1. What it is

Room Bleed is an **audio effect** for hearing the track it's on **from somewhere else**:
- further away in a room;
- through a wall, a door, or a floor;
- on a particular microphone;
- with the room's own background sound underneath.

It works as an insert: the source becomes what the listener would hear. The dry sound is no
longer the point, the listening position is.

**Relationship to underheard-reverb:** both use one shared **room engine** (`libs/room`), which
covers what the room sounds like, including distance. underheard-reverb (later) exposes that
engine for sends and buses. Room Bleed embeds it and adds what a reverb doesn't do: occlusion,
the mic, and room tone.

## 2. Signal flow

```
input ──► ROOM (convolution) ─────────► OCCLUSION ──► MIC ──────────► + ROOM TONE ──► Mix ──► out
          IR made from a modelled room   wall, door,   tone, proximity,    synthetic or a
          + source/mic placement, or a   floor         self-noise, AGC     loaded recording
          loaded recording
```

- **The direction-dependent part of the mic** (its polar pattern) is built into the room's
  impulse response, since that's where arrival directions are known. The rest of the mic (its
  frequency response, proximity effect, noise, auto-gain) runs after occlusion.

## 3. Room engine (`libs/room`, framework-free)

### 3.1 Generated rooms (the default; no recordings needed)
- **A shoebox room:** width × depth × height in metres, plus surface materials. Absorption is
  per octave band (125 Hz–8 kHz) for walls, floor and ceiling.
- **Presets:**

  | Preset | Size | Character |
  |---|---|---|
  | Closet | 1×1.5×2.4 m | clothes, very dead |
  | Bathroom | 2×2.5×2.4 m | tile, bright, ringing |
  | Bedroom | 3.5×4×2.5 m | carpet, soft |
  | Kitchen | 3×4×2.5 m | hard |
  | Living room | 5×6×2.6 m | mixed |
  | Hallway | 1.2×8×2.5 m | long, flutter |
  | Stairwell | 2.5×3×8 m | tall, hard, long tail |
  | Garage | 6×6×3 m | concrete, metallic |
  | Church hall | 12×25×9 m | big, slow |

- **Room controls:** Room (preset), Size (scales the dimensions), Surfaces (softer ↔ harder,
  shifting the absorption).
- **Placement:** the source sits at a fixed spot. The mic is placed by:
  - **Distance:** 0.1 m up to the far wall;
  - **Aim:** 0–180°, how far the mic points away from the source.
- **IR generation** runs on the main thread whenever room or placement settings change, and
  takes about 100–300 ms. The audio thread switches to the new IR with a crossfade.
  - **Early part, by image sources:** reflections up to about 80–100 ms, each with its own delay,
    1/r spreading, band absorption and air absorption. Each is **weighted by the mic's polar
    pattern for its direction of arrival**, per band, so an omni, a cardioid and a figure-8
    hear the same room differently.
  - **Late tail:** decaying noise per octave band with the room's RT60 (Eyring), scaled by the
    pattern's diffuse-field response and crossfaded in where the image sources thin out.
- **Mono or stereo** (a setting from the start):
  - **Mono:** one mic, one IR, the same signal on both sides.
  - **Stereo:** a mic pair, with one IR generated per mic, so the image comes from the room
    itself. **Pair:** XY (coincident, ±45°), ORTF (17 cm apart, ±55°) or Spaced (about 60 cm
    apart, aimed at the source). Pairs use two of the same mic model.
  - **The source is a point in the room:** a stereo input is summed to mono first.

### 3.2 Loaded rooms (your recordings)
- **LOAD IR** takes a WAV: a clap, a balloon pop, or an already-processed impulse response.
  - It's trimmed to the onset, the tail is faded, and the level is normalised.
  - It's saved with the project the same way Splicer saves its loops (content-hashed files in
    `~/Music/Underheard/Room Bleed/`).
- **With a loaded IR,** placement can't be modelled, so Distance becomes direct-to-room balance
  plus pre-delay plus air absorption. The mic pattern can only colour the result, since a
  recording doesn't carry arrival directions.
- **Sweeps** (turning a recorded sine sweep into an IR) come after the PoC.

### 3.3 Convolution
- **Uniformly partitioned FFT convolution:** low latency (one block), IRs up to about 4 s,
  allocation-free on the audio thread.
- **Switching IRs:** a double-buffered convolver with a short crossfade.

## 4. Occlusion

- **Where** sets the listening position:
  - **Same room:** no occlusion.
  - **Next room:** heard through a wall, with a **Door** control from closed to ajar to open.
    - *Closed:* wall transmission (falling roughly 6 dB per octave above about 150 Hz), a
      little stud/panel resonance, and quieter overall.
    - *Ajar:* part of the sound comes through the gap. It's band-limited and slightly comb-
      filtered, and blended with the wall path.
    - *Open:* mostly the doorway path, reaching the listener later and more diffusely.
  - **Floor below:** mostly lows (a strong low-pass around 150–250 Hz), plus footfall-like thump
    from transients.
- **The listener's own room** stays simple in the PoC: a short, small-room colouring after the
  wall. Coupling two full rooms comes later.

## 5. Microphones

- **Emulations of specific microphones,** modelled from published data. Each model has:
  - **Frequency response:** fitted from the manufacturer's curve, as parametric filters.
  - **Polar pattern:** per band, used in IR generation (§3.1).
  - **Proximity effect:** bass rise against distance, for directional types (and minimal where
    the design avoids it).
  - **Self-noise:** from the spec sheet.
  - **Extras** where they apply: handling or rumble, transformer saturation, ribbon high-end
    softness, a phone's auto-gain and noise suppression, a cassette recorder's automatic level
    control and hiss.
- **The mic list,** from the research in Appendix A:
  - **Dynamics:** Shure SM57, Shure SM7B, Electro-Voice RE20, Sennheiser MD 421 II.
  - **Condensers:** Neumann U 87 Ai (cardioid, omni, figure-8); AKG C414 XLS and XLII (patterns);
    DPA 4006 (small-diaphragm omni); DPA 4060 lavalier (worn on the chest).
  - **Ribbons:** Royer R-121, Coles 4038.
  - **Other:** a contact pickup (piezo; partly estimated), a smartphone (iPhone 16 Pro data, with
    estimated voice-processing auto-gain), and a portable cassette recorder (generic built-in
    electret with auto-level; partly estimated).
- **Honest limits:**
  - These are filter models of published curves, which are idealised by their makers. They
    capture tone, pattern, proximity and noise, not every nonlinearity.
  - Model names are fine for personal use. A public release would describe them instead (for
    example "57-style dynamic").

## 6. Room tone

- **A bed of background sound** under everything, as if the mic were really in that room. It's
  heard through the mic (so it gets the mic's tone and auto-gain), but not through occlusion,
  because it belongs to the listener's room.
- **Built-in, synthetic, and all ours:**

  | Tone | Ingredients |
  |---|---|
  | Apartment | soft pink noise, 60 Hz hum and harmonics, a faint distant-traffic rumble with slow swells |
  | Kitchen | the apartment, plus a fridge compressor that cycles on and off (a hum with a gentle start and stop) |
  | Office | HVAC: broadband air noise with a duct resonance and a slow drift |
  | Hallway | thin, airy, distant building hum |
  | Basement | low boiler rumble and pipe ticks |

  Each is generated live from seeded noise (no files) and never repeats audibly.
- **Controls:** Tone (preset or loaded), Level, and **LOAD TONE** for your own recording (looped
  with a crossfade, and saved with the project).

## 7. Parameters (PoC)

| Group | Parameters |
|---|---|
| Room | Room (preset / Loaded IR), Size, Surfaces, LOAD IR |
| Position | Distance, Aim, Where (Same room / Next room / Floor below), Door |
| Mic | Mic (model), plus the model's pattern where it has several (U 87, C414); Output (Mono / Stereo); Pair (XY / ORTF / Spaced) |
| Room tone | Tone (preset / Loaded), Tone Level, LOAD TONE |
| Output | Mix (dry ⟷ bled), Output |

As in Splicer: the state stores a parameter count, so parameters can be appended without
breaking projects, and loaded files are saved and restored visibly.

## 8. UI (PoC)

- **One window:** a **top-down room drawing** (the room's shape, the source, and the mic with
  its aim and pattern drawn around it; drag the mic to set Distance and Aim), then rows for
  ROOM, POSITION, MIC, ROOM TONE and OUTPUT.
- **The Splicer look:** dark warm theme, readable labels (13 px or larger).

## 9. Code layout

```
libs/room/          framework-free: shoebox room model, image sources plus tail IR
                    generator, mic models, partitioned convolver, occlusion filters,
                    room-tone synth
plugins/RoomBleed/  iPlug2 plugin (AU + VST3): parameters, state, UI
```

Reused from Splicer: `libs/tape-core` (WAV I/O, resampling, content-hashed saving) and the build
and signing setup.

## 10. Build stages

| Stage | Scope | Done when |
|---|---|---|
| R0 | Plugin scaffold (pass-through), builds AU + VST3, validators pass | It loads in GarageBand |
| R1 | Partitioned convolver; LOAD IR (trim, fade, normalise, save/restore) | A loaded clap makes the input sound like that room |
| R2 | Shoebox IR generator (image sources + tail), presets, Size, Surfaces, Distance | Rooms sound distinct; distance sounds like distance |
| R3 | Mic models from the research data (response, pattern in the IR, proximity, noise, extras) | Mics are distinguishable, and patterns change the room balance |
| R4 | Occlusion: Where + Door | Next room and floor below sound right |
| R5 | Room tone: synthetic presets + LOAD TONE | A believable bed under the sound |
| R6 | The room drawing UI | Placing the mic by dragging works |

## 11. Decisions log

- 2026-10-04: Room Bleed is an audio effect. The room engine is shared with the later
  underheard-reverb.
- 2026-10-04: there are no user recordings yet, so the default rooms are generated and the
  default room tone is synthetic. Both can be replaced by loading WAVs.
- 2026-10-04: emulate specific real mics from published data, not generic types.
- 2026-10-04: Mono/Stereo is a setting from the start (XY, ORTF or Spaced pairs).
- 2026-10-04: the listener's room next door gets a simple colouring for now. Get it working,
  then discuss how many rooms and how they connect.

## Appendix A: microphone data

See **`docs/MIC-DATA.md`**: frequency-response points, polar behaviour, proximity data and
formulas, self-noise and sensitivity, other character, and sources for every mic, with
estimates marked.

**Correction from the research:** the Sony TC-D5M has no built-in mic (it has external inputs and
a peak limiter), so the cassette model is a generic portable recorder with ALC.
