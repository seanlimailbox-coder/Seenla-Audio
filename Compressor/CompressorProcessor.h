#pragma once

#include <JuceHeader.h>
#include <array>
#include "CompressorFilters.h"
#include "CompressorRmsDetector.h"
#include "../Shared/TimedPeakHold.h"

class OpenCompressorAudioProcessor final : public juce::AudioProcessor,
                                           private juce::AudioProcessorValueTreeState::Listener,
                                           private juce::AsyncUpdater
{
public:
    static constexpr int historySize = 512;

    OpenCompressorAudioProcessor();
    ~OpenCompressorAudioProcessor() override;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return parameters.getParameter ("bypass"); }
    double getTailLengthSeconds() const override { return getLatencySamples() / currentSampleRate.load(); }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
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
    struct HistoryFrame { float input=0, output=0, sidechain=0, detector=0, reduction=0, detectorMinimum=0; bool detectorRms=false; };
    static constexpr int historyCapacity=1024, displayFrames=480, spectrumCapacity=8192;
    bool readHistoryFrame(uint64_t,HistoryFrame&,uint64_t captureState=0) const noexcept;
    uint64_t startHistoryCapture() noexcept { const auto state=(historyCaptureState.load()&~uint64_t(1))+3;historyCaptureState.store(state,std::memory_order_release);return state; }
    void stopHistoryCapture() noexcept { historyCaptureState.fetch_and(~uint64_t(1),std::memory_order_release); }
    uint64_t getHistoryCaptureState() const noexcept { return historyCaptureState.load(std::memory_order_acquire); }
    uint64_t getHistorySequence() const noexcept { return historySequence.load(); }
    uint64_t getHistoryEpoch() const noexcept { return historyEpoch.load(); }
    double historyFramesPerSecond() const noexcept { return currentSampleRate.load()/historyInterval; }
    struct SpectrumFrame { float left=0,right=0; uint64_t epoch=0,sequence=0,captureState=0; };
    void setSpectrumEnabled(bool enabled) noexcept { spectrumEnabled=enabled; }
    bool readSpectrumFrame(SpectrumFrame&) noexcept;
    double spectrumSampleRate() const noexcept { return currentSampleRate.load(); }
    bool externalBusAvailable() const noexcept { return getBusCount(true)>1 && getBus(true,1)->isEnabled(); }
    void selectSnapshot(int); void copySnapshot();
    int getSnapshot() const noexcept { return snapshotSlot.load(); }
    struct EditorState {
        std::atomic<int> page{0},width{720},height{440};
        std::atomic<bool> help{false},trace{false},knee{false},frozen{false};
        std::atomic<uint64_t> revision{0};
    } editorState;
    void editorStateChanged() { updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true)); }
    void rememberFrozenHistory(const std::array<HistoryFrame,displayFrames>&,int);
    bool restoreFrozenHistory(std::array<HistoryFrame,displayFrames>&,int&) const;

    float inputLevel() const noexcept { return inMeter.load(); }
    float outputLevel() const noexcept { return outMeter.load(); }
    float reductionLevel() const noexcept { return grMeter.load(); }
    float reductionLevel(int channel) const noexcept { return grChannelMeter[(size_t) juce::jlimit(0,1,channel)].load(); }
    float maximumReductionLevel() const noexcept { return maxGrMeter.load(); }
    float maximumReductionLevel(int channel) const noexcept { return maxGrChannelMeter[(size_t) juce::jlimit(0,1,channel)].load(); }
    float reductionPeakMarker(int channel) const noexcept
    { return reductionPeakMarkers[(size_t)juce::jlimit(0,1,channel)].value(juce::Time::getMillisecondCounterHiRes()); }
    float automaticMakeupLevel() const noexcept { return autoMakeupMeter.load(); }
    float effectiveReleaseTime() const noexcept { return effectiveReleaseMeter.load(); }
    bool usesLegacyDynamics() const noexcept { return dynamicsVersion.load()==1; }
    int dynamicsEngineVersion() const noexcept { return dynamicsVersion.load(); }
    void setAuditionSidechain(bool enabled);
    void setDetectorTraceVisible(bool enabled);
    bool isAuditioningSidechain() const noexcept { return auditionSidechain.load(); }
    double detectorResponseDb(double frequency) const noexcept;
    struct SidechainCutoffs { float highPass=20,lowPass=20000; };
    static constexpr double sidechainMinimumRatio=1.2599210498948732; // One third octave.
    static SidechainCutoffs constrainSidechainCutoffs(float highPass,float lowPass,bool editedHighPass=true) noexcept;
    SidechainCutoffs sidechainCutoffs() const noexcept;
    int historyPosition() const noexcept { return historyWrite.load(); }
    float historyIn (int i) const noexcept { return inHistory[(size_t) i].load(); }
    float historyOut (int i) const noexcept { return outHistory[(size_t) i].load(); }
    float historyGR (int i) const noexcept { return grHistory[(size_t) i].load(); }
    float historySidechain (int i) const noexcept { return sidechainHistory[(size_t) i].load(); }
    void resetMeters() noexcept;
    void resetMaximumReduction() noexcept { maxGrMeter.store(0.0f); for(auto& value:maxGrChannelMeter)value.store(0.0f); for(auto& marker:reductionPeakMarkers)marker.reset(); }
    static float gainReductionDb(float level, float threshold, float ratio, float knee, float range) noexcept;

