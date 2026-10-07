#pragma once
// Section's ensemble: up to 8 notes, each taken up by 1-6 players who behave like people.
//
// The voice every player plays (docs/SECTION-SPEC.md): two wavetable oscillators -> a 12 dB/oct
// state-variable filter (low, band or high pass) with its own ADSR -> an amp ADSR -> bow noise.
// Notes are seated in desks by register (violins, violas, cellos, basses); each desk has a body
// resonance and comes out on its own channel, so the plugin can place each desk in a room.
//
// The players: their own entry and release timing (Looseness), swell, vibrato that arrives after
// the note starts (loosely following a leader), an intonation that starts a little off (Smear)
// and glides to pure tuning over the chord's root (Settle).
//
// Per-player modulation: each player has one table of offsets, one per Target. Character (fixed
// per player, set at note-on), Drift (a slow wander), the controllers (mod wheel, aftertouch)
// and vibrato each add to the targets they reach; the voice reads the totals. Adding a target is
// one enum entry plus the line that reads it.
//
// Prepare() allocates; everything else is real-time safe.

#include "FxCommon.h"
#include "JustTuning.h"
#include "NoiseLayer.h"
#include "Resonator.h"
#include "WaveOscillator.h"

#include <array>
#include <vector>

namespace underheard::section {

using fx::Rand;
using fx::Wander;
using wavetable::WavetableData;

enum Desk { kViolins = 0, kViolas, kCellos, kBasses, kNumDesks };
const char* DeskName(int desk);
int DeskForNote(int note);

// What per-player modulation can reach. Units: pitch in cents, cutoff in semitones, the rest
// as offsets to their 0..1 settings.
enum Target { kTargetPitch = 0, kTargetPosition, kTargetCutoff, kTargetResonance, kTargetVibrato, kTargetLevel, kTargetNoise, kNumTargets };
// The targets Character and Drift have depths for (the EFFECTS grid), in order.
constexpr Target kSpreadTargets[] = {kTargetPitch, kTargetPosition, kTargetCutoff};
constexpr int kNumSpreadTargets = 3;
// The controllers' Target menu: Off, then these.
constexpr Target kControllerTargets[] = {kTargetCutoff, kTargetResonance, kTargetPosition, kTargetVibrato, kTargetLevel, kTargetNoise};
constexpr int kNumControllerTargets = 6;
const char* TargetName(Target t);

enum FilterType { kLowPass = 0, kBandPass, kHighPass };

struct Envelope
{
  double attack = 0.01, decay = 0.3, sustain = 1., release = 0.5; // seconds, 0..1 sustain
};

struct Oscillator
{
  const WavetableData* table = nullptr; // shared, owned by whoever set it
  double position = 0.;                 // 0..1
  int octave = 0, semi = 0;
  double fine = 0.;                     // cents
  double level = 1.;                    // 0 = off
};

struct Controller
{
  int target = 0;      // 0 Off, else kControllerTargets[target - 1]
  double amount = 0.;  // -1..1
  double value = 0.;   // the controller's position now, 0..1 (set from MIDI)
};

struct Settings
{
  // VOICES
  int players = 4; // per note, 1..6
  Oscillator osc[2];
  int filterType = kLowPass;
  double cutoff = 3000.;    // Hz
  double resonance = 0.1;   // 0..1
  double keyTrack = 0.5;    // 0..1: how much the cutoff follows the note (1 = fully, from middle C)
  double envAmount = 12.;   // semitones the filter envelope opens the cutoff at its peak
  double velocity = 0.5;    // 0..1: velocity -> cutoff (up to +24 semitones)
  Envelope filterEnv{0.2, 0.6, 0.6, 0.5};
  Envelope ampEnv{0.25, 0.5, 1., 0.5};
  double ampVelocity = 0.7; // 0..1: how much velocity sets loudness
  Controller modWheel{1, 0.5}, aftertouch{4, 0.5}; // defaults: wheel -> cutoff, aftertouch -> vibrato
  double bend = 0.;         // semitones
  // EFFECTS: the players
  double looseness = 0.4, smear = 0.4, settle = 0.5;
  double vibrato = 0.5;     // 0..1: depth (up to about 20 cents)
  double onset = 0.4;       // seconds before vibrato arrives
  double character[kNumSpreadTargets] = {0.3, 0.3, 0.3}; // depths: pitch, position, cutoff
  double drift[kNumSpreadTargets] = {0.4, 0.2, 0.2};
  // EFFECTS: colour (libs/voicefx)
  double noiseAmount = 0.55; // 0..1 (x3 the first version's bow noise at 1; 0.55 is +4.3 dB on it)
  double noiseTone = 0.5;    // 0 dark .. 0.5 (the first version's) .. 1 bright
  int body = 0;              // a BodyChoice
  double bodyDepth = 0.7;    // 0..1 (x2 the measured resonances at 1)
};

// The EFFECTS Body menu.
enum BodyChoice { kBodyByRegister = 0, kBodyViolin, kBodyViola, kBodyCello, kBodyDoubleBass, kBodyOff, kNumBodyChoices };
const char* BodyChoiceName(int choice);
// The body a desk gets for a choice (By register: violins a violin, ... basses a double bass).
int ResonatorBody(int choice, int desk);

// How far a depth of 1 reaches, per spread target: pitch +/-15 cents (Character) or 12 cents
// (Drift), position +/-0.15, cutoff +/-12 semitones.
constexpr double kCharacterScale[kNumSpreadTargets] = {15., 0.15, 12.};
constexpr double kDriftScale[kNumSpreadTargets] = {12., 0.15, 12.};
// How far a controller at full Amount reaches, per target.
double ControllerScale(Target t);

class Engine
{
public:
  static constexpr int kMaxNotes = 8, kMaxPlayers = 6;

