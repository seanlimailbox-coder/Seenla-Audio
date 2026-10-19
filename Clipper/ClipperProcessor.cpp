#include "ClipperProcessor.h"
#include "ClipperEditor.h"

namespace ClipParam
{
    constexpr auto input = "input";
    constexpr auto threshold = "threshold";
    constexpr auto softness = "softness";
    constexpr auto ceiling = "ceiling";
    constexpr auto mix = "mix";
    constexpr auto output = "output";
    constexpr auto bypass = "bypass";
    constexpr auto smoothMode = "smoothMode";
}

namespace
{
// Keep every parameter and its original index/ID for preset/session recall.
// Only performance controls belong in the host's automation/configure list;
// quality, routing and secondary setup remain editable in our own editor.
template <typename Parameter>
class ClipperParameter final : public Parameter
{
public:
    using Parameter::Parameter;

    bool isAutomatable() const override
    {
        const auto& id = this->paramID;
        return id == "input" || id == "threshold" || id == "softness"
            || id == "ceiling" || id == "mix" || id == "output" || id == "bypass"
            || id == "hardKnee" || id == "lowDepth" || id == "midDepth" || id == "highDepth";
    }
};

float gainFromDb (float decibels) noexcept
{
    return std::abs (decibels) < 0.0001f ? 1.0f : juce::Decibels::decibelsToGain (decibels);
}

float proportionFromPercent (float percent) noexcept
{
    if (percent <= 0.0001f) return 0.0f;
    if (percent >= 99.9999f) return 1.0f;
    return percent * 0.01f;
}

void migrateModeLink (juce::ValueTree state)
{
    if (! state.isValid() || state.getChildWithProperty ("id", "msLink").isValid()) return;
    // Old presets had a shared Link value. Seed both modes with it so recall
    // sounds identical, then allow independent edits from this version onward.
    const auto legacyLink = state.getChildWithProperty ("id", "stereoLink");
    juce::ValueTree node ("PARAM");
    node.setProperty ("id", "msLink", nullptr);
    node.setProperty ("value", legacyLink.isValid() ? (float) legacyLink["value"] : 100.0f, nullptr);
    state.addChild (node, -1, nullptr);
}

void migrateClipping24 (juce::ValueTree state)
{
    for (const auto* id : { "hardKnee", "lowKnee", "midKnee", "highKnee", "crossover", "linearPhase" })
        if (! state.getChildWithProperty ("id", id).isValid())
        {
            juce::ValueTree node ("PARAM");
            node.setProperty ("id", id, nullptr);
            node.setProperty ("value", 0.0f, nullptr);
            state.addChild (node, -1, nullptr);
        }
}
}

OpenClipperAudioProcessor::OpenClipperAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, &undoManager, "PARAMETERS", createParameterLayout()),
      oversampling (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true)
{
    rawParameters = { parameters.getRawParameterValue ("input"), parameters.getRawParameterValue ("threshold"),
        parameters.getRawParameterValue ("softness"), parameters.getRawParameterValue ("ceiling"),
        parameters.getRawParameterValue ("mix"), parameters.getRawParameterValue ("output"),
        parameters.getRawParameterValue ("bypass"), parameters.getRawParameterValue ("smoothMode"),
        parameters.getRawParameterValue ("liveQuality"), parameters.getRawParameterValue ("renderQuality"),
        parameters.getRawParameterValue ("linearPhase"), parameters.getRawParameterValue ("truePeak"),
        parameters.getRawParameterValue ("release"), parameters.getRawParameterValue ("channelMode"),
        parameters.getRawParameterValue ("msLink"), parameters.getRawParameterValue ("stereoLink"),
        parameters.getRawParameterValue ("hardKnee"), parameters.getRawParameterValue ("crossover"),
        parameters.getRawParameterValue ("lowSplit"), parameters.getRawParameterValue ("highSplit"),
        parameters.getRawParameterValue ("bands"), parameters.getRawParameterValue ("curve"),
        parameters.getRawParameterValue ("dcFilter"), parameters.getRawParameterValue ("msBias"), parameters.getRawParameterValue ("gainLink"),
        parameters.getRawParameterValue ("lowRate") };
    for (size_t q = 0; q < 3; ++q)
    {
        linearOversampling[q] = std::make_unique<juce::dsp::Oversampling<float>> (2, q+2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        linearFinalOversampling[q] = std::make_unique<juce::dsp::Oversampling<float>> (2, q+2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    }
    for (auto& sample : scopeInput)  sample.store (0.0f);
    for (auto& sample : scopeOutput) sample.store (0.0f);
    for (auto& sample : scopeReduction) sample.store (0.0f);
    for (auto& mode : scopeReductionIsSmooth) mode.store (false);
    int b = 0;
    for (const auto* name : { "low", "mid", "high" })
    {
        const juce::String prefix (name);
        bandParameters[(size_t) b++] = { parameters.getRawParameterValue (prefix+"Enabled"), parameters.getRawParameterValue (prefix+"Mix"),
            parameters.getRawParameterValue (prefix+"Curve"), parameters.getRawParameterValue (prefix+"Softness"), parameters.getRawParameterValue (prefix+"Trim"), parameters.getRawParameterValue (prefix+"Knee"), parameters.getRawParameterValue (prefix+"Depth") };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout OpenClipperAudioProcessor::createParameterLayout()
{
    using APF = ClipperParameter<juce::AudioParameterFloat>;
    using APB = ClipperParameter<juce::AudioParameterBool>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<APF> (juce::ParameterID { ClipParam::input, 1 }, "Input",
                                       juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f, 1.0f), 0.0f, "dB"));
    layout.add (std::make_unique<APF> (juce::ParameterID { ClipParam::threshold, 1 }, "Threshold",
                                       juce::NormalisableRange<float> (-36.0f, 0.0f, 0.01f, 1.0f), 0.0f, "dB"));
    layout.add (std::make_unique<APF> (juce::ParameterID { ClipParam::softness, 1 }, "Softness",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 1.0f), 15.0f, "%"));
    layout.add (std::make_unique<APF> (juce::ParameterID { ClipParam::ceiling, 1 }, "Final Limit",
                                       juce::NormalisableRange<float> (-24.0f, 0.0f, 0.01f, 1.0f), 0.0f, "dB"));
    layout.add (std::make_unique<APF> (juce::ParameterID { ClipParam::mix, 1 }, "Mix",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 1.0f), 100.0f, "%"));
    layout.add (std::make_unique<APF> (juce::ParameterID { ClipParam::output, 1 }, "Output Gain",
                                       juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f, 1.0f), 0.0f, "dB"));
    layout.add (std::make_unique<APB> (juce::ParameterID { ClipParam::bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<APB> (juce::ParameterID { ClipParam::smoothMode, 1 }, "Smooth Mode", false));
    using APC = ClipperParameter<juce::AudioParameterChoice>;
    layout.add (std::make_unique<APC> (juce::ParameterID { "liveQuality", 1 }, "Playback Oversampling", juce::StringArray { "4x", "8x", "16x" }, 0));
    layout.add (std::make_unique<APC> (juce::ParameterID { "renderQuality", 1 }, "Render Oversampling", juce::StringArray { "Same as playback", "8x", "16x" }, 0));
    layout.add (std::make_unique<APC> (juce::ParameterID { "curve", 1 }, "Smooth Curve", juce::StringArray { "Classic", "Cubic" }, 0));
    layout.add (std::make_unique<APC> (juce::ParameterID { "channelMode", 1 }, "Channel Processing", juce::StringArray { "Stereo L/R", "Mid / Side" }, 0));
    layout.add (std::make_unique<APF> (juce::ParameterID { "msBias", 1 }, "Mid Side Clipping Bias",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, "dB"));
    layout.add (std::make_unique<APB> (juce::ParameterID { "bands", 1 }, "Three Band Processing", false));
    layout.add (std::make_unique<APF> (juce::ParameterID { "lowSplit", 1 }, "Low Crossover", juce::NormalisableRange<float> (80.0f, 1500.0f, 1.0f, 0.4f), 250.0f, "Hz"));
    layout.add (std::make_unique<APF> (juce::ParameterID { "highSplit", 1 }, "High Crossover", juce::NormalisableRange<float> (2000.0f, 12000.0f, 1.0f, 0.5f), 4000.0f, "Hz"));
    for (const auto* id : { "lowDepth", "midDepth", "highDepth" })
        layout.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, juce::String (id).startsWith ("low") ? "Low Band Depth" : juce::String (id).startsWith ("mid") ? "Mid Band Depth" : "High Band Depth",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f, "dB"));
    layout.add (std::make_unique<APB> (juce::ParameterID { "gainLink", 1 }, "Link Input Output", false));
    layout.add (std::make_unique<APB> (juce::ParameterID { "truePeak", 1 }, "True Peak Safeguard", false));
    layout.add (std::make_unique<APF> (juce::ParameterID { "release", 1 }, "Smooth Release",
        juce::NormalisableRange<float> (1.0f, 100.0f, 0.1f, 0.45f), 5.0f, "ms"));
    layout.add (std::make_unique<APF> (juce::ParameterID { "stereoLink", 1 }, "Smooth Stereo Link",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, "%"));
    layout.add (std::make_unique<APF> (juce::ParameterID { "msLink", 1 }, "Smooth Mid Side Link",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, "%"));
    for (const auto* band : { "low", "mid", "high" })
    {
        const juce::String prefix (band), name = prefix.toUpperCase() + " Band ";
        layout.add (std::make_unique<APB> (juce::ParameterID { prefix + "Enabled", 1 }, name + "Enabled", true));
        layout.add (std::make_unique<APC> (juce::ParameterID { prefix + "Curve", 1 }, name + "Curve",
            juce::StringArray { "Follow Global", "Hard", "Classic", "Cubic" }, 0));
        layout.add (std::make_unique<APF> (juce::ParameterID { prefix + "Softness", 1 }, name + "Softness",
            juce::NormalisableRange<float> (0, 100, 0.1f), 15.0f, "%"));
        layout.add (std::make_unique<APF> (juce::ParameterID { prefix + "Mix", 1 }, name + "Mix",
            juce::NormalisableRange<float> (0, 100, 0.1f), 100.0f, "%"));
        layout.add (std::make_unique<APF> (juce::ParameterID { prefix + "Trim", 1 }, name + "Output",
            juce::NormalisableRange<float> (-12, 12, 0.01f), 0.0f, "dB"));
    }
    layout.add (std::make_unique<APB> (juce::ParameterID { "dcFilter", 1 }, "DC Filter", false));
    layout.add (std::make_unique<APF> (juce::ParameterID { "hardKnee", 1 }, "Hard Knee",
        juce::NormalisableRange<float> (0, 100, 0.1f), 0.0f, "%"));
    for (const auto* id : { "lowKnee", "midKnee", "highKnee" })
        layout.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, juce::String (id) + " Hard Knee",
            juce::NormalisableRange<float> (0, 100, 0.1f), 0.0f, "%"));
    layout.add (std::make_unique<APC> (juce::ParameterID { "crossover", 1 }, "Band Crossover",
        juce::StringArray { "Broad", "Focused 24" }, 1));
    layout.add (std::make_unique<APB> (juce::ParameterID { "linearPhase", 1 }, "Linear Phase Oversampling", false));
    // Append instead of expanding liveQuality: retain old normalized automation
    // (0/.5/1 = 4x/8x/16x) and every existing host parameter index.
    layout.add (std::make_unique<APC> (juce::ParameterID { "lowRate", 1 }, "Playback Base Rate",
        juce::StringArray { "Use quality", "Off", "2x" }, 0));
    return layout;
}

