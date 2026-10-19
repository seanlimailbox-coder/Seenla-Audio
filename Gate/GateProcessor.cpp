#include "GateProcessor.h"
#include "GateEditor.h"

namespace GateParameter
{
constexpr auto input = "input", threshold = "threshold", ratio = "ratio", knee = "knee";
constexpr auto hysteresis = "hysteresis", attack = "attack", hold = "hold", release = "release";
constexpr auto range = "range", mix = "mix", output = "output", link = "link";
constexpr auto sideHighPass = "sideHighPass", sideLowPass = "sideLowPass";
constexpr auto externalSidechain = "externalSidechain", lookahead = "lookahead";
constexpr auto lookaheadEnabled = "lookaheadEnabled", bypass = "bypass";
constexpr auto maxLookahead = "maxLookahead";
constexpr auto flip = "flip";
constexpr auto detectorMode = "detectorMode";
static constexpr float maximumTimes[] { 0, 1, 2, 5, 10, 20 };
}

OpenGateAudioProcessor::OpenGateAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, &undoManager, "OPEN_GATE", createParameterLayout())
{
    parameters.addParameterListener (GateParameter::lookaheadEnabled, this);
    parameters.addParameterListener (GateParameter::maxLookahead, this);
    parameters.addParameterListener (GateParameter::externalSidechain, this);
    parameters.addParameterListener (GateParameter::sideHighPass, this);
    parameters.addParameterListener (GateParameter::sideLowPass, this);
    updateLatency();
    parameters.copyState();
    undoManager.clearUndoHistory();
}

OpenGateAudioProcessor::~OpenGateAudioProcessor()
{
    cancelPendingUpdate();
    parameters.removeParameterListener (GateParameter::lookaheadEnabled, this);
    parameters.removeParameterListener (GateParameter::maxLookahead, this);
    parameters.removeParameterListener (GateParameter::externalSidechain, this);
    parameters.removeParameterListener (GateParameter::sideHighPass, this);
    parameters.removeParameterListener (GateParameter::sideLowPass, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout OpenGateAudioProcessor::createParameterLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto addFloat = [&] (const char* id, const char* name, float low, float high, float step,
                         float skew, float initial, const char* unit)
    {
        layout.add (std::make_unique<F> (juce::ParameterID { id, 1 }, name,
                                        juce::NormalisableRange<float> (low, high, step, skew),
                                        initial, unit));
    };

    // Keep the original parameter IDs so existing sessions retain their saved values.
    addFloat (GateParameter::input, "Input", -24.0f, 24.0f, 0.01f, 1.0f, 0.0f, "dB");
    addFloat (GateParameter::threshold, "Threshold", -80.0f, 0.0f, 0.01f, 1.0f, -12.0f, "dB");
    addFloat (GateParameter::hysteresis, "Hysteresis", 0.0f, 24.0f, 0.01f, 1.0f, 6.0f, "dB");
    addFloat (GateParameter::attack, "Attack", 0.01f, 200.0f, 0.01f, 0.3f, 1.0f, "ms");
    addFloat (GateParameter::hold, "Hold", 0.0f, 1000.0f, 0.1f, 0.4f, 2.0f, "ms");
    addFloat (GateParameter::release, "Release", 5.0f, 3000.0f, 0.1f, 0.35f, 150.0f, "ms");
    addFloat (GateParameter::range, "Range", 0.0f, 80.0f, 0.01f, 0.6f, 80.0f, "dB");
    addFloat (GateParameter::mix, "Mix", 0.0f, 100.0f, 0.1f, 1.0f, 100.0f, "%");
    addFloat (GateParameter::output, "Output", -24.0f, 24.0f, 0.01f, 1.0f, 0.0f, "dB");
    addFloat (GateParameter::link, "Stereo Link", 0.0f, 100.0f, 0.1f, 1.0f, 100.0f, "%");
    layout.add (std::make_unique<B> (juce::ParameterID { GateParameter::bypass, 1 }, "Bypass", false));

    addFloat (GateParameter::ratio, "Ratio", 1.0f, 100.0f, 0.01f, 0.35f, 100.0f, ":1");
    addFloat (GateParameter::knee, "Knee", 0.0f, 24.0f, 0.01f, 1.0f, 0.0f, "dB");
    const juce::NormalisableRange<float> cutoffRange { 20, 20000,
        [] (float a, float b, float n) { return a * std::pow (b / a, n); },
        [] (float a, float b, float hz) { return std::log (hz / a) / std::log (b / a); },
        [] (float a, float b, float hz) { return juce::jlimit (a, b, std::round (hz)); } };
    layout.add (std::make_unique<F> (juce::ParameterID { GateParameter::sideHighPass, 1 }, "Sidechain High Pass", cutoffRange, 20, "Hz"));
    layout.add (std::make_unique<F> (juce::ParameterID { GateParameter::sideLowPass, 1 }, "Sidechain Low Pass", cutoffRange, 20000, "Hz"));
    addFloat (GateParameter::lookahead, "Lookahead", 0.0f, 20.0f, 0.01f, 1.0f, 1.0f, "ms");
    layout.add (std::make_unique<B> (juce::ParameterID { GateParameter::externalSidechain, 1 },
                                    "External Sidechain", false));
    layout.add (std::make_unique<B> (juce::ParameterID { GateParameter::lookaheadEnabled, 1 },
                                    "Lookahead Enabled", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { GateParameter::maxLookahead, 1 }, "Maximum Lookahead",
        juce::StringArray { "0 ms", "1 ms", "2 ms", "5 ms", "10 ms", "20 ms" }, 1));
    layout.add (std::make_unique<B> (juce::ParameterID { GateParameter::flip, 1 }, "Flip", false));
    // Append: existing host parameter indices and IDs remain unchanged.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { GateParameter::detectorMode, 1 }, "Detection Mode",
        // Keep established automation values (Peak 0 / Smooth 1). The editor
        // presents Smooth first independently of these saved/host values.
        juce::StringArray { "Peak", "Smooth" }, 1));
    return layout;
}

