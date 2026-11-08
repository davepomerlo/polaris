#pragma once
// "Listen" analysis: measures a stretch of the incoming mix for the assistant.
// Runs on the analysis thread, never on the audio thread.
#include "LoudnessMeter.h"
#include "TruePeak.h"
#include "FFT.h"
#include <vector>

namespace polaris
{
struct AnalysisResult
{
    bool valid = false;
    double seconds = 0.0;

    float integratedLufs = -100.0f;
    float shortTermMaxLufs = -100.0f;
    float loudnessRange = 0.0f;
    float truePeakDb = -100.0f;
    float samplePeakDb = -100.0f;
    float plrDb = 0.0f;            // true peak minus integrated loudness
    float crestDb = 0.0f;          // sample peak minus RMS

    float lowCorrelation = 1.0f;   // L/R correlation below ~150 Hz (1 = mono, 0 = unrelated, <0 = out of phase)
    float sideToMidLowDb = -60.0f; // side vs mid energy below 150 Hz
    float sideToMidMidDb = -60.0f; // 150 Hz - 2 kHz
    float sideToMidHighDb = -60.0f;// above 2 kHz

    int clipEvents = 0;            // runs of 3+ samples at full scale
    float dcOffset = 0.0f;

    std::vector<float> bandCentersHz;  // 1/6-octave centres, 31.5 Hz - 16 kHz
    std::vector<float> bandLevelsDb;   // mean power per FFT bin in each band (mid channel)
};

class ListenAnalyzer
{
public:
    static constexpr int kFftOrder = 13;   // 8192 points: ~5.9 Hz bins at 48 kHz

    void prepare (double sampleRate);
    void reset();
    void process (const float* left, const float* right, int numSamples);
    double seconds() const { return (double) count / fs; }

    AnalysisResult finish();

    static std::vector<float> bandCenters();

private:
    void processFrame();

    double fs = 48000.0;
    LoudnessMeter meter;
    TruePeakDetector tpL, tpR;
    float truePeak = 0.0f, samplePeak = 0.0f;
    double sumSq = 0.0;
    long long count = 0;

    Biquad lowL1, lowL2, lowR1, lowR2;
    double lowLR = 0, lowLL = 0, lowRR = 0;

    int clipRunL = 0, clipRunR = 0, clipEvents = 0;
    double dcSum = 0;

    FFT fft { kFftOrder };
    std::vector<float> midBuf, sideBuf, power;
    int bufPos = 0;
    std::vector<double> midPow, sidePow;
    int frames = 0;
    float shortTermMax = -100.0f;
};
} // namespace polaris
