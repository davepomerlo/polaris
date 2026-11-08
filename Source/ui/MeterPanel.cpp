#include "MeterPanel.h"
#include "LookAndFeel.h"
#include "../core/ParamIds.h"

using namespace polaris::ui;

MeterPanel::MeterPanel (PolarisProcessor& p) : proc (p)
{
    resetButton.setTooltip ("Reset integrated loudness and true-peak hold");
    resetButton.onClick = [this] { proc.requestMeterReset(); };
    addAndMakeVisible (resetButton);
}

void MeterPanel::resized()
{
    resetButton.setBounds (getLocalBounds().reduced (8).removeFromBottom (24));
}

void MeterPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    auto area = getLocalBounds().toFloat().reduced (10.0f);
    area.removeFromBottom (30.0f);

    const auto& m = proc.meters;
    const float target = polaris::ids::targetLufsForIndex ((int) proc.apvts.getRawParameterValue ("target")->load());
    const float ceiling = proc.apvts.getRawParameterValue ("limCeiling")->load();

    auto fmtLufs = [] (float v) { return v <= -99.0f ? juce::String ("--") : juce::String (v, 1); };

    // big integrated readout
    g.setColour (colours::dim);
    g.setFont (font (11.0f));
    g.drawText ("INTEGRATED", area.removeFromTop (14.0f), juce::Justification::centredLeft);
    const float integ = m.integrated.load();
    g.setColour (integ > target + 1.0f ? colours::warning : colours::text);
    g.setFont (font (30.0f, true));
    g.drawText (fmtLufs (integ), area.removeFromTop (34.0f), juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (font (11.0f));
    g.drawText ("LUFS  (target " + juce::String (juce::roundToInt (target)) + ")", area.removeFromTop (14.0f), juce::Justification::centredLeft);
    area.removeFromTop (8.0f);

    auto row = [&] (const juce::String& name, const juce::String& value, juce::Colour c)
    {
        auto r = area.removeFromTop (18.0f);
        g.setColour (colours::dim);
        g.setFont (font (12.0f));
        g.drawText (name, r, juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (font (12.0f, true));
        g.drawText (value, r, juce::Justification::centredRight);
    };

    const float tp = m.truePeak.load();
    row ("Short-term", fmtLufs (m.shortTerm.load()), colours::text);
    row ("Momentary", fmtLufs (m.momentary.load()), colours::text);
    row ("True peak", tp <= -99.0f ? juce::String ("--") : juce::String (tp, 1) + " dBTP",
         tp > ceiling + 0.1f ? colours::warning : colours::text);
    row ("Range (LRA)", juce::String (m.lra.load(), 1) + " LU", colours::text);
    area.removeFromTop (6.0f);

    // gain reduction bars
    auto grBar = [&] (const juce::String& name, float grDb)
    {
        auto r = area.removeFromTop (30.0f);
        g.setColour (colours::dim);
        g.setFont (font (11.0f));
        g.drawText (name + "  " + juce::String (grDb, 1) + " dB", r.removeFromTop (14.0f), juce::Justification::centredLeft);
        auto bar = r.reduced (0.0f, 3.0f);
        g.setColour (colours::panelLight);
        g.fillRoundedRectangle (bar, 3.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, -grDb / 12.0f);
        g.setColour (colours::listened);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 3.0f);
    };
    grBar ("Glue GR", m.glueGr.load());
    grBar ("Limiter GR", m.limiterGr.load());
}
