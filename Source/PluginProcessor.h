#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "core/MasterChain.h"
#include "core/Analyzer.h"
#include "core/Assistant.h"
#include "core/LoudnessMeter.h"
#include "core/SpscFifo.h"
#include "core/FFT.h"
#include "core/ParamHistory.h"
#include <mutex>

class PolarisProcessor : public juce::AudioProcessor,
                         private juce::Thread
{
public:
    PolarisProcessor();
    ~PolarisProcessor() override;

    // ---- juce::AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                 { return true; }
    const juce::String getName() const override     { return JucePlugin_Name; }
    bool acceptsMidi() const override               { return false; }
    bool producesMidi() const override              { return false; }
    bool isMidiEffect() const override              { return false; }
    double getTailLengthSeconds() const override    { return 0.0; }
    int getNumPrograms() override                   { return 1; }
    int getCurrentProgram() override                { return 0; }
    void setCurrentProgram (int) override           {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- parameters
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    polaris::ChainSettings readSettings() const;

    /** Apply assistant suggestions as host-visible parameter changes (message thread). */
    void applyChanges (const std::vector<polaris::ParamChange>& changes);

    // ---- meters (lock-free reads for the UI)
    struct Meters
    {
        std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f }, integrated { -100.0f }, lra { 0.0f };
        std::atomic<float> truePeak { -100.0f }, inShortTerm { -100.0f };
        std::atomic<float> limiterGr { 0.0f }, glueGr { 0.0f };
    };
    Meters meters;
    void requestMeterReset() { meterResetRequested = true; }

    // ---- spectrum for display (post-chain, dB per bin)
    static constexpr int kDisplayFftOrder = 12;
    void copySpectrum (std::vector<float>& dest) const;
    double getAnalysisSampleRate() const { return analysisSampleRate.load(); }

    // ---- Listen / assistant
    enum class ListenMode { Idle = 0, Mix = 1, Reference = 2 };
    static constexpr double kMaxListenSeconds = 30.0;
    void startListening (ListenMode mode);
    void stopListening()                      { stopRequested = true; }
    ListenMode getListenMode() const          { return (ListenMode) activeListen.load(); }
    double getListenSeconds() const           { return listenSeconds.load(); }
    bool takeFinishedAnalysis (polaris::AnalysisResult& result, ListenMode& mode);

    std::vector<float> getReferenceCurve() const;
    polaris::AnalysisResult getLastAnalysis() const;
    polaris::AssistantSettings currentAssistantSettings() const;

    std::atomic<bool> uiProMode { false };

    // ---- undo/redo and A/B/C/D snapshots (message thread only)
    static constexpr int kNumSlots = 4;
    void historyTick();                  // call regularly from the editor's timer
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    void selectSlot (int slot);          // stores the current sound in the active slot, then loads `slot`
    int getActiveSlot() const;
    bool isSlotUsed (int slot) const;
    void copyActiveSlotTo (int slot);    // copy the current sound into another slot without switching

private:
    polaris::StateSnapshot captureState() const;
    std::vector<float> captureValues() const;
    void applyValues (const std::vector<float>& values);
    juce::String slotsToString() const;
    void slotsFromString (const juce::String& text);

    std::vector<juce::RangedAudioParameter*> snapshotParams;   // every parameter except Compare
    polaris::HistoryRecorder recorder;
    mutable std::mutex slotMutex;
    std::array<std::vector<float>, kNumSlots> slotValues;
    int activeSlot = 0;
    std::atomic<bool> historyResetRequested { true };

    void run() override;   // analysis thread
    void updateSpectrum (const float* left, const float* right, int n);

    struct ParamPtrs
    {
        std::atomic<float>* inputGain = nullptr; std::atomic<float>* drive = nullptr;
        std::atomic<float>* target = nullptr; std::atomic<float>* character = nullptr;
        std::atomic<float>* style = nullptr; std::atomic<float>* compare = nullptr;
        std::atomic<float>* eqOn = nullptr;
        std::array<std::atomic<float>*, polaris::CorrectiveEq::kBands> eqFreq {}, eqGain {}, eqQ {};
        std::atomic<float>* glueOn = nullptr; std::atomic<float>* glueXLow = nullptr; std::atomic<float>* glueXHigh = nullptr;
        std::atomic<float>* glueThreshold = nullptr; std::atomic<float>* glueRatio = nullptr;
        std::atomic<float>* glueAttack = nullptr; std::atomic<float>* glueRelease = nullptr;
        std::atomic<float>* satOn = nullptr; std::atomic<float>* satDrive = nullptr; std::atomic<float>* satMix = nullptr;
        std::atomic<float>* stereoOn = nullptr; std::atomic<float>* monoFreq = nullptr; std::atomic<float>* width = nullptr;
        std::atomic<float>* limOn = nullptr; std::atomic<float>* limGain = nullptr;
        std::atomic<float>* limCeiling = nullptr; std::atomic<float>* limRelease = nullptr;
    };
    ParamPtrs p;

    // audio thread
    polaris::MasterChain chain;
    static constexpr int kChunk = 1024;
    std::vector<float> interleave;           // 2 * kChunk
    std::vector<float> dryL, dryR;           // dry delay ring for the gain-matched compare
    int dryWrite = 0, latency = 0;
    polaris::Smoother compareMix, compareGain;

    // audio -> analysis
    polaris::SpscFifo<float> inFifo, outFifo;
    std::atomic<double> analysisSampleRate { 0.0 };
    std::atomic<float> compareGainDb { 0.0f };

    // analysis thread
    polaris::LoudnessMeter inMeter, outMeter;
    polaris::TruePeakDetector outTpL, outTpR;
    float truePeakHold = 0.0f;
    polaris::ListenAnalyzer listener;
    polaris::FFT displayFft { kDisplayFftOrder };
    std::vector<float> specMid, specPower, specSmoothed;
    int specPos = 0;
    std::atomic<bool> meterResetRequested { false };

    mutable juce::SpinLock spectrumLock;
    std::vector<float> displaySpectrum;

    std::atomic<int> listenRequest { 0 }, activeListen { 0 };
    std::atomic<bool> stopRequested { false };
    std::atomic<double> listenSeconds { 0.0 };

    mutable std::mutex resultMutex;
    bool hasFinished = false;
    ListenMode finishedMode = ListenMode::Idle;
    polaris::AnalysisResult finishedResult, lastAnalysis;
    std::vector<float> referenceCurve;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolarisProcessor)
};
