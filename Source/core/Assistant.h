#pragma once
// The mix/master assistant (v1): an explainable rules engine on top of the Listen analysis.
// Every suggestion carries a reason and a list of parameter changes the user can apply or skip.
#include "Analyzer.h"
#include <string>
#include <vector>

namespace polaris
{
enum class Style { Pop = 0, HipHopEdm = 1, Rock = 2, Acoustic = 3, Reference = 4 };

struct AssistantSettings
{
    Style style = Style::Pop;
    float targetLufs = -14.0f;
    float ceilingDb = -1.0f;
    float drive = 0.0f;                    // 0..1, so the limiter suggestion accounts for the Drive macro
    std::vector<float> referenceCurve;     // band levels from a reference track (Style::Reference)
};

struct ParamChange
{
    std::string paramId;
    float value;                           // in the parameter's own units (dB, Hz, %, ratio...)
};

struct Suggestion
{
    enum class Kind { Eq, Dynamics, Stereo, Loudness, Warning };
    Kind kind = Kind::Eq;
    std::string title;
    std::string detail;
    std::vector<ParamChange> changes;      // empty for pure warnings
};

/** Relative target curve (dB) for a style, at the given band centres. Shape only; the level is arbitrary. */
std::vector<float> targetCurve (Style style, const std::vector<float>& centersHz);

/** Measured minus target, offset-aligned over 100 Hz - 10 kHz and lightly smoothed. */
std::vector<float> spectralDeviation (const AnalysisResult& analysis, const std::vector<float>& target);

/** Build the suggestion list. */
std::vector<Suggestion> makeSuggestions (const AnalysisResult& analysis, const AssistantSettings& settings);

/** One-line summary of the measurements for the UI. */
std::string summarise (const AnalysisResult& analysis);
} // namespace polaris
