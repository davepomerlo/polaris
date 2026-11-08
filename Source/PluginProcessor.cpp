#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/ParamIds.h"
#include <cstring>

namespace ids = polaris::ids;

//==============================================================================
PolarisProcessor::PolarisProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Polaris analysis"),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    auto raw = [this] (const std::string& id)
    {
        auto* v = apvts.getRawParameterValue (juce::String (id));
        jassert (v != nullptr);
        return v;
    };

    p.inputGain = raw (ids::inputGain);   p.drive = raw (ids::drive);
    p.target = raw (ids::target);         p.character = raw (ids::character);
    p.style = raw (ids::style);           p.compare = raw (ids::compare);
    p.eqOn = raw (ids::eqOn);
    for (int b = 0; b < polaris::CorrectiveEq::kBands; ++b)
    {
        p.eqFreq[(size_t) b] = raw (ids::eqFreq (b));
        p.eqGain[(size_t) b] = raw (ids::eqGain (b));
        p.eqQ[(size_t) b]    = raw (ids::eqQ (b));
    }
    p.glueOn = raw (ids::glueOn);               p.glueXLow = raw (ids::glueXLow);
    p.glueXHigh = raw (ids::glueXHigh);         p.glueThreshold = raw (ids::glueThreshold);
    p.glueRatio = raw (ids::glueRatio);         p.glueAttack = raw (ids::glueAttack);
    p.glueRelease = raw (ids::glueRelease);
    p.satOn = raw (ids::satOn);                 p.satDrive = raw (ids::satDrive);   p.satMix = raw (ids::satMix);
    p.stereoOn = raw (ids::stereoOn);           p.monoFreq = raw (ids::monoFreq);   p.width = raw (ids::width);
    p.limOn = raw (ids::limOn);                 p.limGain = raw (ids::limGain);
    p.limCeiling = raw (ids::limCeiling);       p.limRelease = raw (ids::limRelease);

    // Undo history and A/B/C/D snapshots cover every parameter except the Compare listening switch.
    for (auto* prm : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (ranged->paramID != juce::String (ids::compare))
                snapshotParams.push_back (ranged);
}

