#pragma once

#include <JuceHeader.h>
#include <array>
#include "ClippingDSP.h"
#include "../Shared/TimedPeakHold.h"

class OpenClipperAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int scopeSize = 1024;
    static constexpr int historyFrames = 800;
    static constexpr int historyFramesPerSecond = 100;
    static constexpr int lookaheadSamples = 30;
    static constexpr int truePeakLookahead = 48;
    static constexpr int finalMetadataChannels = 19;

    OpenClipperAudioProcessor();
    ~OpenClipperAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return parameters.getParameter ("bypass"); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState parameters;
    bool isDeltaAudition() const noexcept { return deltaAudition.load(); }
    uint64_t startHistoryCapture() noexcept
    {
        const auto state = (historyCaptureState.load() & ~uint64_t(1)) + 3;
        historyCaptureState.store (state, std::memory_order_release);
        return state;
    }
    void stopHistoryCapture() noexcept { historyCaptureState.fetch_and (~uint64_t(1), std::memory_order_release); }
    uint64_t getHistoryCaptureState() const noexcept { return historyCaptureState.load (std::memory_order_acquire); }
    uint64_t readScopeCaptureState (int index) const noexcept { return scopeCaptureState[(size_t) index].load(); }
    void setSpectrumEnabled (bool enabled) noexcept
    {
        if (enabled && ! spectrumEnabled.load()) spectrumReset.store (true);
        spectrumEnabled.store (enabled);
    }
    void setMeteringEnabled (bool enabled) noexcept
    {
        if (enabled && ! analysisEnabled.load()) analysisReset.store (true);
        analysisEnabled.store (enabled);
    }
    float getMomentaryLufs() const noexcept { return momentaryLufs.load(); }
    float getCrestDb() const noexcept { return crestDb.load(); }
    float getOutputTruePeakDb() const noexcept { return outputTruePeakDb.load(); }
    float getOutputPeakDb() const noexcept { return outputPeakDb.load(); }
    float readSpectrumSample (int index, int channel = 0) const noexcept { return (channel == 0 ? spectrumSamples : spectrumRight)[(size_t) (index & 4095)].load(); }
    int getSpectrumWrite() const noexcept { return spectrumWrite.load(); }
    uint64_t getHistorySequence() const noexcept { return historySequence.load(); }

    float readScopeInput (int index) const noexcept  { return scopeInput[static_cast<size_t> (index)].load(); }
    float readScopeOutput (int index) const noexcept { return scopeOutput[static_cast<size_t> (index)].load(); }
    float readScopeReduction (int index) const noexcept { return scopeReduction[static_cast<size_t> (index)].load(); }
    bool readScopeReductionIsSmooth (int index) const noexcept { return scopeReductionIsSmooth[static_cast<size_t> (index)].load(); }
    int getScopeWritePosition() const noexcept { return scopeWritePosition.load(); }
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb.load(); }
    float getMaximumGainReductionDb() const noexcept { return maximumGainReductionDb.load(); }
    float getCurrentGainReductionDb (int channel) const noexcept { return channelGainReduction[(size_t) juce::jlimit (0, 1, channel)].load(); }
    float getMaximumGainReductionDb (int channel) const noexcept { return channelMaximumReduction[(size_t) juce::jlimit (0, 1, channel)].load(); }
    float getReductionPeakMarker (int channel) const noexcept
    { return reductionPeakMarkers[(size_t)juce::jlimit(0,1,channel)].value(juce::Time::getMillisecondCounterHiRes()); }
    float getWetGainReductionDb() const noexcept { return wetGainReductionDb.load(); }
    float getFinalGainReductionDb() const noexcept { return finalGainReductionDb.load(); }
    bool isMatchLimited() const noexcept { return matchLimited.load(); }
    bool hasClipped() const noexcept { return clippingLatched.load(); }
    void resetStatistics() noexcept;
    void setDeltaAudition (bool enabled) noexcept { deltaAudition.store (enabled); }
    void setBandAudition (int band) noexcept { auditionBand.store (juce::jlimit (-1, 2, band)); }
    int getBandAudition() const noexcept { return auditionBand.load(); }
    void setLinkedInputFromEditor (float inputDb);
    float getBandReduction (int band) const noexcept { return bandReduction[(size_t) band].load(); }
    int getOversamplingFactor() const noexcept { return displayedFactor.load(); }
    bool matchOutputToInput();
    bool canMatchOutput() const noexcept;
    void selectSnapshot (int slot);
    void copySnapshot();
    int getSnapshot() const noexcept { return snapshotSlot; }

    // Presentation state belongs to the instance, not a disposable editor or
    // an A/B sound snapshot. No UI cache is touched by processBlock.
    struct EditorState
    {
        std::atomic<int> page { 0 }, selectedBand { 0 }, width { 720 }, height { 440 };
        std::atomic<bool> frozen { false }, meters { false }, hoverHelp { false };
        std::atomic<uint64_t> revision { 0 };
    } editorState;
    struct FrozenHistory
    {
        bool valid = false;
        std::array<float, scopeSize> output {}, reduction {};
        std::array<bool, scopeSize> smooth {};
        float nextOutput = 0, nextReduction = 0, fraction = 0;
        bool nextSmooth = false;
        int inspected = -1;
        uint64_t sequence = 0;
    };
    FrozenHistory getFrozenHistory() const
    { const juce::ScopedLock lock (editorHistoryLock); return editorHistory; }
    void setFrozenHistory (const FrozenHistory& history)
    { const juce::ScopedLock lock (editorHistoryLock); editorHistory = history; }
    void editorStateChanged()
    { updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true)); }

