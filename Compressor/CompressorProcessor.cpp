#include "CompressorProcessor.h"
#include "CompressorFilters.h"
#include "CompressorEditor.h"

namespace P
{
constexpr auto input="input", threshold="threshold", ratio="ratio", knee="knee", attack="attack";
constexpr auto hold="hold", release="release", range="range", lookahead="lookahead", mix="mix";
constexpr auto output="output", link="link";
constexpr auto autoGain="autoGain", autoRelease="autoRelease", smooth="smooth", bypass="bypass";
constexpr auto maxLookahead="maxLookahead";
constexpr auto externalSidechain="externalSidechain", sideHighPass="sideHighPass", sideLowPass="sideLowPass";
constexpr auto sidechainHistoryView="sidechainHistoryView";
constexpr auto detectorMode="detectorMode", rmsTime="rmsTime";
static float gain(float db){return std::abs(db)<.0001f?1.0f:juce::Decibels::decibelsToGain(db);}
}

OpenCompressorAudioProcessor::OpenCompressorAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, &undoManager, "OPEN_COMPRESSOR", createParameterLayout())
{ for(auto id:{P::maxLookahead,P::smooth,P::externalSidechain,P::sidechainHistoryView,P::sideHighPass,P::sideLowPass})parameters.addParameterListener(id,this); resetMeters(); }

OpenCompressorAudioProcessor::~OpenCompressorAudioProcessor()
{ cancelPendingUpdate();for(auto id:{P::maxLookahead,P::smooth,P::externalSidechain,P::sidechainHistoryView,P::sideHighPass,P::sideLowPass})parameters.removeParameterListener(id,this); }

void OpenCompressorAudioProcessor::parameterChanged(const juce::String& id,float value)
{
    if(id==P::maxLookahead||id==P::smooth)updateLookaheadLatency();
    if(id==P::sidechainHistoryView&&value>.5f)editorState.trace=false;
    if(id==P::externalSidechain&&value>.5f&&!restoringState.load())requestSidechainHistory();
    if((id==P::sideHighPass||id==P::sideLowPass)&&!restoringState.load()&&!recallingFilters.load()){
        auto* manager=juce::MessageManager::getInstanceWithoutCreating();const bool onMessageThread=manager&&manager->isThisTheMessageThread();
        if(normalizingFilters.load()&&onMessageThread)return; // Our own bounded correction, not a fresh edit.
        lastEditedHighPass=id==P::sideHighPass;filterNormalizationRequested=true;
        if(onMessageThread)normalizeSidechainFilters();
        else triggerAsyncUpdate();
    }
}

OpenCompressorAudioProcessor::SidechainCutoffs OpenCompressorAudioProcessor::constrainSidechainCutoffs(float hp,float lp,bool editedHighPass) noexcept
{
    hp=std::isfinite(hp)?juce::jlimit(20.f,20000.f,std::round(hp)):20.f;
    lp=std::isfinite(lp)?juce::jlimit(20.f,20000.f,std::round(lp)):20000.f;
    if(editedHighPass){lp=juce::jmax(lp,(float)std::ceil(20*sidechainMinimumRatio));hp=juce::jmin(hp,(float)std::floor(lp/sidechainMinimumRatio));}
    else{hp=juce::jmin(hp,(float)std::floor(20000/sidechainMinimumRatio));lp=juce::jmax(lp,(float)std::ceil(hp*sidechainMinimumRatio));}
    return {hp,lp};
}
OpenCompressorAudioProcessor::SidechainCutoffs OpenCompressorAudioProcessor::sidechainCutoffs() const noexcept
{
    return constrainSidechainCutoffs(parameters.getRawParameterValue(P::sideHighPass)->load(),parameters.getRawParameterValue(P::sideLowPass)->load(),lastEditedHighPass.load());
}
void OpenCompressorAudioProcessor::normalizeSidechainFilters()
{
    // This runs only on the message thread. Audio always applies the same
    // bounded pair immediately, even before a host automation correction can
    // be reflected back to the host/UI. Never notify the host from processing.
    filterNormalizationRequested=false;
    if(normalizingFilters.exchange(true))return;
    const auto pair=sidechainCutoffs();
    for(auto item:std::initializer_list<std::pair<const char*,float>>{{P::sideHighPass,pair.highPass},{P::sideLowPass,pair.lowPass}}){
        auto* parameter=parameters.getParameter(item.first);
        if(std::abs(parameters.getRawParameterValue(item.first)->load()-item.second)>.5f)parameter->setValueNotifyingHost(parameter->convertTo0to1(item.second));
    }
    normalizingFilters=false;
}
void OpenCompressorAudioProcessor::normalizeFilterState(juce::ValueTree tree)
{
    if(!tree.isValid())return;
    auto hp=tree.getChildWithProperty("id",P::sideHighPass),lp=tree.getChildWithProperty("id",P::sideLowPass);
    const auto pair=constrainSidechainCutoffs(hp.isValid()?(float)hp["value"]:20.f,lp.isValid()?(float)lp["value"]:20000.f);
    for(auto item:std::initializer_list<std::pair<const char*,float>>{{P::sideHighPass,pair.highPass},{P::sideLowPass,pair.lowPass}}){auto node=tree.getChildWithProperty("id",item.first);if(!node.isValid()){node=juce::ValueTree("PARAM");node.setProperty("id",item.first,nullptr);tree.addChild(node,-1,nullptr);}node.setProperty("value",item.second,nullptr);}
}

void OpenCompressorAudioProcessor::requestSidechainHistory()
{
    sidechainHistoryRequested=true;
    // Host automation may arrive on the audio thread. Notify the display
    // parameter on the message thread, once per enable event, never per block.
    if(auto* manager=juce::MessageManager::getInstanceWithoutCreating();manager&&manager->isThisTheMessageThread())
        handleAsyncUpdate();
    else triggerAsyncUpdate();
}
void OpenCompressorAudioProcessor::handleAsyncUpdate()
{
    if(restoringState.load())return;
    if(filterNormalizationRequested.exchange(false))normalizeSidechainFilters();
    if(!sidechainHistoryRequested.exchange(false))return;
    editorState.trace=false;
    auto* view=parameters.getParameter(P::sidechainHistoryView);
    if(view->getValue()<.5f)view->setValueNotifyingHost(1.f);
}
void OpenCompressorAudioProcessor::setAuditionSidechain(bool enabled)
{
    if(enabled&&!auditionSidechain.exchange(true))requestSidechainHistory();
    else if(!enabled)auditionSidechain=false;
}
void OpenCompressorAudioProcessor::setDetectorTraceVisible(bool enabled)
{
    if(enabled){sidechainHistoryRequested=false;auto* view=parameters.getParameter(P::sidechainHistoryView);if(view->getValue()>.5f)view->setValueNotifyingHost(0.f);}
    editorState.trace=enabled;
}

