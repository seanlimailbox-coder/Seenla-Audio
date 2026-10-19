#pragma once
#include "../Shared/EditableValueStyle.h"
#include "../Shared/PopupMenuMotion.h"
#include <JuceHeader.h>
#include "LimiterProcessor.h"
#include "ReductionBadges.h"
#include "DisplayCurves.h"
#include "HistoryMotion.h"
#include "VisualTransitions.h"
#include "../Clipper/FramePacing.h"

class LimiterLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void fillTextEditorBackground(juce::Graphics&,int,int,juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&,int,int,juce::TextEditor&) override;
    LimiterLookAndFeel();
    void preparePopupMenuWindow(juce::Component& window)override{popupMotion.prepare(window);}
    void setPopupMenuOwner(juce::Component& owner){popupMotion.setOwner(owner);}
    void dismissPopupMenus(){popupMotion.dismiss();}
    void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider::SliderStyle,juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
    void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool) override;
    juce::Font getTextButtonFont(juce::TextButton&,int) override;
    juce::Font getPopupMenuFont() override;
    void drawToggleButton(juce::Graphics&,juce::ToggleButton&,bool,bool) override;
    void drawComboBox(juce::Graphics&,int,int,bool,int,int,int,int,juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void drawLabel(juce::Graphics&,juce::Label&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    void positionComboBoxText(juce::ComboBox&,juce::Label&) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String&,juce::Point<int>,juce::Rectangle<int>) override;
    void drawTooltip(juce::Graphics&,const juce::String&,int,int) override;
    void drawCornerResizer(juce::Graphics&,int,int,bool,bool) override;
private:
    friend struct LimiterPopupMotionAudit;
    OpenPopupMenuMotion popupMotion;
    OpenEditableValueStyle valueStyle;
    juce::Colour valueAccent(juce::Component&);
    juce::Image wrenchIcon;
};
// Same bounded presentation clock used by the Open dynamics family.
class LimiterHistoryClock
{
public:
    double advance(uint64_t sequence,uint64_t epoch,double now,double rate,double delay)
    {
        const double latest=(double)sequence,elapsed=lastTime<0?0:juce::jmax(0.0,now-lastTime);
        const bool advanced=sequence!=lastSequence;if(advanced)lastAudioTime=now;
        if(position<0||epoch!=lastEpoch||sequence<lastSequence){position=juce::jmax(0.0,latest-delay);running=latest>=delay;lastAudioTime=now;}
        else if(elapsed>250||latest-position>192){position=juce::jmax(position,juce::jmax(0.0,latest-delay));running=latest-position>=delay;}
        else if(!running)running=latest-position>=delay;
        else{position=juce::jmin(latest,position+elapsed*rate/1000);if(!advanced&&now-lastAudioTime>250&&position>=latest)running=false;}
        lastTime=now;lastSequence=sequence;lastEpoch=epoch;return position;
    }
    void reset(){position=lastTime=-1;running=false;}
private:
    double position=-1,lastTime=-1,lastAudioTime=0;
    uint64_t lastSequence=0,lastEpoch=0;bool running=false;
};
class LimiterDisplay final : public juce::Component,public juce::SettableTooltipClient
{
public:
    explicit LimiterDisplay(OpenLimiterAudioProcessor&);
    ~LimiterDisplay() override = default;
    void paint(juce::Graphics&) override;
    void resized() override { repaint(); }
    void setChartHeight(int height) { if(chartHeight!=height){chartHeight=height;repaint();} }
    int getChartHeight() const noexcept { return chartHeight>0?chartHeight:getHeight(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void tick(double);
    void setHistoryView(int selection) { motion.setMode(selection);if(selection==0)reductionBadges.clearVisibility();repaint(); }
    int historyView() const noexcept { return motion.mode(); }
    double historyHead() const noexcept { return motion.fraction(); }
    bool isSweepIdle() const noexcept { return motion.mode()==1&&motion.isParked(); }
    double visibleSeconds() const noexcept { return motion.seconds(); }
    double visibleFrames() const noexcept { return visibleSeconds()*p.historyRate(); }
    float historyX(float index) const noexcept
    { return (float)(motion.fraction()*getWidth()-(framesCount-index+scrollFraction)*getWidth()/visibleFrames()); }
    void setFrozen(bool);
    bool isFrozen() const noexcept { return frozen; }
    void setActive(bool);
    void setLoudnessVisible(bool shown) { showLoudness=shown;if(!active)loudnessOpacity=shown?1.f:0.f;repaint(); }
    bool isLoudnessVisible() const noexcept { return showLoudness; }
    void setCeilingDragging(bool editing) { ceilingDragging=editing;repaint(); }
    void setCeilingTextEditing(bool editing) { ceilingTextEditing=editing;repaint(); }
    bool isCeilingEditing() const noexcept { return ceilingDragging||ceilingTextEditing; }
    float loudnessTraceOpacity() const noexcept { return loudnessOpacity; }
    int loudnessDisplayMode() const noexcept {return modeFade.selection();}
    float loudnessModeOpacity() const noexcept {return modeFade.opacity();}
    int loudnessBarSelection(int slot) const noexcept {return modeFade.barSelection(slot);}
    float loudnessBarOpacity(int slot) const noexcept {return modeFade.barOpacity(slot);}
    float gainReductionOpacity() const noexcept { return reductionOpacity; }
    float historyAppearanceOpacity() const noexcept { return historyAppearance; }
    float boundaryLineOpacity() const noexcept { return boundaryOpacity; }
    float ceilingGuideOpacity() const noexcept { return ceilingOpacity; }
    float freezeOverlayOpacity() const noexcept { return frozenOpacity; }
    float inspectionGuideOpacity() const noexcept { return inspectionOpacity; }
    uint64_t displayedSequence() const noexcept { return lastSequence; }
    double presentationPosition() const noexcept { return double(lastSequence)+scrollFraction; }
    float latestHistoryOutput() const noexcept { return validFrames?frames.back().output:0.f; }
    const juce::Path& historyOutline(int index) const noexcept { return paintPaths[(size_t)index]; }
    float historyLoudness(int selection) const noexcept { return validFrames?frames.back().loudness[(size_t)selection]:-100.f; }
    float displayLoudnessTail(int selection) const noexcept
    { return frozen?frozenLoudnessTail[(size_t)selection]:hasLoudnessTail?loudnessTail[(size_t)selection]:p.liveLoudness(selection); }
    void setLoudnessTail(int selection,float level)
    {
        hasLoudnessTail=true;
        if(juce::exactlyEqual(loudnessTail[(size_t)selection],level))return;
        loudnessTail[(size_t)selection]=level;
        if(!frozen&&showLoudness&&(int)p.parameters.getRawParameterValue("loudnessMode")->load()==selection)
            loudnessTailDirty=true;
    }
private:
    bool hasLoudnessTail=false,loudnessTailDirty=false;
    static constexpr int framesCount=720;
    void capture(uint64_t selected=UINT64_MAX);
    void appendSilence(int count);
    bool pageHasSignal() const noexcept;
    void restartEmptySweep(const std::array<float,3>& oldLoudness);
    bool updateBadgeVisibility(double elapsedMs);
    void refreshHistoryEdge();
    void updateListeningText(int mode);
    int inspectionIndex(float x) const noexcept;
    float ceilingY() const;
    OpenLimiterAudioProcessor& p;
    std::array<OpenLimiterAudioProcessor::HistoryFrame,framesCount> frames{};
    std::array<float,framesCount> reductionCurve{};
    std::array<std::array<float,2>,framesCount> signalDb{};
    std::array<juce::Path,8> paintPaths;
    std::array<juce::Image,2> historyFadeStrips;
    std::vector<float> fadeColumnAlpha;
    std::array<OpenLimiterAudioProcessor::HistoryFrame,2> edgeFrames{};
    std::array<float,2> edgeReduction{};
    int edgeFrameCount=0;
    std::array<std::array<float,3>,framesCount> loudnessCurve{};
    std::array<float,3> loudnessTail{-100.f,-100.f,-100.f},frozenLoudnessTail{-100.f,-100.f,-100.f};
    LimiterReductionTrace reductionTrace;
    LimiterLoudnessTrace loudnessTrace;
    uint64_t lastSequence=0,lastSourceSequence=0,silentFrames=0,lastEpoch=0;
    int validFrames=0,inspect=-1,chartHeight=0;
    bool frozen=false,active=false,showLoudness=true,ceilingDragging=false,ceilingTextEditing=false;

    float scrollFraction=0,reductionOpacity=1,loudnessOpacity=1;
    float historyAppearance=1,boundaryOpacity=0,ceilingOpacity=0,frozenOpacity=0;
    float inspectionOpacity=0;int lastInspect=-1;
    float lastBoundary=0;
    std::array<float,3> previousIdleLoudness{-100.f,-100.f,-100.f};
    bool retainIntegratedHistory=false;
    juce::Label statusLabel;
    int displayedMonitor=0,statusWidth=2;
    float statusOpacity=0;
    double lastFadeTime=-1;
    double animationTime=0,lastTickTime=-1;
    LimiterHistoryClock clock;
    LimiterHistoryMotion motion;
    LimiterLoudnessModeFade modeFade;
    LimiterReductionBadges reductionBadges;
};
class LimiterMeters final : public juce::Component,public juce::SettableTooltipClient
{
public:
    explicit LimiterMeters(OpenLimiterAudioProcessor& x,const LimiterDisplay& source):p(x),history(source)
    {setOpaque(true);setTooltip("Output, GR and M/S markers hold for three seconds, then fall with acceleration. Output numbers retain the overall sample/true-peak maximum. Click held readings above OUT/GR to clear peak holds; click small I/S/M bars to change loudness view.");}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void tick(double now);
    void resetAnimation() { lastTime=-1;lraMotion.reset(); }
    float displayLoudness(int selection) const noexcept
    {return lastTime<0?p.liveLoudness(selection):animatedLoudness[(size_t)selection];}
    void setLayout(int height,int gutter,int padding=0) { chartHeight=height;scaleGutter=gutter;rightPadding=padding;repaint(); }
    int getLogicalWidth() const noexcept { return getWidth()+scaleGutter-rightPadding; }
    int getScaleGutter() const noexcept { return scaleGutter; }
    int getChartHeight() const noexcept { return chartHeight>0?chartHeight:getHeight(); }
    static float mapLevel(float db,float top,float markedBottom,float bottom) noexcept;
private:
    OpenLimiterAudioProcessor& p;
    const LimiterDisplay& history;
    int chartHeight=0,scaleGutter=0,rightPadding=0;
    double lastTime=-1;
    std::array<float,3> animatedLoudness{-100.f,-100.f,-100.f};
    LimiterLraMotion lraMotion;
};
class LimiterGainSlider final : public juce::Slider
{
public:
    LimiterGainSlider();
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void resized() override;
    void cancelEditing();
private:
    void positionEditor();
    void finishEditing(bool commit);
    juce::TextEditor valueEditor;
    bool dragging=false,fineDrag=false;
    float dragStartY=0.f,dragStartHandleY=0.f;
};
class LimiterDetailsBackground final:public juce::Component
{public:void paint(juce::Graphics&) override;};
class LimiterResetButton final:public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onPopup;
    void mouseDown(const juce::MouseEvent& event) override
    {if(event.mods.isPopupMenu()){if(onPopup)onPopup();}else juce::TextButton::mouseDown(event);}
};
class OpenLimiterAudioProcessorEditor final : public juce::AudioProcessorEditor,private juce::Timer
{
public:
    explicit OpenLimiterAudioProcessorEditor(OpenLimiterAudioProcessor&);
    ~OpenLimiterAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
private:
    void setupKnob(juce::Slider&,juce::Label&,const juce::String&,const juce::String&,double);
    friend struct LimiterPopupMotionAudit;
    void timerCallback() override;
    void animate(double);
    void updateStyleMenu();
    void updateStyleControls(int style);
    void setAdvancedControls(bool shown,bool immediate=false);
    void applyAdvancedVisibility();
    int menuQuality=-1,controlStyle=-1;double menuRate=0;
    void updateReadouts();
    void setDisplayActive(bool);
    void setHelp(bool);
    void restoreView();
    void stopListening();
    void showResetOptions();
    void showPresets();void stepPreset(int direction);void savePresetAs();
    void presetError(const juce::String&);void updateBarMotion(double);
    std::vector<LimiterPreset> presetItems;
    juce::TextButton presetButton{"Default"},presetPrevious{"<"},presetNext{">"};
    std::unique_ptr<juce::FileChooser> presetChooser;
    double lastBarMotion=-1;
    OpenLimiterAudioProcessor& p;
    LimiterLookAndFeel lnf;
    LimiterDisplay display;
    LimiterMeters meters;
    LimiterDetailsBackground detailBackdrop;
    LimiterGainSlider drive;
    juce::Slider ceiling,lookahead,attack,release,transientLink,releaseLink;
    juce::Label driveLabel,ceilingLabel,lookaheadLabel,attackLabel,releaseLabel,transientLinkLabel,releaseLinkLabel,linkGroupLabel,modeLabel,latencyLabel;
    juce::ComboBox mode,loudnessView,outputQuality,renderQuality,historyView;
    juce::ToggleButton bypass,truePeak,truePeakMeter,instant;
    bool controlInstant=false;float instantOpacity=0;
    bool advancedShown=true;float advancedProgress=1;
    juce::TextButton undo{"<"},redo{">"},help{"?"},freeze{"FREEZE"};
    juce::TextButton advanced{"Advanced controls"};
    juce::TextButton trace{"Loudness"};
    LimiterResetButton reset{"RESET"};
    juce::TextButton pause{"PAUSE"};
    juce::TextButton slotA{"A"},slotB{"B"},copy{"COPY"},delta{"Delta"},compare{"COMPARE"};
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> choices;
    std::unique_ptr<juce::ParameterAttachment> styleAttachment;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggles;
    juce::VBlankAttachment vblank;
    ClipperFramePacer pacer;
    double lastNative=0,lastFrame=0,lastNativeStamp=-1,nativeOffset=0;
    uint64_t viewRevision=0;
    juce::Rectangle<int> panel;
    bool ready=false,displayActive=false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenLimiterAudioProcessorEditor)
};
