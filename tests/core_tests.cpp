// Unit tests for the JUCE-free DSP core. Build with -DPOLARIS_CORE_ONLY=ON or as part of the main build.
#include "../Source/core/MasterChain.h"
#include "../Source/core/LoudnessMeter.h"
#include "../Source/core/Analyzer.h"
#include "../Source/core/Assistant.h"
#include "../Source/core/ParamIds.h"
#include "../Source/core/FFT.h"
#include "../Source/core/ParamHistory.h"
#include <cstdio>
#include <random>
#include <vector>

using namespace polaris;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static std::vector<float> sine (double fs, double freq, double amp, double seconds, double phase = 0.0)
{
    std::vector<float> v ((size_t) (fs * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2.0 * kPi * freq * (double) i / fs + phase));
    return v;
}

static void testLoudness()
{
    std::printf ("loudness meter\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        LoudnessMeter m; m.prepare (fs);
        auto s = sine (fs, 997.0, dbToGain (-20.0), 10.0);
        m.process (s.data(), s.data(), (int) s.size());
        m.refresh();
        CHECK (std::abs (m.integratedLufs() - (-20.0f)) < 0.15f, "fs %.0f: integrated %.2f, expected -20.0", fs, m.integratedLufs());
        CHECK (std::abs (m.shortTermLufs() - (-20.0f)) < 0.15f, "fs %.0f: short-term %.2f", fs, m.shortTermLufs());
        CHECK (m.loudnessRange() < 0.5f, "steady tone LRA %.2f should be ~0", m.loudnessRange());
    }
    // one channel only: -3.01 LU lower
    LoudnessMeter m; m.prepare (48000.0);
    auto s = sine (48000.0, 997.0, 1.0, 5.0);
    std::vector<float> z (s.size(), 0.0f);
    m.process (s.data(), z.data(), (int) s.size());
    m.refresh();
    CHECK (std::abs (m.integratedLufs() - (-3.01f)) < 0.15f, "0 dBFS one channel: %.2f, expected -3.01", m.integratedLufs());
}

static void testTruePeak()
{
    std::printf ("true peak\n");
    // fs/4 sine at 45 degrees: every sample is +-0.707 but the true peak is 1.0 (+3 dB inter-sample)
    TruePeakDetector tp;
    float peak = 0, samplePeak = 0;
    auto s = sine (48000.0, 12000.0, 1.0, 0.1, kPi / 4.0);
    for (float x : s) { peak = std::max (peak, tp.process (x)); samplePeak = std::max (samplePeak, std::abs (x)); }
    CHECK (samplePeak < 0.72f, "sample peak %.3f", samplePeak);
    CHECK (std::abs (peak - 1.0f) < 0.05f, "true peak %.3f, expected ~1.0", peak);
}

static void testLimiter()
{
    std::printf ("limiter\n");
    const double fs = 48000.0;
    LookaheadLimiter lim; lim.prepare (fs);
    lim.setCeilingDb (-1.0f); lim.setReleaseMs (80.0f); lim.setInputGainDb (12.0f);
    lim.reset();

    std::mt19937 rng (1);
    std::normal_distribution<float> nd (0.0f, 0.25f);
    std::vector<float> l ((size_t) fs * 3), r ((size_t) fs * 3);
    for (size_t i = 0; i < l.size(); ++i)
    {
        const float hit = (i % 12000 < 200) ? 0.9f * (float) std::sin (2 * kPi * 60 * (double) i / fs) : 0.0f; // kick-like bursts
        l[i] = nd (rng) + hit; r[i] = nd (rng) * 0.8f + hit;
    }
    for (size_t pos = 0; pos < l.size(); pos += 333)                  // odd block sizes, like FL Studio can send
    {
        const int n = (int) std::min<size_t> (333, l.size() - pos);
        lim.process (l.data() + pos, r.data() + pos, n);
    }

    const float ceiling = (float) dbToGain (-1.0);
    float maxSample = 0, maxTp = 0;
    TruePeakDetector tl, tr;
    for (size_t i = 0; i < l.size(); ++i)
    {
        maxSample = std::max (maxSample, std::max (std::abs (l[i]), std::abs (r[i])));
        maxTp = std::max (maxTp, std::max (tl.process (l[i]), tr.process (r[i])));
    }
    CHECK (maxSample <= ceiling + 1e-6f, "sample peak %.4f exceeds ceiling %.4f", maxSample, ceiling);
    CHECK (gainToDb (maxTp) < -0.7, "true peak %.2f dBTP exceeds -1 dBTP by too much", gainToDb (maxTp));
    CHECK (lim.getGainReductionDb() < -1.0f, "expected gain reduction, got %.2f", lim.getGainReductionDb());

    // latency: an impulse below the ceiling comes out exactly getLatency() samples later
    LookaheadLimiter lim2; lim2.prepare (fs); lim2.setInputGainDb (0.0f); lim2.reset();
    std::vector<float> a (512, 0.0f), b (512, 0.0f);
    a[10] = b[10] = 0.5f;
    lim2.process (a.data(), b.data(), 512);
    int found = -1;
    for (int i = 0; i < 512; ++i) if (std::abs (a[(size_t) i] - 0.5f) < 1e-4f) found = i;
    CHECK (found == 10 + lim2.getLatency(), "impulse at %d, expected %d", found, 10 + lim2.getLatency());
}

