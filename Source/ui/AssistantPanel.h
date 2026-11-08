#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

/** Listen / Learn reference buttons and the suggestion cards (Apply, Apply all). */
class AssistantPanel : public juce::Component
{
public:
    AssistantPanel (PolarisProcessor& p, std::function<void (const polaris::AnalysisResult&)> onNewAnalysis);

    void poll();   // called from the editor's timer
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class Card : public juce::Component
    {
    public:
        Card (const polaris::Suggestion& s, std::function<void()> onApply);
        void paint (juce::Graphics& g) override;
        void resized() override;
        void markApplied();
        bool canApply() const { return hasChanges && ! applied; }
        void apply() { if (canApply() && onApplyFn) { onApplyFn(); markApplied(); } }

    private:
        polaris::Suggestion suggestion;
        juce::TextButton applyButton { "Apply" };
        std::function<void()> onApplyFn;
        bool hasChanges = false, applied = false;
    };

    void showResult (const polaris::AnalysisResult& result);
    void layoutCards();

    PolarisProcessor& proc;
    std::function<void (const polaris::AnalysisResult&)> onAnalysis;

    juce::TextButton listenButton { "Listen" }, referenceButton { "Learn reference" }, applyAllButton { "Apply all" };
    juce::Label status, summary;
    juce::Viewport viewport;
    juce::Component cardHolder;
    std::vector<std::unique_ptr<Card>> cards;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssistantPanel)
};
