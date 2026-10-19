#pragma once
#include "TruePeakCoefficients.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace open_limiter::tp {
using Stereo=std::array<float,2>;
// These finite delays are derived from the actual reconstruction cascade.
inline int reconstructionFactor(double rate) noexcept {
    int factor=2;while(factor<8&&rate*(factor*2)<=384000.)factor*=2;return factor;
}
inline int reconstructionDelay(double rate) noexcept {const int f=reconstructionFactor(rate);return 19+(f>=4?2:0)+(f>=8?1:0);}
inline int smoothingLength(double rate) noexcept {return std::max(1,int(1.3300000429153442f*float(rate*.001))+1);}
inline int outputDelay(double rate) noexcept {return reconstructionDelay(rate)+smoothingLength(rate)-1;}

template<class T> class DelayLine {
    std::vector<T> ring;int cursor=0,mask=0,delay=0;
public:
    void prepare(int count){delay=count;int size=1;while(size<=delay)size*=2;ring.assign(size,{});mask=size-1;cursor=0;}
    T process(T value) noexcept {ring[cursor]=value;const auto result=ring[(cursor-delay)&mask];cursor=(cursor+1)&mask;return result;}
    void reset() noexcept {std::fill(ring.begin(),ring.end(),T{});cursor=0;}
};

class Halfband {
    std::array<Stereo,128> history{};int cursor=0,zeros=64;
public:
    Stereo past(int delay) const noexcept {return history[cursor+64-delay];}
    template<size_t N> std::array<Stereo,2> process(Stereo input,const std::array<float,N>& taps) noexcept {
        cursor=(cursor+1)&63;history[cursor]=history[cursor+64]=input;
        zeros=input==Stereo{}?std::min(64,zeros+1):0;
        if(zeros>=int(N))return {};
        const Stereo even=past(int(N/2));Stereo odd{};
#if defined(__aarch64__)
        auto sum=vdupq_n_f64(0.);
        for(size_t k=0;k<N/2;++k){const auto a=vcvt_f64_f32(vld1_f32(history[cursor+64-int(k)].data()));
            const auto b=vcvt_f64_f32(vld1_f32(history[cursor+64-int(N-1-k)].data()));
            sum=vaddq_f64(sum,vmulq_n_f64(vaddq_f64(a,b),taps[k]));}
        odd={float(vgetq_lane_f64(sum,0)),float(vgetq_lane_f64(sum,1))};
#else
        for(int c=0;c<2;++c){double sum=0;for(size_t k=0;k<N/2;++k)sum+=(double(past(int(k))[c])+double(past(int(N-1-k))[c]))*taps[k];odd[c]=float(sum);}
#endif
        return {even,odd};
    }
    void reset() noexcept {history={};cursor=0;zeros=64;}
};

class Exposer {
    std::array<Halfband,3> stages;int factor=8,delay=22;float margin=1;
public:
    void prepare(double rate,float safetyMargin=0) noexcept {factor=reconstructionFactor(rate);delay=reconstructionDelay(rate);margin=1.f+safetyMargin;reset();}
    int latency() const noexcept {return delay;}
    Stereo process(Stereo input) noexcept {
        const auto first=stages[0].process(input,firstHalfband);Stereo peak{};
        const auto accumulate=[&peak](Stereo sample){for(int c=0;c<2;++c)peak[c]=std::max(peak[c],std::abs(sample[c]));};
        for(auto a:first){
            if(factor==2){accumulate(a);continue;}
            const auto second=stages[1].process(a,laterHalfband);
            for(auto b:second){if(factor==4)accumulate(b);else for(auto d:stages[2].process(b,laterHalfband))accumulate(d);}
        }
        for(auto& value:peak)value*=margin;
        // Four-phase / twelve-tap bound aligned with the half-band estimate.
#if defined(__aarch64__)
        std::array<float64x2_t,4> sums;for(auto& sum:sums)sum=vdupq_n_f64(0.);
        for(int row=0;row<12;++row){const auto sample=stages[0].past(delay+5-row);const auto x=vcvt_f64_f32(vld1_f32(sample.data()));
            for(int phase=0;phase<4;++phase)sums[phase]=vaddq_f64(sums[phase],vmulq_n_f64(x,auxiliaryPhases[row*4+phase]));}
        auto maximum=vcvt_f64_f32(vld1_f32(peak.data()));for(auto sum:sums)maximum=vmaxq_f64(maximum,vabsq_f64(sum));
        peak={float(vgetq_lane_f64(maximum,0)),float(vgetq_lane_f64(maximum,1))};
#else
        for(int c=0;c<2;++c)for(int phase=0;phase<4;++phase){double sum=0;for(int row=0;row<12;++row)sum+=double(stages[0].past(delay+5-row)[c])*auxiliaryPhases[row*4+phase];peak[c]=std::max(peak[c],std::abs(float(sum)));}
#endif
        return peak;
    }
    void reset() noexcept {for(auto& stage:stages)stage.reset();}
};

class Correction {
    struct Entry {int64_t time=0;float value=0;};
    std::vector<Entry> queue;int head=0,count=0,mask=0,length=1;int64_t time=0;
    struct Box {
        std::vector<double> history;double sum=0;int cursor=0;
        void prepare(int length){history.assign(length,0.);sum=0;cursor=0;}
        double process(double input) noexcept {sum+=input-history[cursor];history[cursor]=input;if(++cursor==int(history.size()))cursor=0;return sum/double(history.size());}
        void reset() noexcept {std::fill(history.begin(),history.end(),0.);sum=0;cursor=0;}
    } first,second;
public:
    void prepare(double rate){length=smoothingLength(rate);int size=1;while(size<=length)size*=2;queue.assign(size,{});mask=size-1;
        first.prepare((length+1)/2);second.prepare(length+1-(length+1)/2);reset();}
    float process(float peak,float threshold) noexcept {
        const float excess=std::max(std::abs(peak)-threshold,0.f);
        const float wanted=1.f-(threshold/(threshold+excess))*(1.f-std::min(excess,.004f));
        while(count&&queue[head].time<time-(length-1)){head=(head+1)&mask;--count;}
        while(count&&queue[(head+count-1)&mask].value<=wanted)--count;
        queue[(head+count)&mask]={time++,wanted};++count;
        const float held=queue[head].value;
        const auto smooth=second.process(first.process(held));
        return float(std::clamp(1.-smooth,0.,1.));
    }
    void reset() noexcept {head=count=0;time=0;first.reset();second.reset();}
};

// Works on the full frame so bypass/delta/history share the audio's real delay.
template<class Frame> class OutputStage {
    Exposer detector;std::array<Correction,2> correction;DelayLine<Frame> audio;
public:
    int latency=0;
    void prepare(double rate){detector.prepare(rate);for(auto& c:correction)c.prepare(rate);latency=outputDelay(rate);audio.prepare(latency);}
    Frame process(Frame input,float ceilingAmplitude) noexcept {
        const auto peaks=detector.process(input.wet);Stereo gains;
        for(int c=0;c<2;++c)gains[c]=correction[c].process(peaks[c],ceilingAmplitude);
        auto frame=audio.process(input);
        for(int c=0;c<2;++c){frame.wet[c]*=gains[c];if(gains[c]<1.f)frame.reduction[c]+=-20.f*std::log10(std::max(gains[c],1.e-15f));}
        return frame;
    }
    void reset() noexcept {detector.reset();for(auto& c:correction)c.reset();audio.reset();}
};
}
