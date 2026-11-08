#pragma once
// Transparent lookahead limiter with true-peak detection.
// Gain path: true-peak -> required gain -> min-hold over lookahead -> box average over lookahead -> release.
// The hold + average pair guarantees the gain has fully reached its target when the peak arrives.
// Real-time safe after prepare().
#include "TruePeak.h"
#include <vector>
#include <cstdint>

namespace polaris
{
class LookaheadLimiter
{
public:
    void prepare (double sampleRate, double lookaheadMs = 1.5)
    {
        fs = sampleRate;
        L = std::max (2, (int) std::lround (lookaheadMs * 0.001 * fs));
        latency = L - 1 + TruePeakDetector::kDelay;

        delayL.assign ((size_t) latency + 1, 0.0f);
        delayR.assign ((size_t) latency + 1, 0.0f);
        holdVal.assign ((size_t) L + 2, 1.0f);
        holdIdx.assign ((size_t) L + 2, 0);
        avgBuf.assign ((size_t) L, 1.0f);

        inGain.prepare (fs, 20.0, dbToGain (inputGainDb));
        setReleaseMs (releaseMs);
        reset();
    }

    void reset()
    {
        tpL.reset(); tpR.reset();
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        std::fill (avgBuf.begin(), avgBuf.end(), 1.0f);
        avgSum = (double) L;
        avgPos = 0;
        dPos = 0;
        head = tail = 0;
        sampleIndex = 0;
        gain = 1.0;
        inGain.snap();
    }

    void setInputGainDb (float db)  { inputGainDb = db; inGain.setTarget (dbToGain (db)); }
    void setCeilingDb (float db)    { ceiling = (float) dbToGain (db); }
    void setReleaseMs (float ms)    { releaseMs = ms; releaseCoef = 1.0 - std::exp (-1.0 / (0.001 * ms * fs)); }
    void setEnabled (bool e)        { enabled = e; }

    int getLatency() const          { return latency; }
    float getGainReductionDb() const { return grDb; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        double minGain = 1.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = enabled ? (float) inGain.next() : 1.0f;
            const float xl = left[i] * g;
            const float xr = right[i] * g;

            // Audio delay line (always runs so latency stays constant when bypassed).
            delayL[(size_t) dPos] = xl;
            delayR[(size_t) dPos] = xr;
            dPos = (dPos + 1) % (int) delayL.size();
            const float dl = delayL[(size_t) dPos];
            const float dr = delayR[(size_t) dPos];

            if (! enabled)
            {
                left[i] = dl; right[i] = dr;
                continue;
            }

            // 1. required gain from true peak (relates to the sample kDelay steps ago)
            const float tp = std::max (tpL.process (xl), tpR.process (xr));
            const float req = tp > ceiling ? ceiling / tp : 1.0f;

            // 2. sliding minimum over the last L samples (monotonic deque in a ring buffer)
            const int cap = (int) holdVal.size();
            while (head != tail && holdVal[(size_t) ((tail - 1 + cap) % cap)] >= req)
                tail = (tail - 1 + cap) % cap;
            holdVal[(size_t) tail] = req;
            holdIdx[(size_t) tail] = sampleIndex;
            tail = (tail + 1) % cap;
            while (holdIdx[(size_t) head] <= sampleIndex - L)
                head = (head + 1) % cap;
            const float held = holdVal[(size_t) head];

            // 3. box average over L samples -> smooth attack that lands exactly on the peak
            avgSum += (double) held - (double) avgBuf[(size_t) avgPos];
            avgBuf[(size_t) avgPos] = held;
            avgPos = (avgPos + 1) % L;
            const double target = std::min (1.0, avgSum / (double) L);

            // 4. release (only slows gain recovery, never delays reduction)
            if (target < gain) gain = target;
            else               gain += (target - gain) * releaseCoef;

            minGain = std::min (minGain, gain);
            ++sampleIndex;

            // 5. apply, with a final sample-peak safety clamp
            left[i]  = std::clamp ((float) (dl * gain), -ceiling, ceiling);
            right[i] = std::clamp ((float) (dr * gain), -ceiling, ceiling);
        }

        grDb = (float) gainToDb (minGain);
    }

private:
    double fs = 48000.0;
    int L = 72, latency = 79;
    bool enabled = true;

    float inputGainDb = 0.0f, ceiling = 0.891f, releaseMs = 100.0f, grDb = 0.0f;
    double releaseCoef = 0.0, gain = 1.0;
    Smoother inGain;

    TruePeakDetector tpL, tpR;
    std::vector<float> delayL, delayR;
    int dPos = 0;

    std::vector<float> holdVal;
    std::vector<int64_t> holdIdx;
    int head = 0, tail = 0;
    int64_t sampleIndex = 0;

    std::vector<float> avgBuf;
    double avgSum = 0;
    int avgPos = 0;
};
} // namespace polaris
