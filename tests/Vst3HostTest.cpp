// Tests for libs/vst3host (Drifter's hosting): loading a real VST3 instrument (Section, built
// and installed by this repo), playing it, its parameters, and its state. macOS, no DAW.
#include "DriftLanes.h"
#include "HostedInstrument.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

using namespace underheard::vst3host;

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

static const double SR = 48000.;
static const int BS = 512;

static std::string Plugin(const char* name) { return std::string(std::getenv("HOME")) + "/Library/Audio/Plug-Ins/VST3/" + name; }

// Renders `secs`, with these events and changes in the first block.
static std::vector<float> Render(HostedInstrument& h, double secs, std::vector<MidiEvent> events = {}, std::vector<ParamChange> changes = {}, bool count = false)
{
  std::vector<float> out, l(BS), r(BS);
  Transport t;
  t.sampleRate = SR;
  const int blocks = (int)(secs * SR / BS);
  out.reserve((size_t)blocks * BS);
  for (int b = 0; b < blocks; b++)
  {
    gCountAllocs = count;
    h.Process(b == 0 ? events.data() : nullptr, b == 0 ? (int)events.size() : 0, b == 0 ? changes.data() : nullptr, b == 0 ? (int)changes.size() : 0, t, l.data(),
              r.data(), BS);
    gCountAllocs = false;
    out.insert(out.end(), l.begin(), l.end());
    t.samplePos += BS;
  }
  return out;
}
static double Rms(const std::vector<float>& v, size_t from = 0)
{
  double e = 0.;
  for (size_t i = from; i < v.size(); i++)
    e += (double)v[i] * v[i];
  return std::sqrt(e / (double)std::max<size_t>(1, v.size() - from));
}
static double PeakHz(const std::vector<float>& v, double lo, double hi)
{
  double best = lo, bestMag = -1.;
  for (double hz = lo; hz <= hi; hz += 0.1)
  {
    const double w = 2. * M_PI * hz / SR, k = 2. * std::cos(w);
    double s1 = 0., s2 = 0.;
    for (size_t i = v.size() / 2; i < v.size(); i++)
    {
      const double s = v[i] + k * s1 - s2;
      s2 = s1;
      s1 = s;
    }
    const double m = s1 * s1 + s2 * s2 - k * s1 * s2;
    if (m > bestMag) { bestMag = m; best = hz; }
  }
  return best;
}
static int Find(const HostedInstrument& h, const char* title)
{
  for (int i = 0; i < h.NumParameters(); i++)
    if (h.Parameter(i).title == title)
      return i;
  return -1;
}