static double responseDbAt (const std::vector<float>& ir, double fs, double freq)
{
    std::complex<double> acc = 0;
    for (size_t i = 0; i < ir.size(); ++i) acc += (double) ir[i] * std::polar (1.0, -2.0 * kPi * freq * (double) i / fs);
    return 20.0 * std::log10 (std::abs (acc));
}

static void testGlueFlat()
{
    std::printf ("glue compressor crossovers\n");
    const double fs = 48000.0;
    GlueCompressor g; g.prepare (fs);
    g.setThresholdDb (0.0f); g.setRatio (1.0f);           // no compression, no makeup
    std::vector<float> l (16384, 0.0f), r (16384, 0.0f);
    l[0] = r[0] = 0.25f;
    g.process (l.data(), r.data(), (int) l.size());
    for (double f : { 30.0, 120.0, 500.0, 3000.0, 10000.0 })
    {
        const double db = responseDbAt (l, fs, f) - 20.0 * std::log10 (0.25);
        CHECK (std::abs (db) < 0.1, "band sum not flat at %.0f Hz: %.2f dB", f, db);
    }

    GlueCompressor c; c.prepare (fs);
    c.setThresholdDb (-20.0f); c.setRatio (4.0f); c.setAttackMs (5.0f);
    auto s = sine (fs, 1000.0, 0.9, 1.0);
    auto s2 = s;
    c.process (s.data(), s2.data(), (int) s.size());
    CHECK (c.getGainReductionDb() < -6.0f, "loud tone should be compressed, GR %.2f", c.getGainReductionDb());
}

static void testStereo()
{
    std::printf ("stereo tool\n");
    const double fs = 48000.0;
    StereoTool st; st.prepare (fs); st.setMonoFrequency (120.0f);
    auto l = sine (fs, 40.0, 0.5, 2.0);
    std::vector<float> r (l.size());
    for (size_t i = 0; i < l.size(); ++i) r[i] = -l[i];   // pure side signal at 40 Hz
    st.process (l.data(), r.data(), (int) l.size());
    float maxSide = 0;
    for (size_t i = l.size() / 2; i < l.size(); ++i) maxSide = std::max (maxSide, std::abs (0.5f * (l[i] - r[i])));
    CHECK (gainToDb (maxSide / 0.5) < -20.0, "40 Hz side only %.1f dB down", gainToDb (maxSide / 0.5));

    StereoTool w; w.prepare (fs); w.setWidth (0.0f);
    auto a = sine (fs, 1000.0, 0.5, 0.1), b = sine (fs, 1000.0, 0.2, 0.1);
    w.process (a.data(), b.data(), (int) a.size());
    float diff = 0; for (size_t i = 0; i < a.size(); ++i) diff = std::max (diff, std::abs (a[i] - b[i]));
    CHECK (diff < 1e-6f, "width 0 should be mono, diff %.6f", diff);
}

