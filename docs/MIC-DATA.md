# Microphone data for Room Bleed

Researched 2026-10-04 from manufacturer spec sheets and reputable measurements. These figures are
what the mic models are fitted to.

**How the numbers were read**
- **"approx"** values were read off published chart images: about ±1 dB in the midrange and
  ±2 dB at the extremes.
- **Reference:** all on-axis values are relative to 0 dB at 1 kHz. Measurement distance isn't
  stated on any sheet; the convention is far field (about 1 m).
- **Gaps** are marked as estimates. No numbers are invented.

## Proximity effect (standard first-order theory)

k = 2πf/c, r = distance:

| Pattern | Bass boost |
|---|---|
| Figure-8 | G = 10·log10(1 + 1/(kr)²) |
| Cardioid (on axis) | G = 10·log10(1 + 1/(2kr)²) |
| Hypercardioid (0.25 + 0.75 cos) | G = 10·log10(1 + 0.5625/(kr)²) |

**Check against published data:** the Royer chart gives about +6 dB at 100 Hz at 1 ft; the
figure-8 formula gives 6.4 dB.

## Per microphone

### Shure SM57 (dynamic cardioid)
- **Response** (approx):

  | Hz | 50 | 100 | 200 | 500 | 1k | 2k | 4k | 6k | 8k | 10k | 12k | 15k |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|
  | dB | −9 | −3 | 0 | −1 | 0 | +1 | +3 | +6 | +4 | +3 | 0 | −8 |

  - **Presence:** about +6 dB at 5.5–6 kHz, rising from 2 kHz, with a jagged ripple region from
    6 to 10 kHz.
  - **Low end:** rolls off at 6 dB/octave below about 150 Hz.
- **Polar:** cardioid, "uniform with frequency". The rear is about −15 to −20 dB in the
  midrange; the rear lobe grows at 4–8 kHz.
- **Proximity:** +6 to +10 dB below 100 Hz at 6 mm (Shure user guide).
- **Sensitivity:** 1.9 mV/Pa. It's passive (no self-noise figure) and has an output transformer.
- **Sources:**
  - recordinghacks.com/pdf/shure/us_pro_sm57_specsheet.pdf
  - recordinghacks.com/pdf/shure/us_pro_sm57_ug.pdf

### Shure SM7B (dynamic cardioid)
- **Response, flat setting** (approx):

  | Hz | 50 | 100 | 200 | 500 | 1k | 2k | 4k | 6k | 8k | 10k | 12k | 15k | ~19.5k |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | dB | −5 | −3 | −2 | −1 | 0 | 0 | +1 | +1 | 0 | 0 | −1 | −2 | −13 |

- **Switches:**
  - **Bass roll-off:** 50 Hz −15, 100 Hz −9, 200 Hz −5.
  - **Presence:** about +3 dB over 2–8 kHz.
- **Polar:** cardioid. The rear is about −15 to −20 dB in the midrange and about −10 dB at
  2.5–6.3 kHz.
- **Proximity:** not published; use the cardioid formula.
- **Sensitivity:** 1.12 mV/Pa. The low output means about 60 dB of gain, so preamp hiss comes
  up.
- **Source:** content-files.shure.com/publications/specSheet/en/sm7b-spec-sheet.pdf

### Neumann U 87 Ai (large-diaphragm condenser; cardioid, omni, figure-8)
- **Response** (approx):

  | Pattern | 20 Hz | 50 Hz | 100 Hz | 5 kHz | 8 kHz | 10 kHz | 12 kHz | 15 kHz | 20 kHz |
  |---|---|---|---|---|---|---|---|---|---|
  | Cardioid | −6 | −2 | 0 | 0 | +2 | +3 | +2 | −2 | −9 |
  | Omni | −4 | −1 | 0 | 0 | +4 | +4.5 (9–10 kHz) | — | +2 | −6 |
  | Figure-8 | −9 | −5 | −2 | +3 (5–6 kHz) | — | 0 | — | −6 | −12 |

  Omni and figure-8 are flat between the points shown (figure-8 from 150 Hz to 2 kHz).
