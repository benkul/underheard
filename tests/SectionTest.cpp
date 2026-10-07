// Offline tests for libs/section: Section's ensemble, its voice (two wavetable oscillators,
// the filter, the envelopes) and the per-player modulation (Character, Drift, controllers).
#include "FactoryTables.h"
#include "SectionEngine.h"


#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

using namespace underheard;
using namespace underheard::section;

static std::atomic<bool> gCountAllocs{false};
static std::atomic<int> gAllocs{0};
void* operator new(size_t n)
{
  if (gCountAllocs)
    gAllocs++;
  if (void* p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void* operator new[](size_t n) { return operator new(n); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double FS = 48000.;
static const int BLOCK = 256;
static std::unique_ptr<WavetableData> gSine, gSaw, gVowels;

struct Desks
{
  std::vector<float> d[kNumDesks];
  std::vector<float> Sum() const
  {
    std::vector<float> s(d[0].size(), 0.f);
    for (const auto& v : d)
      for (size_t i = 0; i < v.size(); i++)
        s[i] += v[i];
    return s;
  }
};

// Renders `secs`, calling `each(time)` after every block.
template <class F>
static Desks Render(Engine& e, double secs, F each)
{
  const size_t n = (size_t)(secs * FS);
  Desks out;
  for (auto& v : out.d)
    v.assign(n, 0.f);
  for (size_t s = 0; s < n; s += BLOCK)
  {
    const int m = (int)std::min((size_t)BLOCK, n - s);
    float* ptrs[kNumDesks];
    for (int k = 0; k < kNumDesks; k++)
      ptrs[k] = out.d[k].data() + s;
    gCountAllocs = true;
    e.Process(ptrs, m);
    gCountAllocs = false;
    each((double)(s + (size_t)m) / FS);
  }
  return out;
}
static Desks Render(Engine& e, double secs) { return Render(e, secs, [](double) {}); }

static double Rms(const std::vector<float>& v, double from = 0., double to = 1e9)
{
  double s = 0.;
  const size_t a = (size_t)(from * FS), b = std::min(v.size(), (size_t)(to * FS));
  for (size_t i = a; i < b; i++)
    s += (double)v[i] * v[i];
  return b > a ? std::sqrt(s / (double)(b - a)) : 0.;
}
// Amplitude of `hz` over [from, to) seconds (Goertzel, Hann window).
static double Level(const std::vector<float>& v, double hz, double from = 0.5, double to = 1e9)
{
  const size_t a = (size_t)(from * FS), b = std::min(v.size(), (size_t)(to * FS)), n = b - a;
  const double w = 2. * kPi * hz / FS, k = 2. * std::cos(w);
  double s1 = 0., s2 = 0., wsum = 0.;
  for (size_t i = 0; i < n; i++)
  {
    const double win = 0.5 - 0.5 * std::cos(2. * kPi * (double)i / (double)(n - 1));
    wsum += win;
    const double s = v[a + i] * win + k * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return std::sqrt(std::max(0., s1 * s1 + s2 * s2 - k * s1 * s2)) * 2. / wsum;
}
static double Db(double x) { return 20. * std::log10(std::max(x, 1e-15)); }
// The strongest frequency between lo and hi (Hz), in 0.1 Hz steps, over the second half.
static double PeakHz(const std::vector<float>& v, double lo, double hi)
{
  double best = lo, bestMag = -1.;
  for (double hz = lo; hz <= hi; hz += 0.1)
  {
    const double m = Level(v, hz, v.size() / FS / 2.);
    if (m > bestMag) { bestMag = m; best = hz; }
  }
  return best;
}

// A plain section: one player, no randomness or vibrato, equal temperament; oscillator A a sine,
// B off; the filter wide open; a quick, full envelope; no bow noise, a flat body.
static void Plain(Engine& e, int players = 1)
{
  Settings& s = e.Set();
  s.players = players;
  s.looseness = 0.;
  s.vibrato = 0.;
  s.smear = 0.;
  s.settle = 0.;
  for (int t = 0; t < kNumSpreadTargets; t++)
    s.character[t] = s.drift[t] = 0.;
  s.osc[0] = {gSine.get(), 0., 0, 0, 0., 1.};
  s.osc[1] = {gSaw.get(), 0., 0, 0, 0., 0.};
  s.filterType = kLowPass;
  s.cutoff = 20000.;
  s.resonance = 0.;
  s.keyTrack = 0.;
  s.envAmount = 0.;
  s.velocity = 0.;
  s.ampEnv = {0.01, 0.1, 1., 0.3};
  s.ampVelocity = 0.;
  s.modWheel.target = s.aftertouch.target = 0;
  s.noiseAmount = 0.;
  s.body = kBodyOff; // a flat body: no resonances colouring what's measured
}
static Engine* Fresh(int players = 1)
{
  static Engine engines[16];
  static int next = 0;
  Engine* e = &engines[next++ % 16];
  *e = Engine{};
  e->Prepare(FS, BLOCK);
  Plain(*e, players);
  return e;
}

int main()
{
  gSine = wavetable::MakeFactoryTable(wavetable::kSine);
  gSaw = wavetable::MakeFactoryTable(wavetable::kSaw);
  gVowels = wavetable::MakeFactoryTable(wavetable::kVowels);

  // ---- The players (as before)
  {
    Engine& e = *Fresh();
    e.NoteOn(69, 100);
    const double hz = PeakHz(Render(e, 2.).Sum(), 430., 450.);
    CHECK(std::fabs(hz - 440.) < 0.3, "A4 plays at 440 Hz (%.1f)", hz);
    e.Set().bend = 2.;
    const double bent = PeakHz(Render(e, 2.).Sum(), 485., 500.);
    CHECK(std::fabs(bent - 493.88) < 0.3, "bent up 2 semitones: 493.9 Hz (%.1f)", bent);
  }
  {
    auto settled = [](double settle, const std::vector<int>& chord, double secs) {
      Engine& e = *Fresh();
      e.Set().settle = settle;
      for (int n : chord)
        e.NoteOn(n, 100);
      Render(e, secs);
      std::vector<double> c;
      for (int n : chord)
        for (int s = 0; s < Engine::kMaxNotes; s++)
          if (e.NoteActive(s) && e.NoteNumber(s) == n)
            c.push_back(e.PlayerTuningCents(s, 0));
      return c;
    };
    const auto pure = settled(1., {60, 64, 67}, 3.);
    CHECK(std::fabs(pure[0]) < 0.1 && std::fabs(pure[1] + 13.7) < 0.3 && std::fabs(pure[2] - 2.0) < 0.3,
          "C major settles pure over C: C %+.1f, E %+.1f, G %+.1f cents", pure[0], pure[1], pure[2]);
    const auto minor = settled(1., {57, 60, 64}, 3.);
    CHECK(std::fabs(minor[1] - 15.6) < 0.3 && std::fabs(minor[2] - 2.0) < 0.3, "A minor settles over A: C %+.1f, E %+.1f cents", minor[1], minor[2]);
    const auto off = settled(0., {60, 64, 67}, 3.);
    CHECK(std::fabs(off[1]) < 1e-9, "Settle 0 stays in equal temperament");
  }
  {
    Engine& e = *Fresh(6);
    e.Set().smear = 1.;
    e.NoteOn(60, 100);
    Render(e, 0.01);
    double early = 0., late = 0.;
    for (int p = 0; p < 6; p++)
      early = std::max(early, std::fabs(e.PlayerCents(0, p)));
    Render(e, 2.5);
    for (int p = 0; p < 6; p++)
      late = std::max(late, std::fabs(e.PlayerCents(0, p)));
    CHECK(early > 5. && late < 0.5, "Smear: players arrive up to %.0f cents off, and agree 2.5 s later", early);
  }
  {
    auto entries = [](double looseness) {
      Engine& e = *Fresh(6);
      e.Set().looseness = looseness;
      e.NoteOn(60, 100);
      std::array<double, 6> first{};
      first.fill(-1.);
      Render(e, 0.2, [&](double t) {
        for (int p = 0; p < 6; p++)
          if (first[(size_t)p] < 0. && e.PlayerLevel(0, p) > 0.)
            first[(size_t)p] = t;
      });
      return *std::max_element(first.begin(), first.end()) - *std::min_element(first.begin(), first.end());
    };
    CHECK(entries(1.) > 0.015 && entries(0.) < 1e-9, "Looseness 1: entries spread out; 0: together");
  }
  {
    Engine& e = *Fresh();
    e.Set().vibrato = 1.;
    e.Set().onset = 0.6;
    e.NoteOn(60, 100);
    double before = 0., after = 0.;
    Render(e, 0.3, [&](double) { before = std::max(before, std::fabs(e.PlayerCents(0, 0))); });
    Render(e, 2., [&](double) { after = std::max(after, std::fabs(e.PlayerCents(0, 0))); });
    CHECK(before < 1e-9 && after > 8., "vibrato waits for its onset, then swings %.0f cents", after);
  }
  {
    Engine& e = *Fresh();
    e.NoteOn(36, 100);
    e.NoteOn(76, 100);
    const Desks d = Render(e, 0.5);
    CHECK(Rms(d.d[kBasses]) > 0.01 && Rms(d.d[kViolins]) > 0.01 && Rms(d.d[kCellos]) == 0. && Rms(d.d[kViolas]) == 0., "notes go to their desks");
  }
  {
    Engine& e = *Fresh();
    for (int n = 0; n < 9; n++)
      e.NoteOn(60 + n, 100);
    bool newest = false, oldest = false;
    for (int s = 0; s < Engine::kMaxNotes; s++)
    {
      newest = newest || e.NoteNumber(s) == 68;
      oldest = oldest || (e.NoteActive(s) && e.NoteNumber(s) == 60);
    }
    CHECK(e.ActiveNotes() == 8 && newest && !oldest, "a ninth note takes the oldest one's place");
    e.AllNotesOff();
    Render(e, 3.);
    e.Sustain(true);
    e.NoteOn(60, 100);
    e.NoteOff(60);
    Render(e, 2.);
    const bool held = e.ActiveNotes() == 1;
    e.Sustain(false);
    Render(e, 3.);
    CHECK(held && e.ActiveNotes() == 0, "the sustain pedal holds a released note; lifting it lets it fade");
  }

  // ---- The voice: two oscillators
  {
    Engine& e = *Fresh();
    e.Set().osc[1] = {gSine.get(), 0., 1, 7, 0., 1.}; // B: an octave and a fifth up
    e.NoteOn(57, 100); // A3, 220 Hz
    const auto out = Render(e, 1.).Sum();
    CHECK(Db(Level(out, 220.)) > -40. && std::fabs(Db(Level(out, 659.26) / Level(out, 220.))) < 1., "oscillators A and B: 220 Hz and, an octave and a fifth up, 659 Hz");
    Engine& f = *Fresh();
    f.Set().osc[1] = {gSine.get(), 0., 1, 7, 0., 0.};
    f.NoteOn(57, 100);
    const auto solo = Render(f, 1.).Sum();
    CHECK(Db(Level(solo, 659.26) / Level(solo, 220.)) < -80., "Level 0 turns an oscillator off");
    Engine& g = *Fresh();
    g.Set().osc[0] = {gVowels.get(), 0., 0, 0, 0., 1.};
    g.NoteOn(55, 100); // G3: the vowel table's reference pitch
    const auto ah = Render(g, 1.).Sum();
    g.Set().osc[0].position = 0.5;
    Render(g, 0.2);
    const auto ee = Render(g, 1.).Sum();
    CHECK(Level(ah, 5 * 196.) > 2. * Level(ah, 12 * 196.) && Level(ee, 12 * 196.) > 2. * Level(ee, 5 * 196.), "Position moves through the table (vowels: ah, then ee)");
  }

  // ---- The filter
  {
    auto through = [](int type, double cutoff, double resonance, double probeHz) {
      Engine& e = *Fresh();
      e.Set().osc[0].table = gSaw.get();
      e.Set().filterType = type;
      e.Set().cutoff = cutoff;
      e.Set().resonance = resonance;
      e.NoteOn(45, 100); // A2, 110 Hz: harmonics every 110 Hz
      return Level(Render(e, 1.).Sum(), probeHz);
    };
    const double open = through(kLowPass, 20000., 0., 3520.), closed = through(kLowPass, 440., 0., 3520.);
    CHECK(Db(closed / open) < -30., "low-pass at 440 Hz: the 32nd harmonic (3.5 kHz) %.0f dB down (12 dB/oct)", Db(closed / open));
    const double low = through(kHighPass, 1760., 0., 110.) / through(kLowPass, 20000., 0., 110.);
    CHECK(Db(low) < -40., "high-pass at 1.76 kHz: the fundamental %.0f dB down", Db(low));
    // Band-pass (6 dB/oct each side), each frequency against the open filter.
    auto bp = [&](double hz) { return Db(through(kBandPass, 880., 0., hz) / through(kLowPass, 20000., 0., hz)); };
    const double bpC = bp(880.), bpLo = bp(110.), bpHi = bp(7040.);
    CHECK(std::fabs(bpC) < 1. && bpLo < -15. && bpHi < -15., "band-pass at 880 Hz: %+.0f dB there, %+.0f / %+.0f dB three octaves either side", bpC, bpLo, bpHi);
    const double res = through(kLowPass, 880., 0.9, 880.) / through(kLowPass, 880., 0., 880.);
    CHECK(Db(res) > 15., "resonance: +%.0f dB at the cutoff", Db(res));
  }
  {
    Engine& e = *Fresh();
    e.Set().cutoff = 500.;
    e.Set().keyTrack = 1.;
    e.NoteOn(60, 100);
    e.NoteOn(72, 100);
    Render(e, 0.1);
    double c60 = 0., c72 = 0.;
    for (int s = 0; s < Engine::kMaxNotes; s++)
      if (e.NoteActive(s))
        (e.NoteNumber(s) == 60 ? c60 : c72) = e.PlayerCutoffHz(s, 0);
    CHECK(std::fabs(c60 - 500.) < 0.5 && std::fabs(c72 / c60 - 2.) < 1e-6, "key tracking 100%%: middle C at the cutoff, an octave up twice it");

    Engine& f = *Fresh();
    f.Set().cutoff = 400.;
    f.Set().envAmount = 24.;
    f.Set().filterEnv = {0.01, 0.3, 0., 0.3};
    f.NoteOn(60, 100);
    Render(f, 0.012);
    const double peak = f.PlayerCutoffHz(0, 0);
    Render(f, 2.);
    CHECK(std::fabs(peak / 400. - 4.) < 0.2 && std::fabs(f.PlayerCutoffHz(0, 0) - 400.) < 1., "the filter envelope opens 2 octaves (%.0f Hz) and decays back (sustain 0)", peak);

    auto cutoffAt = [](int vel) {
      Engine& g = *Fresh();
      g.Set().cutoff = 400.;
      g.Set().velocity = 0.5;
      g.NoteOn(60, vel);
      Render(g, 0.05);
      return g.PlayerCutoffHz(0, 0);
    };
    const double ratio = cutoffAt(127) / cutoffAt(1);
    CHECK(std::fabs(ratio - 2.) < 0.02, "velocity opens the filter: one octave at 50%% from soft to hard (x%.2f)", ratio);
  }

  // ---- The amp envelope
  {
    Engine& e = *Fresh();
    e.Set().ampEnv = {0.05, 0.2, 0.5, 0.3};
    e.NoteOn(60, 100);
    Render(e, 1.5);
    const double sustain = e.PlayerLevel(0, 0);
    e.NoteOff(60);
    Render(e, 2.);
    CHECK(std::fabs(sustain - 0.5) < 0.01 && e.ActiveNotes() == 0, "the amp ADSR holds its sustain (%.2f) and releases to silence", sustain);
    auto loud = [](int vel) {
      Engine& g = *Fresh();
      g.Set().ampVelocity = 1.;
      g.NoteOn(60, vel);
      return Rms(Render(g, 0.5).Sum(), 0.2);
    };
    CHECK(Db(loud(127) / loud(32)) > 10., "amp velocity: a soft note %.0f dB quieter", Db(loud(127) / loud(32)));
  }

  // ---- Per-player modulation: Character and Drift, by target
  {
    Engine& e = *Fresh(6);
    e.Set().character[1] = 1.; // position only
    e.NoteOn(60, 100);
    Render(e, 0.2);
    double lo = 1e9, hi = -1e9, pitch = 0.;
    std::array<double, 6> first{};
    for (int p = 0; p < 6; p++)
    {
      first[(size_t)p] = e.PlayerMod(0, p, kTargetPosition);
      lo = std::min(lo, first[(size_t)p]);
      hi = std::max(hi, first[(size_t)p]);
      pitch = std::max(pitch, std::fabs(e.PlayerMod(0, p, kTargetPitch)));
    }
    Render(e, 1.);
    double moved = 0.;
    for (int p = 0; p < 6; p++)
      moved = std::max(moved, std::fabs(e.PlayerMod(0, p, kTargetPosition) - first[(size_t)p]));
    CHECK(hi - lo > 0.05 && pitch == 0. && moved == 0., "Character (position): each player its own fixed position (spread %.2f); pitch untouched", hi - lo);

    Engine& f = *Fresh(6);
    f.Set().drift[2] = 1.; // cutoff only
    f.NoteOn(60, 100);
    double mn = 1e9, mx = -1e9, pos = 0.;
    Render(f, 6., [&](double) {
      mn = std::min(mn, f.PlayerMod(0, 0, kTargetCutoff));
      mx = std::max(mx, f.PlayerMod(0, 0, kTargetCutoff));
      pos = std::max(pos, std::fabs(f.PlayerMod(0, 0, kTargetPosition)));
    });
    CHECK(mx - mn > 4. && pos == 0., "Drift (cutoff): a player's cutoff wanders %.1f semitones over 6 s; position untouched", mx - mn);
  }

  // ---- The controllers
  {
    Engine& e = *Fresh();
    Settings& s = e.Set();
    s.modWheel = {1, 1., 1.};   // cutoff, full amount, wheel up
    s.aftertouch = {4, 0.5, 1.}; // vibrato depth, half, pressed
    e.NoteOn(60, 100);
    Render(e, 0.05);
    const double cut = e.PlayerMod(0, 0, kTargetCutoff), vib = e.PlayerMod(0, 0, kTargetVibrato);
    s.modWheel = {1, -0.5, 1.};
    Render(e, 0.05);
    const double down = e.PlayerMod(0, 0, kTargetCutoff);
    s.modWheel.target = 0;
    Render(e, 0.05);
    CHECK(cut == 48. && vib == 0.5 && down == -24. && e.PlayerMod(0, 0, kTargetCutoff) == 0.,
          "mod wheel -> cutoff (+48, then -24 semitones at -50%%), aftertouch -> vibrato depth (+0.5), Off does nothing");
    CHECK(std::string(TargetName(kControllerTargets[0])) == "Cutoff" && std::string(TargetName(kControllerTargets[3])) == "Vibrato depth",
          "the controllers' targets are named for their menu");
  }

  // ---- Colour (libs/voicefx): the noise layer and the body
  {
    auto play = [](double amount, double tone, int body, double depth, int note = 48) {
      Engine& e = *Fresh();
      e.Set().osc[0].table = gSaw.get();
      e.Set().noiseAmount = amount;
      e.Set().noiseTone = tone;
      e.Set().body = body;
      e.Set().bodyDepth = depth;
      e.NoteOn(note, 100);
      return Render(e, 2.).Sum();
    };
    const auto quiet = play(0., 0.5, kBodyByRegister, 0.7), third = play(1. / 3., 0.5, kBodyByRegister, 0.7), more = play(0.55, 0.5, kBodyByRegister, 0.7);
    auto noiseOf = [&](const std::vector<float>& v) {
      std::vector<float> d(v.size());
      for (size_t i = 0; i < v.size(); i++)
        d[i] = v[i] - quiet[i];
      return d;
    };
    CHECK(std::fabs(Rms(noiseOf(more), 1., 2.) / Rms(noiseOf(third), 1., 2.) - 1.65) < 0.05, "noise Amount 55%%: x1.65 the first version's bow noise (33%%)");
    // Tone: the share of the noise's energy above about 6 kHz (a two-pole split).
    auto highShare = [](const std::vector<float>& v) {
      double a1 = 0., a2 = 0., lo = 0., hi = 0.;
      const double k = 1. - std::exp(-2. * kPi * 6000. / FS);
      for (size_t i = v.size() / 2; i < v.size(); i++)
      {
        a1 += (v[i] - a1) * k;
        a2 += (a1 - a2) * k;
        lo += a2 * a2;
        hi += (v[i] - a2) * (v[i] - a2);
      }
      return hi / (lo + hi);
    };
    const double darkShare = highShare(noiseOf(play(0.55, 0., kBodyByRegister, 0.7))), midShare = highShare(noiseOf(more)),
                 brightShare = highShare(noiseOf(play(0.55, 1., kBodyByRegister, 0.7)));
    CHECK(darkShare + 0.15 < midShare && midShare + 0.15 < brightShare, "noise Tone: %.0f%% / %.0f%% / %.0f%% of the noise above 6 kHz (dark, middle, bright)",
          darkShare * 100., midShare * 100., brightShare * 100.);
    // Body: By register gives a violins-desk note a violin body and a basses note a double bass.
    const auto byReg = play(0., 0.5, kBodyByRegister, 1., 76), violin = play(0., 0.5, kBodyViolin, 1., 76);
    const auto byRegLow = play(0., 0.5, kBodyByRegister, 1., 36), violinLow = play(0., 0.5, kBodyViolin, 1., 36), bassLow = play(0., 0.5, kBodyDoubleBass, 1., 36);
    double same = 0., sameLow = 0., differ = 0.;
    for (size_t i = 0; i < byReg.size(); i++)
    {
      same = std::max(same, (double)std::fabs(byReg[i] - violin[i]));
      sameLow = std::max(sameLow, (double)std::fabs(byRegLow[i] - bassLow[i]));
      differ = std::max(differ, (double)std::fabs(byRegLow[i] - violinLow[i]));
    }
    CHECK(same < 1e-6 && sameLow < 1e-6 && differ > 0.01, "Body By register: an E5 gets the violin body, a C2 the double bass; a chosen body applies to every desk");
    const auto deep = play(0., 0.5, kBodyCello, 1.), off = play(0., 0.5, kBodyOff, 1.);
    // Level is matched on white noise; a note whose low harmonics sit on the body's modes (C3 on a
    // cello: modes near 118, 193, 231 Hz) comes out somewhat louder, as with a real body.
    CHECK(std::fabs(Db(Rms(deep, 1., 2.) / Rms(off, 1., 2.))) < 4., "a cello body on a C3: the character changes, the level within a few dB (%+.1f dB against Off)",
          Db(Rms(deep, 1., 2.) / Rms(off, 1., 2.)));
  }

  // ---- Safety
  {
    Engine& e = *Fresh(6);
    Settings& s = e.Set();
    s.osc[0].table = gSaw.get();
    s.osc[1] = {gVowels.get(), 0.5, 1, 0, 7., 1.};
    s.resonance = 1.;
    s.cutoff = 2000.;
    for (int t = 0; t < kNumSpreadTargets; t++)
      s.character[t] = s.drift[t] = 1.;
    s.vibrato = 1.;
    s.noiseAmount = 1.;
    s.noiseTone = 1.;
    s.body = kBodyDoubleBass;
    s.bodyDepth = 1.;
    for (int n = 0; n < 8; n++)
      e.NoteOn(40 + 5 * n, 127);
    const auto sum = Render(e, 3.).Sum();
    double peak = 0.;
    bool finite = true;
    for (float x : sum)
    {
      peak = std::max(peak, (double)std::fabs(x));
      finite = finite && std::isfinite(x);
    }
    CHECK(finite && peak < 8. && Rms(sum, 1., 3.) > 0.02, "8 notes x 6 players, everything up, full resonance: bounded (peak %.2f)", peak);
    e.ReleaseTable(gVowels.get());
    const auto after = Render(e, 0.5).Sum();
    CHECK(s.osc[1].table == nullptr && std::isfinite(after.back()), "a released table is let go everywhere (oscillator B now silent)");
  }
  CHECK(gAllocs == 0, "no allocations while playing (%d)", gAllocs.load());
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