int OpenClipperAudioProcessor::requestedOversamplingFactor() const noexcept
{
    const int quality = juce::jlimit (0, 2, (int) rawParameters.liveQuality->load());
    const int baseRate = (int) rawParameters.lowRate->load();
    // LP remains 4x minimum, even if the host automates both controls directly.
    int factor = rawParameters.linearPhase->load() > .5f ? 4 << quality
        : baseRate == 1 ? 1 : baseRate == 2 ? 2 : 4 << quality;
    const int render = juce::jlimit (0, 2, (int) rawParameters.renderQuality->load());
    if (isNonRealtime() && render > 0) factor = juce::jmax (factor, 4 << render);
    return factor;
}

void OpenClipperAudioProcessor::selectOversampling (int factor, bool linearPhase) noexcept
{
    oversamplingFactor = factor;
    previousLinearPhase = linearPhase;
    if (linearPhase)
    {
        const size_t quality = factor == 4 ? 0 : factor == 8 ? 1 : 2;
        activeOversampling = linearOversampling[quality].get();
        finalOversampling = linearFinalOversampling[quality].get();
    }
    else
    {
        activeOversampling = factor == 1 ? &oversamplingOff : factor == 2 ? &oversampling2
            : factor == 4 ? &oversampling : factor == 8 ? &oversampling8 : &oversampling16;
        finalOversampling = factor == 1 ? &finalOversamplingOff : factor == 2 ? &finalOversampling2
            : factor == 4 ? &finalOversampling4 : factor == 8 ? &finalOversampling8 : &finalOversampling16;
    }
    processingLatencySamples = juce::roundToInt (activeOversampling->getLatencyInSamples());
    finalLatencySamples = juce::roundToInt (finalOversampling->getLatencyInSamples());
    displayedFactor.store (factor);
}

void OpenClipperAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool isSmoothMode = rawParameters.smoothMode->load() > 0.5f;
    // Hosts may reprepare when Hard/Smooth changes the reported latency.
    // Keep the eight-second display at the same sample rate across that call.
    const bool keepScopeHistory = hasPrepared && std::abs(sampleRate-lastPreparedSampleRate) < 0.001;
    maximumBlockSize = juce::jmax (1, samplesPerBlock);
    samplesPerBlock = maximumBlockSize;
    meterRelease = std::exp (-1.0f / static_cast<float> (sampleRate * 0.12));
    controlBuffer.setSize (3, samplesPerBlock * 16);
    oversampling.reset();
    oversampling.initProcessing (static_cast<size_t> (samplesPerBlock));
    oversampling8.initProcessing ((size_t) samplesPerBlock);
    oversampling16.initProcessing ((size_t) samplesPerBlock);
    oversampling8.reset(); oversampling16.reset();
    for (auto* engine : { &oversamplingOff, &oversampling2, &finalOversamplingOff, &finalOversampling2,
                          &finalOversampling4, &finalOversampling8, &finalOversampling16 })
    { engine->initProcessing ((size_t) samplesPerBlock); engine->reset(); }
    for (size_t q=0; q<3; ++q)
        for (auto* engine : { linearOversampling[q].get(), linearFinalOversampling[q].get() })
        { engine->initProcessing ((size_t) samplesPerBlock); engine->reset(); }
    selectOversampling (requestedOversamplingFactor(), rawParameters.linearPhase->load() > .5f);
    dryBuffer.setSize (2, samplesPerBlock, false, true, false);
    drivenDelayedBuffer.setSize (2, samplesPerBlock, false, true, false);
    rawDrivenDelayedBuffer.setSize (2, samplesPerBlock, false, true, false);
    gainReductionBuffer.setSize (1, samplesPerBlock, false, true, false);
    laneReductionBuffer.setSize (2, samplesPerBlock);
    clipReductionBuffer.setSize (3, samplesPerBlock);
    processingLatencySamples = juce::roundToInt (activeOversampling->getLatencyInSamples());
    finalLatencySamples = juce::roundToInt (finalOversampling->getLatencyInSamples());
    previousTruePeak = rawParameters.truePeak->load() > 0.5f;
    totalLatencySamples = processingLatencySamples + finalLatencySamples
        + (isSmoothMode ? lookaheadSamples : 0) + (previousTruePeak ? truePeakLookahead : 0);
    finalInput.setSize (2, samplesPerBlock); finalBaseline.setSize (2, samplesPerBlock);
    finalMetadata.setSize (finalMetadataChannels, samplesPerBlock);
    const int maxFinalLatency = juce::roundToInt (linearFinalOversampling[2]->getLatencyInSamples());
    finalDelay.setSize (2, maxFinalLatency + 3); finalDelay.clear();
    finalMetadataDelay.setSize (finalMetadataChannels, maxFinalLatency + 3); finalMetadataDelay.clear();
    truePeakDelay.setSize (finalMetadataChannels + 2, truePeakLookahead + 1); truePeakDelay.clear();
    finalWrite = truePeakWrite = 0; truePeakGain = 1.0f;
    truePeakTarget = 1; truePeakStep = 0; truePeakAttack = 0;
    truePeakHistory.clear(); truePeakHolds.clear();
    const auto preparePeakKernels = [] (auto& kernels)
    {
        for (size_t phase = 0; phase < kernels.size(); ++phase)
        {
            double sum = 0;
            for (int tap = 0; tap < 64; ++tap)
            {
                const double x = tap - 31.0 - (phase + 1.0) / (kernels.size() + 1.0);
                const double sinc = std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double window = 0.42 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * tap / 63.0)
                    + 0.08 * std::cos (2 * juce::MathConstants<double>::twoPi * tap / 63.0);
                kernels[phase][(size_t) tap] = sinc * window;
                sum += sinc * window;
            }
            for (auto& weight : kernels[phase]) weight /= sum;
        }
    };
    preparePeakKernels (truePeakKernel); // retain the existing meter response
    preparePeakKernels (truePeakFineKernel);
    finalReferenceEnergy = finalResultEnergy = 0;
    matchEnergyCoefficient = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.03));
    detectorSplitState = {}; focusedSplit = {}; focusedDetectorSplit = {};
    detectorEnvelope = detectorTarget = { 1, 1 }; detectorStep = {}; detectorAttack = {};
    for (auto& lane : detectorGainHistory) lane.clear();
    stereoLink.reset (sampleRate, 0.02);
    stereoLink.setCurrentAndTargetValue ((rawParameters.channelMode->load() > 0.5f ? rawParameters.msLink : rawParameters.stereoLink)->load() * 0.01f);
    auditionBlend.reset (sampleRate * oversamplingFactor, 0.005);
    auditionBlend.setCurrentAndTargetValue (0); activeAuditionBand = -1;
    const int maxLatency = juce::jmax (juce::roundToInt (oversampling16.getLatencyInSamples()),
        juce::roundToInt (linearOversampling[2]->getLatencyInSamples())) + 2;
    const int delayStorageSize = maxLatency + lookaheadSamples + 1;
    cleanDelayStorage.setSize (2, delayStorageSize, false, true, false);
    drivenDelayStorage.setSize (2, delayStorageSize, false, true, false);
    wetBaselineDelayStorage.setSize (2, maxLatency + 1, false, true, false);
    lookaheadReductionDelayStorage.setSize (3, maxLatency + 1, false, true, false);
    clipReductionDelayStorage.setSize (3, maxLatency + 1); clipReductionDelayStorage.clear();
    clipReductionDelayWritePosition = 0;
    cleanDelayStorage.clear();
    drivenDelayStorage.clear();
    wetBaselineDelayStorage.clear();
    lookaheadReductionDelayStorage.clear();
    delayWritePosition = 0;
    wetBaselineDelayWritePosition = 0;
    lookaheadReductionDelayWritePosition = 0;
    lookaheadReleaseCoefficient = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate * 0.005));
    setLatencySamples (totalLatencySamples);

    inputGain.reset (sampleRate, 0.015);
    wetMix.reset (sampleRate, 0.015);
    outputGain.reset (sampleRate, 0.015);
    bypassBlend.reset (sampleRate, 0.005);
    const double factor = oversamplingFactor;
    dcPole = std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sampleRate);
    dcPreviousInput = {}; dcPreviousOutput = {};
    dcBlend.reset (sampleRate, 0.02);
    dcBlend.setCurrentAndTargetValue (rawParameters.dcFilter->load());
    for (size_t b = 0; b < 3; ++b)
    {
        const auto& settings = bandParameters[b];
        for (auto* s : { &bandMixes[b], &bandSoftness[b], &bandTrims[b], &bandCurveBlends[b] }) s->reset (sampleRate * factor, 0.02);
        bandMixes[b].setCurrentAndTargetValue (settings.enabled->load() * settings.mix->load() * 0.01f);
        const int shape = (int) settings.curve->load();
        const bool hard = shape == 1 || (shape == 0 && ! isSmoothMode);
        bandSoftness[b].setCurrentAndTargetValue ((shape == 0 ? (hard ? rawParameters.hardKnee : rawParameters.softness)->load()
            : (hard ? settings.knee : settings.soft)->load()) * (hard ? 0.625f : 1.0f) * 0.01f);
        bandCurveBlends[b].setCurrentAndTargetValue (! hard && (shape == 3 || (shape == 0 && rawParameters.curve->load() > 0.5f)) ? 1.0f : 0.0f);
        bandTrims[b].setCurrentAndTargetValue (settings.enabled->load() > 0.5f
            ? gainFromDb (settings.trim->load()) : 1.0f);
    }
    const double k = std::tan (juce::MathConstants<double>::pi * 1681.974450955533 / sampleRate);
    const double v = std::pow (10.0, 3.999843853973347 / 20.0), mid = std::pow (v, .4996667741545416);
    const double q = .7071752369554196, den = 1.0 + k/q + k*k;
    const double hp = std::tan (juce::MathConstants<double>::pi * 38.13547087602444 / sampleRate);
    const double hpDen = 1.0 + hp/.5003270373238773 + hp*hp;
    for (int c = 0; c < 2; ++c)
    {
        meterShelf[(size_t) c] = { (v+mid*k/q+k*k)/den, 2*(k*k-v)/den, (v-mid*k/q+k*k)/den, 2*(k*k-1)/den, (1-k/q+k*k)/den, 0, 0 };
        meterHighPass[(size_t) c] = { 1, -2, 1, 2*(hp*hp-1)/hpDen, (1-hp/.5003270373238773+hp*hp)/hpDen, 0, 0 };
    }
    meterHopSize = juce::jmax (1, (int) (sampleRate * 0.1));
    meterHopCount = meterHopIndex = 0; meterHopEnergy = meterHopRaw = 0; meterHopPeak = 0;
    meterEnergy = {}; meterRawEnergy = {}; meterPeaks = {}; outputPeakHistory = {};
    momentaryLufs.store (-100); outputPeakDb.store (-100); outputTruePeakDb.store (-100); crestDb.store (0);
    for (auto* smoother : { &thresholdGain, &ceilingGain, &softness, &hardKnee })
        smoother->reset (sampleRate * factor, 0.015);
    splitState = {};
    midSideBias.reset (sampleRate * factor, 0.02);
    midSideBias.setCurrentAndTargetValue (rawParameters.msBias->load());
    for (size_t band = 0; band < 3; ++band)
    {
        bandThresholds[(size_t) band].reset (sampleRate * factor, 0.02);
        bandThresholds[band].setCurrentAndTargetValue (gainFromDb (-bandParameters[band].depth->load()));
    }
    for (auto* smoother : { &lowCrossover, &highCrossover, &bandsBlend }) smoother->reset (sampleRate * factor, 0.02);
    lowCrossover.setCurrentAndTargetValue (rawParameters.lowSplit->load());
    highCrossover.setCurrentAndTargetValue (rawParameters.highSplit->load());
    bandsBlend.setCurrentAndTargetValue (rawParameters.bands->load());
    crossoverBlend.reset (sampleRate * factor, 0.03);
    crossoverBlend.setCurrentAndTargetValue (rawParameters.crossover->load());
    deltaBlend.reset (sampleRate, 0.005);
    deltaBlend.setCurrentAndTargetValue (0.0f);
    resetMatchMeasurement();
    for (auto& value : bandReduction) value.store (0.0f);

    inputGain.setCurrentAndTargetValue (gainFromDb (rawParameters.input->load()));
    thresholdGain.setCurrentAndTargetValue (gainFromDb (rawParameters.threshold->load()));
    ceilingGain.setCurrentAndTargetValue (gainFromDb (rawParameters.ceiling->load()));
    softness.setCurrentAndTargetValue (rawParameters.softness->load() * 0.01f);
    hardKnee.setCurrentAndTargetValue (rawParameters.hardKnee->load() * 0.01f);
    const bool isBypassed = rawParameters.bypass->load() > 0.5f;
    wetMix.setCurrentAndTargetValue (proportionFromPercent (rawParameters.mix->load()));
    outputGain.setCurrentAndTargetValue (gainFromDb (rawParameters.output->load()));
    bypassBlend.setCurrentAndTargetValue (isBypassed ? 0.0f : 1.0f);
    scopeIntervalSamples = juce::jmax (1, juce::roundToInt (sampleRate / historyFramesPerSecond));
    scopeSamplesUntilWrite = scopeIntervalSamples;
    pendingScopeInput = pendingScopeOutput = pendingScopeReduction = 0.0f;
    pendingScopeReductionIsSmooth = false;
    if (! keepScopeHistory)
    {
        for (auto& value : scopeInput) value.store (0.0f);
        for (auto& value : scopeOutput) value.store (0.0f);
        for (auto& value : scopeReduction) value.store (0.0f);
        for (auto& value : scopeReductionIsSmooth) value.store (false);
        scopeWritePosition.store (0);
        historySequence.store (0);
    }
    currentGainReductionDb.store (0.0f);
    resetStatistics();
    hasPrepared = true;
    lastPreparedSampleRate = sampleRate;
}

