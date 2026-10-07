#include "Section.h"
#include "IPlug_include_in_plug_src.h"

#include "FactoryTables.h"
#include "MicModel.h"
#include "RoomModel.h"
#include "TapeFiles.h"
#if IPLUG_EDITOR
#include "UnderheardUI.h"
using namespace underheard_ui;
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <utility>

using namespace underheard::section;
namespace wt = underheard::wavetable;
using underheard::room::Convolver;

// horsi-vst3's horse tables, embedded at build time (the generated HorsiTables.cpp).
namespace underheard::section_assets
{
struct EmbeddedFile { const char* name; const unsigned char* data; size_t size; };
extern const EmbeddedFile kHorsiTables[];
extern const int kHorsiTablesCount;
}

namespace {

constexpr int32_t kStateMagic = 'SCTN';
constexpr int32_t kStateVersion = 2; // 2: the redesign (wavetable voice, three tabs); version 1 opens with defaults
constexpr int kMaxLoaded = 8;        // loaded tables kept per instance (unused ones beyond this are freed)
// Where the desks sit, across the width of the room near one end (violins on the left, basses
// on the right, like an orchestra), and where they're panned when heard close.
constexpr double kSeatX[kNumDesks] = {0.22, 0.4, 0.6, 0.8}, kSeatY = 0.22;
constexpr double kClosePan[kNumDesks] = {-0.6, -0.2, 0.25, 0.6};
// The default voice: a string section (oscillator A on the bowed-string table, B a saw, off).
constexpr int kDefaultTable[2] = {wt::kBowedString, wt::kSaw};

// horsi-vst3's horse tables, embedded at build time (cmake/UnderheardEmbed.cmake), offered after
// the generated factory tables. Credits: assets/wavetables/horsi/CREDITS.md.
const std::vector<wt::EmbeddedTable>& HorsiTables()
{
  static const std::vector<wt::EmbeddedTable> tables = [] {
    // Display names: the file name without "Horsi-" and ".wav", hyphens to spaces ("Horse: Whinny to Breath").
    static std::vector<std::string> names;
    std::vector<wt::EmbeddedTable> t;
    for (int i = 0; i < underheard::section_assets::kHorsiTablesCount; i++)
    {
      std::string n = underheard::section_assets::kHorsiTables[i].name;
      if (n.rfind("Horsi-", 0) == 0) n = n.substr(6);
      if (n.size() > 4 && n.substr(n.size() - 4) == ".wav") n = n.substr(0, n.size() - 4);
      std::replace(n.begin(), n.end(), '-', ' ');
      names.push_back("Horse: " + n);
    }
    for (int i = 0; i < underheard::section_assets::kHorsiTablesCount; i++)
      t.push_back({names[(size_t)i].c_str(), underheard::section_assets::kHorsiTables[i].data, underheard::section_assets::kHorsiTables[i].size});
    return t;
  }();
  return tables;
}

// The one template: the defaults (a string section).
const char* kTemplateNames[kNumPresets] = {"String section"};

underheard::room::RoomShape CurrentShape(const IParam* room, const IParam* size, const IParam* surfaces)
{
  const auto& presets = underheard::room::RoomPresets();
  return underheard::room::MakeShape(presets[(size_t)std::clamp(room->Int(), 0, (int)presets.size() - 1)], size->Value() / 100., surfaces->Value() / 100.);
}

std::vector<uint8_t> ReadBytes(const std::string& path)
{
  std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
  return f ? std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()) : std::vector<uint8_t>{};
}

uint64_t Fnv1a(const std::vector<uint8_t>& bytes)
{
  uint64_t h = 1469598103934665603ull;
  for (uint8_t b : bytes)
  {
    h ^= b;
    h *= 1099511628211ull;
  }
  return h;
}

std::string CopyName(uint64_t hash)
{
  char name[32];
  std::snprintf(name, sizeof name, "%016llx.wav", (unsigned long long)hash);
  return name;
}

// Above 0.8 of full scale, a soft knee into a hard ceiling at 1.0 (heavy resonance can't clip).
inline double SoftLimit(double x)
{
  const double a = std::fabs(x);
  if (a <= 0.8)
    return x;
  return std::copysign(0.8 + 0.2 * std::tanh((a - 0.8) / 0.2), x);
}

#if IPLUG_EDITOR
// An oscillator's table button: shows the table's name; a click opens the menu of tables.
class TableButton : public IVButtonControl
{
public:
  TableButton(const IRECT& bounds, const IVStyle& style, std::function<std::vector<std::string>()> items, std::function<void(int)> choose)
  : IVButtonControl(bounds, nullptr, "", style), mItems(std::move(items)), mChoose(std::move(choose))
  {
  }
  void OnMouseDown(float, float, const IMouseMod&) override
  {
    mMenu.Clear();
    for (const auto& item : mItems())
      mMenu.AddItem(item.c_str());
    GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }
  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    if (menu && menu->GetChosenItemIdx() >= 0)
      mChoose(menu->GetChosenItemIdx());
  }

private:
  IPopupMenu mMenu;
  std::function<std::vector<std::string>()> mItems;
  std::function<void(int)> mChoose;
};
#endif

} // namespace

