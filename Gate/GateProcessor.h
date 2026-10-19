#pragma once

#include <JuceHeader.h>
#include <array>
#include "PeakDetector.h"
#include "../Compressor/CompressorFilters.h"

class OpenGateAudioProcessor final : public juce::AudioProcessor,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::AsyncUpdater
{
public:
    static constexpr int historySize = 512;
    static constexpr int historyCapacity = 1024; // Visible window plus buffered-display/ring-write slack.

    OpenGateAudioProcessor();
    ~OpenGateAudioProcessor() override;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return getLatencySamples() / latencySampleRate.load(); }
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

    // Saved display preferences, independent of sonic A/B settings and automation.
    // One atomic choice prevents conflicting overlays during host automation.
    enum class HistoryOverlay { none, sidechain, detector };
    HistoryOverlay historyOverlayMode() const noexcept { return historyOverlay.load(); }
    bool isSidechainHistoryVisible() const noexcept { return historyOverlayMode() == HistoryOverlay::sidechain; }
    bool isDetectorHistoryVisible() const noexcept { return historyOverlayMode() == HistoryOverlay::detector; }
    void setSidechainHistoryVisible (bool enabled) noexcept
    {
        if (enabled) historyOverlay.store (HistoryOverlay::sidechain);
        else { auto expected = HistoryOverlay::sidechain; historyOverlay.compare_exchange_strong (expected, HistoryOverlay::none); }
    }
    void setDetectorHistoryVisible (bool enabled) noexcept
    {
        if (enabled) historyOverlay.store (HistoryOverlay::detector);
        else { auto expected = HistoryOverlay::detector; historyOverlay.compare_exchange_strong (expected, HistoryOverlay::none); }
    }
    std::atomic<bool> hoverHelpEnabled { false };
    std::atomic<int> editorPage { 0 }; // 0 = main, 1 = Details, 2 = SC; not a sound/A-B setting.
    void editorPreferencesChanged() { updateHostDisplay (ChangeDetails{}.withNonParameterStateChanged (true)); }

    struct HistoryFrame
    {
        float input = 0, output = 0, sidechain = 0, detectorL = 0, detectorR = 0, attenuation = 0;
        bool detectorPeak = false; // Mode at capture: frozen/past traces retain their meaning.
    };
    uint64_t getHistorySequence() const noexcept { return historySequence.load (std::memory_order_acquire); }
    uint64_t getHistoryEpoch() const noexcept { return historyEpoch.load (std::memory_order_acquire); }
    uint64_t startHistoryCapture() noexcept
    {
        const auto state = (historyCaptureState.load() & ~uint64_t (1)) + 3;
        historyCaptureState.store (state, std::memory_order_release);
        return state;
    }
    void stopHistoryCapture() noexcept { historyCaptureState.fetch_and (~uint64_t (1), std::memory_order_release); }
    uint64_t getHistoryCaptureState() const noexcept { return historyCaptureState.load (std::memory_order_acquire); }
    bool readHistoryFrame (uint64_t sequence, HistoryFrame&, uint64_t captureState = 0) const noexcept;
    double historyFramesPerSecond() const noexcept { return latencySampleRate.load() / historyInterval.load(); }
    double historyDisplayDelayFrames() const noexcept
    {
        return juce::jlimit (4.0, 128.0, std::ceil ((double) historyBlockSamples.load() / historyInterval.load()) + 2.0);
    }
    void selectSnapshot (int slot);
    void copySnapshot();
    int getSnapshot() const noexcept { return snapshotSlot.load(); }
    void setAuditionRemoved (bool enabled) noexcept { auditionRemoved.store (enabled); }
    bool isAuditioningRemoved() const noexcept { return auditionRemoved.load(); }
    bool externalBusAvailable() const noexcept { return getBusCount (true) > 1 && getBus (true, 1)->isEnabled(); }

    float attenuationLevel() const noexcept { return attenuationMeter.load(); }
    float maximumAttenuationLevel() const noexcept { return maximumAttenuationMeter.load(); }
    float inputLevel() const noexcept { return inputMeter.load(); }
    float outputLevel() const noexcept { return outputMeter.load(); }
    bool isGateOpen() const noexcept { return gateOpenMeter.load(); }
    bool hasExternalSidechain() const noexcept { return externalSidechainActive.load(); }
    int historyPosition() const noexcept { return historyWrite.load(); }
    float historyInput (int i) const noexcept { return inputHistory[(size_t) i].load(); }
    float historyOutput (int i) const noexcept { return outputHistory[(size_t) i].load(); }
    float historySidechain (int i) const noexcept { return sidechainHistory[(size_t) i].load(); }
    float historyAttenuation (int i) const noexcept { return attenuationHistory[(size_t) i].load(); }
    void resetMaximumAttenuation() noexcept { maximumAttenuationMeter.store (0.0f); }
    void setAuditionSidechain (bool enabled) noexcept
    {
        if (enabled && ! auditionSidechain.exchange (true)) setSidechainHistoryVisible (true);
        else if (! enabled) auditionSidechain.store (false);
    }
    bool isAuditioningSidechain() const noexcept { return auditionSidechain.load(); }
    double detectorResponseDb (double frequency) const noexcept;
    struct SidechainCutoffs { float highPass = 20, lowPass = 20000; };
    static constexpr double sidechainMinimumRatio = 1.2599210498948732; // One third octave, matching Compressor.
    static SidechainCutoffs constrainSidechainCutoffs (float hp, float lp, bool editedHighPass = true) noexcept;
    SidechainCutoffs sidechainCutoffs() const noexcept;
    // Bounded single-producer/single-consumer monitor; never waits for the UI.
    struct SpectrumFrame { float left = 0, right = 0; uint64_t epoch = 0, sequence = 0, captureState = 0; };
    static constexpr int spectrumCapacity = 8192;
    void setSpectrumEnabled (bool enabled) noexcept { spectrumEnabled.store (enabled); }
    bool readSpectrumFrame (SpectrumFrame&) noexcept;
    double spectrumSampleRate() const noexcept { return latencySampleRate.load(); }
    static float expansionReduction (float levelDb, float thresholdDb, float ratio,
                                     float kneeDb, float rangeDb) noexcept;