bool OpenClipperAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void OpenClipperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processBuffer (buffer, false);
}

void OpenClipperAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processBuffer (buffer, true);
}

void OpenClipperAudioProcessor::processBuffer (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || buffer.getNumSamples() == 0)
        return;
    // Hosts may exceed the block size advertised in prepareToPlay. Keep all
    // scratch storage and the oversampler within their preallocated capacity.
    for (int offset = 0; offset < buffer.getNumSamples(); offset += maximumBlockSize)
    {
        const int count = juce::jmin (maximumBlockSize, buffer.getNumSamples() - offset);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), 2, offset, count);
        processChunk (chunk, forceBypass);
    }
    for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}

void OpenClipperAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    const auto currentSettings = settingsSignature();
    if (currentSettings != matchWindowSettings)
    {
        matchWindowSettings = currentSettings;
        resetMatchMeasurement();
    }
    const bool isBypassed = forceBypass || rawParameters.bypass->load() > 0.5f;
    const bool isSmoothMode = rawParameters.smoothMode->load() > 0.5f;
    const bool truePeak = rawParameters.truePeak->load() > 0.5f;
    const int requestedFactor = requestedOversamplingFactor();
    const bool linearPhase = rawParameters.linearPhase->load() > .5f;
    if (requestedFactor != oversamplingFactor || linearPhase != previousLinearPhase)
    {
        selectOversampling (requestedFactor, linearPhase);
        finalOversampling->reset();
        activeOversampling->reset();
        processingLatencySamples = juce::roundToInt (activeOversampling->getLatencyInSamples());
        finalLatencySamples = juce::roundToInt (finalOversampling->getLatencyInSamples());
        resetMatchMeasurement();
        displayedFactor.store (oversamplingFactor);
        splitState = {}; focusedSplit = {}; focusedDetectorSplit = {}; detectorSplitState = {};
        for (auto* smoother : { &thresholdGain, &ceilingGain, &softness, &hardKnee, &lowCrossover, &highCrossover, &bandsBlend, &midSideBias, &crossoverBlend })
            smoother->reset (lastPreparedSampleRate * oversamplingFactor, 0.015);
        for (auto& smoother : bandThresholds) smoother.reset (lastPreparedSampleRate * oversamplingFactor, 0.02);
        for (int b = 0; b < 3; ++b)
            for (auto* s : { &bandMixes[(size_t) b], &bandSoftness[(size_t) b], &bandTrims[(size_t) b], &bandCurveBlends[(size_t) b] })
                s->reset (lastPreparedSampleRate * oversamplingFactor, 0.02);
        auditionBlend.reset (lastPreparedSampleRate * oversamplingFactor, 0.005);
    }
    const int requestedLatency = processingLatencySamples + finalLatencySamples
        + (isSmoothMode ? lookaheadSamples : 0) + (truePeak ? truePeakLookahead : 0);
    if (requestedLatency != totalLatencySamples)
    {
        totalLatencySamples = requestedLatency;
        setLatencySamples (totalLatencySamples);
        lookaheadReductionDelayStorage.clear();
        clipReductionDelayStorage.clear(); clipReductionDelayWritePosition = 0;
        lookaheadReductionDelayWritePosition = 0;
        detectorEnvelope = detectorTarget = { 1, 1 }; detectorStep = {}; detectorAttack = {};
        for (auto& lane : detectorGainHistory) lane.clear();
    }
    if (truePeak != previousTruePeak)
    {
        truePeakDelay.clear(); truePeakHistory.clear(); truePeakHolds.clear();
        truePeakWrite = 0; truePeakGain = 1; previousTruePeak = truePeak;
        truePeakTarget = 1; truePeakStep = 0; truePeakAttack = 0;
    }
    lookaheadReleaseCoefficient = 1.0f - std::exp (-1.0f / (float) (lastPreparedSampleRate
        * rawParameters.release->load() * 0.001));
    const bool midSide = rawParameters.channelMode->load() > 0.5f;
    stereoLink.setTargetValue ((midSide ? rawParameters.msLink : rawParameters.stereoLink)->load() * 0.01f);
    inputGain.setTargetValue (globalGainCache[0].get (rawParameters.input->load()));
    thresholdGain.setTargetValue (globalGainCache[1].get (rawParameters.threshold->load()));
    ceilingGain.setTargetValue (globalGainCache[2].get (rawParameters.ceiling->load()));
    softness.setTargetValue (rawParameters.softness->load() * 0.01f);
    hardKnee.setTargetValue (rawParameters.hardKnee->load() * 0.01f);
    crossoverBlend.setTargetValue (rawParameters.crossover->load());
    wetMix.setTargetValue (proportionFromPercent (rawParameters.mix->load()));
    outputGain.setTargetValue (globalGainCache[3].get (rawParameters.output->load()));
    bypassBlend.setTargetValue (isBypassed ? 0.0f : 1.0f);
    deltaBlend.setTargetValue (deltaAudition.load() && ! isNonRealtime() ? 1.0f : 0.0f);
    lowCrossover.setTargetValue (rawParameters.lowSplit->load());
    highCrossover.setTargetValue (rawParameters.highSplit->load());
    const float requestedBands = rawParameters.bands->load();
    // Also enforce this while the editor is absent (automation/A-B recall).
    if (requestedBands < 0.5f) auditionBand.store (-1);
    bandsBlend.setTargetValue (requestedBands);
    std::array<float, 3> detectorDepths;
    for (size_t b = 0; b < 3; ++b)
    {
        detectorDepths[b] = depthGainCache[b].get (-bandParameters[b].depth->load());
        bandThresholds[b].setTargetValue (detectorDepths[b]);
    }
    const bool cubic = rawParameters.curve->load() > 0.5f;
    dcBlend.setTargetValue (rawParameters.dcFilter->load());
    std::array<float, 3> detectorBandMix {};
    for (size_t b = 0; b < 3; ++b)
    {
        const auto& settings = bandParameters[b];
        const bool enabled = settings.enabled->load() > 0.5f;
        detectorBandMix[b] = enabled ? settings.mix->load() * 0.01f : 0;
        bandMixes[b].setTargetValue (detectorBandMix[b]);
        const int shape = (int) settings.curve->load();
        const bool hard = shape == 1 || (shape == 0 && ! isSmoothMode);
        bandSoftness[b].setTargetValue ((shape == 0 ? (hard ? rawParameters.hardKnee : rawParameters.softness)->load()
            : hard ? settings.knee->load() : settings.soft->load()) * (hard ? 0.625f : 1.0f) * 0.01f);
        bandCurveBlends[b].setTargetValue (! hard && (shape == 3 || (shape == 0 && cubic)) ? 1.0f : 0.0f);
        bandTrims[b].setTargetValue (enabled ? trimGainCache[b].get (settings.trim->load()) : 1.0f);
    }
    midSideBias.setTargetValue (rawParameters.msBias->load());
    if (midSide != previousMidSide) { splitState = {}; detectorSplitState = {}; focusedSplit = {}; focusedDetectorSplit = {}; previousMidSide = midSide; }
    const int requestedAudition = isNonRealtime() || isBypassed ? -1 : auditionBand.load();
    if (requestedAudition >= 0 && (activeAuditionBand < 0 || ! auditionBlend.isSmoothing())) activeAuditionBand = requestedAudition;
    auditionBlend.setTargetValue (requestedAudition >= 0 && requestedBands > 0.5f ? 1.0f : 0.0f);

    // One sample-accurate control trajectory serves the lookahead detector,
    // oversampled transfer curve, and final wet cap. Never use a block-end
    // threshold for earlier samples during automation.
    for (int i = 0; i < buffer.getNumSamples() * oversamplingFactor; ++i)
    {
        controlBuffer.setSample (0, i, thresholdGain.getNextValue());
        controlBuffer.setSample (1, i, ceilingGain.getNextValue());
        const float soft = softness.getNextValue(), knee = hardKnee.getNextValue();
        controlBuffer.setSample (2, i, isSmoothMode ? soft : knee);
    }

    dryBuffer.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
    drivenDelayedBuffer.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
    rawDrivenDelayedBuffer.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
    gainReductionBuffer.setSize (1, buffer.getNumSamples(), false, false, true);
    laneReductionBuffer.setSize (2, buffer.getNumSamples(), false, false, true);
    clipReductionBuffer.setSize (3, buffer.getNumSamples(), false, false, true);
    gainReductionBuffer.clear();
    clipReductionBuffer.clear();
    const int delayStorageSize = cleanDelayStorage.getNumSamples();
    const int wetBaselineDelaySize = wetBaselineDelayStorage.getNumSamples();
    detectorCrossoverCache[0].update (rawParameters.lowSplit->load(), lastPreparedSampleRate);
    detectorCrossoverCache[1].update (rawParameters.highSplit->load(), lastPreparedSampleRate);
    const double detectorLowPole = detectorCrossoverCache[0].pole, detectorHighPole = detectorCrossoverCache[1].pole;
    const auto& detectorLow = detectorCrossoverCache[0].coefficients;
    const auto& detectorHigh = detectorCrossoverCache[1].coefficients;
    const float detectorFocus = crossoverBlend.getCurrentValue();
    const float detectorBias = rawParameters.msBias->load();
    const std::array<float, 2> detectorScales { midSide ? detectorBiasCache[0].get (juce::jmin (0.0f, detectorBias)) : 1,
        midSide ? detectorBiasCache[1].get (-juce::jmax (0.0f, detectorBias)) : 1 };
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float drive = inputGain.getNextValue();
        const float dcAmount = dcBlend.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto& sample = buffer.getWritePointer (channel)[i];
            const float cleanInput = std::isfinite (sample) ? sample : 0.0f;
            const auto lane = (size_t) channel;
            const double filtered = cleanInput - dcPreviousInput[lane] + dcPole * dcPreviousOutput[lane];
            dcPreviousInput[lane] = cleanInput; dcPreviousOutput[lane] = filtered;
            const float drivenInput = (dcAmount <= 0 ? cleanInput : cleanInput + dcAmount * ((float) filtered - cleanInput)) * drive;

            cleanDelayStorage.setSample (channel, delayWritePosition, cleanInput);
            drivenDelayStorage.setSample (channel, delayWritePosition, drivenInput);
        }

        std::array<float, 2> laneGain { 1, 1 };
        {
            const float left = drivenDelayStorage.getSample (0, delayWritePosition);
            const float right = drivenDelayStorage.getSample (1, delayWritePosition);
            const std::array<float, 2> lanes { midSide ? (left + right) * 0.5f : left,
                                              midSide ? (left - right) * 0.5f : right };
            const float threshold = controlBuffer.getSample (0, i * oversamplingFactor);
            const double lowPole = detectorLowPole, highPole = detectorHighPole;
            const auto& depths = detectorDepths;
            for (int lane = 0; lane < 2; ++lane)
            {
                const float scale = detectorScales[(size_t) lane];
                const float limit = threshold * scale;
                float wanted = std::abs (lanes[(size_t) lane]) > limit ? limit / std::abs (lanes[(size_t) lane]) : 1;
                auto& states = detectorSplitState[(size_t) lane];
                for (int split = 0; split < 2; ++split)
                {
                    const double pole = split == 0 ? lowPole : highPole;
                    states[(size_t) split][0] = lanes[(size_t) lane] + pole * (states[(size_t) split][0] - lanes[(size_t) lane]);
                    states[(size_t) split][1] = states[(size_t) split][0] + pole * (states[(size_t) split][1] - states[(size_t) split][0]);
                }
                const auto focused = focusedDetectorSplit[(size_t) lane].process (lanes[(size_t) lane], detectorLow, detectorHigh);
                if (! isSmoothMode) continue;
                if (requestedBands > 0.5f)
                {
                    std::array<float, 3> signals { (float) states[0][1], (float) (states[1][1] - states[0][1]), (float) (lanes[(size_t) lane] - states[1][1]) };
                    for (size_t b = 0; b < 3; ++b) signals[b] += detectorFocus * (focused[b] - signals[b]);
                    for (int b = 0; b < 3; ++b)
                        if (std::abs (signals[(size_t) b]) > limit * depths[(size_t) b])
                            wanted = juce::jmin (wanted, 1.0f + detectorBandMix[(size_t) b]
                                * (limit * depths[(size_t) b] / std::abs (signals[(size_t) b]) - 1.0f));
                }
                const float desired = detectorGainHistory[(size_t) lane].push (wanted);
                auto& envelope = detectorEnvelope[(size_t) lane];
                auto& remaining = detectorAttack[(size_t) lane];
                if (desired < envelope && (remaining == 0 || desired < detectorTarget[(size_t) lane]))
                {
                    detectorTarget[(size_t) lane] = desired; remaining = lookaheadSamples;
                    detectorStep[(size_t) lane] = (desired - envelope) / lookaheadSamples;
                }
                if (remaining > 0) { envelope += detectorStep[(size_t) lane]; if (--remaining == 0) envelope = detectorTarget[(size_t) lane]; }
                else envelope += (desired - envelope) * lookaheadReleaseCoefficient;
                laneGain[(size_t) lane] = juce::jlimit (0.0f, 1.0f, envelope);
            }
        }
        const float link = stereoLink.getNextValue();
        const float linkedGain = juce::jmin (laneGain[0], laneGain[1]);
        for (auto& gain : laneGain) gain += link * (linkedGain - gain);
        const float lookaheadGain = juce::jmin (laneGain[0], laneGain[1]);

        const int activeLookaheadSamples = isSmoothMode ? lookaheadSamples : 0;
        const int cleanReadPosition = (delayWritePosition - processingLatencySamples - activeLookaheadSamples + delayStorageSize) % delayStorageSize;
        const int lookaheadReadPosition = (delayWritePosition - activeLookaheadSamples + delayStorageSize) % delayStorageSize;
        const int wetBaselineReadPosition = (wetBaselineDelayWritePosition - processingLatencySamples
                                              + wetBaselineDelaySize) % wetBaselineDelaySize;
        const int lookaheadReductionDelaySize = lookaheadReductionDelayStorage.getNumSamples();
        const int lookaheadReductionReadPosition = (lookaheadReductionDelayWritePosition - processingLatencySamples
                                                     + lookaheadReductionDelaySize) % lookaheadReductionDelaySize;
        const float lookaheadReductionDb = lookaheadGain < 1.0f
            ? -juce::Decibels::gainToDecibels (lookaheadGain, -36.0f)
            : 0.0f;
        lookaheadReductionDelayStorage.setSample (0, lookaheadReductionDelayWritePosition,
                                                   lookaheadReductionDb);
        gainReductionBuffer.setSample (0, i,
            lookaheadReductionDelayStorage.getSample (0, lookaheadReductionReadPosition));
        for (int channel = 0; channel < 2; ++channel)
        {
            // Legacy metering aggregates M/S lanes; physical stereo meters use
            // independent detector gains only when the processing lanes are L/R.
            const float gain = midSide ? lookaheadGain : laneGain[(size_t) channel];
            const float reduction = gain < 1 ? -juce::Decibels::gainToDecibels (gain, -36.0f) : 0;
            lookaheadReductionDelayStorage.setSample (channel + 1, lookaheadReductionDelayWritePosition, reduction);
            laneReductionBuffer.setSample (channel, i,
                lookaheadReductionDelayStorage.getSample (channel + 1, lookaheadReductionReadPosition));
        }

        const float delayedLeft = drivenDelayStorage.getSample (0, lookaheadReadPosition);
        const float delayedRight = drivenDelayStorage.getSample (1, lookaheadReadPosition);
        const float processedMid = (delayedLeft + delayedRight) * 0.5f * laneGain[0];
        const float processedSide = (delayedLeft - delayedRight) * 0.5f * laneGain[1];
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            dryBuffer.setSample (channel, i, cleanDelayStorage.getSample (channel, cleanReadPosition));
            rawDrivenDelayedBuffer.setSample (channel, i,
                drivenDelayStorage.getSample (channel, cleanReadPosition));
            const float lookaheadInput = drivenDelayStorage.getSample (channel, lookaheadReadPosition);
            const float processedInput = laneGain[0] == 1.0f && laneGain[1] == 1.0f ? lookaheadInput
                : midSide ? (channel == 0 ? processedMid + processedSide : processedMid - processedSide)
                          : lookaheadInput * laneGain[(size_t) channel];
            buffer.setSample (channel, i, processedInput);
            wetBaselineDelayStorage.setSample (channel, wetBaselineDelayWritePosition, processedInput);
            drivenDelayedBuffer.setSample (channel, i,
                wetBaselineDelayStorage.getSample (channel, wetBaselineReadPosition));
        }

        delayWritePosition = (delayWritePosition + 1) % delayStorageSize;
        wetBaselineDelayWritePosition = (wetBaselineDelayWritePosition + 1) % wetBaselineDelaySize;
        lookaheadReductionDelayWritePosition = (lookaheadReductionDelayWritePosition + 1)
                                             % lookaheadReductionDelaySize;
    }

    juce::dsp::AudioBlock<float> block (buffer);
    auto oversampled = activeOversampling->processSamplesUp (block);
    const auto channels = static_cast<int> (oversampled.getNumChannels());
    const auto samples = static_cast<int> (oversampled.getNumSamples());
    const int baseSamples = buffer.getNumSamples();
    float maximumOver = 1.0f;
    float maximumReductionThisBlock = 0.0f;
    std::array<float, 3> bandPeaks {};
    const bool movingSplits = lowCrossover.isSmoothing() || highCrossover.isSmoothing();
    wetCrossoverCache[0].update (lowCrossover.getCurrentValue(), lastPreparedSampleRate * oversamplingFactor);
    wetCrossoverCache[1].update (highCrossover.getCurrentValue(), lastPreparedSampleRate * oversamplingFactor);
    const double fixedLowPole = wetCrossoverCache[0].pole, fixedHighPole = wetCrossoverCache[1].pole;
    auto focusedLow = wetCrossoverCache[0].coefficients, focusedHigh = wetCrossoverCache[1].coefficients;

    for (int i = 0; i < samples; ++i)
    {
        if (midSide)
        {
            const float left = oversampled.getChannelPointer (0)[i];
            const float right = oversampled.getChannelPointer (1)[i];
            oversampled.getChannelPointer (0)[i] = (left + right) * 0.5f;
            oversampled.getChannelPointer (1)[i] = (left - right) * 0.5f;
        }
        const float threshold = juce::jmax (0.00001f, controlBuffer.getSample (0, i));
        const float soft = controlBuffer.getSample (2, i);
        const float bandAmount = bandsBlend.getNextValue();
        const float focus = crossoverBlend.getNextValue();
        const float listenAmount = auditionBlend.getNextValue();
        const float bias = midSideBias.getNextValue();
        const float midThresholdScale = midSide && bias < 0.0f ? wetBiasCache[0].get (bias) : 1.0f;
        const float sideThresholdScale = midSide && bias > 0.0f ? wetBiasCache[1].get (-bias) : 1.0f;
        const float lowHz = lowCrossover.getNextValue(), highHz = highCrossover.getNextValue();
        const double lowPole = movingSplits ? std::exp (-juce::MathConstants<double>::twoPi * lowHz / (lastPreparedSampleRate * oversamplingFactor)) : fixedLowPole;
        const double highPole = movingSplits ? std::exp (-juce::MathConstants<double>::twoPi * highHz / (lastPreparedSampleRate * oversamplingFactor)) : fixedHighPole;
        if (movingSplits)
        {
            focusedLow = ClippingDSP::CrossoverCoefficients::make (lowHz, lastPreparedSampleRate * oversamplingFactor);
            focusedHigh = ClippingDSP::CrossoverCoefficients::make (highHz, lastPreparedSampleRate * oversamplingFactor);
        }
        std::array<float, 3> bandLimits;
        for (int b = 0; b < 3; ++b) bandLimits[(size_t) b] = threshold * bandThresholds[(size_t) b].getNextValue();
        std::array<float, 3> mixes, softs, trims, curves;
        for (size_t b = 0; b < 3; ++b)
        { mixes[b] = bandMixes[b].getNextValue(); softs[b] = bandSoftness[b].getNextValue(); trims[b] = bandTrims[b].getNextValue(); curves[b] = bandCurveBlends[b].getNextValue(); }
        const auto transfer = [soft, cubic, isSmoothMode] (float magnitude, float limit)
        { return ClippingDSP::shape (magnitude, limit, soft, cubic, ! isSmoothMode); };

        for (int channel = 0; channel < channels; ++channel)
        {
            auto* data = oversampled.getChannelPointer (static_cast<size_t> (channel));
            const float original = data[i];
            const float laneScale = channel == 0 ? midThresholdScale : sideThresholdScale;
            const float laneThreshold = threshold * laneScale;
            float bandCorrected = original;
            float listenedBand = 0;
            float bandGr = 0;
            // Blend Broad's complementary split with phase-aligned LR4 lanes.
            // Recombine corrections, not filtered audio: no clipping means
            // an exactly zero residual even with Bands enabled.
            // Keep both networks warm so enabling bands or changing crossover
            // cannot introduce a filter-startup burst into the correction.
            {
                auto& states = splitState[(size_t) channel];
                for (int split = 0; split < 2; ++split)
                {
                    const double pole = split == 0 ? lowPole : highPole;
                    auto& s = states[(size_t) split];
                    s[0] = original + pole * (s[0] - original);
                    s[1] = s[0] + pole * (s[1] - s[0]);
                }
                std::array<float, 3> bandSignal { (float) states[0][1], (float) (states[1][1] - states[0][1]), (float) (original - states[1][1]) };
                const auto focused = focusedSplit[(size_t) channel].process (original, focusedLow, focusedHigh);
                for (size_t b = 0; b < 3; ++b) bandSignal[b] += focus * (focused[b] - bandSignal[b]);
                if (bandAmount > 0.0f)
                for (int b = 0; b < 3; ++b)
                {
                    const float magnitude = std::abs (bandSignal[(size_t) b]);
                    const auto bi = (size_t) b;
                    const float cap = bandLimits[bi] * laneScale, width = cap * 0.8f * softs[bi], start = cap - width;
                    float shaped = juce::jmin (magnitude, cap);
                    if (width >= 0.000001f && magnitude > start)
                    {
                        const float t = juce::jlimit (0.0f, 1.0f, (magnitude-start)/(2*width));
                        const float classic = 2*t-t*t, smooth = 2*t-2*t*t*t+t*t*t*t;
                        shaped = start + width * (classic + curves[bi] * (smooth-classic));
                    }
                    const float clipped = magnitude + mixes[bi] * (shaped - magnitude);
                    const float bandOutput = std::copysign (clipped * trims[bi], bandSignal[bi]);
                    if (b == activeAuditionBand) listenedBand = bandOutput;
                    bandCorrected += bandAmount * (bandOutput - bandSignal[bi]);
                    if (magnitude > 0.000001f && clipped > 0.000001f)
                    {
                        const float reduction = bandAmount * juce::Decibels::gainToDecibels (magnitude / clipped);
                        bandPeaks[(size_t) b] = juce::jmax (bandPeaks[(size_t) b], reduction);
                        bandGr = juce::jmax (bandGr, reduction);
                    }
                }
            }
            const float magnitude = std::abs (bandCorrected);
            float clippedMagnitude = transfer (magnitude, laneThreshold);

            const float clipped = std::copysign (clippedMagnitude, bandCorrected);

            // Only the nonlinear difference is sent through the oversampling filters.
            // When no clipping occurs this is exactly zero, so the wet path can use an
            // exact integer-delayed copy of the driven input without filter coloration.
            data[i] = clipped + listenAmount * (listenedBand - clipped) - original;
            maximumOver = juce::jmax (maximumOver, magnitude / threshold);
            // 2.1.0: strongest band reduction plus the full-band transfer
            // reduction, peak-held across oversampled phases (not RMS loss).
            if (bandGr > 0 || (magnitude > clippedMagnitude && clippedMagnitude > 0.000001f))
            {
                const float reduction = bandGr + (magnitude > clippedMagnitude && clippedMagnitude > 0.000001f
                    ? juce::Decibels::gainToDecibels (magnitude / clippedMagnitude, 0.0f) : 0);
                const int baseIndex = juce::jmin (baseSamples - 1, i / oversamplingFactor);
                clipReductionBuffer.setSample (0, baseIndex, juce::jmax (clipReductionBuffer.getSample (0, baseIndex), reduction));
                for (int lane = 0; lane < 2; ++lane)
                    if (midSide || lane == channel)
                        clipReductionBuffer.setSample (lane + 1, baseIndex, juce::jmax (clipReductionBuffer.getSample (lane + 1, baseIndex), reduction));
            }
        }
        if (midSide)
        {
            const float mid = oversampled.getChannelPointer (0)[i];
            const float side = oversampled.getChannelPointer (1)[i];
            oversampled.getChannelPointer (0)[i] = mid + side;
            oversampled.getChannelPointer (1)[i] = mid - side;
        }
    }

    activeOversampling->processSamplesDown (block);
    for (int b = 0; b < 3; ++b)
        bandReduction[(size_t) b].store (isBypassed ? 0.0f : juce::jmax (bandPeaks[(size_t) b],
            bandReduction[(size_t) b].load() * std::pow (meterRelease, (float) baseSamples)));
    const int clipDelaySize = clipReductionDelayStorage.getNumSamples();
    for (int i = 0; i < baseSamples; ++i)
    {
        const int read = (clipReductionDelayWritePosition - processingLatencySamples + clipDelaySize) % clipDelaySize;
        for (int ch = 0; ch < 3; ++ch)
        {
            clipReductionDelayStorage.setSample (ch, clipReductionDelayWritePosition, clipReductionBuffer.getSample (ch, i));
            const float reduction = clipReductionDelayStorage.getSample (ch, read);
            if (ch == 0) gainReductionBuffer.addSample (0, i, reduction);
            else laneReductionBuffer.addSample (ch - 1, i, reduction);
        }
        clipReductionDelayWritePosition = (clipReductionDelayWritePosition + 1) % clipDelaySize;
    }

    finalInput.setSize (2, baseSamples, false, false, true);
    finalBaseline.setSize (2, baseSamples, false, false, true);
    finalMetadata.setSize (finalMetadataChannels, baseSamples, false, false, true);
    const auto reductionFromEnergy = [] (double reference, double result)
    {
        return reference > 1.0e-10 ? juce::jlimit (0.0f, 36.0f,
            (float) (10.0 * std::log10 (reference / juce::jmax (result, reference * 0.000251188643)))) : 0.0f;
    };
    float wetMeter = wetGainReductionDb.load();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int controlIndex = (i + 1) * oversamplingFactor - 1;
        const float wetThreshold = controlBuffer.getSample (0, controlIndex);
        const float finalLimit = controlBuffer.getSample (1, controlIndex);
        const float mix = wetMix.getNextValue();
        const float outGain = outputGain.getNextValue();
        const float active = bypassBlend.getNextValue();
        const float delta = deltaBlend.getNextValue();
        float wetCapReduction = 0;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* wet = buffer.getWritePointer (channel);
            const auto* dry = dryBuffer.getReadPointer (channel);
            const auto* driven = drivenDelayedBuffer.getReadPointer (channel);
            const float processedWet = driven[i] + wet[i];
            const float cappedWet = juce::jlimit (-wetThreshold, wetThreshold, processedWet);

            // Keep the inactive path bit-transparent (apart from the latency reported
            // to the host) instead of performing an unnecessary multiply/add pair.
            const float mixed = mix <= 0.0f ? dry[i] : mix >= 1.0f ? cappedWet
                : dry[i] + (cappedWet - dry[i]) * mix;
            const float normal = mixed * outGain;
            finalInput.setSample (channel, i, normal);
            const float raw = rawDrivenDelayedBuffer.getSample (channel, i);
            const float magnitude = std::abs (processedWet);
            const float capReduction = magnitude > wetThreshold && wetThreshold > 0.000001f
                ? juce::Decibels::gainToDecibels (magnitude / wetThreshold, 0.0f) : 0;
            wetCapReduction = juce::jmax (wetCapReduction, capReduction);
            finalMetadata.setSample (16 + channel, i, capReduction);
            finalMetadata.setSample (11 + channel, i, laneReductionBuffer.getSample (channel, i) * mix);
            finalMetadata.setSample (13 + channel, i, normal * normal);
            finalMetadata.setSample (channel, i, dry[i]);
            finalMetadata.setSample (2 + channel, i, raw - cappedWet);
        }
        const float wetGr = juce::jlimit (0.0f, 36.0f, gainReductionBuffer.getSample (0, i) + wetCapReduction);
        wetMeter = isBypassed ? 0 : juce::jmax (wetGr, wetMeter * meterRelease);
        finalMetadata.setSample (4, i, finalLimit);
        finalMetadata.setSample (5, i, active);
        finalMetadata.setSample (6, i, delta);
        finalMetadata.setSample (7, i, gainReductionBuffer.getSample (0, i) * mix);
        finalMetadata.setSample (15, i, wetCapReduction);
        finalMetadata.setSample (18, i, 0);
        finalMetadata.setSample (8, i, (float) (dryBuffer.getSample (0, i) * dryBuffer.getSample (0, i)
            + dryBuffer.getSample (1, i) * dryBuffer.getSample (1, i)));
        finalMetadata.setSample (9, i, juce::jmax (std::abs (finalInput.getSample (0, i)), std::abs (finalInput.getSample (1, i))));
        finalMetadata.setSample (10, i, finalInput.getSample (0, i) * finalInput.getSample (0, i)
            + finalInput.getSample (1, i) * finalInput.getSample (1, i));
    }
    wetGainReductionDb.store (wetMeter);

    // Optional reconstruction-aware, linked lookahead gain. A Blackman-windowed
    // sinc detector estimates sixteenth-sample peaks; it never filters the baseline.
    // Target the final Limit itself, with no fixed headroom offset.
    if (truePeak)
    {
        const float release = 1.0f - std::exp (-1.0f / (float) (lastPreparedSampleRate * 0.01));
        for (int i = 0; i < baseSamples; ++i)
        {
            truePeakHistory.push (finalInput.getSample (0, i), finalInput.getSample (1, i));
            const float peak = truePeakHistory.peak<15> (truePeakFineKernel);
            const float heldPeak = truePeakHolds.push (peak);
            const float target = finalMetadata.getSample (4, i);
            const float wanted = heldPeak > target ? target / heldPeak : 1;
            if (wanted < truePeakGain && (truePeakAttack == 0 || wanted < truePeakTarget))
            {
                truePeakTarget = wanted;
                // Finish ahead of the reconstructed peak rather than ramping
                // through it, which can create a new inter-sample overshoot.
                truePeakAttack = truePeakAttack > 0 ? juce::jmin (truePeakAttack, 4) : 4;
                truePeakStep = (wanted - truePeakGain) / truePeakAttack;
            }
            if (truePeakAttack > 0)
            { truePeakGain += truePeakStep; if (--truePeakAttack == 0) truePeakGain = truePeakTarget; }
            else truePeakGain += release * (wanted - truePeakGain);
            for (int ch = 0; ch < finalMetadataChannels + 2; ++ch)
                truePeakDelay.setSample (ch, truePeakWrite, ch < 2 ? finalInput.getSample (ch, i) : finalMetadata.getSample (ch - 2, i));
            const int read = (truePeakWrite + 1) % (truePeakLookahead + 1);
            for (int ch = 0; ch < 2; ++ch) finalInput.setSample (ch, i, truePeakDelay.getSample (ch, read) * truePeakGain);
            for (int ch = 0; ch < finalMetadataChannels; ++ch) finalMetadata.setSample (ch, i, truePeakDelay.getSample (ch + 2, read));
            finalMetadata.setSample (18, i, truePeakGain < 1 ? -juce::Decibels::gainToDecibels (truePeakGain, -36.0f) : 0);
            truePeakWrite = read;
        }
    }

    juce::dsp::AudioBlock<float> finalBlock (finalInput);
    auto finalUp = finalOversampling->processSamplesUp (finalBlock);
    for (int i = 0; i < (int) finalUp.getNumSamples(); ++i)
    {
        const float limit = finalMetadata.getSample (4, i / oversamplingFactor);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& value = finalUp.getChannelPointer ((size_t) ch)[i];
            // TP already regulates reconstructed peaks. A second nonlinear
            // resampled clip can generate new peaks after that safeguard.
            value = truePeak ? 0.0f : juce::jlimit (-limit, limit, value) - value;
        }
    }
    // Delay baseline and metadata by the final stage's *reported* integer latency.
    for (int i = 0; i < baseSamples; ++i)
    {
        const int size = finalDelay.getNumSamples();
        const int read = (finalWrite - finalLatencySamples + size) % size;
        for (int ch = 0; ch < 2; ++ch)
        {
            finalDelay.setSample (ch, finalWrite, finalInput.getSample (ch, i));
            finalBaseline.setSample (ch, i, finalDelay.getSample (ch, read));
        }
        for (int ch = 0; ch < finalMetadataChannels; ++ch)
        {
            finalMetadataDelay.setSample (ch, finalWrite, finalMetadata.getSample (ch, i));
            finalMetadata.setSample (ch, i, finalMetadataDelay.getSample (ch, read));
        }
        finalWrite = (finalWrite + 1) % size;
    }
    finalOversampling->processSamplesDown (finalBlock);
    float meterEnvelope = currentGainReductionDb.load(), finalMeter = finalGainReductionDb.load();
    std::array<float, 2> channelEnvelope { channelGainReduction[0].load(), channelGainReduction[1].load() };
    std::array<float, 2> channelMaximum {};
    for (int i = 0; i < baseSamples; ++i)
    {
        const float limit = juce::jmax (0.00001f, finalMetadata.getSample (4, i));
        const float active = finalMetadata.getSample (5, i), delta = finalMetadata.getSample (6, i);
        double resultEnergy = 0;
        const float referencePeak = finalMetadata.getSample (9, i);
        const float truePeakReduction = finalMetadata.getSample (18, i);
        const float finalGr = juce::jmax (truePeakReduction, referencePeak > limit
            ? juce::Decibels::gainToDecibels (referencePeak / limit, 0.0f) : 0);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float corrected = finalBaseline.getSample (ch, i) + finalInput.getSample (ch, i);
            const float finalOutput = juce::jlimit (-limit, limit, corrected);
            const float dry = finalMetadata.getSample (ch, i);
            const float removed = finalMetadata.getSample (2 + ch, i);
            const float processed = delta <= 0 ? finalOutput : finalOutput + delta * (removed - finalOutput);
            float result = active <= 0 ? dry : active >= 1 ? processed : dry + (processed - dry) * active;
            if (! isBypassed && delta <= 0) result = juce::jlimit (-limit, limit, result);
            buffer.setSample (ch, i, result);
            // Reference peak is used only for safe matching; energy retains both channels.
            resultEnergy += (double) finalOutput * finalOutput;
            const auto lane = (size_t) ch;
            const double reference = finalMetadata.getSample (13 + ch, i);
            const float normalMagnitude = (float) std::sqrt (juce::jmax (0.0, reference));
            const float limitReduction = juce::jmax (truePeakReduction, normalMagnitude > limit
                ? juce::Decibels::gainToDecibels (normalMagnitude / limit, 0.0f) : 0);
            const float reduction = isBypassed ? 0 : juce::jlimit (0.0f, 36.0f, finalMetadata.getSample (11 + ch, i)
                + juce::jmax (finalMetadata.getSample (16 + ch, i), limitReduction));
            channelEnvelope[lane] = isBypassed ? 0 : juce::jmax (reduction, channelEnvelope[lane] * meterRelease);
            channelMaximum[lane] = juce::jmax (channelMaximum[lane], reduction);
            matchOutputEnergy += (double) finalOutput * finalOutput;
            dryBuffer.setSample (ch, i, dry);
        }
        // RMS energy is retained only for ceiling-aware Match, not GR display.
        finalReferenceEnergy += matchEnergyCoefficient * (finalMetadata.getSample (10, i) - finalReferenceEnergy);
        finalResultEnergy += matchEnergyCoefficient * (resultEnergy - finalResultEnergy);
        const float measuredMatchReduction = referencePeak > 0.000001f ? reductionFromEnergy (finalReferenceEnergy, finalResultEnergy) : 0;
        finalMeter = isBypassed ? 0 : juce::jmax (finalGr, finalMeter * meterRelease);
        matchInputEnergy += finalMetadata.getSample (8, i);
        matchPeak = juce::jmax (matchPeak, referencePeak / limit);
        matchWasLimited |= measuredMatchReduction > 0.05f;
        if (++matchSamples >= (int) (lastPreparedSampleRate * 1.5))
        {
            const bool valid = matchInputEnergy > 0.000001 && matchOutputEnergy > 0.000001
                && ! isBypassed && delta <= 0 && requestedAudition < 0;
            const float correction = valid ? juce::jlimit (-12.0f, 12.0f,
                (float) (10.0 * std::log10 (matchInputEnergy / matchOutputEnergy))) : 0;
            const float headroom = matchPeak > 0 ? -juce::Decibels::gainToDecibels (matchPeak) : 12;
            const float safe = correction > 0 ? (matchWasLimited ? 0.0f
                : juce::jmax (0.0f, juce::jmin (correction, headroom - 0.1f))) : correction;
            matchCorrectionDb.store (safe);
            matchLimited.store (valid && correction > safe + 0.05f);
            matchedSettings.store (matchWindowSettings); matchReady.store (valid);
            matchInputEnergy = matchOutputEnergy = 0; matchSamples = 0; matchPeak = 0; matchWasLimited = false;
        }
        // Exact 2.1.0 aggregate formula: wet-stage peak GR * Mix, plus the
        // larger of wet-cap overshoot and final-cap demand (not their sum).
        const float sampleReductionDb = isBypassed ? 0 : juce::jlimit (0.0f, 36.0f,
            finalMetadata.getSample (7, i) + juce::jmax (finalMetadata.getSample (15, i), finalGr));
        gainReductionBuffer.setSample (0, i, sampleReductionDb);
        maximumReductionThisBlock = juce::jmax (maximumReductionThisBlock, sampleReductionDb);
        meterEnvelope = isBypassed ? 0.0f : juce::jmax (sampleReductionDb, meterEnvelope * meterRelease);
    }
    finalGainReductionDb.store (finalMeter);
    const double peakMarkerTime = juce::Time::getMillisecondCounterHiRes();
    for (int ch = 0; ch < 2; ++ch)
    {
        channelGainReduction[(size_t) ch].store (channelEnvelope[(size_t) ch]);
        reductionPeakMarkers[(size_t) ch].push (channelMaximum[(size_t) ch], peakMarkerTime);
        auto previous = channelMaximumReduction[(size_t) ch].load();
        while (channelMaximum[(size_t) ch] > previous
               && ! channelMaximumReduction[(size_t) ch].compare_exchange_weak (previous, channelMaximum[(size_t) ch])) {}
    }

    const float displayedReduction = isBypassed ? 0.0f : maximumReductionThisBlock;
    currentGainReductionDb.store (meterEnvelope);
    if (! isBypassed && maximumOver > 1.001f)
        clippingLatched.store (true);
    auto previousMaximum = maximumGainReductionDb.load();
    while (displayedReduction > previousMaximum
           && ! maximumGainReductionDb.compare_exchange_weak (previousMaximum, displayedReduction)) {}

    // Display-only work is selected once per chunk. Closing the editor never
    // disables clipping, lookahead, final limiting, DC or audition processing.
    const uint64_t captureState = historyCaptureState.load (std::memory_order_acquire);
    const bool captureHistory = (captureState & 1) != 0;
    const bool captureSpectrum = spectrumEnabled.load (std::memory_order_relaxed);
    const bool analyse = analysisEnabled.load (std::memory_order_relaxed);
    if (captureState != lastHistoryCaptureState)
    {
        pendingScopeInput = pendingScopeOutput = pendingScopeReduction = 0;
        pendingScopeReductionIsSmooth = false;
        scopeSamplesUntilWrite = scopeIntervalSamples;
        lastHistoryCaptureState = captureState;
    }
    if (captureSpectrum && spectrumReset.exchange (false))
    {
        for (auto& value : spectrumSamples) value.store (0, std::memory_order_relaxed);
        for (auto& value : spectrumRight) value.store (0, std::memory_order_relaxed);
        spectrumWrite.store (0, std::memory_order_release);
    }
    if (captureSpectrum || analyse)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (captureSpectrum)
            {
                const int index = spectrumWrite.load (std::memory_order_relaxed);
                spectrumSamples[(size_t) index].store (dryBuffer.getSample (0, i), std::memory_order_relaxed);
                spectrumRight[(size_t) index].store (dryBuffer.getSample (1, i), std::memory_order_relaxed);
                spectrumWrite.store ((index + 1) & 4095, std::memory_order_release);
            }
            if (analyse) updateAnalysis (buffer.getSample (0, i), buffer.getSample (1, i));
        }
    if (captureHistory) for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float output = juce::jmax (std::abs (buffer.getSample (0, i)), std::abs (buffer.getSample (1, i)));
        const float drivenInput = juce::jmax (std::abs (rawDrivenDelayedBuffer.getSample (0, i)),
                                             std::abs (rawDrivenDelayedBuffer.getSample (1, i)));
        const float reduction = gainReductionBuffer.getSample (0, i);
        if (reduction > pendingScopeReduction)
        {
            pendingScopeReduction = reduction;
            pendingScopeReductionIsSmooth = isSmoothMode;
        }
        if (std::abs (drivenInput) > std::abs (pendingScopeInput)) pendingScopeInput = drivenInput;
        if (std::abs (output) > std::abs (pendingScopeOutput)) pendingScopeOutput = output;
        if (--scopeSamplesUntilWrite <= 0)
        {
            const int position = scopeWritePosition.load();
            scopeInput[static_cast<size_t> (position)].store (juce::jlimit (-1.5f, 1.5f, pendingScopeInput));
            scopeOutput[static_cast<size_t> (position)].store (juce::jlimit (-1.5f, 1.5f, pendingScopeOutput));
            scopeReduction[static_cast<size_t> (position)].store (juce::jlimit (0.0f, 36.0f, pendingScopeReduction));
            scopeReductionIsSmooth[static_cast<size_t> (position)].store (pendingScopeReductionIsSmooth);
            // Reject late publications from a previous editor without GUI
            // writes to the audio-owned ring or locks on the audio thread.
            scopeCaptureState[(size_t) position].store (captureState);
            scopeWritePosition.store ((position + 1) & (scopeSize - 1));
            historySequence.fetch_add (1, std::memory_order_release);
            pendingScopeInput = pendingScopeOutput = pendingScopeReduction = 0.0f;
            pendingScopeReductionIsSmooth = false;
            scopeSamplesUntilWrite = scopeIntervalSamples;
        }
    }
}

