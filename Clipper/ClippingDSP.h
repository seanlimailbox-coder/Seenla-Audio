#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ClippingDSP
{
// Same 64-tap/double-precision reconstruction, with L/R in SIMD lanes.
// Mirroring makes the oldest-to-newest window contiguous without shifting it.
struct StereoPeakHistory
{
    using Vec = juce::dsp::SIMDRegister<double>;
    std::array<Vec, 128> samples;
    size_t write = 0;
    StereoPeakHistory() noexcept { clear(); }
    void clear() noexcept { samples.fill (Vec (0.0)); write = 0; }
    void push (float left, float right) noexcept
    {
        alignas (sizeof (Vec)) double lanes[Vec::size()] {};
        lanes[0] = left; lanes[1] = right;
        samples[write] = samples[write + 64] = Vec::fromRawArray (lanes);
        write = (write + 1) & 63;
    }
    template<int Count, int Stride = 1, int First = 0, bool IncludeCentre = true, size_t Phases>
    float peak (const std::array<std::array<double, 64>, Phases>& kernels) const noexcept
    {
        std::array<Vec, Count> sums;
        sums.fill (Vec (0.0));
        for (size_t tap = 0; tap < 64; ++tap)
            for (size_t phase = 0; phase < Count; ++phase)
                sums[phase] += samples[write + tap] * kernels[First + phase * Stride][tap];
        const auto centre = samples[write + 31];
        float result = IncludeCentre ? std::max ((float) std::abs (centre.get (0)), (float) std::abs (centre.get (1))) : 0;
        for (const auto& sum : sums)
            result = std::max (result, std::max ((float) std::abs (sum.get (0)), (float) std::abs (sum.get (1))));
        return result;
    }
};

// Exact rolling extrema, including the current sample, with bounded storage.
// Equal values keep the newest position so plateaus expire at the right time.
template<size_t Window, bool Minimum>
struct SlidingExtreme
{
    struct Entry { float value; uint64_t position; };
    std::array<Entry, Window + 1> queue {};
    size_t head = 0, tail = 0;
    uint64_t position = 0;
    void clear() noexcept { head = tail = 0; position = 0; }
    float push (float value) noexcept
    {
        constexpr size_t capacity = Window + 1;
        if (head != tail && position - queue[head].position >= Window) head = (head + 1) % capacity;
        while (head != tail)
        {
            const auto back = (tail + capacity - 1) % capacity;
            if (Minimum ? queue[back].value < value : queue[back].value > value) break;
            tail = back;
        }
        queue[tail] = { value, position++ }; tail = (tail + 1) % capacity;
        return queue[head].value;
    }
};

struct DbGainCache
{
    float db = std::numeric_limits<float>::quiet_NaN(), gain = 1;
    float get (float value) noexcept
    {
        if (! juce::exactlyEqual (value, db))
        {
            db = value;
            gain = std::abs (value) < 0.0001f ? 1.0f : juce::Decibels::decibelsToGain (value);
        }
        return gain;
    }
};

// A bounded knee: unity below entry, zero slope at exit. Hard's maximum
// knee starts 6 dB below the cap; zero is the exact original hard clip.
inline float shape (float magnitude, float cap, float softness, bool cubic, bool hard) noexcept
{
    const float width = cap * (hard ? 0.5f : 0.8f) * softness;
    const float start = cap - width;
    if (width < 0.000001f || magnitude <= start) return std::min (magnitude, cap);
    const float t = std::clamp ((magnitude - start) / (2.0f * width), 0.0f, 1.0f);
    return start + width * (cubic && ! hard ? 2*t - 2*t*t*t + t*t*t*t : 2*t - t*t);
}

struct Biquad
{
    double z1 = 0, z2 = 0;
    double process (double x, const std::array<double, 5>& c) noexcept
    {
        const double y = c[0]*x + z1;
        z1 = c[1]*x - c[3]*y + z2;
        z2 = c[2]*x - c[4]*y;
        return y;
    }
};

struct CrossoverCoefficients
{
    std::array<double, 5> low {}, high {}, allpass {};
    static CrossoverCoefficients make (double hz, double rate) noexcept
    {
        const double k = std::tan (3.14159265358979323846 * std::clamp (hz / rate, 0.000001, 0.45));
        const double n = 1.0 / (1.0 + std::sqrt (2.0)*k + k*k);
        const double a1 = 2*(k*k-1)*n, a2 = (1-std::sqrt (2.0)*k+k*k)*n;
        return { { k*k*n, 2*k*k*n, k*k*n, a1, a2 },
                 { n, -2*n, n, a1, a2 }, { a2, a1, 1, a1, a2 } };
    }
};

struct CrossoverCache
{
    double hz = -1, rate = -1, pole = 0;
    CrossoverCoefficients coefficients;
    void update (double newHz, double newRate) noexcept
    {
        if (juce::exactlyEqual (hz, newHz) && juce::exactlyEqual (rate, newRate)) return;
        hz = newHz; rate = newRate;
        pole = std::exp (-juce::MathConstants<double>::twoPi * hz / rate);
        coefficients = CrossoverCoefficients::make (hz, rate);
    }
};

// LR4 at each split, with the upper split's allpass applied to Low so
// all three lanes share the same phase response. This network feeds only
// band corrections; it never replaces the unaltered dry baseline.
struct FocusedSplit
{
    std::array<Biquad, 2> lowLP, lowHP, highLP, highHP;
    Biquad lowPhase;
    std::array<float, 3> process (double x, const CrossoverCoefficients& lo,
                                const CrossoverCoefficients& hi) noexcept
    {
        const double low = lowLP[1].process (lowLP[0].process (x, lo.low), lo.low);
        const double upper = lowHP[1].process (lowHP[0].process (x, lo.high), lo.high);
        return { (float) lowPhase.process (low, hi.allpass),
                 (float) highLP[1].process (highLP[0].process (upper, hi.low), hi.low),
                 (float) highHP[1].process (highHP[0].process (upper, hi.high), hi.high) };
    }
};
}
