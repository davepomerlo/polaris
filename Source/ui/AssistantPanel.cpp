#include "AssistantPanel.h"
#include "LookAndFeel.h"
#include "../core/ParamIds.h"

using namespace polaris::ui;
using Kind = polaris::Suggestion::Kind;

namespace
{
juce::Colour kindColour (Kind k)
{
    switch (k)
    {
        case Kind::Eq:       return colours::accent;
        case Kind::Dynamics: return colours::listened;
        case Kind::Stereo:   return juce::Colour (0xffb48cff);
        case Kind::Loudness: return colours::targetBand;
        case Kind::Warning:  return colours::warning;
    }
    return colours::text;
}

juce::String kindName (Kind k)
{
    switch (k)
    {
        case Kind::Eq:       return "EQ";
        case Kind::Dynamics: return "GLUE";
        case Kind::Stereo:   return "STEREO";
        case Kind::Loudness: return "LOUDNESS";
        case Kind::Warning:  return "CHECK";
    }
    return {};
}
} // namespace

//==============================================================================
AssistantPanel::Card::Card (const polaris::Suggestion& s, std::function<void()> onApply)
    : suggestion (s), onApplyFn (std::move (onApply)), hasChanges (! s.changes.empty())
{
    if (hasChanges)
    {
        applyButton.onClick = [this] { apply(); };
        addAndMakeVisible (applyButton);
    }
}

void AssistantPanel::Card::markApplied()
{
    applied = true;
    applyButton.setButtonText ("Applied");
    applyButton.setEnabled (false);
}

void AssistantPanel::Card::resized()
{
    applyButton.setBounds (getLocalBounds().reduced (10).removeFromRight (80).withSizeKeepingCentre (80, 26));
}

void AssistantPanel::Card::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (colours::panelLight);
    g.fillRoundedRectangle (r, 6.0f);
    const auto c = kindColour (suggestion.kind);
    g.setColour (c);
    g.fillRoundedRectangle (r.removeFromLeft (4.0f), 2.0f);

    auto text = r.reduced (10.0f, 6.0f);
    if (hasChanges) text.removeFromRight (90.0f);

    auto top = text.removeFromTop (18.0f);
    g.setFont (font (10.0f, true));
    g.drawText (kindName (suggestion.kind), top.removeFromLeft (70.0f), juce::Justification::centredLeft);
    g.setColour (colours::text);
    g.setFont (font (13.0f, true));
    g.drawText (juce::String (suggestion.title), top, juce::Justification::centredLeft, true);

    g.setColour (colours::dim);
    g.setFont (font (12.0f));
    g.drawFittedText (juce::String (suggestion.detail), text.toNearestInt(), juce::Justification::topLeft, 2);
}

//==============================================================================
AssistantPanel::AssistantPanel (PolarisProcessor& p, std::function<void (const polaris::AnalysisResult&)> onNewAnalysis)
    : proc (p), onAnalysis (std::move (onNewAnalysis))
{
    listenButton.setTooltip ("Play the loudest 20-30 s of your song, then press Listen. Press again to stop early.");
    referenceButton.setTooltip ("Play a finished track you like through Polaris to use it as the tonal target.");

    listenButton.onClick = [this]
    {
        if (proc.getListenMode() == PolarisProcessor::ListenMode::Idle) proc.startListening (PolarisProcessor::ListenMode::Mix);
        else proc.stopListening();
    };
    referenceButton.onClick = [this]
    {
        if (proc.getListenMode() == PolarisProcessor::ListenMode::Idle) proc.startListening (PolarisProcessor::ListenMode::Reference);
        else proc.stopListening();
    };
    applyAllButton.onClick = [this] { for (auto& c : cards) c->apply(); };
    applyAllButton.setEnabled (false);

    status.setText ("Press Listen while your song plays.", juce::dontSendNotification);
    status.setFont (font (13.0f));
    summary.setFont (font (11.5f));
    summary.setColour (juce::Label::textColourId, colours::dim);

    for (auto* c : std::initializer_list<juce::Component*> { &listenButton, &referenceButton, &applyAllButton, &status, &summary, &viewport })
        addAndMakeVisible (c);

    viewport.setViewedComponent (&cardHolder, false);
    viewport.setScrollBarsShown (true, false);

    // show the last analysis again when the editor is reopened
    const auto last = proc.getLastAnalysis();
    if (last.valid) showResult (last);
}

