#include "ClipperEditor.h"
#include "../Shared/PlotStroke.h"
#include "../Shared/FrozenInspection.h"
#include <cstdlib>

namespace ClipTheme
{
    const auto background = juce::Colour::fromRGB (12, 14, 17);
    const auto panel = juce::Colour::fromRGB (21, 24, 29);
    const auto edge = juce::Colour::fromRGB (43, 48, 57);
    const auto text = juce::Colour::fromRGB (232, 236, 241);
    const auto dim = juce::Colour::fromRGB (130, 139, 151);
    const auto purple = juce::Colour::fromRGB (139, 101, 238);
    const auto purpleDark = juce::Colour::fromRGB (65, 49, 101);
    const auto dialSurround = juce::Colour::fromRGB (36, 29, 50);
    const auto dialTrack = juce::Colour::fromRGB (61, 49, 78);
    const auto dialFace = juce::Colour::fromRGB (17, 15, 25);
    const auto red = juce::Colour::fromRGB (247, 74, 92);
    const auto orange = juce::Colour::fromRGB (255, 160, 72);
    const auto cyan = juce::Colour::fromRGB (73, 216, 205);
    const auto smoothBlue = juce::Colour::fromRGB (79, 181, 235);
    const auto cubicMint = juce::Colour::fromRGB (103, 206, 166);
    const std::array<juce::Colour, 3> bandTints { juce::Colour::fromRGB (183, 132, 103),
        juce::Colour::fromRGB (101, 133, 189), juce::Colour::fromRGB (102, 158, 164) };
}

namespace
{
class BandValueLabel final : public juce::Label
{
public:
    explicit BandValueLabel (juce::Slider& owner) : slider (owner) {}
    void cancelInteraction() { armed = false; gesture.reset(); hideEditor (true); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        gesture.reset();
        armed = isEnabled() && e.mods.isLeftButtonDown() && ! isBeingEdited();
        originY = e.position.y;
        start = slider.valueToProportionOfLength (slider.getValue());
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! armed) return;
        const auto distance = originY - e.position.y;
        if (! gesture && std::abs (distance) < 3.0f) return;
        if (! gesture) gesture = std::make_unique<juce::Slider::ScopedDragNotification> (slider);
        const double sensitivity = e.mods.isShiftDown() ? 0.1 : 1.0;
        slider.setValue (slider.proportionOfLengthToValue (juce::jlimit (0.0, 1.0,
            start + distance * sensitivity / 200.0)), juce::sendNotificationSync);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool dragged = gesture != nullptr;
        gesture.reset();
        armed = false;
        if (! dragged && isEnabled() && isEditableOnSingleClick()
            && getLocalBounds().contains (e.getPosition())
            && ! e.mouseWasDraggedSinceMouseDown() && ! e.mods.isPopupMenu())
            showEditor();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
private:
    juce::Slider& slider;
    std::unique_ptr<juce::Slider::ScopedDragNotification> gesture;
    float originY = 0;
    double start = 0;
    bool armed = false;
};

void finishSliderInteraction (juce::Slider& slider)
{
    // End gestures while parameter attachments still exist. JUCE destroys
    // attachments before their sliders, which otherwise loses drag-end events.
    for (auto* child : slider.getChildren())
        if (auto* value = dynamic_cast<BandValueLabel*> (child)) value->cancelInteraction();
        else if (auto* label = dynamic_cast<juce::Label*> (child)) label->hideEditor (true);
    const auto time = juce::Time::getCurrentTime();
    const juce::Point<float> point {};
    slider.mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys {}, 1, 0, 0, 0, 0, &slider, &slider, time, point, time, 1, false));
}

void cacheControls (juce::Component& parent)
{
    // Native-scale ARGB caches: repaint/resize/colour/parameter changes still
    // invalidate normally. The moving history no longer rerasterizes dials.
    for (auto* child : parent.getChildren())
        if (dynamic_cast<juce::Slider*> (child) != nullptr || dynamic_cast<juce::Label*> (child) != nullptr
            || dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<juce::ComboBox*> (child) != nullptr)
            child->setCachedComponentImage (new ClipperControlCache (*child));
}

void setSliderHelp (juce::Slider& slider, const juce::String& text, juce::Label* title = nullptr)
{
    slider.setTooltip (text);
    if (title != nullptr) title->setTooltip (text);
    // Numeric value labels are separate mouse targets in JUCE.
    for (auto* child : slider.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child)) label->setTooltip (text);
}
}

void ClipperControlCache::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    // Fractional scale changes the global glyph raster phase. Don't resample
    // local-origin cached text: retain the direct renderer at those scales.
    if (! juce::exactlyEqual (scale, std::round (scale)))
    { owner.paintEntireComponent (g, true); return; }
    const int width = juce::jmax (1, juce::roundToInt (owner.getWidth()*scale));
    const int height = juce::jmax (1, juce::roundToInt (owner.getHeight()*scale));
    if (dirty || image.getWidth() != width || image.getHeight() != height || ! juce::exactlyEqual (cachedScale, scale))
    {
        image = juce::Image (juce::Image::ARGB, width, height, true);
        juce::Graphics cached (image);
        cached.addTransform (juce::AffineTransform::scale (scale));
        owner.paintEntireComponent (cached, true);
        cachedScale = scale; dirty = false;
    }
    g.setColour (juce::Colours::black.withAlpha (owner.getAlpha()));
    g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale), false);
}

ClipperLookAndFeel::ClipperLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, ClipTheme::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonColourId, ClipTheme::panel);
    setColour (juce::TextButton::buttonOnColourId, ClipTheme::purpleDark);
    setColour (juce::TextButton::textColourOffId, ClipTheme::dim);
    setColour (juce::TextButton::textColourOnId, ClipTheme::text);
    setColour (juce::ComboBox::backgroundColourId, ClipTheme::dialFace);
    setColour (juce::ComboBox::outlineColourId, ClipTheme::edge);
    setColour (juce::ComboBox::textColourId, ClipTheme::text);
    setColour (juce::ComboBox::arrowColourId, ClipTheme::cyan);
    setColour (juce::PopupMenu::backgroundColourId, ClipTheme::panel);
    setColour (juce::PopupMenu::textColourId, ClipTheme::text);
    setColour (juce::TooltipWindow::backgroundColourId, ClipTheme::panel);
    setColour (juce::TooltipWindow::outlineColourId, ClipTheme::edge);
    setColour (juce::TooltipWindow::textColourId, ClipTheme::text);
}

void ClipperLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float position, float start, float end, juce::Slider& slider)
{
    juce::Graphics::ScopedSaveState state (g);
    const float opacity = slider.isEnabled() ? 1.0f : 0.28f;
    const bool compact = slider.getComponentID() == "msBias" || slider.getComponentID() == "release"
        || slider.getComponentID() == "stereoLink" || slider.getComponentID().startsWith ("band");
    const float radius = juce::jmin (width, height) * (compact ? 0.32f : 0.34f);
    const float surround = compact ? 6.0f : 8.0f;
    const juce::Point<float> centre { x + width * 0.5f, y + height * (compact ? 0.5f : 0.45f) };
    const float angle = start + position * (end - start);
    g.setColour (ClipTheme::dialSurround.withAlpha (0.84f * opacity));
    g.fillEllipse (centre.x - radius - surround, centre.y - radius - surround,
                   (radius + surround) * 2.0f, (radius + surround) * 2.0f);
    juce::Path base, active;
    base.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    active.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
    g.setColour (ClipTheme::dialTrack.withAlpha (opacity));
    g.strokePath (base, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour ((slider.getName() == "THRESHOLD" ? ClipTheme::cyan : ClipTheme::purple).withAlpha (opacity));
    g.strokePath (active, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (ClipTheme::dialFace.withAlpha (opacity));
    g.fillEllipse (centre.x - radius + 6.0f, centre.y - radius + 6.0f, (radius - 6.0f) * 2.0f, (radius - 6.0f) * 2.0f);
    const auto tip = centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * radius * 0.58f;
    g.setColour (ClipTheme::text.withAlpha (opacity));
    g.drawLine ({ centre, tip }, 2.0f);
}

void ClipperLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool hover, bool down)
{
    auto area = button.getLocalBounds().toFloat().reduced (1.0f);
    auto fill = button.getToggleState() ? ClipTheme::purpleDark : ClipTheme::panel;
    const bool activeHard = button.getButtonText() == "HARD";
    const bool activeBypass = button.getButtonText() == "BYPASS" && button.getToggleState();
    const bool activeSmooth = button.getButtonText() == "SMOOTH" && button.getToggleState();
    const bool bandsEnabled = button.getButtonText() == "BANDS"
        && static_cast<bool> (button.getProperties().getWithDefault ("processingEnabled", false));
    const bool auditioning = button.getButtonText() == "HOLD DELTA" && down;
    if (activeBypass) fill = ClipTheme::red;
    if (activeSmooth) fill = ClipTheme::smoothBlue;
    if (activeHard) fill = ClipTheme::orange.withAlpha (0.88f);
    if (bandsEnabled && ! button.getToggleState()) fill = ClipTheme::purpleDark.withAlpha (0.45f);
    if (auditioning) fill = ClipTheme::cyan;
    if (button.getComponentID() == "bandEnabled" && button.getToggleState())
        fill = button.findColour (juce::TextButton::buttonOnColourId).darker (0.55f);
    const float opacity = button.isEnabled() ? 1.0f : 0.35f;
    if (hover && button.isEnabled() && static_cast<bool> (button.getProperties().getWithDefault ("topBar", false)))
        fill = fill.brighter (0.08f); // same subtle hover lift as Open Limiter
    if (down) fill = fill.darker (0.08f);
    g.setColour (fill.withMultipliedAlpha (opacity));
    g.fillRoundedRectangle (area, 6.0f);
    if (! static_cast<bool> (button.getProperties().getWithDefault ("topBar", false)))
    {
        g.setColour (ClipTheme::edge.withMultipliedAlpha (opacity));
        g.drawRoundedRectangle (area, 6.0f, 1.0f);
    }
    g.setColour ((activeBypass || activeSmooth || activeHard || auditioning ? ClipTheme::background
        : button.getToggleState() ? ClipTheme::text
        : bandsEnabled ? ClipTheme::text.withAlpha (0.78f) : ClipTheme::dim).withMultipliedAlpha (opacity));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void ClipperLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool hover, bool down)
{
    const auto area = button.getLocalBounds().toFloat().reduced (1);
    if (button.getComponentID().startsWith ("solo"))
    {
        if (button.getToggleState() || down)
        {
            g.setColour (ClipTheme::cyan.withAlpha (button.isEnabled() ? .13f : .04f));
            g.fillRoundedRectangle (area, 4);
        }
        return;
    }
    auto fill = button.getToggleState() ? ClipTheme::purpleDark : ClipTheme::panel;
    if (hover && button.isEnabled() && static_cast<bool> (button.getProperties().getWithDefault ("topBar", false)))
        fill = fill.brighter (0.08f);
    g.setColour ((down ? fill.darker (.08f) : fill).withMultipliedAlpha (button.isEnabled() ? 1.0f : .5f));
    g.fillRoundedRectangle (area, 6);
    if (! static_cast<bool> (button.getProperties().getWithDefault ("topBar", false)))
    {
        g.setColour (ClipTheme::edge.withMultipliedAlpha (button.isEnabled() ? 1.0f : .5f));
        g.drawRoundedRectangle (area, 6, 1);
    }
}

void ClipperLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
    float position, float, float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    const float cy = y + h*.5f, alpha = slider.isEnabled() ? 1.0f : .28f;
    g.setColour (ClipTheme::dialTrack.withAlpha (alpha)); g.fillRoundedRectangle ((float) x, cy-1, (float) w, 2, 1);
    g.setColour (slider.findColour (juce::Slider::trackColourId).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle ((float) x, cy-1, juce::jmax (0.0f, position-x), 2, 1);
    g.fillEllipse (position-3, cy-3, 6, 6);
}

juce::Label* ClipperLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = dynamic_cast<ClipperBandSlider*> (&slider) != nullptr
        ? static_cast<juce::Label*> (new BandValueLabel (slider))
        : juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setJustificationType (juce::Justification::centred);
    label->setKeyboardType (juce::TextInputTarget::decimalKeyboard);
    label->setColour (juce::Label::textColourId, slider.findColour (juce::Slider::textBoxTextColourId));
    label->setColour (juce::Label::backgroundColourId, slider.findColour (juce::Slider::textBoxBackgroundColourId));
    label->setColour (juce::Label::outlineColourId, slider.findColour (juce::Slider::textBoxOutlineColourId));
    label->setColour (juce::TextEditor::textColourId, slider.findColour (juce::Slider::textBoxTextColourId));
    label->setColour (juce::TextEditor::backgroundColourId, slider.findColour (juce::Slider::textBoxBackgroundColourId));
    label->setColour (juce::TextEditor::outlineColourId, slider.findColour (juce::Slider::textBoxOutlineColourId));
    label->setColour (juce::TextEditor::highlightColourId, slider.findColour (juce::Slider::textBoxHighlightColourId));
    if (slider.getComponentID() == "msBias" || slider.getComponentID() == "release"
        || slider.getComponentID() == "stereoLink" || slider.getComponentID().startsWith ("band")) label->setFont (juce::FontOptions (9.5f));
    return label;
}

void ClipperLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool hover, bool down)
{
    if (! button.getComponentID().startsWith ("solo"))
    {
        juce::LookAndFeel_V4::drawButtonText (g, button, hover, down);
        return;
    }
    const auto centre = button.getLocalBounds().toFloat().getCentre();
    g.setColour ((button.getToggleState() ? ClipTheme::cyan : ClipTheme::dim)
        .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.35f));
    juce::Path headphones;
    headphones.addCentredArc (centre.x, centre.y, 5.0f, 5.0f, 0,
        -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.strokePath (headphones, juce::PathStrokeType (1.4f));
    g.fillRoundedRectangle (centre.x - 5.7f, centre.y - 0.5f, 2.8f, 5.0f, 1.0f);
    g.fillRoundedRectangle (centre.x + 2.9f, centre.y - 0.5f, 2.8f, 5.0f, 1.0f);
}

juce::Font ClipperLookAndFeel::getLabelFont (juce::Label& label)
{
    if (auto* slider = dynamic_cast<juce::Slider*> (label.getParentComponent()))
        if (slider->getComponentID() == "msBias" || slider->getComponentID() == "release"
            || slider->getComponentID() == "stereoLink" || slider->getComponentID().startsWith ("band")) return juce::Font (juce::FontOptions (9.5f));
    return label.getFont();
}

void ClipperLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    valueStyle.paintLabel(g,label,ClipTheme::background,valueAccent(label));
    if (label.isBeingEdited())
        return;

    g.setColour (label.findColour (juce::Label::textColourId)
                       .withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    g.setFont (getLabelFont (label));
    const auto textArea = label.getBorderSize().subtractedFrom (label.getLocalBounds());
    g.drawFittedText (label.getText(), textArea, label.getJustificationType(), 1,
                      label.getMinimumHorizontalScale());
}

