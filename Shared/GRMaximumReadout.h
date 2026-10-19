#pragma once
#include "BypassFade.h"

// Held values rise immediately. Resets crossfade without erasing history.
class GRMaximumReadout
{
public:
    bool update(float value,double now)
    {
        value=std::round(std::max(0.f,value)*100.f)/100.f;
        if(!initialised){initialised=true;current=previous=value;return true;}
        if(std::abs(value-current)<.005f)return false;
        if(value<current){previous=current;fade.setBypassed(true,now,false);fade.setBypassed(false,now);}
        current=value;return true;
    }
    bool advance(double now){return fade.advance(now);}
    void settle(){fade.settle();}
    float value()const{return current;}
    float oldValue()const{return previous;}
    float opacity()const{return fade.amount();}
private:
    float current=0,previous=0;
    bool initialised=false;
    BypassFade fade;
};
