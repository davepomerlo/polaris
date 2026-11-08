#pragma once
// ITU-R BS.1770-4 / EBU R128 loudness meter: momentary, short-term, integrated, LRA.
// Not real-time safe (integrated history grows); run it on the analysis thread.
#include "Biquad.h"
#include <vector>
#include <array>

namespace polaris
{
class LoudnessMeter
{
public:
    void prepare (double sampleRate);
    void reset();

    void process (const float* left, const float* right, int numSamples);

    /** Recompute integrated loudness and LRA now (they otherwise update once per second). */
    void refresh() { updateIntegrated(); }

    float momentaryLufs() const  { return momentary; }
    float shortTermLufs() const  { return shortTerm; }
    float integratedLufs() const { return integrated; }
    float loudnessRange() const  { return lra; }
    double secondsMeasured() const { return (double) totalChunks * 0.1; }

    static float energyToLufs (double e) { return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : -100.0f; }
    static double lufsToEnergy (double l) { return std::pow (10.0, (l + 0.691) / 10.0); }

private:
    void finishChunk();
    void updateIntegrated();

    double fs = 48000.0;
    std::array<Biquad, 2> shelf, highpass;   // K-weighting per channel

    int samplesPerChunk = 4800;               // 100 ms
    int chunkPos = 0;
    double chunkAcc = 0;

    std::array<double, 30> chunks {};         // last 3 s of 100 ms energies
    int chunkWrite = 0;
    long long totalChunks = 0;

    std::vector<double> blockEnergies;        // 400 ms blocks, 100 ms hop (integrated)
    std::vector<double> shortTermEnergies;    // 3 s windows, 100 ms hop (LRA)

    float momentary = -100.0f, shortTerm = -100.0f, integrated = -100.0f, lra = 0.0f;
};
} // namespace polaris