void AssistantPanel::poll()
{
    const auto mode = proc.getListenMode();
    const double secs = proc.getListenSeconds();
    const juce::String progress = juce::String (juce::roundToInt (secs)) + " / " + juce::String (juce::roundToInt (PolarisProcessor::kMaxListenSeconds)) + " s";

    listenButton.setButtonText (mode == PolarisProcessor::ListenMode::Mix ? "Stop  " + progress : "Listen");
    referenceButton.setButtonText (mode == PolarisProcessor::ListenMode::Reference ? "Stop  " + progress : "Learn reference");
    listenButton.setEnabled (mode != PolarisProcessor::ListenMode::Reference);
    referenceButton.setEnabled (mode != PolarisProcessor::ListenMode::Mix);
    if (mode == PolarisProcessor::ListenMode::Mix)       status.setText ("Listening to your mix...", juce::dontSendNotification);
    if (mode == PolarisProcessor::ListenMode::Reference) status.setText ("Learning the reference track...", juce::dontSendNotification);

    polaris::AnalysisResult result;
    PolarisProcessor::ListenMode finished = PolarisProcessor::ListenMode::Idle;
    if (! proc.takeFinishedAnalysis (result, finished)) return;

    if (finished == PolarisProcessor::ListenMode::Reference)
    {
        if (result.valid)
        {
            proc.applyChanges ({ { polaris::ids::style, 4.0f } });   // switch the style to "My reference"
            status.setText ("Reference learned (" + juce::String (juce::roundToInt (result.seconds)) + " s). Style set to My reference - now Listen to your mix.",
                            juce::dontSendNotification);
        }
        else status.setText ("No audio heard. Play the reference track and try again.", juce::dontSendNotification);
        return;
    }

    showResult (result);
}

void AssistantPanel::showResult (const polaris::AnalysisResult& result)
{
    const auto suggestions = polaris::makeSuggestions (result, proc.currentAssistantSettings());

    cards.clear();
    for (const auto& s : suggestions)
    {
        auto changes = s.changes;
        auto card = std::make_unique<Card> (s, [this, changes] { proc.applyChanges (changes); });
        cardHolder.addAndMakeVisible (*card);
        cards.push_back (std::move (card));
    }

    int actionable = 0;
    for (const auto& s : suggestions) if (! s.changes.empty()) ++actionable;
    applyAllButton.setEnabled (actionable > 0);
    status.setText (juce::String (actionable) + " suggestions. Apply what sounds right - every move stays editable.", juce::dontSendNotification);
    summary.setText (polaris::summarise (result), juce::dontSendNotification);
    layoutCards();

    if (onAnalysis) onAnalysis (result);
}

void AssistantPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void AssistantPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto top = area.removeFromTop (28);
    listenButton.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (6);
    referenceButton.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (6);
    applyAllButton.setBounds (top.removeFromRight (100));
    status.setBounds (top.reduced (8, 0));
    summary.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);
    viewport.setBounds (area);
    layoutCards();
}

void AssistantPanel::layoutCards()
{
    const int w = juce::jmax (100, viewport.getWidth() - viewport.getScrollBarThickness());
    const int cardH = 62;
    cardHolder.setSize (w, (int) cards.size() * cardH);
    for (size_t i = 0; i < cards.size(); ++i)
        cards[i]->setBounds (0, (int) i * cardH, w, cardH);
}