private:
    friend struct GateSCFilterAudit;
    friend struct GateSCAudioAudit;
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void normalizeSidechainFilters();
    static void normalizeFilterState (juce::ValueTree, bool editedHighPass = true);
    std::atomic<bool> lastEditedHighPass { true }, normalizingFilters { false }, recallingFilters { false };
    std::atomic<bool> filterNormalizationRequested { false };
    void updateLatency();
    struct DetectorFilter
    {
        double highPassState[2][2] {}, lowPassState[2][2] {};
        float process (float sample, const CompressorFilters::Cascade& highPassCoefficients,
                       const CompressorFilters::Cascade& lowPassCoefficients,
                       bool highPassEnabled, bool lowPassEnabled) noexcept;
    };

    double sampleRate = 44100.0;
    std::atomic<double> latencySampleRate { 44100.0 };
    juce::AudioBuffer<float> delayBuffer;
    int delayWrite = 0, delayLength = 0, maxLookaheadSamples = 0;
    DetectorFilter detectorFilters[2];
    DetectorFilter externalFilters[2];
    std::atomic<bool> spectrumEnabled { false };
    juce::AbstractFifo spectrumFifo { spectrumCapacity };
    std::array<SpectrumFrame, spectrumCapacity> spectrumFrames {};
    uint64_t spectrumSequence = 0;
    float envelopeL = 0.0f, envelopeR = 0.0f;
    GatePeakDetector peakDetectors[2];
    juce::SmoothedValue<float> peakBlend;
    float gainDbL = 0.0f, gainDbR = 0.0f;
    float heldReductionL = 0.0f, heldReductionR = 0.0f;
    bool openL = true, openR = true;
    bool hostBypassed = false;
    int holdL = 0, holdR = 0;
    juce::SmoothedValue<float> inputGain, outputGain, wet, activeMix, auditionMix, removedMix;

    std::atomic<float> attenuationMeter { 0.0f }, maximumAttenuationMeter { 0.0f };
    std::atomic<float> inputMeter { 0.0f }, outputMeter { 0.0f };
    std::atomic<bool> gateOpenMeter { true };
    std::atomic<bool> externalSidechainActive { false };
    std::atomic<HistoryOverlay> historyOverlay { HistoryOverlay::none };
    std::atomic<bool> auditionSidechain { false };
    std::atomic<bool> auditionRemoved { false };
    std::array<std::atomic<float>, historyCapacity> inputHistory {}, outputHistory {}, sidechainHistory {}, attenuationHistory {};
    std::array<std::atomic<float>, historyCapacity> detectorHistoryL {}, detectorHistoryR {};
    std::array<std::atomic<bool>, historyCapacity> detectorPeakHistory {};
    std::array<std::atomic<uint64_t>, historyCapacity> frameSequences {};
    std::array<std::atomic<uint64_t>, historyCapacity> frameCaptureStates {};
    std::atomic<uint64_t> historyCaptureState { 0 }; // Off without an editor; low bit enables, upper bits identify a visit.
    uint64_t lastHistoryCaptureState = 0; // Audio-thread owned.
    std::atomic<uint64_t> historySequence { 0 };
    std::atomic<uint64_t> historyEpoch { 0 };
    std::atomic<int> historyWrite { 0 };
    int historyCountdown = 1;
    std::atomic<int> historyInterval { 735 };
    std::atomic<int> historyBlockSamples { 512 };
    float historyInputPeak = 0.0f, historyOutputPeak = 0.0f, historySidechainPeak = 0.0f;
    float historyAttenuationPeak = 0.0f;
    float historyDetectorL = 0, historyDetectorR = 0;
    std::array<juce::ValueTree, 2> snapshots;
    juce::CriticalSection comparisonLock; // State/UI operations only; never acquired in processBlock.
    std::atomic<int> snapshotSlot { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenGateAudioProcessor)
};
