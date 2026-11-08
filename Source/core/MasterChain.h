#pragma once
// The full processing chain: input -> EQ -> glue -> saturation -> stereo -> limiter.
// The Drive macro is applied on top of the user's settings, so riding Drive never overwrites them.
#include "CorrectiveEq.h"
#include "GlueCompressor.h"
#include "Saturator.h"
#include "StereoTool.h"
#include "Limiter.h"
#include <array>

namespace polaris
{
struct ChainSettings
{
    float inputGainDb = 0.0f;
    float drive = 0.0f;          // 0..1
    int character = 0;           // 0 Clean, 1 Warm, 2 Punchy

    bool eqOn = true;
    std::array<CorrectiveEq::Band, CorrectiveEq::kBands> eq {{ { 60.0f, 0.0f, 0.7f }, { 200.0f, 0.0f, 1.0f },
                                                               { 500.0f, 0.0f, 1.0f }, { 1500.0f, 0.0f, 1.0f },
                                                               { 5000.0f, 0.0f, 1.0f }, { 12000.0f, 0.0f, 0.7f } }};

    bool glueOn = true;
    float glueXLow = 120.0f, glueXHigh = 3000.0f;
    float glueThresholdDb = -12.0f, glueRatio = 2.0f, glueAttackMs = 30.0f, glueReleaseMs = 200.0f;

    bool satOn = true;
    float satDriveDb = 0.0f, satMix = 1.0f;

    bool stereoOn = true;
    float monoFreq = 20.0f, width = 1.0f;

    bool limOn = true;
    float limGainDb = 0.0f, limCeilingDb = -1.0f, limReleaseMs = 100.0f;
};

/** Maps the one-knob Drive amount onto the chain. Tuned by ear; adjust the numbers to taste. */
inline ChainSettings applyDrive (ChainSettings s)
{
    const float d = std::pow (std::clamp (s.drive, 0.0f, 1.0f), 1.2f);

    s.eq[0].gainDb = std::clamp (s.eq[0].gainDb + 1.5f * d, -12.0f, 12.0f);   // a little low-end weight
    s.eq[5].gainDb = std::clamp (s.eq[5].gainDb + 1.0f * d, -12.0f, 12.0f);   // and some air
    s.glueThresholdDb -= 10.0f * d;                                            // more glue
    s.glueRatio = std::min (6.0f, s.glueRatio + 1.0f * d);
    s.satDriveDb += 9.0f * d;                                                  // more density
    s.limGainDb += 6.0f * d;                                                   // louder
    return s;
}

class MasterChain
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        inGain.prepare (fs, 20.0, 1.0);
        eq.prepare (fs);
        glue.prepare (fs);
        sat.prepare (fs);
        stereo.prepare (fs);
        limiter.prepare (fs);
        setSettings (settings);
        reset();
    }

    void reset()
    {
        eq.reset(); glue.reset(); sat.reset(); stereo.reset(); limiter.reset();
        inGain.snap();
    }

    void setSettings (const ChainSettings& user)
    {
        settings = user;
        const ChainSettings s = applyDrive (user);

        inGain.setTarget (dbToGain (s.inputGainDb));

        eq.setEnabled (s.eqOn);
        for (int b = 0; b < CorrectiveEq::kBands; ++b)
            eq.setBand (b, s.eq[(size_t) b].freq, s.eq[(size_t) b].gainDb, s.eq[(size_t) b].q);

        glue.setEnabled (s.glueOn);
        glue.setCrossovers (s.glueXLow, s.glueXHigh);
        glue.setThresholdDb (s.glueThresholdDb);
        glue.setRatio (s.glueRatio);
        glue.setAttackMs (s.glueAttackMs);
        glue.setReleaseMs (s.glueReleaseMs);

        sat.setEnabled (s.satOn);
        sat.setMode ((Saturator::Mode) std::clamp (s.character, 0, 2));
        sat.setDriveDb (s.satDriveDb);
        sat.setMix (s.satMix);

        stereo.setEnabled (s.stereoOn);
        stereo.setMonoFrequency (s.monoFreq);
        stereo.setWidth (s.width);

        limiter.setEnabled (s.limOn);
        limiter.setInputGainDb (s.limGainDb);
        limiter.setCeilingDb (s.limCeilingDb);
        limiter.setReleaseMs (s.limReleaseMs);
    }

    /** Works with any block size. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = (float) inGain.next();
            left[i] *= g; right[i] *= g;
        }
        eq.process (left, right, numSamples);
        glue.process (left, right, numSamples);
        sat.process (left, right, numSamples);
        stereo.process (left, right, numSamples);
        limiter.process (left, right, numSamples);
    }

    int getLatency() const              { return limiter.getLatency(); }
    float getLimiterGrDb() const        { return limiter.getGainReductionDb(); }
    float getGlueGrDb() const           { return glue.getGainReductionDb(); }
    const ChainSettings& getSettings() const { return settings; }

private:
    double fs = 48000.0;
    ChainSettings settings;
    Smoother inGain;
    CorrectiveEq eq;
    GlueCompressor glue;
    Saturator sat;
    StereoTool stereo;
    LookaheadLimiter limiter;
};
} // namespace polaris
