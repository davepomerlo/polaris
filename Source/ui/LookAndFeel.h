#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace polaris::ui
{
namespace colours
{
    const juce::Colour background { 0xff121418 };
    const juce::Colour panel      { 0xff1a1d24 };
    const juce::Colour panelLight { 0xff242833 };
    const juce::Colour grid       { 0xff2c313d };
    const juce::Colour text       { 0xffe6e8ee };
    const juce::Colour dim        { 0xff8a90a0 };
    const juce::Colour accent     { 0xff4fa3ff };   // live signal, controls
    const juce::Colour listened   { 0xffffb347 };   // the analysed mix
    const juce::Colour targetBand { 0xff6fdc8c };   // target curve
    const juce::Colour warning    { 0xffff6b6b };
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

class PolarisLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PolarisLookAndFeel()
    {
        using namespace colours;
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, panelLight);
        setColour (juce::Slider::thumbColourId, accent);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonColourId, panelLight);
        setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.8f));
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, panelLight);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, grid);
        setColour (juce::ComboBox::arrowColourId, dim);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, dim);
        setColour (juce::TabbedComponent::backgroundColourId, panel);
        setColour (juce::TabbedComponent::outlineColourId, grid);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, grid);
        setColour (juce::TabbedButtonBar::frontOutlineColourId, accent);
        setColour (juce::TabbedButtonBar::tabTextColourId, dim);
        setColour (juce::TabbedButtonBar::frontTextColourId, text);
        setColour (juce::ScrollBar::thumbColourId, grid);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float lineW = juce::jmax (2.0f, radius * 0.12f);
        const float arcR = radius - lineW * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (track, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        if (slider.isEnabled())
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (value, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        const float knobR = arcR - lineW * 1.4f;
        g.setColour (colours::panelLight);
        g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

        const juce::Point<float> tip (centre.x + (knobR - lineW) * std::sin (angle),
                                      centre.y - (knobR - lineW) * std::cos (angle));
        g.setColour (colours::text);
        g.drawLine ({ centre.getPointOnCircumference (knobR * 0.25f, angle), tip }, juce::jmax (1.5f, lineW * 0.6f));
    }
};
} // namespace polaris::ui
