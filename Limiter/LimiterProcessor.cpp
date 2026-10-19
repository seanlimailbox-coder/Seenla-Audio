#include "LimiterProcessor.h"
#include "LimiterEditor.h"
#include <cmath>

namespace LP
{
constexpr auto drive="drive", ceiling="ceiling", release="release", link="link", mode="mode", bypass="bypass";
static void raisePeak(std::atomic<float>& peak, float value) noexcept
{
    float old=peak.load();
    while(value>old&&!peak.compare_exchange_weak(old,value)) {}
}
}

// Keep existing normalized automation landmarks: Off=0, 4x=.5, 8x=1.
// The new 2x choice occupies .25. Saved raw indices are migrated separately.
class LimiterOversamplingChoice final:public juce::AudioParameterChoice
{
public:
    LimiterOversamplingChoice(const char* id,const char* name,const juce::StringArray& choices)
        :juce::AudioParameterChoice(juce::ParameterID{id,1},name,choices,0),mapping(0.f,3.f,
            [](float,float,float n){return n<=.5f?n*4.f:2.f+(n-.5f)*2.f;},
            [](float,float,float index){return index<=2.f?index*.25f:.5f+(index-2.f)*.5f;},
            [](float,float,float index){return float(juce::roundToInt(juce::jlimit(0.f,3.f,index)));}){mapping.interval=1.f;}
    const juce::NormalisableRange<float>& getNormalisableRange()const override{return mapping;}
private:
    juce::NormalisableRange<float> mapping;
};