void OpenClipperAudioProcessor::updateAnalysis (float left, float right) noexcept
{
    if (analysisReset.exchange (false))
    {
        for (auto* filters : { &meterShelf, &meterHighPass }) for (auto& f : *filters) f.z1 = f.z2 = 0;
        meterEnergy = {}; meterRawEnergy = {}; meterPeaks = {}; outputPeakHistory = {};
        meterHopCount = meterHopIndex = 0; meterHopEnergy = meterHopRaw = 0; meterHopPeak = 0;
        momentaryLufs.store (-100); outputPeakDb.store (-100); outputTruePeakDb.store (-100); crestDb.store (0);
    }
    const double a = meterHighPass[0].process (meterShelf[0].process (left));
    const double b = meterHighPass[1].process (meterShelf[1].process (right));
    meterHopEnergy += a*a + b*b;
    meterHopRaw += ((double) left*left + (double) right*right) * 0.5;
    meterHopPeak = juce::jmax (meterHopPeak, std::abs (left), std::abs (right));
    outputPeakHistory.push (left, right);
    const float truePeak = juce::jmax (std::abs (left), std::abs (right), outputPeakHistory.peak<3, 2, 1, false> (truePeakKernel));
    outputTruePeakDb.store (juce::jmax (outputTruePeakDb.load(), juce::Decibels::gainToDecibels (truePeak, -100.0f)));
    outputPeakDb.store (juce::jmax (outputPeakDb.load(), juce::Decibels::gainToDecibels (juce::jmax (std::abs(left), std::abs(right)), -100.0f)));
    if (++meterHopCount < meterHopSize) return;
    meterEnergy[(size_t) meterHopIndex] = meterHopEnergy / meterHopSize;
    meterRawEnergy[(size_t) meterHopIndex] = meterHopRaw / meterHopSize;
    meterPeaks[(size_t) meterHopIndex] = meterHopPeak;
    meterHopIndex = (meterHopIndex + 1) % 4;
    double energy = 0, raw = 0; float peak = 0;
    for (int n = 0; n < 4; ++n) { energy += meterEnergy[(size_t) n] * .25; raw += meterRawEnergy[(size_t) n] * .25; peak = juce::jmax (peak, meterPeaks[(size_t) n]); }
    momentaryLufs.store (energy > 1.0e-10 ? (float) (-0.691 + 10*std::log10 (energy)) : -100.0f);
    crestDb.store (raw > 1.0e-10 ? juce::jmax (0.0f, (float) (20*std::log10 (peak / std::sqrt(raw)))) : 0.0f);
    meterHopCount = 0; meterHopEnergy = meterHopRaw = 0; meterHopPeak = 0;
}

