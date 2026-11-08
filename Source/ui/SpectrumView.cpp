#include "SpectrumView.h"
#include "LookAndFeel.h"
#include "../core/ParamIds.h"

namespace ids = polaris::ids;
using namespace polaris::ui;

SpectrumView::SpectrumView (PolarisProcessor& p) : proc (p)
{
    eqModel.prepare (sampleRate);
    setOpaque (true);
}

void SpectrumView::refresh()
{
    const double fs = proc.getAnalysisSampleRate();
    if (fs > 0.0 && fs != sampleRate) { sampleRate = fs; eqModel.prepare (sampleRate); }

    proc.copySpectrum (spectrum);

    auto& vt = proc.apvts;
    for (int b = 0; b < polaris::CorrectiveEq::kBands; ++b)
        eqModel.setBand (b, vt.getRawParameterValue (juce::String (ids::eqFreq (b)))->load(),
                            vt.getRawParameterValue (juce::String (ids::eqGain (b)))->load(),
                            vt.getRawParameterValue (juce::String (ids::eqQ (b)))->load());
    repaint();
}

//==============================================================================
juce::Rectangle<float> SpectrumView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f).withTrimmedBottom (14.0f).withTrimmedLeft (28.0f);
}

float SpectrumView::xForFreq (float f) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (f / kMinHz) / std::log (kMaxHz / kMinHz);
}

