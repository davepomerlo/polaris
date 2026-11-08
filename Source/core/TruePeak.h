#pragma once
// 4x oversampled true-peak detector (windowed-sinc polyphase interpolation).
// Output is delayed by kDelay samples relative to the input.
#include "Biquad.h"
#include <array>
#include <cmath>

namespace polaris
{
class TruePeakDetector
{
public:
    static constexpr int kHalfTaps = 8;              // taps each side of the interpolation point
    static constexpr int kTaps = 2 * kHalfTaps;      // taps per phase
    static constexpr int kPhases = 4;                // 4x oversampling
    static constexpr int kDelay = kHalfTaps;         // samples of delay

    TruePeakDetector()
    {
        for (int p = 1; p < kPhases; ++p)
        {
            const double t = (double) p / kPhases;
            for (int j = 0; j < kTaps; ++j)
            {
                const int k = j - kHalfTaps + 1;            // k in [-H+1, H]
                const double u = (double) k - t;            // distance from interpolation point
                const double sinc = std::abs (u) < 1e-12 ? 1.0 : std::sin (kPi * u) / (kPi * u);
                const double win = 0.5 + 0.5 * std::cos (kPi * u / (double) kHalfTaps);
                coeffs[(size_t) p][(size_t) j] = sinc * win;
            }
        }
        reset();
    }

    void reset() { history.fill (0.0f); pos = 0; }

    /** Push one sample, returns the true-peak magnitude of the sample kDelay steps ago
        (max of that sample and the 3 interpolated points after it). */
    inline float process (float x) noexcept
    {
        history[(size_t) pos] = x;
        pos = (pos + 1) % kTaps;            // pos now points at the oldest sample

        // history in time order: oldest at pos, newest at pos-1
        // centre sample (index kHalfTaps - 1 in time order) corresponds to k = 0
        float peak = std::abs (at (kHalfTaps - 1));
        for (int p = 1; p < kPhases; ++p)
        {
            double acc = 0;
            for (int j = 0; j < kTaps; ++j)
                acc += coeffs[(size_t) p][(size_t) j] * at (j);
            peak = std::max (peak, (float) std::abs (acc));
        }
        return peak;
    }

private:
    inline float at (int timeIndex) const noexcept { return history[(size_t) ((pos + timeIndex) % kTaps)]; }

    std::array<std::array<double, kTaps>, kPhases> coeffs {};
    std::array<float, kTaps> history {};
    int pos = 0;
};
} // namespace polaris