void OpenCompressorAudioProcessor::updateLookaheadLatency()
{
    static constexpr float times[]{0,1,2,5,10,20};
    const int index=juce::jlimit(0,5,(int)parameters.getRawParameterValue(P::maxLookahead)->load());
    const bool smoothEnabled=parameters.getRawParameterValue(P::smooth)->load()>.5f;
    setLatencySamples(smoothEnabled?juce::roundToInt(currentSampleRate*times[index]*.001):0);
}

juce::AudioProcessorValueTreeState::ParameterLayout OpenCompressorAudioProcessor::createParameterLayout()
{
    using F=juce::AudioParameterFloat; using B=juce::AudioParameterBool; using C=juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto f=[&] (const char* id, const char* name, float a, float b, float step, float skew, float def, const char* unit)
    { l.add (std::make_unique<F> (juce::ParameterID{id,1}, name, juce::NormalisableRange<float>(a,b,step,skew), def, unit)); };
    f(P::input,"Input",-24,24,.01f,1,0,"dB"); f(P::threshold,"Threshold",-36,0,.01f,1,0,"dB");
    f(P::ratio,"Ratio",1,100,.01f,.35f,4," : 1"); f(P::knee,"Knee",0,72,.1f,1,6,"dB");
    f(P::attack,"Attack",.01f,500,.01f,.25f,1,"ms"); f(P::hold,"Hold",0,500,.1f,.4f,0,"ms");
    f(P::release,"Release",5,5000,.1f,.3f,30,"ms"); f(P::range,"Range",0,60,.01f,.6f,60,"dB");
    f(P::lookahead,"Lookahead",0,20,.01f,.7f,0,"ms"); f(P::mix,"Mix",0,100,.1f,1,100,"%");
    f(P::output,"Output",-24,24,.01f,1,0,"dB"); f(P::link,"Stereo Link",0,100,.1f,1,100,"%");
    const juce::NormalisableRange<float> cutoffRange{20,20000,
        [](float a,float b,float n){return a*std::pow(b/a,n);},
        [](float a,float b,float hz){return std::log(hz/a)/std::log(b/a);},
        [](float a,float b,float hz){return juce::jlimit(a,b,std::round(hz));}};
    l.add(std::make_unique<F>(juce::ParameterID{P::sideHighPass,1},"Sidechain High Pass",cutoffRange,20,"Hz"));
    l.add(std::make_unique<F>(juce::ParameterID{P::sideLowPass,1},"Sidechain Low Pass",cutoffRange,20000,"Hz"));
    l.add(std::make_unique<C>(juce::ParameterID{P::maxLookahead,1},"Maximum Lookahead",juce::StringArray{"0 ms","1 ms","2 ms","5 ms","10 ms","20 ms"},0));
    for (auto item : std::initializer_list<std::pair<const char*,const char*>>{{P::autoGain,"Auto Gain"},{P::autoRelease,"Auto Release"},{P::smooth,"Smooth Gain"},{P::bypass,"Bypass"},{P::externalSidechain,"External Sidechain"},{P::sidechainHistoryView,"Sidechain History View"}})
        l.add(std::make_unique<B>(juce::ParameterID{item.first,1},item.second,false));
    // Append only: existing automation indices/IDs remain unchanged.
    l.add(std::make_unique<C>(juce::ParameterID{P::detectorMode,1},"Detection",juce::StringArray{"Peak","RMS"},0));
    f(P::rmsTime,"RMS Response Time",5,100,.1f,.5f,20,"ms");
    return l;
}

void OpenCompressorAudioProcessor::prepareToPlay (double sr, int block)
{
    // Hosts can re-prepare more than once after a latency change. Retain the
    // display timeline on every same-rate restart, not just the first callback.
    const bool preserveHistory=allocatedDelay>0&&sr==currentSampleRate.load();
    currentSampleRate=sr; allocatedDelay=juce::roundToInt(sr*.020)+block+4;
    // Separate raw, detector, and input-gained stereo paths stay aligned. Smooth
    // enables the selected maximum latency; the detector tap moves within it.
    delayBuffer.setSize(8,allocatedDelay); delayBuffer.clear(); delayWrite=0;
    detectorFilters[0]={}; detectorFilters[1]={};
    externalFilters[0]={}; externalFilters[1]={};
    inputGain.reset(sr,.02); outputGain.reset(sr,.02); wet.reset(sr,.02);
    bypassBlend.reset(sr,.005);
    auditionMix.reset(sr,.005);
    auditionMix.setCurrentAndTargetValue(0.f);
    auditionSidechain=false;
    bypassBlend.setCurrentAndTargetValue(parameters.getRawParameterValue(P::bypass)->load());
    for(auto pair:std::initializer_list<std::pair<juce::SmoothedValue<float>*,const char*>>{{&thresholdValue,P::threshold},{&ratioValue,P::ratio},{&kneeValue,P::knee},{&rangeValue,P::range},{&linkValue,P::link}})
    { pair.first->reset(sr,.005);pair.first->setCurrentAndTargetValue(parameters.getRawParameterValue(pair.second)->load()); }
    inputGain.setCurrentAndTargetValue (P::gain (
        parameters.getRawParameterValue (P::input)->load()));
    outputGain.setCurrentAndTargetValue (P::gain (
        parameters.getRawParameterValue (P::output)->load()));
    wet.setCurrentAndTargetValue (parameters.getRawParameterValue (P::mix)->load() * .01f);
    envL=envR=gainDbL=gainDbR=appliedGainDbL=appliedGainDbR=0; holdL=holdR=0;
    releaseFastL=releaseFastR=releaseTailL=releaseTailR=0;
    sustainL=sustainR=0;
    releaseMemoryL=releaseMemoryR=0;
    compressionActivityL=compressionActivityR=quietTimeL=quietTimeR=0;
    detectorPowerMemoryL=detectorPowerMemoryR=0;detectorPeakMemoryL=detectorPeakMemoryR=0;
    rmsDetector.prepare(sr,parameters.getRawParameterValue(P::rmsTime)->load());
    rmsBlend.reset(sr,.01);
    rmsBlend.setCurrentAndTargetValue(parameters.getRawParameterValue(P::detectorMode)->load()>.5f?1.f:0.f);
    processedDynamicsVersion=dynamicsVersion.load();
    autoInputPower=autoWetPower=0.0;autoMakeupDb=0.0f;autoMakeupMeter=0.0f;
    if(preserveHistory)resetMeterLevels();
    else{resetMeters();historyWrite=0;historySequence=0;++historyEpoch;}
    spectrumSequence=0;lastHistoryCaptureState=getHistoryCaptureState();
    historyInPeak=historyOutPeak=historyGrPeak=historySidechainPeak=historySelectedSidechainPeak=historyDetectorPeak=0.0f;historyDetectorMinimum=1000000;
    historyInterval=juce::jmax(1,juce::roundToInt(sr/60.0)); historyCountdown=historyInterval;
    updateLookaheadLatency();
}