ThresholdScope::ThresholdScope (OpenClipperAudioProcessor& p) : audioProcessor (p)
{
    limitFade.setBypassed(true,0,false);
    auditionFade.setBypassed(true,0,false);
    thresholdParameter = audioProcessor.parameters.getParameter ("threshold");
    thresholdDisplayAttachment = std::make_unique<juce::ParameterAttachment> (*thresholdParameter, [this] (float value)
    {
        displayedThreshold = value;
        updateLimitAppearance(audioProcessor.parameters.getRawParameterValue("ceiling")->load());
        repaint();
    });
    thresholdDisplayAttachment->sendInitialUpdate();
    const std::array<const char*, 2> sceneIds { "ceiling", "smoothMode" };
    for (size_t i=0; i<sceneIds.size(); ++i)
        sceneAttachments[i] = std::make_unique<juce::ParameterAttachment> (*audioProcessor.parameters.getParameter(sceneIds[i]),
            [this,i] (float value) { if(i==0)updateLimitAppearance(value);repaint(); });
    bypassDisplayAttachment=std::make_unique<juce::ParameterAttachment>(*audioProcessor.parameters.getParameter("bypass"),[this](float value)
    {
        bypassFade.setBypassed(value>.5f,juce::Time::getMillisecondCounterHiRes(),displayActive);
        repaint();
    });
    bypassDisplayAttachment->sendInitialUpdate();
    setComponentID ("historyScope");
    audioProcessor.editorState.frozen.store (false);
    audioProcessor.setFrozenHistory ({});
    captureState = audioProcessor.startHistoryCapture();
    captureHistory();
    restoreViewState();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    vblank = juce::VBlankAttachment (this, [this] (double timestamp) { vblankCallback (timestamp); });
    // Fallback only: a native peer normally drives animation via VBlank.
    startTimerHz (60);
}

ThresholdScope::~ThresholdScope()
{
    setDisplayActive (false);
    if (dragging) thresholdParameter->endChangeGesture();
}

void ThresholdScope::setDisplayActive (bool enabled)
{
    if (displayActive == enabled) return;
    displayActive = enabled;
    bypassFade.settle();badgeMotion.settle();metersFade.settle();auditionFade.settle();limitFade.settle();
    stopTimer(); vblank = {};
    frozen = false; inspecting = false; inspected = -1;
    audioProcessor.editorState.frozen.store (false);
    audioProcessor.setFrozenHistory ({});
    audioProcessor.setMeteringEnabled (enabled && metersVisible);
    if (! enabled)
    {
        audioProcessor.stopHistoryCapture();
        cachedHistory = {};
        if (dragging)
        {
            dragging = false;
            thresholdParameter->endChangeGesture();
            if (onThresholdEditEnd) onThresholdEditEnd();
        }
        return;
    }
    captureState = audioProcessor.startHistoryCapture();
    historyOutput.fill (0); historyReduction.fill (0); historySmooth.fill (false);
    nextOutput = nextReduction = scrollFraction = 0; nextSmooth = false;
    historyPosition = -1; historyClockRunning = false;
    lastFrameTime = juce::Time::getMillisecondCounterHiRes();
    lastVblankArrival = lastVblankTimestamp = -1; framePacer.reset();
    captureHistory();
    vblank = juce::VBlankAttachment (this, [this] (double timestamp) { vblankCallback (timestamp); });
    startTimerHz (60); repaint();
}

void ThresholdScope::setMetersVisible(bool visible)
{
    metersVisible=visible;audioProcessor.editorState.meters.store(visible);
    audioProcessor.setMeteringEnabled(visible&&displayActive);
    metersFade.setBypassed(!visible,juce::Time::getMillisecondCounterHiRes(),displayActive);repaint();
}

void ThresholdScope::rememberFrozenHistory()
{
    if (! frozen || restoredViewRevision != audioProcessor.editorState.revision.load()) return;
    OpenClipperAudioProcessor::FrozenHistory history;
    history.valid = true;
    history.output = historyOutput; history.reduction = historyReduction; history.smooth = historySmooth;
    history.nextOutput = nextOutput; history.nextReduction = nextReduction; history.nextSmooth = nextSmooth;
    history.fraction = juce::jlimit (0.0f, std::nextafter (1.0f, 0.0f), scrollFraction);
    history.inspected = inspected; history.sequence = lastSequence;
    audioProcessor.setFrozenHistory (history);
}

void ThresholdScope::restoreViewState()
{
    restoredViewRevision = audioProcessor.editorState.revision.load();
    setMetersVisible (audioProcessor.editorState.meters.load());
    frozen = audioProcessor.editorState.frozen.load();
    inspecting = false; inspected = -1;
    const auto saved = audioProcessor.getFrozenHistory();
    if (frozen && saved.valid)
    {
        historyOutput = saved.output; historyReduction = saved.reduction; historySmooth = saved.smooth;
        nextOutput = saved.nextOutput; nextReduction = saved.nextReduction; nextSmooth = saved.nextSmooth;
        scrollFraction = saved.fraction; inspected = saved.inspected; lastSequence = saved.sequence;
        historyPosition = (double) lastSequence + scrollFraction;
        rebuildHistoryPaths();
    }
    else
    {
        historyPosition = -1; historyClockRunning = false;
        lastFrameTime = juce::Time::getMillisecondCounterHiRes();
        captureHistory();
        scrollFraction = (float) (historyPosition-std::floor (historyPosition));
        rememberFrozenHistory();
    }
    repaint();
}

void ThresholdScope::resized()
{
    rebuildHistoryPaths();
}

void ThresholdScope::setFrozen (bool enabled)
{
    frozen = enabled; inspecting = false; inspected = -1;
    audioProcessor.editorState.frozen.store (enabled);
    if (! frozen)
    {
        historyPosition = -1; historyClockRunning = false;
        lastFrameTime = juce::Time::getMillisecondCounterHiRes();
        captureHistory();
        scrollFraction = (float) (historyPosition-std::floor (historyPosition));
        audioProcessor.setFrozenHistory ({});
    }
    rememberFrozenHistory();
    audioProcessor.editorStateChanged();
    repaint();
}

double ThresholdScope::historyDelayFrames() const
{
    const double rate = audioProcessor.getSampleRate() > 0 ? audioProcessor.getSampleRate() : 48000;
    const int interval = juce::jmax (1, juce::roundToInt (rate/OpenClipperAudioProcessor::historyFramesPerSecond));
    // Leave enough ring-buffer slack to retain all 800 visible history samples.
    return juce::jlimit (4.0, 128.0, std::ceil ((double) audioProcessor.getBlockSize()/interval)+2.0);
}

void ThresholdScope::captureHistory()
{
    const auto sequence = audioProcessor.getHistorySequence();
    if (historyPosition < 0 || historyPosition > (double) sequence)
    {
        historyPosition = juce::jmax (0.0, (double) sequence-historyDelayFrames());
        historyClockRunning = (double) sequence >= historyDelayFrames();
    }
    // Derive the ring position from the published sequence. Reading the write
    // cursor separately can mix two different audio-thread publications.
    const auto target = (uint64_t) historyPosition;
    const int write = (int) (target & 1023);
    for (int x = 0; x < 1024; ++x)
    {
        const int index = (write + x) & 1023;
        const bool current = audioProcessor.readScopeCaptureState (index) == captureState;
        historyOutput[(size_t) x] = current ? audioProcessor.readScopeOutput (index) : 0;
        historyReduction[(size_t) x] = current ? audioProcessor.readScopeReduction (index) : 0;
        historySmooth[(size_t) x] = current && audioProcessor.readScopeReductionIsSmooth (index);
    }
    const bool hasNext = target < sequence && audioProcessor.readScopeCaptureState (write) == captureState;
    nextOutput = hasNext ? audioProcessor.readScopeOutput (write) : 0.0f;
    nextReduction = hasNext ? audioProcessor.readScopeReduction (write) : 0.0f;
    nextSmooth = hasNext && audioProcessor.readScopeReductionIsSmooth (write);
    lastSequence = target;
    lastAudioSequence = sequence;
    if (lastFrameTime <= 0) lastFrameTime = juce::Time::getMillisecondCounterHiRes();
    rebuildHistoryPaths();
}

void ThresholdScope::rebuildHistoryPaths()
{
    cachedOutput.clear(); cachedHard.clear(); cachedSmooth.clear();
    historyRenderDirty = true;
    if (getWidth() <= 0 || getHeight() <= 0) return;
    // Every 10 ms sample keeps its own interval at every window size. Continuous
    // stepped contours remove internal rectangle edges without rounding peaks.
    const float width = (float) getWidth() / historyPoints;
    cachedOutput.startNewSubPath (0, (float) getHeight());
    cachedHard.startNewSubPath (0, 0); cachedSmooth.startNewSubPath (0, 0);
    for (int x = 0; x <= historyPoints; ++x)
    {
        const size_t index = (size_t) (1024-historyPoints+x);
        const float output = x < historyPoints ? historyOutput[index] : nextOutput;
        const float reduction = x < historyPoints ? historyReduction[index] : nextReduction;
        const bool smooth = x < historyPoints ? historySmooth[index] : nextSmooth;
        const float y = getHeight() * juce::jlimit (0.0f, 1.0f, -juce::Decibels::gainToDecibels (output, -60.0f)/36.0f);
        const float grY = reduction > .05f ? getHeight()*juce::jmin (36.0f, reduction)/36.0f : 0.0f;
        cachedOutput.lineTo (x*width, y); cachedOutput.lineTo ((x+1)*width, y);
        cachedHard.lineTo (x*width, smooth ? 0.0f : grY); cachedHard.lineTo ((x+1)*width, smooth ? 0.0f : grY);
        cachedSmooth.lineTo (x*width, smooth ? grY : 0.0f); cachedSmooth.lineTo ((x+1)*width, smooth ? grY : 0.0f);
    }
    cachedOutput.lineTo ((historyPoints+1)*width, (float) getHeight()); cachedOutput.closeSubPath();
    cachedHard.lineTo ((historyPoints+1)*width, 0); cachedHard.closeSubPath();
    cachedSmooth.lineTo ((historyPoints+1)*width, 0); cachedSmooth.closeSubPath();
}

void ThresholdScope::renderHistory (float scale)
{
    if (! historyRenderDirty && std::abs (historyRenderScale-scale) < .001f) return;
    const double step = (double) getWidth() / historyPoints;
    const int w = (int) std::ceil ((getWidth()+step+1.0/scale)*scale);
    const int h = (int) std::ceil (getHeight()*scale);
    const bool sameSize=cachedHistory.getWidth()==w && cachedHistory.getHeight()==h
        && std::abs(historyRenderScale-scale)<.001f;
    const uint64_t advanced=lastSequence>historyRasterSequence?lastSequence-historyRasterSequence:0;
    const bool incremental=sameSize && advanced>0 && advanced<=16;
    const int shift=incremental?(int)(std::floor(lastSequence*step*scale)-std::floor(historyRasterSequence*step*scale)):0;
    historyRenderScale=scale;
    historyRasterPhase=(float)(std::fmod(lastSequence*step*scale,1.0)/scale);
    juce::RectangleList<int> changed;
    if (!sameSize)
        cachedHistory=juce::Image(juce::Image::ARGB,w,h,true,juce::SoftwareImageType{});
    else cachedHistory.duplicateIfShared();
    if(incremental)
    {
        cachedHistory.moveImageSection(0,0,shift,0,w-shift,h);
        const int left=juce::jlimit(0,w,(int)std::floor((getWidth()-(advanced+2)*step-2)*scale));
        changed.add({left,0,w-left,h});
        changed.add({0,0,juce::roundToInt(2*scale),h});
    }
    else {cachedHistory.clear(cachedHistory.getBounds());changed.add(cachedHistory.getBounds());}
    const auto fill=[&](juce::Graphics& imageGraphics,const juce::Path& output,const juce::Path& hard,const juce::Path& smooth)
    {
        imageGraphics.setColour(ClipTheme::purpleDark.withAlpha(.9f));imageGraphics.fillPath(output);
        imageGraphics.setColour(ClipTheme::orange.withAlpha(.82f));imageGraphics.fillPath(hard);
        imageGraphics.setColour(ClipTheme::smoothBlue.withAlpha(.82f));imageGraphics.fillPath(smooth);
    };
    const double origin=std::floor(lastSequence*step*scale);
    const auto drawSpan=[&](juce::Graphics& imageGraphics,int first,int last)
    {
        // Quantize absolute sample positions before subtracting the origin.
        // The same peak then has exactly the same coverage after every copy.
        const auto x=[&](int n){return (float)(std::round(((double)lastSequence+n)*step*scale*64.)/64.-origin);};
        const auto y=[&](float value){return (float)(std::round(value*scale*64.)/64.);};
        juce::Path output,hard,smooth;output.startNewSubPath(x(first),(float)h);hard.startNewSubPath(x(first),0);smooth.startNewSubPath(x(first),0);
        for(int n=first;n<=last;++n)
        {
            const size_t index=(size_t)(1024-historyPoints+n);
            const float signal=n<historyPoints?historyOutput[index]:nextOutput,reduction=n<historyPoints?historyReduction[index]:nextReduction;
            const bool mode=n<historyPoints?historySmooth[index]:nextSmooth;
            const float out=y(getHeight()*juce::jlimit(0.f,1.f,-juce::Decibels::gainToDecibels(signal,-60.f)/36.f));
            const float gr=y(reduction>.05f?getHeight()*juce::jmin(36.f,reduction)/36.f:0.f);
            output.lineTo(x(n),out);output.lineTo(x(n+1),out);
            hard.lineTo(x(n),mode?0.f:gr);hard.lineTo(x(n+1),mode?0.f:gr);
            smooth.lineTo(x(n),mode?gr:0.f);smooth.lineTo(x(n+1),mode?gr:0.f);
        }
        output.lineTo(x(last+1),(float)h);output.closeSubPath();hard.lineTo(x(last+1),0);hard.closeSubPath();smooth.lineTo(x(last+1),0);smooth.closeSubPath();
        fill(imageGraphics,output,hard,smooth);
    };
    if(incremental)
    {
        // Rasterize beyond the copy boundary so clipping cannot change the
        // antialias coverage where retained and newly drawn pixels meet.
        for(const auto& region:changed)
        {
            const auto bounds=region.expanded(4,0).getIntersection(cachedHistory.getBounds());
            juce::Image strip(juce::Image::ARGB,bounds.getWidth(),h,true,juce::SoftwareImageType{});
            {juce::Graphics graphics(strip);graphics.setOrigin(-bounds.getX(),0);drawSpan(graphics,0,historyPoints);}
            juce::Image::BitmapData source(strip,juce::Image::BitmapData::readOnly),dest(cachedHistory,juce::Image::BitmapData::writeOnly);
            for(int row=0;row<h;++row)
                std::memcpy(dest.getPixelPointer(region.getX(),row),source.getPixelPointer(region.getX()-bounds.getX(),row),(size_t)region.getWidth()*4);
        }
    }
    else {juce::Graphics graphics(cachedHistory);drawSpan(graphics,0,historyPoints);}
    historyRasterSequence=lastSequence;historyRenderDirty=false;
}