Section::Section(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
, mTapeDir(underheard::TapeDirectory("Section"))
{
  // ---- VOICES
  GetParam(kParamOutput)->InitGain("Output", 0., -70., 12.);
  GetParam(kParamPlayers)->InitInt("Players", 4, 1, Engine::kMaxPlayers, "per note");
  for (int o = 0; o < 2; o++)
  {
    const int base = o == 0 ? kParamAPosition : kParamBPosition;
    const char* n = o == 0 ? "A" : "B";
    char name[32];
    std::snprintf(name, sizeof name, "Osc %s Position", n);
    GetParam(base)->InitPercentage(name, o == 0 ? 60. : 0.);
    std::snprintf(name, sizeof name, "Osc %s Octave", n);
    GetParam(base + 1)->InitInt(name, 0, -3, 3);
    std::snprintf(name, sizeof name, "Osc %s Semi", n);
    GetParam(base + 2)->InitInt(name, 0, -12, 12);
    std::snprintf(name, sizeof name, "Osc %s Fine", n);
    GetParam(base + 3)->InitDouble(name, 0., -100., 100., 0.1, "cents");
    std::snprintf(name, sizeof name, "Osc %s Level", n);
    GetParam(base + 4)->InitPercentage(name, o == 0 ? 100. : 0.); // B starts off
  }
  GetParam(kParamFilterType)->InitEnum("Filter Type", kLowPass, {"Low pass", "Band pass", "High pass"});
  GetParam(kParamCutoff)->InitFrequency("Cutoff", 3000., 20., 20000.);
  GetParam(kParamResonance)->InitPercentage("Resonance", 10.);
  GetParam(kParamKeyTrack)->InitPercentage("Key Track", 50.);
  GetParam(kParamFilterEnvAmount)->InitDouble("Filter Env", 12., -48., 48., 0.1, "st");
  GetParam(kParamFilterVelocity)->InitPercentage("Filter Velocity", 50.);
  auto envParams = [&](int base, const char* who, double a, double d, double s, double r) {
    char name[32];
    std::snprintf(name, sizeof name, "%s Attack", who);
    GetParam(base)->InitDouble(name, a, 1., 5000., 1., "ms", 0, "", IParam::ShapePowCurve(3.));
    std::snprintf(name, sizeof name, "%s Decay", who);
    GetParam(base + 1)->InitDouble(name, d, 1., 5000., 1., "ms", 0, "", IParam::ShapePowCurve(3.));
    std::snprintf(name, sizeof name, "%s Sustain", who);
    GetParam(base + 2)->InitPercentage(name, s);
    std::snprintf(name, sizeof name, "%s Release", who);
    GetParam(base + 3)->InitDouble(name, r, 5., 8000., 1., "ms", 0, "", IParam::ShapePowCurve(3.));
  };
  envParams(kParamFAttack, "Filter", 200., 600., 60., 500.);
  envParams(kParamAAttack, "Amp", 250., 500., 100., 500.);
  GetParam(kParamAmpVelocity)->InitPercentage("Amp Velocity", 70.);
  const std::initializer_list<const char*> targets = {"Off", "Cutoff", "Resonance", "Position", "Vibrato depth", "Level", "Noise amount"};
  GetParam(kParamWheelTarget)->InitEnum("Mod Wheel Target", 1, targets);
  GetParam(kParamWheelAmount)->InitDouble("Mod Wheel Amount", 50., -100., 100., 0.1, "%");
  GetParam(kParamTouchTarget)->InitEnum("Aftertouch Target", 4, targets);
  GetParam(kParamTouchAmount)->InitDouble("Aftertouch Amount", 50., -100., 100., 0.1, "%");
  // ---- EFFECTS
  GetParam(kParamLooseness)->InitPercentage("Looseness", 40.);
  GetParam(kParamSmear)->InitPercentage("Smear", 40.);
  GetParam(kParamSettle)->InitPercentage("Settle", 50.);
  GetParam(kParamVibrato)->InitPercentage("Vibrato", 50.);
  GetParam(kParamOnset)->InitDouble("Vibrato Onset", 400., 0., 2000., 1., "ms", 0, "", IParam::ShapePowCurve(1.5));
  GetParam(kParamCharPitch)->InitPercentage("Character Pitch", 30.);
  GetParam(kParamCharPosition)->InitPercentage("Character Position", 30.);
  GetParam(kParamCharCutoff)->InitPercentage("Character Cutoff", 30.);
  GetParam(kParamDriftPitch)->InitPercentage("Drift Pitch", 40.);
  GetParam(kParamDriftPosition)->InitPercentage("Drift Position", 20.);
  GetParam(kParamDriftCutoff)->InitPercentage("Drift Cutoff", 20.);
  GetParam(kParamNoiseAmount)->InitPercentage("Noise Amount", 55.);
  GetParam(kParamNoiseTone)->InitPercentage("Noise Tone", 50.);
  GetParam(kParamBody)->InitEnum("Body", kBodyByRegister, {"By register", "Violin", "Viola", "Cello", "Double bass", "Off"});
  GetParam(kParamBodyDepth)->InitPercentage("Body Depth", 70.);
  GetParam(kParamWarmth)->InitPercentage("Warmth", 50.);
  // ---- ROOM
  GetParam(kParamRoomAmount)->InitPercentage("Room", 50.);
  {
    IParam* room = GetParam(kParamRoom);
    const auto& presets = underheard::room::RoomPresets();
    room->InitEnum("Room Preset", 8, (int)presets.size()); // Church hall
    for (int i = 0; i < (int)presets.size(); i++)
      room->SetDisplayText(i, presets[(size_t)i].name);
  }
  GetParam(kParamSize)->InitDouble("Size", 0., -100., 100., 1., "%");
  GetParam(kParamSurfaces)->InitDouble("Surfaces", 0., -100., 100., 1., "", 0, "", IParam::ShapeLinear(), IParam::kUnitCustom,
    [](double v, WDL_String& s) { s.SetFormatted(32, v < -2. ? "softer %.0f%%" : v > 2. ? "harder %.0f%%" : "as built", std::fabs(v)); });
  GetParam(kParamDistance)->InitDouble("Distance", 35., 0., 100., 0.1, "", 0, "", IParam::ShapeLinear(), IParam::kUnitCustom,
    [this](double, WDL_String& s) { s.SetFormatted(32, "%.1f m", DistanceMetres()); });
  {
    IParam* mic = GetParam(kParamMic);
    const auto& mics = underheard::room::MicModels();
    mic->InitEnum("Mics", 9, (int)mics.size()); // a pair of Coles 4038 ribbons
    for (int i = 0; i < (int)mics.size(); i++)
      mic->SetDisplayText(i, mics[(size_t)i].name);
  }
  GetParam(kParamPair)->InitEnum("Pair", 0, {"XY", "ORTF", "Spaced"}); // XY figure-8s: a Blumlein pair
  GetParam(kParamWidth)->InitPercentage("Seating Width", 80.);

  // The factory tables (a moment's work): Section's generated ones, then the horse tables.
  wt::RegisterEmbeddedTables(HorsiTables().data(), (int)HorsiTables().size());
  for (int i = 0; i < wt::NumFactoryTables(); i++)
    mFactory.push_back(wt::MakeFactoryTable(i));
  for (int o = 0; o < 2; o++)
    UseTableSource(o, TableSource{kDefaultTable[o], "", 0, wt::FactoryTableName(kDefaultTable[o])});

  // The template, as the host's factory preset (in the state format).
  {
    IByteChunk chunk;
    SerializeState(chunk); // everything at its default now
    MakePresetFromChunk(kTemplateNames[0], chunk);
  }

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
  };

  mLayoutFunc = [&](IGraphics* g) {
    g->AttachCornerResizer(EUIResizerMode::Scale, false);
    g->AttachPanelBackground(kBackground);
    g->LoadFont("Roboto-Regular", ROBOTO_FN);
    const IVStyle style = PanelStyle();
    const IText rowLabel(14.f, kTextDim, "Roboto-Regular", EAlign::Near);
    IRECT b = g->GetBounds().GetPadded(-16.f);
    IRECT title = b.ReduceFromTop(36.f);
    g->AttachControl(new ITextControl(title.ReduceFromLeft(200.f), "SECTION", IText(22.f, kText, "Roboto-Regular", EAlign::Near)));
    {
      std::vector<std::string> names(kTemplateNames, kTemplateNames + kNumPresets);
      g->AttachControl(new MenuButton(title.ReduceFromRight(150.f).GetPadded(-2.f), "TEMPLATES", style, names, [this](int i) { ApplyTemplate(i); }));
      title.ReduceFromRight(12.f);
    }
    g->AttachControl(new ITextControl(title, "", IText(15.f, kText, "Roboto-Regular", EAlign::Near)), kCtrlTagInfo);
    b.ReduceFromTop(6.f);

    // Three pages: VOICES, EFFECTS, ROOM.
    static const char* kPages[3] = {"voices", "effects", "room"};
    g->AttachControl(new IVTabSwitchControl(b.ReduceFromTop(32.f).GetFromLeft(420.f), [g](IControl* c) {
      const int page = static_cast<IVTabSwitchControl*>(c)->GetSelectedIdx();
      for (int p = 0; p < 3; p++)
        g->ForControlInGroup(kPages[p], [show = p == page](IControl* ctl) { ctl->Hide(!show); });
      g->SetAllControlsDirty(); // repaint the whole window: partial repaints left the old page's knobs showing
    }, {"VOICES", "EFFECTS", "ROOM"}, "", style));
    b.ReduceFromTop(6.f);
    g->AttachControl(new ITextControl(b.ReduceFromTop(24.f), "", IText(14.f, kAccent, "Roboto-Regular", EAlign::Near)), kCtrlTagStatus);
    b.ReduceFromTop(8.f);

    // On every page: the keyboard and the output.
    g->AttachControl(new IVKeyboardControl(b.ReduceFromBottom(80.f), 36, 96), kCtrlTagKeyboard);
    b.ReduceFromBottom(8.f);
    IRECT out = b.ReduceFromBottom(70.f);
    b.ReduceFromBottom(8.f);
    auto cell = [](const IRECT& r, int i, int span = 1, int cols = 8) {
      IRECT c = r.GetGridCell(0, i, 1, cols);
      for (int k = 1; k < span; k++)
        c = c.Union(r.GetGridCell(0, i + k, 1, cols));
      return c.GetPadded(-5.f);
    };
    g->AttachControl(new ITextControl(out.ReduceFromLeft(110.f), "OUTPUT", rowLabel));
    g->AttachControl(new IVKnobControl(cell(out, 0), kParamOutput, "", style));
    g->AttachControl(new IVPeakAvgMeterControl<2>(cell(out, 1, 7).GetMidVPadded(22.f), "", style, EDirection::Horizontal, {"L", "R"}), kCtrlTagOutputMeter);

    const IRECT pageArea = b;
    IRECT page = pageArea;
    const char* group = kPages[0];
    auto row = [&](const char* label, float h) {
      IRECT r = page.ReduceFromTop(h);
      page.ReduceFromTop(8.f);
      g->AttachControl(new ITextControl(r.ReduceFromLeft(110.f), label, rowLabel), kNoTag, group);
      return r;
    };
    auto add = [&](IControl* c, int tag = kNoTag) { g->AttachControl(c, tag, group); };
    auto knob = [&](const IRECT& r, int i, int param, const char* label) { add(new IVKnobControl(cell(r, i), param, label, style)); };

    // ---- VOICES
    for (int o = 0; o < 2; o++)
    {
      IRECT r = row(o == 0 ? "OSC A" : "OSC B", 90.f);
      const int base = o == 0 ? kParamAPosition : kParamBPosition;
      add(new TableButton(cell(r, 0, 2).GetMidVPadded(20.f), style, [this] { return TableMenuItems(); }, [this, o](int i) { ChooseTable(o, i); }),
          o == 0 ? kCtrlTagTableA : kCtrlTagTableB);
      knob(r, 2, base, "Position");
      knob(r, 3, base + 1, "Octave");
      knob(r, 4, base + 2, "Semi");
      knob(r, 5, base + 3, "Fine");
      knob(r, 6, base + 4, "Level");
    }
    IRECT r = row("FILTER", 90.f);
    add(new IVTabSwitchControl(cell(r, 0, 2).GetMidVPadded(20.f), kParamFilterType, {"LP", "BP", "HP"}, "", style));
    knob(r, 2, kParamCutoff, "Cutoff");
    knob(r, 3, kParamResonance, "Resonance");
    knob(r, 4, kParamKeyTrack, "Key Track");
    knob(r, 5, kParamFilterEnvAmount, "Env Amount");
    knob(r, 6, kParamFilterVelocity, "Velocity");

    // Two envelopes side by side, each under its own heading: FILTER (dials 1-4), AMP (5-8).
    r = row("ENVELOPES", 106.f);
    {
      const IRECT heads = r.ReduceFromTop(16.f);
      const IText head(14.f, kAccent, "Roboto-Regular", EAlign::Center);
      add(new ITextControl(cell(heads, 0, 4), "FILTER", head));
      add(new ITextControl(cell(heads, 4, 4), "AMP", head));
      const IRECT whole = cell(r, 0, 8).GetVPadded(16.f);
      const float x = cell(r, 3).R + 5.f; // between the two groups
      add(new IPanelControl(IRECT(x - 0.5f, whole.T, x + 0.5f, whole.B), kTextDim.WithOpacity(0.5f)));
      const char* names[4] = {"Attack", "Decay", "Sustain", "Release"};
      for (int k = 0; k < 4; k++)
      {
        knob(r, k, kParamFAttack + k, names[k]);
        knob(r, 4 + k, kParamAAttack + k, names[k]);
      }
    }

    r = row("PLAYING", 90.f);
    add(new IVTabSwitchControl(cell(r, 0, 2).GetMidVPadded(20.f), kParamPlayers, {"1", "2", "3", "4", "5", "6"}, "", style));
    knob(r, 2, kParamAmpVelocity, "Amp Velocity");
    add(new IVMenuButtonControl(cell(r, 3, 2).GetMidVPadded(20.f), kParamWheelTarget, "Mod wheel", style));
    knob(r, 5, kParamWheelAmount, "Amount");
    add(new IVMenuButtonControl(cell(r, 6).GetMidVPadded(20.f), kParamTouchTarget, "Aftertouch", style));
    knob(r, 7, kParamTouchAmount, "Amount");

    // ---- EFFECTS
    page = pageArea;
    group = kPages[1];
    r = row("ENSEMBLE", 90.f);
    knob(r, 0, kParamLooseness, "Looseness");
    knob(r, 1, kParamSmear, "Smear");
    knob(r, 2, kParamSettle, "Settle");
    knob(r, 3, kParamVibrato, "Vibrato");
    knob(r, 4, kParamOnset, "Onset");
    r = row("CHARACTER", 90.f);
    knob(r, 0, kParamCharPitch, "Pitch");
    knob(r, 1, kParamCharPosition, "Position");
    knob(r, 2, kParamCharCutoff, "Cutoff");
    add(new ITextControl(cell(r, 3, 5), "each player's own fixed offsets, set as the note starts", IText(14.f, kTextDim, "Roboto-Regular", EAlign::Near)));
    r = row("DRIFT", 90.f);
    knob(r, 0, kParamDriftPitch, "Pitch");
    knob(r, 1, kParamDriftPosition, "Position");
    knob(r, 2, kParamDriftCutoff, "Cutoff");
    add(new ITextControl(cell(r, 3, 5), "each player wandering slowly while the note holds", IText(14.f, kTextDim, "Roboto-Regular", EAlign::Near)));
    r = row("COLOUR", 90.f);
    knob(r, 0, kParamNoiseAmount, "Noise");
    knob(r, 1, kParamNoiseTone, "Noise Tone");
    add(new IVMenuButtonControl(cell(r, 2, 2).GetMidVPadded(20.f), kParamBody, "Body", style));
    knob(r, 4, kParamBodyDepth, "Body Depth");
    knob(r, 5, kParamWarmth, "Warmth");

    // ---- ROOM: the drawing large on the right, the controls on the left
    page = pageArea;
    group = kPages[2];
    IRECT view = page.ReduceFromRight(560.f);
    page.ReduceFromRight(12.f);
    add(new IPanelControl(view, IColor(255, 30, 28, 25)));
    add(new roombleed_ui::RoomView(view.GetPadded(-8.f), [this] { return MakeRoomView(); }), kCtrlTagRoomView);
    auto cell4 = [&](const IRECT& rr, int i, int span = 1) { return cell(rr, i, span, 4); };
    r = row("ROOM", 90.f);
    add(new IVMenuButtonControl(cell4(r, 0, 2).GetMidVPadded(20.f), kParamRoom, "", style));
    add(new IVKnobControl(cell4(r, 2), kParamSize, "Size", style));
    add(new IVKnobControl(cell4(r, 3), kParamSurfaces, "Surfaces", style));
    r = row("PLACE", 90.f);
    add(new IVKnobControl(cell4(r, 0), kParamDistance, "Distance", style));
    add(new IVKnobControl(cell4(r, 1), kParamRoomAmount, "Close / Room", style));
    add(new IVKnobControl(cell4(r, 2), kParamWidth, "Width", style));
    r = row("MICS", 90.f);
    add(new IVMenuButtonControl(cell4(r, 0, 2).GetMidVPadded(20.f), kParamMic, "", style));
    add(new IVTabSwitchControl(cell4(r, 2, 2).GetMidVPadded(20.f), kParamPair, {"XY", "ORTF", "Spaced"}, "", style));

    for (int p = 1; p < 3; p++)
      g->ForControlInGroup(kPages[p], [](IControl* c) { c->Hide(true); });
  };
#endif
}

