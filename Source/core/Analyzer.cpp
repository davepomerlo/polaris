#include "Analyzer.h"
#include <algorithm>
#include <cstring>

namespace polaris
{
std::vector<float> ListenAnalyzer::bandCenters()
{
    std::vector<float> c;
    for (int k = 0;; ++k)
    {
        const double f = 31.5 * std::pow (2.0, k / 6.0);
        if (f > 16100.0) break;
        c.push_back ((float) f);
    }
    return c;
}

void ListenAnalyzer::prepare (double sampleRate)
{
    fs = sampleRate;
    meter.prepare (fs);
    const auto lp = BiquadCoeffs::lowpass (fs, 150.0);
    lowL1.c = lowL2.c = lowR1.c = lowR2.c = lp;
    const int n = fft.size();
    midBuf.assign ((size_t) n, 0.0f);
    sideBuf.assign ((size_t) n, 0.0f);
    midPow.assign ((size_t) n / 2 + 1, 0.0);
    sidePow.assign ((size_t) n / 2 + 1, 0.0);
    reset();
}

void ListenAnalyzer::reset()
{
    meter.reset();
    tpL.reset(); tpR.reset();
    truePeak = samplePeak = 0.0f;
    sumSq = 0; count = 0;
    lowL1.reset(); lowL2.reset(); lowR1.reset(); lowR2.reset();
    lowLR = lowLL = lowRR = 0;
    clipRunL = clipRunR = clipEvents = 0;
    dcSum = 0;
    bufPos = 0;
    std::fill (midPow.begin(), midPow.end(), 0.0);
    std::fill (sidePow.begin(), sidePow.end(), 0.0);
    frames = 0;
    shortTermMax = -100.0f;
}

void ListenAnalyzer::process (const float* left, const float* right, int numSamples)
{
    meter.process (left, right, numSamples);
    shortTermMax = std::max (shortTermMax, meter.shortTermLufs());

    const int n = fft.size();
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];

        truePeak = std::max (truePeak, std::max (tpL.process (l), tpR.process (r)));
        samplePeak = std::max (samplePeak, std::max (std::abs (l), std::abs (r)));
        sumSq += 0.5 * ((double) l * l + (double) r * r);
        dcSum += 0.5 * ((double) l + (double) r);
        ++count;

        const double ll = lowL2.process (lowL1.process (l));
        const double lr = lowR2.process (lowR1.process (r));
        lowLR += ll * lr; lowLL += ll * ll; lowRR += lr * lr;

        auto clip = [this] (float x, int& run)
        {
            if (std::abs (x) >= 0.999f) { if (++run == 3) ++clipEvents; }
            else run = 0;
        };
        clip (l, clipRunL);
        clip (r, clipRunR);

        midBuf[(size_t) bufPos] = 0.5f * (l + r);
        sideBuf[(size_t) bufPos] = 0.5f * (l - r);
        if (++bufPos == n)
        {
            processFrame();
            // 50% overlap: keep the second half
            std::memmove (midBuf.data(), midBuf.data() + n / 2, sizeof (float) * (size_t) (n / 2));
            std::memmove (sideBuf.data(), sideBuf.data() + n / 2, sizeof (float) * (size_t) (n / 2));
            bufPos = n / 2;
        }
    }
}

void ListenAnalyzer::processFrame()
{
    const int n = fft.size();
    double ms = 0;
    for (int i = 0; i < n; ++i) ms += (double) midBuf[(size_t) i] * midBuf[(size_t) i];
    if (ms / n < 1.0e-6) return;   // skip silence (below about -60 dBFS)

    fft.powerSpectrum (midBuf.data(), power);
    for (size_t k = 0; k < power.size(); ++k) midPow[k] += power[k];
    fft.powerSpectrum (sideBuf.data(), power);
    for (size_t k = 0; k < power.size(); ++k) sidePow[k] += power[k];
    ++frames;
}

AnalysisResult ListenAnalyzer::finish()
{
    AnalysisResult r;
    meter.refresh();

    r.seconds = seconds();
    r.valid = frames > 0 && count > 0;
    r.integratedLufs = meter.integratedLufs();
    r.shortTermMaxLufs = shortTermMax;
    r.loudnessRange = meter.loudnessRange();
    r.truePeakDb = (float) gainToDb (truePeak);
    r.samplePeakDb = (float) gainToDb (samplePeak);
    r.plrDb = r.truePeakDb - r.integratedLufs;
    const double rms = count > 0 ? std::sqrt (sumSq / (double) count) : 0.0;
    r.crestDb = r.samplePeakDb - (float) gainToDb (rms);
    r.clipEvents = clipEvents;
    r.dcOffset = count > 0 ? (float) (dcSum / (double) count) : 0.0f;
    r.lowCorrelation = (lowLL > 1e-9 && lowRR > 1e-9) ? (float) (lowLR / std::sqrt (lowLL * lowRR)) : 1.0f;

    if (! r.valid) return r;

    const int n = fft.size();
    const double df = fs / n;
    const int nyq = n / 2;

    auto ratioDb = [&] (double lo, double hi)
    {
        double m = 0, s = 0;
        for (int k = std::max (1, (int) std::ceil (lo / df)); k <= std::min (nyq, (int) std::floor (hi / df)); ++k)
        { m += midPow[(size_t) k]; s += sidePow[(size_t) k]; }
        return (float) (m > 1e-20 ? std::max (-60.0, 10.0 * std::log10 (std::max (s, 1e-20) / m)) : -60.0);
    };
    r.sideToMidLowDb = ratioDb (20.0, 150.0);
    r.sideToMidMidDb = ratioDb (150.0, 2000.0);
    r.sideToMidHighDb = ratioDb (2000.0, std::min (16000.0, fs * 0.45));

    r.bandCentersHz = bandCenters();
    r.bandLevelsDb.reserve (r.bandCentersHz.size());
    for (float fc : r.bandCentersHz)
    {
        const double lo = fc * std::pow (2.0, -1.0 / 12.0), hi = fc * std::pow (2.0, 1.0 / 12.0);
        int k0 = (int) std::ceil (lo / df), k1 = std::min (nyq, (int) std::floor (hi / df));
        if (k1 < k0) k0 = k1 = std::clamp ((int) std::lround (fc / df), 1, nyq);
        double sum = 0;
        for (int k = k0; k <= k1; ++k) sum += midPow[(size_t) k];
        const double mean = sum / (double) (k1 - k0 + 1) / (double) frames;
        r.bandLevelsDb.push_back ((float) (10.0 * std::log10 (std::max (mean, 1e-20))));
    }
    return r;
}
} // namespace polaris