void ThresholdScope::advanceHistory (double now)
{
    if (! frozen)
    {
        const double sequence = (double) audioProcessor.getHistorySequence();
        const double rate = audioProcessor.getSampleRate() > 0 ? audioProcessor.getSampleRate() : 48000;
        const double framesPerMs = rate / juce::jmax (1, juce::roundToInt (rate/OpenClipperAudioProcessor::historyFramesPerSecond)) / 1000.0;
        const double delay = historyDelayFrames();
        const double elapsed = juce::jmax (0.0, now-lastFrameTime);
        const bool audioAdvanced = (uint64_t) sequence != lastAudioSequence;
        if (audioAdvanced) lastAudioFrameTime = now;
        if (sequence < (double) lastAudioSequence || elapsed > 250.0 || sequence-historyPosition > 192.0)
        {
            historyPosition = juce::jmax (0.0, sequence-delay);
            historyClockRunning = sequence >= delay;
        }
        else if (! historyClockRunning)
        {
            // On startup/resume, wait for a small reservoir of history before
            // starting motion. Do not move old material backward to make room.
            historyClockRunning = sequence-historyPosition >= delay;
        }
        else
        {
            historyPosition = juce::jmin (sequence, historyPosition + elapsed*framesPerMs);
            if (! audioAdvanced && now-lastAudioFrameTime > 250.0 && historyPosition >= sequence)
                historyClockRunning = false;
        }
        lastFrameTime = now;
        lastAudioSequence = (uint64_t) sequence;
        if ((uint64_t) historyPosition != lastSequence) captureHistory();
        scrollFraction = (float) (historyPosition-std::floor (historyPosition));
    }
}

void ThresholdScope::timerCallback()
{
    if (! isShowing()) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (lastVblankArrival < 0 || now-lastVblankArrival > 100) animationFrame (now);
}

void ThresholdScope::vblankCallback (double timestampSeconds)
{
    if (! isShowing()) return;
    const double nativeMs = timestampSeconds*1000;
    const double arrival = juce::Time::getMillisecondCounterHiRes();
    // VBlank timestamps and JUCE's millisecond clock need not share an epoch.
    if (lastVblankTimestamp < 0 || nativeMs < lastVblankTimestamp || arrival-lastVblankArrival > 250)
    { vblankOffset = arrival-nativeMs; framePacer.reset(); }
    lastVblankTimestamp = nativeMs; lastVblankArrival = arrival;
    if (framePacer.present (nativeMs)) animationFrame (nativeMs+vblankOffset);
}

void ThresholdScope::animationFrame (double timeMs)
{
    if (! displayActive) return;
    const bool bypassChanged=bypassFade.advance(timeMs);
    // Page requests start on the UI clock. Native presentation timestamps may
    // predict ahead, so keep badge travel on elapsed UI time at a real peer.
    const double badgeTime=getPeer()!=nullptr?juce::Time::getMillisecondCounterHiRes():timeMs;
    const bool metricsChanged=metersFade.advance(badgeTime);
    const int listened=audioProcessor.getBandAudition();
    const auto status=audioProcessor.isDeltaAudition()?juce::String("DELTA"):listened>=0?"LISTEN: "+juce::String(listened==0?"LOW":listened==1?"MID":"HIGH"):juce::String();
    if(status.isNotEmpty())auditionText=status;
    auditionFade.setBypassed(status.isEmpty(),badgeTime,displayActive);
    const bool auditionChanged=auditionFade.advance(badgeTime),limitChanged=limitFade.advance(badgeTime);
    if(badgeMotion.advance(badgeTime)||bypassChanged||metricsChanged||auditionChanged||limitChanged)repaint();
    const auto oldSequence = lastSequence;
    const auto oldFraction = scrollFraction;
    advanceHistory (timeMs);
    const bool metrics=metersFade.amount()>0;
    const std::array<float, 8> readout { 0, 0,
        metrics?audioProcessor.getMomentaryLufs():0, metrics?audioProcessor.getOutputTruePeakDb():0, metrics?audioProcessor.getOutputPeakDb():0, metrics?audioProcessor.getCrestDb():0,
        (float) audioProcessor.getBandAudition(), audioProcessor.isDeltaAudition() ? 1.0f : 0.0f };
    if (oldSequence != lastSequence || ! juce::exactlyEqual (oldFraction, scrollFraction) || readout != previousReadout)
        repaint();
    previousReadout = readout;
    if (onAnimationFrame) onAnimationFrame (timeMs);
}

juce::Rectangle<float> ThresholdScope::thresholdBadgeBounds() const
{
    // Only real top readouts need clearance; the removed GR/MAX card does not.
    // The readout row and its reserved space slide together, keeping the badge
    // six pixels below the row throughout its fade, including reversals.
    const float firstY=6.f+topReadoutClearance();
    const auto clampedY=[&](int top){return juce::jlimit(firstY,juce::jmax(firstY,(float)juce::jmin(getHeight()-27,top-27)),thresholdY()-25.f);};
    const float y=badgeGeometryReady?badgeMotion.position(clampedY(badgeMainTop),clampedY(badgePageTop)):clampedY(controlsTop);
    return {22.f,y,145.f,21.f};
}

void ThresholdScope::updateLimitAppearance(float value)
{
    const bool visible=value<-.5f&&std::abs(value-displayedThreshold)>1.f;
    // Keep the outgoing position and number while its line/badge fade away.
    if(visible)displayedLimit=value;
    limitFade.setBypassed(!visible,juce::Time::getMillisecondCounterHiRes(),displayActive);
    repaint();
}

juce::Rectangle<float> ThresholdScope::limitBadgeBounds() const
{
    const float lineY=getHeight()*-displayedLimit/36.f;
    const auto clampedY=[&](int top){return juce::jlimit(6.f,(float)juce::jmax(6,juce::jmin(getHeight()-22,top-24)),lineY-20.f);};
    const float y=badgeGeometryReady?badgeMotion.position(clampedY(badgeMainTop),clampedY(badgePageTop)):clampedY(controlsTop);
    return {(float)getWidth()-112,y,102,18};
}

float ThresholdScope::thresholdY() const
{
    const float db = displayedThreshold;
    return juce::jmap (db, -36.0f, 0.0f, static_cast<float> (getHeight()), 0.0f);
}

bool ThresholdScope::hitThreshold (juce::Point<float> point) const
{
    return std::abs (point.y - thresholdY()) < 9.0f || thresholdBadgeBounds().contains (point);
}

void ThresholdScope::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown()) return;
    if (metersVisible && statisticsBounds().expanded (2.0f).contains (e.position))
    {
        audioProcessor.resetStatistics();
        repaint();
        return;
    }
    if (frozen && inspected >= 0 && e.position.y > 35 && e.position.y < controlsTop
        && std::abs (e.position.x - (float) (inspected-(1024-historyPoints))*getWidth()/historyPoints) < 7)
    { inspecting = true; return; }
    if (! hitThreshold (e.position))
    {
        if (frozen && e.position.y > 35 && e.position.y < controlsTop)
        {
            inspecting = true;
            inspected = juce::jlimit (1024-historyPoints, 1023, 1024-historyPoints + (int) (e.position.x/getWidth()*historyPoints));
            rememberFrozenHistory();
            repaint();
        }
        return;
    }
    audioProcessor.parameters.copyState(); audioProcessor.undoManager.beginNewTransaction();
    dragging = true;
    if (onThresholdEdit) onThresholdEdit();
    thresholdParameter->beginChangeGesture();
    dragY = e.position.y;
    dragDb = audioProcessor.parameters.getRawParameterValue ("threshold")->load();
    fineDrag = e.mods.isShiftDown();
}

void ThresholdScope::mouseDrag (const juce::MouseEvent& e)
{
    if (inspecting)
    {
        inspected = juce::jlimit (1024-historyPoints, 1023, 1024-historyPoints + (int) (e.position.x/getWidth()*historyPoints));
        rememberFrozenHistory();
        repaint(); return;
    }
    if (dragging)
    {
        if (fineDrag != e.mods.isShiftDown())
        {
            dragY = e.position.y;
            dragDb = audioProcessor.parameters.getRawParameterValue ("threshold")->load();
            fineDrag = e.mods.isShiftDown();
        }
        const float db = juce::jlimit (-36.0f, 0.0f, dragDb + (dragY - e.position.y)
            * 36.0f / juce::jmax (1, getHeight()) * (fineDrag ? 0.1f : 1.0f));
        thresholdParameter->setValueNotifyingHost (thresholdParameter->convertTo0to1 (db));
        repaint();
    }
}

void ThresholdScope::mouseUp (const juce::MouseEvent&)
{
    if (inspecting) audioProcessor.editorStateChanged();
    const bool wasDragging = dragging;
    if (dragging) thresholdParameter->endChangeGesture();
    dragging = false;
    inspecting = false;
    if (wasDragging && onThresholdEditEnd) onThresholdEditEnd();
}

void ThresholdScope::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (metersVisible && statisticsBounds().expanded (2.0f).contains (e.position))
    {
        audioProcessor.resetStatistics();
        repaint();
        return;
    }
    if (! hitThreshold (e.position)) return;
    if (onThresholdEdit) onThresholdEdit();
    if (dragging) { thresholdParameter->endChangeGesture(); dragging = false; }
    thresholdParameter->beginChangeGesture();
    thresholdParameter->setValueNotifyingHost (thresholdParameter->convertTo0to1 (0.0f));
    thresholdParameter->endChangeGesture();
    if (onThresholdEditEnd) onThresholdEditEnd();
}

void ThresholdScope::mouseMove (const juce::MouseEvent& e)
{
    const bool overStats = metersVisible && statisticsBounds().expanded (2.0f).contains (e.position);
    setMouseCursor (overStats ? juce::MouseCursor::PointingHandCursor
                              : hitThreshold (e.position) ? juce::MouseCursor::UpDownResizeCursor
                              : frozen ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void ThresholdScope::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void ThresholdScope::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (ClipTheme::background);
    g.fillRoundedRectangle (area, 8.0f);
    const float thresholdDb = displayedThreshold;
    const bool smoothMode = audioProcessor.parameters.getRawParameterValue ("smoothMode")->load() > 0.5f;
    const float appearance=bypassFade.amount();
    const auto reductionColour = smoothMode ? ClipTheme::smoothBlue : ClipTheme::orange;

    const auto dbToY = [this] (float db)
    {
        return juce::jmap (juce::jlimit (-36.0f, 0.0f, db), -36.0f, 0.0f,
                           static_cast<float> (getHeight()), 0.0f);
    };
    for (const float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f })
    {
        const float y = dbToY (db);
        g.setColour (ClipTheme::edge.withAlpha (0.55f));
        g.drawHorizontalLine (juce::roundToInt (y), 22.0f, area.getRight());
    }
    for (const float db : { -3.0f, -9.0f, -15.0f, -21.0f })
    {
        const float y = dbToY (db);
        g.setColour (ClipTheme::edge.withAlpha (0.24f));
        g.drawHorizontalLine (juce::roundToInt (y), 22.0f, area.getRight());
    }

    renderHistory (juce::jmax (2.0f, g.getInternalContext().getPhysicalPixelScaleFactor()));
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (cachedHistory, juce::AffineTransform::scale (1.0f/historyRenderScale)
        .translated (-historyRasterPhase-scrollFraction*getWidth()/historyPoints, 0));

    const float y = thresholdY();
    g.setColour (ClipTheme::red.withAlpha (.008f+.027f*appearance));
    g.fillRect (0.0f, 0.0f, static_cast<float> (getWidth()), y);
    // Scale labels are foreground UI, never buried under the filled history.
    for (const float db : { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -15.0f, -18.0f, -21.0f, -24.0f })
    {
        const bool minor = static_cast<int> (std::abs (db)) % 6 != 0;
        g.setColour (ClipTheme::dim.withAlpha (minor ? 0.58f : 1.0f));
        g.setFont (juce::FontOptions (minor ? 7.5f : 8.0f));
        g.drawText (juce::String (std::abs (db), 0), 2,
                    juce::jlimit (0, getHeight() - 12, juce::roundToInt (dbToY (db)) - 6), 18, 12,
                    juce::Justification::centredLeft);
    }
    g.setColour (ClipTheme::cyan.withAlpha(.28f+.72f*appearance));
    const float thresholdStroke = 1.f+appearance*((dragging?2.5f:1.5f)-1.f);
    const float strokeY = plotStrokeCentre (y, area.getHeight(), thresholdStroke);
    g.drawLine (0.0f, strokeY, static_cast<float> (getWidth()), strokeY, thresholdStroke);

    // Limit is the final output cap. Show it as a separate boundary whenever
    // it is away from both the threshold and the zero-dB edge.
    const float limitOpacity=limitFade.amount();
    if (limitOpacity>0)
    {
        const float limitY=plotStrokeCentre(dbToY(displayedLimit),area.getHeight(),1.f);
        g.setColour (ClipTheme::purple.withAlpha ((.24f+.48f*appearance)*limitOpacity));
        g.drawLine (0.0f, limitY, area.getRight(), limitY, 1.0f);
        const auto limitBadge=limitBadgeBounds();
        g.setColour (ClipTheme::panel.withAlpha (0.86f*limitOpacity));
        g.fillRoundedRectangle (limitBadge, 4.0f);
        g.setColour (ClipTheme::purple.withAlpha ((.45f+.55f*appearance)*limitOpacity));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("LIMIT  " + juce::String (displayedLimit, 1) + " dB", limitBadge,
                    juce::Justification::centred);
    }

    const auto labelArea = thresholdBadgeBounds();
    g.setColour (ClipTheme::panel.withAlpha (0.92f));
    g.fillRoundedRectangle (labelArea, 4.0f);
    g.setColour (ClipTheme::cyan.withAlpha (.5f+.5f*appearance));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("THRESHOLD  " + juce::String (std::abs (thresholdDb) < 0.05f ? 0.0f : thresholdDb, 1) + " dB",
                labelArea.toNearestInt(), juce::Justification::centred);

    const auto stats=statisticsBounds().toNearestInt();
    const float metricsOpacity=metersFade.amount();
    if(metricsOpacity>0)
    {
        g.setColour(ClipTheme::panel.withAlpha(.94f*metricsOpacity));g.fillRoundedRectangle(stats.toFloat(),4.f);
        const auto value=[](float v){return v < -99 ? juce::String("--") : juce::String(v,1);};
        g.setColour(reductionColour.withAlpha(metricsOpacity));g.setFont(juce::FontOptions(9.5f,juce::Font::bold));
        const std::array<const char*,4> names{"LUFS M ","CREST ","PEAK ","TP "};
        for(int n=0;n<4;++n){const float v=n==0?audioProcessor.getMomentaryLufs():n==1?audioProcessor.getCrestDb():n==2?audioProcessor.getOutputPeakDb():audioProcessor.getOutputTruePeakDb();
            g.drawText(juce::String(names[(size_t)n])+value(v),stats.getX()+n*74,stats.getY(),74,stats.getHeight(),juce::Justification::centred);}
    }
    if(auditionFade.amount()>0 && auditionText.isNotEmpty())
    {
        g.setColour(ClipTheme::cyan.withAlpha(auditionFade.amount()));g.setFont(juce::FontOptions(9.f,juce::Font::bold));
        const int x=22+juce::roundToInt(304*metricsOpacity);
        g.drawText(auditionText,x,stats.getY(),juce::jmax(0,getWidth()-x-4),stats.getHeight(),juce::Justification::centredLeft);
    }
    // Keep the inspection selection, but don't let its tooltip peek out
    // underneath the Bands overlay when a frozen view is restored.
    if (inspected >= 0 && ! inspectionHidden)
    {
        const float x = (float) (inspected-(1024-historyPoints))*getWidth()/historyPoints;
        FrozenInspection::paint(g, getLocalBounds().toFloat(), x, (float)controlsTop, (1023-inspected)*.01,
            { { "OUT", FrozenInspection::level(historyOutput[(size_t)inspected], -60.f) },
              { "GR", juce::String(historyReduction[(size_t)inspected], 1) + " dB" } }, {},
            ClipTheme::panel, ClipTheme::text, ClipTheme::dim);
    }
}

