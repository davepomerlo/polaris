#pragma once
// Polaris core: biquad filters (RBJ cookbook). JUCE-free so it can be unit tested.
#include <cmath>
#include <complex>
#include <algorithm>

namespace polaris
{
constexpr double kPi = 3.14159265358979323846;

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g, double floorDb = -120.0)
{
    return g > 0.0 ? std::max (floorDb, 20.0 * std::log10 (g)) : floorDb;
}

struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static double clampFreq (double f, double fs) { return std::clamp (f, 5.0, fs * 0.49); }

    static BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        BiquadCoeffs c;
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
        c.a1 = a1 / a0; c.a2 = a2 / a0;
        return c;
    }

    static BiquadCoeffs peak (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double alpha = std::sin (w) / (2.0 * q);
        const double cw = std::cos (w);
        return normalise (1 + alpha * A, -2 * cw, 1 - alpha * A,
                          1 + alpha / A, -2 * cw, 1 - alpha / A);
    }

    static BiquadCoeffs lowShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double alpha = std::sin (w) / (2.0 * q);
        const double cw = std::cos (w);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) - (A - 1) * cw + sa),
                          2 * A * ((A - 1) - (A + 1) * cw),
                          A * ((A + 1) - (A - 1) * cw - sa),
                          (A + 1) + (A - 1) * cw + sa,
                          -2 * ((A - 1) + (A + 1) * cw),
                          (A + 1) + (A - 1) * cw - sa);
    }

    static BiquadCoeffs highShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double alpha = std::sin (w) / (2.0 * q);
        const double cw = std::cos (w);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) + (A - 1) * cw + sa),
                          -2 * A * ((A - 1) + (A + 1) * cw),
                          A * ((A + 1) + (A - 1) * cw - sa),
                          (A + 1) - (A - 1) * cw + sa,
                          2 * ((A - 1) - (A + 1) * cw),
                          (A + 1) - (A - 1) * cw - sa);
    }

    static BiquadCoeffs lowpass (double fs, double f, double q = 0.70710678118654752)
    {
        const double w = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double alpha = std::sin (w) / (2.0 * q);
        const double cw = std::cos (w);
        return normalise ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    static BiquadCoeffs highpass (double fs, double f, double q = 0.70710678118654752)
    {
        const double w = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double alpha = std::sin (w) / (2.0 * q);
        const double cw = std::cos (w);
        return normalise ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    /** Magnitude response in dB at frequency f. Used for drawing EQ curves. */
    double magnitudeDb (double f, double fs) const
    {
        const double w = 2.0 * kPi * f / fs;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return gainToDb (std::abs (num / den));
    }

    bool operator== (const BiquadCoeffs& o) const
    {
        return b0 == o.b0 && b1 == o.b1 && b2 == o.b2 && a1 == o.a1 && a2 == o.a2;
    }
};

/** Transposed direct form II biquad, double-precision state. */
struct Biquad
{
    BiquadCoeffs c;
    double z1 = 0, z2 = 0;

    inline float process (float xf) noexcept
    {
        const double x = xf;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }

    void reset() noexcept { z1 = z2 = 0; }
};

/** 4th-order Linkwitz-Riley section (two cascaded Butterworth biquads). */
struct LR4
{
    Biquad s1, s2;

    void setLowpass (double fs, double f)  { s1.c = s2.c = BiquadCoeffs::lowpass (fs, f); }
    void setHighpass (double fs, double f) { s1.c = s2.c = BiquadCoeffs::highpass (fs, f); }

    inline float process (float x) noexcept { return s2.process (s1.process (x)); }
    void reset() noexcept { s1.reset(); s2.reset(); }
};

/** LR4 allpass: LP4 + HP4 at the same frequency. Keeps band phases aligned. */
struct LR4Allpass
{
    LR4 lp, hp;
    void set (double fs, double f) { lp.setLowpass (fs, f); hp.setHighpass (fs, f); }
    inline float process (float x) noexcept { return lp.process (x) + hp.process (x); }
    void reset() noexcept { lp.reset(); hp.reset(); }
};

/** One-pole parameter smoother (per-sample). */
struct Smoother
{
    double current = 0, target = 0, coef = 0;

    void prepare (double fs, double timeMs, double initial)
    {
        coef = std::exp (-1.0 / (0.001 * timeMs * fs));
        current = target = initial;
    }
    void setTarget (double t) noexcept { target = t; }
    void snap() noexcept { current = target; }
    inline double next() noexcept { current = target + (current - target) * coef; return current; }
};
} // namespace polaris