private:
    struct DetectorFilter
    {
        double highPassState[2][2] {}, lowPassState[2][2] {};
        float process (float sample, const CompressorFilters::Cascade& highPassCoefficients,
                       const CompressorFilters::Cascade& lowPassCoefficients,
                       bool highPassEnabled, bool lowPassEnabled) noexcept;
    };
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void requestSidechainHistory();
    void normalizeSidechainFilters();
    static void normalizeFilterState(juce::ValueTree);
    std::atomic<bool> restoringState { false };
    std::atomic<bool> recallingFilters{false},normalizingFilters{false},lastEditedHighPass{true},filterNormalizationRequested{false},sidechainHistoryRequested{false};
    void updateLookaheadLatency();
    void resetMeterLevels() noexcept;
    void processBuffer(juce::AudioBuffer<float>&,juce::MidiBuffer&,bool);
    std::atomic<double> currentSampleRate { 44100.0 };
    juce::AudioBuffer<float> delayBuffer;
    int delayWrite = 0;
    int allocatedDelay = 0;
    float envL = 0.0f, envR = 0.0f, gainDbL = 0.0f, gainDbR = 0.0f;
    float appliedGainDbL = 0.0f, appliedGainDbR = 0.0f;
    // State-only compatibility flag: old sessions retain their original sound
    // without adding or reordering any host automation parameters.
    std::atomic<int> dynamicsVersion { 3 };
    int processedDynamicsVersion = 3;
    float releaseFastL = 0.0f, releaseFastR = 0.0f;
    float sustainL = 0.0f, sustainR = 0.0f;
    float releaseTailL = 0.0f, releaseTailR = 0.0f;
    float releaseMemoryL = 0.0f, releaseMemoryR = 0.0f;
    float compressionActivityL = 0.0f, compressionActivityR = 0.0f;
    float quietTimeL = 0.0f, quietTimeR = 0.0f;
    double detectorPowerMemoryL = 0.0, detectorPowerMemoryR = 0.0;
    float detectorPeakMemoryL = 0.0f, detectorPeakMemoryR = 0.0f;
    CompressorRmsDetector rmsDetector;
    juce::SmoothedValue<float> rmsBlend;
    DetectorFilter detectorFilters[2];
    DetectorFilter externalFilters[2];
    double autoInputPower = 0.0, autoWetPower = 0.0;
    float autoMakeupDb = 0.0f;
    int holdL = 0, holdR = 0;
    juce::SmoothedValue<float> inputGain, outputGain, wet, auditionMix;
    std::atomic<bool> auditionSidechain { false };
    juce::SmoothedValue<float> bypassBlend, thresholdValue, ratioValue, kneeValue, rangeValue, linkValue;

    std::atomic<float> inMeter { 0.0f }, outMeter { 0.0f }, grMeter { 0.0f }, maxGrMeter { 0.0f }, autoMakeupMeter { 0.0f }, effectiveReleaseMeter { 30.0f };
    std::array<std::atomic<float>, 2> grChannelMeter {}, maxGrChannelMeter {};
    std::array<TimedPeakHold, 2> reductionPeakMarkers;
    std::array<std::atomic<float>, historySize> inHistory {}, outHistory {}, grHistory {}, sidechainHistory {};
    std::atomic<int> historyWrite { 0 };
    int historyCountdown = 0;
    std::atomic<int> historyInterval{441};
    float historyInPeak = 0.0f, historyOutPeak = 0.0f, historyGrPeak = 0.0f, historySidechainPeak = 0.0f;
    struct AtomicFrame { std::atomic<float> input{0},output{0},sidechain{0},detector{0},reduction{0},minimum{0}; std::atomic<bool> detectorRms{false}; std::atomic<uint64_t> sequence{0},captureState{0}; };
    std::array<AtomicFrame,historyCapacity> coherentHistory;
    std::atomic<uint64_t> historySequence{0},historyEpoch{0};
    std::atomic<uint64_t> historyCaptureState{0}; // Off without an editor; upper bits identify each visible visit.
    uint64_t lastHistoryCaptureState=0; // Audio-thread owned.
    float historyDetectorPeak=0,historyDetectorMinimum=1000000,historySelectedSidechainPeak=0;
    juce::AbstractFifo spectrumFifo{spectrumCapacity};
    std::array<SpectrumFrame,spectrumCapacity> spectrumFrames{};
    std::atomic<bool> spectrumEnabled{false};
    uint64_t spectrumSequence=0;
    mutable juce::CriticalSection comparisonLock;
    std::array<juce::ValueTree,2> snapshots;
    std::atomic<int> snapshotSlot{0};
    std::array<HistoryFrame,displayFrames> frozenFrames{};
    int frozenValidFrames=0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenCompressorAudioProcessor)
};
