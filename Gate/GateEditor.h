#pragma once
#include "../Shared/EditableValueStyle.h"
#include "../Shared/BypassFade.h"
#include "../Shared/BadgeMotion.h"

#include <JuceHeader.h>
#include "GateProcessor.h"
#include "../Shared/PageFade.h"
#include "../Clipper/FramePacing.h"

class GateLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void fillTextEditorBackground(juce::Graphics&,int,int,juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&,int,int,juce::TextEditor&) override;
    GateLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
private:
    OpenEditableValueStyle valueStyle;
    juce::Colour valueAccent(juce::Component&);
};

// Display-only clock: published audio can arrive in bursts; viewport motion must not.
class GateHistoryClock
{
public:
    double advance (uint64_t sequence, uint64_t epoch, double nowMs, double rate, double delay);
    void reset() { position = -1; lastTime = -1; running = false; }
private:
    double position = -1, lastTime = -1, lastAudioTime = 0;
    uint64_t lastSequence = 0, lastEpoch = 0;
    bool running = false;
};

class GateDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit GateDisplay (OpenGateAudioProcessor&);
    ~GateDisplay() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;
    void setDisplayActive (bool);
    void setPanelMode (int mode) { if (panelMode == mode) return; panelMode = mode; repaint(); }
    void setBadgeGeometry(int mainTop,int pageTop)
    { badgeMainTop=mainTop;badgePageTop=pageTop;badgeGeometryReady=true;repaint(); }
    void setBadgePageVisible(bool visible,bool animate)
    { badgeMotion.setPageVisible(visible,juce::Time::getMillisecondCounterHiRes(),animate&&displayActive);repaint(); }
    void setSidechainVisible (bool visible);
    bool isSidechainVisible() const noexcept { return sidechainVisible; }
    void setSidechainFocus (bool focused);
    void setKneeVisible (bool visible);
    bool isKneeVisible() const noexcept { return showKnee; }
    juce::Path transferCurvePath() const;
    void setDetectorVisible (bool visible);
    bool isDetectorVisible() const noexcept { return showDetector; }
    void setFrozen (bool);
    bool isFrozen() const noexcept { return frozen; }
    bool suggestNoiseSettings (float& openingDb, float& hysteresisDb) const;
    juce::Rectangle<float> thresholdBadgeBounds() const;
    juce::Rectangle<float> hysteresisBadgeBounds() const;
    juce::String hysteresisBadgeText() const;
    std::function<void()> onReferenceEdit, onReferenceEditEnd;
    std::function<void()> onHistorySelectionChanged;
    std::function<void(double)> onAnimationFrame;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
private:
    friend struct GateDisplayLifecycleAudit;
    friend struct GateFramePacingAudit;
    friend struct GateSCPerformanceAudit;
    friend struct GateClosedHistoryAudit;
    void timerCallback() override;
    void vblankCallback (double timestampSeconds);
    void fallbackFrame (double timeMs);
    void presentationFrame (double timestampSeconds, double arrivalTimeMs);
    void animationFrame (double timeMs);
    void captureHistory (bool latestForFreeze = false, double presentationTimeMs = -1);
    void paintScene (juce::Graphics&);
    void rebuildHistory (float pixelScale);
    float thresholdY() const;
    float hysteresisY() const;
    float dbToY (float) const;
    bool hitThreshold (juce::Point<float>) const;
    bool hitHysteresis (juce::Point<float>) const;
    void setCloseBadgeVisible (bool);
    void setHistoryAppearance (BypassFade&, bool& state, bool enabled);
    BypassFade sidechainFade, detectorFade, focusFade, kneeFade;
    friend struct GateHistoryAppearanceAudit;
    OpenGateAudioProcessor& processor;
    juce::RangedAudioParameter* threshold = nullptr;
    // Message-thread reference state, shared by full and clipped repaints.
    float displayedThreshold = -12.0f, displayedHysteresis = 6.0f, displayedFlip = 0.0f;
    // Present the new return-threshold side only after the old one is invisible.
    PageFade hysteresisFlipFade;
    friend struct GateFlipFadeAudit;
    BypassFade closeBadgeFade;
    friend struct GateCloseBadgeFadeAudit;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 3> referenceAttachments;
    BadgeMotion badgeMotion;
    int badgeMainTop=10000,badgePageTop=10000;
    bool badgeGeometryReady=false;
    friend struct FrozenInspectionAudit;
    friend struct BadgeMotionAudit;
    friend struct DynamicsPageFadeAudit;
    BypassFade bypassFade;
    std::unique_ptr<juce::ParameterAttachment> bypassDisplayAttachment;
    friend struct BypassFadeAudit;
    friend struct PlotBoundaryAudit;
    juce::RangedAudioParameter* dragParameter = nullptr;
    bool dragging = false, sidechainFocus = false, sidechainVisible = false, showKnee = false;
    bool fineDrag = false;
    float dragY = 0.0f, dragDb = 0.0f;
    int panelMode = 0; // 0 = history, 1 = Details, 2 = Sidechain
    bool frozen = false, inspecting = false, showDetector = false, historyDirty = true;
    int inspected = -1, selectionAnchor = -1, validFrames = 0;
    uint64_t capturedSequence = 0;
    uint64_t capturedEpoch = 0;
    double capturedFrameRate = 60;
    GateHistoryClock clock;
    juce::VBlankAttachment vblank;
    ClipperFramePacer framePacer; // Shared scheduling policy; do not fork Clipper's refresh selection.
    double lastVblankArrival = -1, vblankOffset = 0, lastVblankTimestamp = -1;
    float scrollFraction = 0;
    bool hasPainted = false;
    bool displayActive = false;
    uint64_t captureState = 0, captureStartSequence = 0;
    uint64_t repaintSequence = 0;
    float repaintFraction = -1;
    std::array<float, 7> repaintSettings {};
    std::array<OpenGateAudioProcessor::HistoryFrame, OpenGateAudioProcessor::historySize> frames {};
    juce::Image historyImage;
    float rasterScale = 0, rasterPhase = 0;
    uint64_t rasterSequence = UINT64_MAX, rasterEpoch = UINT64_MAX;
    int rasterFrames = 0;
    std::array<float, 3> rasterAppearance {};
};

class GateMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GateMeter (OpenGateAudioProcessor& p) : processor (p)
    {
        setOpaque (true);
        setTooltip ("I/O: signal level. A: current attenuation, 0-80 dB.");
    }
    void paint (juce::Graphics&) override;
    void refresh()
    {
        const std::array<float, 3> values { processor.inputLevel(), processor.outputLevel(), processor.attenuationLevel() };
        if (isShowing() && values != lastValues) { lastValues = values; repaint(); }
    }
private:
    std::array<float, 3> lastValues { -1, -1, -1 };
    OpenGateAudioProcessor& processor;
};

class GateAdvancedPanel final : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};

class GateSidechainResponse final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GateSidechainResponse (OpenGateAudioProcessor&);
    ~GateSidechainResponse() override { endDrag(); processor.setSpectrumEnabled (false); }
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
    void setDisplayActive (bool enabled) { if (displayActive != enabled) { displayActive = enabled; visibilityChanged(); } }
    void endDrag();
private:
    juce::Rectangle<float> plotBounds() const;
    int hitHandle (juce::Point<float>, bool preferLowPass) const;
    OpenGateAudioProcessor& processor;
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
    bool draggingHighPass = true, fine = false;
    bool displayActive = true;
    double originX = 0, originLog = 0;
};

class OpenGateAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OpenGateAudioProcessorEditor (OpenGateAudioProcessor&);
    ~OpenGateAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusLost (FocusChangeType) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void referenceTextEdit (bool active);
private:
    friend struct FrozenInspectionAudit;
    friend struct BadgeMotionAudit;
    friend struct GateClosedHistoryAudit;
    friend struct GatePageRetentionAudit;
    friend struct DynamicsPageFadeAudit;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    void setupKnob (juce::Slider&, juce::Label&, const juce::String&, const juce::String&, double, bool detail = false);
    void setupButton (juce::ToggleButton&, const juce::String&);
    void updateDetailsVisibility();
    void applyPageFade();
    void advancePageFade(double);
    void restoreEditorPage();
    void updateLookaheadAvailability();
    void syncHistoryPreferences();
    void timerCallback() override;
    void setDisplayActive (bool);
    void stopAudition();
    void hideAdvancedPages();
    void restoreAdvancedPages();
    void beginEditTransaction();
    void learnNoise();
    std::array<float, 9> detectorSettings() const;

    OpenGateAudioProcessor& gateProcessor;
    GateLookAndFeel lookAndFeel;
    GateDisplay display;
    GateMeter meter;
    GateAdvancedPanel advancedPanel;
    GateSidechainResponse sidechainResponse;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    juce::Slider threshold, hysteresis, ratio, range, attack, release;
    juce::Slider knee, hold, lookahead, sideHighPass, sideLowPass;
    juce::Slider input, output, mix, link;
    juce::Label thresholdLabel, hysteresisLabel, ratioLabel, rangeLabel, attackLabel, releaseLabel;
    juce::Label kneeLabel, holdLabel, lookaheadLabel, sideHighPassLabel, sideLowPassLabel;
    juce::Label inputLabel, outputLabel, mixLabel, linkLabel;
    juce::ToggleButton details, scView, externalSidechain, audition, preOpen, flip, kneeView, bypass;
    juce::ToggleButton freeze, detectorView, showSidechain, hoverHelp;
    juce::TextButton undo { "<" }, redo { ">" }, snapshotA { "A" }, snapshotB { "B" }, copy { "COPY A > B" };
    juce::TextButton removed { "HOLD REMOVED" }, learn { "LEARN NOISE" }, apply { "APPLY" };
    juce::Label scStatus, learnStatus;
    juce::ComboBox maxLookahead, detectorMode;
    bool detailsOpen = false, sidechainOpen = false;
    PageFade pageFade;
    bool displayActive = true, viewInitialized = false;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> maximumAttachment;
    std::unique_ptr<juce::ParameterAttachment> detectorModeAttachment;
    std::unique_ptr<juce::ParameterAttachment> lookaheadAttachment;
    bool updatingLookahead = false, lookaheadDragging = false;
    float storedLookahead = 1.0f;
    bool pagesSuspended = false, referenceEditing = false;
    double editUntil = 0;
    float suggestedThreshold = -12, suggestedHysteresis = 6;
    bool suggestionReady = false;
    int copyFeedbackTicks = 0;
    std::array<float, 9> learnedDetectorSettings {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenGateAudioProcessorEditor)
};