void OpenClipperAudioProcessor::resetStatistics() noexcept
{
    currentGainReductionDb.store (0.0f);
    clippingLatched.store (false);
    maximumGainReductionDb.store (0.0f);
    outputPeakDb.store (-100); outputTruePeakDb.store (-100);
    wetGainReductionDb.store (0); finalGainReductionDb.store (0);
    for (auto& value : channelGainReduction) value.store (0);
    for (auto& value : channelMaximumReduction) value.store (0);
    for (auto& marker : reductionPeakMarkers) marker.reset();
    for (auto& value : bandReduction) value.store (0.0f);
}

uint64_t OpenClipperAudioProcessor::settingsSignature() const noexcept
{
    uint64_t signature = 1469598103934665603ull;
    for (auto* parameter : getParameters())
    {
        const float value = parameter->getValue();
        uint32_t bits = 0;
        std::memcpy (&bits, &value, sizeof (bits));
        signature = (signature ^ bits) * 1099511628211ull;
    }
    return signature;
}

void OpenClipperAudioProcessor::resetMatchMeasurement() noexcept
{
    matchInputEnergy = matchOutputEnergy = 0;
    matchSamples = 0; matchPeak = 0; matchWasLimited = false;
    matchReady.store (false); matchLimited.store (false);
}

