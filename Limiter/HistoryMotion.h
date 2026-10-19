#pragma once
#include <algorithm>
#include <cmath>

// Set to 0 to restore the previous sweep motion without reverting other changes.
#ifndef OPEN_LIMITER_SMOOTH_SWEEP_MOTION
#define OPEN_LIMITER_SMOOTH_SWEEP_MOTION 1
#endif

// Camera only: captured frames retain their time order and horizontal scale.
// Sweep drifts at a quarter of the regular scrolling speed. A mode switch
// changes camera velocity, never the width of already captured peaks.
class LimiterHistoryMotion
{
public:
    static constexpr double scrollSeconds=8.,sweepSeconds=scrollSeconds;
    static constexpr double returnSeconds=.60,startPosition=.32,returnHead=.40,changeSeconds=.35;
    static constexpr bool smoothSweepMotion=OPEN_LIMITER_SMOOTH_SWEEP_MOTION!=0;
    static constexpr double sweepRampSeconds=.16;
    void reset(double initialHead,double seconds=0.) noexcept
    {window=std::max(1.,seconds>0?seconds:preferredSeconds());
     head=sweep?startPosition:std::clamp(initialHead,0.,1.);velocity=0;segmentTime=segmentLength=0;parked=false;initialized=true;
     if(sweep&&smoothSweepMotion)beginForward();}
    void park() noexcept
    {head=1.;velocity=0;segmentTime=segmentLength=0;returning=false;parked=true;initialized=true;}
    void restartAtStart() noexcept
    {head=startPosition;velocity=0;segmentTime=segmentLength=0;returning=parked=false;initialized=true;
     if(smoothSweepMotion)beginForward();}
    void setPageHasSignal(bool present) noexcept {pageHasSignal=present;}
    void setMode(int selection) noexcept
    {
        const bool next=selection!=0;if(next==sweep)return;sweep=next;
        if(parked){if(sweep)return;parked=false;}
        if(!initialized){head=sweep?startPosition:0.;return;}
        if(sweep&&head>=.9999){if(!pageHasSignal)park();else beginReturn();}
        else {const double speed=(sweep?.75:1.)/window,end=std::min(1.,head+speed*changeSeconds);
            beginSegment(end,end<1.?speed:0.,changeSeconds,false);}
    }
    void advance(double elapsed) noexcept
    {
        if(!initialized||parked)return;
        double remaining=std::clamp(elapsed,0.,30.);
        while(remaining>1.e-9)
        {
            if(segmentLength>0)
            {
                const double step=std::min(remaining,segmentLength-segmentTime);segmentTime+=step;remaining-=step;
                evaluateSegment();
                if(segmentTime>=segmentLength-1.e-9){const bool finishedReturn=returning;head=endHead;velocity=endVelocity;segmentLength=0;returning=false;
                    if(sweep&&smoothSweepMotion){
                        if(finishedReturn)beginForward();
                        else if(head>=1.-1.e-9){if(!pageHasSignal){park();return;}beginReturn();}
                    }}
                continue;
            }
            if(sweep)
            {
                if(smoothSweepMotion&&head<1.-1.e-9){
                    const double speed=.75/window,brakeStart=1.-speed*sweepRampSeconds*.5;
                    const double toBrake=std::max(0.,(brakeStart-head)/speed),step=std::min(remaining,toBrake);
                    head+=step*speed;velocity=speed;remaining-=step;
                    if(toBrake<=step+1.e-9)beginSegment(1.,0.,std::max(1.e-6,2*(1.-head)/speed),false);
                    continue;
                }
                const double speed=.75/window,toEdge=std::max(0.,(1.-head)/speed),step=std::min(remaining,toEdge);
                head+=step*speed;velocity=speed;remaining-=step;
                if(toEdge<=step+1.e-9){head=1.;if(!pageHasSignal){park();return;}beginReturn();}
            }
            else
            {
                // Ease the camera into full-width scrolling as the newest
                // boundary reaches the rail; preserve the picture on a switch.
                const double speed=1./window,tau=.10,gap=speed*tau;
                const double linear=std::max(0.,(1.-gap-head)/speed),step=std::min(remaining,linear);
                head+=step*speed;remaining-=step;
                if(remaining>0){head=1.-(1.-head)*std::exp(-remaining/tau);remaining=0;}
                velocity=std::min(speed,(1.-head)/tau);
                if(1.-head<1.e-7){head=1.;velocity=0;}
            }
        }
    }
    double fraction() const noexcept {return std::clamp(head,0.,1.);}
    double seconds() const noexcept {return window;}
    double preferredSeconds() const noexcept {return sweep?sweepSeconds:scrollSeconds;}
    bool isInitialized() const noexcept {return initialized;}
    bool isReturning() const noexcept {return returning;}
    bool isParked() const noexcept {return parked;}
    int mode() const noexcept {return sweep?1:0;}
private:
    void beginForward() noexcept {const double speed=.75/window;beginSegment(std::min(1.,head+speed*sweepRampSeconds*.5),speed,sweepRampSeconds,false);}
    void beginReturn() noexcept {if(smoothSweepMotion)velocity=0;beginSegment(returnHead,smoothSweepMotion?0.:.75/window,returnSeconds,true);}
    void beginSegment(double end,double finalVelocity,double duration,bool rewind) noexcept
    {startHead=head;startVelocity=velocity;endHead=end;endVelocity=finalVelocity;segmentTime=0;segmentLength=duration;returning=rewind;}
    void evaluateSegment() noexcept
    {
        const double t=segmentTime/segmentLength,t2=t*t,t3=t2*t,t4=t3*t,t5=t4*t;
        const double smooth=10*t3-15*t4+6*t5,b0=t-6*t3+8*t4-3*t5,b1=-4*t3+7*t4-3*t5;
        head=startHead+(endHead-startHead)*smooth+segmentLength*(startVelocity*b0+endVelocity*b1);
        velocity=((endHead-startHead)*(30*t2-60*t3+30*t4))/segmentLength
                 +startVelocity*(1-18*t2+32*t3-15*t4)+endVelocity*(-12*t2+28*t3-15*t4);
    }
    bool initialized=false,sweep=true,returning=false,parked=false,pageHasSignal=true;
    double window=scrollSeconds;
    double head=startPosition,velocity=0,segmentTime=0,segmentLength=0;
    double startHead=0,startVelocity=0,endHead=0,endVelocity=0;
};
