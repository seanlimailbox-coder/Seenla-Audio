#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>

// Causal exponential mean-square detector. Time is a power-domain time
// constant, not a rectangular window or an audio delay. Keep this warm even
// in Peak mode so switching cannot resurrect stale or empty RMS state.
class CompressorRmsDetector
{
public:
    void prepare(double rate, double milliseconds) noexcept
    {
        sampleRate=rate; power={};
        weight.reset(rate,.02);
        weight.setCurrentAndTargetValue(coefficient(milliseconds));
    }
    void setResponseTime(double milliseconds) noexcept { weight.setTargetValue(coefficient(milliseconds)); }
    void process(float left,float right) noexcept
    {
        const double c=weight.getNextValue();
        power[0]+=c*((double)left*left-power[0]);
        power[1]+=c*((double)right*right-power[1]);
    }
    float level(int channel) const noexcept { return (float)std::sqrt(juce::jmax(0.0,power[(size_t)channel])); }
private:
    double coefficient(double milliseconds) const noexcept
    { return -std::expm1(-1.0/(sampleRate*juce::jlimit(5.0,100.0,milliseconds)*.001)); }
    double sampleRate=44100;
    std::array<double,2> power{};
    juce::SmoothedValue<double> weight;
};
