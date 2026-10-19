#pragma once
#include <algorithm>
#include <cmath>

// Clipper's 160 ms smoothstep page transition, including reversals and
// switching page contents only after the outgoing page becomes invisible.
class PageFade
{
public:
    static constexpr double durationMilliseconds = 160.0;
    int presented() const noexcept { return shown; }
    float alpha() const noexcept { return opacity; }
    bool interactive() const noexcept { return wanted != 0 && wanted == shown && opacity > .05f; }
    bool active() const noexcept { return fading; }
    void request(int page, double now, bool animate = true) noexcept
    {
        page = std::clamp(page, 0, 2);
        if (!animate) { wanted = shown = page; opacity = page ? 1.f : 0.f; fading = false; return; }
        if (wanted == page && fading) return;
        wanted = page;
        if (shown == 0 && page != 0) { shown = page; opacity = 0; }
        from = opacity; to = page != 0 && page == shown ? 1.f : 0.f;
        start = now; duration = durationMilliseconds * std::abs(to - from);
        fading = duration > .001;
        if (!fading) { opacity = to; if (to < .5f) { shown = wanted; if (shown) beginIn(now); } }
    }
    bool advance(double now) noexcept
    {
        if (!fading) return false;
        const float progress = (float)std::clamp((now-start)/duration, 0., 1.);
        opacity = from + (to-from) * progress*progress*(3.f-2.f*progress);
        if (progress >= 1) {
            opacity = to; fading = false;
            if (to < .5f) { shown = wanted; if (shown) beginIn(now); }
        }
        return true;
    }
private:
    void beginIn(double now) noexcept { opacity = from = 0; to = 1; start = now; duration = durationMilliseconds; fading = true; }
    int wanted = 0, shown = 0;
    float opacity = 0, from = 0, to = 0;
    double start = 0, duration = durationMilliseconds;
    bool fading = false;
};
