#include "Assistant.h"
#include "ParamIds.h"
#include "GlueCompressor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace polaris
{
namespace
{
std::string fmt (const char* f, double a)                     { char b[128]; std::snprintf (b, sizeof b, f, a); return b; }
std::string fmt (const char* f, double a, double c)           { char b[160]; std::snprintf (b, sizeof b, f, a, c); return b; }
std::string fmt (const char* f, double a, double c, double d) { char b[200]; std::snprintf (b, sizeof b, f, a, c, d); return b; }

std::string hz (double f)
{
    return f >= 1000.0 ? fmt (f >= 10000.0 ? "%.0f kHz" : "%.1f kHz", f / 1000.0) : fmt ("%.0f Hz", f);
}

double gauss (double f, double centre, double widthOct)
{
    const double o = std::log2 (f / centre) / widthOct;
    return std::exp (-0.5 * o * o);
}

std::string regionName (double f, bool tooMuch)
{
    if (f < 60)    return tooMuch ? "Sub bass is heavy"        : "Sub bass is light";
    if (f < 150)   return tooMuch ? "Bass is boomy"            : "Bass is thin";
    if (f < 500)   return tooMuch ? "Mud in the low mids"      : "Low mids lack body";
    if (f < 1200)  return tooMuch ? "Boxy mids"                : "Mids are scooped";
    if (f < 4000)  return tooMuch ? "Harsh upper mids"         : "Lacks presence";
    if (f < 9000)  return tooMuch ? "Bright / sibilant"        : "Dull top end";
    return tooMuch ? "Too much air / hiss" : "Lacks air";
}

struct Region { int start, end, peak; float dev; double importance; };
} // namespace

std::vector<float> targetCurve (Style style, const std::vector<float>& centers)
{
    std::vector<float> t;
    t.reserve (centers.size());

    // Starting targets, expressed as mean power per FFT bin (pink noise = -3 dB/oct).
    // These are heuristics to be tuned by ear against real reference masters.
    double slope = -4.5, subCorner = 40.0, bump = 0.0, bumpHz = 60.0;
    switch (style)
    {
        case Style::Pop:       slope = -4.5; subCorner = 40.0; bump = 1.0; bumpHz = 70.0; break;
        case Style::HipHopEdm: slope = -5.0; subCorner = 30.0; bump = 3.0; bumpHz = 55.0; break;
        case Style::Rock:      slope = -4.2; subCorner = 55.0; bump = 0.0; break;
        case Style::Acoustic:  slope = -4.0; subCorner = 70.0; bump = -1.0; bumpHz = 90.0; break;
        case Style::Reference: break;
    }

    for (float fc : centers)
    {
        const double f = fc;
        double db = slope * std::log2 (f / 1000.0);
        if (f < subCorner) db -= 12.0 * std::log2 (subCorner / f);   // natural sub roll-off
        if (f > 12000.0)   db -= 6.0 * std::log2 (f / 12000.0);      // natural top roll-off
        db += bump * gauss (f, bumpHz, 0.7);
        t.push_back ((float) db);
    }
    return t;
}

std::vector<float> spectralDeviation (const AnalysisResult& a, const std::vector<float>& target)
{
    const size_t n = std::min (a.bandLevelsDb.size(), target.size());
    std::vector<float> dev (n, 0.0f);
    if (n == 0) return dev;

    double sum = 0; int cnt = 0;
    for (size_t k = 0; k < n; ++k)
    {
        dev[k] = a.bandLevelsDb[k] - target[k];
        if (a.bandCentersHz[k] >= 100.0f && a.bandCentersHz[k] <= 10000.0f) { sum += dev[k]; ++cnt; }
    }
    const float offset = cnt > 0 ? (float) (sum / cnt) : 0.0f;

    // remove level offset, then smooth over +-1/6 octave
    std::vector<float> raw (n);
    for (size_t k = 0; k < n; ++k) raw[k] = dev[k] - offset;
    for (size_t k = 0; k < n; ++k)
    {
        const size_t lo = k > 0 ? k - 1 : k, hi = std::min (n - 1, k + 1);
        float s = 0; for (size_t j = lo; j <= hi; ++j) s += raw[j];
        dev[k] = s / (float) (hi - lo + 1);
    }
    return dev;
}

std::vector<Suggestion> makeSuggestions (const AnalysisResult& a, const AssistantSettings& s)
{
    std::vector<Suggestion> out;

    if (! a.valid || a.integratedLufs < -60.0f)
    {
        out.push_back ({ Suggestion::Kind::Warning, "Nothing to analyse",
                         "Press Listen while the song is playing (the loudest 20-30 seconds work best).", {} });
        return out;
    }

    // ---------- warnings ----------
    if (a.seconds < 8.0)
        out.push_back ({ Suggestion::Kind::Warning, "Short listen",
                         fmt ("Only %.0f s analysed; results may be rough. 20-30 s of the chorus is ideal.", a.seconds), {} });

    if (a.clipEvents > 0 || a.samplePeakDb > -0.05f)
        out.push_back ({ Suggestion::Kind::Warning, "Mix clips before Polaris",
                         fmt ("%.0f clipping events found. Pull your master fader or the loudest tracks down about 3 dB, then listen again.", (double) a.clipEvents), {} });

    if (std::abs (a.dcOffset) > 0.002f)
        out.push_back ({ Suggestion::Kind::Warning, "DC offset",
                         fmt ("The mix has a DC offset of %.3f. Check for a faulty sample or plugin; a low-cut at 20 Hz fixes it.", a.dcOffset), {} });

    // ---------- tonal balance -> EQ ----------
    Style style = s.style;
    std::vector<float> target;
    if (style == Style::Reference)
    {
        if (s.referenceCurve.size() == a.bandLevelsDb.size()) target = s.referenceCurve;
        else
        {
            out.push_back ({ Suggestion::Kind::Warning, "No reference learned yet",
                             "Play a reference track through Polaris and press Learn reference. Using the Pop target for now.", {} });
            style = Style::Pop;
        }
    }
    if (target.empty()) target = targetCurve (style, a.bandCentersHz);

    const auto dev = spectralDeviation (a, target);
    const auto& fc = a.bandCentersHz;
    const float threshold = 2.0f;

    std::vector<Region> regions;
    for (size_t k = 0; k < dev.size();)
    {
        if (std::abs (dev[k]) < threshold) { ++k; continue; }
        const bool positive = dev[k] > 0;
        Region r { (int) k, (int) k, (int) k, dev[k], 0.0 };
        while (k < dev.size() && std::abs (dev[k]) >= threshold && (dev[k] > 0) == positive)
        {
            if (std::abs (dev[k]) > std::abs (r.dev)) { r.dev = dev[k]; r.peak = (int) k; }
            r.end = (int) k;
            ++k;
        }
        r.importance = std::abs (r.dev) * std::sqrt ((double) (r.end - r.start + 1));
        regions.push_back (r);
    }
    std::sort (regions.begin(), regions.end(), [] (const Region& x, const Region& y) { return x.importance > y.importance; });

    bool lowShelfUsed = false, highShelfUsed = false;
    int nextPeak = 1;                                   // EQ bands 2..5 (index 1..4) are peaks
    const int last = (int) dev.size() - 1;

    for (const auto& r : regions)
    {
        const double fPeak = fc[(size_t) r.peak];
        const float gain = -std::clamp (0.6f * r.dev, -4.0f, 4.0f);
        if (std::abs (gain) < 1.0f) continue;

        const bool tooMuch = r.dev > 0;
        int band = -1;
        double freq = fPeak, q = 1.0;

        if (! lowShelfUsed && r.start <= 2 && fPeak < 90.0)
        {
            band = 0; lowShelfUsed = true;
            freq = std::clamp ((double) fc[(size_t) r.end] * std::pow (2.0, 1.0 / 12.0), 30.0, 250.0);
            q = 0.7;
        }
        else if (! highShelfUsed && r.end >= last - 2 && fPeak > 9000.0)
        {
            band = 5; highShelfUsed = true;
            freq = std::clamp ((double) fc[(size_t) r.start] * std::pow (2.0, -1.0 / 12.0), 4000.0, 16000.0);
            q = 0.7;
        }
        else if (nextPeak <= 4)
        {
            band = nextPeak++;
            const double bwOct = std::max (1.0, (double) (r.end - r.start + 1)) / 6.0;
            const double p = std::pow (2.0, bwOct);
            q = std::clamp (std::sqrt (p) / (p - 1.0), 0.5, 3.0);
        }
        if (band < 0) continue;

        Suggestion sug;
        sug.kind = Suggestion::Kind::Eq;
        sug.title = regionName (fPeak, tooMuch);
        sug.detail = fmt ("%+.1f dB vs target around ", r.dev) + hz (fPeak)
                   + (gain < 0 ? " -> cut " : " -> boost ") + fmt ("%.1f dB", std::abs (gain))
                   + (band == 0 ? " (low shelf)" : band == 5 ? " (high shelf)" : fmt (", Q %.1f", q));
        if (tooMuch && fPeak >= 4000.0 && fPeak < 9000.0)
            sug.detail += ". If it only happens on S sounds, a de-esser is the better fix.";
        sug.changes = { { ids::eqFreq (band), (float) freq },
                        { ids::eqGain (band), gain },
                        { ids::eqQ (band), (float) q } };
        out.push_back (sug);
    }

    // ---------- dynamics -> glue compressor ----------
    float inputTrim = 0.0f;
    if (a.integratedLufs > s.targetLufs + 0.5f)
        inputTrim = std::max (-24.0f, s.targetLufs - a.integratedLufs - 1.0f);

    const float level = a.integratedLufs + inputTrim;
    float thrOffset, ratio, attack = 30.0f, release = 200.0f;
    std::string why;
    if (a.plrDb > 14.0f)      { thrOffset = 4.0f; ratio = 2.5f; why = "Very dynamic mix"; }
    else if (a.plrDb > 10.0f) { thrOffset = 6.0f; ratio = 2.0f; why = "Normal dynamics"; }
    else                      { thrOffset = 8.0f; ratio = 1.5f; why = "Already dense mix"; }
    if (style == Style::HipHopEdm) { attack = 15.0f; release = 120.0f; }
    if (style == Style::Acoustic)  { ratio = std::max (1.3f, ratio - 0.5f); attack = 40.0f; release = 300.0f; }

    const float thr = std::clamp (level + thrOffset, -40.0f, 0.0f);
    out.push_back ({ Suggestion::Kind::Dynamics, why + " -> gentle glue",
                     fmt ("Peak-to-loudness ratio is %.1f dB. Glue threshold %.0f dB, ratio %.1f:1", a.plrDb, thr, ratio)
                       + fmt (", attack %.0f ms, release %.0f ms.", attack, release),
                     { { ids::glueThreshold, thr }, { ids::glueRatio, ratio },
                       { ids::glueAttack, attack }, { ids::glueRelease, release } } });

    // ---------- loudness -> limiter ----------
    GlueCompressor g;
    g.setThresholdDb (thr); g.setRatio (ratio);
    const float makeup = g.makeupDb();
    const float driveGain = 6.0f * std::pow (std::clamp (s.drive, 0.0f, 1.0f), 1.2f);
    const float needed = s.targetLufs - (a.integratedLufs + inputTrim);
    const float limGain = std::clamp (needed - makeup - driveGain, 0.0f, 24.0f);

    Suggestion loud;
    loud.kind = Suggestion::Kind::Loudness;
    loud.title = fmt ("Hit %.0f LUFS", s.targetLufs);
    loud.detail = fmt ("Mix measures %.1f LUFS integrated. Limiter gain %.1f dB", a.integratedLufs, limGain)
                + fmt (", ceiling %.1f dBTP.", s.ceilingDb);
    loud.changes = { { ids::limGain, limGain }, { ids::limCeiling, s.ceilingDb } };
    if (inputTrim < 0.0f)
    {
        loud.detail += fmt (" The mix is already louder than the target, so input is turned down %.1f dB.", -inputTrim);
        loud.changes.push_back ({ ids::inputGain, inputTrim });
    }
    out.push_back (loud);

    if (limGain > 10.0f)
        out.push_back ({ Suggestion::Kind::Warning, "Heavy limiting needed",
                         fmt ("Reaching the target takes about %.0f dB of limiting, which can flatten punch. "
                              "Try a lower target, or fix the loudest peaks (often the kick or snare) in the mix.", limGain), {} });

    // ---------- stereo ----------
    if (a.lowCorrelation < 0.6f)
        out.push_back ({ Suggestion::Kind::Stereo, "Wide or phasey bass",
                         fmt ("Bass correlation is %.2f below 150 Hz; it may vanish on mono speakers. Mono the bass below 120 Hz.", a.lowCorrelation),
                         { { ids::monoFreq, 120.0f } } });
    else if (a.sideToMidLowDb > -15.0f)
        out.push_back ({ Suggestion::Kind::Stereo, "Some stereo bass",
                         fmt ("Side energy below 150 Hz is %.0f dB under the mid. Mono below 80 Hz tightens the low end.", -a.sideToMidLowDb),
                         { { ids::monoFreq, 80.0f } } });

    if (a.sideToMidHighDb < -18.0f)
        out.push_back ({ Suggestion::Kind::Stereo, "Narrow top end",
                         fmt ("Side energy above 2 kHz is %.0f dB under the mid. Width 120%% opens it up.", -a.sideToMidHighDb),
                         { { ids::width, 120.0f } } });
    else if (a.sideToMidHighDb > -4.0f)
        out.push_back ({ Suggestion::Kind::Stereo, "Very wide top end",
                         fmt ("Side energy above 2 kHz is only %.0f dB under the mid. Width 90%% keeps it mono-safe.", -a.sideToMidHighDb),
                         { { ids::width, 90.0f } } });

    return out;
}

std::string summarise (const AnalysisResult& a)
{
    if (! a.valid) return "No analysis yet";
    return fmt ("%.1f LUFS integrated  |  true peak %.1f dBTP  |  PLR %.1f dB", a.integratedLufs, a.truePeakDb, a.plrDb)
         + fmt ("  |  LRA %.1f LU  |  bass correlation %.2f", a.loudnessRange, a.lowCorrelation);
}
} // namespace polaris