  void Prepare(double sampleRate, int maxBlock);
  void Reset();
  Settings& Set() { return mSet; }

  void NoteOn(int note, int velocity);
  void NoteOff(int note);
  void Sustain(bool down);
  void AllNotesOff();
  // Forgets a table about to be freed (every player's oscillators let go of it).
  void ReleaseTable(const WavetableData* table);

  // Renders n samples into one mono buffer per desk (body included), added to `desks`.
  void Process(float* const* desks, int n);

  // For tests and drawing.
  bool NoteActive(int slot) const { return mNotes[(size_t)slot].active; }
  int NoteNumber(int slot) const { return mNotes[(size_t)slot].note; }
  double PlayerCents(int slot, int p) const; // pitch offset from equal temperament: tuning, smear, vibrato
  double PlayerTuningCents(int slot, int p) const { return mNotes[(size_t)slot].players[(size_t)p].tune; }
  double PlayerLevel(int slot, int p) const { return mNotes[(size_t)slot].players[(size_t)p].ampEnv.value; }
  double PlayerMod(int slot, int p, Target t) const { return mNotes[(size_t)slot].players[(size_t)p].mod[t]; }
  double PlayerCutoffHz(int slot, int p) const { return mNotes[(size_t)slot].players[(size_t)p].cutoffHz; }
  int ActiveNotes() const;

private:
  struct Env // ADSR: a linear attack, exponential decay and release
  {
    enum Stage { kIdle, kAttack, kDecay, kSustain, kRelease } stage = kIdle;
    double value = 0.;
    void Start() { stage = kAttack; }
    void Release() { if (stage != kIdle) stage = kRelease; }
    double Next(const Envelope& e, double timeScale, double dt);
  };
  struct Svf // Zavalishin's TPT state-variable filter: 12 dB/oct low, band and high pass
  {
    double ic1 = 0., ic2 = 0.;
    double Run(double x, double cutoffHz, double resonance, int type, double fs);
  };
  struct Player
  {
    // Fixed per note-on
    double startDelay = 0., releaseDelay = 0., attackMul = 1., releaseMul = 1., swell = 1.;
    double vibRate = 5.4, vibDepth = 1., vibOnset = 1.;
    double character[kNumSpreadTargets] = {}; // this player's fixed offsets, -1..1
    // Running
    double age = 0., releasedAge = -1.;
    double tune = 0., smearCents = 0., vibPhase = 0., vibIn = 0.;
    double mod[kNumTargets] = {};
    double cutoffHz = 1000.;
    wavetable::WaveOscillator osc[2];
    Svf filter;
    Env ampEnv, filterEnv;
    Wander drift[kNumSpreadTargets];
    voicefx::NoiseLayer noise;
    Rand rng;
  };
  struct Note
  {
    bool active = false, held = false, sustained = false, released = false;
    int note = 0, desk = 0, count = 4; // count: how many players
    double velocity = 1., target = 0.; // target: pure-tuning offset (cents)
    uint64_t order = 0;
    std::array<Player, kMaxPlayers> players{};
  };
  void StartPlayers(Note& n);
  void Release(Note& n);
  void Retune();
  double RenderPlayer(Note& n, Player& p);
  int FreeSlot();

  double mFs = 48000., mDt = 1. / 48000.;
  Settings mSet;
  std::array<Note, kMaxNotes> mNotes;
  std::array<voicefx::Resonator, kNumDesks> mBodies;
  std::vector<float> mDeskBuf[kNumDesks];
  bool mSustain = false;
  uint64_t mOrder = 0;
  Rand mRand;
  double mLeaderRate = 5.4;
};

} // namespace underheard::section