bool OpenClipperAudioProcessor::canMatchOutput() const noexcept
{
    return matchReady.load() && rawParameters.bypass->load() < 0.5f
        && ! deltaAudition.load() && auditionBand.load() < 0
        && matchedSettings.load() == settingsSignature();
}

bool OpenClipperAudioProcessor::matchOutputToInput()
{
    if (! canMatchOutput()) return false;
    auto* parameter = parameters.getParameter ("output");
    const float value = rawParameters.output->load() + matchCorrectionDb.load();
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, value)));
    parameter->endChangeGesture();
    matchReady.store (false);
    return true;
}

void OpenClipperAudioProcessor::setLinkedInputFromEditor (float inputDb)
{
    if (rawParameters.gainLink->load() < 0.5f) return;
    auto* parameter = parameters.getParameter ("output");
    // The editor supplies the input delta; host automation remains independent
    // so playing a linked automation recording cannot apply compensation twice.
    const float value = rawParameters.output->load() - inputDb;
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, value)));
}

void OpenClipperAudioProcessor::selectSnapshot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == snapshotSlot) return;
    snapshots[(size_t) snapshotSlot] = parameters.copyState();
    if (! snapshots[(size_t) slot].isValid()) snapshots[(size_t) slot] = parameters.copyState();
    snapshotSlot = slot;
    migrateModeLink (snapshots[(size_t) slot]);
    migrateClipping24 (snapshots[(size_t) slot]);
    // Notify host for every recalled parameter; do not silently replace APVTS.
    for (auto* rawParameter : getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (rawParameter))
        {
            const auto node = snapshots[(size_t) slot].getChildWithProperty ("id", parameter->paramID);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (node.isValid() ? parameter->convertTo0to1 ((float) node["value"])
                                                            : parameter->getDefaultValue());
            parameter->endChangeGesture();
        }
}

