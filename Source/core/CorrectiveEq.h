#pragma once
// 6-band EQ: low shelf, 4 peaks, high shelf. Coefficients are recomputed only when a band changes.
#include "Biquad.h"
#include <array>

namespace polaris
{
class CorrectiveEq
{
public:
    static constexpr int kBands = 6;

    struct Band { float freq = 1000.0f, gainDb = 0.0f, q = 1.0f; };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int b = 0; b < kBands; ++b) recompute (b);
        reset();
    }

    void reset() { for (auto& c : filters) for (auto& f : c) f.reset(); }

    void setBand (int b, float freq, float gainDb, float q)
    {
        auto& band = bands[(size_t) b];
        if (band.freq == freq && band.gainDb == gainDb && band.q == q) return;
        band = { freq, gainDb, q };
        recompute (b);
    }

    void setEnabled (bool e) { enabled = e; }
    const Band& getBand (int b) const { return bands[(size_t) b]; }

    static BiquadCoeffs coeffsFor (int b, double fs, const Band& band)
    {
        if (b == 0)          return BiquadCoeffs::lowShelf (fs, band.freq, band.q, band.gainDb);
        if (b == kBands - 1) return BiquadCoeffs::highShelf (fs, band.freq, band.q, band.gainDb);
        return BiquadCoeffs::peak (fs, band.freq, band.q, band.gainDb);
    }

    /** Total EQ response in dB at frequency f (for the UI curve). */
    double responseDb (double f) const
    {
        double sum = 0;
        for (int b = 0; b < kBands; ++b)
            if (std::abs (bands[(size_t) b].gainDb) > 0.01f)
                sum += coeffsFor (b, fs, bands[(size_t) b]).magnitudeDb (f, fs);
        return sum;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (! enabled) return;
        for (int b = 0; b < kBands; ++b)
        {
            if (std::abs (bands[(size_t) b].gainDb) < 0.01f) continue;   // flat band: skip
            auto& fl = filters[0][(size_t) b];
            auto& fr = filters[1][(size_t) b];
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] = fl.process (left[i]);
                right[i] = fr.process (right[i]);
            }
        }
    }

private:
    void recompute (int b)
    {
        const auto c = coeffsFor (b, fs, bands[(size_t) b]);
        filters[0][(size_t) b].c = c;
        filters[1][(size_t) b].c = c;
    }

    double fs = 48000.0;
    bool enabled = true;
    std::array<Band, kBands> bands {{ { 60.0f, 0.0f, 0.7f }, { 200.0f, 0.0f, 1.0f }, { 500.0f, 0.0f, 1.0f },
                                      { 1500.0f, 0.0f, 1.0f }, { 5000.0f, 0.0f, 1.0f }, { 12000.0f, 0.0f, 0.7f } }};
    std::array<std::array<Biquad, kBands>, 2> filters {};
};
} // namespace polaris