void GainReductionMeter::refresh()
{
    const auto now=juce::Time::getMillisecondCounterHiRes();
    const std::array<float,7> values{audioProcessor.getCurrentGainReductionDb(0),audioProcessor.getCurrentGainReductionDb(1),
        audioProcessor.getReductionPeakMarker(0),audioProcessor.getReductionPeakMarker(1),
        audioProcessor.parameters.getRawParameterValue("smoothMode")->load(),audioProcessor.parameters.getRawParameterValue("bypass")->load(),audioProcessor.getMaximumGainReductionDb()};
    modeFade.setBypassed(values[4]<.5f,now);bypassFade.setBypassed(values[5]>.5f,now);
    const bool changed=maximum.update(values[6],now),a=maximum.advance(now),b=modeFade.advance(now),c=bypassFade.advance(now);
    if(values!=previous||changed||a||b||c){previous=values;repaint();}
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    g.fillAll(ClipTheme::background);
    const float appearance=.4f+.6f*bypassFade.amount();
    const auto meterColour=ClipTheme::orange.interpolatedWith(ClipTheme::smoothBlue,modeFade.amount());
    const auto readoutColour=ClipTheme::dim;
    const auto text=[](float value){return value<.005f?juce::String("0.00"):"-"+juce::String(value,2);};
    // The stereo bars span x=4..30; keep both crossfade layers on their centre.
    g.setFont(juce::FontOptions(10.5f,juce::Font::bold));
    g.setColour(readoutColour.withAlpha(appearance*(1-maximum.opacity())));g.drawFittedText(text(maximum.oldValue()),0,0,34,14,juce::Justification::centred,1,.8f);
    g.setColour(readoutColour.withAlpha(appearance*maximum.opacity()));g.drawFittedText(text(maximum.value()),0,0,34,14,juce::Justification::centred,1,.8f);
    const float height=juce::jmax(1.f,getHeight()-14.f);
    g.setColour(ClipTheme::panel);g.fillRect(0.f,14.f,(float)getWidth(),height);
    g.addTransform(juce::AffineTransform::translation(0,14));
    const auto reductionToY = [height] (float reductionDb)
    {
        return juce::jmap (juce::jlimit (0.0f, 36.0f, reductionDb), 0.0f, 36.0f, 0.0f, height);
    };

    for (int channel = 0; channel < 2; ++channel)
    {
        const float x = channel == 0 ? 4.0f : 19.0f;
        const auto colour = channel == 0 ? meterColour : meterColour.brighter (0.22f);
        const auto track = juce::Rectangle<float> (x, 0, 11, height);
        g.setColour (ClipTheme::background); g.fillRoundedRectangle (track, 2);
        g.setGradientFill ({ colour, track.getTopLeft(), colour.darker (0.35f), track.getBottomLeft(), false });
        g.fillRoundedRectangle (track.withBottom (reductionToY (audioProcessor.getCurrentGainReductionDb (channel))), 2);
        g.setColour (colour);
        g.fillRect (x - 1, juce::jlimit (0.0f, height - 2, reductionToY (audioProcessor.getReductionPeakMarker (channel)) - 1), 13.0f, 2.0f);
    }

    for (const float db : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f })
    {
        const int y = juce::roundToInt (reductionToY (db));
        g.setColour (ClipTheme::edge);
        g.drawHorizontalLine (y, 32.0f, 37.0f);
        g.setColour (ClipTheme::dim);
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (juce::String (db, 0), 40, juce::jlimit (0, (int)height - 12, y - 6), 24, 12,
                    juce::Justification::centredLeft);
    }
    for (const float db : { 3.0f, 9.0f, 15.0f, 21.0f, 27.0f, 33.0f })
    {
        const int y = juce::roundToInt (reductionToY (db));
        g.setColour (ClipTheme::edge.withAlpha (0.42f));
        g.drawHorizontalLine (y, 32.0f, 37.0f);
        g.setColour (ClipTheme::dim.withAlpha (0.58f));
        g.setFont (juce::FontOptions (7.5f));
        g.drawText (juce::String (db, 0), 40, juce::jlimit (0, (int)height - 12, y - 6), 24, 12,
                    juce::Justification::centredLeft);
    }
}

ClipperAdvancedPanel::ClipperAdvancedPanel (OpenClipperAudioProcessor& p) : processor (p)
{
    const std::array<const char*, 5> ids { "lowSplit", "lowDepth", "midDepth", "highDepth", "highSplit" };
    const std::array<const char*, 5> names { "LOW SPLIT", "LOW DEPTH", "MID DEPTH", "HIGH DEPTH", "HIGH SPLIT" };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.setName (names[i]); knob.setComponentID (ids[i]);
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
        knob.setTextValueSuffix (i == 0 || i == 4 ? " Hz" : " dB");
        knob.setMouseDragSensitivity (180);
        knob.setDoubleClickReturnValue (true, i == 0 ? 250.0 : i == 4 ? 4000.0 : 0.0);
        knobAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, ids[i], knob);
        knob.textFromValueFunction = [i] (double v) { return juce::String (v, i == 0 || i == 4 ? 0 : 1); };
        knob.valueFromTextFunction = [this, i] (juce::String text)
        {
            auto& slider = knobs[i];
            text = text.trim().replaceCharacter (0x2212, '-').replaceCharacter (',', '.');
            const auto unit = slider.getTextValueSuffix().trim();
            if (text.endsWithIgnoreCase (unit)) text = text.dropLastCharacters (unit.length()).trim();
            const auto* start = text.toRawUTF8(); char* end = nullptr;
            const double value = std::strtod (start, &end);
            return end == start || *end != '\0' || ! std::isfinite (value) ? slider.getValue()
                : juce::jlimit (slider.getMinimum(), slider.getMaximum(), value);
        };
        knob.updateText();
        labels[i].setText (names[i], juce::dontSendNotification);
        labels[i].setFont (juce::FontOptions (8.0f, juce::Font::bold));
        labels[i].setJustificationType (juce::Justification::centred);
        labels[i].setColour (juce::Label::textColourId, ClipTheme::dim);
        addChildComponent (knob); addChildComponent (labels[i]);
    }
    const std::array<juce::ComboBox*, 4> choices { &liveQuality, &renderQuality, &curve, &channels };
    const std::array<juce::Label*, 4> choiceLabels { &liveLabel, &renderLabel, &curveLabel, &channelsLabel };
    const std::array<const char*, 4> choiceNames { "PLAYBACK OS", "RENDER OS", "SMOOTH CURVE", "CHANNELS" };
    const std::array<const char*, 4> choiceIds { "liveQuality", "renderQuality", "curve", "channelMode" };
    liveQuality.addItem ("Off", 7);
    liveQuality.addItem ("2x", 8);
    liveQuality.addItemList ({ "4x", "8x", "16x", "4x LP", "8x LP", "16x LP" }, 1);
    liveQuality.setName ("Oversampling; LP uses linear-phase filters");
    liveQuality.onChange = [this]
    {
        const int selected = liveQuality.getSelectedId();
        const int index = selected-1;
        if (index < 0) return;
        for (const auto& item : { std::pair<const char*, float> { "liveQuality", selected>6 ? 0.0f : (float) (index%3) },
                { "linearPhase", selected>=4 && selected<=6 ? 1.0f : 0.0f },
                { "lowRate", selected==7 ? 1.0f : selected==8 ? 2.0f : 0.0f } })
        {
            auto* parameter=processor.parameters.getParameter(item.first);
            parameter->beginChangeGesture(); parameter->setValueNotifyingHost(parameter->convertTo0to1(item.second)); parameter->endChangeGesture();
        }
    };
    renderQuality.addItemList ({ "Playback", "Min 8x", "16x" }, 1);
    curve.addItemList ({ "Classic", "Cubic" }, 1);
    channels.addItemList ({ "Stereo L/R", "Mid / Side" }, 1);
    msBias.setName ("M/S BIAS"); msBias.setComponentID ("msBias");
    msBias.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    msBias.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 14);
    msBias.setDoubleClickReturnValue (true, 0.0);
    msBias.setMouseDragSensitivity (180);
    msBiasAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, "msBias", msBias);
    msBias.textFromValueFunction = [] (double value)
    {
        return std::abs (value) < 0.005 ? juce::String ("Neutral")
            : juce::String (std::abs (value), 1) + (value < 0 ? " M" : " S");
    };
    msBias.valueFromTextFunction = [this] (juce::String text)
    {
        text = text.trim().replaceCharacter (0x2212, '-').replaceCharacter (',', '.');
        if (text.equalsIgnoreCase ("Neutral")) return 0.0;
        const bool mid = text.endsWithIgnoreCase ("M"), side = text.endsWithIgnoreCase ("S");
        if (mid || side) text = text.dropLastCharacters (1).trim();
        if (text.endsWithIgnoreCase ("dB")) text = text.dropLastCharacters (2).trim();
        const auto* start = text.toRawUTF8(); char* end = nullptr;
        double value = std::strtod (start, &end);
        if (end == start || *end != '\0' || ! std::isfinite (value)) return msBias.getValue();
        if (mid) value = -std::abs (value); else if (side) value = std::abs (value);
        return juce::jlimit (-12.0, 12.0, value);
    };
    msBias.updateText();
    msBiasLabel.setText ("M/S BIAS", juce::dontSendNotification);
    msBiasLabel.setFont (juce::FontOptions (8.0f, juce::Font::bold));
    msBiasLabel.setColour (juce::Label::textColourId, ClipTheme::dim);
    msBiasLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (msBias); addAndMakeVisible (msBiasLabel);
    channels.onChange = [this]
    {
        const bool enabled = channels.getSelectedId() == 2;
        msBias.setEnabled (enabled); msBiasLabel.setEnabled (enabled);
        if (linkAttachment) updateLinkBinding();
    };
    for (size_t i = 0; i < choices.size(); ++i)
    {
        choices[i]->setComponentID (choiceIds[i]);
        if (i != 0) choiceAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.parameters, choiceIds[i], *choices[i]);
        choiceLabels[i]->setText (choiceNames[i], juce::dontSendNotification);
        choiceLabels[i]->setFont (juce::FontOptions (8.0f, juce::Font::bold));
        choiceLabels[i]->setColour (juce::Label::textColourId, ClipTheme::dim);
        addAndMakeVisible (choices[i]); addAndMakeVisible (choiceLabels[i]);
    }
    enableBands.setButtonText ("3 BAND ON");
    enableBands.setComponentID ("enableBands");
    enableBands.onClick = [this] { if (! enableBands.getToggleState()) clearBandListen(); };
    bandsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, "bands", enableBands);
    addChildComponent (enableBands);
    for (int b = 0; b < 3; ++b)
    {
        auto& button = bandSolo[(size_t) b];
        button.setButtonText ("");
        button.setName (b == 0 ? "Listen to low band" : b == 1 ? "Listen to mid band" : "Listen to high band");
        button.setClickingTogglesState (true);
        button.setComponentID ("solo" + juce::String (b));
        button.onClick = [this, b]
        {
            const bool listen = bandSolo[(size_t) b].getToggleState() && enableBands.getToggleState() && isVisible();
            clearBandListen();
            if (listen)
            {
                bandSolo[(size_t) b].setToggleState (true, juce::dontSendNotification);
                processor.setBandAudition (b);
            }
            processor.editorStateChanged();
        };
        addChildComponent (button);
    }
    const auto setupExtra = [this, &p] (juce::Slider& slider, juce::Label& label, const char* id, const char* title,
                                      std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        slider.setName (title); slider.setComponentID (id);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 14);
        slider.setMouseDragSensitivity (180);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, id, slider);
        slider.setDoubleClickReturnValue (true, juce::String (id) == "release" ? 5.0 : 100.0);
        slider.setTextValueSuffix (juce::String (id) == "release" ? " ms" : "%");
        slider.textFromValueFunction = [] (double value) { return juce::String (value, 1); };
        slider.valueFromTextFunction = [&slider] (juce::String text)
        {
            text = text.trim().replaceCharacter (0x2212, '-').replaceCharacter (',', '.');
            const auto unit = slider.getTextValueSuffix().trim();
            if (text.endsWithIgnoreCase (unit)) text = text.dropLastCharacters (unit.length()).trim();
            const auto* start = text.toRawUTF8(); char* end = nullptr;
            const double value = std::strtod (start, &end);
            return end == start || *end != '\0' || ! std::isfinite (value) ? slider.getValue()
                : juce::jlimit (slider.getMinimum(), slider.getMaximum(), value);
        };
        slider.updateText();
        label.setText (title, juce::dontSendNotification);
        label.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ClipTheme::dim);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (slider); addAndMakeVisible (label);
    };
    setupExtra (release, releaseLabel, "release", "RELEASE", releaseAttachment);
    setupExtra (stereoLink, linkLabel, "stereoLink", "STEREO LINK", linkAttachment);
    gainLink.setButtonText ("GAIN LINK"); gainLink.setComponentID ("gainLink");
    truePeak.setButtonText ("TRUE PEAK"); truePeak.setComponentID ("truePeak");
    gainLinkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, "gainLink", gainLink);
    truePeakAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, "truePeak", truePeak);
    addAndMakeVisible (gainLink); addAndMakeVisible (truePeak);
    dcFilter.setButtonText ("DC"); dcFilter.setName ("DC filter, 5 Hz"); dcFilter.setComponentID ("dcFilter");
    dcFilterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, "dcFilter", dcFilter);
    addAndMakeVisible (dcFilter);
    stageReduction.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    stageReduction.setColour (juce::Label::textColourId, ClipTheme::dim);
    stageReduction.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (stageReduction);
    delta.setButtonText ("HOLD DELTA"); delta.setComponentID ("delta"); delta.setClickingTogglesState (false);
    delta.onStateChange = [this] { processor.setDeltaAudition (delta.isDown() && isShowing()); };
    snapshotA.setButtonText ("A"); snapshotB.setButtonText ("B");
    snapshotA.onClick = [this] { processor.selectSnapshot (0); timerCallback(); };
    snapshotB.onClick = [this] { processor.selectSnapshot (1); timerCallback(); };
    copy.onClick = [this] { processor.copySnapshot(); copyFeedbackTicks = 45; copy.setButtonText ("COPIED"); };
    match.onClick = [this]
    {
        const bool limited = processor.isMatchLimited();
        match.setButtonText (processor.matchOutputToInput() ? (limited ? "HEADROOM LIMIT" : "MATCHED") : "PLAY AUDIO");
        feedbackTicks = 45;
    };
    for (auto* button : { (juce::Button*) &delta, (juce::Button*) &snapshotA, (juce::Button*) &snapshotB, (juce::Button*) &copy, (juce::Button*) &match })
        addAndMakeVisible (button);
    bandCurve.setComponentID ("bandCurve"); bandCurve.addItemList ({ "Follow Global", "Hard", "Classic", "Cubic" }, 1);
    crossover.setComponentID ("crossover"); crossover.setName ("Band crossover");
    crossover.addItemList ({ "Broad", "Focused 24" }, 1);
    crossoverAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.parameters, "crossover", crossover);
    addChildComponent (crossover);
    bandEnabled.setComponentID ("bandEnabled"); bandEnabled.setButtonText ("BAND ON");
    addChildComponent (bandCurve); addChildComponent (bandEnabled);
    const std::array<juce::Slider*, 3> bandControls { &bandSoft, &bandMix, &bandTrim };
    const std::array<juce::Label*, 3> bandLabels { &bandSoftLabel, &bandMixLabel, &bandTrimLabel };
    for (int n = 0; n < 3; ++n)
    {
        auto& knob = *bandControls[(size_t) n]; auto& label = *bandLabels[(size_t) n];
        knob.setComponentID (n == 0 ? "bandSoftness" : n == 1 ? "bandMix" : "bandTrim");
        knob.setSliderStyle (juce::Slider::LinearHorizontal);
        knob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        knob.setMouseDragSensitivity (180); knob.setTextValueSuffix (n == 2 ? " dB" : "%");
        knob.textFromValueFunction = [n] (double value) { return juce::String (std::abs (value) < .005 ? 0.0 : value, n == 2 ? 2 : 1); };
        knob.valueFromTextFunction = [&knob] (juce::String text)
        {
            text = text.trim().replaceCharacter (0x2212, '-').replaceCharacter (',', '.');
            const auto unit = knob.getTextValueSuffix().trim();
            if (text.endsWithIgnoreCase (unit)) text = text.dropLastCharacters (unit.length()).trim();
            const auto* start = text.toRawUTF8(); char* end = nullptr;
            const double value = std::strtod (start, &end);
            return end == start || *end != '\0' || ! std::isfinite (value) ? knob.getValue()
                : juce::jlimit (knob.getMinimum(), knob.getMaximum(), value);
        };
        knob.setDoubleClickReturnValue (true, n == 0 ? 15 : n == 1 ? 100 : 0);
        label.setText (n == 0 ? "SOFTNESS" : n == 1 ? "MIX" : "OUTPUT", juce::dontSendNotification);
        label.setFont (juce::FontOptions (8.0f, juce::Font::bold)); label.setColour (juce::Label::textColourId, ClipTheme::dim);
        label.setJustificationType (juce::Justification::centred);
        addChildComponent (knob); addChildComponent (label);
    }
    spectrumLevels.fill (-90);
    knobs[1].setSliderStyle (juce::Slider::LinearHorizontal);
    knobs[1].setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    bandCurve.onChange = [this] { timerCallback(); };
    selectBand (processor.editorState.selectedBand.load());
    timerCallback();
    liveQuality.setTooltip ("Playback oversampling: Off (1x), 2x, 4x, 8x or 16x. LP starts at 4x, uses linear-phase filters and adds latency. Lower rates use less CPU but can produce more aliasing when clipping.");
    renderQuality.setTooltip ("Offline-render oversampling. Playback uses the live setting; export uses at least the selected render quality.");
    curve.setTooltip ("Smooth transfer curve: Classic or Cubic. Only used in Smooth mode; Hard has its own Knee control.");
    channels.setTooltip ("Process Left/Right or Mid/Side. M/S Bias adjusts which component clips more; each mode remembers its own Link value.");
    liveLabel.setTooltip (liveQuality.getTooltip()); renderLabel.setTooltip (renderQuality.getTooltip());
    curveLabel.setTooltip (curve.getTooltip()); channelsLabel.setTooltip (channels.getTooltip());
    setSliderHelp (msBias, "Mid/Side clipping bias. Negative values lower the Mid threshold; positive values lower the Side threshold. Stereo L/R is unaffected.", &msBiasLabel);
    setSliderHelp (release, "Smooth gain-release time. Longer values hold attenuation longer after peaks. Only active in Smooth mode.", &releaseLabel);
    setSliderHelp (stereoLink, "Smooth detector linking. 100% applies the strongest attenuation to both lanes; 0% lets them act independently. L/R and M/S settings are remembered separately.", &linkLabel);
    gainLink.setTooltip ("Link editor Input gain changes to opposite Output gain changes. Threshold and Final Limit remain independent.");
    truePeak.setTooltip ("Reconstruction-aware final peak protection at the Limit setting (0 dB by default), with no fixed headroom offset. Adds 48 samples of lookahead; uses additional CPU.");
    dcFilter.setTooltip ("Remove DC with a 5 Hz high-pass filter before clipping. Off preserves the unfiltered input.");
    delta.setTooltip ("Hold to hear the signal removed by the wet processing. Release to return. Audition is disabled during offline export.");
    snapshotA.setTooltip ("Recall A sound settings. Does not change the open page, Freeze, Meters or hover help.");
    snapshotB.setTooltip ("Recall B sound settings. Does not change the open page, Freeze, Meters or hover help.");
    copy.setTooltip ("Copy current sound settings to the other A/B slot.");
    match.setTooltip ("Match Output gain to the measured input/output energy. Play audio first; correction is capped at 12 dB and constrained by Final Limit headroom.");
    stageReduction.setTooltip ("Wet-stage and final-stage gain reduction. The final safeguard acts after Mix and Output gain.");
    enableBands.setTooltip ("Enable three-band clipping. Drag the two cyan crossover markers to change the splits.");
    bandEnabled.setTooltip ("Enable processing for the selected band. Its Listen and controls are unavailable when three-band processing is off.");
    bandCurve.setTooltip ("Selected band's clip mode. Follow Global inherits the main mode and Knee/Softness. The global transfer still acts after the bands are recombined.");
    crossover.setTooltip ("Broad complementary split or Focused 24 dB/octave crossovers. Only nonlinear band corrections are recombined, preserving the inactive signal.");
    setSliderHelp (knobs[1], "Selected band's extra clipping depth below the global threshold. Drag the slider horizontally; drag the numeric value vertically or click to type.", &labels[1]);
    setSliderHelp (bandMix, "Blend clipping within the selected band. Output trim still applies. Drag the slider horizontally, or its numeric value vertically.", &bandMixLabel);
    setSliderHelp (bandTrim, "Selected band's output gain before recombination and global clipping. Drag the slider horizontally, or its numeric value vertically.", &bandTrimLabel);
    for (auto& button : bandSolo)
        button.setTooltip (button.getName() + ". Click to toggle; stays active across editor closing and threshold edits. Leaving Bands or disabling three-band processing stops Listen.");
}