float SpectrumView::freqForX (float x) const
{
    const auto a = plotArea();
    return kMinHz * std::pow (kMaxHz / kMinHz, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

float SpectrumView::yForSpecDb (float db, float freq) const
{
    const auto a = plotArea();
    const float tilted = db + kTiltDbPerOct * std::log2 (freq / 1000.0f);   // so a typical mix reads flat
    return juce::jmap (tilted, kSpecBottom, kSpecTop, a.getBottom(), a.getY());
}

float SpectrumView::yForEqDb (float db) const
{
    const auto a = plotArea();
    return a.getCentreY() - db / kEqRange * a.getHeight() * 0.45f;
}

float SpectrumView::eqDbForY (float y) const
{
    const auto a = plotArea();
    return (a.getCentreY() - y) / (a.getHeight() * 0.45f) * kEqRange;
}

float SpectrumView::spectrumDbAt (float freq) const
{
    if (spectrum.size() < 2) return -120.0f;
    const double n = (double) (spectrum.size() - 1) * 2.0;
    const double bin = freq / sampleRate * n;
    const int i = juce::jlimit (0, (int) spectrum.size() - 2, (int) bin);
    const float frac = (float) juce::jlimit (0.0, 1.0, bin - i);
    return spectrum[(size_t) i] * (1.0f - frac) + spectrum[(size_t) i + 1] * frac;
}

juce::Point<float> SpectrumView::nodePosition (int band) const
{
    const auto& b = eqModel.getBand (band);
    return { xForFreq (b.freq), yForEqDb (b.gainDb) };
}

int SpectrumView::hitNode (juce::Point<float> pos) const
{
    for (int b = 0; b < polaris::CorrectiveEq::kBands; ++b)
        if (nodePosition (b).getDistanceFrom (pos) < 12.0f) return b;
    return -1;
}

void SpectrumView::setParam (const std::string& id, float value)
{
    if (auto* prm = proc.apvts.getParameter (juce::String (id)))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

//==============================================================================
void SpectrumView::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    const auto a = plotArea();

    // ---- grid
    g.setFont (font (11.0f));
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float x = xForFreq (f);
        g.setColour (colours::grid);
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (colours::dim);
        const juce::String label = f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String (juce::roundToInt (f));
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, a.getBottom() + 1.0f, 40.0f, 13.0f), juce::Justification::centred);
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = yForEqDb (db);
        g.setColour (db == 0.0f ? colours::grid.brighter (0.3f) : colours::grid);
        g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
        g.setColour (colours::dim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (a.getX() - 28.0f, y - 7.0f, 25.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    // ---- live spectrum (post-chain)
    if (! spectrum.empty())
    {
        juce::Path fill;
        fill.startNewSubPath (a.getX(), a.getBottom());
        for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
        {
            const float f = freqForX (x);
            fill.lineTo (x, juce::jlimit (a.getY(), a.getBottom(), yForSpecDb (spectrumDbAt (f), f)));
        }
        fill.lineTo (a.getRight(), a.getBottom());
        fill.closeSubPath();
        g.setColour (colours::accent.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (colours::accent.withAlpha (0.7f));
        g.strokePath (fill, juce::PathStrokeType (1.2f));
    }

    // ---- target curve, aligned (by level) to the analysed mix if there is one, else the live spectrum
    const auto centres = polaris::ListenAnalyzer::bandCenters();
    const auto settings = proc.currentAssistantSettings();
    std::vector<float> target;
    if (settings.style == polaris::Style::Reference && settings.referenceCurve.size() == centres.size())
        target = settings.referenceCurve;
    else
        target = polaris::targetCurve (settings.style == polaris::Style::Reference ? polaris::Style::Pop : settings.style, centres);

    // the Listen analysis uses an FFT twice as long, so per-bin noise power reads 3 dB lower
    const float listenOffset = 10.0f * std::log10 (2.0f);
    const bool haveListen = analysis.valid && analysis.bandLevelsDb.size() == centres.size();

    double sumRef = 0, sumT = 0; int cnt = 0;
    for (size_t k = 0; k < centres.size(); ++k)
    {
        if (centres[k] < 100.0f || centres[k] > 10000.0f) continue;
        sumRef += haveListen ? analysis.bandLevelsDb[k] + listenOffset : spectrumDbAt (centres[k]);
        sumT += target[k];
        ++cnt;
    }
    if (cnt > 0)
    {
        const float offset = (float) ((sumRef - sumT) / cnt);
        juce::Path upper, lower;
        for (size_t k = 0; k < centres.size(); ++k)
        {
            const float x = xForFreq (centres[k]);
            const float yU = yForSpecDb (target[k] + offset + 2.0f, centres[k]);
            const float yL = yForSpecDb (target[k] + offset - 2.0f, centres[k]);
            if (k == 0) { upper.startNewSubPath (x, yU); lower.startNewSubPath (x, yL); }
            else        { upper.lineTo (x, yU); lower.lineTo (x, yL); }
        }
        juce::Path band (upper);
        for (int k = (int) centres.size() - 1; k >= 0; --k)
            band.lineTo (xForFreq (centres[(size_t) k]), yForSpecDb (target[(size_t) k] + offset - 2.0f, centres[(size_t) k]));
        band.closeSubPath();
        g.setColour (colours::targetBand.withAlpha (0.12f));
        g.fillPath (band);
        g.setColour (colours::targetBand.withAlpha (0.5f));
        g.strokePath (upper, juce::PathStrokeType (1.0f));
        g.strokePath (lower, juce::PathStrokeType (1.0f));
    }

    // ---- the analysed mix (Listen result)
    if (haveListen)
    {
        juce::Path p;
        for (size_t k = 0; k < centres.size(); ++k)
        {
            const float x = xForFreq (centres[k]);
            const float y = juce::jlimit (a.getY(), a.getBottom(), yForSpecDb (analysis.bandLevelsDb[k] + listenOffset, centres[k]));
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (colours::listened);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }

    // ---- EQ curve and nodes
    {
        juce::Path eq;
        for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
        {
            const float y = yForEqDb ((float) eqModel.responseDb (freqForX (x)));
            if (x == a.getX()) eq.startNewSubPath (x, y); else eq.lineTo (x, y);
        }
        g.setColour (colours::text.withAlpha (0.9f));
        g.strokePath (eq, juce::PathStrokeType (2.0f));

        for (int b = 0; b < polaris::CorrectiveEq::kBands; ++b)
        {
            const auto pt = nodePosition (b);
            const bool active = b == dragBand || b == hoverBand;
            const float r = active ? 9.0f : 7.0f;
            g.setColour (std::abs (eqModel.getBand (b).gainDb) > 0.05f ? colours::accent : colours::panelLight);
            g.fillEllipse (pt.x - r, pt.y - r, 2 * r, 2 * r);
            g.setColour (colours::text);
            g.drawEllipse (pt.x - r, pt.y - r, 2 * r, 2 * r, 1.0f);
            g.setFont (font (10.0f, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (pt.x - r, pt.y - r, 2 * r, 2 * r), juce::Justification::centred);
        }

        if (hoverBand >= 0 || dragBand >= 0)
        {
            const int b = dragBand >= 0 ? dragBand : hoverBand;
            const auto& band = eqModel.getBand (b);
            const juce::String info = (band.freq < 100.0f ? juce::String (band.freq, 1) : juce::String (juce::roundToInt (band.freq))) + " Hz   "
                                    + juce::String (band.gainDb, 1) + " dB   Q " + juce::String (band.q, 2);
            g.setColour (colours::text);
            g.setFont (font (12.0f));
            g.drawText (info, a.reduced (6.0f).removeFromTop (16.0f), juce::Justification::centredRight);
        }
    }

    // ---- legend
    g.setFont (font (11.0f));
    auto legend = a.reduced (6.0f).removeFromTop (16.0f);
    auto item = [&] (juce::Colour c, const juce::String& t)
    {
        g.setColour (c);
        g.fillRect (legend.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 3.0f));
        g.setColour (colours::dim);
        g.drawText (t, legend.removeFromLeft (90.0f).withTrimmedLeft (4.0f), juce::Justification::centredLeft);
    };
    item (colours::accent, "Output");
    item (colours::targetBand, "Target");
    if (haveListen) item (colours::listened, "Your mix");
}

//==============================================================================
void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    dragBand = hitNode (e.position);
    if (dragBand >= 0)
    {
        for (const auto& id : { ids::eqFreq (dragBand), ids::eqGain (dragBand) })
            if (auto* prm = proc.apvts.getParameter (juce::String (id))) prm->beginChangeGesture();
    }
}

void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    setParam (ids::eqFreq (dragBand), juce::jlimit (kMinHz, kMaxHz, freqForX (e.position.x)));
    setParam (ids::eqGain (dragBand), juce::jlimit (-12.0f, 12.0f, eqDbForY (e.position.y)));
    refresh();
}

void SpectrumView::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
    {
        for (const auto& id : { ids::eqFreq (dragBand), ids::eqGain (dragBand) })
            if (auto* prm = proc.apvts.getParameter (juce::String (id))) prm->endChangeGesture();
    }
    dragBand = -1;
}

void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitNode (e.position);
    if (b < 0) return;
    proc.applyChanges ({ { ids::eqGain (b), 0.0f } });
    refresh();
}

void SpectrumView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = hitNode (e.position);
    if (b < 0) { Component::mouseWheelMove (e, w); return; }
    const float q = juce::jlimit (0.3f, 6.0f, eqModel.getBand (b).q * (1.0f + w.deltaY * 0.5f));
    proc.applyChanges ({ { ids::eqQ (b), q } });
    refresh();
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h != hoverBand) { hoverBand = h; repaint(); }
}