void OpenClipperAudioProcessor::copySnapshot()
{
    snapshots[(size_t) (1 - snapshotSlot)] = parameters.copyState();
}

juce::ValueTree OpenClipperAudioProcessor::saveEditorState() const
{
    juce::ValueTree view ("EDITOR_VIEW");
    view.setProperty ("page", editorState.page.load(), nullptr);
    view.setProperty ("band", editorState.selectedBand.load(), nullptr);
    view.setProperty ("width", editorState.width.load(), nullptr);
    view.setProperty ("height", editorState.height.load(), nullptr);
    view.setProperty ("meters", editorState.meters.load(), nullptr);
    view.setProperty ("freeze", editorState.frozen.load(), nullptr);
    view.setProperty ("hoverHelp", editorState.hoverHelp.load(), nullptr);
    view.setProperty ("listen", getBandAudition(), nullptr);
    const auto history = getFrozenHistory();
    if (editorState.frozen.load() && history.valid)
    {
        juce::MemoryOutputStream data;
        data.writeInt (1);
        for (auto value : history.output) data.writeFloat (value);
        for (auto value : history.reduction) data.writeFloat (value);
        for (auto value : history.smooth) data.writeBool (value);
        data.writeFloat (history.nextOutput); data.writeFloat (history.nextReduction);
        data.writeFloat (history.fraction); data.writeBool (history.nextSmooth);
        data.writeInt (history.inspected); data.writeInt64 ((juce::int64) history.sequence);
        view.setProperty ("history", data.getMemoryBlock().toBase64Encoding(), nullptr);
    }
    return view;
}