void OpenGateAudioProcessor::parameterChanged (const juce::String& id, float value)
{
    if (id == GateParameter::externalSidechain && value > 0.5f)
        setSidechainHistoryVisible (true); // One enable event, not a continuously forced overlay.
    if (id == GateParameter::lookaheadEnabled || id == GateParameter::maxLookahead)
        updateLatency();
    if ((id == GateParameter::sideHighPass || id == GateParameter::sideLowPass) && ! recallingFilters.load())
    {
        auto* manager = juce::MessageManager::getInstanceWithoutCreating();
        const bool onMessageThread = manager && manager->isThisTheMessageThread();
        if (normalizingFilters.load() && onMessageThread) return;
        lastEditedHighPass = id == GateParameter::sideHighPass;
        filterNormalizationRequested = true;
        if (onMessageThread) normalizeSidechainFilters(); else triggerAsyncUpdate();
    }
}

OpenGateAudioProcessor::SidechainCutoffs OpenGateAudioProcessor::constrainSidechainCutoffs (float hp, float lp, bool editedHighPass) noexcept
{
    hp = std::isfinite (hp) ? juce::jlimit (20.0f, 20000.0f, std::round (hp)) : 20.0f;
    lp = std::isfinite (lp) ? juce::jlimit (20.0f, 20000.0f, std::round (lp)) : 20000.0f;
    if (editedHighPass)
    {
        lp = juce::jmax (lp, (float) std::ceil (20 * sidechainMinimumRatio));
        hp = juce::jmin (hp, (float) std::floor (lp / sidechainMinimumRatio));
    }
    else
    {
        hp = juce::jmin (hp, (float) std::floor (20000 / sidechainMinimumRatio));
        lp = juce::jmax (lp, (float) std::ceil (hp * sidechainMinimumRatio));
    }
    return { hp, lp };
}

OpenGateAudioProcessor::SidechainCutoffs OpenGateAudioProcessor::sidechainCutoffs() const noexcept
{
    return constrainSidechainCutoffs (parameters.getRawParameterValue (GateParameter::sideHighPass)->load(),
                                     parameters.getRawParameterValue (GateParameter::sideLowPass)->load(), lastEditedHighPass.load());
}

void OpenGateAudioProcessor::normalizeSidechainFilters()
{
    filterNormalizationRequested = false;
    if (normalizingFilters.exchange (true)) return;
    const auto pair = sidechainCutoffs();
    for (auto item : { std::pair<const char*, float> { GateParameter::sideHighPass, pair.highPass },
                      std::pair<const char*, float> { GateParameter::sideLowPass, pair.lowPass } })
    {
        auto* parameter = parameters.getParameter (item.first);
        if (std::abs (parameters.getRawParameterValue (item.first)->load() - item.second) > 0.5f)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (item.second));
    }
    normalizingFilters = false;
}

void OpenGateAudioProcessor::handleAsyncUpdate()
{
    if (! recallingFilters.load() && filterNormalizationRequested.exchange (false)) normalizeSidechainFilters();
}

void OpenGateAudioProcessor::normalizeFilterState (juce::ValueTree tree, bool editedHighPass)
{
    if (! tree.isValid()) return;
    const auto hp = tree.getChildWithProperty ("id", GateParameter::sideHighPass);
    const auto lp = tree.getChildWithProperty ("id", GateParameter::sideLowPass);
    const auto pair = constrainSidechainCutoffs (hp.isValid() ? (float) hp["value"] : 20,
                                               lp.isValid() ? (float) lp["value"] : 20000, editedHighPass);
    for (auto item : { std::pair<const char*, float> { GateParameter::sideHighPass, pair.highPass },
                      std::pair<const char*, float> { GateParameter::sideLowPass, pair.lowPass } })
    {
        auto node = tree.getChildWithProperty ("id", item.first);
        if (! node.isValid()) { node = juce::ValueTree ("PARAM"); node.setProperty ("id", item.first, nullptr); tree.addChild (node, -1, nullptr); }
        node.setProperty ("value", item.second, nullptr);
    }
}