- **Polar:**
  - Cardioid rear is about −20 dB at 1 kHz, with lobes of −10 to −15 dB at 8–16 kHz.
  - Omni narrows: −3 dB at 90° at 8 kHz, and −6 to −10 dB at 16 kHz.
  - Figure-8 has deep nulls at 90°.
- **Proximity:** Neumann says the response is smooth at 30–40 cm in cardioid and at 15–20 cm in
  figure-8.
- **Sensitivity** (omni / cardioid / figure-8): 20 / 28 / 22 mV/Pa.
- **Self-noise:** 15 / 12 / 14 dB-A.
- **Transformer output:** it saturates gently if overdriven.
- **Source:** coutant.org/u87ai/u87.pdf

### AKG C414 XLS / XLII (multi-pattern large-diaphragm condenser)
- **XLS:** flat within ±1.5 dB from 30 Hz to 15 kHz, about −2 dB at 20 kHz.
- **XLII:** the same up to 3 kHz, then +2 to +4 dB over 5–12 kHz (the hypercardioid reaches
  about +4 at 8–10 kHz), and about −3 to −4 dB at 20 kHz.
- **Patterns:** 9, "largely frequency-independent". The cardioid rear is about −20 dB. The
  hypercardioid's rear lobe is about −10 to −11.4 dB, with nulls near 110°.
- **Self-noise / sensitivity:** 6 dB-A; 20–23 mV/Pa.
- **Output:** transformerless.
- **Source:** recordinghacks.com/pdf/akg/C414XLS-brochure.pdf, plus the AKG C414 cut sheet.

### Royer R-121 (ribbon, figure-8)
- **Response** (approx):

  | Hz | 20 | 30 | 50 | 100–1k | 2k | 4k | 8k | 12k | 15k | 18k |
  |---|---|---|---|---|---|---|---|---|---|---|
  | dB | −4 | −2 | +1 | 0 | +1 | +1 | 0 | −1 | −1.5 | −2 |

  A gentle downward tilt above 5 kHz, with no sharp cutoff.
- **Polar:** figure-8, consistent across frequency. The rear is equal in level, opposite in
  polarity, and slightly brighter when close.
- **Proximity** (Royer chart):

  | Distance | 30 Hz | 50 Hz | 100 Hz | 200 Hz | 400 Hz |
  |---|---|---|---|---|---|
  | 1 ft | +15.5 | +11 | +6 | +3 | +1 |
  | 2 ft | +10 | +6 | +3 | +1 | — |
  | 4 ft | +5 | +3 | +1 | — | — |
  | 6 ft | +3 | +2 | +0.5 | — | — |

- **Sensitivity:** about 3.5 mV/Pa. Transformer output.
- **Source:** royerlabs.com/pdf/manuals/R-121manual.pdf

### Coles 4038 (ribbon, figure-8)
- **Response** (approx):

  | Hz | 60 | 100–200 | 500–1k | 2–8k | 10k | 15k | 20k |
  |---|---|---|---|---|---|---|---|
  | dB | −1 | +1 | 0 | −0.3 | −0.5 | −2 | −4 |

  Roughly a 6 dB/octave high roll-off from about 10–12 kHz: dark.
- **Polar:** cosine figure-8 with very deep nulls.
- **Sensitivity:** 0.56 mV/Pa, very low, so preamp noise matters.
- **Other character:** ribbon resonance about 45 Hz. Low-frequency distortion rises (1% THD at
  110 dB SPL at 55 Hz).
- **Sources:** aearibbonmics.com (4038C quickstart); recordinghacks.com/microphones/Coles/4038

### Electro-Voice RE20 (dynamic cardioid, Variable-D)
- **Response** (EV specification text):
  - −3 dB at 45 Hz;
  - flat from 80 Hz to 6 kHz;
  - a broad +2.5 dB over 6–14 kHz;
  - −3 dB at 18 kHz.
- **Polar:** cardioid, uniform off axis. Rear rejection is more than 15 dB from 45 Hz to 10 kHz,
  and more than 13 dB above that.
- **Proximity:** essentially none (Variable-D).
- **Sensitivity:** 1.5 mV/Pa.
- **Source:** recordinghacks.com/pdf/ev/RE20-EDS.pdf

