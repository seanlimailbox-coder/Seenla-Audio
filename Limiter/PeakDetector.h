#pragma once
#include <JuceHeader.h>

// A longer, denser detector for ceiling-targeted protection. Metering retains
// its independent lightweight kernel. Mirrored SIMD history is preallocated.
struct LimiterPeakDetector
{
    static constexpr int taps=128,phases=31;
    using Vec=juce::dsp::SIMDRegister<double>;
    std::array<Vec,taps*2> samples{};
    std::array<std::array<double,taps>,phases> kernels{};
    // Complement the long reconstruction with the shorter Hann-window
    // interpolators used by common BS.1770 meters (49 high-rate taps).
    // They can read slightly higher at particular frequencies. Align their
    // centres with the long detector, and target the same actual ceiling.
    std::array<std::array<double,25>,6> meterWeights{};
    std::array<std::array<int,25>,6> meterIndices{};
    std::array<int,6> meterCounts{};
    int write=0;
    void prepare() noexcept
    {
        for(int phase=0;phase<phases;++phase)
        {
            double sum=0;
            for(int tap=0;tap<taps;++tap)
            {
                const double x=tap-63.-(phase+1)/32.;
                const double window=.42-.5*std::cos(juce::MathConstants<double>::twoPi*tap/(taps-1.))
                    +.08*std::cos(2*juce::MathConstants<double>::twoPi*tap/(taps-1.));
                const double weight=std::sin(juce::MathConstants<double>::pi*x)/(juce::MathConstants<double>::pi*x)*window;
                kernels[(size_t)phase][(size_t)tap]=weight;sum+=weight;
            }
            for(auto& weight:kernels[(size_t)phase])weight/=sum;
        }
        meterWeights={};meterIndices={};meterCounts={};int entry=0;
        for(int factor:{4,2})for(int phase=0;phase<factor;++phase,++entry)
            for(int j=phase;j<49;j+=factor)
            {
                const double x=(j-24.)/factor;
                const double weight=(j==24?1.:std::sin(juce::MathConstants<double>::pi*x)/(juce::MathConstants<double>::pi*x))
                    *.5*(1-std::cos(juce::MathConstants<double>::twoPi*j/48.));
                const int n=meterCounts[(size_t)entry]++;
                meterWeights[(size_t)entry][(size_t)n]=weight;
                meterIndices[(size_t)entry][(size_t)n]=63+24/factor-j/factor;
            }
        clear();
    }
    void clear() noexcept {samples.fill(Vec(0.));write=0;}
    void push(float left,float right) noexcept
    {
        alignas(sizeof(Vec)) double lanes[Vec::size()]{};lanes[0]=left;lanes[1]=right;
        samples[(size_t)write]=samples[(size_t)(write+taps)]=Vec::fromRawArray(lanes);write=(write+1)&(taps-1);
    }
    float peak() const noexcept
    {
        std::array<Vec,phases> sums;sums.fill(Vec(0.));
        for(int tap=0;tap<taps;++tap)for(int phase=0;phase<phases;++phase)
            sums[(size_t)phase]+=samples[(size_t)(write+tap)]*kernels[(size_t)phase][(size_t)tap];
        const auto centre=samples[(size_t)(write+63)];
        double result=std::max(std::abs(centre.get(0)),std::abs(centre.get(1)));
        for(const auto& sum:sums)result=std::max(result,std::max(std::abs(sum.get(0)),std::abs(sum.get(1))));
        for(int phase=0;phase<6;++phase)
        {
            Vec sum(0.);
            for(int n=0;n<meterCounts[(size_t)phase];++n)
                sum+=samples[(size_t)(write+meterIndices[(size_t)phase][(size_t)n])]*meterWeights[(size_t)phase][(size_t)n];
            result=std::max(result,std::max(std::abs(sum.get(0)),std::abs(sum.get(1))));
        }
        return (float)result;
    }
};