static void testSaturator()
{
    std::printf ("saturator\n");
    const double fs = 48000.0;
    for (int mode = 0; mode < 3; ++mode)
    {
        Saturator s; s.prepare (fs); s.setMode ((Saturator::Mode) mode); s.setDriveDb (12.0f); s.reset();
        auto l = sine (fs, 200.0, dbToGain (-50.0), 1.0), r = l;
        s.process (l.data(), r.data(), (int) l.size());
        double in = 0, out = 0;
        auto ref = sine (fs, 200.0, dbToGain (-50.0), 1.0);
        for (size_t i = l.size() / 2; i < l.size(); ++i) { in += (double) ref[i] * ref[i]; out += (double) l[i] * l[i]; }
        const double db = 10.0 * std::log10 (out / in);
        CHECK (std::abs (db) < 0.3, "mode %d quiet-signal gain %.2f dB, expected ~0", mode, db);

        auto big = sine (fs, 100.0, 4.0, 0.5), big2 = big;
        s.process (big.data(), big2.data(), (int) big.size());
        bool finite = true; float pk = 0;
        for (float x : big) { finite &= std::isfinite (x); pk = std::max (pk, std::abs (x)); }
        CHECK (finite, "mode %d produced NaN/inf", mode);
        CHECK (pk < 4.0f, "mode %d should reduce peaks, peak %.2f", mode, pk);
    }
}

static void testChain()
{
    std::printf ("full chain\n");
    const double fs = 44100.0;
    MasterChain chain; chain.prepare (fs);
    ChainSettings s;
    s.drive = 1.0f; s.character = 2; s.monoFreq = 100.0f; s.limCeilingDb = -1.0f;
    chain.setSettings (s);
    chain.reset();

    std::mt19937 rng (7);
    std::normal_distribution<float> nd (0.0f, 0.2f);
    std::vector<float> l ((size_t) fs * 2), r ((size_t) fs * 2);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = nd (rng); r[i] = nd (rng); }
    for (size_t pos = 0; pos < l.size(); pos += 517)
        chain.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (517, l.size() - pos));

    bool finite = true; float pk = 0;
    for (size_t i = 0; i < l.size(); ++i) { finite &= std::isfinite (l[i]) && std::isfinite (r[i]); pk = std::max (pk, std::max (std::abs (l[i]), std::abs (r[i]))); }
    CHECK (finite, "chain produced NaN/inf");
    CHECK (pk <= (float) dbToGain (-1.0) + 1e-6f, "chain output %.3f above ceiling", pk);
    CHECK (chain.getLatency() > 0, "latency should be reported");
}

static void testAnalyzer()
{
    std::printf ("analyzer\n");
    const double fs = 48000.0;
    ListenAnalyzer an; an.prepare (fs);
    auto l = sine (fs, 1000.0, 0.5, 6.0);
    an.process (l.data(), l.data(), (int) l.size());
    auto res = an.finish();
    CHECK (res.valid, "analysis should be valid");
    size_t best = 0;
    for (size_t k = 0; k < res.bandLevelsDb.size(); ++k) if (res.bandLevelsDb[k] > res.bandLevelsDb[best]) best = k;
    CHECK (std::abs (res.bandCentersHz[best] - 1000.0f) < 100.0f, "peak band %.0f Hz, expected ~1000", res.bandCentersHz[best]);
    CHECK (res.lowCorrelation > 0.99f || res.lowCorrelation == 1.0f, "identical channels correlation %.2f", res.lowCorrelation);
    CHECK (res.sideToMidHighDb <= -59.0f, "mono signal side/mid %.1f", res.sideToMidHighDb);
    CHECK (std::abs (res.truePeakDb - gainToDb (0.5)) < 0.2, "true peak %.2f", res.truePeakDb);
}

static void testAssistant()
{
    std::printf ("assistant\n");
    AnalysisResult a;
    a.valid = true; a.seconds = 25.0;
    a.integratedLufs = -18.0f; a.truePeakDb = -3.0f; a.samplePeakDb = -3.2f; a.plrDb = 15.0f;
    a.lowCorrelation = 0.4f; a.sideToMidLowDb = -10.0f; a.sideToMidHighDb = -12.0f;
    a.bandCentersHz = ListenAnalyzer::bandCenters();
    a.bandLevelsDb = targetCurve (Style::Pop, a.bandCentersHz);
    for (size_t k = 0; k < a.bandCentersHz.size(); ++k)   // add mud: +6 dB bump around 300 Hz
    {
        const double o = std::log2 (a.bandCentersHz[k] / 300.0) / 0.35;
        a.bandLevelsDb[k] += (float) (6.0 * std::exp (-0.5 * o * o)) - 30.0f;   // level offset must not matter
    }

    AssistantSettings s; s.style = Style::Pop; s.targetLufs = -14.0f; s.ceilingDb = -1.0f;
    const auto sug = makeSuggestions (a, s);

    bool mudCut = false, limiter = false, mono = false, glue = false;
    for (const auto& x : sug)
    {
        std::printf ("    [%d] %s: %s\n", (int) x.kind, x.title.c_str(), x.detail.c_str());
        for (const auto& c : x.changes)
        {
            if (c.paramId.find ("Gain") != std::string::npos && c.paramId.rfind ("eq", 0) == 0 && c.value < -1.0f)
            {
                for (const auto& c2 : x.changes)
                    if (c2.paramId.find ("Freq") != std::string::npos && c2.value > 180.0f && c2.value < 500.0f) mudCut = true;
            }
            if (c.paramId == ids::limGain && c.value > 1.0f) limiter = true;
            if (c.paramId == ids::monoFreq) mono = true;
            if (c.paramId == ids::glueThreshold) glue = true;
        }
    }
    CHECK (mudCut, "expected an EQ cut between 180 and 500 Hz");
    CHECK (limiter, "expected a limiter gain suggestion");
    CHECK (mono, "expected a mono-bass suggestion for correlation 0.4");
    CHECK (glue, "expected a glue suggestion");

    // flat-to-target mix should produce no EQ suggestions
    AnalysisResult flat = a;
    flat.bandLevelsDb = targetCurve (Style::Pop, a.bandCentersHz);
    int eqCount = 0;
    for (const auto& x : makeSuggestions (flat, s)) if (x.kind == Suggestion::Kind::Eq) ++eqCount;
    CHECK (eqCount == 0, "on-target mix produced %d EQ suggestions", eqCount);
}

