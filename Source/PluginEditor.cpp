#include "PluginEditor.h"
#include "core/ParamIds.h"

namespace ids = polaris::ids;
using namespace polaris::ui;

namespace
{
std::vector<std::string> eqIds()
{
    std::vector<std::string> v { ids::eqOn };
    for (int b = 0; b < ids::numEqBands; ++b)
    {
        v.push_back (ids::eqFreq (b));
        v.push_back (ids::eqGain (b));
        v.push_back (ids::eqQ (b));
    }
    return v;
}

void setupComboFromParam (juce::ComboBox& box, juce::AudioProcessorValueTreeState& apvts, const std::string& id)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (juce::String (id))))
        box.addItemList (choice->choices, 1);
}

void setupLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (font (11.0f));
    l.setColour (juce::Label::textColourId, colours::dim);
    l.setJustificationType (juce::Justification::centredLeft);
}
} // namespace

PolarisEditor::PolarisEditor (PolarisProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      spectrum (p),
      meters (p),
      assistant (p, [this] (const polaris::AnalysisResult& r) { spectrum.setAnalysis (r); }),
      eqPanel (p.apvts, eqIds()),
      gluePanel (p.apvts, { ids::glueOn, ids::glueXLow, ids::glueXHigh, ids::glueThreshold, ids::glueRatio, ids::glueAttack, ids::glueRelease }),
      satPanel (p.apvts, { ids::satOn, ids::satDrive, ids::satMix, ids::character }),
      stereoPanel (p.apvts, { ids::stereoOn, ids::monoFreq, ids::width }),
      limiterPanel (p.apvts, { ids::limOn, ids::limGain, ids::limCeiling, ids::limRelease })
{
    setLookAndFeel (&lookAndFeel);

    title.setText ("POLARIS", juce::dontSendNotification);
    title.setFont (font (20.0f, true));
    title.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (title);

    modeButton.setClickingTogglesState (true);
    modeButton.setTooltip ("Show every module's controls");
    modeButton.onClick = [this] { setProMode (modeButton.getToggleState()); };
    addAndMakeVisible (modeButton);

    compareButton.setClickingTogglesState (true);
    compareButton.setTooltip ("Hear the unprocessed mix at the same loudness, so louder never fools your ears");
    compareAttachment = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, juce::String (ids::compare), compareButton);
    addAndMakeVisible (compareButton);

    undoButton.setTooltip ("Undo the last change (a whole knob move or Apply all counts as one step)");
    redoButton.setTooltip ("Redo");
    undoButton.onClick = [this] { proc.undo(); updateHistoryButtons(); };
    redoButton.onClick = [this] { proc.redo(); updateHistoryButtons(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    const char* slotNames[] = { "A", "B", "C", "D" };
    for (int i = 0; i < PolarisProcessor::kNumSlots; ++i)
    {
        auto& b = slotButtons[(size_t) i];
        b.setButtonText (slotNames[i]);
        b.setTooltip ("Snapshot " + juce::String (slotNames[i]) + ": click to switch and compare settings. "
                      "Shift-click to copy the current sound here.");
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                             | (i < PolarisProcessor::kNumSlots - 1 ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, i]
        {
            if (juce::ModifierKeys::currentModifiers.isShiftDown()) proc.copyActiveSlotTo (i);
            else proc.selectSlot (i);
            updateHistoryButtons();
        };
        addAndMakeVisible (b);
    }

    driveKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    driveKnob.setTextValueSuffix (" %");
    driveKnob.setTooltip ("One knob: pushes EQ, glue, saturation and limiter together");
    driveAttachment = std::make_unique<APVTS::SliderAttachment> (proc.apvts, juce::String (ids::drive), driveKnob);
    inputKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    inputKnob.setTextValueSuffix (" dB");
    inputAttachment = std::make_unique<APVTS::SliderAttachment> (proc.apvts, juce::String (ids::inputGain), inputKnob);

    setupComboFromParam (targetBox, proc.apvts, ids::target);
    setupComboFromParam (characterBox, proc.apvts, ids::character);
    setupComboFromParam (styleBox, proc.apvts, ids::style);
    targetAttachment = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, juce::String (ids::target), targetBox);
    characterAttachment = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, juce::String (ids::character), characterBox);
    styleAttachment = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, juce::String (ids::style), styleBox);

    setupLabel (driveLabel, "DRIVE");
    driveLabel.setJustificationType (juce::Justification::centred);
    setupLabel (inputLabel, "INPUT");
    inputLabel.setJustificationType (juce::Justification::centred);
    setupLabel (targetLabel, "TARGET");
    setupLabel (characterLabel, "CHARACTER");
    setupLabel (styleLabel, "STYLE");

    for (auto* c : std::initializer_list<juce::Component*> { &driveKnob, &inputKnob, &driveLabel, &inputLabel, &targetLabel,
                                                             &characterLabel, &styleLabel, &targetBox, &characterBox, &styleBox,
                                                             &spectrum, &meters, &tabs })
        addAndMakeVisible (c);

    tabs.setTabBarDepth (28);
    tabs.setOutline (0);
    setProMode (proc.uiProMode.load());
    modeButton.setToggleState (proMode, juce::dontSendNotification);

    setResizable (true, true);
    setResizeLimits (960, 640, 2400, 1600);
    setSize (1120, 740);
    startTimerHz (30);
}

