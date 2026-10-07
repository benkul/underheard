// Tests for the Drifter plugin (plugins/Drifter), loaded the way a host would (through
// libs/vst3host), hosting Section inside it. macOS, no DAW: Live's own behaviour (slot names in
// its UI, automation lanes, the synth's editor) is on the Windows checklist.
#include "HostedInstrument.h"
#include "DrifterTestState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace underheard::vst3host;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double SR = 48000.;
static const int BS = 512;

// Plays `secs` (transport playing or not), with these events and parameter changes in the first
// block; returns the level of each quarter second (dB), and the samples if asked.
static std::vector<double> Play(HostedInstrument& h, double secs, bool playing, std::vector<MidiEvent> events = {}, std::vector<ParamChange> changes = {},
                                std::vector<float>* all = nullptr)
{
  std::vector<float> l(BS), r(BS), window;
  std::vector<double> levels;
  Transport t;
  t.sampleRate = SR;
  t.playing = playing;
  for (int b = 0; b < (int)(secs * SR / BS); b++)
  {
    h.Process(b == 0 ? events.data() : nullptr, b == 0 ? (int)events.size() : 0, b == 0 ? changes.data() : nullptr, b == 0 ? (int)changes.size() : 0, t, l.data(),
              r.data(), BS);
    t.samplePos += BS;
    t.ppq += BS / SR * 2.;
    window.insert(window.end(), l.begin(), l.end());
    if (all)
      all->insert(all->end(), l.begin(), l.end());
    if (window.size() >= (size_t)(0.25 * SR))
    {
      double e = 0.;
      for (float x : window) e += (double)x * x;
      levels.push_back(10. * std::log10(e / window.size() + 1e-24));
      window.clear();
    }
  }
  return levels;
}
static double Spread(const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()) - *std::min_element(v.begin(), v.end()); }
static double Mean(const std::vector<double>& v) { double s = 0.; for (double x : v) s += x; return s / (double)v.size(); }
static double PeakHz(const std::vector<float>& v, double lo, double hi)
{
  double best = lo, bestMag = -1.;
  for (double hz = lo; hz <= hi; hz += 0.1)
  {
    const double w = 2. * M_PI * hz / SR, k = 2. * std::cos(w);
    double s1 = 0., s2 = 0.;
    for (size_t i = v.size() / 2; i < v.size(); i++) { const double s = v[i] + k * s1 - s2; s2 = s1; s1 = s; }
    const double m = s1 * s1 + s2 * s2 - k * s1 * s2;
    if (m > bestMag) { bestMag = m; best = hz; }
  }
  return best;
}
static int Slot(const HostedInstrument& d, const char* title)
{
  for (int i = 0; i < d.NumParameters(); i++)
    if (d.Parameter(i).title == title)
      return i;
  return -1;
}