Section::~Section() = default;

// ---- Tables (main thread) -------------------------------------------------------------------

void Section::SetMessage(const std::string& msg, bool sticky)
{
  mMessage = msg;
  mMessageSticky = sticky;
  mMessageAt = Clock::now();
}

std::vector<std::string> Section::TableMenuItems() const
{
  std::vector<std::string> items;
  for (int i = 0; i < wt::NumFactoryTables(); i++)
    items.push_back(wt::FactoryTableName(i));
  for (const auto& l : mLoaded)
    items.push_back(l.source.name);
  items.push_back("Load...");
  return items;
}

void Section::ChooseTable(int osc, int i)
{
  if (i < wt::NumFactoryTables())
    UseTableSource(osc, TableSource{i, "", 0, wt::FactoryTableName(i)});
  else if (i < wt::NumFactoryTables() + (int)mLoaded.size())
    UseTableSource(osc, mLoaded[(size_t)(i - wt::NumFactoryTables())].source);
  else
    LoadTableDialog(osc);
}

void Section::LoadTableDialog(int osc)
{
#if IPLUG_EDITOR
  if (!GetUI())
    return;
  mDialogFile.Set("");
  GetUI()->PromptForFile(mDialogFile, mDialogPath, EFileAction::Open, "wav", [this, osc](const WDL_String& file, const WDL_String&) {
    if (file.GetLength())
      LoadTableFile(osc, file.Get());
  });
#endif
}

