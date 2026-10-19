#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

// Reverse-indexed Fenwick trees: suffix energy/count queries and percentile
// lookup take at most log2(Bins) steps instead of scanning every loudness bin.
// Per instance; prepared storage, no audio-thread allocation or locks.
template<int Bins> class LimiterLoudnessHistogram
{
public:
    struct Totals {double energy=0;uint64_t count=0;};
    void clear() noexcept {energy.fill(0);count.fill(0);}
    void add(int bin,double value) noexcept
    {
        for(int i=Bins-std::clamp(bin,0,Bins-1);i<=Bins;i+=i&-i){energy[(size_t)i]+=value;++count[(size_t)i];}
    }
    Totals from(int first) const noexcept
    {
        Totals result;
        for(int i=Bins-std::clamp(first,0,Bins);i>0;i-=i&-i){result.energy+=energy[(size_t)i];result.count+=count[(size_t)i];}
        return result;
    }
    // Zero-based rank in descending order. Call only with rank < total count.
    int descendingRank(uint64_t rank) const noexcept
    {
        int index=0;
        for(int bit=highestPower;bit;bit>>=1){const int next=index+bit;
            if(next<=Bins&&count[(size_t)next]<=rank){index=next;rank-=count[(size_t)next];}}
        return std::max(0,Bins-1-index);
    }
private:
    static constexpr int highestPower=[] {int n=1;while(n<=Bins/2)n*=2;return n;}();
    std::array<double,Bins+1> energy{};
    std::array<uint64_t,Bins+1> count{};
};
