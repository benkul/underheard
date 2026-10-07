// Offline tests for libs/room MicModel: the filters match the published curves, proximity
// follows theory, and the phone and cassette auto-gain behave.
#include "MicModel.h"

#include <cmath>
#include <cstdio>

using namespace underheard::room;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

int main()
{
  printf("-- each mic's filter matches its published curve (100 Hz to 15 kHz)\n");
  for (const MicModel& m : MicModels())
    for (const MicVariant& v : m.variants)
    {
      const std::vector<float> fir = MicFilter(v.response, 48000.);
      const double ref = FilterGainDb(fir, 1000., 48000.);
      double worst = 0., energyEarly = 0., energy = 0.;
      for (const ResponsePoint& p : v.response)
        if (p.hz >= 100. && p.hz <= 15000.)
          worst = std::max(worst, std::fabs(FilterGainDb(fir, p.hz, 48000.) - ref - (p.db - ResponseDb(v.response, 1000.))));
      for (size_t i = 0; i < fir.size(); i++)
      {
        energy += (double)fir[i] * fir[i];
        if (i < 96) energyEarly += (double)fir[i] * fir[i];
      }
      CHECK(worst < 1.5 && std::fabs(ref) < 0.5 && energyEarly / energy > 0.9, "%s (%d): worst %.2f dB off, 1 kHz at %+.2f dB, %.0f%% of energy in the first 2 ms",
            m.name, (int)v.pattern, worst, ref, 100. * energyEarly / energy);
    }

  printf("\n-- proximity effect\n");
  {
    // Royer's chart for a figure-8: about +6 dB at 100 Hz at 1 ft. The 125 Hz band at 1 ft:
    const double db = 20. * std::log10(ProximityFactor(0, 0.305));
    CHECK(db > 4. && db < 6., "figure-8 at 1 ft: %+.1f dB in the 125 Hz band (theory about +4.9)", db);
    CHECK(20. * std::log10(ProximityFactor(0, 3.0)) < 0.2, "and nothing at 3 m");
    CHECK(20. * std::log10(ProximityFactor(5, 0.3)) < 0.01, "or at 4 kHz");
  }

  printf("\n-- the phone's auto-gain and the cassette's ALC\n");
  for (const char* name : {"Phone (iPhone, voice processing)", "Portable cassette recorder"})
  {
    const MicModel* m = nullptr;
    for (const MicModel& x : MicModels()) if (std::string(x.name) == name) m = &x;
    MicPost post;
    post.Prepare(48000.);
    post.Configure(*m, m->variants[0]);
    std::vector<float> l(480), r(480);
    double phase = 0.;
    auto run = [&](double amp, double secs) {
      double peak = 0.;
      for (int b = 0; b < (int)(secs * 100.); b++)
      {
        for (int i = 0; i < 480; i++) { l[(size_t)i] = r[(size_t)i] = (float)(amp * std::sin(phase)); phase += 2. * 3.14159265 * 300. / 48000.; }
        post.Process(l.data(), r.data(), 480);
        for (float v : l) peak = std::max(peak, (double)std::fabs(v));
      }
      return peak;
    };
    run(0.01, 4.);
    const double quietGain = post.AutoGainDb();
    run(0.7, 0.05);
    const double hitGain = post.AutoGainDb();
    run(0.01, 1.);
    const double afterHit = post.AutoGainDb();
    run(0.01, 8.);
    const double recovered = post.AutoGainDb();
    CHECK(quietGain > 10., "%s: brings a quiet input up (%+.1f dB)", name, quietGain);
    CHECK(hitGain < quietGain - 6., "  pulls back on a loud hit (%+.1f dB)", hitGain);
    CHECK(afterHit < quietGain - 3. && recovered > afterHit + 3., "  and recovers slowly afterwards (%+.1f dB after 1 s, %+.1f dB later): the breathing", afterHit, recovered);
  }

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
