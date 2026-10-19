#pragma once
#include <JuceHeader.h>
struct LimiterPreset
{
    juce::String name,category,key,notes;
    juce::File file;
    juce::ValueTree values;
    bool factory=false;
};
namespace LimiterPresets
{
inline const juce::StringArray& soundIds(){static const juce::StringArray ids{"lookahead","attack","release","transientLink","releaseLink","styleSelect"};return ids;}
inline juce::ValueTree soundValues(const juce::ValueTree& source){juce::ValueTree values("VALUES");for(const auto& id:soundIds())if(source.hasProperty(id))values.setProperty(id,source.getProperty(id),nullptr);return values;}
juce::File userFolder();
std::vector<LimiterPreset> list();
juce::String factoryName(const juce::String& key);
bool read(const juce::File&,LimiterPreset&,juce::String& error);
bool write(const juce::File&,const LimiterPreset&,juce::String& error);
}
