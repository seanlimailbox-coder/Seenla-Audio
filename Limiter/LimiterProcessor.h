#pragma once
#include "LoudnessHistogram.h"
#include <JuceHeader.h>
#include "PeakDetector.h"
#include "TimedPeakHold.h"
#include <array>
#include <cstdint>
#include <vector>
#include "../Clipper/ClippingDSP.h"
#include "Engine/StreamingLimiter.h"
#include "Presets.h"
#include "StyleNames.h"

class OpenLimiterAudioProcessor final : public juce::AudioProcessor,
                                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int historySize = 1024;
    OpenLimiterAudioProcessor();
    ~OpenLimiterAudioProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override { limiter.release();delaySize=0; }
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;
    juce::AudioProcessorParameter* getBypassParameter() const override
    { return parameters.getParameter("bypass"); }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return getLatencySamples()/sampleRate; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::ValueTree presetValues() const;
    bool loadPreset(const LimiterPreset&,juce::String& error);
    bool loadPreset(const juce::File&,juce::String& error);
    bool savePreset(const juce::File&,juce::String& error);
    bool renamePreset(const juce::String&,juce::String& error);
    bool deletePreset(juce::String& error);
    bool isUserPreset() const;
    bool presetModified() const noexcept;
    juce::String currentPresetName() const {const juce::ScopedLock lock(presetLock);return presetName;}
    juce::String currentPresetKey() const {const juce::ScopedLock lock(presetLock);return presetKey;}
    juce::String currentPresetNotes() const {const juce::ScopedLock lock(presetLock);return presetNotes;}
    juce::String presetName="Default",presetKey="factory:Essentials/Default",presetNotes;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState parameters;
    struct EditorState {
        std::atomic<int> width{900},height{600},historyView{1};
        std::atomic<bool> help{false},details{true},loudnessTrace{true},advancedControls{true};
        std::atomic<uint64_t> revision{0};
    } editorState;
    struct HistoryFrame { float raw=0,driven=0,output=0,reduction=0; std::array<float,3> loudness{{-100,-100,-100}}; };
    bool readHistoryFrame(uint64_t sequence,HistoryFrame&) const noexcept;
    uint64_t historySequence() const noexcept { return publishedSequence.load(); }
    uint64_t historyEpoch() const noexcept { return captureEpoch.load(); }
    double processingRate() const noexcept { return sampleRate; }
    double historyBufferFrames() const noexcept { return juce::jmax(2.4,(double)lastCallbackSize.load()/historyInterval+1.5); }
    double historyRate() const noexcept { return sampleRate/historyInterval; }
    // History stays live in its bounded ring when the host closes the editor.
    void setHistoryCapture(bool) noexcept {}
    void setMonitor(int mode) noexcept { monitorMode=mode; }
    void stopMonitoring() noexcept { monitorMode=0; }
    int monitoring() const noexcept { return monitorMode.load(); }
    float comparisonDb() const noexcept { return comparisonGainDb.load(); }
    float comparisonProgress() const noexcept { return compareProgress.load(); }
    double measuredSeconds() const noexcept { return measuredSamples.load()/sampleRate; }
    void pauseLoudness(bool pause) noexcept { loudnessPaused=pause; }
    bool isLoudnessPaused() const noexcept { return loudnessPaused.load(); }
    static constexpr float targetLufs() noexcept { return -9.f; }
    void setAutoReset(bool enabled) noexcept { autoReset=enabled; }
    bool autoResetEnabled() const noexcept { return autoReset.load(); }
    static constexpr int truePeakLatency() noexcept {return 250;}
    float truePeakLevel() const noexcept { return heldTruePeak.load(); }
    void selectSnapshot(int); void copySnapshot();
    int getSnapshot() const noexcept { return snapshotSlot; }
    void editorStateChanged() { updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true)); }
    int outputQuality() const noexcept;
    bool zeroLatencyRequested()const noexcept {return juce::roundToInt(parameters.getRawParameterValue("styleSelect")->load())==8&&parameters.getRawParameterValue("zeroLatency")->load()>.5f;}
    int qualityLatency(int quality) const noexcept { return quality>0?64:0; }
    static int lookaheadSamples(double rate,int style) noexcept
    { return open_limiter::coreLatency(rate,parameterValueForStyle(style)); }
    // Logical API keeps the old Punch/Transparent/Dynamic/Aggressive order.
    static int parameterValueForStyle(int style) noexcept
    { constexpr std::array<int,9> values{0,3,1,2,4,5,6,7,8};return values[(size_t)juce::jlimit(0,8,style)]; }
    int limitingStyle() const noexcept
    {
        constexpr std::array<int,9> styles{0,2,3,1,4,5,6,7,8};
        const int value=juce::roundToInt(parameters.getRawParameterValue("styleSelect")->load());
        return styles[(size_t)juce::jlimit(0,8,value)];
    }
    uint64_t safetyCorrectionCount() const noexcept {return safetyCorrections.load();}

    float inputLevel(int c) const noexcept { return rawInputMeter[(size_t)c].load(); }
    float outputRms(int c) const noexcept { return rmsMeter[(size_t)c].load(); }
    float maximumLoudness(int selection) const noexcept { return (selection==1?maxShortTerm:maxMomentary).load(); }
    float loudnessRange() const noexcept { return rangeHigh.load()-rangeLow.load(); }
    float loudnessRangeLow() const noexcept { return rangeLow.load(); }
    float loudnessRangeHigh() const noexcept { return rangeHigh.load(); }
    float outputLevel(int c) const noexcept { return outputMeter[(size_t)c].load(); }
    float inputPeak(int c) const noexcept { return heldRawInput[(size_t)c].load(); }
    float outputPeak(int c) const noexcept { return heldOutput[(size_t)c].load(); }
    float outputMarkerPeak(int c) const noexcept { return heldOutputMarker[(size_t)c].value(juce::Time::getMillisecondCounterHiRes()); }
    float reductionLevel(int c) const noexcept { return grMeter[(size_t)c].load(); }
    float reductionPeak(int c) const noexcept { return heldReduction[(size_t)c].value(juce::Time::getMillisecondCounterHiRes()); }
    float loudnessPeak(int selection) const noexcept
    { return selection==1||selection==2?heldLoudness[(size_t)(selection-1)].value(juce::Time::getMillisecondCounterHiRes()):-100.f; }
    float maximumReduction() const noexcept { return maxGr.load(); }
    float loudness(int selection) const noexcept;
    float liveLoudness(int selection) const noexcept { return selection==0?integrated.load():(selection==1?liveShortTerm:liveMomentary).load(); }
    int historyPosition() const noexcept { return historyWrite.load(); }
    uint64_t historyRevision() const noexcept { return historyGeneration.load(); }
    float historyInput(int i) const noexcept { return inputHistory[(size_t)i].load(); }
    float historyOutput(int i) const noexcept { return outputHistory[(size_t)i].load(); }
    float historyReduction(int i) const noexcept { return grHistory[(size_t)i].load(); }
    void resetStatistics() noexcept { resetRequested = true; }
    void resetLoudness() noexcept { loudnessResetRequested = true; momentary = shortTerm = integrated = liveMomentary = liveShortTerm = -100.f;for(auto& hold:heldLoudness)hold.reset(); }
    void resetPeakHolds() noexcept;

