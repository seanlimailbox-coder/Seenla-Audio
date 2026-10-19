#pragma once
#include "../Shared/EditableValueStyle.h"
#include "../Shared/BypassFade.h"
#include "../Shared/GRMaximumReadout.h"
#include "../Shared/BadgeMotion.h"
#include <JuceHeader.h>
#include "CompressorProcessor.h"
#include "../Shared/PageFade.h"
#include "../Clipper/FramePacing.h"

class CompressorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void fillTextEditorBackground(juce::Graphics&,int,int,juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&,int,int,juce::TextEditor&) override;
    CompressorLookAndFeel();
    void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider::SliderStyle,juce::Slider&) override;
    void drawToggleButton(juce::Graphics&,juce::ToggleButton&,bool,bool) override;
    void drawComboBox(juce::Graphics&,int,int,bool,int,int,int,int,juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String&,juce::Point<int>,juce::Rectangle<int>) override;
    void drawTooltip(juce::Graphics&,const juce::String&,int,int) override;
    void drawLabel(juce::Graphics&,juce::Label&) override;
    void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
    void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool) override;
private:
    OpenEditableValueStyle valueStyle;
    juce::Colour valueAccent(juce::Component&);
};

class CompressorHistoryClock
{
public:
    double advance(uint64_t,uint64_t,double,double);
    void reset(){position=-1;lastTime=-1;}
private:
    double position=-1,lastTime=-1;
    uint64_t lastSequence=0,lastEpoch=0;
};

class CompressorDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit CompressorDisplay(OpenCompressorAudioProcessor&);
    ~CompressorDisplay() override;
    void paint(juce::Graphics&) override;
    void visibilityChanged() override;
    void setDisplayActive(bool);
    void setKneeVisible(bool);
    void setSidechainVisible(bool);
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void setFrozen(bool);
    bool isFrozen() const noexcept { return frozen; }
    void restoreView();
    void advanceHistory(double now);
    uint64_t displayedHistorySequence() const noexcept { return capturedSequence; }
    void setControlsTop(int top) { controlsTop=top; }
    void setBadgeGeometry(int mainTop,int pageTop)
    { badgeMainTop=mainTop;badgePageTop=pageTop;badgeGeometryReady=true;repaint(); }
    void setBadgePageVisible(bool visible,bool animate)
    { badgeMotion.setPageVisible(visible,juce::Time::getMillisecondCounterHiRes(),animate&&displayActive);repaint(); }
    void setDetectorVisible(bool);
    void setSidechainFocus(bool);
    std::function<void()> onReferenceEdit,onReferenceEditEnd;
    std::function<void(double)> onAnimationFrame;