bool OpenCompressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& b) const
{
    const auto in=b.getMainInputChannelSet(), out=b.getMainOutputChannelSet();
    const auto side=b.inputBuses.size()>1?b.inputBuses[1]:juce::AudioChannelSet::disabled();
    return in==juce::AudioChannelSet::stereo() && out==in
        && (side.isDisabled() || side==juce::AudioChannelSet::mono() || side==juce::AudioChannelSet::stereo());
}

float OpenCompressorAudioProcessor::DetectorFilter::process(float sample,const CompressorFilters::Cascade& highPassCoefficients,
    const CompressorFilters::Cascade& lowPassCoefficients,bool highPassEnabled,bool lowPassEnabled) noexcept
{
    double value=sample;
    // Trapezoidal state-variable sections avoid coefficient-dependent DF-II
    // delay-state bursts when a cutoff is moved across a large frequency range.
    const auto apply=[&](const auto& coefficients,auto& states,bool highPass){for(size_t i=0;i<2;++i){const auto& c=coefficients.integrator[i];auto& s=states[i];const double v3=value-s[1],v1=c[0]*s[0]+c[1]*v3,v2=s[1]+c[1]*s[0]+c[2]*v3;s[0]=2*v1-s[0];s[1]=2*v2-s[1];value=highPass?value-c[3]*v1-v2:v2;}};
    if(highPassEnabled)apply(highPassCoefficients,highPassState,true);else for(auto& state:highPassState)state[0]=state[1]=0;
    if(lowPassEnabled)apply(lowPassCoefficients,lowPassState,false);else for(auto& state:lowPassState)state[0]=state[1]=0;
    return (float)value;
}

float OpenCompressorAudioProcessor::gainReductionDb(float x,float t,float r,float k,float maxR) noexcept
{
    // Taper the soft knee near 0 dB so unity below full scale is continuous,
    // including when the threshold is automated through its final steps.
    k=juce::jmin(k,juce::jmax(0.0f,-2.0f*t));
    float over=x-t, compressed=0;
    if(k>0 && over>-k*.5f && over<k*.5f)
        compressed = -((over+k*.5f)*(over+k*.5f)/(2*k))*(1.0f-1.0f/r);
    else if(over>=k*.5f) compressed=-over*(1.0f-1.0f/r);
    return -juce::jlimit(-maxR,0.0f,compressed);
}

double OpenCompressorAudioProcessor::detectorResponseDb(double frequency) const noexcept
{
    const double sr=currentSampleRate.load();
    frequency=juce::jmax(0.0,frequency);
    const auto cutoffs=sidechainCutoffs();const double hp=cutoffs.highPass,lp=cutoffs.lowPass;
    double magnitude=1.0;
    // Never fold frequencies above Nyquist back up into the display.
    if(lp<19999.0&&frequency>=sr*.5)return -120.0;
    frequency=juce::jmin(frequency,sr*.5);
    if(hp>20.5)magnitude*=CompressorFilters::magnitude(CompressorFilters::design(sr,hp,true),frequency,sr);
    if(lp<19999.0)magnitude*=CompressorFilters::magnitude(CompressorFilters::design(sr,lp,false),frequency,sr);
    return 20.0*std::log10(juce::jmax(1.0e-6,magnitude));
}

void OpenCompressorAudioProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi) { processBuffer(b,midi,false); }
void OpenCompressorAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi) { processBuffer(b,midi,true); }
void OpenCompressorAudioProcessor::processBuffer(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi,bool forceBypass)
{
    // One display-only state snapshot per block. Detector/gain/routing never
    // depend on whether an editor exists; only monitor collection is gated.
    const auto captureState=getHistoryCaptureState();
    const bool captureHistory=(captureState&1)!=0;
    const bool captureSpectrum=captureHistory&&spectrumEnabled.load(std::memory_order_relaxed);
    if(captureState!=lastHistoryCaptureState){
        historyInPeak=historyOutPeak=historyGrPeak=historySidechainPeak=0;
        historyDetectorPeak=historySelectedSidechainPeak=0;historyDetectorMinimum=1000000;
        historyCountdown=historyInterval.load();lastHistoryCaptureState=captureState;
    }
    juce::ScopedNoDenormals nd; const int n=b.getNumSamples(); if(b.getNumChannels()<2||allocatedDelay==0||n==0)return;
    auto side=getBusBuffer(b,true,1);
    auto val=[&](const char* id){return parameters.getRawParameterValue(id)->load();};
    const bool bypass=forceBypass || val(P::bypass)>.5f;
    const bool external=val(P::externalSidechain)>.5f && side.getNumChannels()>0;
    const auto cutoffs=sidechainCutoffs();const float highPassHz=cutoffs.highPass,lowPassHz=cutoffs.lowPass;
    const bool highPassEnabled=highPassHz>20.5f,lowPassEnabled=lowPassHz<19999.0f;
    const auto highPassCoefficient=CompressorFilters::design(currentSampleRate.load(),highPassHz,true);
    const auto lowPassCoefficient=CompressorFilters::design(currentSampleRate.load(),lowPassHz,false);
    thresholdValue.setTargetValue(val(P::threshold));ratioValue.setTargetValue(val(P::ratio));
    kneeValue.setTargetValue(val(P::knee));rangeValue.setTargetValue(val(P::range));linkValue.setTargetValue(val(P::link));
    const int maxLookaheadIndex=(int)val(P::maxLookahead);
    static constexpr float maxLookaheadMs[] {0,1,2,5,10,20};
    const float attackMs=val(P::attack);
    const float baseRelease=val(P::release);
    const double sampleRate=currentSampleRate.load();
    rmsDetector.setResponseTime(val(P::rmsTime));
    const bool detectorRms=val(P::detectorMode)>.5f;
    rmsBlend.setTargetValue(detectorRms?1.f:0.f);
    const float manualReleaseCoefficient=std::exp(-1.0f/(float)(sampleRate*baseRelease*.001));
    const bool automaticRelease=val(P::autoRelease)>.5f;
    const bool smoothGain=val(P::smooth)>.5f;
    const float selectedMaximum=maxLookaheadMs[juce::jlimit(0,5,maxLookaheadIndex)];
    const int holdSamples=juce::roundToInt(currentSampleRate*val(P::hold)*.001);
    const int fixedLatency=smoothGain?juce::roundToInt(currentSampleRate*selectedMaximum*.001):0;
    const int look=smoothGain?juce::roundToInt(currentSampleRate*juce::jmin(val(P::lookahead),selectedMaximum)*.001):0;
    // Restore the original finite-sample attack and its gain-change envelope.
    // At the shortest settings a sample still has finite duration; the extra
    // sample prevents the attack from degenerating into an instant clamp.
    const float atk=std::exp(-1.0f/(float)(sampleRate*attackMs*.001+1.0));
    const int version=dynamicsVersion.load();
    const bool legacyDynamics=version==1;
    const bool musicDynamics=version>=3;
    if(processedDynamicsVersion!=version){
        // State recall can occur while running. Seed both new gain stages
        // from the current reduction rather than resurrecting stale tails.
        releaseFastL=releaseTailL=gainDbL;releaseFastR=releaseTailR=gainDbR;
        sustainL=sustainR=0;holdL=holdR=0;processedDynamicsVersion=version;
        compressionActivityL=compressionActivityR=quietTimeL=quietTimeR=0;
    }
    // At ordinary settings the gain envelope itself rejects carrier ripple.
    // Blend in finite detector conditioning only at sub-0.1 ms attack: an
    // instantaneous rectifier there would turn the onset into a hard clamp.
    const float detectorAttack=std::exp(-1.0f/(float)(sampleRate*.00001+1.0));
    const float detectorRelease=std::exp(-1.0f/(float)(sampleRate*.00015));
    const float detectorConditioning=juce::jlimit(0.f,1.f,(.1f-attackMs)/.09f);
    const float attackScale=.5f+.15f*juce::jlimit(0.f,1.f,(attackMs-10.f)/20.f);
    const float gainAttack=std::exp(-1.0f/(float)(sampleRate*juce::jmax(.01f,attackMs*attackScale)*.001+1.0));
    // Repeated above-threshold peaks get a small, bounded conditioning time;
    // isolated onsets retain the fast envelope. Adding at most 0.7 ms keeps
    // the complete Attack mapping monotonic rather than slowing long attacks.
    const float programmeAttack=std::exp(-1.0f/(float)(sampleRate*juce::jmax(.01f,attackMs*attackScale+.7f*juce::jmin(attackMs,1.f))*.001+1.0));
    const float activityCoefficient=std::exp(-1.0f/(float)(sampleRate*.03));
    const float releaseMemoryCoefficient=std::exp(-1.0f/(float)(sampleRate*.25));
    const double detectorPowerCoefficient=std::exp(-1.0/(sampleRate*.05));
    const float detectorPeakCoefficient=std::exp(-1.0f/(float)(sampleRate*.05));
    const float manualFastTransient=std::exp(-1.0f/(float)(sampleRate*baseRelease*.00025));
    const float manualFastSustained=std::exp(-1.0f/(float)(sampleRate*baseRelease*.00033));
    const float manualSlowTransient=std::exp(-1.0f/(float)(sampleRate*baseRelease*.00065));
    const float manualSlowSustained=std::exp(-1.0f/(float)(sampleRate*(baseRelease*.80f+40.f)*.001));
    // Inter-peak recovery scales sublinearly with Release; genuine recovery
    // retains its full setting below. This avoids under-compressing dense music
    // at 5 ms and accumulating excess reduction at 100+ ms.
    const float releaseExponent=baseRelease<30.f?.4f:.55f;
    const float busyBase=30.f*std::pow(baseRelease/30.f,releaseExponent);
    // A bounded attack/release interaction retains the ordinary 10/100 ms
    // calibration without undoing the fast-attack continuous-music correction.
    // Both mappings remain monotonic; this is time shaping, not makeup gain.
    const float busyRelease=busyBase*(1.f+.7f*juce::jlimit(0.f,1.f,(attackMs-1.f)/9.f)*juce::jlimit(0.f,1.f,(baseRelease-30.f)/70.f));
    const float musicFastTransient=std::exp(-1.0f/(float)(sampleRate*busyRelease*.000175));
    const float musicFastSustained=std::exp(-1.0f/(float)(sampleRate*busyRelease*.000231));
    const float musicSlowTransient=std::exp(-1.0f/(float)(sampleRate*busyRelease*.00065));
    const float musicSlowSustained=std::exp(-1.0f/(float)(sampleRate*busyRelease*.0008));
    // Short manual releases still need a gentle sustained recovery, but its
    // multiplier is bounded (0.8..7.8), not an additive millisecond minimum.
    const float sustainedTailScale=.8f+7.f/(1.f+std::pow(baseRelease/15.f,1.25f));
    const float quietSlowSustained=std::exp(-1.0f/(float)(sampleRate*baseRelease*sustainedTailScale*.001));
    const float gainSmoothing=std::exp(-1.0f/(float)(currentSampleRate*.001));
    const bool autoGainEnabled=val(P::autoGain)>.5f;
    const double levelCoefficient=std::exp(-1.0/(currentSampleRate*1.5));
    const float autoGainStep=1.0f/(float)currentSampleRate;
    const float autoGainReturnStep=6.0f/(float)currentSampleRate;
    constexpr double silenceGatePower=1.0e-6; // -60 dBFS RMS
    const float inputDb=val(P::input),outputDb=val(P::output);
    inputGain.setTargetValue(P::gain(inputDb));
    outputGain.setTargetValue(P::gain(outputDb)); wet.setTargetValue(val(P::mix)*.01f);
    bypassBlend.setTargetValue(bypass?1.0f:0.0f);
    if(bypass)auditionSidechain=false;
    auditionMix.setTargetValue(auditionSidechain.load()&&!bypass&&!isNonRealtime()?1.f:0.f);
    float blockIn=0,blockOut=0,blockGr=0,blockGrL=0,blockGrR=0;
    juce::ignoreUnused(midi);
    for(int i=0;i<n;++i)
    {
        const float ig=inputGain.getNextValue(), og=outputGain.getNextValue(), mx=wet.getNextValue();
        const float threshold=thresholdValue.getNextValue(),ratio=ratioValue.getNextValue();
        const float knee=kneeValue.getNextValue(),range=rangeValue.getNextValue(),link=linkValue.getNextValue()*.01f;
        const float activeReduction=juce::jmax(-gainDbL,-gainDbR);
        releaseMemoryL=releaseMemoryCoefficient*releaseMemoryL+(1-releaseMemoryCoefficient)*(-gainDbL);
        releaseMemoryR=releaseMemoryCoefficient*releaseMemoryR+(1-releaseMemoryCoefficient)*(-gainDbR);
        auto adaptiveRelease=[&](float reduction,float memory,double power,float peak){
            const float crest=power>1e-10?juce::jlimit(1.f,4.f,peak/(float)std::sqrt(power)):1.f;
            return juce::jlimit(60.f,2000.f,90.f*std::pow(1.f+(reduction+memory)/12.f,1.5f)/(1+.15f*juce::jmax(0.f,crest-1.5f)));
        };
        const float releaseMs=automaticRelease?juce::jlimit(60.0f,2000.0f,
            legacyDynamics?60.0f*std::pow(1.0f+activeReduction/6.0f,1.7f)
                          :adaptiveRelease(-gainDbL,releaseMemoryL,detectorPowerMemoryL,detectorPeakMemoryL)):baseRelease;
        const float releaseMsR=automaticRelease&&!legacyDynamics?adaptiveRelease(-gainDbR,releaseMemoryR,detectorPowerMemoryR,detectorPeakMemoryR):releaseMs;
        const float rel=automaticRelease?std::exp(-1.0f/(float)(sampleRate*releaseMs*.001)):manualReleaseCoefficient;
        if(i==n-1)effectiveReleaseMeter=juce::jmax(releaseMs,releaseMsR);
        const float sourceL=b.getSample(0,i),sourceR=b.getSample(1,i);
        float inL=sourceL*ig,inR=sourceR*ig;
        const float sideL=side.getNumChannels()>0?side.getSample(0,i):0.0f;
        const float sideR=side.getNumChannels()>0?side.getSample(juce::jmin(1,side.getNumChannels()-1),i):0.0f;
        // Keep external filter state warm even when monitoring it independently
        // of the selected detector source. Detection and history share this key.
        const float filteredSideL=externalFilters[0].process(sideL,highPassCoefficient,lowPassCoefficient,highPassEnabled,lowPassEnabled);
        const float filteredSideR=externalFilters[1].process(sideR,highPassCoefficient,lowPassCoefficient,highPassEnabled,lowPassEnabled);
        float detectorL=external?filteredSideL:detectorFilters[0].process(inL,highPassCoefficient,lowPassCoefficient,highPassEnabled,lowPassEnabled);
        float detectorR=external?filteredSideR:detectorFilters[1].process(inR,highPassCoefficient,lowPassCoefficient,highPassEnabled,lowPassEnabled);
        if(captureSpectrum) {
            const auto sequence=++spectrumSequence;
            int first,count,second,count2; spectrumFifo.prepareToWrite(1,first,count,second,count2);
            if(count+count2>0) { spectrumFrames[(size_t)(count?first:second)]={detectorL,detectorR,historyEpoch.load(),sequence,captureState}; spectrumFifo.finishedWrite(1); }
        }
        delayBuffer.setSample(0,delayWrite,sourceL);delayBuffer.setSample(1,delayWrite,sourceR);
        delayBuffer.setSample(2,delayWrite,detectorL);delayBuffer.setSample(3,delayWrite,detectorR);
        delayBuffer.setSample(4,delayWrite,inL);delayBuffer.setSample(5,delayWrite,inR);
        delayBuffer.setSample(6,delayWrite,filteredSideL);delayBuffer.setSample(7,delayWrite,filteredSideR);
        const int read=(delayWrite-fixedLatency+allocatedDelay)%allocatedDelay;
        const int detectorRead=(delayWrite-(fixedLatency-look)+allocatedDelay)%allocatedDelay;
        detectorL=delayBuffer.getSample(2,detectorRead);detectorR=delayBuffer.getSample(3,detectorRead);
        const float rawL=delayBuffer.getSample(0,read),rawR=delayBuffer.getSample(1,read);
        const float dryL=delayBuffer.getSample(4,read),dryR=delayBuffer.getSample(5,read);
        float dl=std::abs(detectorL),dr=std::abs(detectorR);
        rmsDetector.process(detectorL,detectorR);
        const float rmsAmount=rmsBlend.getNextValue();
        const float rmsL=rmsAmount>0?rmsDetector.level(0):0;
        const float rmsR=rmsAmount>0?rmsDetector.level(1):0;
        auto follow=[&](float level,float& envelope,int& holdCount)
        {
            if(level>=envelope){envelope=atk*envelope+(1-atk)*level;holdCount=holdSamples;}
            else if(holdCount>0)--holdCount;
            else envelope=rel*envelope+(1-rel)*level;
        };
        if(legacyDynamics){
            // Legacy sessions retain detector-domain Attack/Hold/Release.
            follow(rmsAmount<=0?dl:dl+rmsAmount*(rmsL-dl),envL,holdL);
            follow(rmsAmount<=0?dr:dr+rmsAmount*(rmsR-dr),envR,holdR);
        }
        else {
            auto detect=[&](float level,float& envelope){const float c=level>envelope?detectorAttack:detectorRelease;envelope=c*envelope+(1-c)*level;};
            detect(dl,envL);detect(dr,envR);
        }
        const float peakL=legacyDynamics?envL:dl+detectorConditioning*(envL-dl);
        const float peakR=legacyDynamics?envR:dr+detectorConditioning*(envR-dr);
        // Explicit Peak branch preserves the original floating-point path.
        // RMS averaging replaces detection only, never the gain engine.
        const float levelL=legacyDynamics||rmsAmount<=0?peakL:rmsAmount>=1?rmsL:peakL+rmsAmount*(rmsL-peakL);
        const float levelR=legacyDynamics||rmsAmount<=0?peakR:rmsAmount>=1?rmsR:peakR+rmsAmount*(rmsR-peakR);
        float linked=juce::jmax(levelL,levelR); float dL=levelL+(linked-levelL)*link,dR=levelR+(linked-levelR)*link;
        // Analyze the linked keys so Link=0 stays independent and Link=100
        // produces identical timing, even with an asymmetric stereo source.
        // RMS must not flatten Auto Release's crest-factor measurement.
        // Peak keeps its exact pre-existing analysis; RMS measures the raw
        // rectified filtered key, with the same independent/linked mapping.
        const float rawLinked=juce::jmax(dl,dr);
        const float analysisL=rmsAmount<=0?dL:dl+(rawLinked-dl)*link;
        const float analysisR=rmsAmount<=0?dR:dr+(rawLinked-dr)*link;
        detectorPowerMemoryL=detectorPowerCoefficient*detectorPowerMemoryL+(1-detectorPowerCoefficient)*(double)analysisL*analysisL;
        detectorPowerMemoryR=detectorPowerCoefficient*detectorPowerMemoryR+(1-detectorPowerCoefficient)*(double)analysisR*analysisR;
        detectorPeakMemoryL=juce::jmax(analysisL,detectorPeakMemoryL*detectorPeakCoefficient);
        detectorPeakMemoryR=juce::jmax(analysisR,detectorPeakMemoryR*detectorPeakCoefficient);
        const float targetDbL=-gainReductionDb(juce::Decibels::gainToDecibels(dL,-120.0f),threshold,ratio,knee,range);
        const float targetDbR=-gainReductionDb(juce::Decibels::gainToDecibels(dR,-120.0f),threshold,ratio,knee,range);
        if(legacyDynamics){
            // Legacy recovery follows the released detector directly: never
            // apply its Release or Hold a second time in the gain envelope.
            gainDbL=targetDbL<gainDbL?atk*gainDbL+(1.0f-atk)*targetDbL:targetDbL;
            gainDbR=targetDbR<gainDbR?atk*gainDbR+(1.0f-atk)*targetDbR:targetDbR;
        } else {
            // Two gain-domain release stages: prompt initial recovery followed
            // by a gentle tail. Neither terminates abruptly at the threshold.
            auto shape=[&](float target,float& fast,float& slow,int& holdCount,float& sustain,float memory,float reduction,float time,float& activity,float& quietTime){
                // Capture recent compression depth while attacking; keep it
                // during recovery so sustained material gets a longer tail
                // than isolated hits, including at short Release settings.
                if(target<fast)sustain=juce::jlimit(0.f,1.f,memory/juce::jmax(.1f,reduction));
                float fastRelease=automaticRelease?std::exp(-1.0f/(float)(sampleRate*time*.00033)):manualFastTransient+sustain*(manualFastSustained-manualFastTransient);
                float slowRelease=automaticRelease?std::exp(-1.0f/(float)(sampleRate*time*.00115)):manualSlowTransient+sustain*(manualSlowSustained-manualSlowTransient);
                float attackCoefficient=gainAttack;
                if(musicDynamics){
                    const bool active=target<-.0001f;
                    activity=activityCoefficient*activity+(1-activityCoefficient)*(active?1.f:0.f);
                    quietTime=active?0.f:juce::jmin(10.f,quietTime+(float)(1000.0/sampleRate));
                    attackCoefficient+=juce::jlimit(0.f,1.f,6.f*activity)*(programmeAttack-gainAttack);
                    if(!automaticRelease){
                        fastRelease=musicFastTransient+sustain*(musicFastSustained-musicFastTransient);
                        slowRelease=musicSlowTransient+sustain*(musicSlowSustained-musicSlowTransient);
                        // Bridge carrier zero crossings. Genuine below-threshold
                        // passages fade into a release-proportional recovery tail.
                        // No fixed 40 ms extension and no amplitude/gain offset.
                        const float quiet=juce::jlimit(0.f,1.f,(quietTime-2.f)/8.f);
                        const float quietFast=manualFastTransient+sustain*(manualFastSustained-manualFastTransient);
                        const float quietSlow=manualSlowTransient+sustain*(quietSlowSustained-manualSlowTransient);
                        fastRelease+=quiet*(quietFast-fastRelease);
                        slowRelease+=quiet*(quietSlow-slowRelease);
                    }
                }
                if(target<fast){fast=attackCoefficient*fast+(1-attackCoefficient)*target;holdCount=holdSamples;}
                else if(holdCount>0)--holdCount;
                else fast=fastRelease*fast+(1-fastRelease)*target;
                const bool holding=holdCount>0;
                if(target<slow)slow=attackCoefficient*slow+(1-attackCoefficient)*target;
                else if(!holding)slow=slowRelease*slow+(1-slowRelease)*target;
            };
            shape(targetDbL,releaseFastL,releaseTailL,holdL,sustainL,releaseMemoryL,-gainDbL,releaseMs,compressionActivityL,quietTimeL);
            shape(targetDbR,releaseFastR,releaseTailR,holdR,sustainR,releaseMemoryR,-gainDbR,releaseMsR,compressionActivityR,quietTimeR);
            auto combine=[&](float fast,float slow,float sustain,float quiet,float target,float previous){
                const float tailWeight=musicDynamics&&!automaticRelease?.2f+.1f*sustain*juce::jlimit(0.f,1.f,(quiet-2.f)/8.f):.35f;
                const float result=(1-tailWeight)*fast+tailWeight*slow;
                // Moving toward the longer recovery tail must never create
                // fresh attenuation while the key remains below threshold.
                return musicDynamics&&target>=0.f?juce::jmax(previous,result):result;
            };
            gainDbL=combine(releaseFastL,releaseTailL,sustainL,quietTimeL,targetDbL,gainDbL);
            gainDbR=combine(releaseFastR,releaseTailR,sustainR,quietTimeR,targetDbR,gainDbR);
        }
        appliedGainDbL=smoothGain?gainSmoothing*appliedGainDbL+(1.0f-gainSmoothing)*gainDbL:gainDbL;
        appliedGainDbR=smoothGain?gainSmoothing*appliedGainDbR+(1.0f-gainSmoothing)*gainDbR:gainDbR;
        float gL=juce::Decibels::decibelsToGain(appliedGainDbL),gR=juce::Decibels::decibelsToGain(appliedGainDbR);
        float wetL=0,wetR=0;
        const float delayedL=dryL,delayedR=dryR;
        wetL=delayedL*gL;wetR=delayedR*gR;
        const double inputPower=.5*((double)dryL*dryL+(double)dryR*dryR);
        const double compressedPower=.5*((double)wetL*wetL+(double)wetR*wetR);
        if(!bypass)
        {
            autoInputPower=levelCoefficient*autoInputPower+(1.0-levelCoefficient)*inputPower;
            autoWetPower=levelCoefficient*autoWetPower+(1.0-levelCoefficient)*compressedPower;
            if(autoGainEnabled&&autoInputPower>silenceGatePower&&inputPower>1.0e-12&&autoWetPower>1.0e-12)
            {
                const bool neutral=std::abs(appliedGainDbL)<1.0e-6f&&std::abs(appliedGainDbR)<1.0e-6f;
                const float target=neutral?0.0f:juce::jlimit(0.0f,18.0f,(float)(10.0*std::log10(autoInputPower/autoWetPower)));
                autoMakeupDb+=juce::jlimit(neutral?-autoGainReturnStep:-autoGainStep,autoGainStep,target-autoMakeupDb);
            }
        }
        if(!autoGainEnabled)autoMakeupDb+=juce::jlimit(-autoGainReturnStep,autoGainReturnStep,-autoMakeupDb);
        const float makeup=juce::Decibels::decibelsToGain(autoMakeupDb);
        wetL*=makeup;wetR*=makeup;
        const float blend=bypassBlend.getNextValue();
        auto mixSignal=[&](float dry,float compressed,float raw)
        {const float processed=(mx<=0?dry:mx>=1?compressed:dry+(compressed-dry)*mx)*og;return blend>=1?raw:blend<=0?processed:processed+(raw-processed)*blend;};
        float outL=mixSignal(dryL,wetL,rawL),outR=mixSignal(dryR,wetR,rawR);
        // History describes the programme path, not the temporary monitor
        // substitution. SC already has its own filtered, aligned contour.
        const float programOutL=outL,programOutR=outR;
        const float listen=auditionMix.getNextValue();
        // Audition the signed filtered key, aligned to the program latency.
        // Keep stereo image intact: stereo linking affects detection envelopes,
        // and must not rectify or collapse the signal sent to the speakers.
        const float keyL=delayBuffer.getSample(2,read),keyR=delayBuffer.getSample(3,read);
        if(!bypass){outL+=listen*(keyL-outL);outR+=listen*(keyR-outR);}
        b.setSample(0,i,outL);b.setSample(1,i,outR);delayWrite=(delayWrite+1)%allocatedDelay;
        blockIn=juce::jmax(blockIn,std::abs(bypass?rawL:dryL),std::abs(bypass?rawR:dryR));blockOut=juce::jmax(blockOut,std::abs(outL),std::abs(outR));
        if(!bypass){blockGrL=juce::jmax(blockGrL,-appliedGainDbL);blockGrR=juce::jmax(blockGrR,-appliedGainDbR);blockGr=juce::jmax(blockGr,blockGrL,blockGrR);}
        if(captureHistory){
        historyInPeak=juce::jmax(historyInPeak,std::abs(bypass?rawL:dryL),std::abs(bypass?rawR:dryR));historyOutPeak=juce::jmax(historyOutPeak,std::abs(programOutL),std::abs(programOutR));historySidechainPeak=juce::jmax(historySidechainPeak,std::abs(delayBuffer.getSample(6,read)),std::abs(delayBuffer.getSample(7,read)));historySelectedSidechainPeak=juce::jmax(historySelectedSidechainPeak,std::abs(keyL),std::abs(keyR));historyDetectorPeak=juce::jmax(historyDetectorPeak,dL,dR);historyDetectorMinimum=juce::jmin(historyDetectorMinimum,juce::jmax(dL,dR));if(!bypass)historyGrPeak=juce::jmax(historyGrPeak,-appliedGainDbL,-appliedGainDbR);
        if(--historyCountdown<=0){
            const uint64_t sequence=historySequence.load()+1;
            auto& frame=coherentHistory[(size_t)((sequence-1)&(historyCapacity-1))];
            frame.sequence=0; frame.captureState=captureState;frame.input=historyInPeak;frame.output=historyOutPeak;frame.sidechain=historySelectedSidechainPeak;frame.detector=historyDetectorPeak;frame.reduction=historyGrPeak;frame.minimum=historyDetectorMinimum;frame.detectorRms=detectorRms;
            frame.sequence=sequence;historySequence=sequence;historyDetectorPeak=historySelectedSidechainPeak=0;historyDetectorMinimum=1000000;
            const int write=historyWrite.load(std::memory_order_relaxed),p=write&(historySize-1);inHistory[(size_t)p]=historyInPeak;outHistory[(size_t)p]=historyOutPeak;grHistory[(size_t)p]=historyGrPeak;sidechainHistory[(size_t)p]=historySidechainPeak;historyWrite.store((write+1)&(historySize-1),std::memory_order_release);historyInPeak=historyOutPeak=historyGrPeak=historySidechainPeak=0;historyCountdown=historyInterval;}
        }
    }
    const float meterDecay=std::exp(-(float)n/(float)(currentSampleRate*.15));
    inMeter=juce::jmax(blockIn,inMeter.load()*meterDecay);outMeter=juce::jmax(blockOut,outMeter.load()*meterDecay);grMeter=bypass?0.0f:juce::jmax(blockGr,grMeter.load()*meterDecay);autoMakeupMeter=autoMakeupDb;
    grChannelMeter[0]=bypass?0.0f:juce::jmax(blockGrL,grChannelMeter[0].load()*meterDecay);
    grChannelMeter[1]=bypass?0.0f:juce::jmax(blockGrR,grChannelMeter[1].load()*meterDecay);
    const double peakMarkerTime=juce::Time::getMillisecondCounterHiRes();
    reductionPeakMarkers[0].push(blockGrL,peakMarkerTime);reductionPeakMarkers[1].push(blockGrR,peakMarkerTime);
    auto previous=maxGrMeter.load();while(blockGr>previous&&!maxGrMeter.compare_exchange_weak(previous,blockGr)){}
    for(int channel=0;channel<2;++channel){const float peak=channel==0?blockGrL:blockGrR;auto& maximum=maxGrChannelMeter[(size_t)channel];auto held=maximum.load();while(peak>held&&!maximum.compare_exchange_weak(held,peak)) {}}
}