OpenLimiterAudioProcessor::OpenLimiterAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                                      .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      parameters(*this,&undoManager,"OPEN_LIMITER",createParameterLayout())
{
    for(auto* id:{"mode","style","styleSelect","link","truePeak","truePeakMeter","outputQuality","renderQuality","zeroLatency"})parameters.addParameterListener(id,this);
    clearStatistics();saveSnapshot(0);saveSnapshot(1);presetBaseline=presetValues();
}
OpenLimiterAudioProcessor::~OpenLimiterAudioProcessor(){for(auto* id:{"mode","style","styleSelect","link","truePeak","truePeakMeter","outputQuality","renderQuality","zeroLatency"})parameters.removeParameterListener(id,this);}
juce::AudioProcessorValueTreeState::ParameterLayout OpenLimiterAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    using F=juce::AudioParameterFloat;
    auto add=[&](const char* id,const char* name,float min,float max,float step,float def,const char* unit,bool meta=false)
    { l.add(std::make_unique<F>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(min,max,step),def,juce::AudioParameterFloatAttributes().withLabel(unit).withMeta(meta))); };
    add(LP::drive,"Gain",0,24,.01f,0,"dB");
    add(LP::ceiling,"Output Ceiling",-18,0,.01f,0,"dBFS");
    add(LP::release,"Release",20,2000,.1f,400,"ms");
    // These controls update other parameters; advertise their existing behavior
    // to AU hosts so parameter save/restore validation treats them as meta controls.
    add(LP::link,"Stereo Link",0,100,.1f,100,"%",true);
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{LP::mode,1},"Legacy Style",juce::StringArray{"Punch","Dynamic","Aggressive","Transparent"},3,juce::AudioParameterChoiceAttributes().withMeta(true)));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{LP::bypass,1},"Bypass",false));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"loudnessMode",1},"Loudness View",
          juce::StringArray{"Integrated","Short term","Momentary"},0,juce::AudioParameterChoiceAttributes().withAutomatable(false)));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"truePeak",1},"True Peak Protection",true,juce::AudioParameterBoolAttributes().withMeta(true)));
    l.add(std::make_unique<LimiterOversamplingChoice>("outputQuality","Output Oversampling",juce::StringArray{"Off","2x","4x","8x"}));
    l.add(std::make_unique<LimiterOversamplingChoice>("renderQuality","Render Oversampling",juce::StringArray{"Playback","At least 2x","At least 4x","8x"}));
    auto attackRange=juce::NormalisableRange<float>(0,10000,.001f);attackRange.setSkewForCentre(275);
    l.add(std::make_unique<F>(juce::ParameterID{"attack",2},"Attack",attackRange,275.f,"ms"));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"style",1},"Legacy Eight Styles",juce::StringArray{"Punch","Dynamic","Aggressive","Transparent","Safe","Allround","Modern","Bus"},6,juce::AudioParameterChoiceAttributes().withMeta(true)));
    auto aheadRange=juce::NormalisableRange<float>(0,5,.001f);aheadRange.setSkewForCentre(.5f);
    l.add(std::make_unique<F>(juce::ParameterID{"lookahead",1},"Lookahead",aheadRange,.180f,"ms"));
    add("transientLink","Transient Link",0,100,.1f,75,"%");
    add("releaseLink","Release Link",0,100,.1f,100,"%");
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"styleSelect",1},"Style",LimiterStyles::names(),6,juce::AudioParameterChoiceAttributes().withValueFromStringFunction([](const juce::String& name){return juce::jmax(0,LimiterStyles::index(name));})));
    // Append so existing host parameter indices and True Peak automation stay valid.
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"truePeakMeter",1},"True Peak Metering",true));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"zeroLatency",1},"Zero Instant",false,juce::AudioParameterBoolAttributes().withMeta(true)));
    return l;
}
void OpenLimiterAudioProcessor::parameterChanged(const juce::String& id,float value)
{
    if(id=="truePeak"||id=="truePeakMeter"){
        // Clear even while the host is suspended or the editor is closed.
        // Repeat at the next block so an in-flight old-mode block cannot
        // leave its maxima behind after the switch.
        resetOutputPeakHolds();outputPeakResetRequested=true;
    }
    if(!restoringState&&id=="truePeak"&&value>.5f&&parameters.getRawParameterValue("truePeakMeter")->load()<.5f){
        parameters.getParameter("truePeakMeter")->setValueNotifyingHost(1.f);
    }
    // The meter never requests a different audio engine or latency.
    if(id=="truePeakMeter")return;
    // Preserve normalized 6.x host automation: the legacy four-choice range
    // is unchanged. The appended nine-choice "styleSelect" parameter drives new sessions.
    if(id=="mode"&&!restoringState){auto* style=parameters.getParameter("style");style->setValueNotifyingHost(style->convertTo0to1(value));}
    if(id=="style"&&!restoringState){auto* selected=parameters.getParameter("styleSelect");selected->setValueNotifyingHost(selected->convertTo0to1(value));}
    // Old Stereo Link automation still controls both stages. New parameters
    // remain independent, and state restore must not overwrite either one.
    if(id=="link"&&!restoringState)for(auto* target:{"transientLink","releaseLink"}){
        auto* parameter=parameters.getParameter(target);parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
    if(!restoringState){
        const open_limiter::Controls c{parameters.getRawParameterValue("drive")->load(),parameters.getRawParameterValue("attack")->load(),
            parameters.getRawParameterValue("release")->load(),parameters.getRawParameterValue("ceiling")->load(),parameters.getRawParameterValue("transientLink")->load()*.01f,parameters.getRawParameterValue("releaseLink")->load()*.01f,parameters.getRawParameterValue("lookahead")->load(),parameters.getRawParameterValue("zeroLatency")->load()>.5f};
        limiter.request(juce::roundToInt(parameters.getRawParameterValue("styleSelect")->load()),outputQuality(),parameters.getRawParameterValue("truePeak")->load()>.5f,c);
    }
}
void OpenLimiterAudioProcessor::updateLatency()
{
    // Only the installed path determines host delay. A requested replacement
    // is built off the audio thread and warmed before it takes over.
    if(delaySize>0)setLatencySamples(limiter.latency());
}
void OpenLimiterAudioProcessor::prepareToPlay(double sr,int maxBlock)
{
    const bool retainHistory=historyPrepared&&juce::exactlyEqual(sampleRate,sr);
    maximumBlock=juce::jmax(1,maxBlock);
    sampleRate=sr;
    activeQuality=outputQuality();activeTruePeak=parameters.getRawParameterValue("truePeak")->load()>.5f;
    open_limiter::Controls controls{parameters.getRawParameterValue("drive")->load(),parameters.getRawParameterValue("attack")->load(),
        parameters.getRawParameterValue("release")->load(),parameters.getRawParameterValue("ceiling")->load(),parameters.getRawParameterValue("transientLink")->load()*.01f,parameters.getRawParameterValue("releaseLink")->load()*.01f,parameters.getRawParameterValue("lookahead")->load(),parameters.getRawParameterValue("zeroLatency")->load()>.5f};
    limiter.prepare(sr,juce::roundToInt(parameters.getRawParameterValue("styleSelect")->load()),activeQuality,activeTruePeak,controls);
    for(int phase=0;phase<7;++phase)
    {
        double sum=0;
        for(int tap=0;tap<64;++tap)
        {
            const double x=tap-31.0-(phase+1)*.125;
            const double window=.42-.5*std::cos(juce::MathConstants<double>::twoPi*tap/63.0)
                +.08*std::cos(2*juce::MathConstants<double>::twoPi*tap/63.0);
            const double weight=std::sin(juce::MathConstants<double>::pi*x)/(juce::MathConstants<double>::pi*x)*window;
            peakKernels[(size_t)phase][(size_t)tap]=weight;sum+=weight;
        }
        for(auto& weight:peakKernels[(size_t)phase])weight/=sum;
    }
    bypassBlend.reset(sr,.005);deltaBlend.reset(sr,.005);compareBlend.reset(sr,.05);
    bypassBlend.setCurrentAndTargetValue(parameters.getRawParameterValue("bypass")->load()>.5f?1.f:0.f);
    deltaBlend.setCurrentAndTargetValue(monitoring()==1?1.f:0.f);compareBlend.setCurrentAndTargetValue(monitoring()==2?comparisonGain:1.f);
    if(!retainHistory){compareSamples=0;compareRawEnergy=compareWetEnergy=0;compareProgress=0;compareSettle=juce::roundToInt(sr*.2);}
    delaySize=1;sampleIndex=0;updateLatency();safetyCorrections=0;

    // BS.1770 K-weighting shelf and RLB high-pass, recalculated for the host rate.
    // Filter constants: https://github.com/jiixyj/libebur128 (ebur128_init_filter).
    const double k=std::tan(juce::MathConstants<double>::pi*1681.974450955533/sr);
    const double v=std::pow(10.0,3.999843853973347/20.0),mid=std::pow(v,.4996667741545416);
    const double q=.7071752369554196,den=1.0+k/q+k*k;
    const double hp=std::tan(juce::MathConstants<double>::pi*38.13547087602444/sr);
    const double hpDen=1.0+hp/.5003270373238773+hp*hp;
    if(!retainHistory)for(int c=0;c<2;++c)
    {
        shelf[c]={(v+mid*k/q+k*k)/den,2.0*(k*k-v)/den,(v-mid*k/q+k*k)/den,
                  2.0*(k*k-1.0)/den,(1.0-k/q+k*k)/den,0,0};
        highPass[c]={1,-2,1,2.0*(hp*hp-1.0)/hpDen,(1.0-hp/.5003270373238773+hp*hp)/hpDen,0,0};
    }
    historyInterval=juce::jmax(1,juce::roundToInt(sr/60.0));
    loudnessInterval=juce::jmax(1,juce::roundToInt(sr*.1));
    if(!retainHistory)liveEnergy.assign((size_t)juce::jmax(1,juce::roundToInt(sr*3.)),0.);
    liveMomentaryLength=juce::jmax(1,juce::roundToInt(sr*.4));liveInterval=juce::jmax(1,juce::roundToInt(sr*.02));
    for(auto& ring:rmsRing)ring.assign((size_t)juce::jmax(1,juce::roundToInt(sampleRate*.4)),0.);rmsSum={};rmsIndex=rmsCount=0;for(auto& meter:rmsMeter)meter=0;
    clearStatistics(!retainHistory,!retainHistory);historyPrepared=true;resetRequested=false;loudnessResetRequested=false;
}
bool OpenLimiterAudioProcessor::isBusesLayoutSupported(const BusesLayout& b) const
{return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&b.getMainOutputChannelSet()==b.getMainInputChannelSet();}
void OpenLimiterAudioProcessor::reset()
{
    sampleIndex=0;limiter.reset();safetyCorrections=0;meterPeakHistory.clear();meterSilentSamples=64;
    bypassBlend.setCurrentAndTargetValue(parameters.getRawParameterValue("bypass")->load()>.5f?1.f:0.f);
    deltaBlend.setCurrentAndTargetValue(monitoring()==1?1.f:0.f);compareBlend.setCurrentAndTargetValue(monitoring()==2?comparisonGain:1.f);
    clearStatistics(false,false);resetRequested=false;loudnessResetRequested=false;
}
float OpenLimiterAudioProcessor::energyToLufs(double e) noexcept
{return e>1.e-12?(float)(-.691+10.0*std::log10(e)):-100.f;}
float OpenLimiterAudioProcessor::loudness(int selection) const noexcept
{return (selection==0?integrated:selection==1?shortTerm:momentary).load();}
void OpenLimiterAudioProcessor::updateLoudness(double l,double r) noexcept
{
    const double a=highPass[0].process(shelf[0].process(l));
    const double b=highPass[1].process(shelf[1].process(r));
    const double sampleEnergy=a*a+b*b;hopEnergy+=sampleEnergy;
    const int length=(int)liveEnergy.size();
    liveMomentarySum+=sampleEnergy-liveEnergy[(size_t)((liveIndex+length-liveMomentaryLength)%length)];
    liveShortTermSum+=sampleEnergy-liveEnergy[(size_t)liveIndex];liveEnergy[(size_t)liveIndex]=sampleEnergy;
    liveIndex=(liveIndex+1)%length;liveCount=juce::jmin(length,liveCount+1);
    if(--liveCountdown<=0)
    {
        liveCountdown=liveInterval;
        // Refresh only from a newly measured window, never from a stale bar
        // during host bypass or measurement pause. No work on the GUI thread.
        const double peakTime=juce::Time::getMillisecondCounterHiRes();
        if(liveCount>=liveMomentaryLength){liveMomentary=energyToLufs(juce::jmax(0.,liveMomentarySum)/liveMomentaryLength);heldLoudness[1].push(liveMomentary.load(),peakTime);}
        if(liveCount>=length){liveShortTerm=energyToLufs(juce::jmax(0.,liveShortTermSum)/length);heldLoudness[0].push(liveShortTerm.load(),peakTime);}
    }
    if(--loudnessCountdown>0)return;
    loudnessRing[(size_t)loudnessIndex]=hopEnergy/loudnessInterval;
    hopEnergy=0;loudnessCountdown=loudnessInterval;
    loudnessIndex=(loudnessIndex+1)%30;++loudnessHops;
    double m=0,s=0;
    for(int i=0;i<30;++i)
    {
        const double value=loudnessRing[(size_t)((loudnessIndex+29-i)%30)];
        if(i<4)m+=value;s+=value;
    }
    if(loudnessHops>=4){const float value=energyToLufs(m/4.0);momentary=value;maxMomentary=juce::jmax(maxMomentary.load(),value);}
    if(loudnessHops>=30)
    {
        const float value=energyToLufs(s/30.0);shortTerm=value;maxShortTerm=juce::jmax(maxShortTerm.load(),value);
        // EBU Tech 3342: 3 s windows at 1 s intervals, -70 absolute gate,
        // -20 LU relative gate and the rounded 10th/95th percentiles.
        if((loudnessHops-30)%10==0)
        {
            if(value>=-70.f){const int bin=juce::jlimit(0,loudnessBins-1,(int)((value+70)*100));rangeHistogram.add(bin,s/30.);}
            const auto total=rangeHistogram.from(0);
            if(total.count>0)
            {
                const float gate=juce::jmax(-70.f,energyToLufs(total.energy/double(total.count))-20.f);
                const int first=juce::jlimit(0,loudnessBins-1,(int)std::ceil((gate+70)*100-.5f));
                const auto gated=rangeHistogram.from(first);
                if(gated.count>0){const auto low=(uint64_t)std::floor((double(gated.count)-1)*.1+.5),high=(uint64_t)std::floor((double(gated.count)-1)*.95+.5);
                    rangeLow=-70.f+(rangeHistogram.descendingRank(gated.count-1-low)+.5f)*.01f;
                    rangeHigh=-70.f+(rangeHistogram.descendingRank(gated.count-1-high)+.5f)*.01f;}
            }
        }
    }
    // Integrated loudness uses 400 ms blocks at a 100 ms hop (75% overlap),
    // an absolute -70 LUFS gate, then a relative gate 10 LU below the first mean.
    if(loudnessHops>=4)
    {
        const double energy=m/4.0;const float level=energyToLufs(energy);
        if(level>=-70.f)
        {
            const int bin=juce::jlimit(0,loudnessBins-1,(int)((level+70.f)*100.f));
            gatedHistogram.add(bin,energy);
        }
        auto total=gatedHistogram.from(0);
        if(total.count>0)
        {
            const float gate=juce::jmax(-70.f,energyToLufs(total.energy/double(total.count))-10.f);
            const int first=juce::jlimit(0,loudnessBins-1,(int)((gate+70.f)*100.f));
            total=gatedHistogram.from(first);
        }
        integrated=total.count>0?energyToLufs(total.energy/double(total.count)):-100.f;
    }
}
void OpenLimiterAudioProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi)
{ process(b,midi,false); }
void OpenLimiterAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi)
{ process(b,midi,true); }
int OpenLimiterAudioProcessor::outputQuality() const noexcept
{
    if(zeroLatencyRequested())return 0;
    int quality=(int)parameters.getRawParameterValue("outputQuality")->load();
    if(isNonRealtime()) quality=juce::jmax(quality,(int)parameters.getRawParameterValue("renderQuality")->load());
    return quality;
}
void OpenLimiterAudioProcessor::process(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi,bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;juce::ignoreUnused(midi);
    if(b.getNumChannels()<2||b.getNumSamples()==0||delaySize==0)return;
    if(resetRequested.exchange(false))clearStatistics();
    if(outputPeakResetRequested.exchange(false))resetOutputPeakHolds();
    if(loudnessResetRequested.exchange(false))clearLoudness();
    if(auto* playhead=getPlayHead()) if(auto position=playhead->getPosition())
    {
        const bool playing=position->getIsPlaying();
        if(playing&&!wasPlaying&&autoReset.load())clearLoudness();
        wasPlaying=playing;
    }
    lastCallbackSize=b.getNumSamples();
    const bool bypass=hostBypass||parameters.getRawParameterValue("bypass")->load()>.5f;
    // A host can start directly in bypass. Initialize before the first delayed
    // sample arrives; subsequent live changes retain the normal 5 ms crossfade.
    if(sampleIndex==0)bypassBlend.setCurrentAndTargetValue(bypass?1.f:0.f);
    if(bypass&&!hostBypass)stopMonitoring();
    for(int base=0;base<b.getNumSamples();base+=maximumBlock)
    {
        const int count=juce::jmin(maximumBlock,b.getNumSamples()-base);
        float* channels[]{b.getWritePointer(0,base),b.getWritePointer(1,base)};
        juce::AudioBuffer<float> chunk(channels,2,count);processChunk(chunk,bypass,hostBypass);
    }
}
void OpenLimiterAudioProcessor::processChunk(juce::AudioBuffer<float>& b,bool bypass,bool hostBypass)
{
    auto get=[&](const char* id){return parameters.getRawParameterValue(id)->load();};
    const float ceiling=juce::Decibels::decibelsToGain(get("ceiling"));
    const open_limiter::Controls controls{get("drive"),get("attack"),get("release"),get("ceiling"),get("transientLink")*.01f,get("releaseLink")*.01f,get("lookahead"),get("zeroLatency")>.5f};
    const int requestedStyle=juce::roundToInt(get("styleSelect")),quality=outputQuality();
    const bool tp=get("truePeak")>.5f;
    const bool tpMeter=get("truePeakMeter")>.5f;
    limiter.request(requestedStyle,quality,tp,controls);
    const float meterDecay=(float)std::exp(-1.0/(sampleRate*.35));
    const float grDecay=(float)std::exp(-1.0/(sampleRate*.15));
    bypassBlend.setTargetValue(bypass?1.f:0.f);
    const int monitor=isNonRealtime()||bypass?0:monitorMode.load();
    deltaBlend.setTargetValue(monitor==1?1.f:0.f);
    const std::array<float,7> settings{get("drive"),get("ceiling"),get("attack"),get("release"),get("lookahead"),get("transientLink"),get("releaseLink")};
    if(settings!=comparedSettings)
    {
        comparedSettings=settings;compareSamples=0;compareRawEnergy=compareWetEnergy=0;
        compareProgress=0;compareSettle=getLatencySamples()+juce::roundToInt(sampleRate*.1);
    }
    compareBlend.setTargetValue(monitor==2&&compareProgress.load()>=.999f?comparisonGain:1.f);
    const int count=b.getNumSamples();
    float blockRaw[2]{},blockInput[2]{},blockOutput[2]{},blockGR[2]{},blockReduction=0;
    for(int i=0;i<count;++i,++sampleIndex)
    {
        float left=b.getSample(0,i),right=b.getSample(1,i);
        if(!std::isfinite(left))left=0;if(!std::isfinite(right))right=0;
        left=juce::jlimit(-1.e6f,1.e6f,left);right=juce::jlimit(-1.e6f,1.e6f,right);
        const auto frame=limiter.process(left,right,controls);
        float metadata[6]{frame.raw[0],frame.raw[1],frame.driven[0],frame.driven[1],frame.reduction[0],frame.reduction[1]};
        float normal[2]{frame.wet[0],frame.wet[1]};
        // A lowering ceiling takes effect immediately, including during a
        // style crossfade or while a prepared path is waiting for activation.
        float peak=juce::jmax(std::abs(normal[0]),std::abs(normal[1]));
        if(peak>ceiling){float bound=ceiling/peak;for(int c=0;c<2;++c){normal[c]*=bound;metadata[c+4]-=juce::Decibels::gainToDecibels(bound,-120.f);}}
        if(getLatencySamples()!=limiter.latency()){
            updateLatency();compareSamples=0;compareRawEnergy=compareWetEnergy=0;compareProgress=0;compareSettle=getLatencySamples();
        }
        const float dryBlend=bypassBlend.getNextValue();
        float measured[2]{};
        for(int c=0;c<2;++c)
        {
            // Endpoint branches preserve exact inactive/bypass samples.
            measured[c]=dryBlend>=1.f?metadata[c]:dryBlend<=0.f?normal[c]:normal[c]+dryBlend*(metadata[c]-normal[c]);
            const float in=std::abs(metadata[c+2]),out=std::abs(measured[c]);
            const float reduction=metadata[c+4]*(1-dryBlend);
            rawBallistics[(size_t)c]=juce::jmax(std::abs(metadata[c]),rawBallistics[(size_t)c]*meterDecay);
            blockRaw[c]=juce::jmax(blockRaw[c],std::abs(metadata[c]));
            inBallistics[(size_t)c]=juce::jmax(in,inBallistics[(size_t)c]*meterDecay);
            outBallistics[(size_t)c]=juce::jmax(out,outBallistics[(size_t)c]*meterDecay);
            grBallistics[(size_t)c]=juce::jmax(reduction,grBallistics[(size_t)c]*grDecay);
            blockInput[c]=juce::jmax(blockInput[c],in);blockOutput[c]=juce::jmax(blockOutput[c],out);
            blockReduction=juce::jmax(blockReduction,reduction);blockGR[c]=juce::jmax(blockGR[c],reduction);
        }
        // Independent 400 ms rectangular RMS windows continue during LUFS pause.
        if(!rmsRing[0].empty())
        {
            for(int c=0;c<2;++c){const double energy=(double)measured[c]*measured[c];auto& ring=rmsRing[(size_t)c];rmsSum[(size_t)c]+=energy-ring[(size_t)rmsIndex];ring[(size_t)rmsIndex]=energy;}
            rmsIndex=(rmsIndex+1)%(int)rmsRing[0].size();rmsCount=juce::jmin(rmsCount+1,(int)rmsRing[0].size());
        }
        // Measurement always follows the normal output, before temporary listening.
        if(!hostBypass&&!loudnessPaused.load()) {updateLoudness(measured[0],measured[1]);++measuredSamples;}
        meterPeakHistory.push(measured[0],measured[1]);
        // The 4x metering kernel is independent of the limiting detectors.
        // Metering has its own switch and never controls protection or latency.
        // Continue recording input history so enabling it has a valid FIR window.
        meterSilentSamples=(measured[0]==0.f&&measured[1]==0.f)?juce::jmin(64,meterSilentSamples+1):0;
        if(tpMeter&&meterSilentSamples<64)LP::raisePeak(heldTruePeak,juce::jmax(std::abs(measured[0]),std::abs(measured[1]),meterPeakHistory.peak<3,2,1>(peakKernels)));
        const double rawEnergy=(double)metadata[0]*metadata[0]+(double)metadata[1]*metadata[1];
        const double wetEnergy=(double)normal[0]*normal[0]+(double)normal[1]*normal[1];
        if(compareSettle>0)--compareSettle;
        else if(!bypass&&rawEnergy>2.e-6&&compareSamples<(uint64_t)(sampleRate*3))
        {
            compareRawEnergy+=rawEnergy;compareWetEnergy+=wetEnergy;++compareSamples;
            compareProgress=(float)(compareSamples/(sampleRate*3));
            if(compareSamples>=(uint64_t)(sampleRate*3))
            {
                comparisonGain=(float)std::sqrt(juce::jlimit(.001,1.0,compareRawEnergy/juce::jmax(1.e-12,compareWetEnergy)));
                comparisonGainDb=juce::Decibels::gainToDecibels(comparisonGain);
            }
        }
        const float deltaAmount=deltaBlend.getNextValue();
        const float listenGain=compareBlend.getNextValue();
        for(int c=0;c<2;++c)
        {
            const float output=measured[c]*listenGain;
            const float removed=metadata[c+2]-normal[c];
            b.setSample(c,i,isNonRealtime()?measured[c]:output+deltaAmount*(removed-output));
        }
        if(hostBypass)publishHistory(0,0,0,0);else publishHistory(juce::jmax(std::abs(metadata[0]),std::abs(metadata[1])),
            juce::jmax(std::abs(metadata[2]),std::abs(metadata[3])),
            juce::jmax(std::abs(measured[0]),std::abs(measured[1])),
            juce::jmax(metadata[4],metadata[5])*(1-dryBlend));
    }
    const double peakTime=juce::Time::getMillisecondCounterHiRes();
    for(int c=0;c<2;++c)
    {
        rmsMeter[(size_t)c]=(float)std::sqrt(juce::jmax(0.,rmsSum[(size_t)c])/juce::jmax(1,rmsCount));
        rawInputMeter[(size_t)c]=rawBallistics[(size_t)c];LP::raisePeak(heldRawInput[(size_t)c],blockRaw[c]);
        inputMeter[(size_t)c]=inBallistics[(size_t)c];outputMeter[(size_t)c]=outBallistics[(size_t)c];grMeter[(size_t)c]=grBallistics[(size_t)c];
        LP::raisePeak(heldInput[(size_t)c],blockInput[c]);LP::raisePeak(heldOutput[(size_t)c],blockOutput[c]);heldReduction[(size_t)c].push(blockGR[c],peakTime);
        heldOutputMarker[(size_t)c].push(juce::Decibels::gainToDecibels(blockOutput[c],-100.f),peakTime);
    }
    LP::raisePeak(maxGr,blockReduction);safetyCorrections=limiter.corrections();
}
void OpenLimiterAudioProcessor::publishHistory(float raw,float driven,float out,float reduction) noexcept
{
    historyRawPeak=juce::jmax(historyRawPeak,raw);historyInPeak=juce::jmax(historyInPeak,driven);
    historyOutPeak=juce::jmax(historyOutPeak,out);historyGrPeak=juce::jmax(historyGrPeak,reduction);
    if(--historyCountdown>0)return;
    const auto sequence=publishedSequence.load()+1;const auto index=(size_t)((sequence-1)&(historySize-1));
    auto& frame=coherentHistory[index];frame.stamp.store(0,std::memory_order_seq_cst);
    frame.raw=historyRawPeak;frame.driven=historyInPeak;frame.output=historyOutPeak;frame.reduction=historyGrPeak;
    for(int i=0;i<3;++i)frame.loudness[(size_t)i]=liveLoudness(i);
    frame.epoch=captureEpoch.load();frame.stamp.store(sequence,std::memory_order_seq_cst);
    inputHistory[index]=historyRawPeak;outputHistory[index]=historyOutPeak;grHistory[index]=historyGrPeak;
    historyWrite=(int)(sequence&(historySize-1));publishedSequence=sequence;++historyGeneration;
    historyRawPeak=historyInPeak=historyOutPeak=historyGrPeak=0;historyCountdown=historyInterval;
}
bool OpenLimiterAudioProcessor::readHistoryFrame(uint64_t sequence,HistoryFrame& result) const noexcept
{
    if(sequence==0)return false;
    const auto& f=coherentHistory[(size_t)((sequence-1)&(historySize-1))];
    const auto epoch=captureEpoch.load();if(f.stamp.load()!=sequence||f.epoch.load()!=epoch)return false;
    result={f.raw.load(),f.driven.load(),f.output.load(),f.reduction.load()};for(int i=0;i<3;++i)result.loudness[(size_t)i]=f.loudness[(size_t)i].load();
    return f.stamp.load()==sequence&&f.epoch.load()==epoch&&captureEpoch.load()==epoch;
}
void OpenLimiterAudioProcessor::resetPeakHolds() noexcept
{
    resetOutputPeakHolds();for(auto& v:heldRawInput)v=0;for(auto& v:heldInput)v=0;
}
void OpenLimiterAudioProcessor::resetOutputPeakHolds() noexcept
{
    maxGr=0;heldTruePeak=0;for(auto& v:heldOutput)v=0;for(auto& v:heldReduction)v.reset();for(auto& v:heldOutputMarker)v.reset();
}
void OpenLimiterAudioProcessor::clearLoudness() noexcept
{
    for(auto& hold:heldLoudness)hold.reset();
    measuredSamples=0;
    loudnessRing.fill(0);gatedHistogram.clear();rangeHistogram.clear();maxMomentary=maxShortTerm=rangeLow=rangeHigh=-100.f;
    momentary=shortTerm=integrated=-100.f;
    loudnessHops=0;loudnessIndex=0;hopEnergy=0;loudnessCountdown=loudnessInterval;
    std::fill(liveEnergy.begin(),liveEnergy.end(),0.);liveMomentarySum=liveShortTermSum=0;
    liveIndex=liveCount=0;liveCountdown=liveInterval;liveMomentary=liveShortTerm=-100.f;
    for(auto& filter:shelf)filter.reset();for(auto& filter:highPass)filter.reset();
}
void OpenLimiterAudioProcessor::clearStatistics(bool clearHistory,bool clearMeasurement) noexcept
{
    for(auto& ring:rmsRing)std::fill(ring.begin(),ring.end(),0.);rmsSum={};rmsIndex=rmsCount=0;for(auto& meter:rmsMeter)meter=0;
    rawBallistics.fill(0);for(auto& v:rawInputMeter)v=0;
    inBallistics.fill(0);outBallistics.fill(0);grBallistics.fill(0);
    for(auto& v:inputMeter)v=0;for(auto& v:outputMeter)v=0;for(auto& v:grMeter)v=0;
    if(clearHistory)
    {
    for(auto& v:inputHistory)v=0;for(auto& v:outputHistory)v=0;for(auto& v:grHistory)v=0;
    historyWrite=0;
    historyRawPeak=historyInPeak=historyOutPeak=historyGrPeak=0;historyCountdown=historyInterval;
    publishedSequence=0;++captureEpoch;++historyGeneration;
    }
    if(clearMeasurement){resetPeakHolds();clearLoudness();}
}
void OpenLimiterAudioProcessor::saveSnapshot(int slot)
{
    juce::ValueTree state("SOUND");
    for(auto* id:{"drive","ceiling","attack","release","link","lookahead","transientLink","releaseLink"})state.setProperty(id,parameters.getRawParameterValue(id)->load(),nullptr);
    snapshots[(size_t)slot]=state;
}
void OpenLimiterAudioProcessor::selectSnapshot(int slot)
{
    slot=juce::jlimit(0,1,slot);if(slot==snapshotSlot)return;
    saveSnapshot(snapshotSlot);stopMonitoring();parameters.copyState();undoManager.beginNewTransaction("Recall comparison");
    for(auto* id:{"drive","ceiling","attack","release","lookahead","transientLink","releaseLink"})
    {
        auto* parameter=parameters.getParameter(id);
        parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->convertTo0to1((float)snapshots[(size_t)slot].getProperty(id,parameters.getRawParameterValue(id)->load())));parameter->endChangeGesture();
    }
    parameters.copyState();snapshotSlot=slot;editorStateChanged();
}
void OpenLimiterAudioProcessor::copySnapshot()
{saveSnapshot(snapshotSlot);snapshots[(size_t)(1-snapshotSlot)]=snapshots[(size_t)snapshotSlot].createCopy();editorStateChanged();}
void OpenLimiterAudioProcessor::getStateInformation(juce::MemoryBlock& d)
{
    auto state=parameters.copyState();saveSnapshot(snapshotSlot);
    state.setProperty("styleSchema",3,nullptr);
    state.setProperty("oversamplingSchema",1,nullptr);
    {const juce::ScopedLock lock(presetLock);
    state.setProperty("presetName",presetName,nullptr);state.setProperty("presetKey",presetKey,nullptr);state.setProperty("presetNotes",presetNotes,nullptr);
    state.removeChild(state.getChildWithName("PRESET_BASE"),nullptr);auto baseline=LimiterPresets::soundValues(presetBaseline);baseline.setProperty("baseline",true,nullptr);
    juce::ValueTree presetState("PRESET_BASE");presetState.addChild(baseline,-1,nullptr);state.addChild(presetState,-1,nullptr);}
    juce::ValueTree view("EDITOR_VIEW");
    view.setProperty("width",editorState.width.load(),nullptr);view.setProperty("height",editorState.height.load(),nullptr);
    view.setProperty("help",editorState.help.load(),nullptr);view.setProperty("details",true,nullptr);
    view.setProperty("advancedControls",editorState.advancedControls.load(),nullptr);
    view.setProperty("historyView",editorState.historyView.load(),nullptr);view.setProperty("historyMotionSchema",1,nullptr);view.setProperty("loudnessTrace",editorState.loudnessTrace.load(),nullptr);
    view.setProperty("target",targetLufs(),nullptr);view.setProperty("autoReset",autoReset.load(),nullptr);
    view.setProperty("slot",snapshotSlot,nullptr);state.removeChild(state.getChildWithName("EDITOR_VIEW"),nullptr);state.addChild(view,-1,nullptr);
    for(int i=0;i<2;++i) {auto sound=snapshots[(size_t)i].createCopy();sound.setProperty("slot",i,nullptr);state.addChild(sound,-1,nullptr);}
    if(auto xml=state.createXml())copyXmlToBinary(*xml,d);
}
void OpenLimiterAudioProcessor::setStateInformation(const void* data,int size)
{
    auto xml=getXmlFromBinary(data,size);if(!xml||!xml->hasTagName(parameters.state.getType()))return;
    auto state=juce::ValueTree::fromXml(*xml);auto view=state.getChildWithName("EDITOR_VIEW");
    if(!state.hasProperty("styleSchema"))
    {
        auto style=state.getChildWithProperty("id","mode");
        if(style.isValid())style.setProperty("value",(float)style.getProperty("value",1.f)>.5f?3.f:0.f,nullptr);
        state.setProperty("styleSchema",1,nullptr);
    }
    stopMonitoring();loudnessPaused=false;
    editorState.width=juce::jlimit(720,1400,(int)view.getProperty("width",900));editorState.height=juce::jlimit(440,900,(int)view.getProperty("height",600));
    editorState.help=(bool)view.getProperty("help",false);editorState.details=true;
    editorState.advancedControls=(bool)view.getProperty("advancedControls",true);
    editorState.historyView=view.hasProperty("historyMotionSchema")?juce::jlimit(0,1,(int)view.getProperty("historyView",1)):1;editorState.loudnessTrace=(bool)view.getProperty("loudnessTrace",true);
    autoReset=(bool)view.getProperty("autoReset",false);
    snapshotSlot=juce::jlimit(0,1,(int)view.getProperty("slot",0));
    state.removeChild(view,nullptr);std::array<juce::ValueTree,2> saved;
    for(int i=state.getNumChildren()-1;i>=0;--i)if(state.getChild(i).hasType("SOUND"))
    {auto child=state.getChild(i);saved[(size_t)juce::jlimit(0,1,(int)child.getProperty("slot",0))]=child.createCopy();state.removeChild(i,nullptr);}
    // Newly appended controls have safe defaults when old states omit them.
    for(auto* id:{"truePeak","outputQuality","renderQuality","zeroLatency"})
        if(!state.getChildWithProperty("id",id).isValid()) {juce::ValueTree child("PARAM");child.setProperty("id",id,nullptr);child.setProperty("value",0.f,nullptr);state.addChild(child,-1,nullptr);}
    if(!state.hasProperty("oversamplingSchema")){
        for(auto* id:{"outputQuality","renderQuality"}){auto child=state.getChildWithProperty("id",id);const int old=juce::jlimit(0,2,int(child.getProperty("value",0)));
            child.setProperty("value",old==0?0:old+1,nullptr);}
        state.setProperty("oversamplingSchema",1,nullptr);
    }
    // Before 7.3.20 the protection switch also selected reconstructed metering.
    if(!state.getChildWithProperty("id","truePeakMeter").isValid()){
        juce::ValueTree child("PARAM");child.setProperty("id","truePeakMeter",nullptr);
        child.setProperty("value",state.getChildWithProperty("id","truePeak").getProperty("value",0.f),nullptr);state.addChild(child,-1,nullptr);
    }
    if(!state.getChildWithProperty("id","attack").isValid()) {juce::ValueTree child("PARAM");child.setProperty("id","attack",nullptr);child.setProperty("value",275.f,nullptr);state.addChild(child,-1,nullptr);}
    if(!state.getChildWithProperty("id","style").isValid()) {
        juce::ValueTree child("PARAM");child.setProperty("id","style",nullptr);
        child.setProperty("value",state.getChildWithProperty("id","mode").getProperty("value",3.f),nullptr);state.addChild(child,-1,nullptr);
    }
    const auto legacyLink=state.getChildWithProperty("id","link").getProperty("value",100.f);
    for(auto* id:{"lookahead","transientLink","releaseLink"})if(!state.getChildWithProperty("id",id).isValid()){
        juce::ValueTree child("PARAM");child.setProperty("id",id,nullptr);
        child.setProperty("value",juce::String(id)=="lookahead"?juce::var(.180f):legacyLink,nullptr);state.addChild(child,-1,nullptr);
    }
    if(!state.getChildWithProperty("id","styleSelect").isValid()){
        juce::ValueTree child("PARAM");child.setProperty("id","styleSelect",nullptr);
        child.setProperty("value",state.getChildWithProperty("id","style").getProperty("value",3.f),nullptr);state.addChild(child,-1,nullptr);
    }
    restoringState=true;parameters.replaceState(state);restoringState=false;undoManager.clearUndoHistory();
    for(int i=0;i<2;++i)if(saved[(size_t)i].isValid()){snapshots[(size_t)i]=saved[(size_t)i];if(!snapshots[(size_t)i].hasProperty("attack"))snapshots[(size_t)i].setProperty("attack",275.f,nullptr);
        auto& sound=snapshots[(size_t)i];if(!sound.hasProperty("lookahead"))sound.setProperty("lookahead",.180f,nullptr);
        for(auto* id:{"transientLink","releaseLink"})if(!sound.hasProperty(id))sound.setProperty(id,sound.getProperty("link",100.f),nullptr);
    }else saveSnapshot(i);
    {const juce::ScopedLock lock(presetLock);
    presetName=state.getProperty("presetName","Custom").toString();presetKey=state.getProperty("presetKey").toString();presetNotes=state.getProperty("presetNotes").toString();
    // Factory keys remain stable across display-name changes. Restore only
    // current metadata, preserving the session's sound and modified baseline.
    if(presetKey.startsWith("factory:")){const auto currentName=LimiterPresets::factoryName(presetKey);if(currentName.isNotEmpty())presetName=currentName;}
    const auto savedBaseline=state.getChildWithName("PRESET_BASE").getChildWithName("VALUES");presetBaseline=savedBaseline.isValid()?LimiterPresets::soundValues(savedBaseline):presetValues();}
    ++editorState.revision;parameterChanged("styleSelect",parameters.getRawParameterValue("styleSelect")->load());updateLatency();
}
juce::AudioProcessorEditor* OpenLimiterAudioProcessor::createEditor(){return new OpenLimiterAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new OpenLimiterAudioProcessor();}