int main()
{
  std::string error;
  auto d = HostedInstrument::Load(Plugin("Drifter.vst3"), 0, error);
  CHECK(d != nullptr, "Drifter loads as a VST3 instrument (%s)", error.c_str());
  if (!d)
    return 1;
  d->Prepare(SR, BS);
  CHECK(d->NumParameters() >= kOwnParams + kSlots && d->Parameter(kOwnParams).title == "Slot 1" && d->Latency() == 0,
        "its own controls and 128 slots (%d parameters)", d->NumParameters());
  auto silent = Play(*d, 0.5, true, {{0, 0x90, 69, 100}});
  CHECK(Mean(silent) < -200., "no synth loaded: silent");

  // Section's default state and its Output parameter, for the Drifter state.
  auto s = HostedInstrument::Load(Plugin("Section.vst3"), 0, error);
  std::vector<uint8_t> comp, ctrl;
  s->SaveState(comp, ctrl);
  const uint32_t outputId = s->Parameter(0).id;
  const double outputHome = s->ControllerValue(outputId);
  s.reset();

  // A Drifter with Section inside, and a lane on its Output (drifting quieter, never louder).
  const auto state = DrifterState(Plugin("Section.vst3"), comp, ctrl, {{outputId, outputHome, 0.5, outputHome}}, 0.5);
  CHECK(d->RestoreState(state, {}), "restoring a project with Section inside");
  if (d->TakeRestartFlags() & HostedInstrument::kTitlesChanged)
    d->RefreshParameters();
  const int out = Slot(*d, "Output"), players = Slot(*d, "Players"), width = Slot(*d, "Seating Width");
  CHECK(out == kOwnParams && players == kOwnParams + 1 && width == kOwnParams + 54 && d->Parameter(kOwnParams + 55).title == "Slot 56 (unused)",
        "the slots take Section's parameter names, in order (Output, Players ... Seating Width; the rest unused)");
  CHECK(std::fabs(d->ControllerValue(d->Parameter(out).id) - outputHome) < 1e-6 && d->ValueText(d->Parameter(out).id, outputHome).find("0.0") != std::string::npos,
        "and show its values as it writes them (Output: \"%s\")", d->ValueText(d->Parameter(out).id, outputHome).c_str());

  // Automation through the slots reaches the synth: one player, no detuning, no vibrato.
  std::vector<ParamChange> plain;
  for (const char* t : {"Players", "Character Pitch", "Drift Pitch", "Smear", "Vibrato", "Looseness"})
    plain.push_back({d->Parameter(Slot(*d, t)).id, 0, 0.});
  std::vector<float> samples;
  Play(*d, 0.3, false, {}, plain);
  Play(*d, 2., false, {{0, 0x90, 69, 100}}, {}, &samples);
  CHECK(std::fabs(PeakHz(samples, 435., 445.) - 440.) < 0.3, "MIDI plays the synth inside, and slot automation reaches it (one untuned player: %.2f Hz)",
        PeakHz(samples, 435., 445.));

  // The lane: drifts the level while the song plays, holds when it stops.
  Play(*d, 0.5, false);
  const auto moving = Play(*d, 6., true), still = Play(*d, 3., false);
  CHECK(Spread(moving) > 6. && Spread(still) < 0.5, "the lane drifts Section's level while playing (%.0f dB), holds when stopped (%.1f dB)", Spread(moving),
        Spread(still));

  // Automation on the drifting parameter's slot moves its home.
  const auto before = Play(*d, 1., false);
  const auto after = Play(*d, 1.5, false, {}, {{d->Parameter(out).id, 0, 0.6}});
  CHECK(Mean(after) < Mean(before) - 3., "automating the drifting Output slot moves its home down (%.0f dB)", Mean(after) - Mean(before));

  // Saved and reopened: the synth, its settings and the lane come back.
  std::vector<uint8_t> saved, unused;
  d->SaveState(saved, unused);
  auto e = HostedInstrument::Load(Plugin("Drifter.vst3"), 0, error);
  e->Prepare(SR, BS);
  e->RestoreState(saved, {});
  if (e->TakeRestartFlags() & HostedInstrument::kTitlesChanged)
    e->RefreshParameters();
  Play(*e, 0.3, false, {}, plain);
  samples.clear();
  Play(*e, 2., false, {{0, 0x90, 69, 100}}, {}, &samples);
  const auto reopened = Play(*e, 6., true);
  CHECK(Slot(*e, "Output") == kOwnParams && std::fabs(PeakHz(samples, 435., 445.) - 440.) < 0.3 && Spread(reopened) > 6.,
        "saved and reopened: Section inside, playing, and its lane drifting");

  // A synth that isn't there: silent, and its path and settings survive saving again.
  auto m = HostedInstrument::Load(Plugin("Drifter.vst3"), 0, error);
  m->Prepare(SR, BS);
  const std::string gone = "/Library/Audio/Plug-Ins/VST3/Not Here.vst3";
  m->RestoreState(DrifterState(gone, comp, ctrl, {}, 1.), {});
  const auto quiet = Play(*m, 0.5, true, {{0, 0x90, 69, 100}});
  std::vector<uint8_t> resaved;
  m->SaveState(resaved, unused);
  const bool kept = std::search(resaved.begin(), resaved.end(), gone.begin(), gone.end()) != resaved.end() &&
                    std::search(resaved.begin(), resaved.end(), comp.begin(), comp.end()) != resaved.end();
  CHECK(Mean(quiet) < -200. && kept, "a missing synth: silent, and saving keeps its path and its settings");

  m.reset();
  e.reset();
  d.reset();
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
