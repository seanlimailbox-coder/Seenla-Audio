#include "Presets.h"
#include "FactoryPresets.h"
#include "LimiterProcessor.h"

juce::File LimiterPresets::userFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Application Support/Seenla Audio/Open Limiter/Presets");
   #else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Seenla Audio/Open Limiter/Presets");
   #endif
}
bool LimiterPresets::read(const juce::File& file,LimiterPreset& preset,juce::String& error)
{
    if(file.getSize()>1024*1024){error="This preset is too large.";return false;}
    auto xml=juce::XmlDocument::parse(file);if(!xml||!xml->hasTagName("OPEN_LIMITER_PRESET")||xml->getIntAttribute("version")!=1){error="This is not a supported Open Limiter preset.";return false;}
    auto tree=juce::ValueTree::fromXml(*xml);preset={};preset.name=tree.getProperty("name",file.getFileNameWithoutExtension()).toString();
    preset.category="User";preset.key=file.getFullPathName();preset.file=file;preset.notes=tree.getProperty("notes").toString();preset.values=tree.getChildWithName("VALUES");
    if(!preset.values.isValid()){error="This preset has no sound settings.";return false;}preset.values=soundValues(preset.values);return true;
}
bool LimiterPresets::write(const juce::File& file,const LimiterPreset& preset,juce::String& error)
{
    if(file.getFileExtension()!=".openlimiter"){error="Use the .openlimiter preset extension.";return false;}
    if(!file.getParentDirectory().createDirectory()){error="Could not create the preset folder.";return false;}
    juce::ValueTree tree("OPEN_LIMITER_PRESET");tree.setProperty("version",1,nullptr);tree.setProperty("name",preset.name,nullptr);tree.setProperty("notes",preset.notes,nullptr);tree.addChild(soundValues(preset.values),-1,nullptr);
    juce::TemporaryFile temporary(file);if(!tree.createXml()->writeTo(temporary.getFile())||!temporary.overwriteTargetFileWithTemporary()){error="Could not save this preset.";return false;}return true;
}
static const std::vector<LimiterPreset>& factoryPresets()
{
    static const std::vector<LimiterPreset> factory=[](){std::vector<LimiterPreset> result;const auto data=juce::JSON::parse(LimiterPresets::factoryJson);
        if(auto* items=data.getArray())for(auto& item:*items){LimiterPreset preset;preset.name=item["name"].toString();preset.category=item["category"].toString();preset.key=item["key"].toString();preset.notes=item["notes"].toString();preset.factory=true;preset.values=juce::ValueTree("VALUES");
            if(auto* values=item["values"].getDynamicObject())for(auto& value:values->getProperties())if(LimiterPresets::soundIds().contains(value.name.toString()))preset.values.setProperty(value.name,value.value,nullptr);result.push_back(preset);}
        return result;}();
    return factory;
}
juce::String LimiterPresets::factoryName(const juce::String& key)
{
    for(const auto& preset:factoryPresets())if(preset.key==key)return preset.name;
    return {};
}
std::vector<LimiterPreset> LimiterPresets::list()
{
    auto result=factoryPresets();juce::Array<juce::File> files;userFolder().findChildFiles(files,juce::File::findFiles,true,"*.openlimiter");
    for(auto file:files){LimiterPreset preset;juce::String error;if(read(file,preset,error))result.push_back(preset);}
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){if(a.factory!=b.factory)return a.factory;
        if(a.name=="Default"&&a.factory)return b.name!="Default";if(b.name=="Default"&&b.factory)return false;
        const int category=a.category.compareNatural(b.category);return category!=0?category<0:a.name.compareNatural(b.name)<0;});return result;
}
juce::ValueTree OpenLimiterAudioProcessor::presetValues() const
{
    juce::ValueTree values("VALUES");for(const auto& id:LimiterPresets::soundIds())values.setProperty(id,parameters.getRawParameterValue(id)->load(),nullptr);return values;
}
bool OpenLimiterAudioProcessor::presetModified() const noexcept
{
    const juce::ScopedLock lock(presetLock);
    for(const auto& id:LimiterPresets::soundIds())if(std::abs(parameters.getRawParameterValue(id)->load()-float(presetBaseline.getProperty(id,0.f)))>1.e-4f)return true;return false;
}
bool OpenLimiterAudioProcessor::loadPreset(const LimiterPreset& preset,juce::String& error)
{
    if(!preset.values.hasType("VALUES")){error="Invalid preset settings.";return false;}
    for(const auto& id:LimiterPresets::soundIds()){
        if(!preset.values.hasProperty(id)){error="Missing setting: "+id;return false;}
        const auto v=preset.values.getProperty(id);double value=0;
        if(v.isString()){const auto text=v.toString().trim();const char* start=text.toRawUTF8();char* end=nullptr;value=std::strtod(start,&end);
            if(start==end||*end!='\0'){error="Invalid setting: "+id;return false;}}
        else if(v.isInt()||v.isInt64()||v.isDouble()||v.isBool())value=double(v);
        else {error="Invalid setting: "+id;return false;}
        if((id=="styleSelect"||id=="outputQuality"||id=="renderQuality"||id=="truePeak"||id=="truePeakMeter")&&value!=std::round(value)){error="Invalid option: "+id;return false;}
        const auto& range=parameters.getParameter(id)->getNormalisableRange();
        if(!std::isfinite(value)||value<double(range.start)-1.e-5||value>double(range.end)+1.e-5){error="Setting outside its supported range: "+id;return false;}
    }
    stopMonitoring();parameters.copyState();undoManager.beginNewTransaction("Load "+preset.name);restoringState=true;
    for(const auto& id:LimiterPresets::soundIds()){auto* parameter=parameters.getParameter(id);parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->convertTo0to1(float(preset.values.getProperty(id))));parameter->endChangeGesture();}
    restoringState=false;parameters.copyState();undoManager.beginNewTransaction();
    {const juce::ScopedLock lock(presetLock);presetName=preset.name;presetKey=preset.key;presetNotes=preset.notes;presetBaseline=presetValues();}saveSnapshot(snapshotSlot);
    parameterChanged("styleSelect",parameters.getRawParameterValue("styleSelect")->load());updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));return true;
}
bool OpenLimiterAudioProcessor::loadPreset(const juce::File& file,juce::String& error)
{LimiterPreset preset;return LimiterPresets::read(file,preset,error)&&loadPreset(preset,error);}
bool OpenLimiterAudioProcessor::savePreset(const juce::File& file,juce::String& error)
{
    LimiterPreset preset;preset.name=file.getFileNameWithoutExtension();preset.values=presetValues();preset.notes=currentPresetNotes();
    if(!LimiterPresets::write(file,preset,error))return false;{const juce::ScopedLock lock(presetLock);presetName=preset.name;presetKey=file.getFullPathName();presetBaseline=preset.values;}updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));return true;
}
bool OpenLimiterAudioProcessor::renamePreset(const juce::String& name,juce::String& error)
{
    const auto clean=juce::File::createLegalFileName(name.trim());
    if(!isUserPreset()||clean.isEmpty()){error="Choose a user preset and a valid name.";return false;}
    auto file=juce::File(currentPresetKey());auto target=file.getSiblingFile(clean+".openlimiter");if(target!=file&&target.exists()){error="A preset with that name already exists.";return false;}
    LimiterPreset preset;if(!LimiterPresets::read(file,preset,error))return false;preset.name=clean;
    if(!LimiterPresets::write(target,preset,error))return false;
    if(target!=file&&!file.deleteFile()){target.deleteFile();error="Could not rename the original preset.";return false;}
    {const juce::ScopedLock lock(presetLock);presetName=clean;presetKey=target.getFullPathName();}updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));return true;
}
bool OpenLimiterAudioProcessor::deletePreset(juce::String& error)
{
    if(!isUserPreset()||!juce::File(currentPresetKey()).deleteFile()){error="Could not delete this user preset.";return false;}
    {const juce::ScopedLock lock(presetLock);presetKey={};presetName="Custom";presetNotes={};presetBaseline=presetValues();}updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));return true;
}
bool OpenLimiterAudioProcessor::isUserPreset() const
{const juce::ScopedLock lock(presetLock);return juce::File::isAbsolutePath(presetKey)&&juce::File(presetKey).isAChildOf(LimiterPresets::userFolder())&&juce::File(presetKey).hasFileExtension("openlimiter")&&juce::File(presetKey).existsAsFile();}