void OpenGateAudioProcessor::updateLatency()
{
    const bool enabled = parameters.getRawParameterValue (GateParameter::lookaheadEnabled)->load() > 0.5f;
    const int index = juce::jlimit (0, 5, (int) parameters.getRawParameterValue (GateParameter::maxLookahead)->load());
    setLatencySamples (enabled ? juce::roundToInt (latencySampleRate.load() * GateParameter::maximumTimes[index] * 0.001) : 0);
}

void OpenGateAudioProcessor::prepareToPlay (double newSampleRate, int maximumBlockSize)
{
    // Hosts may re-prepare after a lookahead/latency or buffer-size change.
    // Keep completed visual frames and their sequence when their timebase agrees.
    const bool resetTimeline = historyEpoch.load() == 0 || ! juce::approximatelyEqual (sampleRate, newSampleRate);
    sampleRate = newSampleRate;
    historyBlockSamples.store (juce::jmax (1, maximumBlockSize));
    latencySampleRate.store (newSampleRate);
    maxLookaheadSamples = juce::roundToInt (sampleRate * 0.020);
    delayLength = maxLookaheadSamples + juce::jmax (maximumBlockSize, 1) + 2;
    delayBuffer.setSize (6, delayLength);
    delayBuffer.clear();
    delayWrite = 0;
    detectorFilters[0] = {};
    detectorFilters[1] = {};
    externalFilters[0] = {};
    externalFilters[1] = {};
    spectrumSequence = 0;
    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    wet.reset (sampleRate, 0.02);
    activeMix.reset (sampleRate, 0.005);
    auditionMix.reset (sampleRate, 0.005);
    removedMix.reset (sampleRate, 0.005);
    removedMix.setCurrentAndTargetValue (0.0f);
    auditionRemoved.store (false);
    activeMix.setCurrentAndTargetValue (parameters.getRawParameterValue (GateParameter::bypass)->load() > 0.5f ? 0.0f : 1.0f);
    auditionMix.setCurrentAndTargetValue (auditionSidechain.load() && ! isNonRealtime() ? 1.0f : 0.0f);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        parameters.getRawParameterValue (GateParameter::input)->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        parameters.getRawParameterValue (GateParameter::output)->load()));
    wet.setCurrentAndTargetValue (parameters.getRawParameterValue (GateParameter::mix)->load() * 0.01f);
    envelopeL = envelopeR = 0.0f;
    for (auto& detector : peakDetectors) detector.prepare (sampleRate);
    peakBlend.reset (sampleRate, 0.005);
    peakBlend.setCurrentAndTargetValue (parameters.getRawParameterValue (GateParameter::detectorMode)->load() < 0.5f ? 1.0f : 0.0f);
    const bool startsOpen = parameters.getRawParameterValue (GateParameter::flip)->load() > 0.5f
        || parameters.getRawParameterValue (GateParameter::ratio)->load() <= 1.0f;
    heldReductionL = heldReductionR = startsOpen ? 0.0f : parameters.getRawParameterValue (GateParameter::range)->load();
    gainDbL = gainDbR = -heldReductionL;
    openL = openR = startsOpen;
    gateOpenMeter = startsOpen;
    holdL = holdR = 0;
    attenuationMeter = maximumAttenuationMeter = inputMeter = outputMeter = 0.0f;
    if (resetTimeline)
    {
        for (auto& v : inputHistory) v = 0.0f;
        for (auto& v : outputHistory) v = 0.0f;
        for (auto& v : sidechainHistory) v = 0.0f;
        for (auto& v : attenuationHistory) v = 0.0f;
        for (auto& v : detectorHistoryL) v = 0.0f;
        for (auto& v : detectorHistoryR) v = 0.0f;
        for (auto& v : detectorPeakHistory) v = false;
        for (auto& v : frameSequences) v = 0;
        historySequence = 0;
        historyWrite = 0;
    }
    historyDetectorL = historyDetectorR = 0;
    historyInputPeak = historyOutputPeak = historySidechainPeak = historyAttenuationPeak = 0.0f;
    externalSidechainActive = false;
    historyInterval = juce::jmax (1, juce::roundToInt (sampleRate / 60.0));
    historyCountdown = historyInterval;
    if (resetTimeline) historyEpoch.fetch_add (1, std::memory_order_release);
    lastHistoryCaptureState = getHistoryCaptureState();
    updateLatency();
}

bool OpenGateAudioProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto mainInput = layout.getMainInputChannelSet();
    const auto output = layout.getMainOutputChannelSet();
    const auto sidechain = layout.inputBuses.size() > 1 ? layout.inputBuses[1]
                                                        : juce::AudioChannelSet::disabled();
    return mainInput == juce::AudioChannelSet::stereo()
        && output == mainInput
        && (sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono()
                                   || sidechain == juce::AudioChannelSet::stereo());
}

