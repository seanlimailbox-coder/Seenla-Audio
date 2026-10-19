#pragma once
#include <array>
#include <algorithm>
#include <cmath>

// Uniformly spaced monotone cubic Hermite interpolation. The harmonic-mean
// tangents agree at adjoining segments and vanish at extrema/flat runs.
// Bezier controls remain between endpoints, so the curve cannot overshoot.
struct LimiterLoudnessSpline
{
    struct Controls { float first,second; };
    static float tangent(float previous,float current,float next) noexcept
    {
        const float left=current-previous,right=next-current;
        return left*right>0?2.f*left*right/(left+right):0.f;
    }
    static Controls controls(float previous,float start,float end,float next) noexcept
    {return {start+tangent(previous,start,end)/3.f,end-tangent(start,end,next)/3.f};}
};

// Presentation only. Raw loudness samples and standards-based calculations
// remain untouched. Invalid readings start/stop runs rather than drawing a
// fabricated ramp from -infinity.
class LimiterLoudnessTrace
{
public:
    LimiterLoudnessTrace() noexcept { reset(); }
    void reset(double rate=60) noexcept
    { values.fill(-100.f);amount=(float)(1.-std::exp(-1./(std::max(1.,rate)*.024))); }
    float push(int selection,float level) noexcept
    {
        auto& value=values[(size_t)selection];
        if(!std::isfinite(level)||level<=-99.f)value=-100.f;
        else value=value<=-99.f?level:value+amount*(level-value);
        return value;
    }
private:
    std::array<float,3> values{};
    float amount=1;
};
