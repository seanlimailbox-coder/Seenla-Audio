#pragma once
#include <algorithm>

// Keep the entire reference stroke inside the plot at either boundary.
// This affects drawing only; parameter coordinates and drag sensitivity stay exact.
inline float plotStrokeCentre (float position, float extent, float thickness) noexcept
{
    const float inset = std::min (std::max (0.0f, extent) * 0.5f, thickness * 0.5f);
    return std::clamp (position, inset, std::max (inset, extent - inset));
}
