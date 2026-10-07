#include "SectionEngine.h"

namespace underheard::section {

namespace {
// Where the desks split, by note: basses below G2, cellos below G3, violas below E4.
constexpr int kDeskFloor[kNumDesks] = {64, 55, 43, 0};

double U(Rand& r) { return 0.5 + 0.5 * r.Bipolar(); } // 0 .. 1
} // namespace

const char* DeskName(int desk)
{
  static const char* names[kNumDesks] = {"Violins", "Violas", "Cellos", "Basses"};
  return names[std::clamp(desk, 0, kNumDesks - 1)];
}

int DeskForNote(int note)
{
  for (int d = 0; d < kNumDesks; d++)
    if (note >= kDeskFloor[d])
      return d;
  return kBasses;
}

const char* TargetName(Target t)
{
  static const char* names[kNumTargets] = {"Pitch", "Position", "Cutoff", "Resonance", "Vibrato depth", "Level", "Noise amount"};
  return names[std::clamp((int)t, 0, kNumTargets - 1)];
}

double ControllerScale(Target t)
{
  switch (t)
  {
    case kTargetCutoff: return 48.; // semitones: four octaves either way at full Amount
    case kTargetPitch: return 100.; // cents
    default: return 1.;             // 0..1 settings
  }
}

// ---- Envelope and filter -------------------------------------------------------------------

double Engine::Env::Next(const Envelope& e, double timeScale, double dt)
{
  switch (stage)
  {
    case kAttack:
      value += dt / std::max(0.001, e.attack * timeScale);
      if (value >= 1.)
      {
        value = 1.;
        stage = kDecay;
      }
      break;
    case kDecay:
      value += (e.sustain - value) * (1. - std::exp(-dt * 3. / std::max(0.001, e.decay * timeScale)));
      if (std::fabs(value - e.sustain) < 1e-4)
        stage = kSustain;
      break;
    case kSustain: value = e.sustain; break;
    case kRelease:
      value -= value * (1. - std::exp(-dt * 3. / std::max(0.005, e.release * timeScale)));
      if (value < 1e-5)
      {
        value = 0.;
        stage = kIdle;
      }
      break;
    default: break;
  }
  return value;
}

double Engine::Svf::Run(double x, double cutoffHz, double resonance, int type, double fs)
{
  const double g = std::tan(kPi * std::clamp(cutoffHz, 10., 0.45 * fs) / fs);
  const double k = 1. / (0.7071 + 19. * resonance * resonance); // 1/Q: Q from 0.71 to about 20
  const double a1 = 1. / (1. + g * (g + k)), a2 = g * a1, a3 = g * a2;
  const double v3 = x - ic2;
  const double v1 = a1 * ic1 + a2 * v3;
  const double v2 = ic2 + a2 * ic1 + a3 * v3;
  ic1 = 2. * v1 - ic1;
  ic2 = 2. * v2 - ic2;
  // Band-pass is scaled to unity at its peak, so it isn't quieter than low or high pass.
  return type == kBandPass ? k * v1 : type == kHighPass ? x - k * v1 - v2 : v2;
}

// ---- Body choice -------------------------------------------------------------------------

const char* BodyChoiceName(int choice)
{
  static const char* names[kNumBodyChoices] = {"By register", "Violin", "Viola", "Cello", "Double bass", "Off"};
  return names[std::clamp(choice, 0, kNumBodyChoices - 1)];
}

int ResonatorBody(int choice, int desk)
{
  using R = voicefx::Resonator;
  switch (choice)
  {
    case kBodyViolin: return R::kViolin;
    case kBodyViola: return R::kViola;
    case kBodyCello: return R::kCello;
    case kBodyDoubleBass: return R::kDoubleBass;
    case kBodyOff: return R::kOff;
    default:
    {
      static const int byDesk[kNumDesks] = {R::kViolin, R::kViola, R::kCello, R::kDoubleBass};
      return byDesk[std::clamp(desk, 0, kNumDesks - 1)];
    }
  }
}

// ---- Notes ---------------------------------------------------------------------------------

void Engine::Prepare(double fs, int maxBlock)
{
  mFs = fs;
  mDt = 1. / fs;
  for (auto& b : mDeskBuf)
    b.assign((size_t)std::max(16, maxBlock), 0.f);
  for (auto& b : mBodies)
    b.Prepare(fs);
  for (auto& n : mNotes)
    for (auto& p : n.players)
    {
      for (auto& o : p.osc)
        o.Prepare(fs);
      p.noise.Prepare(fs);
    }
  Reset();
}

void Engine::Reset()
{
  for (auto& n : mNotes)
    n.active = false;
  for (auto& b : mBodies)
    b.Reset();
  mSustain = false;
}

void Engine::ReleaseTable(const WavetableData* table)
{
  for (auto& n : mNotes)
    for (auto& p : n.players)
      for (auto& o : p.osc)
        o.ReleaseTable(table);
  for (auto& o : mSet.osc)
    if (o.table == table)
      o.table = nullptr;
}

int Engine::ActiveNotes() const
{
  int k = 0;
  for (const auto& n : mNotes)
    k += n.active;
  return k;
}

int Engine::FreeSlot()
{
  int oldest = 0, oldestReleased = -1;
  for (int i = 0; i < kMaxNotes; i++)
  {
    const Note& n = mNotes[(size_t)i];
    if (!n.active)
      return i;
    if (n.order < mNotes[(size_t)oldest].order)
      oldest = i;
    if (n.released && (oldestReleased < 0 || n.order < mNotes[(size_t)oldestReleased].order))
      oldestReleased = i;
  }
  return oldestReleased >= 0 ? oldestReleased : oldest; // take a fading note before a held one
}

void Engine::StartPlayers(Note& n)
{
  const Settings& s = mSet;
  for (int i = 0; i < n.count; i++)
  {
    Player& p = n.players[(size_t)i];
    // Keep the oscillators and noise (their sample rate and tables); reset everything else.
    wavetable::WaveOscillator osc0 = p.osc[0], osc1 = p.osc[1];
    voicefx::NoiseLayer noise = p.noise;
    p = Player{};
    p.osc[0] = osc0;
    p.osc[1] = osc1;
    p.noise = noise;
    mRand.Bipolar(); // step the section's own generator, then seed this player from it
    p.rng.s = mRand.s ^ (0x9E3779B9u * (uint32_t)(i + 1));
    Rand& r = p.rng;
    // The leader (player 0) comes in most promptly; the others spread around it.
    p.startDelay = s.looseness * std::pow(U(r), 1.5) * 0.06 * (i == 0 ? 0.4 : 1.);
    p.releaseDelay = s.looseness * U(r) * 0.08;
    p.attackMul = 1. + s.looseness * (U(r) - 0.4) * 0.8;
    p.releaseMul = 1. + s.looseness * (U(r) - 0.4) * 0.6;
    p.swell = std::pow(2., (U(r) - 0.5) * 1.2 * s.looseness);
    const double ownRate = 5.4 + (U(r) - 0.5) * 1.2;
    if (i == 0)
      mLeaderRate = ownRate;
    p.vibRate = i == 0 ? ownRate : 0.5 * ownRate + 0.5 * mLeaderRate; // loosely following the leader
    p.vibDepth = 0.6 + 0.8 * U(r);
    p.vibOnset = s.onset * (0.7 + 0.7 * U(r));
    p.vibPhase = U(r);
    for (double& c : p.character)
      c = r.Bipolar(); // this player's own offsets, scaled by the Character depths
    p.smearCents = s.smear * 25. * r.Bipolar();
    for (int t = 0; t < kNumSpreadTargets; t++)
      p.drift[t].Set(mFs, 1.5, p.rng.s + 77u * (uint32_t)(t + 1));
    for (auto& o : p.osc)
    {
      o.SetPhase(U(r)); // players don't start in phase
      o.SetPosition(0., true);
    }
    p.noise.Reset(U(r));
    p.noise.Seed(p.rng.s + 4242u);
    p.ampEnv.Start();
    p.filterEnv.Start();
  }
}

void Engine::NoteOn(int note, int velocity)
{
  if (velocity <= 0)
  {
    NoteOff(note);
    return;
  }
  // The same note again: that note restarts (a fresh bow).
  int slot = -1;
  for (int i = 0; i < kMaxNotes; i++)
    if (mNotes[(size_t)i].active && mNotes[(size_t)i].note == note)
      slot = i;
  if (slot < 0)
    slot = FreeSlot();
  Note& n = mNotes[(size_t)slot];
  n.active = true;
  n.held = true;
  n.sustained = false;
  n.released = false;
  n.note = note;
  n.desk = DeskForNote(note);
  n.count = std::clamp(mSet.players, 1, kMaxPlayers);
  n.velocity = std::clamp(velocity / 127., 0., 1.);
  n.order = ++mOrder;
  StartPlayers(n);
  // Start each player's position where it'll sit (no slide in from 0).
  for (int i = 0; i < n.count; i++)
  {
    Player& p = n.players[(size_t)i];
    for (int o = 0; o < 2; o++)
      p.osc[o].SetPosition(mSet.osc[o].position + p.character[1] * mSet.character[1] * kCharacterScale[1], true);
  }
  Retune();
}

void Engine::Release(Note& n)
{
  n.released = true;
  for (int i = 0; i < n.count; i++)
    n.players[(size_t)i].releasedAge = n.players[(size_t)i].age;
}

void Engine::NoteOff(int note)
{
  for (auto& n : mNotes)
    if (n.active && n.held && n.note == note)
    {
      n.held = false;
      if (mSustain)
        n.sustained = true;
      else
        Release(n);
    }
  Retune();
}

void Engine::Sustain(bool down)
{
  mSustain = down;
  if (!down)
    for (auto& n : mNotes)
      if (n.active && n.sustained && !n.held)
      {
        n.sustained = false;
        Release(n);
      }
  Retune();
}

void Engine::AllNotesOff()
{
  mSustain = false;
  for (auto& n : mNotes)
    if (n.active && !n.released)
    {
      n.held = n.sustained = false;
      Release(n);
    }
}

// Pure-tuning targets for the notes still sounding (held or sustained).
void Engine::Retune()
{
  int notes[kMaxNotes], slots[kMaxNotes], k = 0;
  for (int i = 0; i < kMaxNotes; i++)
    if (mNotes[(size_t)i].active && !mNotes[(size_t)i].released)
    {
      notes[k] = mNotes[(size_t)i].note;
      slots[k++] = i;
    }
  double cents[kMaxNotes] = {};
  JustOffsets(notes, k, cents);
  for (int j = 0; j < k; j++)
    mNotes[(size_t)slots[j]].target = cents[j];
}

double Engine::PlayerCents(int slot, int i) const
{
  const Player& p = mNotes[(size_t)slot].players[(size_t)i];
  const double depth = std::clamp(mSet.vibrato + p.mod[kTargetVibrato], 0., 1.);
  const double vib = depth * 20. * p.vibDepth * p.vibIn * std::sin(2. * kPi * p.vibPhase);
  return p.tune + p.smearCents + vib; // (Character, Drift and bend aside)
}

// ---- One player, one sample ----------------------------------------------------------------

double Engine::RenderPlayer(Note& n, Player& p)
{
  const Settings& s = mSet;
  const double dt = mDt;
  p.age += dt;
  if (p.age < p.startDelay)
    return 0.;
  const double local = p.age - p.startDelay;
  if (p.releasedAge >= 0. && p.age >= p.releasedAge + p.releaseDelay && p.ampEnv.stage != Env::kRelease && p.ampEnv.stage != Env::kIdle)
  {
    p.ampEnv.Release();
    p.filterEnv.Release();
  }

  // The modulation table: Character (fixed), Drift (wandering), the controllers.
  for (double& m : p.mod)
    m = 0.;
  for (int t = 0; t < kNumSpreadTargets; t++)
    p.mod[kSpreadTargets[t]] += p.character[t] * s.character[t] * kCharacterScale[t] + p.drift[t].Next(3.) * s.drift[t] * kDriftScale[t];
  for (const Controller* c : {&s.modWheel, &s.aftertouch})
    if (c->target > 0 && c->target <= kNumControllerTargets)
    {
      const Target t = kControllerTargets[c->target - 1];
      p.mod[t] += c->value * c->amount * ControllerScale(t);
    }

  // Pitch: pure-tuning glide, the arriving smear, vibrato, the modulation table, bend.
  const double settleTarget = s.settle > 0. ? n.target : 0.;
  const double tau = 4. * std::pow(0.06, std::clamp(s.settle, 0., 1.)); // 4 s .. 0.25 s
  p.tune += (settleTarget - p.tune) * (1. - std::exp(-dt / tau));
  p.smearCents *= std::exp(-dt / 0.35);
  if (local > p.vibOnset)
    p.vibIn = std::min(1., p.vibIn + dt / 0.6);
  p.vibPhase += p.vibRate * dt;
  if (p.vibPhase >= 1.)
    p.vibPhase -= 1.;
  const double vibDepth = std::clamp(s.vibrato + p.mod[kTargetVibrato], 0., 1.);
  const double vib = vibDepth * 20. * p.vibDepth * p.vibIn * std::sin(2. * kPi * p.vibPhase);
  const double cents = p.tune + p.smearCents + vib + p.mod[kTargetPitch] + s.bend * 100.;

  // The oscillators.
  double x = 0.;
  for (int o = 0; o < 2; o++)
  {
    const Oscillator& set = s.osc[o];
    if (set.level <= 0. || !set.table)
      continue;
    wavetable::WaveOscillator& osc = p.osc[o];
    osc.SetTable(set.table);
    osc.SetFrequency(440. * std::pow(2., (n.note - 69 + 12 * set.octave + set.semi) / 12. + (cents + set.fine) / 1200.));
    osc.SetPosition(std::clamp(set.position + p.mod[kTargetPosition], 0., 1.));
    x += set.level * osc.Process();
  }

  // The filter: cutoff from the knob, the note (key tracking from middle C), velocity, its
  // envelope, and the modulation table.
  const double fenv = p.filterEnv.Next(s.filterEnv, p.attackMul, dt);
  const double semis = s.keyTrack * (n.note - 60) + s.velocity * 24. * n.velocity + s.envAmount * fenv + p.mod[kTargetCutoff];
  p.cutoffHz = std::clamp(s.cutoff * std::pow(2., semis / 12.), 20., 0.45 * mFs);
  x = p.filter.Run(x, p.cutoffHz, std::clamp(s.resonance + p.mod[kTargetResonance], 0., 1.), s.filterType, mFs);

  // The noise layer, after the filter: bursts once per cycle of the note, louder with velocity.
  const double noiseAmount = std::max(0., 3. * s.noiseAmount + p.mod[kTargetNoise]);
  if (noiseAmount > 0.)
  {
    p.noise.SetTone(s.noiseTone); // (only recomputes when it moves)
    x += p.noise.Next(440. * std::pow(2., (n.note - 69) / 12.), (0.03 + 0.08 * n.velocity) * noiseAmount);
  }

  // The amp: its envelope (each player's own swell and times), velocity, the Level target.
  const double env = p.ampEnv.Next(s.ampEnv, p.ampEnv.stage == Env::kRelease ? p.releaseMul : p.attackMul, dt);
  const double vel = 1. - s.ampVelocity * (1. - std::pow(n.velocity, 1.5));
  const double level = std::max(0., 1. + p.mod[kTargetLevel]);
  return x * std::pow(std::max(0., env), p.swell) * vel * level;
}

void Engine::Process(float* const* desks, int n)
{
  for (int d = 0; d < kNumDesks; d++) // re-voices only when the body or depth moved
    mBodies[(size_t)d].Set(ResonatorBody(mSet.body, d), 2. * mSet.bodyDepth);
  for (int start = 0; start < n;)
  {
    const int m = std::min(n - start, (int)mDeskBuf[0].size());
    for (auto& b : mDeskBuf)
      std::fill(b.begin(), b.begin() + m, 0.f);
    for (auto& note : mNotes)
    {
      if (!note.active)
        continue;
      const double norm = 0.5 / std::sqrt((double)note.count);
      float* buf = mDeskBuf[note.desk].data();
      for (int i = 0; i < m; i++)
      {
        double sum = 0.;
        for (int pl = 0; pl < note.count; pl++)
          sum += RenderPlayer(note, note.players[(size_t)pl]);
        buf[i] += (float)(sum * norm);
      }
      bool sounding = false;
      for (int pl = 0; pl < note.count; pl++)
      {
        const Player& p = note.players[(size_t)pl];
        sounding = sounding || p.ampEnv.stage != Env::kIdle || p.age < p.startDelay;
      }
      if (!sounding)
        note.active = false;
    }
    for (int d = 0; d < kNumDesks; d++)
      for (int i = 0; i < m; i++)
        desks[d][start + i] += (float)mBodies[(size_t)d].Process(mDeskBuf[d][(size_t)i]);
    start += m;
  }
}

} // namespace underheard::section