void OpenClipperAudioProcessor::restoreEditorState (const juce::ValueTree& view)
{
    editorState.page.store (juce::jlimit (0, 2, (int) view.getProperty ("page", 0)));
    editorState.selectedBand.store (juce::jlimit (0, 2, (int) view.getProperty ("band", 0)));
    editorState.width.store (juce::jlimit (720, 1200, (int) view.getProperty ("width", 720)));
    editorState.height.store (juce::jlimit (440, 760, (int) view.getProperty ("height", 440)));
    editorState.meters.store ((bool) view.getProperty ("meters", false));
    editorState.frozen.store ((bool) view.getProperty ("freeze", false));
    editorState.hoverHelp.store ((bool) view.getProperty ("hoverHelp", false));
    const bool listenAllowed = editorState.page.load() == 2
        && rawParameters.bands->load() > .5f;
    setBandAudition (listenAllowed ? (int) view.getProperty ("listen", -1) : -1);
    setMeteringEnabled (editorState.meters.load());
    FrozenHistory history;
    juce::MemoryBlock bytes;
    if (editorState.frozen.load() && bytes.fromBase64Encoding (view.getProperty ("history").toString())
        && bytes.getSize() == 4 + scopeSize * 9 + 12 + 1 + 4 + 8)
    {
        juce::MemoryInputStream data (bytes, false);
        history.valid = data.readInt() == 1;
        for (auto& value : history.output) { value = data.readFloat(); history.valid &= std::isfinite (value) && value >= 0; }
        for (auto& value : history.reduction) { value = data.readFloat(); history.valid &= std::isfinite (value) && value >= 0 && value <= 36; }
        for (auto& value : history.smooth) value = data.readBool();
        history.nextOutput = data.readFloat(); history.nextReduction = data.readFloat(); history.fraction = data.readFloat();
        history.nextSmooth = data.readBool(); history.inspected = data.readInt(); history.sequence = (uint64_t) data.readInt64();
        history.valid &= std::isfinite (history.nextOutput) && history.nextOutput >= 0
            && std::isfinite (history.nextReduction) && history.nextReduction >= 0 && history.nextReduction <= 36
            && std::isfinite (history.fraction) && history.fraction >= 0 && history.fraction < 1
            && (history.inspected == -1 || (history.inspected >= scopeSize-historyFrames && history.inspected < scopeSize));
    }
    setFrozenHistory (history.valid ? history : FrozenHistory {});
    editorState.revision.fetch_add (1);
}

void OpenClipperAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    snapshots[(size_t) snapshotSlot] = state.createCopy();
    state.setProperty ("activeSnapshot", snapshotSlot, nullptr);
    juce::ValueTree comparison ("COMPARISON");
    for (int slot = 0; slot < 2; ++slot)
    {
        auto saved = snapshots[(size_t) slot].isValid() ? snapshots[(size_t) slot].createCopy() : state.createCopy();
        saved.setProperty ("slot", slot, nullptr);
        comparison.addChild (saved, -1, nullptr);
    }
    state.addChild (comparison, -1, nullptr);
    state.addChild (saveEditorState(), -1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void OpenClipperAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            migrateModeLink (state);
            migrateClipping24 (state);
            snapshotSlot = juce::jlimit (0, 1, (int) state.getProperty ("activeSnapshot", 0));
            auto comparison = state.getChildWithName ("COMPARISON");
            for (int slot = 0; slot < 2; ++slot) snapshots[(size_t) slot] = comparison.getChild (slot).createCopy();
            state.removeChild (comparison, nullptr);
            const auto view = state.getChildWithName ("EDITOR_VIEW");
            state.removeChild (view, nullptr);
            // Older sessions contain only the original eight parameters. Do
            // not inherit advanced settings from a previously loaded preset.
            for (auto* raw : getParameters())
                if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (raw);
                    parameter && ! state.getChildWithProperty ("id", parameter->paramID).isValid())
                {
                    juce::ValueTree node ("PARAM");
                    node.setProperty ("id", parameter->paramID, nullptr);
                    node.setProperty ("value", parameter->convertFrom0to1 (parameter->getDefaultValue()), nullptr);
                    state.addChild (node, -1, nullptr);
                }
            parameters.replaceState (state);
            undoManager.clearUndoHistory();
            deltaAudition.store (false);
            restoreEditorState (view);
        }
}

juce::AudioProcessorEditor* OpenClipperAudioProcessor::createEditor()
{
    return new OpenClipperAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpenClipperAudioProcessor();
}