static void testHistory()
{
    std::printf ("undo history\n");
    auto snap = [] (float a, float b, int slot = 0) { return StateSnapshot { { a, b }, slot }; };

    HistoryRecorder rec;
    rec.settleMs = 300.0;
    rec.reset (snap (0.5f, 0.5f), 0.0);
    CHECK (! rec.history.canUndo(), "fresh history should have nothing to undo");

    // a knob drag: many small changes, then it settles -> exactly one undo step
    double t = 0.0;
    for (int i = 1; i <= 20; ++i) { t += 33.0; rec.tick (snap (0.5f + 0.01f * (float) i, 0.5f), t); }
    CHECK (rec.history.size() == 1, "nothing should commit while the knob is moving (%zu steps)", rec.history.size());
    bool committed = false;
    for (int i = 0; i < 15; ++i) { t += 33.0; committed |= rec.tick (snap (0.7f, 0.5f), t); }
    CHECK (committed && rec.history.size() == 2, "drag should become one step (%zu)", rec.history.size());

    // "Apply all": two parameters change in the same tick -> one step
    t += 33.0; rec.tick (snap (0.7f, 0.9f), t);
    for (int i = 0; i < 15; ++i) { t += 33.0; rec.tick (snap (0.7f, 0.9f), t); }
    CHECK (rec.history.size() == 3, "apply-all should be one step (%zu)", rec.history.size());

    auto u = rec.history.undo();
    CHECK (u.sameAs (snap (0.7f, 0.5f)), "undo should restore the state after the drag");
    rec.markApplied (u, t);
    for (int i = 0; i < 15; ++i) { t += 33.0; rec.tick (u, t); }
    CHECK (rec.history.canRedo(), "applying an undo must not wipe the redo step");
    auto r = rec.history.redo();
    CHECK (r.sameAs (snap (0.7f, 0.9f)), "redo should bring apply-all back");

    // new edit after undo drops the redo branch
    rec.history.undo();
    rec.history.commit (snap (0.1f, 0.1f));
    CHECK (! rec.history.canRedo(), "a new edit should clear redo");

    // quick edit then undo: flushPending makes undo revert that edit
    HistoryRecorder q; q.reset (snap (0.2f, 0.2f), 0.0);
    q.tick (snap (0.3f, 0.2f), 10.0);
    q.flushPending();
    CHECK (q.history.canUndo() && q.history.undo().sameAs (snap (0.2f, 0.2f)), "flushPending should allow undoing an unsettled edit");

    // slot switches count as changes; size cap holds
    CHECK (! snap (0.1f, 0.1f, 0).sameAs (snap (0.1f, 0.1f, 1)), "different slot means different state");
    ParamHistory capped (5);
    capped.reset (snap (0, 0));
    for (int i = 1; i <= 20; ++i) capped.commit (snap ((float) i, 0));
    CHECK (capped.size() == 5, "history should be capped at 5 (%zu)", capped.size());
}

int main()
{
    testHistory();
    testLoudness();
    testTruePeak();
    testLimiter();
    testGlueFlat();
    testStereo();
    testSaturator();
    testChain();
    testAnalyzer();
    testAssistant();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