PolarisProcessor::~PolarisProcessor()
{
    stopThread (2000);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PolarisProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout] (const std::string& id, const juce::String& name, float min, float max,
                               float def, const juce::String& unit, float centre = 0.0f, float step = 0.0f)
    {
        juce::NormalisableRange<float> range (min, max, step);
        if (centre > 0.0f) range.setSkewForCentre (centre);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { juce::String (id), 1 }, name, range, def,
                                                                 juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addBool = [&layout] (const std::string& id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { juce::String (id), 1 }, name, def));
    };
    auto addChoice = [&layout] (const std::string& id, const juce::String& name, const juce::StringArray& choices, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { juce::String (id), 1 }, name, choices, def));
    };

    // global
    addFloat (ids::inputGain, "Input", -24.0f, 24.0f, 0.0f, "dB");
    addFloat (ids::drive, "Drive", 0.0f, 100.0f, 0.0f, "%");
    addChoice (ids::target, "Target", { "Streaming (-14 LUFS)", "Loud (-9 LUFS)", "Club (-7 LUFS)" }, 0);
    addChoice (ids::character, "Character", { "Clean", "Warm", "Punchy" }, 0);
    addChoice (ids::style, "Style", { "Pop", "Hip-hop / EDM", "Rock", "Acoustic / Jazz", "My reference" }, 0);
    addBool (ids::compare, "Compare (gain-matched bypass)", false);

    // EQ
    addBool (ids::eqOn, "EQ On", true);
    const float defFreq[] = { 60.0f, 200.0f, 500.0f, 1500.0f, 5000.0f, 12000.0f };
    for (int b = 0; b < polaris::CorrectiveEq::kBands; ++b)
    {
        const juce::String n = "EQ " + juce::String (b + 1) + (b == 0 ? " (low shelf)" : b == 5 ? " (high shelf)" : "");
        addFloat (ids::eqFreq (b), n + " Freq", 20.0f, 20000.0f, defFreq[b], "Hz", 1000.0f);
        addFloat (ids::eqGain (b), n + " Gain", -12.0f, 12.0f, 0.0f, "dB");
        addFloat (ids::eqQ (b), n + " Q", 0.3f, 6.0f, (b == 0 || b == 5) ? 0.7f : 1.0f, "", 1.0f);
    }

    // glue
    addBool (ids::glueOn, "Glue On", true);
    addFloat (ids::glueXLow, "Glue Low X-over", 40.0f, 500.0f, 120.0f, "Hz", 150.0f);
    addFloat (ids::glueXHigh, "Glue High X-over", 1000.0f, 10000.0f, 3000.0f, "Hz", 3000.0f);
    addFloat (ids::glueThreshold, "Glue Threshold", -40.0f, 0.0f, -12.0f, "dB");
    addFloat (ids::glueRatio, "Glue Ratio", 1.0f, 6.0f, 2.0f, ":1", 2.0f);
    addFloat (ids::glueAttack, "Glue Attack", 0.1f, 100.0f, 30.0f, "ms", 10.0f);
    addFloat (ids::glueRelease, "Glue Release", 20.0f, 2000.0f, 200.0f, "ms", 200.0f);

    // saturation
    addBool (ids::satOn, "Saturation On", true);
    addFloat (ids::satDrive, "Saturation Drive", 0.0f, 24.0f, 0.0f, "dB");
    addFloat (ids::satMix, "Saturation Mix", 0.0f, 100.0f, 100.0f, "%");

    // stereo
    addBool (ids::stereoOn, "Stereo On", true);
    addFloat (ids::monoFreq, "Mono Bass Below", 20.0f, 300.0f, 20.0f, "Hz", 80.0f);
    addFloat (ids::width, "Width", 0.0f, 200.0f, 100.0f, "%");

    // limiter
    addBool (ids::limOn, "Limiter On", true);
    addFloat (ids::limGain, "Limiter Gain", 0.0f, 24.0f, 0.0f, "dB");
    addFloat (ids::limCeiling, "Ceiling", -3.0f, 0.0f, -1.0f, "dBTP");
    addFloat (ids::limRelease, "Limiter Release", 10.0f, 1000.0f, 100.0f, "ms", 100.0f);

    return layout;
}

polaris::ChainSettings PolarisProcessor::readSettings() const
{
    polaris::ChainSettings s;
    s.inputGainDb = p.inputGain->load();
    s.drive = p.drive->load() * 0.01f;
    s.character = (int) p.character->load();

    s.eqOn = p.eqOn->load() > 0.5f;
    for (size_t b = 0; b < s.eq.size(); ++b)
        s.eq[b] = { p.eqFreq[b]->load(), p.eqGain[b]->load(), p.eqQ[b]->load() };

    s.glueOn = p.glueOn->load() > 0.5f;
    s.glueXLow = p.glueXLow->load();
    s.glueXHigh = p.glueXHigh->load();
    s.glueThresholdDb = p.glueThreshold->load();
    s.glueRatio = p.glueRatio->load();
    s.glueAttackMs = p.glueAttack->load();
    s.glueReleaseMs = p.glueRelease->load();

    s.satOn = p.satOn->load() > 0.5f;
    s.satDriveDb = p.satDrive->load();
    s.satMix = p.satMix->load() * 0.01f;

    s.stereoOn = p.stereoOn->load() > 0.5f;
    s.monoFreq = p.monoFreq->load();
    s.width = p.width->load() * 0.01f;

    s.limOn = p.limOn->load() > 0.5f;
    s.limGainDb = p.limGain->load();
    s.limCeilingDb = p.limCeiling->load();
    s.limReleaseMs = p.limRelease->load();
    return s;
}

void PolarisProcessor::applyChanges (const std::vector<polaris::ParamChange>& changes)
{
    for (const auto& c : changes)
    {
        if (auto* param = apvts.getParameter (juce::String (c.paramId)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (c.value));
            param->endChangeGesture();
        }
    }
}

