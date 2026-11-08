#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

/** FabFilter-style spectrum: live analyzer, target curve, the analysed mix, and draggable EQ nodes.
    Drag a node to move frequency/gain, scroll on it for Q, double-click to reset its gain. */
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (PolarisProcessor& p);

    void refresh();                                       // called from the editor's timer
    void setAnalysis (const polaris::AnalysisResult& a)   { analysis = a; repaint(); }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseMove (const juce::MouseEvent& e) override;

private:
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    static constexpr float kSpecTop = 0.0f, kSpecBottom = -96.0f;   // tilt-compensated dB
    static constexpr float kEqRange = 15.0f;
    static constexpr float kTiltDbPerOct = 4.5f;

    juce::Rectangle<float> plotArea() const;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForSpecDb (float db, float freq) const;
    float yForEqDb (float db) const;
    float eqDbForY (float y) const;
    float spectrumDbAt (float freq) const;
    juce::Point<float> nodePosition (int band) const;
    int hitNode (juce::Point<float> pos) const;
    void setParam (const std::string& id, float value);

    PolarisProcessor& proc;
    std::vector<float> spectrum;
    double sampleRate = 48000.0;
    polaris::AnalysisResult analysis;
    polaris::CorrectiveEq eqModel;
    int dragBand = -1, hoverBand = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};
