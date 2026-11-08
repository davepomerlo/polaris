#pragma once
// Saturation with first-order antiderivative anti-aliasing (ADAA): no oversampling, no latency.
// Modes: Clean (tanh), Warm (biased tanh = even harmonics), Punchy (hard clip).
#include "Biquad.h"
#include <array>

namespace polaris
{
class Saturator
{
public:
    enum class Mode { Clean = 0, Warm = 1, Punchy = 2 };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        drive.prepare (fs, 30.0, dbToGain (driveDb));
        // DC blocker at ~8 Hz for the asymmetric (Warm) curve
        dcCoef = std::exp (-2.0 * kPi * 8.0 / fs);
        reset();
    }

    void reset()
    {
        for (auto& s : state) s = {};
        drive.snap();
    }

    void setMode (Mode m)        { mode = m; }
    void setDriveDb (float db)   { driveDb = db; drive.setTarget (dbToGain (db)); }
    void setMix (float m)        { mix = std::clamp (m, 0.0f, 1.0f); }
    void setEnabled (bool e)     { enabled = e; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (! enabled) return;

        for (int i = 0; i < numSamples; ++i)
        {
            const double g = drive.next();
            left[i]  = processSample (left[i], g, state[0]);
            right[i] = processSample (right[i], g, state[1]);
        }
    }

    // --- static curves (public for tests) ---
    static constexpr double kBias = 0.25;

    static double logCosh (double x)
    {
        const double a = std::abs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
    }

    static double f (Mode m, double x)
    {
        switch (m)
        {
            case Mode::Clean:  return std::tanh (x);
            case Mode::Warm:   return std::tanh (x + kBias) - std::tanh (kBias);
            case Mode::Punchy: return std::clamp (x, -1.0, 1.0);
        }
        return x;
    }

    static double F (Mode m, double x)   // antiderivative of f
    {
        switch (m)
        {
            case Mode::Clean:  return logCosh (x);
            case Mode::Warm:   return logCosh (x + kBias) - std::tanh (kBias) * x;
            case Mode::Punchy: return std::abs (x) <= 1.0 ? 0.5 * x * x : std::abs (x) - 0.5;
        }
        return 0.5 * x * x;
    }

    static double smallSignalGain (Mode m)
    {
        if (m == Mode::Warm) { const double t = std::tanh (kBias); return 1.0 - t * t; }
        return 1.0;
    }

private:
    struct ChannelState { double x1 = 0; double dcX = 0, dcY = 0; };

    inline float processSample (float in, double g, ChannelState& s) noexcept
    {
        const double x = (double) in * g;
        const double d = x - s.x1;
        double y = std::abs (d) > 1.0e-6 ? (F (mode, x) - F (mode, s.x1)) / d
                                          : f (mode, 0.5 * (x + s.x1));
        s.x1 = x;

        y /= (g * smallSignalGain (mode));   // unity gain for quiet signals

        if (mode == Mode::Warm)              // remove the DC the asymmetric curve creates
        {
            const double out = y - s.dcX + dcCoef * s.dcY;
            s.dcX = y; s.dcY = out;
            y = out;
        }

        return (float) ((1.0 - mix) * in + mix * y);
    }

    double fs = 48000.0, dcCoef = 0.999;
    Mode mode = Mode::Clean;
    float driveDb = 0.0f, mix = 1.0f;
    bool enabled = true;
    Smoother drive;
    std::array<ChannelState, 2> state {};
};
} // namespace polaris
