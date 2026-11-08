#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

/** Loudness, true peak and gain-reduction readouts. Repainted from the editor's timer. */
class MeterPanel : public juce::Component
{
public:
    explicit MeterPanel (PolarisProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    PolarisProcessor& proc;
    juce::TextButton resetButton { "Reset" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterPanel)
};