// The user's file: decoded (to check it's a table), copied into the tables folder by content,
// then played from the copy.
bool Section::LoadTableFile(int osc, const std::string& path)
{
  const std::vector<uint8_t> bytes = ReadBytes(path);
  if (bytes.empty())
  {
    SetMessage("can't read " + path, true);
    return false;
  }
  const std::string name = std::filesystem::u8path(path).stem().u8string();
  wt::DecodedWavetable d = wt::DecodeWavetable(bytes.data(), bytes.size(), name);
  if (!d.table)
  {
    SetMessage("can't load " + name + ": " + d.error, true);
    return false;
  }
  const uint64_t hash = Fnv1a(bytes);
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::u8path(mTapeDir), ec);
  const std::filesystem::path copy = std::filesystem::u8path(mTapeDir) / CopyName(hash);
  if (!std::filesystem::exists(copy, ec))
  {
    std::ofstream f(copy, std::ios::binary);
    f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    if (!f)
    {
      SetMessage("couldn't keep a copy of " + name + " in " + mTapeDir, true);
      return false;
    }
  }
  TableSource source{-1, copy.u8string(), hash, name};
  bool have = false;
  for (const auto& l : mLoaded)
    have = have || l.source.hash == hash;
  if (!have)
    mLoaded.push_back({source, std::move(d.table)});
  UseTableSource(osc, source);
  SetMessage(std::string("loaded ") + name + " into oscillator " + (osc == 0 ? "A" : "B"));
  return true;
}

