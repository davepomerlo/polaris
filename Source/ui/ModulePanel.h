#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/** A grid of controls built from parameter IDs: knobs for floats, toggles for bools, menus for choices. */
class ModulePanel : public juce::Component
{
public:
    ModulePanel (juce::AudioProcessorValueTreeState& state, const std::vector<std::string>& paramIds, int columns = 0);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Cell
    {
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::Label> label;
    };

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Cell> cells;
    int fixedColumns = 0;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulePanel)
};