float OpenGateAudioProcessor::DetectorFilter::process (float sample,
    const CompressorFilters::Cascade& highPassCoefficients,
    const CompressorFilters::Cascade& lowPassCoefficients,
    bool highPassEnabled, bool lowPassEnabled) noexcept
{
    // Match Compressor's double-precision trapezoidal state-variable sections.
    // Their integrator states remain well behaved across large cutoff changes.
    double value = sample;
    const auto apply = [&] (const auto& coefficients, auto& states, bool highPass)
    {
        for (size_t i = 0; i < 2; ++i)
        {
            const auto& c = coefficients.integrator[i]; auto& s = states[i];
            const double v3 = value - s[1], v1 = c[0] * s[0] + c[1] * v3;
            const double v2 = s[1] + c[1] * s[0] + c[2] * v3;
            s[0] = 2 * v1 - s[0]; s[1] = 2 * v2 - s[1];
            value = highPass ? value - c[3] * v1 - v2 : v2;
        }
    };
    if (highPassEnabled) apply (highPassCoefficients, highPassState, true);
    else for (auto& state : highPassState) state[0] = state[1] = 0;
    if (lowPassEnabled) apply (lowPassCoefficients, lowPassState, false);
    else for (auto& state : lowPassState) state[0] = state[1] = 0;
    return (float) value;
}

double OpenGateAudioProcessor::detectorResponseDb (double frequency) const noexcept
{
    // Use Compressor's exact digital fourth-order Butterworth response.
    const double rate = latencySampleRate.load();
    if (! std::isfinite (frequency)) frequency = 0;
    frequency = juce::jmax (0.0, frequency);
    const auto cutoffs = sidechainCutoffs();
    const double hp = cutoffs.highPass, lp = cutoffs.lowPass;
    double magnitude = 1.0;
    if (lp < 19999.0 && frequency >= rate * 0.5) return -120.0;
    frequency = juce::jmin (frequency, rate * 0.5);
    if (hp > 20.5) magnitude *= CompressorFilters::magnitude (CompressorFilters::design (rate, hp, true), frequency, rate);
    if (lp < 19999.0) magnitude *= CompressorFilters::magnitude (CompressorFilters::design (rate, lp, false), frequency, rate);
    return 20.0 * std::log10 (juce::jmax (1.0e-6, magnitude));
}

float OpenGateAudioProcessor::expansionReduction (float levelDb, float thresholdDb, float ratio,
                                                  float kneeDb, float rangeDb) noexcept
{
    const float deficit = thresholdDb - levelDb;
    const float slope = ratio - 1.0f;
    if (slope <= 0.0f || rangeDb <= 0.0f)
        return 0.0f;
    if (kneeDb <= 0.0f)
        return juce::jlimit (0.0f, rangeDb, slope * deficit);
    if (deficit <= -kneeDb * 0.5f)
        return 0.0f;
    if (deficit >= kneeDb * 0.5f)
        return juce::jmin (rangeDb, slope * deficit);
    const float inside = deficit + kneeDb * 0.5f;
    return juce::jmin (rangeDb, slope * inside * inside / (2.0f * kneeDb));
}

void OpenGateAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedValueSetter<bool> hostBypass (hostBypassed, true);
    activeMix.setCurrentAndTargetValue (0.0f);
    processBlock (buffer, midi);
}

void OpenGateAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);
    if (buffer.getNumChannels() < 2 || delayLength == 0)
        return;
    historyBlockSamples.store (juce::jmax (1, buffer.getNumSamples()), std::memory_order_relaxed);
    // Display-only capture is selected once per block, never gating the audio algorithm.
    const auto captureState = getHistoryCaptureState();
    const bool captureHistory = (captureState & 1) != 0;
    if (captureState != lastHistoryCaptureState)
    {
        historyInputPeak = historyOutputPeak = historySidechainPeak = historyAttenuationPeak = 0;
        historyDetectorL = historyDetectorR = 0;
        historyCountdown = historyInterval.load();
        lastHistoryCaptureState = captureState;
    }

    auto main = getBusBuffer (buffer, true, 0);
    auto side = getBusBuffer (buffer, true, 1);
    const bool useExternal = parameters.getRawParameterValue (GateParameter::externalSidechain)->load() > 0.5f
                             && side.getNumChannels() > 0;
    externalSidechainActive = useExternal;
    auto value = [&] (const char* id) { return parameters.getRawParameterValue (id)->load(); };
    const float threshold = value (GateParameter::threshold);
    const bool flipped = value (GateParameter::flip) > 0.5f;
    const float direction = flipped ? -1.0f : 1.0f;
    const float directionalThreshold = threshold * direction;
    const float closeThreshold = directionalThreshold - value (GateParameter::hysteresis);
    const float ratio = value (GateParameter::ratio), knee = value (GateParameter::knee);
    const float range = value (GateParameter::range);
    const int holdSamples = juce::roundToInt (sampleRate * value (GateParameter::hold) * 0.001);
    holdL = juce::jmin (holdL, holdSamples);
    holdR = juce::jmin (holdR, holdSamples);
    const float stereoLink = value (GateParameter::link) * 0.01f;
    const bool bypassed = hostBypassed || value (GateParameter::bypass) > 0.5f;
    if (bypassed || isNonRealtime())
    {
        auditionSidechain.store (false);
        auditionRemoved.store (false);
    }
    if (isNonRealtime())
    {
        // Offline entry must not print even the live monitor's fade-out samples.
        auditionMix.setCurrentAndTargetValue (0.0f);
        removedMix.setCurrentAndTargetValue (0.0f);
    }
    const bool audition = auditionSidechain.load() && ! isNonRealtime();
    const bool removed = auditionRemoved.load() && ! audition && ! isNonRealtime();
    const bool lookaheadOn = value (GateParameter::lookaheadEnabled) > 0.5f;
    const int maximumIndex = juce::jlimit (0, 5, (int) value (GateParameter::maxLookahead));
    const int latency = lookaheadOn ? juce::roundToInt (sampleRate * GateParameter::maximumTimes[maximumIndex] * 0.001) : 0;
    const int preOpen = lookaheadOn ? juce::jlimit (0, latency,
        juce::roundToInt (sampleRate * value (GateParameter::lookahead) * 0.001)) : 0;
    const float detectorAttack = std::exp (-1.0f / (float) (sampleRate * 0.0005));
    const float detectorRelease = std::exp (-1.0f / (float) (sampleRate * 0.030));
    peakBlend.setTargetValue (value (GateParameter::detectorMode) < 0.5f ? 1.0f : 0.0f);
    const float openCoefficient = std::exp (-1.0f / (float) (sampleRate * value (GateParameter::attack) * 0.001));
    const float closeCoefficient = std::exp (-1.0f / (float) (sampleRate * value (GateParameter::release) * 0.001));
    const auto cutoffs = sidechainCutoffs();
    const float highPassHz = cutoffs.highPass, lowPassHz = cutoffs.lowPass;
    const bool highPassEnabled = highPassHz > 20.5f;
    const bool lowPassEnabled = lowPassHz < 19999.0f;
    // Stack-only coefficient design once per host block, exactly as in Compressor.
    const auto highPassCoefficient = CompressorFilters::design (sampleRate, highPassHz, true);
    const auto lowPassCoefficient = CompressorFilters::design (sampleRate, lowPassHz, false);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (value (GateParameter::input)));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (value (GateParameter::output)));
    wet.setTargetValue (value (GateParameter::mix) * 0.01f);
    activeMix.setTargetValue (bypassed ? 0.0f : 1.0f);
    auditionMix.setTargetValue (audition && ! bypassed ? 1.0f : 0.0f);
    removedMix.setTargetValue (removed && ! bypassed ? 1.0f : 0.0f);

    const float meterDecay = std::exp (-1.0f / (float) (sampleRate * 0.3));
    float meterInput = inputMeter.load(), meterOutput = outputMeter.load(), meterAttenuation = attenuationMeter.load();
    float blockAttenuation = 0.0f;
    const auto finite = [] (float x) { return std::isfinite (x) ? x : 0.0f; };
    int spectrumStart1 = 0, spectrumSize1 = 0, spectrumStart2 = 0, spectrumSize2 = 0;
    if (spectrumEnabled.load (std::memory_order_relaxed))
        spectrumFifo.prepareToWrite (main.getNumSamples(), spectrumStart1, spectrumSize1, spectrumStart2, spectrumSize2);
    const int spectrumCount = spectrumSize1 + spectrumSize2;
    const auto spectrumEpoch = getHistoryEpoch();
    for (int sample = 0; sample < main.getNumSamples(); ++sample)
    {
        const float inGain = inputGain.getNextValue();
        const float outGain = outputGain.getNextValue();
        const float mix = wet.getNextValue();
        const float sourceL = finite (main.getSample (0, sample));
        const float sourceR = finite (main.getSample (1, sample));
        const float gainedL = finite (sourceL * inGain), gainedR = finite (sourceR * inGain);
        const float sideL = side.getNumChannels() > 0 ? finite (side.getSample (0, sample)) : 0.0f;
        const float sideR = side.getNumChannels() > 0
            ? finite (side.getSample (juce::jmin (1, side.getNumChannels() - 1), sample)) : 0.0f;
        // Separate internal/external state matches Compressor's routing: the
        // external key stays warm without inheriting internal-filter memory.
        const float externalL = externalFilters[0].process (sideL, highPassCoefficient, lowPassCoefficient, highPassEnabled, lowPassEnabled);
        const float externalR = externalFilters[1].process (sideR, highPassCoefficient, lowPassCoefficient, highPassEnabled, lowPassEnabled);
        const float keyL = useExternal ? externalL
            : detectorFilters[0].process (gainedL, highPassCoefficient, lowPassCoefficient, highPassEnabled, lowPassEnabled);
        const float keyR = useExternal ? externalR
            : detectorFilters[1].process (gainedR, highPassCoefficient, lowPassCoefficient, highPassEnabled, lowPassEnabled);
        if (sample < spectrumCount)
        {
            const int at = sample < spectrumSize1 ? spectrumStart1 + sample : spectrumStart2 + sample - spectrumSize1;
            spectrumFrames[(size_t) at] = { keyL, keyR, spectrumEpoch, spectrumSequence + (uint64_t) sample + 1, captureState };
        }
        delayBuffer.setSample (0, delayWrite, sourceL);
        delayBuffer.setSample (1, delayWrite, sourceR);
        delayBuffer.setSample (2, delayWrite, keyL);
        delayBuffer.setSample (3, delayWrite, keyR);
        delayBuffer.setSample (4, delayWrite, gainedL);
        delayBuffer.setSample (5, delayWrite, gainedR);
        const int programRead = (delayWrite - latency + delayLength) % delayLength;
        const int detectorRead = (delayWrite - (latency - preOpen) + delayLength) % delayLength;
        const float delayedL = delayBuffer.getSample (0, programRead);
        const float delayedR = delayBuffer.getSample (1, programRead);
        const float detectorL = delayBuffer.getSample (2, detectorRead);
        const float detectorR = delayBuffer.getSample (3, detectorRead);
        delayWrite = (delayWrite + 1) % delayLength;

        auto follow = [&] (float key, float& envelope)
        {
            const float magnitude = std::abs (key);
            const float coefficient = magnitude > envelope ? detectorAttack : detectorRelease;
            envelope = coefficient * envelope + (1.0f - coefficient) * magnitude;
        };
        follow (detectorL, envelopeL);
        follow (detectorR, envelopeR);
        // Run both followers continuously. Automation crossfades current levels,
        // never resumes stale detector state or imposes an attack on new Peak instances.
        const float blend = peakBlend.getNextValue();
        const float peakL = peakDetectors[0].process (detectorL);
        const float peakR = peakDetectors[1].process (detectorR);
        const float selectedL = envelopeL + (peakL - envelopeL) * blend;
        const float selectedR = envelopeR + (peakR - envelopeR) * blend;
        const float linked = juce::jmax (selectedL, selectedR);
        const float levelL = selectedL + (linked - selectedL) * stereoLink;
        const float levelR = selectedR + (linked - selectedR) * stereoLink;

        auto updateGate = [&] (float level, bool& isOpen, int& hold, float& gainDb, float& heldReduction)
        {
            // Mirror detector levels around the threshold for a true reverse gate.
            const float db = direction * juce::Decibels::gainToDecibels (level, -100.0f);
            bool holding = false;
            if (db >= directionalThreshold)
            {
                isOpen = true;
                hold = holdSamples;
            }
            else if (isOpen && db < closeThreshold)
            {
                if (hold > 0) { --hold; holding = true; }
                else isOpen = false;
            }
            else if (isOpen)
            {
                hold = holdSamples;
            }

            const float effectiveThreshold = isOpen ? closeThreshold : directionalThreshold;
            const float candidate = expansionReduction (db, effectiveThreshold, ratio, knee, range);
            const float reduction = holding ? juce::jmin (candidate, heldReduction) : candidate;
            heldReduction = reduction;
            const float target = -reduction;
            const float coefficient = target > gainDb ? openCoefficient : closeCoefficient;
            gainDb = coefficient * gainDb + (1.0f - coefficient) * target;
        };
        updateGate (levelL, openL, holdL, gainDbL, heldReductionL);
        updateGate (levelR, openR, holdR, gainDbR, heldReductionR);

        const float dryL = delayBuffer.getSample (4, programRead);
        const float dryR = delayBuffer.getSample (5, programRead);
        const float linkedGain = flipped ? juce::jmin (gainDbL, gainDbR) : juce::jmax (gainDbL, gainDbR);
        const float appliedL = gainDbL + stereoLink * (linkedGain - gainDbL);
        const float appliedR = gainDbR + stereoLink * (linkedGain - gainDbR);
        const float wetL = dryL * juce::Decibels::decibelsToGain (appliedL);
        const float wetR = dryR * juce::Decibels::decibelsToGain (appliedR);
        const float processedL = (dryL + (wetL - dryL) * mix) * outGain;
        const float processedR = (dryR + (wetR - dryR) * mix) * outGain;
        const float active = activeMix.getNextValue(), listen = auditionMix.getNextValue();
        const float removedBlend = removedMix.getNextValue();
        // Audition only the gate's removed component, not input/output gain differences.
        // Include Mix so this is the material actually removed by the gate stage.
        const float normalL = processedL + removedBlend * ((dryL - wetL) * mix - processedL);
        const float normalR = processedR + removedBlend * ((dryR - wetR) * mix - processedR);
        const float monitoredL = (1.0f - listen) * normalL + listen * delayBuffer.getSample (2, programRead);
        const float monitoredR = (1.0f - listen) * normalR + listen * delayBuffer.getSample (3, programRead);
        const float outputL = finite ((1.0f - active) * delayedL + active * monitoredL);
        const float outputR = finite ((1.0f - active) * delayedR + active * monitoredR);
        main.setSample (0, sample, outputL);
        main.setSample (1, sample, outputR);

        const float attenuation = active * (1.0f - listen) * juce::jmax (-appliedL, -appliedR);
        meterInput = juce::jmax (meterInput * meterDecay, std::abs (dryL), std::abs (dryR));
        meterOutput = juce::jmax (meterOutput * meterDecay, std::abs (outputL), std::abs (outputR));
        meterAttenuation = juce::jmax (meterAttenuation * meterDecay, attenuation);
        blockAttenuation = juce::jmax (blockAttenuation, attenuation);
        if (captureHistory)
        {
            historyInputPeak = juce::jmax (historyInputPeak, std::abs (dryL), std::abs (dryR));
            historyOutputPeak = juce::jmax (historyOutputPeak, std::abs (outputL), std::abs (outputR));
            historySidechainPeak = juce::jmax (historySidechainPeak, std::abs (delayBuffer.getSample (2, programRead)),
                                              std::abs (delayBuffer.getSample (3, programRead)));
            historyAttenuationPeak = juce::jmax (historyAttenuationPeak, attenuation);
            historyDetectorL = juce::jmax (historyDetectorL, levelL);
            historyDetectorR = juce::jmax (historyDetectorR, levelR);
            if (--historyCountdown <= 0)
            {
                const int position = historyWrite.load (std::memory_order_relaxed);
                const auto sequence = historySequence.load (std::memory_order_relaxed) + 1;
                frameSequences[(size_t) position].store (0, std::memory_order_release);
                inputHistory[(size_t) position] = historyInputPeak;
                outputHistory[(size_t) position] = historyOutputPeak;
                sidechainHistory[(size_t) position] = historySidechainPeak;
                attenuationHistory[(size_t) position] = historyAttenuationPeak;
                detectorHistoryL[(size_t) position] = historyDetectorL;
                detectorHistoryR[(size_t) position] = historyDetectorR;
                detectorPeakHistory[(size_t) position] = blend >= 0.5f;
                frameCaptureStates[(size_t) position].store (captureState, std::memory_order_relaxed);
                frameSequences[(size_t) position].store (sequence, std::memory_order_release);
                historyWrite.store ((position + 1) & (historyCapacity - 1), std::memory_order_release);
                historySequence.store (sequence, std::memory_order_release);
                historyInputPeak = historyOutputPeak = historySidechainPeak = historyAttenuationPeak = 0.0f;
                historyCountdown = historyInterval;
                historyDetectorL = historyDetectorR = 0;
            }
        }
    }

    spectrumFifo.finishedWrite (spectrumCount);
    spectrumSequence += (uint64_t) main.getNumSamples();
    inputMeter = meterInput;
    outputMeter = meterOutput;
    attenuationMeter = meterAttenuation;
    gateOpenMeter = openL || openR;
    auto previous = maximumAttenuationMeter.load();
    while (blockAttenuation > previous && ! maximumAttenuationMeter.compare_exchange_weak (previous, blockAttenuation)) {}
}

