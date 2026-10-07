# Built-in horse wavetables (from horsi-vst3)

Copied from horsi-vst3's `assets/wavetables`, where they're made (`tools/make_wavetable.cpp`,
`tools/build-horse-tables.sh`). Section offers them after its own generated tables.


The Horsi tables are made from recordings of real horses
(`tools/make_wavetable.cpp`, regenerate with `tools/build-horse-tables.sh`).
Pitched tables are single cycles of the voice (pitch-tracked, resampled to
2048-sample cycles, phase-aligned, levelled); breath tables are looped slices
of unpitched breath and snort sounds. Except for `Horsi-Breath` (public
domain) and `Horsi-Human` (CC0, and not a horse), the recordings are licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) and these tables are
adaptations of them.

| Table | Source recording | Excerpt | Authors |
|---|---|---|---|
| `Horsi-Whinny` | [Supplementary Audio S2](https://commons.wikimedia.org/wiki/File:Segregation-of-information-about-emotional-arousal-and-valence-in-horse-whinnies-srep09989-s3.oga) of Briefer EF et al. (2015) "Segregation of information about emotional arousal and valence in horse whinnies", *Scientific Reports* 5:9989, doi:10.1038/srep09989 | 0.22-1.64 s | Briefer E, Maigrot A, Mandel R, Freymond S, Bachmann I, Hillmann E |
| `Horsi-Stallion` | [Stimulus S3](https://commons.wikimedia.org/wiki/File:Mares-Prefer-the-Voices-of-Highly-Fertile-Stallions-pone.0118468.s003.oga) (low-pitched) of Lemasson A et al. (2015) "Mares Prefer the Voices of Highly Fertile Stallions", *PLOS ONE* 10(2): e0118468, doi:10.1371/journal.pone.0118468 | 1.0-2.2 s | Lemasson A, Remeuf K, Trabalon M, Cuir F, Hausberger M |
| `Horsi-Call` | [Stimulus S2](https://commons.wikimedia.org/wiki/File:Mares-Prefer-the-Voices-of-Highly-Fertile-Stallions-pone.0118468.s002.oga) (high-pitched) of the same study | 1.04-1.96 s | Lemasson A, Remeuf K, Trabalon M, Cuir F, Hausberger M |
| `Horsi-Breath` | [Wiehern.ogg](https://commons.wikimedia.org/wiki/File:Wiehern.ogg), "a sounding recording of a neighing horse" (public domain) | 0.64-0.84 s, breath | Hü. |
| `Horsi-Huff` | [Supplementary Audio S1](https://commons.wikimedia.org/wiki/File:Segregation-of-information-about-emotional-arousal-and-valence-in-horse-whinnies-srep09989-s2.oga) of Briefer EF et al. (2015), as above | 1.32-1.94 s, breath after a whinny | Briefer E, Maigrot A, Mandel R, Freymond S, Bachmann I, Hillmann E |
| `Horsi-Rasp` | Supplementary Audio S2 of Briefer EF et al. (2015), as for `Horsi-Whinny` | 14.76-15.24 s, breath, 12 ms slices | Briefer E et al. |
| `Horsi-Whinny-to-Breath` | `Horsi-Whinny` (32 frames) followed by `Horsi-Huff` (32 frames) | | Briefer E et al. |
| `Horsi-Human` | [LL-Q1860 (eng)-Wodencafe-whinny.wav](https://commons.wikimedia.org/wiki/File:LL-Q1860_(eng)-Wodencafe-whinny.wav), a person saying the word "whinny", from the [Lingua Libre](https://lingualibre.org) project (CC0) | 0.24-0.60 s | Wodencafe |
