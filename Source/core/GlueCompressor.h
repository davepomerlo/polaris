#pragma once
// 3-band glue compressor. Linkwitz-Riley 4th-order crossovers (bands sum back flat),
// stereo-linked soft-knee feed-forward detection per band, auto makeup.
#include "Biquad.h"
#include <array>

namespace polaris
{
class GlueCompressor
{
public:
    static constexpr int kBands = 3;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        updateCrossovers();
        updateTimes();
        reset();
    }

    void reset()
    {
        for (auto& c : ch)
        {
            c.lowLP.reset(); c.restHP.reset(); c.midLP.reset(); c.highHP.reset(); c.lowAP.reset();
        }
        env.fill (0.0);
    }

    void setCrossovers (float lowHz, float highHz)
    {
        highHz = std::max (highHz, lowHz * 2.0f);
        if (lowHz != xLow || highHz != xHigh) { xLow = lowHz; xHigh = highHz; updateCrossovers(); }
    }

    void setThresholdDb (float t) { threshold = t; }
    void setRatio (float r)       { ratio = std::max (1.0f, r); }
    void setAttackMs (float a)    { if (a != attackMs)  { attackMs = a;  updateTimes(); } }
    void setReleaseMs (float r)   { if (r != releaseMs) { releaseMs = r; updateTimes(); } }
    void setEnabled (bool e)      { enabled = e; }

    float getGainReductionDb() const { return grDb; }

    /** Auto makeup: restores part of the gain lost at the threshold. */
    float makeupDb() const { return -threshold * (1.0f - 1.0f / ratio) * 0.35f; }

    /** Static gain computer with a 6 dB soft knee. Returns gain reduction in dB (>= 0). */
    static double gainReduction (double levelDb, double thr, double r, double knee = 6.0)
    {
        const double over = levelDb - thr;
        if (2.0 * over <= -knee) return 0.0;
        if (2.0 * over >= knee)  return over * (1.0 - 1.0 / r);
        const double k = over + knee / 2.0;
        return (1.0 - 1.0 / r) * k * k / (2.0 * knee);
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (! enabled) { grDb = 0.0f; return; }

        const double makeup = dbToGain (makeupDb());
        double maxGr = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            std::array<std::array<float, kBands>, 2> band {};
            const float in[2] = { left[i], right[i] };

            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                const float low  = s.lowAP.process (s.lowLP.process (in[c]));
                const float rest = s.restHP.process (in[c]);
                band[(size_t) c][0] = low;
                band[(size_t) c][1] = s.midLP.process (rest);
                band[(size_t) c][2] = s.highHP.process (rest);
            }

            float outL = 0, outR = 0;
            for (int b = 0; b < kBands; ++b)
            {
                const double peak = std::max (std::abs (band[0][(size_t) b]), std::abs (band[1][(size_t) b]));
                const double target = gainReduction (gainToDb (peak), threshold, ratio);
                double& e = env[(size_t) b];
                e = target > e ? target + (e - target) * attackCoef
                               : target + (e - target) * releaseCoef;
                maxGr = std::max (maxGr, e);
                const double g = dbToGain (-e) * makeup;
                outL += (float) (band[0][(size_t) b] * g);
                outR += (float) (band[1][(size_t) b] * g);
            }

            left[i] = outL;
            right[i] = outR;
        }

        grDb = (float) -maxGr;
    }

private:
    struct Channel { LR4 lowLP, restHP, midLP, highHP; LR4Allpass lowAP; };

    void updateCrossovers()
    {
        for (auto& c : ch)
        {
            c.lowLP.setLowpass (fs, xLow);
            c.restHP.setHighpass (fs, xLow);
            c.midLP.setLowpass (fs, xHigh);
            c.highHP.setHighpass (fs, xHigh);
            c.lowAP.set (fs, xHigh);
        }
    }

    void updateTimes()
    {
        attackCoef  = std::exp (-1.0 / (0.001 * std::max (0.05f, attackMs) * fs));
        releaseCoef = std::exp (-1.0 / (0.001 * std::max (1.0f, releaseMs) * fs));
    }

    double fs = 48000.0;
    float xLow = 120.0f, xHigh = 3000.0f;
    float threshold = -12.0f, ratio = 2.0f, attackMs = 30.0f, releaseMs = 200.0f;
    bool enabled = true;
    double attackCoef = 0.0, releaseCoef = 0.0;
    std::array<Channel, 2> ch {};
    std::array<double, kBands> env {};
    float grDb = 0.0f;
};
} // namespace polaris
