#pragma once
// Parameter IDs shared by the JUCE layer and the assistant (so suggestions can name parameters).
#include <string>

namespace polaris::ids
{
inline const std::string inputGain   = "inputGain";
inline const std::string drive       = "drive";        // 0..100 %
inline const std::string target      = "target";       // choice: Streaming / Loud / Club
inline const std::string character   = "character";    // choice: Clean / Warm / Punchy
inline const std::string style       = "style";        // choice: Pop / Hip-hop-EDM / Rock / Acoustic / Reference
inline const std::string compare     = "compare";      // gain-matched bypass

inline const std::string eqOn        = "eqOn";
inline std::string eqFreq (int band) { return "eq" + std::to_string (band + 1) + "Freq"; }
inline std::string eqGain (int band) { return "eq" + std::to_string (band + 1) + "Gain"; }
inline std::string eqQ (int band)    { return "eq" + std::to_string (band + 1) + "Q"; }
constexpr int numEqBands = 6;

inline const std::string glueOn        = "glueOn";
inline const std::string glueXLow      = "glueXLow";
inline const std::string glueXHigh     = "glueXHigh";
inline const std::string glueThreshold = "glueThreshold";
inline const std::string glueRatio     = "glueRatio";
inline const std::string glueAttack    = "glueAttack";
inline const std::string glueRelease   = "glueRelease";

inline const std::string satOn    = "satOn";
inline const std::string satDrive = "satDrive";
inline const std::string satMix   = "satMix";          // 0..100 %

inline const std::string stereoOn = "stereoOn";
inline const std::string monoFreq = "monoFreq";        // 20 = off
inline const std::string width    = "width";           // 0..200 %

inline const std::string limOn      = "limOn";
inline const std::string limGain    = "limGain";
inline const std::string limCeiling = "limCeiling";
inline const std::string limRelease = "limRelease";

/** Loudness targets matching the "target" choice indices. */
inline float targetLufsForIndex (int index)
{
    switch (index) { case 1: return -9.0f; case 2: return -7.0f; default: return -14.0f; }
}
} // namespace polaris::ids