// Plays a factory table, or a stored copy (already loaded, at its path, or found in the
// tables folder by its hash).
bool Section::UseTableSource(int osc, const TableSource& source)
{
  const wt::WavetableData* table = nullptr;
  if (source.factory >= 0)
    table = mFactory[(size_t)std::clamp(source.factory, 0, wt::NumFactoryTables() - 1)].get();
  else
  {
    for (const auto& l : mLoaded)
      if (l.source.hash == source.hash)
        table = l.table.get();
    if (!table)
    {
      std::vector<uint8_t> bytes = ReadBytes(source.path);
      if (bytes.empty() || Fnv1a(bytes) != source.hash)
        bytes = ReadBytes((std::filesystem::u8path(mTapeDir) / CopyName(source.hash)).u8string());
      if (bytes.empty() || Fnv1a(bytes) != source.hash)
      {
        SetMessage("table not found: " + source.name + " (oscillator " + (osc == 0 ? "A" : "B") + " stays on " + mSource[osc].name + ")", true);
        return false;
      }
      wt::DecodedWavetable d = wt::DecodeWavetable(bytes.data(), bytes.size(), source.name);
      if (!d.table)
        return false;
      table = d.table.get();
      mLoaded.push_back({source, std::move(d.table)});
    }
  }
  mSource[osc] = source;
  mOscTable[osc].store(table);
  RetireUnusedTables();
  return true;
}

// Keeps at most kMaxLoaded loaded tables: the oldest one neither oscillator plays is handed
// to the audio thread to let go of, then freed in OnIdle.
void Section::RetireUnusedTables()
{
  if ((int)mLoaded.size() <= kMaxLoaded || mRetireRequest.load() || mRetired.load())
    return;
  for (const auto& l : mLoaded)
    if (l.source.hash != mSource[0].hash && l.source.hash != mSource[1].hash)
    {
      mRetireRequest.store(l.table.get());
      return;
    }
}

// ---- Room (main thread) ---------------------------------------------------------------------

double Section::DistanceMetres() const
{
  const double maxD = underheard::room::MaxDistance(CurrentShape(GetParam(kParamRoom), GetParam(kParamSize), GetParam(kParamSurfaces)), 0.5, kSeatY, 90.);
  return 0.5 * std::pow(std::max(maxD, 0.51) / 0.5, GetParam(kParamDistance)->Value() / 100.);
}

void Section::ApplyTemplate(int index)
{
  if (index != 0)
    return;
  // The only template is the defaults (a string section).
  for (int i = 0; i < kNumParams; i++)
  {
    const double norm = GetParam(i)->ToNormalized(GetParam(i)->GetDefault());
    BeginInformHostOfParamChangeFromUI(i);
    SendParameterValueFromUI(i, norm);
    EndInformHostOfParamChangeFromUI(i);
#if IPLUG_EDITOR
    if (GetUI())
      GetUI()->ForControlWithParam(i, [norm](IControl* c) { c->SetValueFromDelegate(norm); });
#endif
  }
  for (int o = 0; o < 2; o++)
    UseTableSource(o, TableSource{kDefaultTable[o], "", 0, wt::FactoryTableName(kDefaultTable[o])});
}

bool Section::RoomSettingsChanged()
{
  std::vector<double> now = {GetSampleRate()};
  for (int p : {kParamRoom, kParamSize, kParamSurfaces, kParamDistance, kParamMic, kParamPair})
    now.push_back(GetParam(p)->Value());
  if (now == mBuiltSettings)
    return false;
  mBuiltSettings = now;
  return true;
}

