#pragma once
#include <algorithm>
#include <cmath>

// Message-thread visual state. Paints sample the last presentation frame,
// so clipped control repaints cannot show a different bypass appearance.
class BypassFade
{
public:
    static constexpr double durationMilliseconds=160.0;
    float amount() const noexcept { return current; }
    bool active() const noexcept { return fading; }
    void setBypassed(bool bypassed,double now,bool animate=true) noexcept
    {
        const float next=bypassed?0.f:1.f;
        if(!initialised||!animate){initialised=true;current=target=next;fading=false;return;}
        if(std::abs(next-target)<.001f)return;
        advance(now);
        from=current;target=next;start=now;
        duration=durationMilliseconds*std::abs(target-from);
        fading=duration>.001;
        if(!fading)current=target;
    }
    bool advance(double now) noexcept
    {
        if(!fading)return false;
        const float progress=(float)std::clamp((now-start)/duration,0.,1.);
        current=from+(target-from)*progress*progress*(3.f-2.f*progress);
        if(progress>=1){current=target;fading=false;}
        return true;
    }
    void settle() noexcept { current=target;fading=false; }
private:
    float current=1,target=1,from=1;
    double start=0,duration=durationMilliseconds;
    bool initialised=false,fading=false;
};