void ClipperAdvancedPanel::selectBand (int band)
{
    selectedBand = juce::jlimit (0, 2, band);
    if (processor.editorState.selectedBand.exchange (selectedBand) != selectedBand)
        processor.editorStateChanged();
    const juce::String prefix = selectedBand == 0 ? "low" : selectedBand == 1 ? "mid" : "high";
    bandEnabled.setButtonText (prefix.toUpperCase() + " ON");
    bandEnabled.setColour (juce::TextButton::buttonOnColourId, ClipTheme::bandTints[(size_t) selectedBand]);
    for (auto* slider : { &knobs[1], &bandSoft, &bandMix, &bandTrim })
        slider->setColour (juce::Slider::trackColourId, ClipTheme::bandTints[(size_t) selectedBand].brighter (.25f));
    knobAttachments[1].reset();
    knobAttachments[1] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, prefix + "Depth", knobs[1]);
    knobs[1].setComponentID ("bandDepth");
    labels[1].setText ("DEPTH", juce::dontSendNotification);
    const std::array<juce::Slider*, 3> controls { &bandSoft, &bandMix, &bandTrim };
    const std::array<const char*, 3> suffix { "Softness", "Mix", "Trim" };
    for (int n = 0; n < 3; ++n)
    {
        auto& slider = *controls[(size_t) n];
        const auto textFromValue = slider.textFromValueFunction;
        const auto valueFromText = slider.valueFromTextFunction;
        bandControlAttachments[(size_t) n].reset();
        bandControlAttachments[(size_t) n] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, prefix + suffix[(size_t) n], *controls[(size_t) n]);
        slider.textFromValueFunction = textFromValue;
        slider.valueFromTextFunction = valueFromText;
        slider.updateText();
    }
    bandCurveAttachment.reset(); bandEnabledAttachment.reset();
    bandCurveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.parameters, prefix + "Curve", bandCurve);
    bandEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, prefix + "Enabled", bandEnabled);
    bandSoftParameter.clear();
    timerCallback(); repaint();
}

void ClipperAdvancedPanel::updateSpectrum()
{
    const int write = processor.getSpectrumWrite();
    std::array<float, 2048> power {};
    for (int ch = 0; ch < 2; ++ch)
    {
        fftData.fill (0);
        for (int n = 0; n < 4096; ++n)
            fftData[(size_t) n] = processor.readSpectrumSample (write+n, ch) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi*n/4095.0f));
        spectrumFFT.performFrequencyOnlyForwardTransform (fftData.data());
        for (int n = 0; n < 2048; ++n) power[(size_t) n] += fftData[(size_t) n]*fftData[(size_t) n]*.5f;
    }
    for (int n = 0; n < 2048; ++n)
    {
        const float db = juce::Decibels::gainToDecibels (std::sqrt (power[(size_t) n])/1024.0f, -90.0f);
        spectrumLevels[(size_t) n] += (db > spectrumLevels[(size_t) n] ? .7f : .15f) * (db-spectrumLevels[(size_t) n]);
    }
}

ClipperAdvancedPanel::~ClipperAdvancedPanel()
{
    stopTimer();
    processor.setSpectrumEnabled (false);
    for (auto& knob : knobs) finishSliderInteraction (knob);
    for (auto* slider : { &msBias, &release, &stereoLink, (juce::Slider*) &bandSoft,
                         (juce::Slider*) &bandMix, (juce::Slider*) &bandTrim }) finishSliderInteraction (*slider);
    processor.setDeltaAudition (false);
    if (draggedSplit != nullptr) draggedSplit->endChangeGesture();
}

void ClipperAdvancedPanel::visibilityChanged()
{
    if (! isVisible()) processor.setDeltaAudition (false);
    processor.setSpectrumEnabled (displayActive && isVisible() && bandsView);
    if (displayActive && isVisible()) { timerCallback(); startTimerHz (30); } else stopTimer();
}

void ClipperAdvancedPanel::setDisplayActive (bool enabled)
{
    displayActive = enabled;
    if (! enabled)
    {
        spectrumLevels.fill (-90);
        processor.setDeltaAudition (false);
        for (auto& knob : knobs) finishSliderInteraction (knob);
        for (auto* slider : { &msBias, &release, &stereoLink, (juce::Slider*) &bandSoft,
                             (juce::Slider*) &bandMix, (juce::Slider*) &bandTrim }) finishSliderInteraction (*slider);
    }
    visibilityChanged();
}

void ClipperAdvancedPanel::clearBandListen()
{
    processor.setBandAudition (-1);
    for (auto& button : bandSolo) button.setToggleState (false, juce::dontSendNotification);
}

void ClipperAdvancedPanel::updateLinkBinding()
{
    const bool midSide = processor.parameters.getRawParameterValue ("channelMode")->load() > 0.5f;
    if (midSide != linkUsesMidSide)
    {
        linkAttachment.reset();
        linkUsesMidSide = midSide;
        linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameters, midSide ? "msLink" : "stereoLink", stereoLink);
    }
    linkLabel.setText (midSide ? "M/S LINK" : "STEREO LINK", juce::dontSendNotification);
}