// The section's room: one impulse per desk (each from its own seat), all heard by the same pair
// of mics standing out in the room facing them.
void Section::RebuildRoom()
{
  using namespace underheard::room;
  RoomSettingsChanged();
  mRebuildPending = false;
  const double fs = GetSampleRate() > 0. ? GetSampleRate() : 48000.;
  const RoomShape shape = CurrentShape(GetParam(kParamRoom), GetParam(kParamSize), GetParam(kParamSurfaces));
  const double distance = DistanceMetres();
  const Placement centre = PlaceMics(shape, 0.5, kSeatY, 90., distance, 0., true, GetParam(kParamPair)->Int());
  const MicModel& mic = MicModels()[(size_t)GetParam(kParamMic)->Int()];
  const MicVariant& variant = mic.MultiPattern() ? mic.Variant(MicPattern::Cardioid) : mic.variants[0];
  const std::vector<float> micFilter = MicFilter(variant.response, fs);

  std::vector<std::vector<float>> all[kNumDesks];
  double energy = 0.;
  RoomIR last;
  for (int d = 0; d < kNumDesks; d++)
  {
    RoomSpec spec;
    spec.shape = shape;
    spec.source = {std::clamp(kSeatX[d] * shape.width, 0.3, shape.width - 0.3), centre.source.y, centre.source.z};
    spec.mics = centre.mics;
    spec.tailCorrelation = centre.tailCorrelation;
    spec.pickup.a = variant.a;
    spec.pickup.proximity = mic.proximity;
    spec.pickup.roomGain = mic.kind == MicKind::Contact ? 0.06 : 1.;
    spec.sampleRate = fs;
    spec.seed = 11u + (uint32_t)d;
    last = GenerateRoom(spec);
    all[d] = std::move(last.channels);
    double e = 0.;
    for (auto& ch : all[d])
    {
      ApplyFilter(ch, micFilter);
      double ce = 0.;
      for (float v : ch)
        ce += (double)v * v;
      e = std::max(e, ce);
    }
    energy += e / kNumDesks;
  }
  // One gain for all four (so their balance stays as the room makes it): about unit energy.
  const float g = energy > 0. ? (float)(1. / std::sqrt(energy)) : 1.f;
  for (int d = 0; d < kNumDesks; d++)
  {
    for (auto& ch : all[d])
      for (float& v : ch)
        v *= g;
    if (auto c = Convolver::Make(all[d], fs))
      mDeskRooms[d].Offer(std::move(c));
  }
  char info[192];
  std::snprintf(info, sizeof info, "%s   %.1f x %.1f m   decay %.1f s   %s pair (%s) at %.1f m", RoomPresets()[(size_t)GetParam(kParamRoom)->Int()].name,
                shape.width, shape.depth, last.rt60[3], mic.name, GetParam(kParamPair)->GetDisplayText(GetParam(kParamPair)->Int()), distance);
  mInfo = info;
}

#if IPLUG_EDITOR
roombleed_ui::RoomViewData Section::MakeRoomView() const
{
  using namespace underheard::room;
  roombleed_ui::RoomViewData d;
  const RoomShape shape = CurrentShape(GetParam(kParamRoom), GetParam(kParamSize), GetParam(kParamSurfaces));
  const Placement p = PlaceMics(shape, 0.5, kSeatY, 90., DistanceMetres(), 0., true, GetParam(kParamPair)->Int());
  d.width = shape.width;
  d.depth = shape.depth;
  d.sx = p.source.x;
  d.sy = p.source.y;
  d.distance = DistanceMetres();
  d.maxDistance = p.maxDistance;
  d.dirx = 0.;
  d.diry = 1.;
  d.cax = 0.;
  d.cay = -1.;
  d.numMics = (int)p.mics.size();
  for (int m = 0; m < d.numMics && m < 2; m++)
    d.mics[m] = {p.mics[(size_t)m].position.x, p.mics[(size_t)m].position.y, p.mics[(size_t)m].aim.x, p.mics[(size_t)m].aim.y};
  const MicModel& mic = MicModels()[(size_t)GetParam(kParamMic)->Int()];
  d.pattern = (mic.MultiPattern() ? mic.Variant(MicPattern::Cardioid) : mic.variants[0]).a[3];
  for (int w = 0; w < 4; w++)
  {
    double mean = 0.;
    for (double a : shape.surfaces[(size_t)w]->alpha)
      mean += a * shape.surfaceScale / kBands;
    d.hardness[w] = 1. - std::clamp(mean * 3., 0., 1.);
  }
  for (int k = 0; k < kNumDesks; k++)
    d.seats.push_back({std::clamp(kSeatX[k] * shape.width, 0.3, shape.width - 0.3), p.source.y, DeskName(k)});
  char title[128];
  std::snprintf(title, sizeof title, "%s   %.1f x %.1f m   the section and its mics", RoomPresets()[(size_t)GetParam(kParamRoom)->Int()].name, shape.width,
                shape.depth);
  d.title = title;
  return d;
}
#endif

void Section::UpdateStatus()
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui)
    return;
  if (!mMessage.empty() && !mMessageSticky && Clock::now() - mMessageAt > std::chrono::seconds(8))
    mMessage.clear();
  auto setText = [ui](int tag, const char* text) {
    if (auto* c = static_cast<ITextControl*>(ui->GetControlWithTag(tag)))
      if (strcmp(c->GetStr(), text) != 0)
        c->SetStr(text);
  };
  setText(kCtrlTagInfo, mInfo.c_str());
  setText(kCtrlTagStatus, mMessage.c_str());
  for (int o = 0; o < 2; o++)
    if (auto* b = dynamic_cast<IVButtonControl*>(ui->GetControlWithTag(o == 0 ? kCtrlTagTableA : kCtrlTagTableB)))
      if (std::string(b->GetLabelStr()) != mSource[o].name)
      {
        b->SetLabelStr(mSource[o].name.c_str());
        b->SetDirty(false);
      }
  if (IControl* view = ui->GetControlWithTag(kCtrlTagRoomView))
    if (!view->IsHidden())
      view->SetDirty(false);
#endif
}

