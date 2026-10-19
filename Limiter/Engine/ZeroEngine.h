#pragma once
#include "CompiledEngine.h"

namespace open_limiter {
// Short finite-preview core: stable peak events, three finite smoothers,
// tonal/bass hold and a light program envelope. Instant uses the same causal
// hold/recovery with zero preview and one-sample (identity) smoothers.
// It deliberately retains its fixed window; the separate Lookahead control
// belongs to the eight newer styles. Optional OS/TP stages remain available.
class ZeroEngine final : public open_limiter_fast::EngineBase {
    struct Planner {
        struct Entry {int64_t expires=0;double gain=1;};
        std::vector<Entry> queue;
        struct Average {
            std::vector<double> data;double sum=0,scale=1;int cursor=0;
            void prepare(int length){data.resize(size_t(length));scale=1./length;reset();}
            void reset(){std::fill(data.begin(),data.end(),1.);sum=double(data.size());cursor=0;}
            double process(double x){sum+=x-data[size_t(cursor)];data[size_t(cursor)]=x;if(++cursor==int(data.size()))cursor=0;return std::clamp(sum*scale,0.,1.);}
        };
        std::array<Average,3> averages;
        int head=0,count=0,mask=0,window=1;int64_t lastExpiry=0;
        double releaseGain=1;
        void prepare(int capacity,int delay){int size=1;while(size<capacity+delay+4)size*=2;queue.resize(size_t(size));mask=size-1;window=delay;
            int remaining=delay;for(int i=0;i<3;++i){const int support=remaining/(3-i);remaining-=support;averages[size_t(i)].prepare(support+1);}reset();}
        void reset(){head=count=0;lastExpiry=0;releaseGain=1;for(auto& a:averages)a.reset();}
        float process(float target,int64_t sample,double coefficient,int hold){
            // Nondecreasing expiry preserves deque validity when the adaptive hold shrinks.
            // A peak already scheduled for holding cannot be released by a changing estimate.
            const int64_t expiry=std::max(lastExpiry,sample+window+hold);lastExpiry=expiry;
            while(count>0&&queue[size_t(head)].expires<sample){head=(head+1)&mask;--count;}
            while(count>0&&queue[size_t((head+count-1)&mask)].gain>=target)--count;
            queue[size_t((head+count)&mask)]={expiry,target};++count;
            const double wanted=queue[size_t(head)].gain;
            releaseGain=std::min(wanted,coefficient*releaseGain+(1-coefficient)*wanted);
            double gain=releaseGain;for(auto& a:averages)gain=a.process(gain);return float(gain);
        }
    };
    struct Features {
        double low=0,power=0,lowPower=0,bass=0,cycle=0,confidence=0,peak=0,hold=0;int64_t crossing=-1;
        void push(float input,int64_t sample,double lowCoefficient,double energyCoefficient,double rate){
            const double previous=low;low=input+(low-input)*lowCoefficient;peak=std::max(std::abs(double(input)),peak*energyCoefficient);
            power=input*double(input)+(power-input*double(input))*energyCoefficient;
            lowPower=low*low+(lowPower-low*low)*energyCoefficient;
            if((previous<0&&low>=0)||(previous>0&&low<=0)){
                if(crossing>=0){const double next=std::clamp(double(sample-crossing),rate*.0002,rate*.030);
                    const double consistent=cycle>0?std::clamp(1.-std::abs(next-cycle)/std::max(next,cycle)*5.,0.,1.):0.;
                    confidence+=.25*(consistent-confidence);cycle=next;}crossing=sample;}
            bass=std::clamp((lowPower/std::max(1.e-20,power)-.12)/.55,0.,1.);
        }
    };
    std::array<Planner,2> planners;
    std::array<Features,2> features{};
    std::array<double,2> program{1,1};
    std::array<float,2> gains{1,1},previousTransient{-1,-1},programTargets{1,1};
    std::array<std::array<double,16>,2> silenceSnapshot{};
    std::vector<std::array<float,2>> delay;
    double rate,lowCoefficient,energyCoefficient,holdCoefficient,attackCoefficient=0,releaseCoefficient=0;
    bool instant=false;
    float gainDb=-100,attackMs=-1,releaseMs=-1,ceilingDb=100,drive=1,ceiling=1,transientLink=1,releaseLink=1;
    int cursor=0;int64_t sample=0;
public:
    static int delaySamples(double rate){return std::max(1,int(std::lround(rate*.00035)));}
    ZeroEngine(float sr,float gain,float attack,float release,float ahead,float out,bool noDelay=false):rate(sr),lowCoefficient(std::exp(-2*3.14159265358979323846*160/sr)),energyCoefficient(std::exp(-1/(sr*.02))),holdCoefficient(1.-std::exp(-1/(sr*.001))),instant(noDelay){
        latency=instant?0:delaySamples(sr);delay.resize(size_t(latency)+1);
        for(auto& p:planners)p.prepare(int(std::lround(rate*.05))+4,latency);
        setParameters(gain,attack,release,ahead,out);reset();
    }
    void setParameters(float gain,float attack,float release,float,float out) override {
        if(gain!=gainDb){gainDb=gain;drive=std::pow(10.f,gain/20.f);}
        if(out!=ceilingDb){ceilingDb=out;ceiling=std::pow(10.f,out/20.f);}
        if(attack!=attackMs){attackMs=attack;attackCoefficient=attack<=0?0:std::exp(-1/(rate*attack*.0005));}
        if(release!=releaseMs){releaseMs=release;releaseCoefficient=std::exp(-1/(rate*release*.00008));}
    }
    void setLinks(float transient,float release) override {transientLink=std::clamp(transient,0.f,1.f);releaseLink=std::clamp(release,0.f,1.f);}
    std::array<float,2> process(float left,float right) override {return processTriggered(left,right,left,right);}
    std::array<float,2> processTriggered(float left,float right,float triggerLeft,float triggerRight) override {
        const std::array<float,2> input{left*drive,right*drive},trigger{triggerLeft*drive,triggerRight*drive};delay[size_t(cursor)]=input;
        const auto delayed=delay[size_t((cursor+1)%int(delay.size()))];cursor=(cursor+1)%int(delay.size());
        for(int c=0;c<2;++c)features[size_t(c)].push(input[size_t(c)],sample,lowCoefficient,energyCoefficient,rate);
        const float peak=std::max(std::abs(trigger[0]),std::abs(trigger[1]));
        const double maxBass=std::max(features[0].bass,features[1].bass),maxCycle=std::max(features[0].cycle,features[1].cycle);
        std::array<float,2> transient{},wanted{},output{};
        for(int c=0;c<2;++c){auto i=size_t(c);const double bass=features[i].bass+(maxBass-features[i].bass)*transientLink,cycle=features[i].cycle+(maxCycle-features[i].cycle)*transientLink;
            const double holdWeight=.12+.98*bass*bass*bass*bass;
            const double legacyHold=cycle*bass*holdWeight;
            const double crest=features[i].peak*features[i].peak/std::max(1.e-30,features[i].power);
            const double tonal=features[i].confidence*std::clamp((4.-crest)*.5,0.,1.);
            const double desired=std::clamp(legacyHold+tonal*(std::max(legacyHold,cycle*1.04)-legacyHold),0.,rate*.036);
            features[i].hold+=(desired-features[i].hold)*holdCoefficient;
            const int hold=int(std::lround(features[i].hold));
            const double duration=rate*(.0005+releaseMs*.000004+bass*.0015);
            // Pade approximation to exp(-1/duration); avoids two per-sample exponentials.
            const double recovery=(2*duration-1)/(2*duration+1);
            const float detected=std::abs(trigger[i])+(peak-std::abs(trigger[i]))*transientLink;
            transient[i]=planners[i].process(detected>ceiling?ceiling/detected:1.f,sample,recovery,hold);
            if(transient[i]!=previousTransient[i]){
                programTargets[i]=c==1&&transient[1]==transient[0]?programTargets[0]:std::pow(transient[i],.22f);
                previousTransient[i]=transient[i];
            }
            wanted[i]=programTargets[i];
        }
        const float shared=std::min(wanted[0],wanted[1]);
        for(int c=0;c<2;++c){auto i=size_t(c);const float target=wanted[i]+(shared-wanted[i])*releaseLink;
            program[i]=target+(program[i]-target)*(target<program[i]?attackCoefficient:releaseCoefficient);
            gains[i]=std::min(transient[i],float(program[i]));
            if(std::abs(delayed[i])*gains[i]>ceiling*1.00001f)++safetyActivations;
            if(std::abs(delayed[i])>ceiling)gains[i]=std::min(gains[i],ceiling/std::abs(delayed[i]));

        }
        const float linked=std::min(gains[0],gains[1]);
        for(int c=0;c<2;++c){auto i=size_t(c);gains[i]+= (linked-gains[i])*transientLink;output[i]=delayed[i]*gains[i];}
        ++sample;return output;
    }
    bool silenceStateUnchanged() override {
        bool unchanged=true;silenceHistorySamples=int(rate*.1)+int(delay.size());
        for(size_t i=0;i<2;++i){const auto& f=features[i];const auto& p=planners[i];
            const std::array<double,16> now{f.low,f.power,f.lowPower,f.bass,f.cycle,f.confidence,f.peak,f.hold,
                p.releaseGain,p.averages[0].sum,p.averages[1].sum,program[i],gains[i],p.averages[2].sum,double(p.window),double(p.averages[0].data.size())};
            unchanged&=now==silenceSnapshot[i];silenceSnapshot[i]=now;}
        return unchanged;
    }
    std::array<float,2> reductionGain() const override {return gains;}
    void reset() override {for(auto& p:planners)p.reset();features={};program={1,1};gains={1,1};previousTransient={-1,-1};programTargets={1,1};std::fill(delay.begin(),delay.end(),std::array<float,2>{});cursor=0;sample=0;safetyActivations=0;}
};
inline std::unique_ptr<open_limiter_fast::EngineBase> makeEngine(int style,float rate,float gain,float attack,float release,float ahead,float ceiling){
    if(style==8||style==9)return std::make_unique<ZeroEngine>(rate,gain,attack,release,ahead,ceiling,style==9);
    return open_limiter_fast::create(style,rate,gain,attack,release,ahead,ceiling);
}
}