//==============================================================================
void PolarisProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    stopThread (2000);   // the analysis objects are re-prepared below
    listenRequest = 0;   // a Listen pass in progress is cancelled by a restart
    activeListen = 0;
    stopRequested = false;

    chain.prepare (sampleRate);
    chain.setSettings (readSettings());
    chain.reset();

    latency = chain.getLatency();
    setLatencySamples (latency);

    interleave.assign (2 * kChunk, 0.0f);
    dryL.assign ((size_t) (latency + kChunk + 1), 0.0f);
    dryR.assign ((size_t) (latency + kChunk + 1), 0.0f);
    dryWrite = 0;
    compareMix.prepare (sampleRate, 30.0, p.compare->load() > 0.5f ? 1.0 : 0.0);
    compareGain.prepare (sampleRate, 300.0, 1.0);

    const auto fifoSize = (size_t) (sampleRate * 2.0);   // ~1 s of stereo audio
    inFifo.prepare (fifoSize);
    outFifo.prepare (fifoSize);

    inMeter.prepare (sampleRate);
    outMeter.prepare (sampleRate);
    outTpL.reset(); outTpR.reset();
    truePeakHold = 0.0f;
    listener.prepare (sampleRate);

    const int n = displayFft.size();
    specMid.assign ((size_t) n, 0.0f);
    specSmoothed.assign ((size_t) n / 2 + 1, -120.0f);
    specPos = 0;
    {
        const juce::SpinLock::ScopedLockType lock (spectrumLock);
        displaySpectrum.assign ((size_t) n / 2 + 1, -120.0f);
    }

    analysisSampleRate = sampleRate;
    startThread();
}

void PolarisProcessor::releaseResources()
{
    stopThread (2000);
}

bool PolarisProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void PolarisProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples == 0 || interleave.empty()) return;

    chain.setSettings (readSettings());
    compareMix.setTarget (p.compare->load() > 0.5f ? 1.0 : 0.0);
    compareGain.setTarget (polaris::dbToGain (compareGainDb.load()));

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
    const int ringSize = (int) dryL.size();

    // Hosts (FL Studio included) may send any block size, so work in fixed chunks.
    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int n = std::min (kChunk, numSamples - start);
        float* l = left + start;
        float* r = right + start;

        for (int i = 0; i < n; ++i)
        {
            interleave[(size_t) (2 * i)] = l[i];
            interleave[(size_t) (2 * i + 1)] = r[i];
            const int w = (dryWrite + i) % ringSize;
            dryL[(size_t) w] = l[i];
            dryR[(size_t) w] = r[i];
        }
        inFifo.pushIfRoom (interleave.data(), (size_t) (2 * n));

        chain.process (l, r, n);

        for (int i = 0; i < n; ++i)
        {
            const double m = compareMix.next();
            const double g = compareGain.next();
            if (m > 1.0e-4)
            {
                const int rd = ((dryWrite + i - latency) % ringSize + ringSize) % ringSize;
                l[i] = (float) ((1.0 - m) * l[i] + m * g * dryL[(size_t) rd]);
                r[i] = (float) ((1.0 - m) * r[i] + m * g * dryR[(size_t) rd]);
            }
            interleave[(size_t) (2 * i)] = l[i];
            interleave[(size_t) (2 * i + 1)] = r[i];
        }
        outFifo.pushIfRoom (interleave.data(), (size_t) (2 * n));
        dryWrite = (dryWrite + n) % ringSize;
    }

    meters.limiterGr = chain.getLimiterGrDb();
    meters.glueGr = chain.getGlueGrDb();
}