void Section::OnIdle()
{
  mOutputPeakSender.TransmitData(*this);
  for (auto& r : mDeskRooms)
    r.TakeRetired();
  // A table the audio thread has let go of: free it.
  if (const auto* t = mRetired.exchange(nullptr))
  {
    mLoaded.erase(std::remove_if(mLoaded.begin(), mLoaded.end(), [t](const LoadedTable& l) { return l.table.get() == t; }), mLoaded.end());
    RetireUnusedTables();
  }
  if (RoomSettingsChanged())
  {
    mSettingsChangedAt = Clock::now();
    mRebuildPending = true;
  }
  if (mRebuildPending && Clock::now() - mSettingsChangedAt > std::chrono::milliseconds(60))
    RebuildRoom();
  UpdateStatus();
}

// ---- State ----------------------------------------------------------------------------------

// Version 2: magic, version, parameter count, the parameters, then each oscillator's table
// (factory index or -1, the copy's path, its hash, its name).
bool Section::SerializeState(IByteChunk& chunk) const
{
  chunk.Put(&kStateMagic);
  chunk.Put(&kStateVersion);
  const int32_t n = kNumParams;
  chunk.Put(&n);
  for (int i = 0; i < kNumParams; i++)
  {
    const double v = GetParam(i)->Value();
    chunk.Put(&v);
  }
  for (const TableSource& s : mSource)
  {
    const int32_t factory = s.factory;
    const uint64_t hash = s.hash;
    chunk.Put(&factory);
    chunk.PutStr(s.path.c_str());
    chunk.Put(&hash);
    chunk.PutStr(s.name.c_str());
  }
  return true;
}

int Section::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int32_t magic = 0, version = 0, n = 0;
  int p = chunk.Get(&magic, startPos);
  if (p < 0 || magic != kStateMagic)
    return UnserializeParams(chunk, startPos);
  p = chunk.Get(&version, p);
  if (p >= 0) p = chunk.Get(&n, p);
  if (p < 0 || n < 0)
    return -1;
  if (version < 2)
  {
    // Before the redesign: a different instrument. Skip it and start from the defaults.
    for (int i = 0; i < n && p >= 0; i++)
    {
      double v = 0.;
      p = chunk.Get(&v, p);
    }
    SetMessage("a Section project from before the redesign: starting from the defaults", true);
    return p;
  }
  ENTER_PARAMS_MUTEX
  for (int i = 0; i < n && p >= 0; i++)
  {
    double v = 0.;
    p = chunk.Get(&v, p);
    if (p >= 0 && i < kNumParams)
      GetParam(i)->Set(v);
  }
  OnParamReset(kPresetRecall);
  LEAVE_PARAMS_MUTEX
  for (int o = 0; o < 2 && p >= 0; o++)
  {
    TableSource s;
    int32_t factory = 0;
    uint64_t hash = 0;
    WDL_String path, name;
    p = chunk.Get(&factory, p);
    if (p >= 0) p = chunk.GetStr(path, p);
    if (p >= 0) p = chunk.Get(&hash, p);
    if (p >= 0) p = chunk.GetStr(name, p);
    if (p < 0)
      break;
    s.factory = factory;
    s.path = path.Get();
    s.hash = hash;
    s.name = name.Get();
    UseTableSource(o, s);
  }
  return p;
}

// ---- Audio thread ---------------------------------------------------------------------------

#if IPLUG_DSP
void Section::OnReset()
{
  const double fs = GetSampleRate();
  mOutputPeakSender.Reset(fs);
  const int block = std::max(GetBlockSize(), 64);
  mEngine.Prepare(fs, block);
  for (auto& d : mDesk)
    d.assign((size_t)block, 0.f);
  for (auto* v : {&mRoomL, &mRoomR, &mTmpL, &mTmpR})
    v->assign((size_t)block, 0.f);
  mWarmth.Prepare(fs);
  mSmoothCoef = 1. - std::exp(-1. / (0.02 * fs));
  mRoomSmoothed = GetParam(kParamRoomAmount)->Value() / 100.;
  mMidiQueue.Resize(1024);
  mMidiQueue.Clear();
  mRebuildPending = true; // the rooms have to match the host's sample rate
}

void Section::ProcessMidiMsg(const IMidiMsg& msg) { mMidiQueue.Add(msg); }

void Section::HandleMidi(const IMidiMsg& msg)
{
  Settings& s = mEngine.Set();
  switch (msg.StatusMsg())
  {
    case IMidiMsg::kNoteOn: mEngine.NoteOn(msg.NoteNumber(), msg.Velocity()); break;
    case IMidiMsg::kNoteOff: mEngine.NoteOff(msg.NoteNumber()); break;
    case IMidiMsg::kChannelAftertouch: s.aftertouch.value = msg.ChannelAfterTouch() / 127.; break;
    case IMidiMsg::kPitchWheel: s.bend = 2. * msg.PitchWheel(); break;
    case IMidiMsg::kControlChange:
    {
      const auto cc = msg.ControlChangeIdx();
      if (cc == IMidiMsg::kSustainOnOff)
        mEngine.Sustain(msg.ControlChange(cc) >= 0.5);
      else if (cc == IMidiMsg::kModWheel)
        s.modWheel.value = msg.ControlChange(cc);
      else if (cc == IMidiMsg::kAllNotesOff || (int)cc == 120 /* all sound off */)
        mEngine.AllNotesOff();
      break;
    }
    default: break;
  }
}

