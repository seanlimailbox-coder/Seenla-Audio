#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Instantaneous peak pickup, with a bounded half-cycle bridge rather than a
// second attack envelope. Both channels track independently, before linking.
class GatePeakDetector
{
public:
    void prepare (double rate) noexcept
    {
        minimumBridge = std::max (1, (int) std::ceil (rate * 0.001));
        maximumBridge = std::max (minimumBridge, (int) std::ceil (rate * 0.026));
        recovery = (float) std::exp (-1.0 / (rate * 0.004));
        peak = 0;
        bridge = maximumBridge;
        sinceCrossing = polarity = 0;
        haveCrossing = false;
        // Allocated only in prepare, never process. A monotonic queue gives the
        // exact recent maximum with amortised O(1) work, even on noisy crossings.
        crests.resize ((size_t) maximumBridge + 1);
        firstCrest = crestCount = 0;
        sampleSequence = 0;
    }

    float process (float sample) noexcept
    {
        const float magnitude = std::isfinite (sample) ? std::abs (sample) : 0.0f;
        // Schmitt polarity tracking follows substantial lobes, not small HF
        // crossings riding a bass waveform. Saturate
        // elapsed time so silence/DC cannot overflow a counter in a long session.
        sinceCrossing = std::min (sinceCrossing + 1, maximumBridge * 4);
        const float deadband = std::max (1.0e-9f, peak * 0.6f);
        const int nextPolarity = sample > deadband ? 1 : sample < -deadband ? -1 : polarity;
        if (nextPolarity != polarity && nextPolarity != 0)
        {
            if (polarity != 0)
            {
                if (haveCrossing && sinceCrossing <= maximumBridge * 2)
                {
                    const int measured = std::clamp (sinceCrossing + minimumBridge,
                                                     minimumBridge, maximumBridge);
                    // Lengthen immediately for falling pitch; shorten gradually
                    // so an isolated extra crossing does not destabilise bass.
                    bridge = measured > bridge ? measured : (3 * bridge + measured) / 4;
                }
                else bridge = maximumBridge;
                haveCrossing = true;
            }
            sinceCrossing = 0;
            polarity = nextPolarity;
        }
        ++sampleSequence;
        const int capacity = (int) crests.size();
        const auto index = [capacity] (int n) { return n >= capacity ? n - capacity : n; };
        while (crestCount > 0 && sampleSequence - crests[(size_t) firstCrest].sequence >= (uint64_t) bridge)
        {
            firstCrest = index (firstCrest + 1);
            --crestCount;
        }
        // Do not discard progressively quieter crests just because they cannot
        // beat an older held peak. They become the new maximum as that peak ages
        // out, instead of letting a short recovery dive into each bass trough.
        while (crestCount > 0 && crests[(size_t) index (firstCrest + crestCount - 1)].level <= magnitude)
            --crestCount;
        if (magnitude > 1.0e-12f)
        {
            crests[(size_t) index (firstCrest + crestCount)] = { magnitude, sampleSequence };
            ++crestCount;
        }
        const float recent = crestCount > 0 ? crests[(size_t) firstCrest].level : 0.0f;
        peak = std::max (recent, peak * recovery); // Instant pickup; bounded silence/DC recovery.
        if (peak < 1.0e-12f) peak = 0;
        return peak;
    }

private:
    struct Crest { float level = 0; uint64_t sequence = 0; };
    std::vector<Crest> crests { 2 }; // Safe before prepare; host prepare sizes it for the rate.
    int firstCrest = 0, crestCount = 0;
    uint64_t sampleSequence = 0;
    float peak = 0, recovery = 0;
    int minimumBridge = 1, maximumBridge = 1, bridge = 1;
    int sinceCrossing = 0, polarity = 0;
    bool haveCrossing = false;
};