void OpenCompressorAudioProcessor::resetMeters() noexcept
{
    resetMeterLevels();
    for(auto&v:inHistory)v=0;for(auto&v:outHistory)v=0;for(auto&v:grHistory)v=0;for(auto&v:sidechainHistory)v=0;
}
void OpenCompressorAudioProcessor::resetMeterLevels() noexcept
{
    for(auto& marker:reductionPeakMarkers)marker.reset();
    inMeter=outMeter=grMeter=maxGrMeter=autoMakeupMeter=0;for(auto&v:grChannelMeter)v=0;for(auto&v:maxGrChannelMeter)v=0;
}

bool OpenCompressorAudioProcessor::readHistoryFrame(uint64_t sequence,HistoryFrame& result,uint64_t captureState) const noexcept {
    if(!sequence)return false;
    const auto& f=coherentHistory[(size_t)((sequence-1)&(historyCapacity-1))];
    if(f.sequence.load()!=sequence)return false;
    if(captureState!=0&&f.captureState.load(std::memory_order_relaxed)!=captureState){result={};return f.sequence.load()==sequence;}
    const HistoryFrame copy{f.input.load(),f.output.load(),f.sidechain.load(),f.detector.load(),f.reduction.load(),f.minimum.load(),f.detectorRms.load()};
    if(f.sequence.load()!=sequence)return false;result=copy;return true;
}
bool OpenCompressorAudioProcessor::readSpectrumFrame(SpectrumFrame& result) noexcept {
    int first,count,second,count2;spectrumFifo.prepareToRead(1,first,count,second,count2);
    if(count+count2==0)return false;result=spectrumFrames[(size_t)(count?first:second)];spectrumFifo.finishedRead(1);return true;
}
void OpenCompressorAudioProcessor::rememberFrozenHistory(const std::array<HistoryFrame,displayFrames>& frames,int valid) {
    const juce::ScopedLock lock(comparisonLock);frozenFrames=frames;frozenValidFrames=valid;
}
bool OpenCompressorAudioProcessor::restoreFrozenHistory(std::array<HistoryFrame,displayFrames>& frames,int& valid) const {
    const juce::ScopedLock lock(comparisonLock);frames=frozenFrames;valid=frozenValidFrames;return valid>0;
}
void OpenCompressorAudioProcessor::selectSnapshot(int slot) {
    const juce::ScopedLock lock(comparisonLock);slot=juce::jlimit(0,1,slot);if(slot==snapshotSlot)return;
    auditionSidechain=false;snapshots[(size_t)snapshotSlot.load()]=parameters.copyState();
    if(!snapshots[(size_t)slot].isValid())snapshots[(size_t)slot]=parameters.copyState();
    normalizeFilterState(snapshots[(size_t)slot]);recallingFilters=true;filterNormalizationRequested=false;
    undoManager.beginNewTransaction("Compare Compressor settings");snapshotSlot=slot;
    for(auto* raw:getParameters())if(auto* parameter=dynamic_cast<juce::RangedAudioParameter*>(raw)) {
        const auto id=parameter->paramID;
        if(id==P::bypass||id==P::smooth||id==P::maxLookahead||id==P::sidechainHistoryView)continue;
        const auto node=snapshots[(size_t)slot].getChildWithProperty("id",id);
        parameter->beginChangeGesture();parameter->setValueNotifyingHost(node.isValid()?parameter->convertTo0to1((float)node["value"]):parameter->getDefaultValue());parameter->endChangeGesture();
    }
    recallingFilters=false;lastEditedHighPass=true;normalizeSidechainFilters();parameters.copyState();undoManager.beginNewTransaction();
}
void OpenCompressorAudioProcessor::copySnapshot() { const juce::ScopedLock lock(comparisonLock);snapshots[(size_t)(1-snapshotSlot.load())]=parameters.copyState(); }
void OpenCompressorAudioProcessor::getStateInformation(juce::MemoryBlock& destination) {
    const juce::ScopedLock lock(comparisonLock);auto state=parameters.copyState();normalizeFilterState(state);
    state.setProperty("dynamicsVersion",dynamicsVersion.load(),nullptr);
    state.removeChild(state.getChildWithName("COMPARISON"),nullptr);state.removeChild(state.getChildWithName("EDITOR"),nullptr);
    snapshots[(size_t)snapshotSlot.load()]=state.createCopy();state.setProperty("activeSnapshot",snapshotSlot.load(),nullptr);
    juce::ValueTree comparison("COMPARISON");
    for(int slot=0;slot<2;++slot){auto snapshot=snapshots[(size_t)slot].isValid()?snapshots[(size_t)slot].createCopy():state.createCopy();normalizeFilterState(snapshot);comparison.addChild(snapshot,-1,nullptr);}
    state.addChild(comparison,-1,nullptr);
    juce::ValueTree editor("EDITOR");editor.setProperty("page",editorState.page.load(),nullptr);editor.setProperty("width",editorState.width.load(),nullptr);editor.setProperty("height",editorState.height.load(),nullptr);
    editor.setProperty("help",editorState.help.load(),nullptr);editor.setProperty("trace",editorState.trace.load(),nullptr);editor.setProperty("knee",editorState.knee.load(),nullptr);editor.setProperty("frozen",editorState.frozen.load(),nullptr);
    if(editorState.frozen&&frozenValidFrames>0) {
        editor.setProperty("validFrames",frozenValidFrames,nullptr);
        for(const auto& f:frozenFrames) { juce::ValueTree point("FRAME");point.setProperty("in",f.input,nullptr);point.setProperty("out",f.output,nullptr);point.setProperty("sc",f.sidechain,nullptr);point.setProperty("det",f.detector,nullptr);point.setProperty("gr",f.reduction,nullptr);point.setProperty("min",f.detectorMinimum,nullptr);point.setProperty("rms",f.detectorRms,nullptr);editor.addChild(point,-1,nullptr); }
    }
    state.addChild(editor,-1,nullptr);if(auto xml=state.createXml())copyXmlToBinary(*xml,destination);
}
void OpenCompressorAudioProcessor::setStateInformation(const void* data,int size) {
    const juce::ScopedLock lock(comparisonLock);auditionSidechain=false;
    restoringState=true;cancelPendingUpdate();filterNormalizationRequested=false;sidechainHistoryRequested=false;lastEditedHighPass=true;
    if(auto xml=getXmlFromBinary(data,size))if(xml->hasTagName(parameters.state.getType())) {
        auto state=juce::ValueTree::fromXml(*xml);const auto editor=state.getChildWithName("EDITOR");const auto comparison=state.getChildWithName("COMPARISON");
        // APVTS otherwise retains the live RMS selection when an old state
        // omits it. Migrate the main state AND both comparison slots.
        auto migrateDetector=[](juce::ValueTree tree){if(!tree.isValid())return;for(auto item:std::initializer_list<std::pair<const char*,float>>{{P::detectorMode,0},{P::rmsTime,20}})if(!tree.getChildWithProperty("id",item.first).isValid()){juce::ValueTree node("PARAM");node.setProperty("id",item.first,nullptr);node.setProperty("value",item.second,nullptr);tree.addChild(node,-1,nullptr);}};
        migrateDetector(state);for(int slot=0;slot<comparison.getNumChildren();++slot)migrateDetector(comparison.getChild(slot));
        normalizeFilterState(state);for(int slot=0;slot<comparison.getNumChildren();++slot)normalizeFilterState(comparison.getChild(slot));
        dynamicsVersion=juce::jlimit(1,3,(int)state.getProperty("dynamicsVersion",1));
        snapshotSlot=juce::jlimit(0,1,(int)state.getProperty("activeSnapshot",0));for(int slot=0;slot<2;++slot)snapshots[(size_t)slot]=comparison.getChild(slot).createCopy();
        editorState.page=juce::jlimit(0,2,(int)editor.getProperty("page",0));editorState.width=juce::jlimit(720,1200,(int)editor.getProperty("width",720));editorState.height=juce::jlimit(440,760,(int)editor.getProperty("height",440));
        editorState.help=(bool)editor.getProperty("help",false);editorState.trace=(bool)editor.getProperty("trace",false);editorState.knee=(bool)editor.getProperty("knee",false);editorState.frozen=(bool)editor.getProperty("frozen",false);
        frozenValidFrames=editor.getNumChildren()==displayFrames?juce::jlimit(0,displayFrames,(int)editor.getProperty("validFrames",0)):0;
        const bool oldFrozenRms=(float)state.getChildWithProperty("id",P::detectorMode)["value"]>.5f;
        if(frozenValidFrames>0)for(int i=0;i<displayFrames;++i) { const auto f=editor.getChild(i);frozenFrames[(size_t)i]={(float)f["in"],(float)f["out"],(float)f["sc"],(float)f["det"],(float)f["gr"],(float)f["min"],(bool)f.getProperty("rms",oldFrozenRms)}; }
        state.removeChild(editor,nullptr);state.removeChild(comparison,nullptr);state.removeProperty("activeSnapshot",nullptr);parameters.replaceState(state);undoManager.clearUndoHistory();++editorState.revision;
    }
    // Restoring Ext SC must not override a deliberately hidden overlay.
    // Older states containing both overlays resolve in favour of SHOW SC.
    if(parameters.getRawParameterValue(P::sidechainHistoryView)->load()>.5f)editorState.trace=false;
    restoringState=false;updateLookaheadLatency();
}
juce::AudioProcessorEditor* OpenCompressorAudioProcessor::createEditor(){return new OpenCompressorAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new OpenCompressorAudioProcessor();}
