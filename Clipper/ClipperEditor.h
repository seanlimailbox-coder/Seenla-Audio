#pragma once
#include "../Shared/EditableValueStyle.h"
#include "../Shared/BypassFade.h"
#include "../Shared/GRMaximumReadout.h"
#include "../Shared/BadgeMotion.h"

#include <JuceHeader.h>
#include "ClipperProcessor.h"
#include "FramePacing.h"

class ClipperControlCache final : public juce::CachedComponentImage
{
public:
    explicit ClipperControlCache (juce::Component& c) : owner (c) {}
    void paint (juce::Graphics&) override;
    bool invalidateAll() override { dirty = true; return true; }
    bool invalidate (const juce::Rectangle<int>&) override { return invalidateAll(); }
    void releaseResources() override { image = {}; dirty = true; }
private:
    juce::Component& owner;
    juce::Image image;
    bool dirty = true;
    float cachedScale = 0;
};

class ClipperLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void fillTextEditorBackground(juce::Graphics&,int,int,juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&,int,int,juce::TextEditor&) override;
    ClipperLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (10.0f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (9.5f, juce::Font::bold)); }
private:
    OpenEditableValueStyle valueStyle;
    juce::Colour valueAccent(juce::Component&);
};

class ClipperBandSlider final : public juce::Slider
{
};

class ClipperAdvancedPanel final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ClipperAdvancedPanel (OpenClipperAudioProcessor&);
    ~ClipperAdvancedPanel() override;
    void setBandsView (bool);
    bool isBandsView() const noexcept { return bandsView; }
    void stopBandListen() { clearBandListen(); }
    void setDisplayActive (bool);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;
private:
    friend struct ClipperEditorStateAudit;
    friend struct ClipperPageFadeAudit;
    void timerCallback() override;
    void clearBandListen();
    void updateLinkBinding();
    void selectBand (int);
    void updateSpectrum();
    juce::Rectangle<float> graphBounds() const;
    juce::Rectangle<float> bandHeaderBounds (int band) const;
    float frequencyX (float) const;
    OpenClipperAudioProcessor& processor;
    bool bandsView = false;
    bool displayActive = true;
    bool linkUsesMidSide = false;
    juce::String bandSoftParameter;
    juce::RangedAudioParameter* draggedSplit = nullptr;
    std::array<ClipperBandSlider, 5> knobs;
    std::array<juce::Label, 5> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> knobAttachments;
    juce::ComboBox liveQuality, renderQuality, curve, channels;
    juce::Label liveLabel, renderLabel, curveLabel, channelsLabel;
    juce::Slider msBias;
    juce::Label msBiasLabel;
    juce::Slider release, stereoLink;
    juce::Label releaseLabel, linkLabel, stageReduction;
    juce::ToggleButton gainLink, truePeak, dcFilter;
    std::array<juce::TextButton, 3> bandSolo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment, linkAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gainLinkAttachment, truePeakAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dcFilterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> msBiasAttachment;
    juce::ToggleButton enableBands, delta, snapshotA, snapshotB;
    juce::TextButton copy { "COPY A > B" }, match { "MATCH OUTPUT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bandsAttachment;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, 4> choiceAttachments;
    int feedbackTicks = 0, copyFeedbackTicks = 0;
    int selectedBand = 0;
    ClipperBandSlider bandSoft, bandMix, bandTrim;
    juce::Label bandSoftLabel, bandMixLabel, bandTrimLabel;
    juce::ComboBox bandCurve;
    juce::ComboBox crossover;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> crossoverAttachment;
    juce::ToggleButton bandEnabled;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> bandControlAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bandCurveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bandEnabledAttachment;
    juce::dsp::FFT spectrumFFT { 12 };
    std::array<float, 8192> fftData {};
    std::array<float, 2048> spectrumLevels {};
    std::vector<double> spectrumBins;
    double spectrumBinRate = 0;
    float spectrumBinWidth = 0;
};