private:
    friend struct CompressorFramePacingAudit;
    friend struct CompressorClosedHistoryAudit;
    friend struct CompressorHistoryContinuityAudit;
    friend struct CompressorAppearanceAudit;
    void setHistoryAppearance(BypassFade&,bool&,bool);
    BypassFade sidechainFade,detectorFade,focusFade,kneeFade;
    bool sidechainVisible=false;
    friend struct DynamicsPageFadeAudit;
    void timerCallback() override;
    void vblankCallback(double timestampSeconds);
    void fallbackFrame(double timeMs);
    void presentationFrame(double timestampSeconds,double arrivalTimeMs);
    void animationFrame(double timeMs);
    void captureHistory(bool latest=false,double now=juce::Time::getMillisecondCounterHiRes());
    void setThreshold(float);
    juce::Rectangle<float> thresholdBadgeBounds() const;
    bool hitThreshold(juce::Point<float>) const;
    OpenCompressorAudioProcessor& p;
    juce::RangedAudioParameter* threshold=nullptr;
    // Publish threshold changes on the message thread and invalidate the entire
    // plot, including when a slider requests a smaller repaint of its own.
    float displayedThreshold=0.f;
    std::unique_ptr<juce::ParameterAttachment> thresholdDisplayAttachment;
    std::array<std::unique_ptr<juce::ParameterAttachment>,4> sceneAttachments;
    BadgeMotion badgeMotion;
    int badgeMainTop=10000,badgePageTop=10000;
    bool badgeGeometryReady=false;
    friend struct FrozenInspectionAudit;
    friend struct BadgeMotionAudit;
    friend struct BadgeLimitAudit;
    BypassFade bypassFade;
    std::unique_ptr<juce::ParameterAttachment> bypassDisplayAttachment;
    friend struct BypassFadeAudit;
    friend struct PlotBoundaryAudit;
    bool dragging=false;
    bool showKnee=false;
    float dragStartY=0,dragStartDb=0;
    bool fineDrag=false,frozen=false,inspecting=false,detectorVisible=false;
    int inspected=-1,validFrames=0,controlsTop=10000;
    std::array<OpenCompressorAudioProcessor::HistoryFrame,OpenCompressorAudioProcessor::displayFrames> frames{};
    CompressorHistoryClock clock;
    bool displayActive=false;
    uint64_t captureState=0,captureStartSequence=0;
    juce::VBlankAttachment vblank;
    ClipperFramePacer framePacer;
    double lastVblankArrival=-1,vblankOffset=0,lastVblankTimestamp=-1;
    std::array<float,4> previousMeterValues{-1,-1,-1,-1};
    uint64_t capturedSequence=0,capturedEpoch=0;
    float scrollFraction=0;
    juce::Image waveImage;
    uint64_t cachedSequence=UINT64_MAX,cachedEpoch=UINT64_MAX;
    bool cachedSC=false,sidechainFocus=false;
    std::array<float,3> cachedHistoryAppearance{-1,-1,-1};
    float cachedScale=0,rasterPhase=0;
};

class CompressorSidechainPreview final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CompressorSidechainPreview (OpenCompressorAudioProcessor&);
    ~CompressorSidechainPreview() override { endDrag(); processor.setSpectrumEnabled (false); }
    void paint (juce::Graphics&) override;
    void updateSpectrum (bool frozen = false);
    float spectrumLevelAtFrequency (double hz) const noexcept;
    juce::Point<float> handlePosition (bool highPass) const;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { endDrag(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }
    void visibilityChanged() override;
    void setDisplayActive(bool enabled) { if(displayActive!=enabled){displayActive=enabled;visibilityChanged();} }
    void endDrag();
private:
    juce::Rectangle<float> plotBounds() const;
    int hitHandle (juce::Point<float>, bool preferLowPass) const;
    OpenCompressorAudioProcessor& processor;
    // Twice the frequency resolution, with the original 1024-sample update
    // cadence/smoothing. Analysis runs on the UI thread, never in the audio path.
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, fftHop = 1024;
    juce::dsp::FFT fft { fftOrder };
    std::array<std::array<float, fftSize>, 2> spectrumHistory {};
    std::array<float, fftSize> window {};
    std::array<float, fftSize * 2> fftWork {};
    std::array<float, fftSize / 2 + 1> spectrumLevels {}, spectrumPower {};
    int spectrumWrite = 0, spectrumReceived = 0, spectrumHop = 0;
    uint64_t spectrumEpoch = 0, lastSpectrumSequence = 0;
    double spectrumRate = 44100;
    void transformSpectrum();
    juce::RangedAudioParameter* dragged = nullptr;
    int hovered = -1;
    bool displayActive=true;
    bool draggingHighPass = true, fine = false;
    double originX = 0, originLog = 0;
};

class CompressorMeters final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CompressorMeters(OpenCompressorAudioProcessor& x):p(x){setComponentID("gainReductionMeter");setMouseCursor(juce::MouseCursor::PointingHandCursor);setTooltip("Maximum gain reduction above the stereo GR strip. Click to reset the held maximum.");refresh();}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { p.resetMaximumReduction();refresh(); }
    void refresh();
    void settleAppearance(){maximum.settle();bypassFade.settle();}