bool OpenGateAudioProcessor::readSpectrumFrame (SpectrumFrame& frame) noexcept
{
    int start1, size1, start2, size2;
    spectrumFifo.prepareToRead (1, start1, size1, start2, size2);
    if (size1 + size2 == 0) return false;
    frame = spectrumFrames[(size_t) (size1 > 0 ? start1 : start2)];
    spectrumFifo.finishedRead (1);
    return true;
}

bool OpenGateAudioProcessor::readHistoryFrame (uint64_t sequence, HistoryFrame& frame, uint64_t captureState) const noexcept
{
    if (sequence == 0) return false;
    const auto index = (size_t) ((sequence - 1) & (historyCapacity - 1));
    if (frameSequences[index].load (std::memory_order_acquire) != sequence) return false;
    if (captureState != 0 && frameCaptureStates[index].load (std::memory_order_relaxed) != captureState)
    {
        // An old visit (including a late audio publication) is silence in the new viewport.
        frame = {};
        return frameSequences[index].load (std::memory_order_acquire) == sequence;
    }
    HistoryFrame result { inputHistory[index].load(), outputHistory[index].load(), sidechainHistory[index].load(),
                          detectorHistoryL[index].load(), detectorHistoryR[index].load(), attenuationHistory[index].load(),
                          detectorPeakHistory[index].load() };
    if (frameSequences[index].load (std::memory_order_acquire) != sequence) return false;
    frame = result;
    return true;
}

