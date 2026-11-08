#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/SpectrumView.h"
#include "ui/MeterPanel.h"
#include "ui/AssistantPanel.h"
#include "ui/ModulePanel.h"

class PolarisEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit PolarisEditor (PolarisProcessor& p);
    ~PolarisEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void setProMode (bool pro);
    void updateHistoryButtons();

    using APVTS = juce::AudioProcessorValueTreeState;

    PolarisProcessor& proc;
    polaris::ui::PolarisLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    // header
    juce::Label title;
    juce::TextButton modeButton { "Pro mode" };
    juce::TextButton compareButton { "Compare" };
    std::unique_ptr<APVTS::ButtonAttachment> compareAttachment;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    std::array<juce::TextButton, PolarisProcessor::kNumSlots> slotButtons;

    // simple controls
    juce::Slider driveKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider inputKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label driveLabel, inputLabel, targetLabel, characterLabel, styleLabel;
    juce::ComboBox targetBox, characterBox, styleBox;
    std::unique_ptr<APVTS::SliderAttachment> driveAttachment, inputAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> targetAttachment, characterAttachment, styleAttachment;

    // main views (spectrum before assistant: the assistant pushes results into it)
    SpectrumView spectrum;
    MeterPanel meters;
    AssistantPanel assistant;
    ModulePanel eqPanel, gluePanel, satPanel, stereoPanel, limiterPanel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    bool proMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolarisEditor)
};
