#include "StreamingLimiter.h"
#include "ZeroEngine.h"
#include "TruePeak.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace open_limiter {
using Stereo=std::array<float,2>;
static int key(int style,int quality,bool tp,bool instant=false){if(style==8&&instant)return 80;return std::clamp(style,0,8)+9*std::clamp(quality,0,3)+(tp?36:0);}
static int factor(int quality){return 1<<std::clamp(quality,0,3);}
int coreLatency(double rate,int style) noexcept {
    if(style==8)return ZeroEngine::delaySamples(rate);
    struct Counts {int stages=0,convolvers=0,clippers=0;};
    static const std::array<Counts,8> counts=[] {
        std::array<Counts,8> result{};
        for(size_t i=0;i<result.size();++i){
            auto p=std::find_if(open_limiter_research::presets.begin(),open_limiter_research::presets.end(),[&](const auto& preset){return preset.name==styles[i];});
            if(p==open_limiter_research::presets.end())continue;
            auto read=[&](const char* name){auto entry=p->ints.find(name);return entry==p->ints.end()?0:entry->second;};
            result[i]={read("Num Look-ahead Stages"),read("Num Output Convolvers"),read("Num Output SmartClippers")};
        }
        return result;
    }();
    const auto c=counts[(size_t)std::clamp(style,0,7)];const float internalRate=float(rate);
    return int(internalRate*.005)*c.stages+256*c.convolvers+int(internalRate*.05)*c.clippers;
}
int pathLatency(double rate,int style,int quality,bool tp,bool instant) noexcept {
    if(style==8&&instant)return 0;
    const int f=factor(quality),pre=tp?open_limiter::tp::reconstructionDelay(rate*f):0;
    return (coreLatency(rate*f,style)+pre+f-1)/f+(f>1?64:0)+(tp?open_limiter::tp::outputDelay(rate):0);
}
static double bessel(double x){double s=1,t=1;for(int j=1;j<80;++j){t*=x*x/(4*j*j);s+=t;if(t<s*1e-16)break;}return s;}
static std::vector<double> fir(int f,int span){
    const int count=span*f+1;std::vector<double> h(count);double sum=0,den=bessel(10);
    for(int j=0;j<count;++j){double x=(j-(count-1)*.5)/f,u=2.*j/(count-1)-1;
        h[j]=(std::abs(x)<1e-12?1.:std::sin(M_PI*x)/(M_PI*x))/f*bessel(10*std::sqrt(std::max(0.,1-u*u)))/den;sum+=h[j];}
    for(auto& v:h)v/=sum;return h;
}
template<class T> struct Delay {
    std::vector<T> data;int cursor=0,mask=0;
    void prepare(int n,T initial={}){int size=1;while(size<=n)size*=2;data.assign(size,initial);mask=size-1;cursor=0;}
    T process(T x,int n){data[cursor]=x;T y=data[(cursor-n)&mask];cursor=(cursor+1)&mask;return y;}
    void reset(T initial={}){std::fill(data.begin(),data.end(),initial);cursor=0;}
};
struct Fir {
    std::vector<double> taps,peakWeights,symmetricSum,symmetricDifference;std::vector<Stereo> samples;int cursor=0,mask=0,ringSize=0,phases=1,rows=0,zeroSamples=0;
    void prepare(int f,int span,bool fullRate=false){
        taps=fir(f,span);int count=1;while(count<(fullRate?(int)taps.size():span+1))count*=2;
        samples.assign(count*2,{});ringSize=count;mask=count-1;cursor=0;phases=f;rows=span+1;zeroSamples=count;
        peakWeights.assign(rows*f,0.);for(size_t j=0;j<taps.size();++j)peakWeights[j]=taps[j]*f;
        const int half=f/2;symmetricSum.assign(span/2*half,0.);symmetricDifference.assign(span/2*half,0.);
        for(int k=0;k<span/2;++k)for(int p=1;p<=half;++p){const double a=peakWeights[size_t(k*f+p)],b=peakWeights[size_t((span-1-k)*f+p)];
            symmetricSum[size_t(k*half+p-1)]=(a+b)*.5;symmetricDifference[size_t(k*half+p-1)]=(a-b)*.5;}

    }
    bool silent() const {return zeroSamples>=ringSize;}
    void push(Stereo x){zeroSamples=(x[0]==0.f&&x[1]==0.f)?std::min(zeroSamples+1,ringSize):0;cursor=(cursor+1)&mask;samples[cursor]=samples[cursor+ringSize]=x;}
    Stereo phase(int p,int f,double scale=1) const {
#if defined(__aarch64__)
        float64x2_t sum=vdupq_n_f64(0);int k=0;
        for(int j=p;j<(int)taps.size();j+=f,++k){auto values=vld1_f32(samples[cursor+ringSize-k].data());auto x=vcvt_f64_f32(values);
            sum=vaddq_f64(sum,vmulq_n_f64(x,taps[j]*scale));}
        return {(float)vgetq_lane_f64(sum,0),(float)vgetq_lane_f64(sum,1)};
#else
        std::array<double,2> sum{};int k=0;
        for(int j=p;j<(int)taps.size();j+=f,++k){auto x=samples[cursor+ringSize-k];for(int c=0;c<2;++c)sum[c]+=x[c]*taps[j]*scale;}
        return {(float)sum[0],(float)sum[1]};
#endif
    }
#if defined(__aarch64__)
    // Reflected phase pair: a*x+b*y and b*x+a*y share
    // ((a+b)/2)*(x+y) +/- ((a-b)/2)*(x-y). All operations and
    // accumulators remain double precision; filters and timing are unchanged.
    template<int Phases> float symmetricPeak() const {
        constexpr int Groups=Phases/4;const int halfRows=(rows-1)/2;
        std::array<float64x2_t,Groups> leftSum,rightSum,leftDifference,rightDifference;
        for(int group=0;group<Groups;++group)leftSum[group]=rightSum[group]=leftDifference[group]=rightDifference[group]=vdupq_n_f64(0.);
        auto phaseZero=vdupq_n_f64(0.);
        for(int k=0;k<halfRows;++k){const auto x=vcvt_f64_f32(vld1_f32(samples[cursor+ringSize-k].data()));
            const auto reflected=vcvt_f64_f32(vld1_f32(samples[cursor+ringSize-(rows-2-k)].data()));
            const auto sum=vaddq_f64(x,reflected),difference=vsubq_f64(x,reflected);
            const auto zeroReflected=vcvt_f64_f32(vld1_f32(samples[cursor+ringSize-(rows-1-k)].data()));
            phaseZero=vfmaq_n_f64(phaseZero,vaddq_f64(x,zeroReflected),peakWeights[size_t(k*Phases)]);
            const double* sums=symmetricSum.data()+k*(Phases/2);const double* differences=symmetricDifference.data()+k*(Phases/2);
#pragma clang loop unroll(full)
            for(int group=0;group<Groups;++group){const auto a=vld1q_f64(sums+2*group),b=vld1q_f64(differences+2*group);
                leftSum[group]=vfmaq_laneq_f64(leftSum[group],a,sum,0);rightSum[group]=vfmaq_laneq_f64(rightSum[group],a,sum,1);
                leftDifference[group]=vfmaq_laneq_f64(leftDifference[group],b,difference,0);rightDifference[group]=vfmaq_laneq_f64(rightDifference[group],b,difference,1);}}
        const auto centre=vcvt_f64_f32(vld1_f32(samples[cursor+ringSize-halfRows].data()));
        auto maximum=vabsq_f64(vfmaq_n_f64(phaseZero,centre,peakWeights[size_t(halfRows*Phases)]));
        for(int group=0;group<Groups;++group){maximum=vmaxq_f64(maximum,vabsq_f64(vaddq_f64(leftSum[group],leftDifference[group])));
            maximum=vmaxq_f64(maximum,vabsq_f64(vsubq_f64(leftSum[group],leftDifference[group])));
            maximum=vmaxq_f64(maximum,vabsq_f64(vaddq_f64(rightSum[group],rightDifference[group])));
            maximum=vmaxq_f64(maximum,vabsq_f64(vsubq_f64(rightSum[group],rightDifference[group])));}
        return (float)std::max(vgetq_lane_f64(maximum,0),vgetq_lane_f64(maximum,1));
    }

#endif
    float reconstructedPeak() const {
        if(silent())return 0.f;
        float result=0;
#if defined(__aarch64__)
        if(phases==8)return symmetricPeak<8>();if(phases==16)return symmetricPeak<16>();
#endif
        for(int p=0;p<phases;++p){auto x=phase(p,phases,phases);result=std::max({result,std::abs(x[0]),std::abs(x[1])});}
        return result;
    }
    template<int Factor> std::array<Stereo,Factor> interpolate() const {
        std::array<Stereo,Factor> result{};if(silent())return result;
#if defined(__aarch64__)
        std::array<float64x2_t,Factor/2> left,right;
        for(int group=0;group<Factor/2;++group)left[group]=right[group]=vdupq_n_f64(0.);
        // Independent phase lanes preserve ascending-tap arithmetic while
        // loading each input sample once for every interpolated phase.
        for(int k=0;k<rows;++k){const auto x=samples[cursor+ringSize-k];const double* weights=peakWeights.data()+k*Factor;
#pragma clang loop unroll(full)
            for(int group=0;group<Factor/2;++group){const auto w=vld1q_f64(weights+2*group);
                left[group]=vaddq_f64(left[group],vmulq_n_f64(w,x[0]));right[group]=vaddq_f64(right[group],vmulq_n_f64(w,x[1]));}}
        for(int group=0;group<Factor/2;++group){result[size_t(2*group)]={float(vgetq_lane_f64(left[group],0)),float(vgetq_lane_f64(right[group],0))};
            result[size_t(2*group+1)]={float(vgetq_lane_f64(left[group],1)),float(vgetq_lane_f64(right[group],1))};}
#else
        for(int p=0;p<Factor;++p)result[size_t(p)]=phase(p,Factor,Factor);
#endif
        return result;
    }
    Stereo full() const {
        if(silent())return {};
#if defined(__aarch64__)
        // Four independent sums remove the long serial accumulator dependency.
        // Retain every tap and double precision for decimation.
        auto a=vdupq_n_f64(0.),b=a,c=a,d=a;size_t j=0;
        for(;j+3<taps.size();j+=4){
            a=vaddq_f64(a,vmulq_n_f64(vcvt_f64_f32(vld1_f32(samples[cursor+ringSize-int(j)].data())),taps[j]));
            b=vaddq_f64(b,vmulq_n_f64(vcvt_f64_f32(vld1_f32(samples[cursor+ringSize-int(j)-1].data())),taps[j+1]));
            c=vaddq_f64(c,vmulq_n_f64(vcvt_f64_f32(vld1_f32(samples[cursor+ringSize-int(j)-2].data())),taps[j+2]));
            d=vaddq_f64(d,vmulq_n_f64(vcvt_f64_f32(vld1_f32(samples[cursor+ringSize-int(j)-3].data())),taps[j+3]));}
        auto sum=vaddq_f64(vaddq_f64(a,b),vaddq_f64(c,d));
        for(;j<taps.size();++j)sum=vaddq_f64(sum,vmulq_n_f64(vcvt_f64_f32(vld1_f32(samples[cursor+ringSize-int(j)].data())),taps[j]));
        return {float(vgetq_lane_f64(sum,0)),float(vgetq_lane_f64(sum,1))};
#else
        return phase(0,1);
#endif
    }
    void reset(){std::fill(samples.begin(),samples.end(),Stereo{});cursor=0;zeroSamples=ringSize;}
};
class Path {
public:
    const int configuration,oversampling;
    std::unique_ptr<open_limiter_fast::EngineBase> engine;
    const int latency;
    Fir up,down;tp::OutputStage<Frame> guard;tp::Exposer inputDetector;
    Delay<Stereo> inputAudio;int inputDelay=0,mainDelay=0;
    Delay<Stereo> padding,reductionDelay;
    Delay<Frame> metadata;
    int pad=0;bool truePeak=false;
    float smoothedDrive=0,smoothedCeiling=0;double smoothing=0;
    float cachedDriveDb=0,cachedCeilingDb=0,cachedDrive=1,cachedCeiling=1;
    uint64_t sampleBounds=0;
    int silentSamples=0,probeCountdown=1,stableSamples=0,probeInterval=1,silenceHorizon=1;
    bool sleeping=false;Frame silenceFrame{};Controls silenceControls{};
    float probeDrive=0,probeCeiling=0;
    static bool sameControls(const Controls& a,const Controls& b){return a.drive==b.drive&&a.ceiling==b.ceiling&&a.attack==b.attack&&a.release==b.release&&a.lookahead==b.lookahead&&a.transientLink==b.transientLink&&a.releaseLink==b.releaseLink;}
    static bool sameFrame(const Frame& a,const Frame& b){return a.wet==b.wet&&a.raw==b.raw&&a.driven==b.driven&&a.reduction==b.reduction;}

