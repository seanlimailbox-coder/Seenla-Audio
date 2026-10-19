#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Annotate distinct reduction events, rather than every small local ripple.
// Accepted peaks are immutable and travel with their history sequence.
class LimiterReductionBadges
{
public:
    struct Badge { uint64_t sequence=0; float reduction=0; double createdAt=0; float displayReduction=0,clearanceReduction=0; float obstructionOpacity=1; };
    static float opacity(const Badge& badge,double now) noexcept
    {
        if(badge.createdAt<=0)return 1;
        const auto t=(float)std::clamp((now-badge.createdAt-380.)/260.,0.,1.);
        return t*t*(3-2*t);
    }
    static float clearanceOpacity(float gap) noexcept
    {const float t=std::clamp(gap/24.f,0.f,1.f);return t*t*(3-2*t);}
    static float obstacleVisibility(float clearance,float visibility) noexcept
    {if(visibility>=1)return clearance;if(visibility<=0)return 1;return 1-visibility*(1-clearance);}
    // A fixed pixel fade can last only one frame during the camera's return.
    // Predict contact in presentation time, including acceleration, so the
    // label has time to leave before reaching a stationary control.
    static constexpr int projectionSteps=16;
    static constexpr float projectionStepSeconds=.02f;
    using Projection=std::array<float,projectionSteps+1>;
    void clearVisibility() noexcept {for(auto& badge:badges)badge.obstructionOpacity=1;}
    static float projectedClearance(float left,float right,float obstacleLeft,float obstacleRight,const Projection& offsets) noexcept
    {
        const float gap=std::max(obstacleLeft-right,left-obstacleRight);
        float opacity=clearanceOpacity(gap);if(opacity==0)return 0;
        for(int i=1;i<=projectionSteps;++i){
            const float a=offsets[size_t(i-1)],b=offsets[size_t(i)],delta=b-a;
            const float lo=obstacleLeft-right,hi=obstacleRight-left;
            if(std::max(a,b)<lo||std::min(a,b)>hi||delta==0)continue;
            const float fraction=std::clamp(((delta>0?lo:hi)-a)/delta,0.f,1.f);
            const float time=(float(i-1)+fraction)/projectionSteps;
            opacity=std::min(opacity,time*time*(3-2*time));break;
        }
        return opacity;
    }
    template<class Target> bool updateVisibility(double elapsedMs,Target&& target) noexcept
    {
        bool changed=false;const float rise=float(std::clamp(elapsedMs,0.,180.)/220.);
        for(auto& badge:badges)if(badge.sequence){
            const float wanted=std::clamp(target(badge),0.f,1.f),before=badge.obstructionOpacity;
            // Forecasting supplies the outgoing fade. Rate-limit reappearance
            // so clearing a control or freezing mid-return cannot flash it on.
            badge.obstructionOpacity=wanted<before?wanted:std::min(wanted,before+rise);
            changed|=badge.obstructionOpacity!=before;
        }
        return changed;
    }
    void reset(double rate=60) noexcept
    {
        badges={};next=0;lastAccepted=peakSequence=0;
        peak=trough=peakCurve=recentPeak=0;tracking=trackingSmall=false;firstSequence=lastLarge=recentPeakSequence=0;
        settle=std::max<uint64_t>(1,(uint64_t)std::ceil(rate*.55));
        spacing=std::max<uint64_t>(1,(uint64_t)std::ceil(rate*1.6));
        quietPeriod=std::max<uint64_t>(1,(uint64_t)std::ceil(rate*5.));smallSpacing=std::max<uint64_t>(1,(uint64_t)std::ceil(rate*3.));
        prominenceDecay=(float)std::exp(-1/(rate*.35));
    }
    Badge* push(uint64_t sequence,float reduction,double now=0,float displayReduction=-1) noexcept
    {
        const float value=std::isfinite(reduction)?std::max(0.f,reduction):0.f;
        const float curve=std::isfinite(displayReduction)&&displayReduction>=0?displayReduction:value;
        if(!firstSequence)firstSequence=sequence;
        if(value>=.9f)lastLarge=sequence;
        if(value>=recentPeak){recentPeak=value;recentPeakSequence=sequence;}
        else if(sequence-recentPeakSequence>=quietPeriod)recentPeak=std::max(value,recentPeak*prominenceDecay);
        const bool quiet=sequence-std::max(firstSequence,lastLarge)>=quietPeriod;
        // Small peaks become useful annotations only after stronger activity
        // has subsided. A held reference also suppresses low ripples beneath
        // repeated moderate peaks; it relaxes smoothly after five seconds.
        const bool dominant=recentPeak<=value*1.2f+.005f;

        if(tracking&&!trackingSmall&&quiet&&value<.9f&&sequence-peakSequence>=quietPeriod){tracking=false;trough=value;}
        if(!tracking)
        {
            trough=std::min(trough,value);
            if(value>=.9f&&(value-trough>=1.2f||trough<.2f)
               &&(!lastAccepted||sequence-lastAccepted>=spacing))
            {tracking=true;trackingSmall=false;peak=value;peakCurve=curve;peakSequence=sequence;}
            else if(quiet&&value>=.05f&&value<.9f&&dominant&&(!lastAccepted||sequence-lastAccepted>=smallSpacing))
            {tracking=trackingSmall=true;peak=value;peakCurve=curve;peakSequence=sequence;}
            return nullptr;
        }
        if(value>peak+.0001f){peak=value;peakCurve=curve;peakSequence=sequence;}
        if(trackingSmall&&value>=.9f)trackingSmall=false;
        // A persistent small reduction may settle without a large recovery.
        // Stronger events retain their existing recovery/settling rule.
        const bool smallSettled=trackingSmall&&quiet&&recentPeak<=peak*1.2f+.005f;
        // Require real recovery and let nearby stronger peaks settle first.
        if((smallSettled||value<=peak-.8f)&&sequence-peakSequence>=settle)
        {
            auto* badge=&badges[next];*badge={peakSequence,peak,now,peakCurve,peakCurve};next=(next+1)%badges.size();
            lastAccepted=peakSequence;trough=value;tracking=false;
            return badge;
        }
        return nullptr;
    }
    const auto& accepted() const noexcept { return badges; }
    const Badge& ordered(size_t index) const noexcept { return badges[(next+index)%badges.size()]; }
private:
    std::array<Badge,64> badges{};
    size_t next=0;
    uint64_t lastAccepted=0,peakSequence=0,settle=33,spacing=96;
    uint64_t firstSequence=0,lastLarge=0,recentPeakSequence=0,quietPeriod=300,smallSpacing=180;
    float peak=0,trough=0,peakCurve=0,recentPeak=0,prominenceDecay=.95f;
    bool tracking=false,trackingSmall=false;
};

// Presentation only: tame the frame-maximum sawtooth without changing audio,
// event peak readings, or the captured values used by frozen inspection.
class LimiterReductionTrace
{
public:
    LimiterReductionTrace() noexcept { reset(); }
    void reset(double rate=60) noexcept
    { value=0;attack=(float)(1-std::exp(-1/(rate*.022)));release=(float)(1-std::exp(-1/(rate*.070))); }
    float push(float peak) noexcept
    { peak=std::isfinite(peak)?std::max(0.f,peak):0.f;value+=(peak-value)*(peak>value?attack:release);return value; }
private:
    float value=0,attack=0,release=0;
};