void OpenGateAudioProcessor::selectSnapshot (int slot)
{
    const juce::ScopedLock lock (comparisonLock);
    slot = juce::jlimit (0, 1, slot);
    if (slot == snapshotSlot) return;
    auditionRemoved = false;
    auditionSidechain = false;
    snapshots[(size_t) snapshotSlot] = parameters.copyState();
    normalizeFilterState (snapshots[(size_t) snapshotSlot], lastEditedHighPass.load());
    if (! snapshots[(size_t) slot].isValid()) snapshots[(size_t) slot] = snapshots[(size_t) snapshotSlot].createCopy();
    undoManager.beginNewTransaction ("Compare Gate settings");
    snapshotSlot = slot;
    normalizeFilterState (snapshots[(size_t) slot]);
    recallingFilters = true; filterNormalizationRequested = false;
    for (auto* raw : getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (raw))
        {
            // A/B is a sonic comparison; keep transport/bypass and host latency unchanged.
            if (parameter->paramID == GateParameter::lookaheadEnabled
                || parameter->paramID == GateParameter::maxLookahead
                || parameter->paramID == GateParameter::bypass) continue;
            const auto node = snapshots[(size_t) slot].getChildWithProperty ("id", parameter->paramID);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (node.isValid() ? parameter->convertTo0to1 ((float) node["value"])
                                                             : parameter->getDefaultValue());
            parameter->endChangeGesture();
        }
    recallingFilters = false; lastEditedHighPass = true; normalizeSidechainFilters();
    parameters.copyState();
    undoManager.beginNewTransaction();
}

