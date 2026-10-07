#include "HostedInstrument.h"

#include "base/source/fobject.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/common/commonstringconvert.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace underheard::vst3host
{

namespace
{
// The host context every hosted plugin sees (IHostApplication), set up once.
FUnknown* HostContext()
{
  static IPtr<HostApplication> app = owned(new HostApplication());
  static bool set = [] {
    PluginContextFactory::instance().setPluginContext(app);
    return true;
  }();
  (void)set;
  return app;
}

bool IsInstrument(const VST3::Hosting::ClassInfo& c)
{
  return c.category() == kVstAudioEffectClass && c.subCategoriesString().find("Instrument") != std::string::npos;
}

std::string Utf8(const TChar* s) { return Steinberg::StringConvert::convert(std::u16string(reinterpret_cast<const char16_t*>(s))); }

// What the hosted controller tells us about edits, passed on to the callback.
class Handler : public FObject, public IComponentHandler
{
public:
  HostedInstrument::EditCallback callback;
  tresult PLUGIN_API beginEdit(ParamID id) override { Tell(HostedInstrument::Edit::kBegin, id, 0.); return kResultOk; }
  tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override { Tell(HostedInstrument::Edit::kPerform, id, v); return kResultOk; }
  tresult PLUGIN_API endEdit(ParamID id) override { Tell(HostedInstrument::Edit::kEnd, id, 0.); return kResultOk; }
  tresult PLUGIN_API restartComponent(int32 flags) override
  {
    restart.fetch_or(flags);
    return kResultOk;
  }
  std::atomic<int32> restart{0};
  OBJ_METHODS(Handler, FObject)
  DEFINE_INTERFACES
    DEF_INTERFACE(IComponentHandler)
  END_DEFINE_INTERFACES(FObject)
  REFCOUNT_METHODS(FObject)

private:
  void Tell(HostedInstrument::Edit e, ParamID id, double v)
  {
    if (callback)
      callback(e, id, v);
  }
};

// The frame the hosted editor sits in: passes its resize requests on.
class Frame : public FObject, public IPlugFrame
{
public:
  HostedInstrument::ResizeCallback callback;
  tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override
  {
    if (!view || !newSize)
      return kInvalidArgument;
    if (callback)
      callback(newSize->getWidth(), newSize->getHeight());
    view->onSize(newSize);
    return kResultTrue;
  }
  OBJ_METHODS(Frame, FObject)
  DEFINE_INTERFACES
    DEF_INTERFACE(IPlugFrame)
  END_DEFINE_INTERFACES(FObject)
  REFCOUNT_METHODS(FObject)
};

#if defined(_WIN32)
const FIDString kPlatformType = kPlatformTypeHWND;
#elif defined(__APPLE__)
const FIDString kPlatformType = kPlatformTypeNSView;
#else
const FIDString kPlatformType = kPlatformTypeX11EmbedWindowID;
#endif

constexpr int kMaxEvents = 512;
constexpr int kNumMidiControllers = 130; // CC 0-127, then aftertouch (128) and pitch bend (129)
} // namespace

struct HostedInstrument::Impl
{
  VST3::Hosting::Module::Ptr module;
  VST3::Hosting::ClassInfo info;
  IPtr<PlugProvider> provider;
  IPtr<IComponent> component;
  IPtr<IEditController> controller;
  FUnknownPtr<IAudioProcessor> processor;
  IPtr<Handler> handler = owned(new Handler());
  IPtr<Frame> frame = owned(new Frame());
  IPtr<IPlugView> view; // the open editor
  HostProcessData data;
  EventList events{kMaxEvents};
  ParameterChanges inChanges, outChanges;
  ProcessContext context{};
  std::vector<ParameterInfo> params;
  // MIDI controllers the instrument maps to parameters (VST3 has no CC events: IMidiMapping).
  ParamID midiMap[kNumMidiControllers];
  std::string name, path;
  int maxBlock = 0, latency = 0;
  bool active = false;

  void Stop()
  {
    if (!active)
      return;
    processor->setProcessing(false);
    component->setActive(false);
    data.unprepare();
    active = false;
  }
};

void ReadParameters(HostedInstrument::Impl& m)
{
  m.params.clear();
  const int32 n = m.controller->getParameterCount();
  for (int32 i = 0; i < n; i++)
  {
    Vst::ParameterInfo pi{};
    if (m.controller->getParameterInfo(i, pi) != kResultOk)
      continue;
    ParameterInfo p;
    p.id = pi.id;
    p.title = Utf8(pi.title);
    p.units = Utf8(pi.units);
    p.stepCount = pi.stepCount;
    p.defaultValue = pi.defaultNormalizedValue;
    p.automatable = pi.flags & Vst::ParameterInfo::kCanAutomate;
    p.readOnly = pi.flags & Vst::ParameterInfo::kIsReadOnly;
    p.hidden = pi.flags & Vst::ParameterInfo::kIsHidden;
    p.isBypass = pi.flags & Vst::ParameterInfo::kIsBypass;
    p.isProgramChange = pi.flags & Vst::ParameterInfo::kIsProgramChange;
    m.params.push_back(p);
  }
}

std::vector<ClassChoice> HostedInstrument::ListInstruments(const std::string& bundlePath, std::string& error)
{
  std::vector<ClassChoice> out;
  auto module = VST3::Hosting::Module::create(bundlePath, error);
  if (!module)
    return out;
  int index = 0;
  for (const auto& c : module->getFactory().classInfos())
    if (IsInstrument(c))
      out.push_back({index++, c.name()});
  if (out.empty())
    error = "no instruments in " + bundlePath;
  return out;
}

std::unique_ptr<HostedInstrument> HostedInstrument::Load(const std::string& bundlePath, int classIndex, std::string& error)
{
  HostContext();
  auto impl = std::make_unique<Impl>();
  impl->module = VST3::Hosting::Module::create(bundlePath, error);
  if (!impl->module)
    return nullptr;
  int index = 0;
  bool found = false;
  for (const auto& c : impl->module->getFactory().classInfos())
    if (IsInstrument(c) && index++ == classIndex)
    {
      impl->info = c;
      found = true;
      break;
    }
  if (!found)
  {
    error = "no such instrument in " + bundlePath;
    return nullptr;
  }
  impl->provider = owned(new PlugProvider(impl->module->getFactory(), impl->info, true));
  if (!impl->provider->initialize())
  {
    error = "couldn't start " + impl->info.name();
    return nullptr;
  }
  impl->component = impl->provider->getComponentPtr();
  impl->controller = impl->provider->getControllerPtr();
  impl->processor = FUnknownPtr<IAudioProcessor>(impl->component);
  if (!impl->component || !impl->controller || !impl->processor)
  {
    error = impl->info.name() + " isn't a complete VST3 instrument (component, controller and processor)";
    return nullptr;
  }
  impl->controller->setComponentHandler(impl->handler);
  impl->name = impl->info.name();
  impl->path = bundlePath;

  ReadParameters(*impl);
  // MIDI controllers it maps to parameters (on channel 1; used for every channel).
  std::fill(std::begin(impl->midiMap), std::end(impl->midiMap), kNoParamId);
  FUnknownPtr<IMidiMapping> mapping(impl->controller);
  if (mapping)
    for (int cc = 0; cc < kNumMidiControllers; cc++)
    {
      ParamID id = kNoParamId;
      if (mapping->getMidiControllerAssignment(0, 0, (CtrlNumber)cc, id) == kResultOk)
        impl->midiMap[cc] = id;
    }

  auto host = std::unique_ptr<HostedInstrument>(new HostedInstrument());
  host->mImpl = std::move(impl);
  return host;
}

HostedInstrument::~HostedInstrument()
{
  if (!mImpl)
    return;
  CloseEditor();
  mImpl->Stop();
  if (mImpl->controller)
    mImpl->controller->setComponentHandler(nullptr);
  mImpl->processor = nullptr;
  mImpl->controller = nullptr;
  mImpl->component = nullptr;
  mImpl->provider = nullptr; // terminates the plugin
  mImpl->module = nullptr;   // then unloads the bundle
}

const std::string& HostedInstrument::Name() const { return mImpl->name; }
const std::string& HostedInstrument::BundlePath() const { return mImpl->path; }
std::string HostedInstrument::ClassId() const { return mImpl->info.ID().toString(); }
int HostedInstrument::Latency() const { return mImpl->latency; }

bool HostedInstrument::Prepare(double sampleRate, int maxBlock)
{
  Impl& m = *mImpl;
  m.Stop();
  ProcessSetup setup{kRealtime, kSample32, maxBlock, sampleRate};
  if (m.processor->setupProcessing(setup) != kResultOk)
    return false;
  // Stereo out (main), MIDI in; everything else off.
  SpeakerArrangement stereo = SpeakerArr::kStereo;
  m.processor->setBusArrangements(nullptr, 0, &stereo, 1);
  for (int32 b = 0; b < m.component->getBusCount(kAudio, kOutput); b++)
    m.component->activateBus(kAudio, kOutput, b, b == 0);
  for (int32 b = 0; b < m.component->getBusCount(kAudio, kInput); b++)
    m.component->activateBus(kAudio, kInput, b, false);
  for (int32 b = 0; b < m.component->getBusCount(kEvent, kInput); b++)
    m.component->activateBus(kEvent, kInput, b, b == 0);
  if (!m.data.prepare(*m.component, maxBlock, kSample32))
    return false;
  m.inChanges.setMaxParameters(std::max<int32>(64, (int32)m.params.size()));
  m.outChanges.setMaxParameters(std::max<int32>(64, (int32)m.params.size()));
  m.data.inputEvents = &m.events;
  m.data.inputParameterChanges = &m.inChanges;
  m.data.outputParameterChanges = &m.outChanges;
  m.data.processContext = &m.context;
  m.maxBlock = maxBlock;
  m.context.sampleRate = sampleRate;
  if (m.component->setActive(true) != kResultOk)
    return false;
  m.processor->setProcessing(true);
  m.latency = (int)m.processor->getLatencySamples();
  m.active = true;
  return true;
}

void HostedInstrument::Process(const MidiEvent* events, int numEvents, const ParamChange* changes, int numChanges, const Transport& transport, float* outL,
                               float* outR, int n)
{
  Impl& m = *mImpl;
  if (!m.active || n <= 0 || n > m.maxBlock)
  {
    std::fill(outL, outL + std::max(0, n), 0.f);
    std::fill(outR, outR + std::max(0, n), 0.f);
    return;
  }
  m.events.clear();
  m.inChanges.clearQueue();
  m.outChanges.clearQueue();
  auto change = [&](ParamID id, int offset, double value) {
    int32 qi = 0, pi = 0;
    if (IParamValueQueue* q = m.inChanges.addParameterData(id, qi))
      q->addPoint(std::clamp(offset, 0, n - 1), std::clamp(value, 0., 1.), pi);
  };
  for (int i = 0; i < numChanges; i++)
    change(changes[i].id, changes[i].offset, changes[i].value);
  for (int i = 0; i < numEvents; i++)
  {
    const MidiEvent& e = events[i];
    const int type = e.status & 0xF0, ch = e.status & 0x0F;
    Event ev{};
    ev.busIndex = 0;
    ev.sampleOffset = std::clamp(e.offset, 0, n - 1);
    if (type == 0x90 && e.data2 > 0)
    {
      ev.type = Event::kNoteOnEvent;
      ev.noteOn = {(int16)ch, (int16)e.data1, 0.f, e.data2 / 127.f, 0, -1};
      m.events.addEvent(ev);
    }
    else if (type == 0x80 || (type == 0x90 && e.data2 == 0))
    {
      ev.type = Event::kNoteOffEvent;
      ev.noteOff = {(int16)ch, (int16)e.data1, e.data2 / 127.f, -1, 0.f};
      m.events.addEvent(ev);
    }
    else if (type == 0xB0 && e.data1 < 128 && m.midiMap[e.data1] != kNoParamId)
      change(m.midiMap[e.data1], e.offset, e.data2 / 127.);
    else if (type == 0xD0 && m.midiMap[kAfterTouch] != kNoParamId)
      change(m.midiMap[kAfterTouch], e.offset, e.data1 / 127.);
    else if (type == 0xE0 && m.midiMap[kPitchBend] != kNoParamId)
      change(m.midiMap[kPitchBend], e.offset, ((e.data2 << 7) | e.data1) / 16383.);
  }
  ProcessContext& c = m.context;
  c.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid | (transport.playing ? ProcessContext::kPlaying : 0);
  c.sampleRate = transport.sampleRate;
  c.tempo = transport.tempo;
  c.projectTimeMusic = transport.ppq;
  c.projectTimeSamples = transport.samplePos;
  m.data.numSamples = n;
  if (m.data.numOutputs > 0)
    m.data.outputs[0].silenceFlags = 0;
  m.processor->process(m.data);
  if (m.data.numOutputs > 0 && m.data.outputs[0].numChannels > 0)
  {
    float** ch = m.data.outputs[0].channelBuffers32;
    std::copy(ch[0], ch[0] + n, outL);
    std::copy(ch[std::min(1, m.data.outputs[0].numChannels - 1)], ch[std::min(1, m.data.outputs[0].numChannels - 1)] + n, outR);
  }
  else
  {
    std::fill(outL, outL + n, 0.f);
    std::fill(outR, outR + n, 0.f);
  }
}

int HostedInstrument::NumParameters() const { return (int)mImpl->params.size(); }
const ParameterInfo& HostedInstrument::Parameter(int index) const { return mImpl->params[(size_t)index]; }
int HostedInstrument::FindParameter(uint32_t id) const
{
  for (size_t i = 0; i < mImpl->params.size(); i++)
    if (mImpl->params[i].id == id)
      return (int)i;
  return -1;
}
std::vector<int> HostedInstrument::DriftableParameters() const
{
  std::vector<int> out;
  for (size_t i = 0; i < mImpl->params.size(); i++)
  {
    const ParameterInfo& p = mImpl->params[i];
    if (p.automatable && !p.readOnly && !p.hidden && !p.isProgramChange)
      out.push_back((int)i);
  }
  return out;
}

double HostedInstrument::ControllerValue(uint32_t id) const { return mImpl->controller->getParamNormalized(id); }
void HostedInstrument::SetControllerValue(uint32_t id, double v) { mImpl->controller->setParamNormalized(id, std::clamp(v, 0., 1.)); }
std::string HostedInstrument::ValueText(uint32_t id, double v) const
{
  String128 s{};
  if (mImpl->controller->getParamStringByValue(id, v, s) != kResultOk)
    return {};
  return Utf8(s);
}

bool HostedInstrument::SaveState(std::vector<uint8_t>& component, std::vector<uint8_t>& controller) const
{
  auto grab = [](MemoryStream& s, std::vector<uint8_t>& out) { out.assign((const uint8_t*)s.getData(), (const uint8_t*)s.getData() + s.getSize()); };
  MemoryStream cs, ks;
  if (mImpl->component->getState(&cs) != kResultOk)
    return false;
  grab(cs, component);
  controller.clear();
  if (mImpl->controller->getState(&ks) == kResultOk)
    grab(ks, controller);
  return true;
}

bool HostedInstrument::RestoreState(const std::vector<uint8_t>& component, const std::vector<uint8_t>& controller)
{
  MemoryStream cs((void*)component.data(), (TSize)component.size());
  if (mImpl->component->setState(&cs) != kResultOk)
    return false;
  int64 pos = 0;
  cs.seek(0, IBStream::kIBSeekSet, &pos);
  mImpl->controller->setComponentState(&cs);
  if (!controller.empty())
  {
    MemoryStream ks((void*)controller.data(), (TSize)controller.size());
    mImpl->controller->setState(&ks);
  }
  return true;
}

bool HostedInstrument::EditorSize(int& w, int& h)
{
  if (mImpl->view)
  {
    ViewRect r;
    if (mImpl->view->getSize(&r) != kResultOk)
      return false;
    w = r.getWidth();
    h = r.getHeight();
    return true;
  }
  IPtr<IPlugView> v = owned(mImpl->controller->createView(ViewType::kEditor));
  ViewRect r;
  if (!v || v->isPlatformTypeSupported(kPlatformType) != kResultTrue || v->getSize(&r) != kResultOk)
    return false;
  w = r.getWidth();
  h = r.getHeight();
  return true;
}

bool HostedInstrument::OpenEditor(void* parent, double scale, int& w, int& h)
{
  CloseEditor();
  IPtr<IPlugView> v = owned(mImpl->controller->createView(ViewType::kEditor));
  if (!v || !parent || v->isPlatformTypeSupported(kPlatformType) != kResultTrue)
    return false;
  v->setFrame(mImpl->frame);
#if defined(_WIN32)
  if (FUnknownPtr<IPlugViewContentScaleSupport> scaling(v); scaling)
    scaling->setContentScaleFactor((IPlugViewContentScaleSupport::ScaleFactor)scale);
#else
  (void)scale;
#endif
  if (v->attached(parent, kPlatformType) != kResultOk)
  {
    v->setFrame(nullptr);
    return false;
  }
  ViewRect r;
  v->getSize(&r);
  w = r.getWidth();
  h = r.getHeight();
  mImpl->view = v;
  return true;
}

void HostedInstrument::CloseEditor()
{
  if (!mImpl->view)
    return;
  mImpl->view->removed();
  mImpl->view->setFrame(nullptr);
  mImpl->view = nullptr;
}

bool HostedInstrument::EditorOpen() const { return mImpl->view != nullptr; }
void HostedInstrument::SetEditorResizeCallback(ResizeCallback cb) { mImpl->frame->callback = std::move(cb); }

void HostedInstrument::SetEditCallback(EditCallback cb) { mImpl->handler->callback = std::move(cb); }
int HostedInstrument::TakeRestartFlags() { return mImpl->handler->restart.exchange(0); }
void HostedInstrument::RefreshParameters() { ReadParameters(*mImpl); }

} // namespace underheard::vst3host