void ClipperAdvancedPanel::timerCallback()
{
    if (! displayActive) return;
    if (selectedBand != processor.editorState.selectedBand.load())
    { selectBand (processor.editorState.selectedBand.load()); return; }
    const bool lp = processor.parameters.getRawParameterValue ("linearPhase")->load() > .5f;
    const int lowRate = (int) processor.parameters.getRawParameterValue ("lowRate")->load();
    liveQuality.setSelectedId (! lp && lowRate > 0 ? 6 + lowRate
        : 1 + (int) processor.parameters.getRawParameterValue ("liveQuality")->load() + (lp ? 3 : 0), juce::dontSendNotification);
    updateLinkBinding();
    const bool enabled = processor.parameters.getRawParameterValue ("bands")->load() > 0.5f;
    if (! enabled) clearBandListen();
    for (int b = 0; b < 3; ++b)
        bandSolo[(size_t) b].setToggleState (processor.getBandAudition() == b, juce::dontSendNotification);
    for (size_t i = 0; i < knobs.size(); ++i) { knobs[i].setEnabled (enabled); labels[i].setEnabled (enabled); }
    const bool smooth = processor.parameters.getRawParameterValue ("smoothMode")->load() > 0.5f;
    curve.setEnabled (smooth); curveLabel.setEnabled (smooth);
    for (auto* c : { (juce::Component*) &release, (juce::Component*) &releaseLabel,
                     (juce::Component*) &stereoLink, (juce::Component*) &linkLabel }) c->setEnabled (smooth);
    for (auto& button : bandSolo) button.setEnabled (enabled);
    if (bandsView && isShowing()) updateSpectrum();
    bandEnabled.setEnabled (enabled); bandCurve.setEnabled (enabled && bandEnabled.getToggleState());
    const bool bandActive = enabled && bandEnabled.getToggleState();
    knobs[1].setEnabled (bandActive); labels[1].setEnabled (bandActive);
    const bool followGlobal = bandCurve.getSelectedId() == 1;
    const bool useKnee = bandCurve.getSelectedId() == 2 || (followGlobal && ! smooth);
    const juce::String prefix = selectedBand == 0 ? "low" : selectedBand == 1 ? "mid" : "high";
    const juce::String softParameter = followGlobal ? (useKnee ? "hardKnee" : "softness") : prefix + (useKnee ? "Knee" : "Softness");
    if (bandSoftParameter != softParameter)
    {
        bandControlAttachments[0].reset(); bandSoftParameter = softParameter;
        const auto format = bandSoft.textFromValueFunction;
        const auto parse = bandSoft.valueFromTextFunction;
        bandControlAttachments[0] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, softParameter, bandSoft);
        bandSoft.textFromValueFunction = format; bandSoft.valueFromTextFunction = parse; bandSoft.updateText();
    }
    bandSoftLabel.setText (useKnee ? "KNEE" : "SOFTNESS", juce::dontSendNotification);
    setSliderHelp (bandSoft, followGlobal ? "Inherited global Knee/Softness. Choose a per-band clip mode to edit this band's own setting."
        : useKnee ? "Round this band's Hard clipping corner. 0% is an exact hard clip; higher settings begin bending below the band's threshold."
                  : "Soften this band's Smooth clipping curve below its threshold. Classic/Cubic controls the transfer shape.", &bandSoftLabel);
    bandSoft.setDoubleClickReturnValue (true, useKnee ? 0 : 15);
    bandSoft.setEnabled (bandActive && bandCurve.getSelectedId() >= 2); bandSoftLabel.setEnabled (bandSoft.isEnabled());
    crossover.setEnabled (enabled);
    bandMix.setEnabled (bandActive); bandTrim.setEnabled (bandActive);
    bandMixLabel.setEnabled (bandActive); bandTrimLabel.setEnabled (bandActive);
    stageReduction.setText ("WET " + juce::String (processor.getWetGainReductionDb(), 1)
        + "   FINAL " + juce::String (processor.getFinalGainReductionDb(), 1) + " dB", juce::dontSendNotification);
    const bool midSide = processor.parameters.getRawParameterValue ("channelMode")->load() > 0.5f;
    msBias.setEnabled (midSide); msBiasLabel.setEnabled (midSide);
    linkLabel.setText (midSide ? "M/S LINK" : "STEREO LINK", juce::dontSendNotification);
    snapshotA.setToggleState (processor.getSnapshot() == 0, juce::dontSendNotification);
    snapshotB.setToggleState (processor.getSnapshot() == 1, juce::dontSendNotification);
    if (copyFeedbackTicks > 0) --copyFeedbackTicks;
    else copy.setButtonText (processor.getSnapshot() == 0 ? "COPY A > B" : "COPY B > A");
    if (feedbackTicks > 0) --feedbackTicks;
    else match.setButtonText (processor.canMatchOutput() ? (processor.isMatchLimited() ? "MATCH: LIMITED" : "MATCH OUTPUT") : "MATCH: PLAY AUDIO");
    if (! delta.isDown() || ! isShowing()) processor.setDeltaAudition (false);
    if (isShowing()) repaint();
}

void ClipperAdvancedPanel::setBandsView (bool bands)
{
    setTooltip (bands ? "Select a band by its header. Drag cyan split markers horizontally; Shift = fine adjustment. Shaded reduction uses the 0-24 dB scales; the white contour shows the spectrum."
                     : "Details controls. Hover a control for an explanation. Threshold editing temporarily hides this page and restores it when finished.");
    bandsView = bands; processor.setDeltaAudition (false);
    // Explicitly leaving Bands ends Listen; temporary hiding/editor disposal
    // must not change the audio or forget its latched toggle.
    if (! bands) clearBandListen();
    for (auto& button : bandSolo) button.setVisible (bands);
    for (size_t i = 0; i < knobs.size(); ++i) { knobs[i].setVisible (bands && i == 1); labels[i].setVisible (bands && i == 1); }
    for (auto* c : { (juce::Component*) &bandSoft, (juce::Component*) &bandMix, (juce::Component*) &bandTrim,
        (juce::Component*) &bandSoftLabel, (juce::Component*) &bandMixLabel, (juce::Component*) &bandTrimLabel,
        (juce::Component*) &bandCurve, (juce::Component*) &bandEnabled, (juce::Component*) &crossover }) c->setVisible (bands);
    enableBands.setVisible (bands);
    for (auto* c : { (juce::Component*) &liveQuality, (juce::Component*) &renderQuality, (juce::Component*) &curve,
                    (juce::Component*) &channels, (juce::Component*) &channelsLabel,
                    (juce::Component*) &msBias, (juce::Component*) &msBiasLabel,
                    (juce::Component*) &liveLabel, (juce::Component*) &renderLabel, (juce::Component*) &curveLabel,
                    (juce::Component*) &delta, (juce::Component*) &snapshotA, (juce::Component*) &snapshotB,
                    (juce::Component*) &copy, (juce::Component*) &match,
                    (juce::Component*) &release, (juce::Component*) &releaseLabel,
                    (juce::Component*) &stereoLink, (juce::Component*) &linkLabel,
                    (juce::Component*) &gainLink, (juce::Component*) &truePeak, (juce::Component*) &dcFilter,
                    (juce::Component*) &stageReduction }) c->setVisible (! bands);
    processor.setSpectrumEnabled (displayActive && isVisible() && bandsView);
    timerCallback(); resized(); repaint();
}

juce::Rectangle<float> ClipperAdvancedPanel::graphBounds() const
{
    return { 30.0f, 58.0f, (float) getWidth() - 60.0f, (float) juce::jmax (42, getHeight() - 115) };
}

juce::Rectangle<float> ClipperAdvancedPanel::bandHeaderBounds (int band) const
{
    const auto graph = graphBounds();
    const float width = graph.getWidth()/3.0f;
    return { graph.getX()+band*width+2, 33.0f, width-4, 20.0f };
}

float ClipperAdvancedPanel::frequencyX (float hz) const
{
    const auto graph = graphBounds();
    return graph.getX() + graph.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f);
}

void ClipperAdvancedPanel::paint (juce::Graphics& g)
{
    g.setColour (ClipTheme::panel.withAlpha (bandsView ? 1.0f : 0.82f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    g.setColour (ClipTheme::edge); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 7.0f, 1.0f);
    g.setFont (juce::FontOptions (9.0f)); g.setColour (ClipTheme::dim);
    if (! bandsView)
        return;
    auto graph = graphBounds();
    const float lowX = frequencyX (processor.parameters.getRawParameterValue ("lowSplit")->load());
    const float highX = frequencyX (processor.parameters.getRawParameterValue ("highSplit")->load());
    const std::array<float, 4> edges { graph.getX(), lowX, highX, graph.getRight() };
    const bool smooth = processor.parameters.getRawParameterValue ("smoothMode")->load() > 0.5f;
    const bool processing = processor.parameters.getRawParameterValue ("bands")->load() > .5f;
    const std::array<const char*, 3> names { "LOW", "MID", "HIGH" };
    g.setColour (ClipTheme::background); g.fillRect (graph);
    for (int b = 0; b < 3; ++b)
    {
        const auto area = juce::Rectangle<float> (edges[(size_t) b], graph.getY(), edges[(size_t) b + 1] - edges[(size_t) b], graph.getHeight());
        const juce::String prefix = b == 0 ? "low" : b == 1 ? "mid" : "high";
        const bool bandOn = processing && processor.parameters.getRawParameterValue (prefix + "Enabled")->load() > .5f;
        g.setColour (ClipTheme::bandTints[(size_t) b].withAlpha (bandOn ? (b == selectedBand ? .115f : .055f) : .025f)); g.fillRect (area);
    }
    // Frequency markers sit over the band tints and underneath reduction fills.
    g.setFont (juce::FontOptions (8.0f));
    for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const int x = (int) frequencyX (hz);
        const bool major = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
        g.setColour (ClipTheme::dim.withAlpha (major ? .13f : .28f));
        g.drawVerticalLine (x, major ? graph.getY() : graph.getBottom()-3, graph.getBottom());
        g.setColour (ClipTheme::dim.withAlpha (.8f));
        g.drawText (hz >= 1000 ? juce::String (hz/1000, 0)+"k" : juce::String (hz, 0),
            juce::jlimit (9, getWidth()-40, x-16), (int) graph.getBottom()+2, 32, 12, juce::Justification::centred);
    }
    for (float db : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
    {
        const float y = graph.getY() + graph.getHeight() * db / 24.0f;
        g.setColour (ClipTheme::edge.withAlpha (.35f));
        g.drawHorizontalLine (juce::roundToInt (y), graph.getX(), graph.getRight());
    }
    for (int b = 0; b < 3; ++b)
    {
        const auto area = juce::Rectangle<float> (edges[(size_t) b], graph.getY(), edges[(size_t) b + 1] - edges[(size_t) b], graph.getHeight());
        const juce::String prefix = b == 0 ? "low" : b == 1 ? "mid" : "high";
        int shape = (int) processor.parameters.getRawParameterValue (prefix + "Curve")->load();
        if (shape == 0) shape = smooth ? ((int) processor.parameters.getRawParameterValue ("curve")->load() + 2) : 1;
        const auto colour = shape == 1 ? ClipTheme::orange : shape == 2 ? ClipTheme::smoothBlue : ClipTheme::cubicMint;
        const bool bandOn = processing && processor.parameters.getRawParameterValue (prefix + "Enabled")->load() > .5f;
        const float reduction = bandOn ? processor.getBandReduction (b) : 0.0f;
        const auto reduced = area.withHeight (area.getHeight()*juce::jlimit (0.0f, 1.0f, reduction/24.0f));
        if (reduction > .05f)
        {
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (.22f), reduced.getTopLeft(),
                colour.withAlpha (.08f), reduced.getBottomLeft(), false));
            g.fillRect (reduced);
            g.setColour (colour.withAlpha (.7f));
            g.drawLine (area.getX()+1, reduced.getBottom(), area.getRight()-1, reduced.getBottom(), 1.0f);
        }
        const auto header = bandHeaderBounds (b);
        if (b == selectedBand)
        {
            g.setColour (ClipTheme::bandTints[(size_t) b].withAlpha (.10f)); g.fillRoundedRectangle (header, 4);
            g.setColour (ClipTheme::bandTints[(size_t) b].withAlpha (processing ? .75f : .25f));
            g.fillRoundedRectangle (header.getX()+7, header.getBottom()-2, header.getWidth()-14, 1.5f, .75f);
        }
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        g.setColour (ClipTheme::bandTints[(size_t) b].brighter (.25f).withAlpha (bandOn ? 1.0f : .4f));
        g.drawText (names[(size_t) b], (int) header.getX()+7, (int) header.getY(), 32, 18, juce::Justification::centredLeft);
        g.setColour (bandOn ? colour : ClipTheme::dim.withAlpha (.4f));
        g.drawText (juce::String (reduction, 1)+" dB", (int) header.getX()+40, (int) header.getY(),
            (int) header.getWidth()-65, 18, juce::Justification::centredRight);
    }
    juce::Path spectrum;
    const double rate = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000;
    if (! juce::exactlyEqual (spectrumBinWidth, graph.getWidth()) || ! juce::exactlyEqual (spectrumBinRate, rate))
    {
        spectrumBinWidth = graph.getWidth(); spectrumBinRate = rate;
        spectrumBins.resize ((size_t) (int) graph.getWidth());
        for (size_t x = 0; x < spectrumBins.size(); ++x)
            spectrumBins[x] = juce::jlimit (1.0, 2046.0,
                20 * std::pow (1000.0, x / (double) graph.getWidth()) * 4096 / rate);
    }
    spectrum.startNewSubPath (graph.getX(), graph.getBottom());
    for (int x = 0; x < (int) graph.getWidth(); ++x)
    {
        const double bin = spectrumBins[(size_t) x];
        const int lower = (int) bin;
        const float db = juce::jmap ((float) (bin-lower), spectrumLevels[(size_t) lower], spectrumLevels[(size_t) lower+1]);
        const float y = graph.getBottom() - graph.getHeight()*juce::jlimit (0.0f, 1.0f, (db+90)/90);
        spectrum.lineTo (graph.getX()+x, y);
    }
    g.setColour (ClipTheme::text.withAlpha (processing ? .52f : .2f)); g.strokePath (spectrum, juce::PathStrokeType (1));
    for (float x : { lowX, highX })
    {
        g.setColour (ClipTheme::cyan.withAlpha (processing ? .5f : .2f));
        g.drawVerticalLine ((int) x, graph.getY(), graph.getBottom());
        g.setColour (ClipTheme::cyan.withAlpha (processing ? .95f : .3f));
        g.fillEllipse (x-2.5f, graph.getY()-2.5f, 5, 5);
    }
    g.setFont (juce::FontOptions (8.0f));
    for (float db : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
    {
        const float y = graph.getY() + graph.getHeight() * db / 24.0f;
        const int labelY = juce::roundToInt (juce::jlimit (graph.getY(), graph.getBottom()-10.0f, y-5.0f));
        g.setColour (ClipTheme::dim.withAlpha (db == 6.0f || db == 18.0f ? .62f : .9f));
        g.drawText (juce::String (db, 0), 5, labelY, 19, 10, juce::Justification::centredRight);
        g.drawText (juce::String (db, 0), (int) graph.getRight()+6, labelY, 19, 10, juce::Justification::centredLeft);
        g.drawHorizontalLine (juce::roundToInt (y), graph.getX()-4, graph.getX());
        g.drawHorizontalLine (juce::roundToInt (y), graph.getRight(), graph.getRight()+4);
    }
    g.setColour (ClipTheme::dim);
    g.drawText (juce::String (processor.parameters.getRawParameterValue ("lowSplit")->load(), 0) + " Hz  /  "
        + juce::String (processor.parameters.getRawParameterValue ("highSplit")->load()/1000, 2) + " kHz",
        452, 8, getWidth()-464, 20, juce::Justification::centredRight);
}