PolarisEditor::~PolarisEditor()
{
    stopTimer();
    tabs.clearTabs();
    setLookAndFeel (nullptr);
}

void PolarisEditor::setProMode (bool pro)
{
    proMode = pro;
    proc.uiProMode = pro;
    const auto bg = colours::panel;

    tabs.clearTabs();
    tabs.addTab ("Assistant", bg, &assistant, false);
    if (pro)
    {
        tabs.addTab ("EQ", bg, &eqPanel, false);
        tabs.addTab ("Glue", bg, &gluePanel, false);
        tabs.addTab ("Saturation", bg, &satPanel, false);
        tabs.addTab ("Stereo", bg, &stereoPanel, false);
        tabs.addTab ("Limiter", bg, &limiterPanel, false);
    }
    modeButton.setButtonText (pro ? "Simple mode" : "Pro mode");
}

void PolarisEditor::updateHistoryButtons()
{
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    const int active = proc.getActiveSlot();
    for (int i = 0; i < PolarisProcessor::kNumSlots; ++i)
    {
        auto& b = slotButtons[(size_t) i];
        b.setToggleState (i == active, juce::dontSendNotification);
        b.setColour (juce::TextButton::textColourOffId, proc.isSlotUsed (i) ? colours::text : colours::dim);
    }
}

void PolarisEditor::timerCallback()
{
    proc.historyTick();
    updateHistoryButtons();
    spectrum.refresh();
    meters.repaint();
    assistant.poll();
}

void PolarisEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::dim);
    g.setFont (font (11.0f));
    g.drawText ("AI mix & master assistant", title.getBounds().translated (title.getWidth() - 10, 0).withWidth (220),
                juce::Justification::centredLeft);
}

void PolarisEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto header = area.removeFromTop (34);
    title.setBounds (header.removeFromLeft (120));
    modeButton.setBounds (header.removeFromRight (110).reduced (0, 3));
    header.removeFromRight (8);
    compareButton.setBounds (header.removeFromRight (100).reduced (0, 3));
    header.removeFromRight (12);
    for (int i = PolarisProcessor::kNumSlots - 1; i >= 0; --i)
        slotButtons[(size_t) i].setBounds (header.removeFromRight (34).reduced (0, 3));
    header.removeFromRight (12);
    redoButton.setBounds (header.removeFromRight (60).reduced (0, 3));
    header.removeFromRight (4);
    undoButton.setBounds (header.removeFromRight (60).reduced (0, 3));
    area.removeFromTop (6);

    const int bottomH = juce::jmax (250, (int) (area.getHeight() * 0.42f));
    auto bottom = area.removeFromBottom (bottomH);
    area.removeFromBottom (8);

    // left column: one-knob controls
    auto left = area.removeFromLeft (190);
    driveLabel.setBounds (left.removeFromTop (16));
    driveKnob.setBounds (left.removeFromTop (juce::jmin (150, left.getHeight() / 2)));
    left.removeFromTop (6);
    auto combo = [&left] (juce::Label& l, juce::ComboBox& b)
    {
        l.setBounds (left.removeFromTop (16));
        b.setBounds (left.removeFromTop (26));
        left.removeFromTop (6);
    };
    combo (targetLabel, targetBox);
    combo (styleLabel, styleBox);
    combo (characterLabel, characterBox);
    inputLabel.setBounds (left.removeFromTop (14));
    inputKnob.setBounds (left.removeFromTop (juce::jmax (0, juce::jmin (90, left.getHeight()))));
    area.removeFromLeft (8);

    // right column: meters
    meters.setBounds (area.removeFromRight (190));
    area.removeFromRight (8);

    spectrum.setBounds (area);
    tabs.setBounds (bottom);
}
