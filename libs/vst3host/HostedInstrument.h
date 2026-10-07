#pragma once
// Hosting one VST3 instrument (for Drifter: docs/DRIFTER-SPEC.md), on the VST3 SDK's own hosting
// code. Load a .vst3 bundle, pick one of its instruments, run it (MIDI in, stereo out, with
// sample-accurate parameter changes), list and set its parameters, and save and restore its
// state. Edits coming from its controller (its editor, when the user moves a control) are
// passed to a callback - Drifter's learn and grab.
//
// Threads: Load/Prepare/state/parameter info and SetControllerValue on the main thread;
// Process on the audio thread (no allocation, no locks).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace underheard::vst3host
{

struct ClassChoice
{
  int index = 0;      // within the bundle's instrument classes
  std::string name;   // as the bundle names it
};

struct ParameterInfo
{
  uint32_t id = 0;
  std::string title, units;
  int stepCount = 0;      // 0: continuous
  double defaultValue = 0.; // normalised
  bool automatable = false, readOnly = false, hidden = false, isBypass = false, isProgramChange = false;
};

// A MIDI note or controller for Process, at a sample offset in the block.
struct MidiEvent
{
  int offset = 0;
  uint8_t status = 0, data1 = 0, data2 = 0;
};

// A parameter change for Process, at a sample offset in the block (normalised 0..1).
struct ParamChange
{
  uint32_t id = 0;
  int offset = 0;
  double value = 0.;
};

// The song, as the host reports it (for the hosted instrument's own sync).
struct Transport
{
  double sampleRate = 48000., tempo = 120., ppq = 0.;
  bool playing = false;
  int64_t samplePos = 0;
};

class HostedInstrument
{
public:
  // What the controller tells its host: begin, perform (value) and end of an edit.
  enum class Edit { kBegin, kPerform, kEnd };
  using EditCallback = std::function<void(Edit, uint32_t id, double value)>;

  // The instrument classes in a bundle (empty, with `error` set, if it can't be opened).
  static std::vector<ClassChoice> ListInstruments(const std::string& bundlePath, std::string& error);
  // Loads one of them (`classIndex` within ListInstruments). nullptr on failure, with `error`.
  static std::unique_ptr<HostedInstrument> Load(const std::string& bundlePath, int classIndex, std::string& error);
  ~HostedInstrument();

  const std::string& Name() const;
  const std::string& BundlePath() const;
  std::string ClassId() const; // hex

  // Sets up and starts processing (main thread; call again to change rate or block size).
  bool Prepare(double sampleRate, int maxBlock);
  int Latency() const;

  // Audio thread: renders n samples into outL, outR (n <= maxBlock), with these events and
  // parameter changes (each sorted by offset).
  void Process(const MidiEvent* events, int numEvents, const ParamChange* changes, int numChanges, const Transport& transport, float* outL,
               float* outR, int n);

  // Parameters (main thread).
  int NumParameters() const;
  const ParameterInfo& Parameter(int index) const;
  int FindParameter(uint32_t id) const; // index, or -1
  // The parameters Drifter offers (its 128 slots and its lanes), in order: automatable, not
  // read-only, not hidden, not a program change. (This leaves out the MIDI-controller
  // parameters plugins like iPlug2's add, which aren't automatable.)
  std::vector<int> DriftableParameters() const;
  double ControllerValue(uint32_t id) const;
  void SetControllerValue(uint32_t id, double normalised); // what the editor shows
  std::string ValueText(uint32_t id, double normalised) const;

  // State (main thread): the instrument's own data, processor then controller.
  bool SaveState(std::vector<uint8_t>& component, std::vector<uint8_t>& controller) const;
  bool RestoreState(const std::vector<uint8_t>& component, const std::vector<uint8_t>& controller);

  void SetEditCallback(EditCallback cb);

  // The instrument's own editor (main thread), attached to a native view of the host's:
  // an NSView on macOS, an HWND on Windows. Sizes are in the platform's units (points on
  // macOS, pixels on Windows); `scale` is the screen's (Windows: passed to the editor).
  // EditorSize asks for the size without keeping an editor open (false: it has none).
  bool EditorSize(int& w, int& h);
  bool OpenEditor(void* parent, double scale, int& w, int& h);
  void CloseEditor();
  bool EditorOpen() const;
  // The editor asked to be this size (IPlugFrame::resizeView): resize the view it's in, then
  // return; the editor is told its new size after the callback.
  using ResizeCallback = std::function<void(int w, int h)>;
  void SetEditorResizeCallback(ResizeCallback cb);

  // What the synth has asked its host to restart since the last call (VST3 RestartFlags:
  // e.g. latency or parameter titles changed). Main thread.
  int TakeRestartFlags();
  static constexpr int kTitlesChanged = 1 << 4, kLatencyChanged = 1 << 3; // as in ivsteditcontroller.h
  // Re-reads the parameter list (after titles changed). Main thread.
  void RefreshParameters();

  struct Impl;

private:
  HostedInstrument() = default;
  std::unique_ptr<Impl> mImpl;
};

} // namespace underheard::vst3host