//==============================================================================
// Analysis thread: meters, spectrum, Listen. Never touches the audio thread's objects.
void PolarisProcessor::run()
{
    constexpr int kFrames = 4096;
    std::vector<float> buf (2 * kFrames), l (kFrames), r (kFrames);
    int listening = 0;

    auto deinterleave = [&] (size_t got)
    {
        const int frames = (int) got / 2;
        for (int i = 0; i < frames; ++i) { l[(size_t) i] = buf[(size_t) (2 * i)]; r[(size_t) i] = buf[(size_t) (2 * i + 1)]; }
        return frames;
    };

    while (! threadShouldExit())
    {
        if (meterResetRequested.exchange (false))
        {
            outMeter.reset();
            truePeakHold = 0.0f;
        }

        if (const int req = listenRequest.exchange (0); req != 0)
        {
            listener.reset();
            listening = req;
            stopRequested = false;
        }

        // ---- input side: input loudness (for compare) and Listen
        for (size_t got; (got = inFifo.pop (buf.data(), buf.size())) > 0;)
        {
            const int frames = deinterleave (got);
            inMeter.process (l.data(), r.data(), frames);
            if (listening != 0)
                listener.process (l.data(), r.data(), frames);
        }

        // ---- output side: meters and spectrum
        for (size_t got; (got = outFifo.pop (buf.data(), buf.size())) > 0;)
        {
            const int frames = deinterleave (got);
            outMeter.process (l.data(), r.data(), frames);
            for (int i = 0; i < frames; ++i)
                truePeakHold = std::max (truePeakHold, std::max (outTpL.process (l[(size_t) i]), outTpR.process (r[(size_t) i])));
            updateSpectrum (l.data(), r.data(), frames);
        }

        meters.momentary = outMeter.momentaryLufs();
        meters.shortTerm = outMeter.shortTermLufs();
        meters.integrated = outMeter.integratedLufs();
        meters.lra = outMeter.loudnessRange();
        meters.truePeak = (float) polaris::gainToDb (truePeakHold);
        meters.inShortTerm = inMeter.shortTermLufs();

        const float inSt = inMeter.shortTermLufs(), outSt = outMeter.shortTermLufs();
        compareGainDb = (inSt > -70.0f && outSt > -70.0f) ? juce::jlimit (-24.0f, 24.0f, outSt - inSt) : 0.0f;

        // ---- finish a Listen pass
        if (listening != 0)
        {
            listenSeconds = listener.seconds();
            if (stopRequested.exchange (false) || listener.seconds() >= kMaxListenSeconds)
            {
                auto result = listener.finish();
                {
                    const std::lock_guard<std::mutex> lock (resultMutex);
                    finishedResult = result;
                    finishedMode = (ListenMode) listening;
                    hasFinished = true;
                    if (finishedMode == ListenMode::Reference && result.valid) referenceCurve = result.bandLevelsDb;
                    if (finishedMode == ListenMode::Mix) lastAnalysis = result;
                }
                listening = 0;
                activeListen = 0;
            }
        }

        wait (20);
    }
}

void PolarisProcessor::updateSpectrum (const float* left, const float* right, int n)
{
    const int size = displayFft.size();
    const int hop = size / 4;
    bool updated = false;

    for (int i = 0; i < n; ++i)
    {
        specMid[(size_t) specPos++] = 0.5f * (left[i] + right[i]);
        if (specPos == size)
        {
            displayFft.powerSpectrum (specMid.data(), specPower);
            for (size_t k = 0; k < specPower.size(); ++k)
            {
                const float db = 10.0f * std::log10 (specPower[k] + 1.0e-20f);
                float& s = specSmoothed[k];
                s = db > s ? 0.5f * s + 0.5f * db : 0.85f * s + 0.15f * db;
            }
            std::memmove (specMid.data(), specMid.data() + hop, sizeof (float) * (size_t) (size - hop));
            specPos = size - hop;
            updated = true;
        }
    }

    if (updated)
    {
        const juce::SpinLock::ScopedLockType lock (spectrumLock);
        displaySpectrum = specSmoothed;
    }
}

void PolarisProcessor::copySpectrum (std::vector<float>& dest) const
{
    const juce::SpinLock::ScopedLockType lock (spectrumLock);
    dest = displaySpectrum;
}

