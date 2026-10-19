#pragma once
#include <JuceHeader.h>
#include <complex>

namespace CompressorFilters {
using Section = std::array<double, 6>;
struct Cascade { std::array<Section,2> response; std::array<std::array<double,4>,2> integrator; };

// Two differently damped biquads form one fourth-order Butterworth filter.
// ArrayCoefficients does not allocate: these are designed once per audio block.
inline Cascade design(double rate, double cutoff, bool highPass) noexcept {
    cutoff = juce::jlimit(1.0, rate * .45, cutoff);
    constexpr std::array<double, 2> q{.541196100146197, 1.306562964876377};
    Cascade result;
    const double g=std::tan(juce::MathConstants<double>::pi*cutoff/rate);
    for(size_t i=0;i<q.size();++i) {
        result.response[i] = highPass ? juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(rate,cutoff,q[i])
                            : juce::dsp::IIR::ArrayCoefficients<double>::makeLowPass(rate,cutoff,q[i]);
        const double k=1/q[i],a1=1/(1+g*(g+k));
        result.integrator[i]={a1,g*a1,g*g*a1,k};
    }
    return result;
}
inline double magnitude(const Cascade& coefficients, double frequency, double rate) noexcept {
    const auto z=std::polar(1.0,-juce::MathConstants<double>::twoPi*frequency/rate);
    double result=1;
    for(const auto& c:coefficients.response)
        result*=std::abs((c[0]+c[1]*z+c[2]*z*z)/(c[3]+c[4]*z+c[5]*z*z));
    return result;
}
}