void ClipperAdvancedPanel::resized()
{
    if (! bandsView)
    {
        const int contentWidth = getWidth() - 20;
        constexpr int utilityWidth = 174, knobWidth = 62;
        const int choicesWidth = contentWidth - utilityWidth - 3 * knobWidth - 16;
        const int cell = (choicesWidth - 6) / 2;
        const std::array<juce::ComboBox*, 4> choices { &liveQuality, &renderQuality, &curve, &channels };
        const std::array<juce::Label*, 4> titles { &liveLabel, &renderLabel, &curveLabel, &channelsLabel };
        for (int i = 0; i < 4; ++i)
        {
            const int x = 10 + (i % 2) * (cell + 6), y = 4 + (i / 2) * 31;
            titles[(size_t) i]->setBounds (x, y, cell, 11);
            choices[(size_t) i]->setBounds (x, y + 11, cell, 20);
        }
        const int knobsX = 10 + choicesWidth + 8;
        msBiasLabel.setBounds (knobsX, 6, knobWidth, 11); msBias.setBounds (knobsX, 17, knobWidth, 62);
        releaseLabel.setBounds (knobsX + knobWidth, 6, knobWidth, 11); release.setBounds (knobsX + knobWidth, 17, knobWidth, 62);
        linkLabel.setBounds (knobsX + knobWidth * 2, 6, knobWidth, 11); stereoLink.setBounds (knobsX + knobWidth * 2, 17, knobWidth, 62);
        for (auto* slider : { &msBias, &release, &stereoLink }) slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth, 14);
        const int utilityX = knobsX + knobWidth * 3 + 8;
        gainLink.setBounds (utilityX, 6, 60, 21); truePeak.setBounds (utilityX + 64, 6, 68, 21);
        dcFilter.setBounds (utilityX + 136, 6, 38, 21);
        delta.setBounds (utilityX, 31, 76, 21); match.setBounds (utilityX + 80, 31, utilityWidth - 80, 21);
        snapshotA.setBounds (utilityX, 56, 26, 21); snapshotB.setBounds (utilityX + 30, 56, 26, 21);
        copy.setBounds (utilityX + 60, 56, utilityWidth - 60, 21);
        stageReduction.setBounds (10, 67, choicesWidth, 14);
        return;
    }
    enableBands.setBounds (10, 6, 94, 24);
    bandEnabled.setBounds (112, 6, 84, 24); bandCurve.setBounds (204, 8, 114, 20);
    crossover.setBounds (326, 8, 118, 20);
    const int width = (getWidth() - 24) / 4;
    const int top = (int) graphBounds().getBottom() + 17;
    for (int b = 0; b < 3; ++b)
    {
        const auto header = bandHeaderBounds (b);
        bandSolo[(size_t) b].setBounds ((int) header.getRight()-22, (int) header.getY(), 20, 18);
    }
    const std::array<juce::Slider*, 4> controls { &knobs[1], &bandSoft, &bandMix, &bandTrim };
    const std::array<juce::Label*, 4> titles { &labels[1], &bandSoftLabel, &bandMixLabel, &bandTrimLabel };
    for (int i = 0; i < 4; ++i)
    {
        titles[(size_t) i]->setBounds (12 + i * width, top, width, 12);
        controls[(size_t) i]->setBounds (16 + i * width, top + 12, width - 8, 23);
    }
}

void ClipperAdvancedPanel::mouseDown (const juce::MouseEvent& e)
{
    if (bandsView && e.mods.isLeftButtonDown())
        for (int b = 0; b < 3; ++b)
            if (bandHeaderBounds (b).contains (e.position)) { selectBand (b); return; }
    if (! bandsView || ! e.mods.isLeftButtonDown() || ! enableBands.getToggleState() || ! graphBounds().expanded (6).contains (e.position)) return;
    for (const auto* id : { "lowSplit", "highSplit" })
        if (std::abs (e.position.x - frequencyX (processor.parameters.getRawParameterValue (id)->load())) < 9.0f)
        { draggedSplit = processor.parameters.getParameter (id); draggedSplit->beginChangeGesture(); break; }
    if (draggedSplit == nullptr)
        selectBand (e.position.x < frequencyX (processor.parameters.getRawParameterValue ("lowSplit")->load()) ? 0
            : e.position.x < frequencyX (processor.parameters.getRawParameterValue ("highSplit")->load()) ? 1 : 2);
}

void ClipperAdvancedPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedSplit == nullptr) return;
    const auto graph = graphBounds();
    const float frequency = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (e.position.x - graph.getX()) / graph.getWidth()));
    draggedSplit->setValueNotifyingHost (draggedSplit->convertTo0to1 (juce::jlimit (draggedSplit->getNormalisableRange().start, draggedSplit->getNormalisableRange().end, frequency)));
    resized(); repaint();
}

void ClipperAdvancedPanel::mouseUp (const juce::MouseEvent&)
{
    if (draggedSplit != nullptr) draggedSplit->endChangeGesture();
    draggedSplit = nullptr;
}

OpenClipperAudioProcessorEditor::OpenClipperAudioProcessorEditor (OpenClipperAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), scope (p), meter (p), advanced (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (juce::jlimit (720, 1200, p.editorState.width.load()), juce::jlimit (440, 760, p.editorState.height.load()));
    setResizable (true, true);
    setResizeLimits (720, 440, 1200, 760);
    addAndMakeVisible (scope);
    addAndMakeVisible (meter);
    scope.onAnimationFrame = [this] (double timeMs) { meter.refresh(); advancePageFade (timeMs); };
    addChildComponent (advanced);
    // The panel constructs its sliders before inheriting the editor's theme.
    // Recreate their text boxes with our numeric-drag label implementation.
    advanced.sendLookAndFeelChange();
    advanced.setComponentID ("advancedPanel");
    undo.setComponentID ("undoButton"); redo.setComponentID ("redoButton");
    meters.setComponentID ("metersButton"); meters.setButtonText ("METERS");
    freeze.setComponentID ("historyFreeze"); freeze.setButtonText ("FREEZE");
    hoverHelp.setComponentID ("hoverHelp"); hoverHelp.setButtonText ("?");
    hoverHelp.setClickingTogglesState (true);
    hoverHelp.setTooltip ("Enable hover explanations. Off by default; saved with this instance.");
    hoverHelp.onClick = [this]
    {
        audioProcessor.editorState.hoverHelp.store (hoverHelp.getToggleState());
        syncHoverHelp(); audioProcessor.editorStateChanged();
    };
    freeze.onClick = [this] { scope.setFrozen (freeze.getToggleState()); };
    undo.setName ("Undo"); redo.setName ("Redo");
    undo.onClick = [this] { audioProcessor.parameters.copyState(); audioProcessor.undoManager.undo(); timerCallback(); };
    redo.onClick = [this] { audioProcessor.parameters.copyState(); audioProcessor.undoManager.redo(); timerCallback(); };
    meters.setClickingTogglesState (true);
    meters.onClick = [this] { scope.setMetersVisible (meters.getToggleState()); audioProcessor.editorStateChanged(); };
    for (auto* b : { (juce::Button*) &undo, (juce::Button*) &redo, (juce::Button*) &hoverHelp, (juce::Button*) &freeze, (juce::Button*) &meters }) addAndMakeVisible (b);
    addMouseListener (this, true);
    details.setButtonText ("DETAILS"); details.setComponentID ("detailsButton");
    bands.setButtonText ("BANDS"); bands.setComponentID ("bandsButton");
    for (auto* button : { &details, &bands })
    {
        button->setClickingTogglesState (true);
        addAndMakeVisible (button);
        button->onClick = [this, button]
        {
            pagesSuspended = false;
            if (button->getToggleState())
                (button == &details ? bands : details).setToggleState (false, juce::dontSendNotification);
            audioProcessor.editorState.page.store (bands.getToggleState() ? 2 : details.getToggleState() ? 1 : 0);
            audioProcessor.editorStateChanged();
            const int page = bands.getToggleState() ? 2 : details.getToggleState() ? 1 : 0;
            // Explicitly leaving Bands ends Listen immediately, not at the
            // end of its visual fade. Temporary threshold suspension does not.
            if (page != 2) advanced.stopBandListen();
            presentAdvancedPage (page, displayActive && isShowing());
        };
    }
    setupKnob (input, inputLabel, "INPUT", " dB");
    setupKnob (threshold, thresholdLabel, "THRESHOLD", " dB");
    // Like the compressor, redraw the transparent knob over its own plot
    // parent rather than a separately repainted overlapping sibling.
    scope.addAndMakeVisible (threshold);
    scope.onThresholdEdit = [this] { thresholdScopeEditing = true; hideAdvancedPages(); };
    scope.onThresholdEditEnd = [this] { thresholdScopeEditing = false; restoreAdvancedPages(); };
    threshold.onDragStart = [this] { thresholdDragging = true; hideAdvancedPages(); };
    threshold.onDragEnd = [this] { thresholdDragging = false; restoreAdvancedPages(); };
    threshold.onValueChange = [this] { if (threshold.hasKeyboardFocus (false)
        && ! thresholdPointerEditing && ! thresholdDragging && ! thresholdTextEditing)
        { thresholdEditUntil = juce::Time::getMillisecondCounterHiRes() + 350; hideAdvancedPages(); } };
    setupKnob (softness, softnessLabel, "SOFTNESS", "%");
    setupKnob (mix, mixLabel, "MIX", "%");
    setupKnob (output, outputLabel, "OUTPUT", " dB");
    setupKnob (ceiling, ceilingLabel, "LIMIT", " dB");
    input.setDoubleClickReturnValue (true, 0.0);
    threshold.setDoubleClickReturnValue (true, 0.0);
    softness.setDoubleClickReturnValue (true, 15.0);
    mix.setDoubleClickReturnValue (true, 100.0);
    output.setDoubleClickReturnValue (true, 0.0);
    ceiling.setDoubleClickReturnValue (true, 0.0);
    mode.setClickingTogglesState (true);
    mode.onStateChange = [this]
    {
        const bool smoothMode = mode.getToggleState();
        mode.setButtonText (smoothMode ? "SMOOTH" : "HARD");
        updateKneeBinding (! smoothMode);
        repaint();
    };
    addAndMakeVisible (mode);
    bypass.setButtonText ("BYPASS");
    bypass.setClickingTogglesState (true);
    addAndMakeVisible (bypass);
    for (auto* button : { (juce::Button*) &undo, (juce::Button*) &redo, (juce::Button*) &hoverHelp, (juce::Button*) &freeze,
        (juce::Button*) &meters, (juce::Button*) &details, (juce::Button*) &bands,
        (juce::Button*) &mode, (juce::Button*) &bypass }) button->getProperties().set ("topBar", true);
    auto& state = audioProcessor.parameters;
    input.setRange (-24, 24, 0.01); input.setDoubleClickReturnValue (true, 0);
    linkedInputAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter ("input"), [this] (float value)
    { previousInputDb = value; input.setValue (value, juce::dontSendNotification); });
    linkedInputAttachment->sendInitialUpdate();
    input.onDragStart = [this]
    {
        inputGesture = true; linkedInputAttachment->beginGesture();
        audioProcessor.parameters.getParameter ("output")->beginChangeGesture();
    };
    input.onDragEnd = [this]
    {
        inputGesture = false; linkedInputAttachment->endGesture();
        audioProcessor.parameters.getParameter ("output")->endChangeGesture();
    };
    input.onValueChange = [this]
    {
        const float value = (float) input.getValue(), difference = value - previousInputDb;
        if (std::abs (difference) < 0.0001f) return;
        if (! inputGesture) { linkedInputAttachment->beginGesture(); audioProcessor.parameters.getParameter ("output")->beginChangeGesture(); }
        previousInputDb = value;
        audioProcessor.setLinkedInputFromEditor (difference);
        linkedInputAttachment->setValueAsPartOfGesture (value);
        if (! inputGesture) { linkedInputAttachment->endGesture(); audioProcessor.parameters.getParameter ("output")->endChangeGesture(); }
    };
    thresholdAttachment = std::make_unique<SliderAttachment> (state, "threshold", threshold);
    softnessAttachment = std::make_unique<SliderAttachment> (state, "softness", softness);
    mixAttachment = std::make_unique<SliderAttachment> (state, "mix", mix);
    outputAttachment = std::make_unique<SliderAttachment> (state, "output", output);
    ceilingAttachment = std::make_unique<SliderAttachment> (state, "ceiling", ceiling);
    modeAttachment = std::make_unique<ButtonAttachment> (state, "smoothMode", mode);
    bypassAttachment = std::make_unique<ButtonAttachment> (state, "bypass", bypass);
    for (auto* slider : { &input, &threshold, &softness, &mix, &output, &ceiling })
    {
        const int decimals = slider == &softness || slider == &mix ? 1 : 2;
        slider->textFromValueFunction = [decimals] (double value)
        {
            return juce::String (std::abs (value) < (decimals == 2 ? 0.005 : 0.05) ? 0.0 : value,
                                 decimals);
        };
        slider->valueFromTextFunction = [slider] (const juce::String& inputText)
        {
            auto text = inputText.trim().replaceCharacter (0x2212, '-').replaceCharacter (',', '.');
            const auto unit = slider->getTextValueSuffix().trim();
            if (text.endsWithIgnoreCase (unit)) text = text.dropLastCharacters (unit.length()).trim();
            const auto* start = text.toRawUTF8();
            char* end = nullptr;
            const double value = std::strtod (start, &end);
            if (end == start || *end != '\0' || ! std::isfinite (value)) return slider->getValue();
            return juce::jlimit (slider->getMinimum(), slider->getMaximum(), value);
        };
        slider->updateText();
    }
    mode.onStateChange();
    undo.setTooltip ("Undo the last sound-setting edit."); redo.setTooltip ("Redo the last undone sound-setting edit.");
    freeze.setTooltip ("Freeze the eight-second history. Once frozen, click or drag the white cursor to inspect past output and gain reduction.");
    meters.setTooltip ("Show output peak, true peak, momentary LUFS and crest factor in the graph. Click the readout to reset held peaks.");
    details.setTooltip ("Show secondary quality, curve, M/S, release, link and utility controls. Temporarily hides during threshold editing.");
    bands.setTooltip ("Show the three-band spectrum and controls. A light tint when closed means three-band processing is enabled.");
    mode.setTooltip ("Switch Hard/Smooth. Hard uses the Knee control; Smooth adds 30 samples of lookahead and uses Softness and Release.");
    bypass.setTooltip ("Pass the original input aligned to the reported plugin latency. Dimmed threshold line indicates bypass.");
    scope.setTooltip ("Drag the cyan threshold line or badge; Shift = fine adjustment. Click the maximum above the GR strip to reset. Freeze first to inspect or drag the history cursor.");
    setSliderHelp (input, "Input gain before clipping. With Gain Link enabled, editor changes apply the opposite Output gain.", &inputLabel);
    setSliderHelp (threshold, "Wet clipping threshold. Drag this knob, its value, or the cyan line. Dry signal can exceed it when Mix is below 100%; Final Limit still caps the final output.", &thresholdLabel);
    setSliderHelp (mix, "Blend original dry audio with the clipped wet signal. Both paths are latency-aligned; Final Limit still acts at 0% wet.", &mixLabel);
    setSliderHelp (output, "Output gain after the wet/dry mix and before Final Limit.", &outputLabel);
    setSliderHelp (ceiling, "Final output limit after Mix and Output gain. True Peak adds reconstruction-aware protection when enabled.", &ceilingLabel);
    for (auto* child : threshold.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            label->onEditorShow = [this] { thresholdTextEditing = true; hideAdvancedPages(); };
            label->onEditorHide = [this] { thresholdTextEditing = false; thresholdEditUntil = 0; restoreAdvancedPages(); };
        }
    restoreViewState();
    cacheControls (*this);
    cacheControls (scope);
    cacheControls (advanced);
    viewInitialized = true;
    timerCallback();
    startTimerHz (30);
    resized();
}

