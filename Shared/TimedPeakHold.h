#pragma once
#include <atomic>
#include <algorithm>
#include <cmath>

// Meter-only hold/release. Elapsed-time evaluation keeps the fall continuous
// across frame-rate changes, editor closure and suspended audio callbacks.
class TimedPeakHold
{
public:
    static constexpr double holdMilliseconds=3000.;
    static constexpr float fallAcceleration=6.f; // dB (or LU) per second squared
    explicit TimedPeakHold(float minimum=0.f) noexcept : floor(minimum),peak(minimum) {}
    void push(float value,double now) noexcept
    {
        // Compare with the released marker, not its original peak. Lower
        // signals neither renew the hold nor snap it down when the hold ends.
        if(std::isfinite(value)&&value>floor&&value>=this->value(now))
        {peak=value;expires=now+holdMilliseconds;}
    }
    float value(double now) const noexcept
    {
        const float captured=peak.load();
        const double seconds=std::max(0.,now-expires.load())*.001;
        return std::max(floor,captured-(float)(.5*fallAcceleration*seconds*seconds));
    }
    void reset() noexcept {peak=floor;expires=0.;}
private:
    static_assert(std::atomic<double>::is_always_lock_free);
    const float floor;
    std::atomic<float> peak;
    std::atomic<double> expires{0};
};