void Section::ApplySettings()
{
  Settings& s = mEngine.Set();
  s.players = GetParam(kParamPlayers)->Int();
  for (int o = 0; o < 2; o++)
  {
    const int base = o == 0 ? kParamAPosition : kParamBPosition;
    Oscillator& osc = s.osc[o];
    osc.table = mOscTable[o].load();
    osc.position = GetParam(base)->Value() / 100.;
    osc.octave = GetParam(base + 1)->Int();
    osc.semi = GetParam(base + 2)->Int();
    osc.fine = GetParam(base + 3)->Value();
    osc.level = GetParam(base + 4)->Value() / 100.;
  }
  s.filterType = GetParam(kParamFilterType)->Int();
  s.cutoff = GetParam(kParamCutoff)->Value();
  s.resonance = GetParam(kParamResonance)->Value() / 100.;
  s.keyTrack = GetParam(kParamKeyTrack)->Value() / 100.;
  s.envAmount = GetParam(kParamFilterEnvAmount)->Value();
  s.velocity = GetParam(kParamFilterVelocity)->Value() / 100.;
  s.filterEnv = {GetParam(kParamFAttack)->Value() / 1000., GetParam(kParamFDecay)->Value() / 1000., GetParam(kParamFSustain)->Value() / 100.,
                 GetParam(kParamFRelease)->Value() / 1000.};
  s.ampEnv = {GetParam(kParamAAttack)->Value() / 1000., GetParam(kParamADecay)->Value() / 1000., GetParam(kParamASustain)->Value() / 100.,
              GetParam(kParamARelease)->Value() / 1000.};
  s.ampVelocity = GetParam(kParamAmpVelocity)->Value() / 100.;
  s.modWheel.target = GetParam(kParamWheelTarget)->Int();
  s.modWheel.amount = GetParam(kParamWheelAmount)->Value() / 100.;
  s.aftertouch.target = GetParam(kParamTouchTarget)->Int();
  s.aftertouch.amount = GetParam(kParamTouchAmount)->Value() / 100.;
  s.looseness = GetParam(kParamLooseness)->Value() / 100.;
  s.smear = GetParam(kParamSmear)->Value() / 100.;
  s.settle = GetParam(kParamSettle)->Value() / 100.;
  s.vibrato = GetParam(kParamVibrato)->Value() / 100.;
  s.onset = GetParam(kParamOnset)->Value() / 1000.;
  s.character[0] = GetParam(kParamCharPitch)->Value() / 100.;
  s.character[1] = GetParam(kParamCharPosition)->Value() / 100.;
  s.character[2] = GetParam(kParamCharCutoff)->Value() / 100.;
  s.drift[0] = GetParam(kParamDriftPitch)->Value() / 100.;
  s.drift[1] = GetParam(kParamDriftPosition)->Value() / 100.;
  s.drift[2] = GetParam(kParamDriftCutoff)->Value() / 100.;
  s.noiseAmount = GetParam(kParamNoiseAmount)->Value() / 100.;
  s.noiseTone = GetParam(kParamNoiseTone)->Value() / 100.;
  s.body = GetParam(kParamBody)->Int();
  s.bodyDepth = GetParam(kParamBodyDepth)->Value() / 100.;
}

void Section::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  (void)inputs;
  // A table the main thread wants to free: let go of it, then say so.
  if (const auto* t = mRetireRequest.exchange(nullptr))
  {
    mEngine.ReleaseTable(t);
    mRetired.store(t);
  }
  const double gain = GetParam(kParamOutput)->DBToAmp();
  const double roomTarget = GetParam(kParamRoomAmount)->Value() / 100., width = GetParam(kParamWidth)->Value() / 100.;
  mWarmth.SetAmount(GetParam(kParamWarmth)->Value() / 100.);
  bool haveRoom = true;
  for (auto& r : mDeskRooms)
    haveRoom = haveRoom && r.HasConvolver();
  const int nOut = NOutChansConnected();

  for (int pos = 0; pos < nFrames;)
  {
    // MIDI due by now, then render up to the next event (or the buffer's size).
    while (!mMidiQueue.Empty() && mMidiQueue.Peek().mOffset <= pos)
    {
      HandleMidi(mMidiQueue.Peek());
      mMidiQueue.Remove();
    }
    int end = std::min(nFrames, pos + (int)mTmpL.size());
    if (!mMidiQueue.Empty())
      end = std::min(end, std::max(pos + 1, mMidiQueue.Peek().mOffset));
    const int n = end - pos;
    ApplySettings();
    float* desks[kNumDesks];
    for (int d = 0; d < kNumDesks; d++)
    {
      std::fill(mDesk[d].begin(), mDesk[d].begin() + n, 0.f);
      desks[d] = mDesk[d].data();
    }
    mEngine.Process(desks, n);

    // Through the room: each desk from its seat, heard by the mics.
    std::fill(mRoomL.begin(), mRoomL.begin() + n, 0.f);
    std::fill(mRoomR.begin(), mRoomR.begin() + n, 0.f);
    for (int d = 0; d < kNumDesks; d++)
    {
      mDeskRooms[d].Process(mDesk[d].data(), mTmpL.data(), mTmpR.data(), n);
      for (int i = 0; i < n; i++)
      {
        mRoomL[(size_t)i] += mTmpL[(size_t)i];
        mRoomR[(size_t)i] += mTmpR[(size_t)i];
      }
    }
    for (int i = 0; i < n; i++)
    {
      // Close: each desk panned to its seat.
      double cl = 0., cr = 0.;
      for (int d = 0; d < kNumDesks; d++)
      {
        const double a = (kClosePan[d] * width + 1.) * 0.25 * 3.14159265358979;
        cl += mDesk[d][(size_t)i] * std::cos(a);
        cr += mDesk[d][(size_t)i] * std::sin(a);
      }
      mRoomSmoothed += (roomTarget - mRoomSmoothed) * mSmoothCoef;
      const double a = (haveRoom ? mRoomSmoothed : 0.) * 3.14159265358979 / 2.;
      float l = (float)(cl * std::cos(a) + mRoomL[(size_t)i] * std::sin(a));
      float r = (float)(cr * std::cos(a) + mRoomR[(size_t)i] * std::sin(a));
      mWarmth.Process(l, r);
      const double o[2] = {SoftLimit(l * gain), SoftLimit(r * gain)};
      for (int c = 0; c < nOut; c++)
        outputs[c][pos + i] = o[std::min(c, 1)];
    }
    pos = end;
  }
  mMidiQueue.Flush(nFrames);
  mOutputPeakSender.ProcessBlock(outputs, nFrames, kCtrlTagOutputMeter, nOut, 0);
}
#endif // IPLUG_DSP
