#pragma once
#include "BypassFade.h"

// Animate only the clearance caused by a page. Threshold dragging itself
// remains direct, and switching between pages with the same clearance holds
// the badge in place. Use the same timing/reversal policy as the other fades.
class BadgeMotion
{
public:
    BadgeMotion() { transition.setBypassed(true,0,false); }
    void setPageVisible(bool visible,double now,bool animate)
    { transition.setBypassed(!visible,now,animate); }
    bool advance(double now) { return transition.advance(now); }
    void settle() { transition.settle(); }
    float position(float normalY,float raisedY) const
    { return normalY+(raisedY-normalY)*transition.amount(); }
private:
    BypassFade transition;
};
