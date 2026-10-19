#pragma once
#include <algorithm>
#include <array>
#include <cmath>
// Swap each visual selection only while transparent. Bar slots own their
// transitions so an unchanged I/S/M bar stays visible, even on rapid changes.
class LimiterLoudnessModeFade
{
public:
    static constexpr double halfMilliseconds=90.;
    void reset(int selection) noexcept
    {
        selection=std::clamp(selection,0,2);view.reset(selection);
        for(int slot=0;slot<3;++slot)bars[(size_t)slot].reset(contents(selection,slot));
    }
    bool advance(int requested,double milliseconds) noexcept
    {
        requested=std::clamp(requested,0,2);bool changed=view.advance(requested,milliseconds);
        for(int slot=0;slot<3;++slot)changed=bars[(size_t)slot].advance(contents(requested,slot),milliseconds)||changed;
        return changed;
    }
    int selection() const noexcept {return view.mode;}
    float opacity() const noexcept {return view.alpha;}
    int barSelection(int slot) const noexcept {return bars[(size_t)std::clamp(slot,0,2)].mode;}
    float barOpacity(int slot) const noexcept {return bars[(size_t)std::clamp(slot,0,2)].alpha;}
private:
    static int contents(int mode,int slot) noexcept
    {
        return slot==0?mode:slot==1?(mode==0?1:0):(mode==2?1:2);
    }
    struct Fade
    {
        int mode=0;float alpha=1.f;
        void reset(int selection) noexcept {mode=selection;alpha=1.f;}
        bool advance(int requested,double milliseconds) noexcept
        {
            const int oldMode=mode;const float oldAlpha=alpha;
            double remaining=std::clamp(milliseconds,0.,180.);
            if(requested!=mode){const double spent=std::min(remaining,double(alpha)*halfMilliseconds);
                alpha=std::max(0.f,alpha-float(spent/halfMilliseconds));remaining-=spent;
                if(alpha<1.e-6f){alpha=0;mode=requested;}}
            if(requested==mode)alpha=std::min(1.f,alpha+float(remaining/halfMilliseconds));
            return oldMode!=mode||oldAlpha!=alpha;
        }
    };
    Fade view;std::array<Fade,3> bars{{{0,1.f},{1,1.f},{2,1.f}}};
};

// Smooth brace endpoints in LUFS, independent of window size and frame rate.
// Initialization/reset starts at the measured range, never at a fake -INF.
class LimiterLraMotion
{
public:
    void reset() noexcept {ready=false;}
    void advance(float low,float high,bool valid,double milliseconds) noexcept
    {
        if(!valid||!std::isfinite(low)||!std::isfinite(high)||low>high){reset();return;}
        if(!ready){lower=low;upper=high;ready=true;return;}
        const float amount=float(-std::expm1(-std::clamp(milliseconds,0.,250.)/120.));
        lower+=amount*(low-lower);upper+=amount*(high-upper);
    }
    float low(float measured) const noexcept {return ready?lower:measured;}
    float high(float measured) const noexcept {return ready?upper:measured;}
private:
    bool ready=false;float lower=0,upper=0;
};