void OpenGateAudioProcessor::copySnapshot()
{
    const juce::ScopedLock lock (comparisonLock);
    snapshots[(size_t) (1 - snapshotSlot)] = parameters.copyState();
    normalizeFilterState (snapshots[(size_t) (1 - snapshotSlot)], lastEditedHighPass.load());
}

void OpenGateAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    const juce::ScopedLock lock (comparisonLock);
    auto state = parameters.copyState();
    normalizeFilterState (state, lastEditedHighPass.load());
    snapshots[(size_t) snapshotSlot] = state.createCopy();
    state.setProperty ("activeSnapshot", snapshotSlot.load(), nullptr);
    juce::ValueTree comparison ("COMPARISON");
    for (int slot = 0; slot < 2; ++slot)
    {
        auto saved = snapshots[(size_t) slot].isValid() ? snapshots[(size_t) slot].createCopy() : state.createCopy();
        normalizeFilterState (saved);
        saved.removeChild (saved.getChildWithName ("COMPARISON"), nullptr);
        comparison.addChild (saved, -1, nullptr);
    }
    state.addChild (comparison, -1, nullptr);
    juce::ValueTree editor ("EDITOR");
    editor.setProperty ("page", juce::jlimit (0, 2, editorPage.load()), nullptr);
    const auto overlay = historyOverlayMode();
    editor.setProperty ("showSC", overlay == HistoryOverlay::sidechain, nullptr);
    editor.setProperty ("detector", overlay == HistoryOverlay::detector, nullptr);
    editor.setProperty ("hoverHelp", hoverHelpEnabled.load(), nullptr);
    state.addChild (editor, -1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void OpenGateAudioProcessor::setStateInformation (const void* data, int size)
{
    const juce::ScopedLock lock (comparisonLock);
    auditionSidechain.store (false);
    auditionRemoved.store (false);
    recallingFilters = true; cancelPendingUpdate(); filterNormalizationRequested = false; lastEditedHighPass = true;
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            const auto editor = state.getChildWithName ("EDITOR");
            state.removeChild (editor, nullptr);
            snapshotSlot = juce::jlimit (0, 1, (int) state.getProperty ("activeSnapshot", 0));
            const auto comparison = state.getChildWithName ("COMPARISON");
            for (int slot = 0; slot < 2; ++slot) snapshots[(size_t) slot] = comparison.getChild (slot).createCopy();
            state.removeChild (comparison, nullptr);
            state.removeProperty ("activeSnapshot", nullptr);
            // Old main states AND their A/B slots used the fixed follower. Do not
            // silently reinterpret a saved threshold with the new Peak default.
            const auto migrateDetector = [] (juce::ValueTree tree)
            {
                if (tree.isValid() && ! tree.getChildWithProperty ("id", GateParameter::detectorMode).isValid())
                {
                    juce::ValueTree mode ("PARAM");
                    mode.setProperty ("id", GateParameter::detectorMode, nullptr);
                    mode.setProperty ("value", 1.0f, nullptr);
                    tree.addChild (mode, -1, nullptr);
                }
            };
            migrateDetector (state);
            for (auto& snapshot : snapshots) migrateDetector (snapshot);
            normalizeFilterState (state);
            for (auto& snapshot : snapshots) normalizeFilterState (snapshot);
            if (! state.getChildWithProperty ("id", GateParameter::maxLookahead).isValid())
            {
                juce::ValueTree maximum ("PARAM");
                maximum.setProperty ("id", GateParameter::maxLookahead, nullptr);
                maximum.setProperty ("value", 4.0f, nullptr); // Older sessions used a fixed 10 ms maximum.
                state.addChild (maximum, -1, nullptr);
            }
            if (! state.getChildWithProperty ("id", GateParameter::flip).isValid())
            {
                juce::ValueTree mode ("PARAM");
                mode.setProperty ("id", GateParameter::flip, nullptr);
                mode.setProperty ("value", 0.0f, nullptr);
                state.addChild (mode, -1, nullptr);
            }
            parameters.replaceState (state);
            // Restore explicit saved choices after parameter listeners. Loading an
            // already-enabled external key is not a new user enable gesture.
            // Old versions allowed both: prefer SHOW SC, matching Compressor.
            historyOverlay.store ((bool) editor.getProperty ("showSC", false) ? HistoryOverlay::sidechain
                : (bool) editor.getProperty ("detector", false) ? HistoryOverlay::detector : HistoryOverlay::none);
            hoverHelpEnabled = (bool) editor.getProperty ("hoverHelp", false);
            editorPage = juce::jlimit (0, 2, (int) editor.getProperty ("page", 0));
            undoManager.clearUndoHistory();
        }
    recallingFilters = false;
    updateLatency();
}

juce::AudioProcessorEditor* OpenGateAudioProcessor::createEditor()
{
    return new OpenGateAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpenGateAudioProcessor();
}