class ThresholdScope final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ThresholdScope (OpenClipperAudioProcessor&);
    ~ThresholdScope() override;
    void setControlsTop (int y) { controlsTop = y; }
    void setBadgeGeometry(int mainTop,int pageTop)
    { badgeMainTop=mainTop;badgePageTop=pageTop;badgeGeometryReady=true;repaint(); }
    void setBadgePageVisible(bool visible,bool animate)
    { badgeMotion.setPageVisible(visible,juce::Time::getMillisecondCounterHiRes(),animate&&displayActive);repaint(); }
    std::function<void()> onThresholdEdit;
    std::function<void()> onThresholdEditEnd;
    std::function<void(double)> onAnimationFrame;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void resized() override;
    void setMetersVisible (bool visible);
    bool areMetersVisible() const noexcept { return metersVisible; }
    void setDisplayActive (bool);
    void setInspectionHidden (bool hidden) { inspectionHidden = hidden; }
    void setFrozen (bool);
    bool isFrozen() const noexcept { return frozen; }
    int getInspectedFrame() const noexcept { return inspected; }
    void restoreViewState();

private:
    friend struct ClipperHistoryAudit;
    friend struct ClipperPageFadeAudit;
    friend struct ClipperCpuAudit;
    friend struct ClipperEditorStateAudit;
    void timerCallback() override;
    void vblankCallback (double timestampSeconds);
    void animationFrame (double timeMs);
    juce::VBlankAttachment vblank;
    ClipperFramePacer framePacer;
    double lastVblankArrival = -1, vblankOffset = 0, lastVblankTimestamp = -1;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 2> sceneAttachments;
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
    friend struct ClipperHistoryMeterAudit;
    friend struct GRStripAudit;
    BypassFade metersFade, auditionFade, limitFade;
    float displayedLimit = 0;
    void updateLimitAppearance(float limitDb);
    juce::Rectangle<float> limitBadgeBounds() const;
    juce::String auditionText;
    std::array<float, 8> previousReadout {};
    void captureHistory();
    double historyDelayFrames() const;
    void advanceHistory (double now);
    void renderHistory (float scale);
    float thresholdY() const;
    juce::Rectangle<float> thresholdBadgeBounds() const;
    bool hitThreshold (juce::Point<float>) const;
    float topReadoutClearance() const
    { return 34.f*juce::jmax(metersFade.amount(),auditionFade.amount()); }
    juce::Rectangle<float> statisticsBounds() const
    {
        return { 22.0f, -24.f+topReadoutClearance(), 296.0f, 24.0f };
    }
    OpenClipperAudioProcessor& audioProcessor;
    juce::RangedAudioParameter* thresholdParameter = nullptr;
    // Match Open Compressor: one message-thread value for the whole plot,
    // invalidated immediately by threshold changes, not just the history timer.
    float displayedThreshold = 0;
    std::unique_ptr<juce::ParameterAttachment> thresholdDisplayAttachment;
    bool dragging = false;
    int controlsTop = 10000;
    float dragY = 0.0f, dragDb = 0.0f;
    bool fineDrag = false;
    bool inspecting = false;
    bool frozen = false, metersVisible = false;
    bool inspectionHidden = false;
    bool displayActive = true;
    uint64_t captureState = 0;
    static constexpr int historyPoints = 800;
    int inspected = -1;
    std::array<float, 1024> historyOutput {}, historyReduction {};
    std::array<bool, 1024> historySmooth {};
    uint64_t lastSequence = 0;
    double lastFrameTime = 0;
    double lastAudioFrameTime = 0;
    double historyPosition = -1;
    uint64_t lastAudioSequence = 0;
    bool historyClockRunning = false;
    float scrollFraction = 0;
    float nextOutput = 0, nextReduction = 0;
    bool nextSmooth = false;
    juce::Path cachedOutput, cachedHard, cachedSmooth;
    juce::Image cachedHistory;
    float historyRenderScale = 0, historyRasterPhase = 0;
    uint64_t historyRasterSequence = UINT64_MAX;
    bool historyRenderDirty = true;
    void rebuildHistoryPaths();
    void rememberFrozenHistory();
    uint64_t restoredViewRevision = 0;
};

class GainReductionMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GainReductionMeter (OpenClipperAudioProcessor& p) : audioProcessor (p)
    { setMouseCursor (juce::MouseCursor::PointingHandCursor);
      setComponentID("gainReductionMeter");
      setTooltip ("Maximum GR above the stereo strip. Orange = Hard, blue = Smooth. Click to reset held maxima."); refresh(); }
    void paint (juce::Graphics&) override;
    void refresh();
    void settleAppearance(){maximum.settle();modeFade.settle();bypassFade.settle();}
    void mouseDown (const juce::MouseEvent&) override
    {
        audioProcessor.resetStatistics();
        refresh();
    }
private:
    friend struct ClipperHistoryMeterAudit;
    friend struct GRStripAudit;
    std::array<float, 7> previous {};
    GRMaximumReadout maximum;
    BypassFade modeFade,bypassFade;
    OpenClipperAudioProcessor& audioProcessor;
};

class OpenClipperAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
    friend struct BadgeLimitAudit;
    friend struct GRStripAudit;
public:
    explicit OpenClipperAudioProcessorEditor (OpenClipperAudioProcessor&);
    ~OpenClipperAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

private:
    friend struct FrozenInspectionAudit;
    friend struct BadgeMotionAudit;
    friend struct ClipperEditorStateAudit;
    void timerCallback() override;
    void hideAdvancedPages();
    friend struct ClipperPageFadeAudit;
    friend struct ClipperCpuAudit;
    void restoreAdvancedPages();
    void presentAdvancedPage (int page, bool animate);
    void showAdvancedPage (int page);
    void advancePageFade (double timeMs);
    void updatePageMouseAccess();
    int requestedAdvancedPage = 0, presentedAdvancedPage = 0;
    bool pageFading = false;
    float pageFadeFrom = 0, pageFadeTo = 0;
    static constexpr double pageFadeTimeMs = 160;
    double pageFadeStart = 0, pageFadeDuration = pageFadeTimeMs;
    void restoreViewState();
    void syncHoverHelp();
    void setDisplayActive (bool);
    bool displayActive = true;
    uint64_t restoredViewRevision = 0;
    bool viewInitialized = false;
    void mouseDown (const juce::MouseEvent& e) override
    { audioProcessor.parameters.copyState(); audioProcessor.undoManager.beginNewTransaction();
      if (e.eventComponent == &threshold || threshold.isParentOf (e.eventComponent))
        { thresholdPointerEditing = true; hideAdvancedPages(); } }
    void mouseUp (const juce::MouseEvent& e) override
    { if (e.eventComponent == &threshold || threshold.isParentOf (e.eventComponent))
        { thresholdPointerEditing = false; restoreAdvancedPages(); } }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails&) override
    { if (e.eventComponent == &threshold || threshold.isParentOf (e.eventComponent))
        { thresholdEditUntil = juce::Time::getMillisecondCounterHiRes() + 350; hideAdvancedPages(); } }
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    void setupKnob (juce::Slider&, juce::Label&, const juce::String&, const juce::String&);

    OpenClipperAudioProcessor& audioProcessor;
    ClipperLookAndFeel lookAndFeel;
    ThresholdScope scope;
    GainReductionMeter meter;
    ClipperAdvancedPanel advanced;
    juce::Slider input, threshold, softness, mix, output, ceiling;
    juce::Label inputLabel, thresholdLabel, softnessLabel, mixLabel, outputLabel, ceilingLabel;
    juce::ToggleButton mode, bypass, details, bands, freeze, meters, hoverHelp;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    juce::TextButton undo { "<" }, redo { ">" };
    std::unique_ptr<SliderAttachment> inputAttachment, thresholdAttachment, softnessAttachment, mixAttachment, outputAttachment, ceilingAttachment;
    std::unique_ptr<ButtonAttachment> modeAttachment, bypassAttachment;
    float previousInputDb = 0;
    std::unique_ptr<juce::ParameterAttachment> linkedInputAttachment;
    bool inputGesture = false;
    bool softnessUsesKnee = false;
    void updateKneeBinding (bool useKnee);
    bool thresholdPointerEditing = false, thresholdDragging = false, thresholdTextEditing = false;
    bool thresholdScopeEditing = false, pagesSuspended = false;
    double thresholdEditUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenClipperAudioProcessorEditor)
};
