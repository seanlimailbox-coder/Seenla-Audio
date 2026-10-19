#pragma once
#include <JuceHeader.h>
namespace LimiterStyles
{
inline juce::StringArray names(){return {"Impact","Contour","Force","Clear","Guard","Balance","Density","Glue","Zero"};}
inline int index(const juce::String& text){auto current=names();for(int i=0;i<current.size();++i)if(current[i].equalsIgnoreCase(text))return i;
    const juce::StringArray former{"Punchy","Dynamic","Aggressive","Transparent","Safe","Allround","Modern","Bus","Zero"};
    if(text.equalsIgnoreCase("Punch"))return 0;for(int i=0;i<former.size();++i)if(former[i].equalsIgnoreCase(text))return i;return -1;}
}