### Sennheiser MD 421 II (dynamic cardioid)
- **Response** (approx):

  | Hz | 40 | 50 | 70 | 100–1k | 2k | 3k | 4k | 5k | 6k | 8k | 10k | 12k | 15k | 17k | 20k |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | dB | −8 | −6 | −2.5 | 0 | +2.5 | +4.5 | +7 | +7.5 | +7 | +4.5 | +4.5 | +5 | +4 | −3 | −11 |

  A broad presence of about +7.5 dB near 4.5–5 kHz, then a steep cut above 15 kHz.
- **Polar:** cardioid; irregular at 8–16 kHz.
- **Sensitivity:** 2 mV/Pa.
- **Source:** recordinghacks.com/pdf/sennheiser/MD_421_II_GB.pdf

### DPA 4006 (small-diaphragm omni, free-field grid)
- **Response:** flat ±1 dB from 20 Hz to 15 kHz on axis. The diffuse-field response falls above
  3 kHz: about −2 at 5 kHz, −4 at 10 kHz, −8 at 20 kHz.
- **Polar:** omni, narrowing above 5 kHz.
- **Proximity:** none.
- **Self-noise / sensitivity:** 15 dB-A; 10 mV/Pa.
- **Source:** DPA 4006 manual.

### DPA 4060 (lavalier, omni)
- **Response:** flat to 5 kHz, then about +3 dB over 8–20 kHz (soft-boost grid).
- **Worn on the chest:** a −5 to −10 dB dip at 2–4 kHz plus a chest resonance near 700–800 Hz
  (DPA, qualitative).
- **Self-noise / sensitivity:** 23 dB-A; 20 mV/Pa.
- **Sources:** DPA 4060 sheet; DPA Mic University.

### Contact pickup (generic piezo disc) — largely estimated
- **Capacitance:** about 20 nF, so the input load sets a 6 dB/octave high-pass: about 8 Hz into
  1 MΩ, 80 Hz into 100 kΩ, 800 Hz into 10 kΩ (the "thin piezo" sound).
- **Resonance:** about 4.6 kHz unmounted (Murata 7BB-27-4). Mounted, expect a peak somewhere in
  2–6 kHz (estimate).
- **Hears structure, not air:** almost no room.

### Smartphone (iPhone 16 Pro, bottom mic, measurement mode; Faber Acoustical)
- **Response** (approx):

  | Hz | 20 | 30 | 50 | 100 | 1k | 2–8k | ~12k | 15k | ~20k |
  |---|---|---|---|---|---|---|---|---|---|
  | dB | −15 | −7 | −3.3 | −1.3 | 0 | ±0.5 | −3 (notch) | −1 | brick-wall cutoff |

- **Polar:** omni at 1 kHz; at 16 kHz the rear is −9 to −12 dB and lumpy from the body.
- **Voice processing** (calls, voice memos in standard mode) adds AGC and noise suppression.
  Their times aren't published; model a slow AGC (seconds) and spectral gating (estimate).
- **Sources:** faberacoustical.com (iPhone 16 Pro and 17 Pro measurements); Acoustics Today 2017
  (Faber).

### Portable cassette recorder (built-in electret plus ALC) — partly estimated
- **Correction:** the Sony TC-D5M has *no* built-in mic. It has external mic inputs and a peak
  limiter. Room Bleed's model is therefore a generic portable cassette recorder.
- **Generic ALC** (Rohm BA3308, a standard cassette preamp IC):
  - a 40–45 dB range;
  - fast attack and a very long release, set by an RC network;
  - quiet passages come up, and hiss and hum rise in pauses: the "breathing".
- **Modelling estimate:** attack 5–50 ms, release 1–10 s, peak-detected.
- **Deck noise:** S/N about 59 dB with Dolby off; wow and flutter about 0.06% WRMS
  (TC-D5M, for reference).
- **Sources:** transom.org/2005/sony-tc-d5m; the Rohm BA3308 datasheet.

## Gaps
- **No curves** were found for Barcus-Berry or Schertler piezos, the Schoeps MK2 or the Sony
  ECM-77B.
- **No published attack and release times** for cassette ALC or Apple's AGC.
- **No published proximity curves** for the SM7B, U 87, C414 or MD 421; use the formulas above.
