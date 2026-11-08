#pragma once
// Mid/side stereo tool: mono bass below a cutoff (LR4 split, phase-matched) and width above it.
#include "Biquad.h"

namespace polaris
{
class StereoTool
{
public:
    void prepare (double sampleRate) { fs = sampleRate; update(); reset(); }
    void reset() { midAP.reset(); sideHP.reset(); }

    /** cutoff <= 20 Hz disables the mono-bass split. */
    void setMonoFrequency (float hz) { if (hz != monoHz) { monoHz = hz; update(); } }
    void setWidth (float w)          { width = std::clamp (w, 0.0f, 2.0f); }
    void setEnabled (bool e)         { enabled = e; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (! enabled) return;
        const bool split = monoHz > 20.5f;

        for (int i = 0; i < numSamples; ++i)
        {
            float m = 0.5f * (left[i] + right[i]);
            float s = 0.5f * (left[i] - right[i]);
            if (split)
            {
                m = midAP.process (m);    // same phase shift as the side split
                s = sideHP.process (s);   // side content below the cutoff is removed
            }
            s *= width;
            left[i] = m + s;
            right[i] = m - s;
        }
    }

private:
    void update()
    {
        const double f = std::max (21.0f, monoHz);
        midAP.set (fs, f);
        sideHP.setHighpass (fs, f);
    }

    double fs = 48000.0;
    float monoHz = 20.0f, width = 1.0f;
    bool enabled = true;
    LR4Allpass midAP;
    LR4 sideHP;
};
} // namespace polaris