//==============================================================================
void PolarisProcessor::startListening (ListenMode mode)
{
    listenSeconds = 0.0;
    stopRequested = false;
    activeListen = (int) mode;
    listenRequest = (int) mode;
}

bool PolarisProcessor::takeFinishedAnalysis (polaris::AnalysisResult& result, ListenMode& mode)
{
    const std::lock_guard<std::mutex> lock (resultMutex);
    if (! hasFinished) return false;
    hasFinished = false;
    result = finishedResult;
    mode = finishedMode;
    return true;
}

std::vector<float> PolarisProcessor::getReferenceCurve() const
{
    const std::lock_guard<std::mutex> lock (resultMutex);
    return referenceCurve;
}

polaris::AnalysisResult PolarisProcessor::getLastAnalysis() const
{
    const std::lock_guard<std::mutex> lock (resultMutex);
    return lastAnalysis;
}

polaris::AssistantSettings PolarisProcessor::currentAssistantSettings() const
{
    polaris::AssistantSettings s;
    s.style = (polaris::Style) juce::jlimit (0, 4, (int) p.style->load());
    s.targetLufs = ids::targetLufsForIndex ((int) p.target->load());
    s.ceilingDb = s.targetLufs <= -12.0f ? -1.0f : -0.5f;
    s.drive = p.drive->load() * 0.01f;
    s.referenceCurve = getReferenceCurve();
    return s;
}

//==============================================================================
void PolarisProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    juce::StringArray curve;
    for (float v : getReferenceCurve()) curve.add (juce::String (v, 2));
    state.setProperty ("referenceCurve", curve.joinIntoString (","), nullptr);
    state.setProperty ("proMode", uiProMode.load(), nullptr);
    state.setProperty ("snapshots", slotsToString(), nullptr);
    state.setProperty ("activeSlot", getActiveSlot(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PolarisProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xml);

    std::vector<float> curve;
    const auto tokens = juce::StringArray::fromTokens (tree.getProperty ("referenceCurve").toString(), ",", "");
    for (const auto& t : tokens)
        if (t.isNotEmpty()) curve.push_back (t.getFloatValue());
    {
        const std::lock_guard<std::mutex> lock (resultMutex);
        referenceCurve = curve;
    }
    uiProMode = (bool) tree.getProperty ("proMode", false);

    apvts.replaceState (tree);

    slotsFromString (tree.getProperty ("snapshots").toString());
    {
        const std::lock_guard<std::mutex> lock (slotMutex);
        activeSlot = juce::jlimit (0, kNumSlots - 1, (int) tree.getProperty ("activeSlot", 0));
    }
    historyResetRequested = true;   // loading a project is not an undoable edit
}

//==============================================================================
// Undo/redo and snapshots. All of this runs on the message thread (editor timer and buttons).

std::vector<float> PolarisProcessor::captureValues() const
{
    std::vector<float> v;
    v.reserve (snapshotParams.size());
    for (auto* prm : snapshotParams) v.push_back (prm->getValue());
    return v;
}

polaris::StateSnapshot PolarisProcessor::captureState() const
{
    return { captureValues(), getActiveSlot() };
}

void PolarisProcessor::applyValues (const std::vector<float>& values)
{
    if (values.size() != snapshotParams.size()) return;
    for (size_t i = 0; i < values.size(); ++i)
    {
        auto* prm = snapshotParams[i];
        if (std::abs (prm->getValue() - values[i]) < 1.0e-6f) continue;
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (values[i]);
        prm->endChangeGesture();
    }
}

void PolarisProcessor::historyTick()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const auto state = captureState();
    if (historyResetRequested.exchange (false) || recorder.history.empty())
    {
        recorder.reset (state, now);
        return;
    }
    recorder.tick (state, now);
}

bool PolarisProcessor::canUndo() const { return recorder.history.canUndo() || recorder.hasPendingEdit(); }
bool PolarisProcessor::canRedo() const { return recorder.history.canRedo() && ! recorder.hasPendingEdit(); }