private:
    void resetOutputPeakHolds() noexcept;
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process(double x) noexcept
        {
            const double y = b0*x + z1;
            z1 = b1*x - a1*y + z2; z2 = b2*x - a2*y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0; }
    };
    mutable juce::CriticalSection presetLock;
    juce::ValueTree presetBaseline{"VALUES"};
    std::atomic<bool> restoringState{false};
    void parameterChanged(const juce::String&, float) override;
    void updateLatency();
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool hostBypass);
    void updateLoudness(double left, double right) noexcept;
    void clearLoudness() noexcept;
    void clearStatistics(bool clearHistory=true,bool clearMeasurement=true) noexcept;
    static float energyToLufs(double energy) noexcept;
    void publishHistory(float raw,float driven,float output,float reduction) noexcept;
    void processChunk(juce::AudioBuffer<float>&,bool,bool hostBypass=false);
    void saveSnapshot(int);
    struct AtomicFrame {
        std::atomic<uint64_t> stamp{0},epoch{0};
        std::atomic<float> raw{0},driven{0},output{0},reduction{0};
        std::array<std::atomic<float>,3> loudness;
    };
    std::array<AtomicFrame,historySize> coherentHistory;
    std::atomic<uint64_t> publishedSequence{0},captureEpoch{0};
    float historyRawPeak=0;
    std::atomic<int> monitorMode{0};
    std::atomic<float> comparisonGainDb{0},compareProgress{0},heldTruePeak{0};
    std::atomic<bool> loudnessPaused{false},autoReset{false};
    std::atomic<uint64_t> measuredSamples{0};
    bool wasPlaying=false;
    double compareRawEnergy=0,compareWetEnergy=0;
    uint64_t compareSamples=0; float comparisonGain=1;
    std::array<float,7> comparedSettings{};
    int compareSettle=0;
    juce::SmoothedValue<float> bypassBlend,deltaBlend,compareBlend;
    int snapshotSlot=0;
    std::array<juce::ValueTree,2> snapshots;
    open_limiter::StreamingLimiter limiter;
    int maximumBlock=1,activeQuality=0;
    std::atomic<int> lastCallbackSize{1};
    bool activeTruePeak=false;
    ClippingDSP::StereoPeakHistory meterPeakHistory;
    int meterSilentSamples=64;
    std::array<std::array<double,64>,7> peakKernels{};
    double sampleRate = 44100.0;
    int delaySize = 0;
    bool historyPrepared=false;
    int64_t sampleIndex = 0;
    std::atomic<uint64_t> safetyCorrections{0};
    Biquad shelf[2], highPass[2];
    std::array<float, 2> inBallistics {}, rawBallistics {}, outBallistics {}, grBallistics {};
    std::array<std::atomic<float>, 2> rawInputMeter {}, heldRawInput {}, inputMeter {}, outputMeter {}, grMeter {}, heldInput {}, heldOutput {};
    std::array<LimiterTimedPeakHold,2> heldReduction;
    std::array<LimiterTimedPeakHold,2> heldOutputMarker{{LimiterTimedPeakHold{-100.f},LimiterTimedPeakHold{-100.f}}};
    std::array<LimiterTimedPeakHold,2> heldLoudness{{LimiterTimedPeakHold{-100.f},LimiterTimedPeakHold{-100.f}}};
    std::atomic<float> maxGr {0.0f};
    std::array<std::atomic<float>, historySize> inputHistory {}, outputHistory {}, grHistory {};
    std::atomic<int> historyWrite {0};
    std::atomic<uint64_t> historyGeneration {0};
    int historyCountdown = 1, historyInterval = 1;
    float historyInPeak = 0, historyOutPeak = 0, historyGrPeak = 0;
    int loudnessCountdown = 1, loudnessInterval = 1, loudnessIndex = 0;
    uint64_t loudnessHops = 0;
    double hopEnergy = 0;
    std::array<double, 30> loudnessRing {};
    // Independent rolling windows for fluid bars; the standard 100 ms gates
    // and stored loudness statistics retain their original measurement cadence.
    std::vector<double> liveEnergy;
    double liveMomentarySum=0,liveShortTermSum=0;
    int liveIndex=0,liveCount=0,liveMomentaryLength=1,liveInterval=1,liveCountdown=1;
    std::atomic<float> liveMomentary{-100.f},liveShortTerm{-100.f};
    // 0.01 LU gating bins keep memory and audio-thread work bounded for any session length.
    static constexpr int loudnessBins = 10000;
    LimiterLoudnessHistogram<loudnessBins> gatedHistogram;
    std::array<std::vector<double>,2> rmsRing;
    std::array<double,2> rmsSum{};
    std::array<std::atomic<float>,2> rmsMeter{};
    int rmsIndex=0,rmsCount=0;
    LimiterLoudnessHistogram<loudnessBins> rangeHistogram;
    std::atomic<float> maxMomentary{-100},maxShortTerm{-100},rangeLow{-100},rangeHigh{-100};
    std::atomic<float> momentary {-100.0f}, shortTerm {-100.0f}, integrated {-100.0f};
    std::atomic<bool> resetRequested {false}, loudnessResetRequested {false};
    std::atomic<bool> outputPeakResetRequested {false};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenLimiterAudioProcessor)
};