OpenClipperAudioProcessorEditor::~OpenClipperAudioProcessorEditor()
{
    stopTimer();
    pageFading = false;
    scope.setDisplayActive (false);
    advanced.setDisplayActive (false);
    tooltips.reset();
    scope.onAnimationFrame = {};
    viewInitialized = false; // Disposal/layout callbacks must not overwrite a just-loaded view.
    for (auto* slider : { &input, &threshold, &softness, &mix, &output, &ceiling }) finishSliderInteraction (*slider);
    // Momentary controls/gestures are not a saved state. Latched Listen is.
    audioProcessor.setDeltaAudition (false);
    if (inputGesture)
    {
        linkedInputAttachment->endGesture();
        audioProcessor.parameters.getParameter ("output")->endChangeGesture();
        inputGesture = false;
    }
    removeMouseListener (this);
    setLookAndFeel (nullptr);
}

void OpenClipperAudioProcessorEditor::setDisplayActive (bool enabled)
{
    if (displayActive == enabled) return;
    displayActive = enabled;
    advanced.setDisplayActive (enabled);
    scope.setDisplayActive (enabled); meter.settleAppearance();
    if (! enabled)
    {
        for (auto* slider : { &input, &threshold, &softness, &mix, &output, &ceiling }) finishSliderInteraction (*slider);
        thresholdPointerEditing = thresholdDragging = thresholdTextEditing = thresholdScopeEditing = false;
        thresholdEditUntil = 0;
        restoreAdvancedPages();
        presentAdvancedPage (pagesSuspended ? 0 : bands.getToggleState() ? 2 : details.getToggleState() ? 1 : 0, false);
    }
    freeze.setToggleState (false, juce::dontSendNotification);
    // A retained/minimised host editor only polls for visibility at 2 Hz;
    // its history/VBlank and panel timers are detached while hidden.
    startTimerHz (enabled ? 30 : 2);
}

void OpenClipperAudioProcessorEditor::visibilityChanged()
{
    if (viewInitialized) setDisplayActive (isVisible());
}

void OpenClipperAudioProcessorEditor::parentHierarchyChanged()
{
    if (viewInitialized && getPeer() != nullptr) setDisplayActive (isShowing());
}

void OpenClipperAudioProcessorEditor::restoreViewState()
{
    restoredViewRevision = audioProcessor.editorState.revision.load();
    const int page = audioProcessor.editorState.page.load();
    details.setToggleState (page == 1, juce::dontSendNotification);
    bands.setToggleState (page == 2, juce::dontSendNotification);
    freeze.setToggleState (audioProcessor.editorState.frozen.load(), juce::dontSendNotification);
    meters.setToggleState (audioProcessor.editorState.meters.load(), juce::dontSendNotification);
    syncHoverHelp();
    scope.restoreViewState();
    pagesSuspended = false; thresholdEditUntil = 0;
    advanced.setBandsView (page == 2);
    setSize (audioProcessor.editorState.width.load(), audioProcessor.editorState.height.load());
    presentAdvancedPage (page, false); // size recall precedes this helper's layout/save callbacks
    resized(); repaint();
}

void OpenClipperAudioProcessorEditor::hideAdvancedPages()
{
    if (pagesSuspended || (! details.getToggleState() && ! bands.getToggleState())) return;
    pagesSuspended = true;
    presentAdvancedPage (0, displayActive && isShowing());
}

void OpenClipperAudioProcessorEditor::restoreAdvancedPages()
{
    if (restoredViewRevision != audioProcessor.editorState.revision.load()) return;
    if (! pagesSuspended || thresholdPointerEditing || thresholdDragging || thresholdTextEditing
        || thresholdScopeEditing || juce::Time::getMillisecondCounterHiRes() < thresholdEditUntil) return;
    pagesSuspended = false;
    presentAdvancedPage (bands.getToggleState() ? 2 : details.getToggleState() ? 1 : 0, displayActive && isShowing());
}

void OpenClipperAudioProcessorEditor::updatePageMouseAccess()
{
    const bool interactive = requestedAdvancedPage != 0 && requestedAdvancedPage == presentedAdvancedPage
        && ! pagesSuspended && advanced.getAlpha() > 0.05f;
    advanced.setInterceptsMouseClicks (interactive, interactive);
}

void OpenClipperAudioProcessorEditor::showAdvancedPage (int page)
{
    presentedAdvancedPage = page;
    if (page != 0)
    {
        if (advanced.isBandsView() != (page == 2)) advanced.setBandsView (page == 2);
        advanced.setVisible (true);
    }
    else advanced.setVisible (false);
    resized(); updatePageMouseAccess(); repaint();
}

void OpenClipperAudioProcessorEditor::presentAdvancedPage (int page, bool animate)
{
    page = juce::jlimit (0, 2, page);
    if (animate && requestedAdvancedPage == page && pageFading) return;
    requestedAdvancedPage = page;
    scope.setBadgePageVisible(page==1,animate);
    if (! animate)
    {
        pageFading = false;
        advanced.setAlpha (page == 0 ? 0.0f : 1.0f);
        showAdvancedPage (page);
        return;
    }
    if (presentedAdvancedPage == 0 && page != 0)
    {
        advanced.setAlpha (0);
        showAdvancedPage (page);
    }
    pageFadeFrom = advanced.getAlpha();
    pageFadeTo = page != 0 && page == presentedAdvancedPage ? 1.0f : 0.0f;
    pageFadeStart = juce::Time::getMillisecondCounterHiRes();
    pageFadeDuration = pageFadeTimeMs * std::abs (pageFadeTo - pageFadeFrom);
    pageFading = true;
    if (pageFadeDuration <= 0.001)
    {
        // A second click can arrive before the first visible fade-in frame.
        // Complete the zero-distance exit so the newest page cannot get stuck.
        pageFadeDuration = 1;
        advancePageFade (pageFadeStart + 1);
    }
    updatePageMouseAccess();
}

void OpenClipperAudioProcessorEditor::advancePageFade (double timeMs)
{
    if (! pageFading || ! displayActive) return;
    const float progress = (float) juce::jlimit (0.0, 1.0, (timeMs - pageFadeStart) / pageFadeDuration);
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    advanced.setAlpha (pageFadeFrom + (pageFadeTo - pageFadeFrom) * eased);
    updatePageMouseAccess();
    if (progress < 1.0f) return;
    pageFading = false;
    advanced.setAlpha (pageFadeTo);
    if (pageFadeTo < 0.5f)
    {
        showAdvancedPage (0);
        if (requestedAdvancedPage != 0)
        {
            // Switch content only while invisible, keeping its own layout
            // throughout fade-out rather than resizing the outgoing page.
            showAdvancedPage (requestedAdvancedPage);
            pageFadeFrom = 0; pageFadeTo = 1;
            pageFadeStart = timeMs; pageFadeDuration = pageFadeTimeMs; pageFading = true;
        }
    }
}

void OpenClipperAudioProcessorEditor::timerCallback()
{
    if (getPeer() != nullptr)
    {
        setDisplayActive (isShowing());
        if (! displayActive) return;
    }
    if (! displayActive) return;
    if (restoredViewRevision != audioProcessor.editorState.revision.load()) restoreViewState();
    syncHoverHelp();
    updateKneeBinding (audioProcessor.parameters.getRawParameterValue ("smoothMode")->load() < .5f);
    restoreAdvancedPages();
    undo.setEnabled (audioProcessor.undoManager.canUndo()); redo.setEnabled (audioProcessor.undoManager.canRedo());
    const bool enabled = audioProcessor.parameters.getRawParameterValue ("bands")->load() > 0.5f;
    if (static_cast<bool> (bands.getProperties().getWithDefault ("processingEnabled", false)) != enabled)
    {
        bands.getProperties().set ("processingEnabled", enabled);
        bands.repaint();
    }
}

void OpenClipperAudioProcessorEditor::syncHoverHelp()
{
    const bool enabled = audioProcessor.editorState.hoverHelp.load();
    hoverHelp.setToggleState (enabled, juce::dontSendNotification);
    if (enabled && ! tooltips) tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
    else if (! enabled && tooltips) tooltips.reset();
}

void OpenClipperAudioProcessorEditor::updateKneeBinding (bool useKnee)
{
    if (softnessAttachment && softnessUsesKnee == useKnee) return;
    if (softnessAttachment && softnessUsesKnee != useKnee)
    {
        softnessAttachment.reset(); softnessUsesKnee = useKnee;
        const auto format = softness.textFromValueFunction;
        const auto parse = softness.valueFromTextFunction;
        softnessAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, useKnee ? "hardKnee" : "softness", softness);
        softness.textFromValueFunction = format; softness.valueFromTextFunction = parse; softness.updateText();
    }
    softness.setEnabled (true); softnessLabel.setEnabled (true);
    softnessLabel.setText (useKnee ? "KNEE" : "SOFTNESS", juce::dontSendNotification);
    setSliderHelp (softness, useKnee ? "Round the Hard clipping corner. 0% is an exact hard clip; higher settings start bending up to 6 dB below the threshold."
        : "Soften the Smooth transfer below its threshold. Smooth also uses 30-sample lookahead and the Release control.", &softnessLabel);
    softness.setName (useKnee ? "Hard knee" : "Softness");
    softness.setDoubleClickReturnValue (true, useKnee ? 0 : 15);
}

void OpenClipperAudioProcessorEditor::setupKnob (juce::Slider& slider, juce::Label& label,
                                                  const juce::String& title, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setName (title);
    slider.setComponentID (title.toLowerCase() + "Control");
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    slider.setTextValueSuffix (suffix);
    slider.setMouseDragSensitivity (180);
    addAndMakeVisible (slider);
    label.setText (title, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, ClipTheme::dim);
    label.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    addAndMakeVisible (label);
}

void OpenClipperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ClipTheme::background);
    g.setColour (ClipTheme::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("OPEN CLIPPER", 18, 7, 190, 25, juce::Justification::centredLeft);
    g.setColour (ClipTheme::purple);
    g.fillRoundedRectangle (18.0f, 35.0f, 42.0f, 2.5f, 1.25f);
}

void OpenClipperAudioProcessorEditor::resized()
{
    if (viewInitialized && restoredViewRevision == audioProcessor.editorState.revision.load())
    {
        const int oldWidth = audioProcessor.editorState.width.exchange (getWidth());
        const int oldHeight = audioProcessor.editorState.height.exchange (getHeight());
        if (oldWidth != getWidth() || oldHeight != getHeight()) audioProcessor.editorStateChanged();
    }
    const int w = getWidth();
    const int h = getHeight();
    constexpr int meterWidth = 66;
    const auto scopeArea = juce::Rectangle<int> (14, 48, w - meterWidth - 30, h - 62);
    const auto meterArea = juce::Rectangle<int> (w - meterWidth - 12, 48, meterWidth, h - 62);
    scope.setBounds (scopeArea);
    meter.setBounds (meterArea.withY(meterArea.getY()-14).withHeight(meterArea.getHeight()+14));
    int toolbarX = w - 544;
    const std::array<juce::Button*, 9> buttons { &undo, &redo, &hoverHelp, &freeze, &meters, &details, &bands, &mode, &bypass };
    const std::array<int, 9> widths { 24, 24, 24, 62, 62, 72, 64, 72, 90 };
    for (size_t i = 0; i < buttons.size(); ++i) { buttons[i]->setBounds (toolbarX, 7, widths[i], 28); toolbarX += widths[i]+4; }

    const int line24Y = scopeArea.getY() + juce::roundToInt (scopeArea.getHeight() * 24.0f / 36.0f);
    const int controlsTop = juce::jmax (scopeArea.getBottom() - 145, line24Y + 6);
    if (advanced.isBandsView())
        advanced.setBounds (scopeArea.getX() + 22, scopeArea.getY() + 4,
                            scopeArea.getWidth() - 30, line24Y - scopeArea.getY() - 8);
    else
        advanced.setBounds (scopeArea.getX() + 22, line24Y - 90,
                            scopeArea.getWidth() - 30, 86);
    scope.setControlsTop ((! advanced.isBandsView() && advanced.isVisible() ? advanced.getY() : controlsTop) - scopeArea.getY());
    scope.setBadgeGeometry(controlsTop-scopeArea.getY(),line24Y-90-scopeArea.getY());
    scope.setInspectionHidden (advanced.isBandsView() && advanced.isVisible());
    auto controls = juce::Rectangle<int> (scopeArea.getX() + 8, controlsTop,
                                          scopeArea.getWidth() - 16, scopeArea.getBottom() - 11 - controlsTop);
    const int width = controls.getWidth() / 6;
    auto place = [&] (juce::Slider& slider, juce::Label& label)
    {
        auto cell = controls.removeFromLeft (width).reduced (6, 0);
        label.setBounds (cell.removeFromTop (15));
        slider.setBounds (slider.getParentComponent() == &scope ? cell.translated (-scopeArea.getX(), -scopeArea.getY()) : cell);
    };
    place (threshold, thresholdLabel);
    place (input, inputLabel);
    place (softness, softnessLabel);
    place (mix, mixLabel);
    place (output, outputLabel);
    place (ceiling, ceilingLabel);
}

juce::Colour ClipperLookAndFeel::valueAccent(juce::Component& c)
{ if(auto* slider=c.findParentComponentOfClass<juce::Slider>()){
        if(slider->getComponentID().startsWith("band"))return slider->findColour(juce::Slider::trackColourId);
        return slider->getName()=="THRESHOLD"?ClipTheme::cyan:ClipTheme::purple;
    }return ClipTheme::purple; }
void ClipperLookAndFeel::fillTextEditorBackground(juce::Graphics& g,int w,int h,juce::TextEditor& editor)
{
    if(OpenEditableValueStyle::isValueEditor(editor))valueStyle.observeEditor(editor);
    if(OpenEditableValueStyle::isValueEditor(editor))OpenEditableValueStyle::fillEditor(g,w,h,ClipTheme::background);
    else juce::LookAndFeel_V4::fillTextEditorBackground(g,w,h,editor);
}
void ClipperLookAndFeel::drawTextEditorOutline(juce::Graphics& g,int w,int h,juce::TextEditor& editor)
{
    if(OpenEditableValueStyle::isValueEditor(editor))OpenEditableValueStyle::outlineEditor(g,w,h,editor,valueAccent(editor),ClipTheme::edge);
    else juce::LookAndFeel_V4::drawTextEditorOutline(g,w,h,editor);
}