    Path(double rate,int configuration,const Controls& c):configuration(configuration),oversampling(factor((configuration%36)/9)),engine(makeEngine(configuration>=72?9:configuration%9,float(rate*oversampling),c.drive,c.attack,c.release,c.lookahead,c.ceiling)),
        latency(pathLatency(rate,configuration%9,(configuration%36)/9,configuration>=36&&configuration<72,configuration>=72)),truePeak(configuration>=36&&configuration<72),smoothedDrive(c.drive),smoothedCeiling(c.ceiling),smoothing(std::exp(-1/(rate*.02))) {
        probeInterval=std::max(1,int(std::ceil(rate*.25)));
        silenceHorizon=std::max(latency+int(rate*2),1);silenceControls=c;
        inputDelay=truePeak?tp::reconstructionDelay(rate*oversampling):0;
        pad=(oversampling-(engine->latency+inputDelay)%oversampling)%oversampling;
        mainDelay=(engine->latency+inputDelay+pad)/oversampling+(oversampling>1?64:0);padding.prepare(pad);reductionDelay.prepare(pad+32*oversampling,Stereo{1,1});
        metadata.prepare(mainDelay);
        if(oversampling>1){up.prepare(oversampling,64);down.prepare(oversampling,64,true);}if(truePeak){guard.prepare(rate);inputDetector.prepare(rate*oversampling,.004f);inputAudio.prepare(inputDelay);}
        engine->setLinks(c.transientLink,c.releaseLink);cachedDriveDb=c.drive;cachedCeilingDb=c.ceiling;cachedDrive=std::pow(10.f,c.drive/20.f);cachedCeiling=std::pow(10.f,c.ceiling/20.f);
    }
    Stereo processEngine(Stereo input){
        if(!truePeak)return engine->process(input[0],input[1]);
        const auto trigger=inputDetector.process(input),audio=inputAudio.process(input,inputDelay);
        return engine->processTriggered(audio[0],audio[1],trigger[0],trigger[1]);
    }
    template<int Factor> void oversampled(Stereo in,Stereo& out,Stereo& reduction){
        up.push(in);const auto phases=up.interpolate<Factor>();
        for(int p=0;p<Factor;++p){const auto x=phases[size_t(p)];auto y=processEngine(x);y=padding.process(y,pad);down.push(y);
            auto gr=reductionDelay.process(engine->reductionGain(),pad+32*Factor);if(p==0){out=down.full();reduction=gr;}}
    }
    Frame process(float left,float right,const Controls& c){
        if(left!=0.f||right!=0.f){sleeping=false;silentSamples=stableSamples=0;probeCountdown=1;}
        else {
            if(sleeping&&sameControls(c,silenceControls))return silenceFrame;
            if(sleeping){sleeping=false;silentSamples=stableSamples=0;probeCountdown=1;}
            if(silentSamples<silenceHorizon)++silentSamples;
        }
        smoothedDrive=float(c.drive+(smoothedDrive-c.drive)*smoothing);
        smoothedCeiling=float(c.ceiling+(smoothedCeiling-c.ceiling)*smoothing);
        engine->setParameters(smoothedDrive,c.attack,c.release,c.lookahead,smoothedCeiling);engine->setLinks(c.transientLink,c.releaseLink);
        Stereo in{left,right},out{};std::array<float,2> reduction{1,1};
        if(oversampling==1){out=processEngine(in);reduction=engine->reductionGain();}
        else if(oversampling==2)oversampled<2>(in,out,reduction);
        else if(oversampling==4)oversampled<4>(in,out,reduction);
        else oversampled<8>(in,out,reduction);
        if(c.ceiling!=cachedCeilingDb){cachedCeilingDb=c.ceiling;cachedCeiling=std::pow(10.f,c.ceiling/20.f);}
        if(smoothedDrive!=cachedDriveDb){cachedDriveDb=smoothedDrive;cachedDrive=std::pow(10.f,smoothedDrive/20.f);}
        float ceiling=cachedCeiling,peak=std::max(std::abs(out[0]),std::abs(out[1]));
        float bound=peak>ceiling?ceiling/peak:1.f;if(bound<1)++sampleBounds;
        Frame frame;frame.raw=in;float drive=cachedDrive;
        for(int ch=0;ch<2;++ch)frame.driven[ch]=in[ch]*drive;
        frame=metadata.process(frame,mainDelay);
        float boundDb=bound<1?-20.f*std::log10(bound):0;
        auto reductionDb=[](float gain){return gain==1.f?0.f:-open_limiter_research::db(gain);};
        float leftReduction=reductionDb(reduction[0]);
        frame.wet={out[0]*bound,out[1]*bound};frame.reduction={leftReduction+boundDb,(reduction[1]==reduction[0]?leftReduction:reductionDb(reduction[1]))+boundDb};
        if(truePeak)frame=guard.process(frame,ceiling);
        if(silentSamples>=silenceHorizon&&--probeCountdown<=0){
            probeCountdown=probeInterval;
            const bool unchanged=engine->silenceStateUnchanged();
            // Wait longer than every finite delay, after exact (not thresholded)
            // scalar convergence. Never reset an envelope or gate quiet input.
            silenceHorizon=std::max(silenceHorizon,(engine->silenceHistorySamples+oversampling-1)/oversampling+latency+probeInterval);
            const bool settled=unchanged&&sameControls(c,silenceControls)&&smoothedDrive==probeDrive&&smoothedCeiling==probeCeiling&&sameFrame(frame,silenceFrame)
                &&frame.wet==Stereo{}&&frame.raw==Stereo{}&&frame.driven==Stereo{};
            stableSamples=settled?stableSamples+probeInterval:0;
            silenceControls=c;probeDrive=smoothedDrive;probeCeiling=smoothedCeiling;silenceFrame=frame;
            sleeping=stableSamples>=silenceHorizon;
        }
        return frame;
    }
    void reset(){sleeping=false;silentSamples=stableSamples=0;probeCountdown=1;engine->reset();padding.reset();reductionDelay.reset(Stereo{1,1});metadata.reset();if(oversampling>1){up.reset();down.reset();}if(truePeak){guard.reset();inputDetector.reset();inputAudio.reset();}sampleBounds=0;}
};
StreamingLimiter::StreamingLimiter()=default;
StreamingLimiter::~StreamingLimiter(){stop();
#if defined(__APPLE__)
    dispatch_release(workerSignal);
#endif
}
void StreamingLimiter::wakeWorker() noexcept {
#if defined(__APPLE__)
    dispatch_semaphore_signal(workerSignal);
#else
    workerWake.notify_one();
#endif
}
void StreamingLimiter::stop(){exiting=true;wakeWorker();if(worker.joinable())worker.join();delete active;active=nullptr;delete incoming;incoming=nullptr;delete ready.exchange(nullptr);warmingKey=-1;for(auto& slot:garbage)delete slot.exchange(nullptr);}
void StreamingLimiter::prepare(double sr,int style,int quality,bool tp,const Controls& c){
    stop();rate=sr;int k=key(style,quality,tp,c.instant);active=new Path(rate,k,c);activeKey=k;reportedLatency=active->latency;requestedKey=k;
    request(style,quality,tp,c);warming=0;fadeLength=std::max(1,int(rate*.01));fadePosition=fadeLength;fadingOut=false;exiting=false;worker=std::thread([this]{run();});
}
void StreamingLimiter::request(int style,int quality,bool tp,const Controls& c) noexcept {
    requestedControls[0]=c.drive;requestedControls[1]=c.attack;requestedControls[2]=c.release;requestedControls[3]=c.ceiling;requestedControls[4]=c.transientLink;requestedControls[5]=c.releaseLink;requestedControls[6]=c.lookahead;requestedControls[7]=c.instant?1.f:0.f;
    const int next=key(style,quality,tp,c.instant);if(requestedKey.exchange(next)!=next)wakeWorker();
}
bool StreamingLimiter::retire(Path* p) noexcept {if(!p)return true;for(auto& slot:garbage){Path* empty=nullptr;if(slot.compare_exchange_strong(empty,p)){wakeWorker();return true;}}return false;}
void StreamingLimiter::run(){
    while(!exiting.load()){
        for(auto& slot:garbage)delete slot.exchange(nullptr);
        int k=requestedKey.load();
        if(k!=activeKey.load()&&k!=warmingKey.load()&&!ready.load()){
            Controls c{requestedControls[0].load(),requestedControls[1].load(),requestedControls[2].load(),requestedControls[3].load(),requestedControls[4].load(),requestedControls[5].load(),requestedControls[6].load(),requestedControls[7].load()>.5f};
            auto* p=new Path(rate,k,c);
            if(exiting||requestedKey.load()!=k)delete p;else {Path* empty=nullptr;if(!ready.compare_exchange_strong(empty,p))delete p;}
        }
#if defined(__APPLE__)
        // Counting semaphore retains notifications even before the worker waits.
        // Steady audio needs no polling, locking, notification or wakeups.
        dispatch_semaphore_wait(workerSignal,DISPATCH_TIME_FOREVER);
#else
        std::unique_lock<std::mutex> lock(workerMutex);
        workerWake.wait_for(lock,std::chrono::milliseconds(100),[this]{
            if(exiting.load())return true;
            for(auto& slot:garbage)if(slot.load())return true;
            return requestedKey.load()!=activeKey.load()&&requestedKey.load()!=warmingKey.load()&&!ready.load();
        });
#endif
    }
}
Frame StreamingLimiter::process(float left,float right,const Controls& c) noexcept {
    if(!active)return {};
    if(!incoming&&ready.load()){
        bool canRetire=false;for(auto& slot:garbage)if(!slot.load()){canRetire=true;break;}
        if(canRetire){auto* p=ready.exchange(nullptr);if(p){if(p->configuration==requestedKey.load()){incoming=p;warmingKey=p->configuration;warming=p->latency+int(rate*.05);fadingOut=false;}else retire(p);}}
    }
    if(incoming&&!fadingOut&&incoming->configuration!=requestedKey.load()&&retire(incoming)){incoming=nullptr;warmingKey=-1;}
    Frame result=active->process(left,right,c);
    // Finish the active path's fade-in even if another prepared path arrives.
    // Warming a replacement must not restore full output halfway through it.
    if(!fadingOut&&fadePosition<fadeLength){float blend=float(fadePosition++)/fadeLength;for(auto* values:{&result.wet,&result.raw,&result.driven})for(auto& x:*values)x*=blend;}
    if(incoming){
        incoming->process(left,right,c);
        if(warming>0)--warming;
        else if(!fadingOut){fadingOut=true;fadePosition=fadeLength;}
        if(fadingOut){float blend=float(std::max(0,fadePosition))/fadeLength;for(auto* values:{&result.wet,&result.raw,&result.driven})for(auto& x:*values)x*=blend;
            if(--fadePosition<=0&&retire(active)){active=incoming;incoming=nullptr;warmingKey=-1;activeKey=active->configuration;reportedLatency=active->latency;fadePosition=0;fadingOut=false;}}
    }
    return result;
}
void StreamingLimiter::reset() noexcept {if(active)active->reset();if(incoming)incoming->reset();warming=incoming?incoming->latency+int(rate*.05):0;fadingOut=false;fadePosition=fadeLength;}
bool StreamingLimiter::isIdle() const noexcept {return active&&active->sleeping&&!incoming&&!ready.load();}
uint64_t StreamingLimiter::corrections() const noexcept {return active?active->engine->safetyActivations+active->sampleBounds:0;}
}
