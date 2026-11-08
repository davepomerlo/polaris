#include "LoudnessMeter.h"
#include <algorithm>
#include <numeric>

namespace polaris
{
void LoudnessMeter::prepare (double sampleRate)
{
    fs = sampleRate;

    // K-weighting filter coefficients for any sample rate (same derivation as libebur128).
    double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    double K = std::tan (kPi * f0 / fs);
    const double Vh = std::pow (10.0, G / 20.0);
    const double Vb = std::pow (Vh, 0.4996667741545416);
    double a0 = 1.0 + K / Q + K * K;

    BiquadCoeffs s;
    s.b0 = (Vh + Vb * K / Q + K * K) / a0;
    s.b1 = 2.0 * (K * K - Vh) / a0;
    s.b2 = (Vh - Vb * K / Q + K * K) / a0;
    s.a1 = 2.0 * (K * K - 1.0) / a0;
    s.a2 = (1.0 - K / Q + K * K) / a0;

    f0 = 38.13547087602444; Q = 0.5003270373238773;
    K = std::tan (kPi * f0 / fs);
    a0 = 1.0 + K / Q + K * K;

    BiquadCoeffs h;
    h.b0 = 1.0; h.b1 = -2.0; h.b2 = 1.0;
    h.a1 = 2.0 * (K * K - 1.0) / a0;
    h.a2 = (1.0 - K / Q + K * K) / a0;

    for (int ch = 0; ch < 2; ++ch)
    {
        shelf[(size_t) ch].c = s;
        highpass[(size_t) ch].c = h;
    }

    samplesPerChunk = std::max (1, (int) std::lround (fs * 0.1));
    blockEnergies.reserve (36000);
    shortTermEnergies.reserve (36000);
    reset();
}

void LoudnessMeter::reset()
{
    for (int ch = 0; ch < 2; ++ch) { shelf[(size_t) ch].reset(); highpass[(size_t) ch].reset(); }
    chunkPos = 0;
    chunkAcc = 0;
    chunks.fill (0.0);
    chunkWrite = 0;
    totalChunks = 0;
    blockEnergies.clear();
    shortTermEnergies.clear();
    momentary = shortTerm = integrated = -100.0f;
    lra = 0.0f;
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double l = highpass[0].process (shelf[0].process (left[i]));
        const double r = highpass[1].process (shelf[1].process (right[i]));
        chunkAcc += l * l + r * r;   // channel weights are 1.0 for L and R

        if (++chunkPos >= samplesPerChunk)
            finishChunk();
    }
}

void LoudnessMeter::finishChunk()
{
    chunks[(size_t) chunkWrite] = chunkAcc / (double) samplesPerChunk;
    chunkWrite = (chunkWrite + 1) % (int) chunks.size();
    chunkAcc = 0;
    chunkPos = 0;
    ++totalChunks;

    auto meanOfLast = [this] (int n)
    {
        double sum = 0;
        for (int k = 1; k <= n; ++k)
            sum += chunks[(size_t) ((chunkWrite - k + (int) chunks.size()) % (int) chunks.size())];
        return sum / n;
    };

    if (totalChunks >= 4)
    {
        const double m = meanOfLast (4);
        momentary = energyToLufs (m);
        blockEnergies.push_back (m);
    }

    if (totalChunks >= 30)
    {
        const double st = meanOfLast (30);
        shortTerm = energyToLufs (st);
        shortTermEnergies.push_back (st);
    }
    else if (totalChunks >= 4)
    {
        shortTerm = energyToLufs (meanOfLast ((int) totalChunks));
    }

    if (totalChunks % 10 == 0)
        updateIntegrated();
}

void LoudnessMeter::updateIntegrated()
{
    // Integrated loudness: absolute gate -70 LUFS, then relative gate -10 LU.
    const double absGate = lufsToEnergy (-70.0);
    double sum = 0; size_t n = 0;
    for (double e : blockEnergies)
        if (e > absGate) { sum += e; ++n; }

    if (n == 0) { integrated = -100.0f; }
    else
    {
        const double relGate = (sum / (double) n) * std::pow (10.0, -10.0 / 10.0);
        double sum2 = 0; size_t n2 = 0;
        for (double e : blockEnergies)
            if (e > absGate && e > relGate) { sum2 += e; ++n2; }
        integrated = n2 > 0 ? energyToLufs (sum2 / (double) n2) : -100.0f;
    }

    // Loudness range (EBU Tech 3342): short-term values, abs gate -70, rel gate -20 LU, 10th-95th percentile.
    std::vector<double> st;
    st.reserve (shortTermEnergies.size());
    double stSum = 0;
    for (double e : shortTermEnergies)
        if (e > absGate) { st.push_back (e); stSum += e; }

    if (st.size() < 2) { lra = 0.0f; return; }

    const double stRel = (stSum / (double) st.size()) * std::pow (10.0, -20.0 / 10.0);
    std::vector<float> lufs;
    lufs.reserve (st.size());
    for (double e : st)
        if (e > stRel) lufs.push_back (energyToLufs (e));

    if (lufs.size() < 2) { lra = 0.0f; return; }

    std::sort (lufs.begin(), lufs.end());
    auto pct = [&lufs] (double p)
    {
        const double idx = p * (double) (lufs.size() - 1);
        const size_t i = (size_t) idx;
        const double frac = idx - (double) i;
        const size_t j = std::min (i + 1, lufs.size() - 1);
        return (float) (lufs[i] * (1.0 - frac) + lufs[j] * frac);
    };
    lra = pct (0.95) - pct (0.10);
}
} // namespace polaris
