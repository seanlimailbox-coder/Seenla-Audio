#pragma once

#include <cmath>

// Presentation timestamps, not timer arrival times, drive the animation.
// On 120 Hz displays this selects every other refresh; higher rates use the
// nearest available presentation slot. No missed-frame catch-up loop.
class ClipperFramePacer
{
public:
    bool present (double timeMs) noexcept
    {
        constexpr double period = 1000.0 / 60.0;
        const double elapsed = last < 0 ? period : timeMs-last;
        const bool reset = last < 0 || elapsed < 0 || elapsed > 250;
        last = timeMs;
        // Follow native ~60 Hz (including 59.94) without periodically skipping
        // a refresh due to a fractional mismatch with a nominal 60 Hz target.
        if (reset || elapsed >= period*.95)
        { next = timeMs+period; return true; }
        if (timeMs+.25 < next) return false;
        next += period * (1+std::floor ((timeMs+.25-next)/period));
        return true;
    }
    void reset() noexcept { last = next = -1; }
private:
    double last = -1, next = -1;
};