private:
    friend struct ClipperCpuAudit;
    // Parameter objects survive state recall; cache pointers, never their values.
    struct RawParameters
    {
        std::atomic<float> *input, *threshold, *softness, *ceiling, *mix, *output,
            *bypass, *smoothMode, *liveQuality, *renderQuality, *linearPhase,
            *truePeak, *release, *channelMode, *msLink, *stereoLink, *hardKnee,
            *crossover, *lowSplit, *highSplit, *bands, *curve, *dcFilter, *msBias, *gainLink, *lowRate;
    } rawParameters {};
    std::array<ClippingDSP::DbGainCache, 4> globalGainCache;
    std::array<ClippingDSP::DbGainCache, 3> depthGainCache, trimGainCache;
    std::array<ClippingDSP::CrossoverCache, 2> detectorCrossoverCache, wetCrossoverCache;
    std::array<ClippingDSP::DbGainCache, 2> detectorBiasCache, wetBiasCache;
    mutable juce::CriticalSection editorHistoryLock;
    FrozenHistory editorHistory;
    juce::ValueTree saveEditorState() const;
    void restoreEditorState (const juce::ValueTree&);
    void processBuffer (juce::AudioBuffer<float>&, bool forceBypass);
    void processChunk (juce::AudioBuffer<float>&, bool forceBypass);
    int requestedOversamplingFactor() const noexcept;
    void selectOversampling (int factor, bool linearPhase) noexcept;
    int maximumBlockSize = 1;
    float meterRelease = 0.0f;
    juce::dsp::Oversampling<float> oversampling;
    // Exponent zero is JUCE's unity-rate copy stage: no resampling filters.
    juce::dsp::Oversampling<float> oversamplingOff { 2, 0, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float> oversampling2 { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float> oversampling8 { 2, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float> oversampling16 { 2, 4, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float>* activeOversampling = &oversampling;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> linearOversampling, linearFinalOversampling;
    bool previousLinearPhase = false;
    juce::dsp::Oversampling<float> finalOversamplingOff { 2, 0, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float> finalOversampling2 { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float> finalOversampling4 { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float> finalOversampling8 { 2, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float> finalOversampling16 { 2, 4, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float>* finalOversampling = &finalOversampling4;
    int finalLatencySamples = 0;
    juce::AudioBuffer<float> finalInput, finalBaseline, finalDelay, finalMetadata, finalMetadataDelay;
    int finalWrite = 0;
    // A reconstruction detector only: the dry baseline never passes through it.
    ClippingDSP::StereoPeakHistory truePeakHistory;
    std::array<std::array<double, 64>, 7> truePeakKernel {};
    std::array<std::array<double, 64>, 15> truePeakFineKernel {};
    ClippingDSP::SlidingExtreme<64, false> truePeakHolds;
    juce::AudioBuffer<float> truePeakDelay;
    int truePeakWrite = 0;
    float truePeakGain = 1.0f;
    float truePeakTarget = 1.0f, truePeakStep = 0;
    int truePeakAttack = 0;
    bool previousTruePeak = false;
    double finalReferenceEnergy = 0, finalResultEnergy = 0;
    std::array<std::atomic<float>, 2> channelGainReduction {}, channelMaximumReduction {};
    std::array<TimedPeakHold, 2> reductionPeakMarkers;
    std::atomic<float> wetGainReductionDb { 0 }, finalGainReductionDb { 0 };
    float matchEnergyCoefficient = 0;
    std::atomic<int> auditionBand { -1 };
    juce::SmoothedValue<float> auditionBlend;
    int activeAuditionBand = -1;
    std::array<std::array<std::array<double, 2>, 2>, 2> detectorSplitState {};
    std::array<float, 2> detectorEnvelope { 1, 1 };
    std::array<float, 2> detectorTarget { 1, 1 }, detectorStep {};
    std::array<int, 2> detectorAttack {};
    std::array<ClippingDSP::SlidingExtreme<lookaheadSamples + 1, true>, 2> detectorGainHistory;
    juce::SmoothedValue<float> stereoLink;
    int oversamplingFactor = 4;
    std::atomic<int> displayedFactor { 4 };
    // Complementary split; only nonlinear band corrections leave this network.
    std::array<std::array<std::array<double, 2>, 2>, 2> splitState {};
    std::array<ClippingDSP::FocusedSplit, 2> focusedSplit {}, focusedDetectorSplit {};
    juce::SmoothedValue<float> crossoverBlend;
    bool previousMidSide = false;
    std::array<juce::SmoothedValue<float>, 3> bandThresholds;
    std::array<juce::SmoothedValue<float>, 3> bandMixes, bandSoftness, bandTrims, bandCurveBlends;
    struct BandParameters { std::atomic<float>* enabled; std::atomic<float>* mix; std::atomic<float>* curve; std::atomic<float>* soft; std::atomic<float>* trim; std::atomic<float>* knee; std::atomic<float>* depth; };
    std::array<BandParameters, 3> bandParameters;
    juce::SmoothedValue<float> dcBlend;
    std::array<double, 2> dcPreviousInput {}, dcPreviousOutput {};
    double dcPole = 0;
    std::array<std::atomic<float>, 4096> spectrumSamples {};
    std::array<std::atomic<float>, 4096> spectrumRight {};
    std::atomic<int> spectrumWrite { 0 };
    std::atomic<uint64_t> historySequence { 0 };
    std::atomic<uint64_t> historyCaptureState { 0 };
    uint64_t lastHistoryCaptureState = 0;
    std::array<std::atomic<uint64_t>, scopeSize> scopeCaptureState {};
    std::atomic<bool> spectrumEnabled { false }, spectrumReset { false };
    struct MeterBiquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x) noexcept { const double y = b0*x + z1; z1 = b1*x - a1*y + z2; z2 = b2*x - a2*y; return y; }
    };
    std::array<MeterBiquad, 2> meterShelf, meterHighPass;
    ClippingDSP::StereoPeakHistory outputPeakHistory;
    std::array<double, 4> meterEnergy {}, meterRawEnergy {};
    std::array<float, 4> meterPeaks {};
    int meterHopSize = 4800, meterHopCount = 0, meterHopIndex = 0;
    double meterHopEnergy = 0, meterHopRaw = 0;
    float meterHopPeak = 0;
    std::atomic<bool> analysisEnabled { false };
    std::atomic<bool> analysisReset { false };
    std::atomic<float> momentaryLufs { -100 }, crestDb { 0 }, outputTruePeakDb { -100 }, outputPeakDb { -100 };
    void updateAnalysis (float left, float right) noexcept;
    juce::SmoothedValue<float> lowCrossover, highCrossover, bandsBlend, deltaBlend;
    juce::SmoothedValue<float> midSideBias;
    std::array<std::atomic<float>, 3> bandReduction {};
    std::atomic<bool> deltaAudition { false };
    double matchInputEnergy = 0.0, matchOutputEnergy = 0.0;
    std::atomic<float> matchCorrectionDb { 0.0f };
    std::atomic<bool> matchReady { false };
    std::atomic<bool> matchLimited { false };
    float matchPeak = 0;
    bool matchWasLimited = false;
    uint64_t matchWindowSettings = 0;
    std::atomic<uint64_t> matchedSettings { 0 };
    uint64_t settingsSignature() const noexcept;
    int matchSamples = 0;
    void resetMatchMeasurement() noexcept;
    std::array<juce::ValueTree, 2> snapshots;
    int snapshotSlot = 0;
    juce::AudioBuffer<float> controlBuffer;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> drivenDelayedBuffer;
    juce::AudioBuffer<float> rawDrivenDelayedBuffer;
    juce::AudioBuffer<float> gainReductionBuffer;
    juce::AudioBuffer<float> clipReductionBuffer, clipReductionDelayStorage, laneReductionBuffer;
    int clipReductionDelayWritePosition = 0;
    juce::AudioBuffer<float> cleanDelayStorage;
    juce::AudioBuffer<float> drivenDelayStorage;
    juce::AudioBuffer<float> wetBaselineDelayStorage;
    juce::AudioBuffer<float> lookaheadReductionDelayStorage;
    int delayWritePosition = 0;
    int wetBaselineDelayWritePosition = 0;
    int lookaheadReductionDelayWritePosition = 0;
    int processingLatencySamples = 0;
    int totalLatencySamples = 0;
    float lookaheadReleaseCoefficient = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ceilingGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> softness;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hardKnee;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bypassBlend;

    std::array<std::atomic<float>, scopeSize> scopeInput {};
    std::array<std::atomic<float>, scopeSize> scopeOutput {};
    std::array<std::atomic<float>, scopeSize> scopeReduction {};
    std::array<std::atomic<bool>, scopeSize> scopeReductionIsSmooth {};
    std::atomic<int> scopeWritePosition { 0 };
    std::atomic<float> currentGainReductionDb { 0.0f };
    std::atomic<float> maximumGainReductionDb { 0.0f };
    std::atomic<bool> clippingLatched { false };
    int scopeIntervalSamples = 480;
    int scopeSamplesUntilWrite = 480;
    float pendingScopeInput = 0.0f, pendingScopeOutput = 0.0f, pendingScopeReduction = 0.0f;
    bool pendingScopeReductionIsSmooth = false;
    bool hasPrepared = false;
    double lastPreparedSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenClipperAudioProcessor)
};