int main()
{
  std::string error;
  const auto list = HostedInstrument::ListInstruments(Plugin("Section.vst3"), error);
  CHECK(list.size() == 1 && list[0].name == "Section", "Section.vst3 holds one instrument, Section (%zu)", list.size());
  error.clear();
  const auto effect = HostedInstrument::ListInstruments(Plugin("UnderheardDelay.vst3"), error);
  CHECK(effect.empty() && !error.empty(), "an effect bundle offers no instruments (\"%s\")", error.c_str());
  error.clear();
  auto none = HostedInstrument::Load("/no/such/plugin.vst3", 0, error);
  CHECK(!none && !error.empty(), "a missing bundle: an error, not a crash");

  error.clear();
  auto h = HostedInstrument::Load(Plugin("Section.vst3"), 0, error);
  CHECK(h != nullptr, "loads Section (%s)", error.c_str());
  if (!h)
    return 1;
  CHECK(h->Name() == "Section" && !h->ClassId().empty(), "its name and class ID (%s)", h->ClassId().c_str());
  const int cutoff = Find(*h, "Cutoff"), output = Find(*h, "Output");
  CHECK(h->NumParameters() >= 55 && cutoff >= 0 && output >= 0, "its parameters are listed (%d, Cutoff and Output among them)", h->NumParameters());
  const auto driftable = h->DriftableParameters();
  CHECK(driftable.size() == 55 && h->Parameter(driftable.front()).title == "Output" && h->Parameter(driftable.back()).title == "Seating Width",
        "the driftable ones are its own 55, in order (not the %d MIDI-controller parameters iPlug2 adds)", h->NumParameters() - 55);
  const uint32_t cutoffId = h->Parameter(cutoff).id, outputId = h->Parameter(output).id;
  CHECK(h->Parameter(cutoff).automatable && h->ValueText(cutoffId, h->ControllerValue(cutoffId)).find("3000") != std::string::npos,
        "Cutoff is automatable and reads as its default (\"%s\")", h->ValueText(cutoffId, h->ControllerValue(cutoffId)).c_str());
  CHECK(h->Prepare(SR, BS) && h->Latency() == 0, "prepared at 48 kHz, 512-sample blocks");

  // One player, no detuning, no vibrato (Section's own settings, set through the audio path).
  std::vector<ParamChange> plain;
  for (const char* t : {"Players", "Character Pitch", "Drift Pitch", "Smear", "Vibrato"})
    plain.push_back({h->Parameter(Find(*h, t)).id, 0, 0.});
  Render(*h, 0.2, {}, plain);
  const auto note = Render(*h, 2., {{0, 0x90, 69, 100}});
  CHECK(Rms(note, (size_t)SR) > 0.01 && std::fabs(PeakHz(note, 435., 445.) - 440.) < 0.3, "MIDI A4 plays it at 440 Hz (%.2f)", PeakHz(note, 435., 445.));
  const auto quiet = Render(*h, 1., {}, {{outputId, 0, 0.}});
  CHECK(20. * std::log10(Rms(quiet, (size_t)(0.5 * SR)) / Rms(note, (size_t)SR)) < -40., "a parameter change on the audio path takes effect (Output to -70 dB)");
  Render(*h, 0.5, {}, {{outputId, 0, h->Parameter(output).defaultValue}}, true);
  Render(*h, 0.5, {{0, 0x90, 64, 90}, {200, 0x80, 64, 0}}, {{cutoffId, 100, 0.3}}, true);
  CHECK(gAllocs == 0, "processing (notes and parameter changes) allocates nothing (%d)", gAllocs.load());
  Render(*h, 0.1, {{0, 0x80, 69, 0}});
  Render(*h, 3.);

  // State: set Cutoff (controller and processor), save, and restore into a fresh Section.
  h->SetControllerValue(cutoffId, 0.25);
  Render(*h, 0.1, {}, {{cutoffId, 0, 0.25}});
  std::vector<uint8_t> comp, ctrl;
  CHECK(h->SaveState(comp, ctrl) && !comp.empty(), "saves its state (%zu bytes)", comp.size());
  const std::string text = h->ValueText(cutoffId, 0.25);
  auto g = HostedInstrument::Load(Plugin("Section.vst3"), 0, error);
  g->Prepare(SR, BS);
  CHECK(g->RestoreState(comp, ctrl) && std::fabs(g->ControllerValue(cutoffId) - 0.25) < 1e-6, "restored into a fresh Section, Cutoff is back (%s)", text.c_str());
  // Both now sound the same (Section's randomness starts from the same seed).
  auto fresh = HostedInstrument::Load(Plugin("Section.vst3"), 0, error);
  fresh->Prepare(SR, BS);
  fresh->RestoreState(comp, ctrl);
  const auto a = Render(*g, 1., {{0, 0x90, 60, 100}}), b = Render(*fresh, 1., {{0, 0x90, 60, 100}});
  double diff = 0.;
  for (size_t i = 0; i < a.size(); i++)
    diff = std::max(diff, (double)std::fabs(a[i] - b[i]));
  CHECK(Rms(a) > 0.01 && diff < 1e-6, "two Sections restored from the same state play identically");
  g.reset();
  fresh.reset();
  for (int i = 0; i < 5; i++)
  {
    auto k = HostedInstrument::Load(Plugin("Section.vst3"), 0, error);
    k->Prepare(SR, BS);
    Render(*k, 0.05, {{0, 0x90, 60, 100}});
  }
  CHECK(true, "loaded and unloaded five more times");
  // ---- The lanes driving the hosted synth (stage 2): a lane on Section's Output.
  {
    auto k = HostedInstrument::Load(Plugin("Section.vst3"), 0, error);
    k->Prepare(SR, BS);
    std::vector<ParamChange> flat;
    for (const char* t : {"Players", "Character Pitch", "Drift Pitch", "Smear", "Vibrato", "Looseness"})
      flat.push_back({k->Parameter(Find(*k, t)).id, 0, 0.});
    Render(*k, 0.1, {}, flat);
    const uint32_t out = k->Parameter(Find(*k, "Output")).id;
    underheard::DriftLanes lanes;
    lanes.SetLengthSeconds(0.5);
    lanes.Engine().SetReach(1.);
    lanes.Engine().SetEnabled(true);
    const double home = k->ControllerValue(out);
    lanes.Add(out, home, 0.5, home); // drifts quieter, never louder
    // Plays a held note for `secs`, the lanes advancing (if playing) and their values going to
    // the synth every block; returns the level of each quarter second (dB).
    auto play = [&](double secs, bool playing) {
      std::vector<float> l(BS), r(BS), window;
      std::vector<double> levels;
      Transport t;
      t.sampleRate = SR;
      t.playing = playing;
      underheard::DriftLanes::Change c[underheard::DriftLanes::kMaxLanes];
      for (int b = 0; b < (int)(secs * SR / BS); b++)
      {
        lanes.Advance(BS / SR, playing, 120., 4.);
        std::vector<ParamChange> changes;
        const int n = lanes.Changes(c);
        for (int i = 0; i < n; i++)
          changes.push_back({c[i].id, 0, c[i].value});
        k->Process(nullptr, 0, changes.data(), (int)changes.size(), t, l.data(), r.data(), BS);
        window.insert(window.end(), l.begin(), l.end());
        if (window.size() >= (size_t)(0.25 * SR))
        {
          levels.push_back(20. * std::log10(Rms(window) + 1e-12));
          window.clear();
        }
      }
      return levels;
    };
    MidiEvent on{0, 0x90, 57, 100};
    k->Process(&on, 1, nullptr, 0, Transport{}, std::vector<float>(BS).data(), std::vector<float>(BS).data(), BS);
    play(1., false);
    const auto moving = play(6., true);
    const auto still = play(3., false);
    auto spread = [](const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()) - *std::min_element(v.begin(), v.end()); };
    CHECK(spread(moving) > 6. && spread(still) < 0.5, "a lane on Output drifts the synth's level while playing (%.0f dB of movement), and holds still when stopped (%.1f dB)",
          spread(moving), spread(still));
    // What the synth's editor shows follows the lane (the plugin will do this a few times a second).
    k->SetControllerValue(out, lanes.Value(0));
    CHECK(std::fabs(k->ControllerValue(out) - lanes.Value(0)) < 1e-6 && lanes.Value(0) <= home + 1e-9 && lanes.Value(0) >= 0.5 - 1e-9,
          "the synth's own control shows the drifted value (%s)", k->ValueText(out, lanes.Value(0)).c_str());
  }
  h.reset();
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