void PolarisProcessor::undo()
{
    recorder.flushPending();   // a quick edit that hasn't settled yet is what gets undone
    if (! recorder.history.canUndo()) return;
    const auto s = recorder.history.undo();
    {
        const std::lock_guard<std::mutex> lock (slotMutex);
        activeSlot = s.slot;
    }
    applyValues (s.values);
    recorder.markApplied (s, juce::Time::getMillisecondCounterHiRes());
}

void PolarisProcessor::redo()
{
    if (! canRedo()) return;
    const auto s = recorder.history.redo();
    {
        const std::lock_guard<std::mutex> lock (slotMutex);
        activeSlot = s.slot;
    }
    applyValues (s.values);
    recorder.markApplied (s, juce::Time::getMillisecondCounterHiRes());
}

int PolarisProcessor::getActiveSlot() const
{
    const std::lock_guard<std::mutex> lock (slotMutex);
    return activeSlot;
}

bool PolarisProcessor::isSlotUsed (int slot) const
{
    const std::lock_guard<std::mutex> lock (slotMutex);
    return slot == activeSlot || ! slotValues[(size_t) slot].empty();
}

void PolarisProcessor::selectSlot (int slot)
{
    slot = juce::jlimit (0, kNumSlots - 1, slot);
    recorder.flushPending();
    std::vector<float> target;
    {
        const std::lock_guard<std::mutex> lock (slotMutex);
        if (slot == activeSlot) return;
        slotValues[(size_t) activeSlot] = captureValues();                      // keep what you were hearing
        if (slotValues[(size_t) slot].empty())
            slotValues[(size_t) slot] = slotValues[(size_t) activeSlot];       // an empty slot starts as a copy
        activeSlot = slot;
        target = slotValues[(size_t) slot];
    }
    applyValues (target);
    // the history recorder picks up the new slot and values on its next tick as one undo step
}

void PolarisProcessor::copyActiveSlotTo (int slot)
{
    slot = juce::jlimit (0, kNumSlots - 1, slot);
    const auto values = captureValues();
    const std::lock_guard<std::mutex> lock (slotMutex);
    if (slot != activeSlot) slotValues[(size_t) slot] = values;
}

juce::String PolarisProcessor::slotsToString() const
{
    // "id:value,id:value|id:value,...|..." - stored by parameter ID so older projects survive new parameters
    const auto live = captureValues();
    const std::lock_guard<std::mutex> lock (slotMutex);
    juce::StringArray slots;
    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto& values = s == activeSlot ? live : slotValues[(size_t) s];
        juce::StringArray entries;
        if (values.size() == snapshotParams.size())
            for (size_t i = 0; i < values.size(); ++i)
                entries.add (snapshotParams[i]->paramID + ":" + juce::String (values[i], 6));
        slots.add (entries.joinIntoString (","));
    }
    return slots.joinIntoString ("|");
}

void PolarisProcessor::slotsFromString (const juce::String& text)
{
    const auto slots = juce::StringArray::fromTokens (text, "|", "");
    std::array<std::vector<float>, kNumSlots> parsed;

    for (int s = 0; s < juce::jmin (kNumSlots, slots.size()); ++s)
    {
        if (slots[s].isEmpty()) continue;
        auto values = captureValues();   // parameters missing from the saved slot keep their current value
        for (const auto& entry : juce::StringArray::fromTokens (slots[s], ",", ""))
        {
            const auto id = entry.upToFirstOccurrenceOf (":", false, false);
            const float v = entry.fromFirstOccurrenceOf (":", false, false).getFloatValue();
            for (size_t i = 0; i < snapshotParams.size(); ++i)
                if (snapshotParams[i]->paramID == id) { values[i] = juce::jlimit (0.0f, 1.0f, v); break; }
        }
        parsed[(size_t) s] = std::move (values);
    }

    const std::lock_guard<std::mutex> lock (slotMutex);
    slotValues = std::move (parsed);
}

//==============================================================================
juce::AudioProcessorEditor* PolarisProcessor::createEditor()
{
    return new PolarisEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolarisProcessor();
}
