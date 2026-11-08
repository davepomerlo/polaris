#include "ModulePanel.h"
#include "LookAndFeel.h"

using namespace polaris::ui;
using APVTS = juce::AudioProcessorValueTreeState;

ModulePanel::ModulePanel (APVTS& state, const std::vector<std::string>& paramIds, int columns)
    : apvts (state), fixedColumns (columns)
{
    for (const auto& idStd : paramIds)
    {
        const juce::String id (idStd);
        auto* param = apvts.getParameter (id);
        if (param == nullptr) { jassertfalse; continue; }

        Cell cell;
        cell.label = std::make_unique<juce::Label> ();
        cell.label->setText (param->getName (40), juce::dontSendNotification);
        cell.label->setJustificationType (juce::Justification::centred);
        cell.label->setFont (font (11.0f));
        cell.label->setColour (juce::Label::textColourId, colours::dim);

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (choice->choices, 1);
            comboAttachments.push_back (std::make_unique<APVTS::ComboBoxAttachment> (apvts, id, *box));
            cell.control = std::move (box);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
        {
            auto toggle = std::make_unique<juce::ToggleButton> ("On");
            buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (apvts, id, *toggle));
            cell.control = std::move (toggle);
        }
        else
        {
            auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
            sliderAttachments.push_back (std::make_unique<APVTS::SliderAttachment> (apvts, id, *slider));
            slider->setTextValueSuffix (" " + param->getLabel());
            slider->setTooltip (param->getName (100));
            cell.control = std::move (slider);
        }

        addAndMakeVisible (*cell.control);
        addAndMakeVisible (*cell.label);
        cells.push_back (std::move (cell));
    }
}

void ModulePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void ModulePanel::resized()
{
    const int cellW = 96, cellH = 110;
    auto area = getLocalBounds().reduced (8);
    const int cols = fixedColumns > 0 ? fixedColumns : juce::jmax (1, area.getWidth() / cellW);
    const int w = area.getWidth() / cols;

    for (size_t i = 0; i < cells.size(); ++i)
    {
        const int c = (int) i % cols, r = (int) i / cols;
        juce::Rectangle<int> cellArea (area.getX() + c * w, area.getY() + r * cellH, w, cellH);
        cells[i].label->setBounds (cellArea.removeFromTop (16));
        auto ctrl = cellArea.reduced (6, 2);
        if (dynamic_cast<juce::Slider*> (cells[i].control.get()) == nullptr)
            ctrl = ctrl.withSizeKeepingCentre (ctrl.getWidth(), 26);
        cells[i].control->setBounds (ctrl);
    }
}