private:
    friend struct ClipperHistoryMeterAudit;
    friend struct GRStripAudit;
    std::array<float,5> previous{-1,-1,-1,-1,-1};
    GRMaximumReadout maximum;
    BypassFade bypassFade;
    OpenCompressorAudioProcessor& p;
};

class OpenCompressorAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
    friend struct BadgeLimitAudit;
    friend struct GRStripAudit;
public:
    explicit OpenCompressorAudioProcessorEditor(OpenCompressorAudioProcessor&);
    ~OpenCompressorAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    void focusLost(FocusChangeType) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
private:
    friend struct CompressorAppearanceAudit;
    friend struct FrozenInspectionAudit;
    friend struct BadgeMotionAudit;
    friend struct CompressorClosedHistoryAudit;
    using SA=juce::AudioProcessorValueTreeState::SliderAttachment;
    friend struct DynamicsPageFadeAudit;
    using BA=juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA=juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    void knob(juce::Slider&,juce::Label&,const juce::String&,const juce::String&,double);
    void fader(juce::Slider&,juce::Label&,const juce::String&,const juce::String&,double);
    void button(juce::ToggleButton&,const juce::String&);
    void combo(juce::ComboBox&,const juce::StringArray&);
    void placeKnob(juce::Rectangle<int>,juce::Slider&,juce::Label&);
    void placeFader(juce::Rectangle<int>,juce::Slider&,juce::Label&);
    void updateOverlayVisibility();
    void applyPageFade();
    void advancePageFade(double);
    void updateLookaheadAvailability();
    void updateDetectorControls();
    void applyDetectorAppearance();
    void advanceDetectorAppearance(double);
    void layoutSidechainControls(juce::Rectangle<int>);
    void timerCallback() override;
    void stopAudition();
    void syncView();
    void setDisplayActive(bool);
    void suspendPages(); void restorePages();
    void beginTransaction();

    OpenCompressorAudioProcessor& p; CompressorLookAndFeel lnf; CompressorDisplay display; CompressorMeters meters;
    CompressorSidechainPreview sidechainPreview;
    juce::Slider input,thresholdKnob,ratio,knee,attack,hold,release,range,lookahead,mix,output,link,sideHighPass,sideLowPass;
    juce::Label inputL,thresholdL,ratioL,kneeL,attackL,holdL,releaseL,rangeL,lookaheadL,mixL,outputL,linkL,sideHighPassL,sideLowPassL;
    juce::ComboBox maxLookahead,detectorMode;
    juce::Slider rmsTime;
    juce::Label rmsTimeL,detectorModeL;
    juce::ToggleButton autoGain,autoRelease,smooth,kneeView,detailsView,sidechainView,externalSidechain,sidechainHistoryView,bypass,listen;
    juce::ToggleButton freeze,hoverHelp,trace;
    juce::TextButton undo{"<"},redo{">"},snapshotA{"A"},snapshotB{"B"},copy{"COPY A > B"};
    juce::Label scStatus;
    class Panel final : public juce::Component { public: void paint(juce::Graphics&) override; } advancedPanel;
    bool detailsOpen=false,sidechainOpen=false;
    PageFade pageFade;
    BypassFade rmsLayoutFade,rmsKnobFade,rmsTintFade;
    std::vector<std::unique_ptr<SA>> sliderAttachments; std::vector<std::unique_ptr<BA>> buttonAttachments; std::vector<std::unique_ptr<CA>> comboAttachments;
    std::unique_ptr<juce::ParameterAttachment> lookaheadAttachment;
    bool updatingLookahead=false,lookaheadDragging=false;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    bool pagesSuspended=false,referenceEditing=false,viewInitialized=false;
    bool displayActive=true;
    double restoreAfter=0;
    uint64_t viewRevision=0;
    int copyFeedback=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenCompressorAudioProcessorEditor)
};
