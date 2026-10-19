#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <condition_variable>
#include <mutex>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#endif
#include <vector>

namespace open_limiter {
struct Controls {float drive=0,attack=275,release=400,ceiling=0,transientLink=1,releaseLink=1,lookahead=.180f;bool instant=false;};
struct Frame {std::array<float,2> wet{},raw{},driven{},reduction{};};
// Persistent parameter IDs retain their 6.x meanings.
inline constexpr std::array<const char*,9> styles{"Punchy","Dynamic","Aggressive","Transparent","Safe","Allround","Modern","Bus","Zero"};
int coreLatency(double rate,int style) noexcept;
int pathLatency(double rate,int style,int quality,bool truePeak,bool instant=false) noexcept;
class Path;
// One active path and one prepared replacement: large DSP graphs are constructed
// and destroyed by the worker, never by the host's audio callback.
class StreamingLimiter {
public:
    StreamingLimiter();
    ~StreamingLimiter();
    void prepare(double rate,int style,int quality,bool truePeak,const Controls&);
    void request(int style,int quality,bool truePeak,const Controls&) noexcept;
    Frame process(float left,float right,const Controls&) noexcept;
    void reset() noexcept;
    void release() { stop(); }
    int latency() const noexcept {return reportedLatency.load();}
    int activeConfiguration() const noexcept {return activeKey.load();}
    uint64_t corrections() const noexcept;
    // Audio-thread diagnostic: no extra synchronization in the callback.
    bool isIdle() const noexcept;
private:
    void stop();
    void run();
    void wakeWorker() noexcept;
    bool retire(Path*) noexcept;
    double rate=48000;
    Path* active=nullptr;
    Path* incoming=nullptr;
    std::atomic<Path*> ready{nullptr};
    std::array<std::atomic<Path*>,8> garbage{};
    std::atomic<bool> exiting{false};
    std::atomic<int> requestedKey{3},activeKey{3},warmingKey{-1},reportedLatency{0};
    std::array<std::atomic<float>,8> requestedControls{};
    std::thread worker;
#if defined(__APPLE__)
    dispatch_semaphore_t workerSignal=dispatch_semaphore_create(0);
#else
    std::mutex workerMutex;std::condition_variable workerWake;
#endif
    int warming=0,fadePosition=0,fadeLength=1;
    bool fadingOut=false;
};
}
