#include "GateEditor.h"
#include "../Shared/PlotStroke.h"
#include "../Shared/FrozenInspection.h"
#include <cstdlib>

namespace GateTheme
{
const auto background = juce::Colour::fromRGB (18, 10, 27);
const auto panel = juce::Colour::fromRGB (31, 18, 44);
const auto panelRaised = juce::Colour::fromRGB (44, 25, 58);
const auto grid = juce::Colour::fromRGB (74, 47, 89);
const auto dim = juce::Colour::fromRGB (164, 137, 176);
const auto text = juce::Colour::fromRGB (218, 205, 231);
const auto violet = juce::Colour::fromRGB (181, 112, 255);
const auto peakTrace = juce::Colour::fromRGB (255, 190, 117);
const auto magenta = juce::Colour::fromRGB (255, 92, 190);
const auto waveInput = juce::Colour::fromRGB (207, 105, 214);
const auto waveOutput = juce::Colour::fromRGB (255, 126, 208);
const auto sidechain = juce::Colour::fromRGB (83, 212, 228);
const auto mint = juce::Colour::fromRGB (104, 235, 190);
const auto red = juce::Colour::fromRGB (255, 91, 107);
const auto advanced = juce::Colour::fromRGB (27, 23, 33);
const auto edge = juce::Colour::fromRGB (61, 53, 72);
const auto control = juce::Colour::fromRGB (20, 15, 28);
const auto selected = juce::Colour::fromRGB (73, 47, 103);
const auto dialSurround = juce::Colour::fromRGB (47, 33, 63);
const auto dialTrack = juce::Colour::fromRGB (69, 48, 88);
}

namespace GateLayout
{
static juce::Rectangle<int> detailsPanel (juce::Rectangle<int> displayBounds)
{
    const int line = displayBounds.getY() + juce::roundToInt (displayBounds.getHeight() * 2.0f / 3.0f);
    // Exact Clipper Details bounds: 22 px left / 8 px right; 86 px tall.
    return { displayBounds.getX() + 22, line - 90, displayBounds.getWidth() - 30, 86 };
}
}

namespace
{
double parseSliderValue (juce::Slider& slider, juce::String text)
{
    text = text.trim().replaceCharacter (0x2212, '-').replaceCharacter (',', '.');
    const auto unit = slider.getTextValueSuffix().trim();
    if (unit.isNotEmpty() && text.endsWithIgnoreCase (unit)) text = text.dropLastCharacters (unit.length()).trim();
    const bool kilo = unit == "Hz" && text.endsWithIgnoreCase ("k");
    if (kilo) text = text.dropLastCharacters (1).trim();
    const auto* start = text.toRawUTF8(); char* end = nullptr;
    const double value = std::strtod (start, &end) * (kilo ? 1000.0 : 1.0);
    return end == start || *end != '\0' || ! std::isfinite (value) ? slider.getValue()
        : juce::jlimit (slider.getMinimum(), slider.getMaximum(), value);
}

class GateValueLabel final : public juce::Label
{
public:
    explicit GateValueLabel (juce::Slider& owner) : slider (owner) {}
    void cancelInteraction() { armed = false; gesture.reset(); hideEditor (true); }
    void mouseDown (const juce::MouseEvent& event) override
    {
        gesture.reset();
        armed = isEnabled() && event.mods.isLeftButtonDown() && ! isBeingEdited();
        originY = event.position.y;
        start = slider.valueToProportionOfLength (slider.getValue());
        fine = event.mods.isShiftDown();
    }
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! armed) return;
        if (gesture && fine != event.mods.isShiftDown())
        {
            originY = event.position.y;
            start = slider.valueToProportionOfLength (slider.getValue());
        }
        fine = event.mods.isShiftDown();
        const float distance = originY - event.position.y;
        if (! gesture && std::abs (distance) < 3.0f) return;
        if (! gesture) gesture = std::make_unique<juce::Slider::ScopedDragNotification> (slider);
        const double sensitivity = event.mods.isShiftDown() ? 0.1 : 1.0;
        slider.setValue (slider.proportionOfLengthToValue (juce::jlimit (0.0, 1.0,
            start + distance * sensitivity / 200.0)), juce::sendNotificationSync);
    }
    void mouseUp (const juce::MouseEvent& event) override
    {
        const bool dragged = gesture != nullptr;
        gesture.reset(); armed = false;
        if (! dragged && isEnabled() && isEditableOnSingleClick() && getLocalBounds().contains (event.getPosition())
            && ! event.mouseWasDraggedSinceMouseDown() && ! event.mods.isPopupMenu()) showEditor();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
private:
    juce::Slider& slider;
    std::unique_ptr<juce::Slider::ScopedDragNotification> gesture;
    bool armed = false, fine = false;
    float originY = 0;
    double start = 0;
};

void finishSliderInteraction (juce::Slider& slider)
{
    for (auto* child : slider.getChildren())
        if (auto* value = dynamic_cast<GateValueLabel*> (child)) value->cancelInteraction();
        else if (auto* label = dynamic_cast<juce::Label*> (child)) label->hideEditor (true);
    const auto time = juce::Time::getCurrentTime();
    const juce::Point<float> point {};
    slider.mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys {}, 1, 0, 0, 0, 0, &slider, &slider, time, point, time, 1, false));
}
}

GateLookAndFeel::GateLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, GateTheme::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, GateTheme::text);
    setColour (juce::PopupMenu::backgroundColourId, GateTheme::panel);
    setColour (juce::PopupMenu::textColourId, GateTheme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, GateTheme::violet.withAlpha (0.35f));
}

void GateLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float position, float start, float end, juce::Slider& slider)
{
    juce::Graphics::ScopedSaveState state (g);
    g.beginTransparencyLayer (slider.isEnabled() ? 1.0f : 0.28f);
    const bool detail = (bool) slider.getProperties()["detailKnob"];
    const auto centre = juce::Point<float> (x + width * 0.5f, y + height * (detail ? 0.5f : 0.45f));
    const float radius = juce::jmin (width, height) * (detail ? 0.32f : 0.34f);
    const float angle = start + position * (end - start);
    const float surround = detail ? 6.0f : 8.0f;
    g.setColour (detail ? GateTheme::dialSurround.withAlpha (0.84f) : GateTheme::panelRaised.withAlpha (0.78f));
    g.fillEllipse (centre.x - radius - surround, centre.y - radius - surround,
                   (radius + surround) * 2.0f, (radius + surround) * 2.0f);
    juce::Path base, active;
    base.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    active.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
    g.setColour (detail ? GateTheme::dialTrack : GateTheme::grid);
    g.strokePath (base, juce::PathStrokeType (4.0f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    const auto accent = slider.getName() == "THRESHOLD" ? GateTheme::magenta
        : slider.getName() == "SC LP" ? GateTheme::sidechain : GateTheme::violet;
    g.setColour (accent.withAlpha (detail ? 0.9f : 1.0f));
    g.strokePath (active, juce::PathStrokeType (4.0f,
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    g.setColour (detail ? GateTheme::control : GateTheme::panel);
    g.fillEllipse (centre.x - radius + 6.0f, centre.y - radius + 6.0f,
                   (radius - 6.0f) * 2.0f, (radius - 6.0f) * 2.0f);
    const auto tip = centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * radius * 0.58f;
    g.setColour (GateTheme::text.withAlpha (detail ? 0.88f : 1.0f));
    g.drawLine ({ centre, tip }, 2.0f);
    g.endTransparencyLayer();
}

void GateLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool over, bool down)
{
    juce::Graphics::ScopedSaveState state (g);
    g.beginTransparencyLayer (button.isEnabled() ? 1.0f : 0.32f);
    auto area = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();
    const bool topBar = (bool) button.getProperties()["topBar"];
    const float corner = topBar ? 5.0f : 6.0f;
    const bool externalIndicator = button.getButtonText() == "SC"
                                  && (bool) button.getProperties()["externalActive"];
    const bool auditionIndicator = button.getButtonText() == "SC"
                                  && (bool) button.getProperties()["auditionActive"];
    auto fill = on ? (topBar ? GateTheme::violet : GateTheme::selected)
                   : auditionIndicator ? GateTheme::sidechain.withAlpha (0.32f)
                   : externalIndicator ? GateTheme::violet.withAlpha (0.32f)
                                       : topBar ? GateTheme::panelRaised : GateTheme::advanced;
    if (button.getButtonText() == "BYPASS" && on)
        fill = GateTheme::red;
    const bool detectorTrace = button.getComponentID() == "detectorView";
    const auto traceColour = (bool) button.getProperties()["peakMode"] ? GateTheme::peakTrace : GateTheme::violet;
    if (detectorTrace && on) fill = traceColour.withAlpha (0.26f);
    if (over) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (area, corner);
    if (! topBar)
    {
        g.setColour (GateTheme::edge); g.drawRoundedRectangle (area, corner, 1.0f);
    }
    g.setColour (on ? (topBar ? GateTheme::background : GateTheme::text)
                   : auditionIndicator ? GateTheme::sidechain : externalIndicator ? GateTheme::violet : GateTheme::dim);
    if (detectorTrace && on) g.setColour (traceColour);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    if (auditionIndicator)
    {
        g.setColour (GateTheme::sidechain);
        g.drawLine (area.getX() + 7, area.getBottom() - 3, area.getRight() - 7, area.getBottom() - 3, 1.0f);
    }
    g.endTransparencyLayer();
}

void GateLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down)
{
    const bool topBar = (bool) button.getProperties()["topBar"];
    const float corner = topBar ? 4.0f : 6.0f;
    const bool auditioning = button.getComponentID() == "removedAudition" && down;
    auto colour = auditioning ? GateTheme::sidechain : button.getToggleState()
        ? GateTheme::selected : topBar ? GateTheme::panelRaised : GateTheme::advanced;
    if (over) colour = colour.brighter (0.08f);
    if (down) colour = colour.darker (0.08f);
    g.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.32f));
    g.fillRoundedRectangle (button.getLocalBounds().toFloat().reduced (1), corner);
    if (! topBar)
    {
        g.setColour (GateTheme::edge.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.32f));
        g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (1), corner, 1);
    }
}

void GateLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const bool auditioning = button.getComponentID() == "removedAudition" && down;
    g.setColour ((auditioning ? GateTheme::background : button.getToggleState() ? GateTheme::text : GateTheme::dim)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.32f));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (3, 0), juce::Justification::centred);
}

void GateLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    valueStyle.paintLabel(g,label,GateTheme::background,valueAccent(label));
    if (label.isBeingEdited())
        return;
    g.setColour (label.findColour (juce::Label::textColourId)
                       .withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.3f));
    g.setFont (label.getFont());
    g.drawFittedText (label.getText(), label.getLocalBounds(), label.getJustificationType(),
                      (bool) label.getProperties()["wrapped"] ? 3 : 1,
                      label.getMinimumHorizontalScale());
}

void GateLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const float alpha = box.isEnabled() ? 1.0f : 0.28f;
    g.setColour (GateTheme::control.withAlpha (alpha));
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 5);
    g.setColour (GateTheme::edge.withAlpha (alpha));
    g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 5, 1);
    g.setColour (GateTheme::sidechain.withAlpha (alpha));
    juce::Path arrow;
    const float arrowX = w - 18.0f, arrowY = h * 0.5f;
    // Give the chevron a proper right inset and centre it vertically, instead
    // of crowding the rounded border. The existing text area stays unchanged.
    arrow.startNewSubPath (arrowX - 6.0f, arrowY - 2.5f);
    arrow.lineTo (arrowX, arrowY + 2.5f); arrow.lineTo (arrowX + 6.0f, arrowY - 2.5f);
    g.strokePath (arrow, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font GateLookAndFeel::getComboBoxFont (juce::ComboBox&) { return juce::Font (juce::FontOptions (10.5f)); }

juce::Label* GateLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = new GateValueLabel (slider);
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::textColourId, GateTheme::text.withAlpha (0.88f));
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::textColourId, GateTheme::text);
    label->setColour (juce::TextEditor::backgroundColourId, GateTheme::panelRaised);
    label->setFont (juce::FontOptions ((bool) slider.getProperties()["detailKnob"] ? 9.5f : 14.0f));
    if (slider.getName() == "THRESHOLD" || slider.getName() == "HYSTERESIS")
    {
        label->onEditorShow = [&slider]
        {
            if (auto* editor = slider.findParentComponentOfClass<OpenGateAudioProcessorEditor>()) editor->referenceTextEdit (true);
        };
        label->onEditorHide = [&slider]
        {
            if (auto* editor = slider.findParentComponentOfClass<OpenGateAudioProcessorEditor>()) editor->referenceTextEdit (false);
        };
    }
    return label;
}

GateDisplay::GateDisplay (OpenGateAudioProcessor& p) : processor (p)
{
    closeBadgeFade.setBypassed (true, 0, false);
    for (auto* fade : { &sidechainFade, &detectorFade, &focusFade, &kneeFade })
        fade->setBypassed (true, 0, false);
    // This history overlaps transparent sibling controls. Let JUCE repaint the
    // shared background instead of treating it as an independent opaque layer.
    threshold = processor.parameters.getParameter ("threshold");
    // Match Compressor: a knob/text/host change invalidates the entire old
    // and new reference lines immediately, rather than waiting for the timer.
    const char* ids[] { "threshold", "hysteresis", "flip" };
    float* values[] { &displayedThreshold, &displayedHysteresis, &displayedFlip };
    for (size_t i = 0; i < referenceAttachments.size(); ++i)
    {
        referenceAttachments[i] = std::make_unique<juce::ParameterAttachment>
            (*processor.parameters.getParameter (ids[i]), [this, value = values[i], i] (float v)
            {
                *value = v;
                if (i == 2)
                    hysteresisFlipFade.request (v > .5f ? 2 : 1,
                        juce::Time::getMillisecondCounterHiRes(), displayActive);
                repaint();
            });
        referenceAttachments[i]->sendInitialUpdate();
    }
    bypassDisplayAttachment=std::make_unique<juce::ParameterAttachment>(*processor.parameters.getParameter("bypass"),[this](float value)
    {
        bypassFade.setBypassed(value>.5f,juce::Time::getMillisecondCounterHiRes(),displayActive);
        repaint();
    });
    bypassDisplayAttachment->sendInitialUpdate();
    setComponentID ("historyDisplay");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setDisplayActive (true);
}

GateDisplay::~GateDisplay()
{
    setDisplayActive (false);
    if (dragging && dragParameter != nullptr) dragParameter->endChangeGesture();
}

float GateDisplay::thresholdY() const
{
    return dbToY (displayedThreshold);
}

float GateDisplay::dbToY (float db) const
{
    return juce::jmap (juce::jlimit (-80.0f, 0.0f, db), -80.0f, 0.0f, (float) getHeight(), 0.0f);
}

float GateDisplay::hysteresisY() const
{
    const float direction = hysteresisFlipFade.presented() == 2 ? 1 : -1;
    return dbToY (displayedThreshold + direction * displayedHysteresis);
}

bool GateDisplay::hitHysteresis (juce::Point<float> point) const
{
    if (! hysteresisFlipFade.interactive()) return false;
    const float direction = hysteresisFlipFade.presented() == 2 ? 1 : -1;
    const float closeDb = displayedThreshold + direction * displayedHysteresis;
    return closeDb >= -80 && closeDb <= 0 && std::abs (point.y - hysteresisY()) < 7;
}

double GateHistoryClock::advance (uint64_t sequence, uint64_t epoch, double now, double rate, double delay)
{
    const double latest = (double) sequence;
    const double elapsed = lastTime < 0 ? 0 : juce::jmax (0.0, now - lastTime);
    const bool audioAdvanced = sequence != lastSequence;
    if (audioAdvanced) lastAudioTime = now;
    if (position < 0 || epoch != lastEpoch || sequence < lastSequence)
    {
        position = juce::jmax (0.0, latest - delay);
        running = latest >= delay;
        lastAudioTime = now;
    }
    else if (elapsed > 250 || latest - position > 192)
    {
        // A UI stall can skip ahead, but cannot rewind a stopped viewport.
        position = juce::jmax (position, juce::jmax (0.0, latest - delay));
        running = latest - position >= delay;
    }
    else if (! running)
        running = latest - position >= delay;
    else
    {
        position = juce::jmin (latest, position + elapsed * rate / 1000);
        if (! audioAdvanced && now - lastAudioTime > 250 && position >= latest) running = false;
    }
    lastTime = now; lastSequence = sequence; lastEpoch = epoch;
    return position;
}

void GateDisplay::visibilityChanged()
{
    setDisplayActive (isVisible());
}

void GateDisplay::setHistoryAppearance (BypassFade& fade, bool& state, bool enabled)
{
    if (state == enabled) return;
    state = enabled;
    fade.setBypassed (! enabled, juce::Time::getMillisecondCounterHiRes(), displayActive);
    historyDirty = true;
    repaint();
}

void GateDisplay::setSidechainVisible (bool visible) { setHistoryAppearance (sidechainFade, sidechainVisible, visible); }
void GateDisplay::setDetectorVisible (bool visible) { setHistoryAppearance (detectorFade, showDetector, visible); }
void GateDisplay::setSidechainFocus (bool focused) { setHistoryAppearance (focusFade, sidechainFocus, focused); }
void GateDisplay::setKneeVisible (bool visible) { setHistoryAppearance (kneeFade, showKnee, visible); }

void GateDisplay::setDisplayActive (bool enabled)
{
    if (displayActive == enabled) return;
    displayActive = enabled;
    bypassFade.settle();badgeMotion.settle();
    hysteresisFlipFade.request (displayedFlip > .5f ? 2 : 1, 0, false);
    closeBadgeFade.setBypassed (true, 0, false);
    sidechainFade.settle(); detectorFade.settle(); focusFade.settle(); kneeFade.settle();
    stopTimer(); vblank = {};
    frozen = inspecting = false;
    inspected = selectionAnchor = -1;
    if (! enabled)
    {
        processor.stopHistoryCapture();
        if (dragging && dragParameter != nullptr)
        {
            dragging = false;
            dragParameter->endChangeGesture();
            dragParameter = nullptr;
            if (onReferenceEditEnd) onReferenceEditEnd();
        }
        return;
    }
    captureState = processor.startHistoryCapture();
    captureStartSequence = capturedSequence = processor.getHistorySequence();
    capturedEpoch = processor.getHistoryEpoch();
    frames = {}; validFrames = 0; scrollFraction = 0;
    clock.reset(); historyDirty = true; hasPainted = false;
    historyImage = {}; repaintFraction = -1;
    lastVblankArrival = lastVblankTimestamp = -1; framePacer.reset();
    vblank = juce::VBlankAttachment (this, [this] (double timestamp) { vblankCallback (timestamp); });
    startTimerHz (60); repaint();
}

void GateDisplay::captureHistory (bool latestForFreeze, double presentationTimeMs)
{
    if (! displayActive) return;
    const auto latest = processor.getHistorySequence();
    const auto epoch = processor.getHistoryEpoch();
    const double rate = processor.historyFramesPerSecond();
    const double position = latestForFreeze ? (double) latest : clock.advance (latest, epoch,
        presentationTimeMs >= 0 ? presentationTimeMs : juce::Time::getMillisecondCounterHiRes(),
        rate, processor.historyDisplayDelayFrames());
    if (epoch != capturedEpoch || latest < captureStartSequence) captureStartSequence = 0;
    const auto target = juce::jmax ((uint64_t) position, captureStartSequence);
    if (target == capturedSequence && epoch == capturedEpoch)
    { scrollFraction = juce::jmax (0.0f, (float) (position - target)); return; }
    const bool incremental = epoch == capturedEpoch && target > capturedSequence
        && target - capturedSequence < frames.size();
    const int count = (int) juce::jmin (incremental ? target - capturedSequence : target, (uint64_t) frames.size());
    auto next = incremental ? frames : decltype (frames) {};
    if (incremental) std::move (next.begin() + count, next.end(), next.begin());
    for (int i = 0; i < count; ++i)
        if (! processor.readHistoryFrame (target - (uint64_t) count + 1 + (uint64_t) i,
                                         next[frames.size() - (size_t) count + (size_t) i], captureState)) return;
    if (epoch != processor.getHistoryEpoch()) return;
    frames = next;
    validFrames = incremental ? juce::jmin ((int) frames.size(), validFrames + count) : count;
    capturedSequence = target;
    capturedEpoch = epoch;
    capturedFrameRate = rate;
    scrollFraction = juce::jmax (0.0f, (float) (position - target));
    historyDirty = true;
}

void GateDisplay::timerCallback()
{
    if (! displayActive || ! isShowing()) return;
    fallbackFrame (juce::Time::getMillisecondCounterHiRes());
}

void GateDisplay::fallbackFrame (double timeMs)
{
    if (lastVblankArrival < 0 || timeMs - lastVblankArrival > 100) animationFrame (timeMs);
}

void GateDisplay::vblankCallback (double timestampSeconds)
{
    if (! displayActive || ! isShowing()) return;
    presentationFrame (timestampSeconds, juce::Time::getMillisecondCounterHiRes());
}

void GateDisplay::presentationFrame (double timestampSeconds, double arrival)
{
    const double nativeMs = timestampSeconds * 1000;
    // Native and JUCE clocks may have different epochs. Match Clipper's mapping
    // and restart after timestamp resets/stalls without catch-up paint bursts.
    if (lastVblankTimestamp < 0 || nativeMs < lastVblankTimestamp || arrival - lastVblankArrival > 250)
    { vblankOffset = arrival - nativeMs; framePacer.reset(); }
    lastVblankTimestamp = nativeMs; lastVblankArrival = arrival;
    if (framePacer.present (nativeMs)) animationFrame (nativeMs + vblankOffset);
}

void GateDisplay::animationFrame (double timeMs)
{
    if (! displayActive) return;
    const bool bypassChanged=bypassFade.advance(timeMs);
    // Page requests start on the UI clock. Native presentation timestamps may
    // predict ahead, so keep badge travel and Flip fades on elapsed UI time at a real peer.
    const double badgeTime=getPeer()!=nullptr?juce::Time::getMillisecondCounterHiRes():timeMs;
    const bool flipChanged=hysteresisFlipFade.advance(badgeTime);
    const bool closeBadgeChanged=closeBadgeFade.advance(badgeTime);
    bool appearanceChanged=false;
    for (auto* fade : { &sidechainFade, &detectorFade, &focusFade, &kneeFade })
        appearanceChanged=fade->advance(badgeTime)||appearanceChanged;
    if(appearanceChanged)historyDirty=true;
    if(badgeMotion.advance(badgeTime)||bypassChanged||flipChanged||closeBadgeChanged||appearanceChanged)repaint();
    if (! frozen) captureHistory (false, timeMs);
    std::array<float, 7> settings {};
    static constexpr const char* ids[] { "threshold", "hysteresis", "flip", "ratio", "knee", "range", "bypass" };
    for (size_t i = 0; i < settings.size(); ++i) settings[i] = processor.parameters.getRawParameterValue (ids[i])->load();
    if (historyDirty || capturedSequence != repaintSequence || settings != repaintSettings
        || ! juce::approximatelyEqual (scrollFraction, repaintFraction))
    {
        repaintSequence = capturedSequence; repaintFraction = scrollFraction; repaintSettings = settings;
        repaint();
    }
    if (onAnimationFrame) onAnimationFrame (timeMs);
}

void GateDisplay::setFrozen (bool enabled)
{
    if (enabled && ! frozen && ! hasPainted) captureHistory (true);
    frozen = enabled;
    inspected = selectionAnchor = -1;
    inspecting = false;
    if (! frozen) { clock.reset(); captureHistory(); }
    repaint();
}

bool GateDisplay::suggestNoiseSettings (float& openingDb, float& hysteresisDb) const
{
    if (! frozen || inspected < 0 || selectionAnchor < 0) return false;
    const int first = juce::jmin (inspected, selectionAnchor), last = juce::jmax (inspected, selectionAnchor);
    if (first < (int) frames.size() - validFrames || last - first + 1 < capturedFrameRate * 0.5)
        return false;
    std::vector<float> levels;
    for (int i = first; i <= last; ++i)
    {
        const auto& frame = frames[(size_t) i];
        const float level = juce::jmax (frame.detectorL, frame.detectorR);
        if (level > 0) levels.push_back (juce::Decibels::gainToDecibels (level, -100.0f));
    }
    if (levels.size() < 4) return false;
    std::sort (levels.begin(), levels.end());
    const float noise = levels[(size_t) ((levels.size() - 1) * 0.9)];
    // Never treat silence or loud program material as an established noise floor.
    if (noise < -79 || noise > -12) return false;
    openingDb = juce::jlimit (-80.0f, 0.0f, noise + 6.0f);
    hysteresisDb = 3.0f;
    return true;
}

void GateDisplay::rebuildHistory (float scale)
{
    const float dx = (float) getWidth() / (frames.size() - 1);
    const int w = juce::jmax (1, (int) std::ceil ((getWidth() + dx + 4) * scale));
    const int h = juce::jmax (1, juce::roundToInt (getHeight() * scale));
    const float focus = focusFade.amount(), scOpacity = sidechainFade.amount(), traceOpacity = detectorFade.amount();
    const std::array<float, 3> appearance { focus, scOpacity, traceOpacity };
    const bool sameSize = historyImage.getWidth() == w && historyImage.getHeight() == h
        && juce::approximatelyEqual (rasterScale, scale);
    if (! historyDirty && sameSize && rasterSequence == capturedSequence && rasterEpoch == capturedEpoch
        && rasterFrames == validFrames && rasterAppearance == appearance) return;
    const uint64_t advanced = capturedSequence > rasterSequence ? capturedSequence - rasterSequence : 0;
    const bool incremental = sameSize && rasterEpoch == capturedEpoch && rasterAppearance == appearance
        && advanced > 0 && advanced <= 16
        && validFrames == juce::jmin ((int) frames.size(), rasterFrames + (int) advanced);
    juce::RectangleList<int> damaged;
    auto changed = juce::Rectangle<int> (0, 0, w, h);
    if (! sameSize)
        historyImage = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType {});
    else
    {
        // Match Compressor: don't mutate an image retained by native presentation.
        historyImage.duplicateIfShared();
        if (incremental)
        {
            const int shift = (int) (std::floor ((double) capturedSequence * dx * scale)
                                    - std::floor ((double) rasterSequence * dx * scale));
            historyImage.moveImageSection (0, 0, shift, 0, w - shift, h);
            const float phase = (float) (std::fmod ((double) capturedSequence * dx * scale, 1.0) / scale);
            const int left = juce::jlimit (0, w, (int) std::floor ((getWidth() - advanced * dx + phase - 4) * scale));
            changed = { left, 0, w - left, h };
            // Endpoint/cap coverage changes when the leftmost segment expires.
            // Unlike Compressor's GR raster, Gate has no permanent top baseline:
            // don't dirty the entire top edge for ordinary below-full-scale audio.
            const juce::Rectangle<int> leftEdge (0, 0, juce::roundToInt (4 * scale), h);
            historyImage.clear (leftEdge); damaged.add (leftEdge);
            const bool touchesTop = std::any_of (frames.begin(), frames.end(), [&] (const auto& frame)
            { return juce::jmax (frame.input, frame.output) >= .99f
                || (scOpacity > 0 && frame.sidechain >= .99f)
                || (traceOpacity > 0 && juce::jmax (frame.detectorL, frame.detectorR) >= .99f); });
            if (touchesTop)
            {
                const juce::Rectangle<int> topEdge (0, 0, w, juce::roundToInt (4 * scale));
                historyImage.clear (topEdge); damaged.add (topEdge);
            }
        }
        historyImage.clear (changed);
    }
    damaged.add (changed);
    rasterScale = scale;
    // Stable absolute, physical-pixel coordinates avoid old peaks shimmering as
    // the viewport advances. Quantize stroke geometry to the same 1/64 px grid.
    rasterPhase = (float) (std::fmod ((double) capturedSequence * dx * scale, 1.0) / scale);
    const double origin = std::floor ((double) capturedSequence * dx * scale);
    juce::Graphics g (historyImage);
    g.reduceClipRegion (damaged);
    const auto rasterPoint = [&] (size_t i, float value, bool belowFloor)
    {
        const float db = juce::Decibels::gainToDecibels (value, belowFloor ? -100.0f : -80.0f);
        const float y = belowFloor ? juce::jlimit (-12.0f, getHeight() + 12.0f, -db * getHeight() / 80.0f) : dbToY (db);
        return juce::Point<float> { (float) (std::round (((double) capturedSequence + i) * dx * scale * 64.0) / 64.0 - origin),
                                   (float) (std::round (y * scale * 64.0) / 64.0) };
    };
    const float right = (getWidth() + dx + 3 + rasterPhase) * scale;
    const auto pathFor = [&] (auto value, bool belowFloor = false)
    {
        juce::Path path;
        const size_t first = frames.size() - (size_t) validFrames;
        for (size_t i = first; i < frames.size(); ++i)
        {
            const auto point = rasterPoint (i, value (frames[i]), belowFloor);
            if (i == first) path.startNewSubPath (point); else path.lineTo (point);
        }
        if (! path.isEmpty()) path.lineTo (right, path.getCurrentPosition().y);
        return path;
    };
    const auto shade = [&] (const juce::Path& path, juce::Colour colour, float alpha)
    {
        if (path.isEmpty()) return;
        const auto visibleBounds = path.getBounds().withBottom (h).expanded (1);
        if (! g.clipRegionIntersects (visibleBounds.getSmallestIntegerContainer())) return;
        auto fill = path;
        fill.lineTo (path.getCurrentPosition().x, getHeight() * scale);
        fill.lineTo (path.getBounds().getX(), getHeight() * scale); fill.closeSubPath();
        g.setGradientFill ({ colour.withAlpha (alpha), 0, getHeight() * 0.25f * scale,
                             colour.withAlpha (alpha * 0.18f), 0, getHeight() * scale, false });
        g.fillPath (fill);
    };
    // Like Limiter, keep one continuous contour through quiet/silent sections.
    // Map below-floor points outside the plot, rather than breaking the contour
    // or clamping it into a horizontal line at the bottom of the display.
    const auto strokeOutline = [&] (const juce::Path& edge, juce::Colour colour)
    {
        if (! g.clipRegionIntersects (edge.getBounds().expanded (2 * scale).getSmallestIntegerContainer())) return;
        juce::Graphics::ScopedSaveState hideBelowFloor (g);
        g.reduceClipRegion ({ 0, 0, w, juce::jmax (0, juce::roundToInt ((getHeight() - 1) * scale)) });
        g.setColour (colour);
        g.strokePath (edge, juce::PathStrokeType (0.75f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    const auto outline = [&] (auto value, juce::Colour colour)
    {
        strokeOutline (pathFor (value, true), colour);
    };
    shade (pathFor ([] (const auto& f) { return f.input; }), GateTheme::waveInput, juce::jmap(focus,.19f,.10f));
    shade (pathFor ([] (const auto& f) { return f.output; }), GateTheme::waveOutput, juce::jmap(focus,.26f,.11f));
    const bool showSC = scOpacity > 0;
    if (showSC)
        shade (pathFor ([] (const auto& f) { return f.sidechain; }), GateTheme::sidechain, juce::jmap(focus,.17f,.27f)*scOpacity);
    if (traceOpacity > 0)
    {
        // Colour the captured mode, not the current selector. A frozen trace or
        // old part of the timeline must not change identity when mode changes.
        const auto pointAt = [&] (size_t i)
        {
            return rasterPoint (i, juce::jmax (frames[i].detectorL, frames[i].detectorR), true);
        };
        const size_t first = frames.size() - (size_t) validFrames;
        for (size_t start = first; start < frames.size();)
        {
            size_t end = start + 1;
            while (end < frames.size() && frames[end].detectorPeak == frames[start].detectorPeak) ++end;
            const auto colour = frames[start].detectorPeak ? GateTheme::peakTrace : GateTheme::violet;
            juce::Path contour;
            contour.startNewSubPath (start == first ? pointAt (start)
                : (pointAt (start - 1) + pointAt (start)) * 0.5f);
            for (size_t i = start; i < end; ++i) contour.lineTo (pointAt (i));
            if (end < frames.size()) contour.lineTo ((pointAt (end - 1) + pointAt (end)) * 0.5f);
            else contour.lineTo (right, contour.getCurrentPosition().y);
            shade (contour, colour, 0.21f*traceOpacity);
            strokeOutline (contour, colour.withAlpha (0.66f*traceOpacity));
            start = end;
        }
    }
    // Stroke only the measured output contour, not the closed fill polygon.
    // Keep the SC contour on top and retain the audition-focused dimming.
    outline ([] (const auto& f) { return f.output; }, GateTheme::waveOutput.withAlpha (juce::jmap(focus,.78f,.38f)));
    if (showSC)
        outline ([] (const auto& f) { return f.sidechain; }, GateTheme::sidechain.withAlpha (juce::jmap(focus,.90f,1.f)*scOpacity));
    rasterSequence = capturedSequence; rasterEpoch = capturedEpoch;
    rasterFrames = validFrames; rasterAppearance = appearance;
    historyDirty = false;
}

juce::Rectangle<float> GateDisplay::thresholdBadgeBounds() const
{
    const auto panel=GateLayout::detailsPanel(getLocalBounds());
    const int mainTop=juce::jmax(getHeight()-145,panel.getBottom()+6);
    const auto clampedY=[&](int top){return juce::jlimit(5.f,(float)top-26.f,thresholdY()-25.f);};
    const float y=badgeGeometryReady?badgeMotion.position(clampedY(badgeMainTop),clampedY(badgePageTop))
        :clampedY(panelMode!=0?panel.getY()-3:mainTop);
    return {(float)panel.getX(),y,146.f,22.f};
}

juce::Rectangle<float> GateDisplay::hysteresisBadgeBounds() const
{
    const bool flipped = hysteresisFlipFade.presented() == 2;
    const auto fixedThreshold = thresholdBadgeBounds();
    // Threshold never yields to the moving CLOSE readout. In Flip it stops
    // four pixels above Threshold; normally it stays below the return line.
    // Neither constraint changes the parameter.
    const float y = flipped
        ? juce::jmin (juce::jmax (4.0f, hysteresisY() - 25.0f), fixedThreshold.getY() - 25.0f)
        : juce::jlimit (4.0f, juce::jmax (4.0f, getHeight() - 25.0f),
                       juce::jmax (hysteresisY() + 4.0f, fixedThreshold.getBottom() + 4.0f));
    return { fixedThreshold.getX(), y, 134.0f, 21.0f };
}

juce::String GateDisplay::hysteresisBadgeText() const
{
    const bool flipped = hysteresisFlipFade.presented() == 2;
    const float returnDb = displayedThreshold + (flipped ? 1 : -1) * displayedHysteresis;
    return "CLOSE  " + juce::String (returnDb, 1) + " dB";
}

bool GateDisplay::hitThreshold (juce::Point<float> point) const
{
    return std::abs (point.y - thresholdY()) < 9.0f || thresholdBadgeBounds().contains (point);
}

void GateDisplay::setCloseBadgeVisible (bool visible)
{
    closeBadgeFade.setBypassed (! visible, juce::Time::getMillisecondCounterHiRes(), displayActive);
    repaint();
}

void GateDisplay::mouseDown (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown()) return;
    if (panelMode != 0 && GateLayout::detailsPanel (getLocalBounds()).contains (event.getPosition()))
        return;
    const bool editThreshold = hitThreshold (event.position) && ! event.mods.isAltDown();
    const bool editHysteresis = hitHysteresis (event.position);
    if (! editThreshold && ! editHysteresis)
    {
        const auto panel = GateLayout::detailsPanel (getLocalBounds());
        const int controlsTop = panelMode != 0 ? panel.getY() - 5 : juce::jmax (getHeight() - 145, panel.getBottom() + 6);
        if (frozen && event.position.y > 30 && event.position.y < controlsTop && validFrames > 0)
        {
            inspecting = true;
            inspected = juce::jlimit ((int) frames.size() - validFrames, (int) frames.size() - 1,
                (int) (event.position.x / getWidth() * (frames.size() - 1)));
            if (! event.mods.isShiftDown() || selectionAnchor < 0) selectionAnchor = inspected;
            if (onHistorySelectionChanged) onHistorySelectionChanged();
            repaint();
        }
        return;
    }
    processor.parameters.copyState(); processor.undoManager.beginNewTransaction (editThreshold ? "Threshold" : "Hysteresis");
    dragging = true;
    dragParameter = editThreshold ? threshold : processor.parameters.getParameter ("hysteresis");
    dragY = event.position.y;
    dragDb = dragParameter->convertFrom0to1 (dragParameter->getValue());
    fineDrag = event.mods.isShiftDown();
    dragParameter->beginChangeGesture();
    setCloseBadgeVisible (dragParameter != threshold);
    if (onReferenceEdit) onReferenceEdit();
}

void GateDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (inspecting)
    {
        inspected = juce::jlimit ((int) frames.size() - validFrames, (int) frames.size() - 1,
            (int) (event.position.x / getWidth() * (frames.size() - 1)));
        if (! event.mods.isShiftDown()) selectionAnchor = inspected;
        if (onHistorySelectionChanged) onHistorySelectionChanged();
        repaint(); return;
    }
    if (! dragging) return;
    if (fineDrag != event.mods.isShiftDown())
    {
        dragY = event.position.y;
        dragDb = dragParameter->convertFrom0to1 (dragParameter->getValue());
        fineDrag = event.mods.isShiftDown();
    }
    const bool isThreshold = dragParameter == threshold;
    const float direction = isThreshold || processor.parameters.getRawParameterValue ("flip")->load() > 0.5f ? 1 : -1;
    const float db = juce::jlimit (isThreshold ? -80.0f : 0.0f, isThreshold ? 0.0f : 24.0f,
        dragDb + direction * (dragY - event.position.y) * 80.0f / juce::jmax (1, getHeight()) * (fineDrag ? 0.1f : 1.0f));
    dragParameter->setValueNotifyingHost (dragParameter->convertTo0to1 (db));
    repaint();
}

void GateDisplay::mouseUp (const juce::MouseEvent&)
{
    const bool wasDragging = dragging;
    if (dragging) dragParameter->endChangeGesture();
    dragging = false;
    inspecting = false;
    setCloseBadgeVisible (false);
    if (wasDragging)
    {
        processor.parameters.copyState(); processor.undoManager.beginNewTransaction();
        if (onReferenceEditEnd) onReferenceEditEnd();
    }
}

void GateDisplay::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (panelMode != 0 && GateLayout::detailsPanel (getLocalBounds()).contains (event.getPosition())) return;
    auto* parameter = hitThreshold (event.position) && ! event.mods.isAltDown() ? threshold
        : hitHysteresis (event.position) ? processor.parameters.getParameter ("hysteresis") : nullptr;
    if (parameter == nullptr) return;
    if (dragging) { dragParameter->endChangeGesture(); dragging = false; }
    setCloseBadgeVisible (false);
    processor.parameters.copyState(); processor.undoManager.beginNewTransaction ("Reset reference");
    parameter->beginChangeGesture(); parameter->setValueNotifyingHost (parameter->getDefaultValue()); parameter->endChangeGesture();
    processor.parameters.copyState(); processor.undoManager.beginNewTransaction();
    if (onReferenceEditEnd) onReferenceEditEnd();
}

void GateDisplay::mouseMove (const juce::MouseEvent& event)
{
    const bool overDetails = panelMode != 0
        && GateLayout::detailsPanel (getLocalBounds()).contains (event.getPosition());
    setMouseCursor (overDetails ? juce::MouseCursor::NormalCursor
                               : hitThreshold (event.position) || hitHysteresis (event.position) ? juce::MouseCursor::UpDownResizeCursor
                               : frozen ? juce::MouseCursor::LeftRightResizeCursor
                                                               : juce::MouseCursor::NormalCursor);
    repaint();
}

void GateDisplay::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

juce::Path GateDisplay::transferCurvePath() const
{
    juce::Path transfer;
    if (getWidth() <= 0 || getHeight() <= 0) return transfer;
    const auto value = [&] (const char* id) { return processor.parameters.getRawParameterValue (id)->load(); };
    const float direction = displayedFlip > 0.5f ? -1.0f : 1.0f;
    const float thresholdDb = displayedThreshold, ratio = value ("ratio");
    const float knee = value ("knee"), range = value ("range");
    for (int i = 0; i <= getWidth(); ++i)
    {
        const float inDb = juce::jmap ((float) i, 0.0f, (float) getWidth(), -80.0f, 0.0f);
        // Flip mirrors the detector comparison, not the audible output axis.
        const float reduction = OpenGateAudioProcessor::expansionReduction (
            direction * inDb, direction * thresholdDb, ratio, knee, range);
        const float outDb = inDb - reduction;
        // Keep the actual below-floor geometry. Painting clips it rather than
        // inventing a horizontal -80 dB shelf, including in reverse-gate mode.
        const float y = -outDb * getHeight() / 80.0f;
        if (i == 0) transfer.startNewSubPath ((float) i, y); else transfer.lineTo ((float) i, y);
    }
    return transfer;
}

void GateDisplay::paint (juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    // Publish one immutable viewport per timer tick, not per dirty rectangle.
    if (! frozen && ! hasPainted) captureHistory();
    // Paint into the host's actual clip/transform without extra scene/control
    // caches. Transparent sibling corners always expose the current viewport.
    paintScene (g);
    hasPainted = true;
}

void GateDisplay::paintScene (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.fillAll (GateTheme::background);
    const auto dbToY = [&] (float db)
    {
        return juce::jmap (juce::jlimit (-80.0f, 0.0f, db), -80.0f, 0.0f,
                           area.getBottom(), area.getY());
    };
    for (const float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f, -80.0f })
    {
        const float y = dbToY (db);
        g.setColour (GateTheme::grid.withAlpha (db == 0.0f ? 0.72f : 0.38f));
        // Clipper reserves 22 px for the scale; only reference lines span it.
        g.drawHorizontalLine (juce::roundToInt (y),
                              (float) GateLayout::detailsPanel (getLocalBounds()).getX(), area.getRight());
    }

    // Match Clipper: supersample at least 2x, and follow the actual render context
    // on Retina displays and scaled snapshots instead of the logical component scale.
    rebuildHistory (juce::jmax (2.0f, g.getInternalContext().getPhysicalPixelScaleFactor()));
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (historyImage, juce::AffineTransform::scale (1.0f / rasterScale)
        .translated (-rasterPhase - scrollFraction * getWidth() / (frames.size() - 1), 0));
    for (const float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f, -80.0f })
    {
        g.setColour (GateTheme::dim);
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (juce::String ((int) std::abs (db)), 2,
                    juce::jlimit (0, getHeight() - 12, juce::roundToInt (dbToY (db)) - 6), 18, 12,
                    juce::Justification::centredLeft);
    }

    const float thresholdDb = displayedThreshold;
    const float thresholdY = dbToY (thresholdDb);
    const float appearance=bypassFade.amount();
    const float hysteresisDb = displayedHysteresis;
    const bool flipped = hysteresisFlipFade.presented() == 2;
    const float hysteresisOpacity = hysteresisFlipFade.alpha();
    {
    juce::Graphics::ScopedSaveState referenceLines (g);
    // The floating controls paint their own shapes. Keep the reference lines
    // continuous through the otherwise transparent control rows.
    if (hysteresisDb > 0.0f)
    {
        const float closeDb = thresholdDb + (flipped ? hysteresisDb : -hysteresisDb);
        const float closeY = dbToY (closeDb);
        g.setColour (GateTheme::violet.brighter (0.35f).withAlpha ((.18f+.52f*appearance)*hysteresisOpacity));
        if (closeDb >= -80 && closeDb <= 0)
            g.drawLine (0.0f, closeY, area.getRight(), closeY, dragging && dragParameter != threshold ? 2.0f : 1.0f);
        else
        {
            juce::Path edge;
            if (closeDb < -80) edge.addTriangle (18, getHeight() - 6.0f, 24, getHeight() - 6.0f, 21, getHeight() - 1.0f);
            else edge.addTriangle (18, 6, 24, 6, 21, 1);
            g.fillPath (edge);
        }
    }
    g.setColour (GateTheme::magenta.withAlpha (.3f+.7f*appearance));
    const float thresholdStroke = dragging && dragParameter == threshold ? 2.5f : 1.5f;
    const float strokeY = plotStrokeCentre (thresholdY, area.getHeight(), thresholdStroke);
    g.drawLine (0.0f, strokeY, area.getRight(), strokeY, thresholdStroke);

    const float curveOpacity=kneeFade.amount()*appearance;
    if (curveOpacity>0)
    {
        juce::Graphics::ScopedSaveState curveClip (g);
        g.reduceClipRegion ({ 0, 0, getWidth(), juce::jmax (0, getHeight() - 1) });
        g.setColour (GateTheme::text.withAlpha (0.74f*curveOpacity));
        g.strokePath (transferCurvePath(), juce::PathStrokeType (1.35f));
    }
    }

    const auto badge = thresholdBadgeBounds();
    g.setColour (GateTheme::panelRaised.withAlpha (0.94f));
    g.fillRoundedRectangle (badge, 4.0f);
    g.setColour (GateTheme::magenta.withAlpha (.4f+.6f*appearance));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText ("THRESHOLD  " + juce::String (thresholdDb, 1) + " dB", badge,
                juce::Justification::centred);

    const float closeBadgeOpacity = closeBadgeFade.amount() * hysteresisOpacity;
    if (closeBadgeOpacity > 0.0f)
    {
        const auto box = hysteresisBadgeBounds();
        // At the very top, no full second card fits above fixed Threshold.
        // Omit it instead of clipping text, swapping sides or moving Threshold.
        if (box.getY() >= 4.0f)
        {
            g.setColour (GateTheme::panelRaised.withAlpha(closeBadgeOpacity)); g.fillRoundedRectangle (box, 4);
            g.setColour (GateTheme::violet.brighter (0.35f).withAlpha((.4f+.6f*appearance)*closeBadgeOpacity));
            g.drawText (hysteresisBadgeText(), box, juce::Justification::centred);
        }
    }
    if (frozen && inspected >= 0 && ! dragging)
    {
        const float dx = area.getWidth() / (frames.size() - 1);
        const float x = inspected * dx;
        const int first = juce::jmin (inspected, selectionAnchor), last = juce::jmax (inspected, selectionAnchor);
        const auto panel = GateLayout::detailsPanel (getLocalBounds());
        const float controlsTop = (float) (panelMode != 0 ? panel.getY() - 5
            : juce::jmax (getHeight() - 145, panel.getBottom() + 6));
        if (first != last)
        {
            g.setColour (GateTheme::violet.withAlpha (0.10f));
            g.fillRect (first * dx, 30.0f, (last - first) * dx, controlsTop - 30);
        }
        const auto& frame = frames[(size_t) inspected];
        FrozenInspection::paint(g, area, x, controlsTop,
            (frames.size()-1-(size_t)inspected)/capturedFrameRate,
            { { "IN", FrozenInspection::level(frame.input, -80.f) },
              { "OUT", FrozenInspection::level(frame.output, -80.f) },
              { "GR", juce::String(frame.attenuation, 1)+" dB" } },
            { { "DET", FrozenInspection::level(juce::jmax(frame.detectorL, frame.detectorR), -80.f) } },
            GateTheme::panelRaised, GateTheme::text, GateTheme::dim);
    }

}

void GateMeter::paint (juce::Graphics& g)
{
    const float height = (float) getHeight();
    constexpr float top = 0.0f;
    const float bottom = height;
    const auto attenuationY = [height] (float db)
    {
        return juce::jmap (juce::jlimit (0.0f, 80.0f, db), 0.0f, 80.0f, 0.0f, height);
    };
    g.fillAll (GateTheme::panel);
    const auto levelY = [height] (float gain)
    {
        const float db = juce::Decibels::gainToDecibels (gain, -80.0f);
        return juce::jmap (juce::jlimit (-80.0f, 0.0f, db), -80.0f, 0.0f,
                           height, 0.0f);
    };
    for (const float db : { 0.0f, 12.0f, 24.0f, 36.0f, 48.0f, 60.0f, 72.0f, 80.0f })
    {
        const int y = juce::jlimit (0, getHeight() - 1, juce::roundToInt (attenuationY (db)));
        g.setColour (GateTheme::grid.withAlpha (0.8f));
        g.drawHorizontalLine (y, 4.0f, 49.0f);
        g.setColour (GateTheme::dim);
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (juce::String ((int) db), 50, juce::jlimit (0, getHeight() - 12, y - 6), 16, 12,
                    juce::Justification::centredLeft);
    }
    const auto inTrack = juce::Rectangle<float> (6.0f, top, 7.0f, bottom - top);
    const auto outTrack = juce::Rectangle<float> (18.0f, top, 7.0f, bottom - top);
    const auto attenuationTrack = juce::Rectangle<float> (33.0f, top, 12.0f, bottom - top);
    g.setColour (GateTheme::background);
    g.fillRoundedRectangle (inTrack, 1.0f);
    g.fillRoundedRectangle (outTrack, 1.0f);
    g.fillRoundedRectangle (attenuationTrack, 1.0f);
    g.setColour (GateTheme::waveInput.withAlpha (0.9f));
    g.fillRect (inTrack.withTop (levelY (processor.inputLevel())));
    g.setColour (GateTheme::waveOutput);
    g.fillRect (outTrack.withTop (levelY (processor.outputLevel())));
    const bool bypassed = processor.parameters.getRawParameterValue ("bypass")->load() > 0.5f;
    const float current = attenuationY (bypassed ? 0.0f : processor.attenuationLevel());
    g.setGradientFill ({ GateTheme::magenta, attenuationTrack.getTopLeft(),
                         GateTheme::violet.darker (0.4f), attenuationTrack.getBottomLeft(), false });
    g.fillRect (attenuationTrack.withBottom (current));
}

void GateAdvancedPanel::paint (juce::Graphics& g)
{
    g.setColour (GateTheme::advanced.withAlpha (0.82f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 7);
    g.setColour (GateTheme::edge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 7, 1);
}

GateSidechainResponse::GateSidechainResponse (OpenGateAudioProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (true, false);
    spectrumLevels.fill (-96.0f);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (fftSize - 1));
    setTooltip ("Filtered internal/external SC spectrum, 0 to -96 dBFS, before envelope smoothing. Purple line = HP, cyan line = LP; the tinted band marks the selected frequency range, not an EQ response. Drag lines/caps; Shift = fine, Alt prefers LP. Cutoffs stay at least 1/3 octave apart. HP 20 Hz / LP 20 kHz are Off. Freeze holds the spectrum.");
}

void GateSidechainResponse::visibilityChanged()
{
    processor.setSpectrumEnabled (displayActive && isVisible());
    if (! displayActive || ! isVisible()) endDrag();
    // Never replay queued audio from the previous page visit.
    OpenGateAudioProcessor::SpectrumFrame frame;
    for (int i = 0; i < OpenGateAudioProcessor::spectrumCapacity && processor.readSpectrumFrame (frame); ++i) {}
    spectrumReceived = spectrumHop = spectrumWrite = 0;
    lastSpectrumSequence = 0;
    spectrumLevels.fill (-96.0f);
}

void GateSidechainResponse::transformSpectrum()
{
    spectrumPower.fill (0);
    for (int ch = 0; ch < 2; ++ch)
    {
        fftWork.fill (0);
        for (int i = 0; i < fftSize; ++i)
            fftWork[(size_t) i] = spectrumHistory[(size_t) ch][(size_t) ((spectrumWrite + i) % fftSize)] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (fftWork.data(), true);
        for (int bin = 0; bin <= fftSize / 2; ++bin)
            spectrumPower[(size_t) bin] += 0.5f * fftWork[(size_t) bin] * fftWork[(size_t) bin];
    }
    // Hann coherent gain correction. Sum channel power, never waveforms:
    // opposite-polarity stereo must remain visible.
    for (int bin = 0; bin <= fftSize / 2; ++bin)
    {
        const float db = juce::Decibels::gainToDecibels (std::sqrt (spectrumPower[(size_t) bin]) * 4.0f / (fftSize - 1), -96.0f);
        auto& level = spectrumLevels[(size_t) bin];
        level += (db > level ? 0.72f : 0.12f) * (db - level);
    }
}

void GateSidechainResponse::updateSpectrum (bool frozen)
{
    const auto epoch = processor.getHistoryEpoch();
    if (epoch != spectrumEpoch)
    {
        spectrumEpoch = epoch;
        spectrumRate = processor.spectrumSampleRate();
        spectrumReceived = spectrumHop = spectrumWrite = 0;
        lastSpectrumSequence = 0;
        spectrumLevels.fill (-96.0f);
    }
    OpenGateAudioProcessor::SpectrumFrame frame;
    int received = 0;
    for (int count = 0; count < OpenGateAudioProcessor::spectrumCapacity && processor.readSpectrumFrame (frame); ++count)
    {
        if (! displayActive || ! isVisible() || frozen || frame.epoch != spectrumEpoch
            || frame.captureState != processor.getHistoryCaptureState()) continue;
        if (lastSpectrumSequence != 0 && frame.sequence != lastSpectrumSequence + 1)
            spectrumReceived = spectrumHop = spectrumWrite = 0;
        lastSpectrumSequence = frame.sequence;
        const auto analysisSample = [] (float value)
        {
            // Display-only guard against corrupt/absurd host levels overflowing
            // the FFT or its power sum. Ordinary signals are unchanged.
            return std::isfinite (value) ? juce::jlimit (-16.0f, 16.0f, value) : 0.0f;
        };
        spectrumHistory[0][(size_t) spectrumWrite] = analysisSample (frame.left);
        spectrumHistory[1][(size_t) spectrumWrite] = analysisSample (frame.right);
        spectrumWrite = (spectrumWrite + 1) % fftSize;
        ++received; ++spectrumReceived; ++spectrumHop;
        if (spectrumReceived >= fftSize && spectrumHop >= fftHop)
        {
            spectrumHop = 0;
            spectrumReceived = fftSize;
            transformSpectrum();
        }
    }
    // Host transport may stop delivering audio entirely; decay rather than stick.
    if (received == 0 && ! frozen)
        for (auto& level : spectrumLevels) level = juce::jmax (-96.0f, level - 2.0f);
}

float GateSidechainResponse::spectrumLevelAtFrequency (double hz) const noexcept
{
    if (hz > spectrumRate * 0.5 || hz < 0) return -96.0f;
    const auto bin = juce::jlimit (0, fftSize / 2, juce::roundToInt (hz * fftSize / spectrumRate));
    return spectrumLevels[(size_t) bin];
}

juce::Rectangle<float> GateSidechainResponse::plotBounds() const
{
    return getLocalBounds().toFloat().reduced (7, 0).withTrimmedLeft (17).withTrimmedTop (18).withTrimmedBottom (15);
}

juce::Point<float> GateSidechainResponse::handlePosition (bool highPass) const
{
    const auto plot = plotBounds();
    const auto cutoffs = processor.sidechainCutoffs();
    const double hz = highPass ? cutoffs.highPass : cutoffs.lowPass;
    return { plot.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * plot.getWidth(),
             highPass ? plot.getY() + 3.5f : plot.getBottom() - 3.5f };
}

int GateSidechainResponse::hitHandle (juce::Point<float> point, bool preferLowPass) const
{
    const float hpDistance = point.getDistanceFrom (handlePosition (true));
    const float lpDistance = point.getDistanceFrom (handlePosition (false));
    if (preferLowPass && lpDistance <= 10) return 1;
    if (hpDistance <= 10 && hpDistance <= lpDistance) return 0;
    if (lpDistance <= 10) return 1;
    if (point.y < plotBounds().getY() - 3 || point.y > plotBounds().getBottom() + 3) return -1;
    const float hpLine = std::abs (point.x - handlePosition (true).x);
    const float lpLine = std::abs (point.x - handlePosition (false).x);
    if (preferLowPass && lpLine <= 6) return 1;
    if (hpLine <= 6 && hpLine <= lpLine) return 0;
    return lpLine <= 6 ? 1 : -1;
}

void GateSidechainResponse::mouseDown (const juce::MouseEvent& event)
{
    endDrag();
    if (! isEnabled() || ! isVisible() || ! event.mods.isLeftButtonDown() || plotBounds().getWidth() <= 0) return;
    const int handle = hitHandle (event.position, event.mods.isAltDown());
    if (handle < 0) return;
    draggingHighPass = handle == 0; hovered = handle;
    dragged = processor.parameters.getParameter (draggingHighPass ? "sideHighPass" : "sideLowPass");
    processor.parameters.copyState();
    processor.undoManager.beginNewTransaction (draggingHighPass ? "SC high-pass" : "SC low-pass");
    dragged->beginChangeGesture();
    originX = event.position.x;
    originLog = std::log (dragged->convertFrom0to1 (dragged->getValue()) / 20.0) / std::log (1000.0);
    fine = event.mods.isShiftDown();
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void GateSidechainResponse::mouseDrag (const juce::MouseEvent& event)
{
    if (dragged == nullptr || plotBounds().getWidth() <= 0) return;
    if (fine != event.mods.isShiftDown())
    {
        originX = event.position.x;
        originLog = std::log (dragged->convertFrom0to1 (dragged->getValue()) / 20.0) / std::log (1000.0);
        fine = event.mods.isShiftDown();
    }
    const double position = juce::jlimit (0.0, 1.0, originLog
        + (event.position.x - originX) / plotBounds().getWidth() * (fine ? 0.1 : 1.0));
    const auto& range = dragged->getNormalisableRange();
    const auto current = processor.sidechainCutoffs();
    const auto pair = OpenGateAudioProcessor::constrainSidechainCutoffs (
        draggingHighPass ? (float) (20 * std::pow (1000.0, position)) : current.highPass,
        draggingHighPass ? current.lowPass : (float) (20 * std::pow (1000.0, position)), draggingHighPass);
    const float hz = range.snapToLegalValue (draggingHighPass ? pair.highPass : pair.lowPass);
    dragged->setValueNotifyingHost (dragged->convertTo0to1 (hz));
    repaint();
}

void GateSidechainResponse::endDrag()
{
    if (dragged == nullptr) return;
    auto* parameter = dragged; dragged = nullptr;
    parameter->endChangeGesture();
    processor.parameters.copyState(); processor.undoManager.beginNewTransaction();
    hovered = -1; setMouseCursor (juce::MouseCursor::NormalCursor); repaint();
}

void GateSidechainResponse::mouseMove (const juce::MouseEvent& event)
{
    if (dragged != nullptr) return;
    hovered = hitHandle (event.position, event.mods.isAltDown());
    setMouseCursor (hovered >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void GateSidechainResponse::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (GateTheme::control.withAlpha (0.45f));
    g.fillRoundedRectangle (bounds, 4);
    const auto plot = plotBounds();
    if (plot.getWidth() <= 0 || plot.getHeight() <= 0) return;
    const auto xFor = [&] (double hz) { return plot.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * plot.getWidth(); };
    const auto yFor = [&] (double db) { return plot.getY() - (float) juce::jmin (0.0, db) * plot.getHeight() / 96; };
    const auto cutoffs = processor.sidechainCutoffs();
    const double hp = cutoffs.highPass, lp = cutoffs.lowPass;
    // Restore the old purple-to-cyan fade, bounded by the current cutoff lines.
    // This is a range backdrop, not filter gain or measured spectrum energy.
    g.setGradientFill ({ GateTheme::violet.withAlpha (0.18f), plot.getTopLeft(),
                         GateTheme::sidechain.withAlpha (0.02f), plot.getBottomLeft(), false });
    g.fillRect (juce::Rectangle<float> (xFor (hp), plot.getY(), xFor (lp) - xFor (hp), plot.getHeight()));
    g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
    g.setColour (GateTheme::dim);
    g.drawText ("SC SPECTRUM", 7, 2, getWidth() - 55, 12, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (7.0f));
    g.drawText ("24 dB/oct", getWidth() - 48, 2, 41, 12, juce::Justification::centredRight);
    for (int db : { 0, -24, -48, -72, -96 })
    {
        g.setColour (GateTheme::dim.withAlpha (db == 0 ? 0.25f : 0.10f));
        if (db > -96) g.drawHorizontalLine (juce::roundToInt (yFor (db)), plot.getX(), plot.getRight());
        if (db == 0 || db == -48 || db == -96)
        {
            g.setColour (GateTheme::dim.withAlpha (0.75f));
            g.drawText (juce::String ((int) db), 0, juce::roundToInt (yFor (db)) - 5, 20, 10, juce::Justification::centredRight);
        }
    }
    for (double hz : { 20.0, 100.0, 1000.0, 10000.0, 20000.0 })
    {
        const float x = xFor (hz);
        g.setColour (GateTheme::dim.withAlpha (0.10f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        // 10k is too close to 20k at the default size; the log position remains exact.
        if (hz == 10000.0 && plot.getWidth() < 260) continue;
        g.setColour (GateTheme::dim.withAlpha (0.85f));
        const auto label = hz >= 1000 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::jlimit (0, getWidth() - 26, juce::roundToInt (x) - 13),
                    getHeight() - 12, 26, 10, juce::Justification::centred);
    }
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());
        juce::Path trace;
        juce::Path spectrumFill;
        spectrumFill.startNewSubPath (plot.getBottomLeft());
        bool tracing = false;
        const int points = juce::jmax (1, juce::roundToInt (plot.getWidth()));
        for (int i = 0; i <= points; ++i)
        {
            const double hz = 20 * std::pow (1000.0, i / (double) points);
            const double nextHz = 20 * std::pow (1000.0, (i + 1.0) / points);
            float db = spectrumLevelAtFrequency (hz);
            // Keep narrow peaks when several FFT bins fit in one screen pixel.
            for (int bin = juce::jmax (1, (int) std::ceil (hz * fftSize / spectrumRate));
                 bin <= juce::jmin (fftSize / 2, (int) (nextHz * fftSize / spectrumRate)); ++bin)
                db = juce::jmax (db, spectrumLevels[(size_t) bin]);
            const float x = plot.getX() + plot.getWidth() * i / points;
            const float y = plot.getY() + juce::jlimit (0.0f, 1.0f, -db / 96.0f) * plot.getHeight();
            spectrumFill.lineTo (x, y);
            if (db <= -95.5f)
            {
                if (tracing) trace.lineTo (x, plot.getBottom());
                tracing = false;
            }
            else if (! tracing)
            {
                trace.startNewSubPath (x, plot.getBottom()); trace.lineTo (x, y); tracing = true;
            }
            else trace.lineTo (x, y);
        }
        // Fill closes at the floor; the outline omits silent baseline segments.
        spectrumFill.lineTo (plot.getBottomRight()); spectrumFill.closeSubPath();
        g.setGradientFill ({ GateTheme::sidechain.withAlpha (0.22f), plot.getTopLeft(),
                             GateTheme::sidechain.withAlpha (0.025f), plot.getBottomLeft(), false });
        g.fillPath (spectrumFill);
        // Only the contour is stroked, not the fill's bottom/side closing edges.
        g.setColour (GateTheme::sidechain.withAlpha (0.40f));
        g.strokePath (trace, juce::PathStrokeType (0.8f));
    }
    // Keep the draggable cutoff markers above the range backdrop and measured spectrum.
    for (bool highPass : { true, false })
    {
        const bool off = highPass ? hp <= 20.5 : lp >= 19999;
        const auto point = handlePosition (highPass);
        const float x = point.x, y = point.y;
        const auto colour = highPass ? GateTheme::violet : GateTheme::sidechain;
        const bool focused = hovered == (highPass ? 0 : 1) || (dragged != nullptr && draggingHighPass == highPass);
        g.setColour (colour.withAlpha (focused ? 0.95f : off ? 0.28f : 0.72f));
        g.drawLine (x, plot.getY(), x, plot.getBottom(), focused ? 1.6f : 1.1f);
        if (focused) { g.setColour (colour.withAlpha (0.18f)); g.fillEllipse (x - 6, y - 6, 12, 12); }
        g.setColour (GateTheme::control); g.fillEllipse (x - 3.5f, y - 3.5f, 7, 7);
        g.setColour (colour.withAlpha (off && ! focused ? 0.55f : 1.0f));
        g.drawEllipse (x - 3, y - 3, 6, 6, focused ? 1.7f : 1.3f);
    }
}

OpenGateAudioProcessorEditor::OpenGateAudioProcessorEditor (OpenGateAudioProcessor& p)
    : AudioProcessorEditor (&p), gateProcessor (p), display (p), meter (p), sidechainResponse (p)
{
    setOpaque (true);
    setLookAndFeel (&lookAndFeel);
    setSize (720, 440);
    setResizable (true, true);
    setResizeLimits (720, 440, 1200, 760);
    addAndMakeVisible (display);
    addAndMakeVisible (meter);
    display.onAnimationFrame = [this] (double timeMs) { meter.refresh(); advancePageFade(timeMs); };
    advancedPanel.setComponentID ("advancedPanel");
    addChildComponent (advancedPanel);
    sidechainResponse.setComponentID ("sidechainResponse");
    addChildComponent (sidechainResponse);
    addMouseListener (this, true);
    display.onReferenceEdit = [this] { referenceEditing = true; hideAdvancedPages(); };
    display.onReferenceEditEnd = [this] { referenceEditing = false; restoreAdvancedPages(); };
    display.onHistorySelectionChanged = [this]
    {
        suggestionReady = false; apply.setEnabled (false);
        learnStatus.setText ("Quiet passage selected\nClick Learn to review settings", juce::dontSendNotification);
    };

    setupKnob (threshold, thresholdLabel, "THRESHOLD", " dB", -12.0);
    setupKnob (hysteresis, hysteresisLabel, "HYSTERESIS", " dB", 6.0);
    // Reference knobs belong to the scene they overlap, as in Compressor.
    // Their transparent corners repaint the same parent reference state.
    display.addAndMakeVisible (threshold);
    display.addAndMakeVisible (hysteresis);
    setupKnob (ratio, ratioLabel, "RATIO", ":1", 100.0);
    setupKnob (range, rangeLabel, "RANGE", " dB", 80.0);
    setupKnob (attack, attackLabel, "ATTACK", " ms", 1.0);
    setupKnob (release, releaseLabel, "RELEASE", " ms", 150.0);
    setupKnob (knee, kneeLabel, "KNEE", " dB", 0.0, true);
    setupKnob (hold, holdLabel, "HOLD", " ms", 2.0, true);
    setupKnob (lookahead, lookaheadLabel, "LOOKAHEAD", " ms", 1.0, true);
    setupKnob (sideHighPass, sideHighPassLabel, "SC HP", " Hz", 20.0, true);
    setupKnob (sideLowPass, sideLowPassLabel, "SC LP", " Hz", 20000.0, true);
    setupKnob (input, inputLabel, "INPUT", " dB", 0.0, true);
    setupKnob (output, outputLabel, "OUTPUT", " dB", 0.0, true);
    setupKnob (mix, mixLabel, "MIX", "%", 100.0, true);
    setupKnob (link, linkLabel, "LINK", "%", 100.0, true);
    sideHighPassLabel.setText ("HIGH PASS", juce::dontSendNotification);
    sideLowPassLabel.setText ("LOW PASS", juce::dontSendNotification);
    sideHighPass.setTooltip ("Detector high-pass filter. The minimum (20 Hz) is Off. Filters the sidechain, not the audible program signal.");
    sideLowPass.setTooltip ("Detector low-pass filter. The maximum (20 kHz) is Off. Filters the sidechain, not the audible program signal.");

    setupButton (details, "DETAILS");
    details.setComponentID ("detailsView");
    setupButton (scView, "SC");
    scView.setComponentID ("scView");
    setupButton (externalSidechain, "Ext. SC");
    externalSidechain.setComponentID ("externalSidechainControl");
    setupButton (showSidechain, "SHOW SC");
    showSidechain.setComponentID ("showSidechain");
    showSidechain.setTooltip ("Show the filtered internal/external SC signal on every page instead of TRACE. Defaults off; enabling Ext. SC or Audition SC selects this view. You can hide it or select TRACE without changing routing or audition.");
    showSidechain.onClick = [this]
    {
        gateProcessor.setSidechainHistoryVisible (showSidechain.getToggleState());
        syncHistoryPreferences(); gateProcessor.editorPreferencesChanged();
    };
    setupButton (audition, "AUDITION SC");
    audition.setComponentID ("auditionControl");
    audition.setTooltip ("Hear the filtered sidechain instead of the Gate output. Stays on across pages and threshold/hysteresis edits. Cyan SC underline = audition active. Escape or Bypass stops it.");
    setupButton (preOpen, "PREOPEN");
    setupButton (flip, "FLIP");
    flip.setComponentID ("flipControl");
    flip.setTooltip ("Reverse the gate: pass quiet signals and attenuate signals above the threshold.");
    preOpen.setComponentID ("preOpenControl");
    setupButton (kneeView, "KNEE");
    kneeView.setComponentID ("kneeView");
    kneeView.onClick = [this] { display.setKneeVisible (detailsOpen && kneeView.getToggleState()); };
    setupButton (bypass, "BYPASS");
    bypass.setComponentID ("bypassControl");
    setupButton (freeze, "FREEZE"); freeze.setComponentID ("historyFreeze");
    setupButton (hoverHelp, "?"); hoverHelp.setComponentID ("hoverHelp");
    hoverHelp.setTooltip ("Enable hover explanations. Off by default; saved with this instance.");
    hoverHelp.onClick = [this]
    {
        gateProcessor.hoverHelpEnabled = hoverHelp.getToggleState();
        syncHistoryPreferences(); gateProcessor.editorPreferencesChanged();
    };
    freeze.setTooltip ("Freeze history, then drag to inspect. Shift-drag selects a quiet passage for Learn Noise.");
    freeze.onClick = [this]
    {
        display.setFrozen (freeze.getToggleState()); suggestionReady = false;
        apply.setEnabled (false);
        learnStatus.setText ("Freeze, then Shift-drag\n0.5s or more of quiet noise", juce::dontSendNotification);
    };
    setupButton (detectorView, "TRACE"); detectorView.setComponentID ("detectorView");
    detectorView.setTooltip ("Show the actual detector level instead of SHOW SC, after filtering, stereo linking and lookahead: violet for Smooth, amber for Peak. Captured history retains its mode colour. Display only; switching views does not stop SC audition or change detection.");
    detectorView.onClick = [this]
    {
        gateProcessor.setDetectorHistoryVisible (detectorView.getToggleState());
        syncHistoryPreferences(); gateProcessor.editorPreferencesChanged();
    };
    for (auto* button : { &undo, &redo, &snapshotA, &snapshotB, &copy, &removed, &learn, &apply })
        addAndMakeVisible (button);
    undo.setName ("Undo"); undo.setComponentID ("undoButton"); undo.setTooltip ("Undo the last edit (Command-Z)");
    redo.setName ("Redo"); redo.setComponentID ("redoButton"); redo.setTooltip ("Redo the last edit (Command-Shift-Z)");
    undo.onClick = [this] { stopAudition(); gateProcessor.parameters.copyState(); gateProcessor.undoManager.undo(); timerCallback(); };
    redo.onClick = [this] { stopAudition(); gateProcessor.parameters.copyState(); gateProcessor.undoManager.redo(); timerCallback(); };
    snapshotA.setComponentID ("snapshotA"); snapshotB.setComponentID ("snapshotB"); copy.setComponentID ("snapshotCopy");
    snapshotA.onClick = [this] { stopAudition(); gateProcessor.selectSnapshot (0); timerCallback(); };
    snapshotB.onClick = [this] { stopAudition(); gateProcessor.selectSnapshot (1); timerCallback(); };
    for (auto* button : { &snapshotA, &snapshotB, &copy })
        button->setTooltip ("Compare settings without changing Bypass, Preopen or Max latency. Lookahead is capped by the current Max.");
    copy.onClick = [this] { gateProcessor.copySnapshot(); copyFeedbackTicks = 15; timerCallback(); };
    removed.setComponentID ("removedAudition");
    removed.setButtonText ("REMOVED");
    removed.setTooltip ("Hold to hear what the gate removes, including Mix but excluding output gain. Release to return.");
    removed.onStateChange = [this]
    {
        const bool on = removed.isDown() && detailsOpen && ! pagesSuspended && isVisible()
            && ! bypass.getToggleState();
        if (on) { audition.setToggleState (false, juce::sendNotificationSync); gateProcessor.setAuditionSidechain (false); }
        gateProcessor.setAuditionRemoved (on);
    };
    learn.setComponentID ("learnNoise"); apply.setComponentID ("applyNoise");
    learn.setTooltip ("Freeze history and Shift-drag at least 0.5 seconds of noise only. Proposes settings; does not apply them.");
    learn.onClick = [this] { learnNoise(); };
    apply.onClick = [this]
    {
        if (! suggestionReady) return;
        if (detectorSettings() != learnedDetectorSettings)
        {
            suggestionReady = false; apply.setEnabled (false);
            learnStatus.setText ("Detector changed - refreeze and learn again", juce::dontSendNotification);
            return;
        }
        beginEditTransaction();
        for (auto pair : { std::pair<const char*, float> { "threshold", suggestedThreshold }, { "hysteresis", suggestedHysteresis } })
        {
            auto* parameter = gateProcessor.parameters.getParameter (pair.first);
            parameter->beginChangeGesture(); parameter->setValueNotifyingHost (parameter->convertTo0to1 (pair.second)); parameter->endChangeGesture();
        }
        gateProcessor.parameters.copyState(); gateProcessor.undoManager.beginNewTransaction();
        suggestionReady = false;
        learnStatus.setText ("Noise settings applied", juce::dontSendNotification);
    };
    for (auto* label : { &scStatus, &learnStatus })
    {
        label->setFont (juce::FontOptions (9.0f)); label->setColour (juce::Label::textColourId, GateTheme::dim);
        label->setJustificationType (juce::Justification::centredLeft); label->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
        label->getProperties().set ("wrapped", true);
        label->setMinimumHorizontalScale (0.95f);
    }
    learnStatus.setText ("Freeze, then Shift-drag\n0.5s or more of quiet noise", juce::dontSendNotification);
    maxLookahead.addItemList ({ "Max 0 ms", "Max 1 ms", "Max 2 ms", "Max 5 ms", "Max 10 ms", "Max 20 ms" }, 1);
    maxLookahead.setComponentID ("maxLookaheadControl");
    maxLookahead.setJustificationType (juce::Justification::centred);
    maxLookahead.setTooltip ("Sets the maximum lookahead and host latency while Preopen is enabled.");
    addAndMakeVisible (maxLookahead);
    detectorMode.addItem ("Smooth", 2);
    detectorMode.addItem ("Peak", 1);
    detectorMode.setComponentID ("detectorModeControl");
    detectorMode.setJustificationType (juce::Justification::centred);
    detectorMode.setTooltip ("Detection mode: Smooth (default, violet trace) uses 0.5 ms detector attack / 30 ms decay. Peak (amber trace) catches a single-sample transient immediately, bridges bass zero crossings (1-26 ms), then recovers over 4 ms. Neither replaces gain Attack, Hold or Release.");
    addAndMakeVisible (detectorMode);
    for (auto* button : { &details, &scView }) button->onClick = [this, button]
    {
        pagesSuspended = false;
        if (button->getToggleState())
            (button == &details ? scView : details).setToggleState (false, juce::dontSendNotification);
        detailsOpen = details.getToggleState();
        sidechainOpen = scView.getToggleState();
        if (! detailsOpen) gateProcessor.setAuditionRemoved (false);
        if (button == &scView) suggestionReady = false;
        const int page = sidechainOpen ? 2 : detailsOpen ? 1 : 0;
        if (gateProcessor.editorPage.exchange (page) != page) gateProcessor.editorPreferencesChanged();
        updateDetailsVisibility();
        resized();
        repaint();
    };
    audition.onClick = [this]
    {
        const bool on = audition.getToggleState() && ! bypass.getToggleState();
        gateProcessor.setAuditionSidechain (on);
        if (on) gateProcessor.setAuditionRemoved (false);
        syncHistoryPreferences();
        if (on) gateProcessor.editorPreferencesChanged();
        display.setSidechainFocus (on);
        scView.getProperties().set ("auditionActive", on);
        scView.repaint();
    };

    auto& state = gateProcessor.parameters;
    auto attachSlider = [&] (const char* id, juce::Slider& slider)
    {
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, id, slider));
    };
    attachSlider ("threshold", threshold); attachSlider ("ratio", ratio);
    attachSlider ("range", range); attachSlider ("attack", attack);
    attachSlider ("hold", hold); attachSlider ("release", release);
    attachSlider ("knee", knee); attachSlider ("hysteresis", hysteresis);
    attachSlider ("sideHighPass", sideHighPass);
    attachSlider ("sideLowPass", sideLowPass); attachSlider ("input", input);
    attachSlider ("output", output); attachSlider ("mix", mix); attachSlider ("link", link);
    for (auto* slider : { &threshold, &hysteresis, &ratio, &range, &attack, &release,
                         &knee, &hold, &lookahead, &input, &output, &mix, &link, &sideHighPass, &sideLowPass })
    {
        // Attachments install their own parser; restore our unit-aware, validated one afterwards.
        slider->valueFromTextFunction = [slider] (const juce::String& text) { return parseSliderValue (*slider, text); };
        slider->textFromValueFunction = [slider] (double value)
        {
            const bool detail = (bool) slider->getProperties()["detailKnob"];
            if (detail && slider->getTextValueSuffix().trim() == "Hz" && value >= 1000)
                return juce::String (value / 1000.0, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "k";
            const int places = slider->getNumDecimalPlacesToDisplay();
            if (std::abs (value) < 0.5 * std::pow (10.0, -places)) value = 0.0;
            auto formatted = juce::String (value, places);
            return detail && formatted.containsChar ('.')
                ? formatted.trimCharactersAtEnd ("0").trimCharactersAtEnd (".") : formatted;
        };
        slider->updateText();
    }
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "externalSidechain", externalSidechain));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "lookaheadEnabled", preOpen));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "bypass", bypass));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "flip", flip));
    scView.getProperties().set ("externalActive", externalSidechain.getToggleState());
    externalSidechain.onStateChange = [this]
    {
        const bool wasEnabled = (bool) scView.getProperties()["externalActive"];
        if (externalSidechain.getToggleState() && ! wasEnabled)
            gateProcessor.setSidechainHistoryVisible (true);
        scView.getProperties().set ("externalActive", externalSidechain.getToggleState());
        scView.repaint();
        display.setSidechainFocus (audition.getToggleState());
        syncHistoryPreferences();
        suggestionReady = false;
    };
    externalSidechain.onStateChange();
    maximumAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, "maxLookahead", maxLookahead);
    // Bind stable item IDs, not menu indices: Smooth can appear first without
    // reversing existing saved presets or host automation (Peak 0 / Smooth 1).
    detectorModeAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter ("detectorMode"),
        [this] (float value)
        {
            detectorMode.setSelectedId (value < 0.5f ? 1 : 2, juce::dontSendNotification);
            detectorView.getProperties().set ("peakMode", value < 0.5f);
            detectorView.repaint();
        }, &gateProcessor.undoManager);
    detectorMode.onChange = [this]
    {
        if (detectorMode.getSelectedId() != 0)
            detectorModeAttachment->setValueAsCompleteGesture ((float) (detectorMode.getSelectedId() - 1));
    };
    detectorModeAttachment->sendInitialUpdate();
    lookaheadAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter ("lookahead"),
        [this] (float value) { storedLookahead = value; updateLookaheadAvailability(); }, &gateProcessor.undoManager);
    lookaheadAttachment->sendInitialUpdate();
    lookahead.onDragStart = [this] { beginEditTransaction(); lookaheadDragging = true; lookaheadAttachment->beginGesture(); };
    lookahead.onDragEnd = [this] { lookaheadAttachment->endGesture(); lookaheadDragging = false; gateProcessor.parameters.copyState(); gateProcessor.undoManager.beginNewTransaction(); };
    lookahead.onValueChange = [this]
    {
        if (updatingLookahead) return;
        if (lookaheadDragging)
            lookaheadAttachment->setValueAsPartOfGesture ((float) lookahead.getValue());
        else
            lookaheadAttachment->setValueAsCompleteGesture ((float) lookahead.getValue());
    };
    maxLookahead.onChange = [this] { updateLookaheadAvailability(); };
    preOpen.onStateChange = [this] { updateLookaheadAvailability(); };
    updateLookaheadAvailability();
    restoreEditorPage();
    updateDetailsVisibility();
    resized();
    threshold.setTooltip ("Opening threshold. Drag its line or badge; hold Shift for fine adjustment.");
    hysteresis.setTooltip ("Drag the light-purple return-threshold line: Close normally, Open in Flip. Shift is fine adjustment; Alt selects it when lines overlap.");
    ratio.setTooltip ("Downward expansion slope; 1:1 is neutral and 100:1 is a hard gate. Flip reverses which side is attenuated.");
    range.setTooltip ("Maximum attenuation in dB. 0 dB leaves the signal unchanged.");
    attack.setTooltip ("Gain-opening time constant, separate from Peak/Smooth detection. Peak catches transients immediately; Attack still sets how quickly the audio gain opens.");
    release.setTooltip ("Closing time constant after the detector crosses the return threshold and Hold ends. Gain follows an exponential dB fade, not a fixed-duration fade: at 80 dB Range, one time constant reaches about 51 dB attenuation.");
    hold.setTooltip ("Hold the current gain after the detector crosses the closing threshold.");
    knee.setTooltip ("Width of the smooth transition around the active threshold. A soft knee begins attenuating above that line (below it in Flip); 0 dB gives a hard knee. Hysteresis selects the return threshold while open.");
    input.setTooltip ("Input gain before the gate and internal detector.");
    output.setTooltip ("Output gain after the wet/dry mix.");
    mix.setTooltip ("Blend the dry and gated signals. Both paths use the same latency.");
    link.setTooltip ("Stereo detector and gain linking. 100% keeps the same gain on both channels.");
    sideHighPass.setTooltip ("24 dB/oct Butterworth detector high-pass, matching Open Compressor. 20 Hz–20 kHz scale; 20 Hz is Off. Stops at least 1/3 octave below LP without moving it. Drag the purple spectrum line/cap or type Hz/kHz.");
    sideLowPass.setTooltip ("24 dB/oct Butterworth detector low-pass, matching Open Compressor. 20 Hz–20 kHz scale; 20 kHz is Off. Stops at least 1/3 octave above HP without moving it. Drag the cyan spectrum line/cap or type Hz/kHz.");
    details.setTooltip ("Show secondary dynamics, gain, mix, and latency controls.");
    scView.setTooltip ("Show sidechain controls. Purple tint = Ext. SC enabled; cyan underline = SC audition active, even when this page is closed. Escape stops audition.");
    externalSidechain.setTooltip ("Use the host's external sidechain. With no routed input, the internal signal is used.");
    preOpen.setTooltip ("Enable the selected Max latency and lookahead. Turning this off restores zero latency.");
    kneeView.setTooltip ("Show the opening transfer curve in Details: horizontal axis = detector input dB, vertical axis = gated output dB, before Mix/output gain. Flip reverses the attenuated side, not the output axis. Hysteresis shifts the return threshold; Hold temporarily freezes gain.");
    bypass.setTooltip ("Pass the original input, aligned to the selected latency.");
    setWantsKeyboardFocus (true);
    for (auto* button : { (juce::Button*) &undo, (juce::Button*) &redo, (juce::Button*) &hoverHelp, (juce::Button*) &freeze,
                         (juce::Button*) &details, (juce::Button*) &scView, (juce::Button*) &flip,
                         (juce::Button*) &bypass })
        button->getProperties().set ("topBar", true);
    viewInitialized = true;
    timerCallback();
    // Keep controls live: transparent corners must reveal this repaint's
    // history, including native clipped knob/hover repaints.
    startTimerHz (15);
}

OpenGateAudioProcessorEditor::~OpenGateAudioProcessorEditor()
{
    stopTimer();
    display.onAnimationFrame = {};
    viewInitialized = false;
    display.setDisplayActive (false);
    sidechainResponse.setDisplayActive (false);
    for (auto* slider : { &threshold, &hysteresis, &ratio, &range, &attack, &release, &knee, &hold,
                         &lookahead, &sideHighPass, &sideLowPass, &input, &output, &mix, &link }) finishSliderInteraction (*slider);
    tooltips.reset();
    removeMouseListener (this);
    gateProcessor.setAuditionSidechain (false);
    gateProcessor.setAuditionRemoved (false);
    setLookAndFeel (nullptr);
}

void OpenGateAudioProcessorEditor::setupKnob (juce::Slider& slider, juce::Label& label,
                                               const juce::String& name, const juce::String& suffix,
                                               double initial, bool detail)
{
    slider.setName (name);
    slider.setComponentID (name.toLowerCase() + "Control");
    slider.getProperties().set ("detailKnob", detail);
    slider.setLookAndFeel (&lookAndFeel);
    slider.setColour (juce::Slider::textBoxTextColourId, GateTheme::text);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, detail ? 62 : 78, detail ? 14 : 18);
    slider.setTextValueSuffix (suffix);
    slider.setDoubleClickReturnValue (true, initial);
    slider.setMouseDragSensitivity (180);
    addAndMakeVisible (slider);
    label.setText (name, juce::dontSendNotification);
    label.getProperties().set ("detailKnob", detail);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, GateTheme::dim);
    label.setFont (juce::FontOptions (detail ? 8.5f : 9.0f, juce::Font::bold));
    label.setMinimumHorizontalScale (0.9f);
    addAndMakeVisible (label);
}

void OpenGateAudioProcessorEditor::setupButton (juce::ToggleButton& button, const juce::String& name)
{
    button.setButtonText (name);
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
}

void OpenGateAudioProcessorEditor::restoreEditorPage()
{
    const int page = juce::jlimit (0, 2, gateProcessor.editorPage.load());
    if (detailsOpen == (page == 1) && sidechainOpen == (page == 2)) return;
    // State loads/recreated editors restore the selected page, not a temporary
    // threshold-edit suspension. Never write the restored page back into A/B.
    pagesSuspended = false;
    suggestionReady = false;
    gateProcessor.setAuditionRemoved (false);
    detailsOpen = page == 1; sidechainOpen = page == 2;
    pageFade.request(page,juce::Time::getMillisecondCounterHiRes(),false);
    display.setBadgePageVisible(page!=0,false);
    details.setToggleState (detailsOpen, juce::dontSendNotification);
    scView.setToggleState (sidechainOpen, juce::dontSendNotification);
    updateDetailsVisibility(); resized(); repaint();
}

void OpenGateAudioProcessorEditor::updateDetailsVisibility()
{
    const int previous=pageFade.presented();
    if(previous==2&&(pagesSuspended||!sidechainOpen))sidechainResponse.endDrag();
    display.setBadgePageVisible(!pagesSuspended&&(detailsOpen||sidechainOpen),viewInitialized&&displayActive);
    pageFade.request(pagesSuspended?0:sidechainOpen?2:detailsOpen?1:0,
                     juce::Time::getMillisecondCounterHiRes(),viewInitialized&&displayActive);
    applyPageFade();
    if(previous!=pageFade.presented())resized();
}

void OpenGateAudioProcessorEditor::applyPageFade()
{
    const int page=pageFade.presented();const float alpha=pageFade.alpha();
    const bool interactive=pageFade.interactive()&&!pagesSuspended;
    advancedPanel.setVisible(page!=0);advancedPanel.setAlpha(alpha);
    advancedPanel.setInterceptsMouseClicks(interactive,false);
    juce::Component* detailControls[] { &knee, &kneeLabel, &hold, &holdLabel,
                                        &lookahead, &lookaheadLabel, &input, &inputLabel,
                                        &output, &outputLabel, &mix, &mixLabel, &link, &linkLabel,
                                        &preOpen, &kneeView, &maxLookahead, &snapshotA, &snapshotB, &copy, &removed };
    for(auto*c:detailControls){c->setVisible(page==1);c->setAlpha(alpha);c->setInterceptsMouseClicks(interactive,interactive);}
    juce::Component* sidechainControls[] { &sideHighPass, &sideHighPassLabel,
                                           &sideLowPass, &sideLowPassLabel,
                                           &externalSidechain, &audition, &detectorView, &detectorMode, &scStatus,
                                           &showSidechain, &learn, &apply, &learnStatus, &sidechainResponse };
    for(auto*c:sidechainControls){c->setVisible(page==2);c->setAlpha(alpha);c->setInterceptsMouseClicks(interactive,interactive);}
    display.setPanelMode(page);
    display.setKneeVisible(page==1&&kneeView.getToggleState());
    syncHistoryPreferences();
}

void OpenGateAudioProcessorEditor::advancePageFade(double timeMs)
{
    if(!displayActive)return;
    const int previous=pageFade.presented();
    if(!pageFade.advance(timeMs))return;
    applyPageFade();
    if(previous!=pageFade.presented())resized();
}

void OpenGateAudioProcessorEditor::syncHistoryPreferences()
{
    const auto overlay = gateProcessor.historyOverlayMode();
    showSidechain.setToggleState (overlay == OpenGateAudioProcessor::HistoryOverlay::sidechain, juce::dontSendNotification);
    detectorView.setToggleState (overlay == OpenGateAudioProcessor::HistoryOverlay::detector, juce::dontSendNotification);
    hoverHelp.setToggleState (gateProcessor.hoverHelpEnabled.load(), juce::dontSendNotification);
    display.setSidechainVisible (showSidechain.getToggleState());
    display.setDetectorVisible (detectorView.getToggleState());
    if (hoverHelp.getToggleState() && ! tooltips)
        tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
    else if (! hoverHelp.getToggleState() && tooltips)
        tooltips.reset();
}

void OpenGateAudioProcessorEditor::updateLookaheadAvailability()
{
    juce::ScopedValueSetter<bool> guard (updatingLookahead, true);
    static constexpr double times[] { 0, 1, 2, 5, 10, 20 };
    const double maximum = times[juce::jlimit (0, 5, maxLookahead.getSelectedItemIndex())];
    const float stored = storedLookahead;
    lookahead.setNormalisableRange ({ 0.0, juce::jmax (0.01, maximum), 0.01, 1.0 });
    lookahead.setValue (juce::jmin ((double) stored, maximum), juce::dontSendNotification);
    const bool enabled = preOpen.getToggleState();
    maxLookahead.setEnabled (enabled);
    lookahead.setEnabled (enabled && maximum > 0);
    lookaheadLabel.setEnabled (enabled && maximum > 0);
    const double effective = enabled ? juce::jmin ((double) stored, maximum) : 0.0;
    lookahead.setTooltip ("Detector lead: " + juce::String (effective, 2) + " ms."
                         + (stored > maximum ? " Saved setting: " + juce::String (stored, 2) + " ms." : "")
                         + " Max sets the knob's full scale and host latency.");
}

void OpenGateAudioProcessorEditor::stopAudition()
{
    audition.setToggleState (false, juce::sendNotificationSync);
    gateProcessor.setAuditionSidechain (false);
    gateProcessor.setAuditionRemoved (false);
    display.setSidechainFocus (false);
    scView.getProperties().set ("auditionActive", false);
    scView.repaint();
}

void OpenGateAudioProcessorEditor::beginEditTransaction()
{
    gateProcessor.parameters.copyState();
    gateProcessor.undoManager.beginNewTransaction();
}

void OpenGateAudioProcessorEditor::hideAdvancedPages()
{
    if (pagesSuspended || (! detailsOpen && ! sidechainOpen)) return;
    pagesSuspended = true;
    gateProcessor.setAuditionRemoved (false);
    updateDetailsVisibility();
    repaint();
}

void OpenGateAudioProcessorEditor::restoreAdvancedPages()
{
    if (! pagesSuspended || referenceEditing || juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown()
        || juce::Time::getMillisecondCounterHiRes() < editUntil) return;
    for (auto* slider : { &threshold, &hysteresis })
        for (int i = 0; i < slider->getNumChildComponents(); ++i)
            if (auto* label = dynamic_cast<juce::Label*> (slider->getChildComponent (i)))
                if (label->isBeingEdited()) return;
    pagesSuspended = false;
    updateDetailsVisibility();
    repaint();
}

void OpenGateAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    if (event.eventComponent == &sidechainResponse) return; // Graph owns its gesture/undo transaction.
    beginEditTransaction();
    for (auto* slider : { &threshold, &hysteresis })
        if (event.eventComponent == slider || slider->isParentOf (event.eventComponent)) hideAdvancedPages();
}

void OpenGateAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    gateProcessor.parameters.copyState();
    restoreAdvancedPages();
}

void OpenGateAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails&)
{
    for (auto* slider : { &threshold, &hysteresis })
        if (event.eventComponent == slider || slider->isParentOf (event.eventComponent))
        {
            editUntil = juce::Time::getMillisecondCounterHiRes() + 350;
            hideAdvancedPages();
        }
}

void OpenGateAudioProcessorEditor::referenceTextEdit (bool active)
{
    referenceEditing = active;
    if (active) { beginEditTransaction(); hideAdvancedPages(); }
    else
    {
        gateProcessor.parameters.copyState(); gateProcessor.undoManager.beginNewTransaction();
        restoreAdvancedPages();
    }
}

void OpenGateAudioProcessorEditor::learnNoise()
{
    suggestionReady = false;
    if (flip.getToggleState())
        learnStatus.setText ("Noise Learn is for normal gating", juce::dontSendNotification);
    else if (! display.isFrozen())
        learnStatus.setText ("Freeze, then Shift-drag noise only", juce::dontSendNotification);
    else if (! display.suggestNoiseSettings (suggestedThreshold, suggestedHysteresis))
        learnStatus.setText ("Select at least 0.5s of audible quiet noise", juce::dontSendNotification);
    else
    {
        suggestionReady = true;
        learnedDetectorSettings = detectorSettings();
        learnStatus.setText ("Suggested " + juce::String (suggestedThreshold, 1) + " dB\nHysteresis 3 dB", juce::dontSendNotification);
    }
    apply.setEnabled (suggestionReady);
}

std::array<float, 9> OpenGateAudioProcessorEditor::detectorSettings() const
{
    std::array<float, 9> values {};
    const std::array<const char*, 9> ids { "input", "sideHighPass", "sideLowPass", "link", "externalSidechain",
                                         "lookahead", "lookaheadEnabled", "maxLookahead", "detectorMode" };
    for (size_t i = 0; i < ids.size(); ++i) values[i] = gateProcessor.parameters.getRawParameterValue (ids[i])->load();
    return values;
}

void OpenGateAudioProcessorEditor::timerCallback()
{
    if (getPeer() != nullptr) setDisplayActive (isShowing());
    if (! displayActive) return;
    restoreEditorPage();
    syncHistoryPreferences();
    updateLookaheadAvailability();
    restoreAdvancedPages();
    advancePageFade(juce::Time::getMillisecondCounterHiRes());
    gateProcessor.parameters.copyState();
    undo.setEnabled (gateProcessor.undoManager.canUndo()); redo.setEnabled (gateProcessor.undoManager.canRedo());
    snapshotA.setToggleState (gateProcessor.getSnapshot() == 0, juce::dontSendNotification);
    snapshotB.setToggleState (gateProcessor.getSnapshot() == 1, juce::dontSendNotification);
    copy.setButtonText (copyFeedbackTicks-- > 0 ? "COPIED" : gateProcessor.getSnapshot() == 0 ? "COPY A > B" : "COPY B > A");
    copyFeedbackTicks = juce::jmax (0, copyFeedbackTicks);
    if (bypass.getToggleState() || (audition.getToggleState() && ! gateProcessor.isAuditioningSidechain())) stopAudition();
    audition.setEnabled (! bypass.getToggleState());
    removed.setEnabled (! bypass.getToggleState());
    if (! removed.isDown() || ! detailsOpen || pagesSuspended || ! isVisible()) gateProcessor.setAuditionRemoved (false);
    if (! display.isFrozen() || flip.getToggleState() || bypass.getToggleState()) suggestionReady = false;
    if (suggestionReady && detectorSettings() != learnedDetectorSettings)
    {
        suggestionReady = false;
        learnStatus.setText ("Detector changed - refreeze and learn again", juce::dontSendNotification);
    }
    apply.setEnabled (suggestionReady);
    learn.setEnabled (! bypass.getToggleState() && ! flip.getToggleState());
    const bool external = externalSidechain.getToggleState(), available = gateProcessor.externalBusAvailable();
    scStatus.setText (external ? available ? "External sidechain\nHost bus enabled"
                                         : "Ext. SC unavailable\nUsing internal input"
                              : "Internal detector\nInput gain + SC filters", juce::dontSendNotification);
    scStatus.setColour (juce::Label::textColourId, external && available ? GateTheme::sidechain : GateTheme::dim);
    scStatus.setTooltip ("The bus must be routed in your host. A connected but silent bus does not fall back to internal audio.");
    sidechainResponse.updateSpectrum (display.isFrozen());
    if (sidechainResponse.isVisible()) sidechainResponse.repaint();
    const float closingDb = gateProcessor.parameters.getRawParameterValue ("threshold")->load()
        + (flip.getToggleState() ? 1 : -1) * gateProcessor.parameters.getRawParameterValue ("hysteresis")->load();
    hysteresis.setTooltip (juce::String (flip.getToggleState() ? "Opening threshold: " : "Closing threshold: ")
        + juce::String (closingDb, 1) + " dB."
        + (closingDb < -80 || closingDb > 0 ? " Outside the history scale; the purple edge arrow indicates it." : " Drag the light-purple line to adjust.")
        + " Shift is fine adjustment; Alt selects it when lines overlap. Flip reverses its direction.");
}

bool OpenGateAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { sidechainResponse.endDrag(); stopAudition(); return true; }
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
    if (key.getModifiers().isCommandDown() && juce::CharacterFunctions::toLowerCase (key.getTextCharacter()) == 'z')
    {
        stopAudition(); gateProcessor.parameters.copyState();
        if (key.getModifiers().isShiftDown()) gateProcessor.undoManager.redo(); else gateProcessor.undoManager.undo();
        timerCallback(); return true;
    }
    return false;
}

void OpenGateAudioProcessorEditor::focusLost (FocusChangeType) { sidechainResponse.endDrag(); gateProcessor.setAuditionRemoved (false); }
void OpenGateAudioProcessorEditor::setDisplayActive (bool enabled)
{
    if (displayActive == enabled) return;
    displayActive = enabled;
    display.setDisplayActive (enabled);
    sidechainResponse.setDisplayActive (enabled);
    freeze.setToggleState (false, juce::dontSendNotification);
    suggestionReady = false;
    if (! enabled)
    {
        sidechainResponse.endDrag(); stopAudition();
        for (auto* slider : { &threshold, &hysteresis, &ratio, &range, &attack, &release, &knee, &hold,
                             &lookahead, &sideHighPass, &sideLowPass, &input, &output, &mix, &link }) finishSliderInteraction (*slider);
        referenceEditing = false; editUntil = 0; pagesSuspended = false;
        updateDetailsVisibility();
    }
    else restoreEditorPage();
    // Reused hidden editors only poll visibility; destroyed editors have no timers.
    startTimerHz (enabled ? 15 : 2);
}

void OpenGateAudioProcessorEditor::visibilityChanged()
{
    if (viewInitialized) setDisplayActive (isVisible());
}

void OpenGateAudioProcessorEditor::parentHierarchyChanged()
{
    if (viewInitialized && getPeer() != nullptr) setDisplayActive (isShowing());
}

void OpenGateAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (GateTheme::background);
    g.setColour (GateTheme::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("OPEN GATE", 18, 7, 190, 25, juce::Justification::left);
    g.setColour (GateTheme::magenta);
    g.fillRoundedRectangle (18.0f, 35.0f, 44.0f, 2.5f, 1.25f);
    g.setColour (GateTheme::dim);
    g.setFont (juce::FontOptions (9.0f));
    g.setColour (GateTheme::dim);
    g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
    g.drawText ("I", meter.getX() + 5, 37, 9, 10, juce::Justification::centred);
    g.drawText ("O", meter.getX() + 17, 37, 9, 10, juce::Justification::centred);
    g.drawText ("A", meter.getX() + 34, 37, 10, 10, juce::Justification::centred);
}

void OpenGateAudioProcessorEditor::resized()
{
    const int width = getWidth(), height = getHeight();
    int toolbarX = width - 442;
    const std::array<juce::Button*, 8> buttons { &undo, &redo, &hoverHelp, &freeze, &details, &scView, &flip, &bypass };
    const std::array<int, 8> widths { 24, 24, 24, 62, 74, 50, 50, 84 };
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        buttons[i]->setBounds (toolbarX, 7, widths[i], 28); toolbarX += widths[i] + 4;
    }
    constexpr int meterWidth = 66;
    display.setBounds (14, 48, width - meterWidth - 30, height - 62);
    meter.setBounds (width - meterWidth - 12, 48, meterWidth, height - 62);

    const auto overlay = display.getBounds();
    const auto panelBounds = GateLayout::detailsPanel (overlay);
    advancedPanel.setBounds (panelBounds);
    if (pageFade.presented()==1)
    {
        auto content = panelBounds.reduced (10, 0);
        const auto utility = content.removeFromRight (174);
        content.removeFromRight (8);
        const int cellWidth = (content.getWidth() - 12) / 7;
        int index = 0;
        for (auto pair : std::initializer_list<std::pair<juce::Slider*, juce::Label*>> {
                 { &input, &inputLabel },
                 { &output, &outputLabel }, { &mix, &mixLabel }, { &link, &linkLabel },
                 { &knee, &kneeLabel }, { &hold, &holdLabel }, { &lookahead, &lookaheadLabel } })
        {
            if (index++ == 4) content.removeFromLeft (12);
            auto cell = content.removeFromLeft (cellWidth);
            pair.second->setBounds (cell.getX(), cell.getY() + 7, cell.getWidth(), 11);
            pair.first->setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (66, cell.getWidth()), 14);
            pair.first->setBounds (cell.getX(), cell.getY() + 18, cell.getWidth(), 62);
            pair.first->updateText();
        }
        const int x = utility.getX(), y = panelBounds.getY() + 6;
        preOpen.setBounds (x, y, 108, 21);
        kneeView.setBounds (x + 112, y, 62, 21);
        maxLookahead.setBounds (x, y + 25, 108, 21);
        removed.setBounds (x + 112, y + 25, 62, 21);
        snapshotA.setBounds (x, y + 50, 26, 21);
        snapshotB.setBounds (x + 30, y + 50, 26, 21);
        copy.setBounds (x + 60, y + 50, 114, 21);
    }
    if (pageFade.presented()==2)
    {
        auto content = panelBounds.reduced (10, 0);
        const auto routing = content.removeFromLeft (128);
        content.removeFromLeft (8);
        const auto learning = content.removeFromRight (126);
        content.removeFromRight (8);
        const int x = routing.getX(), y = panelBounds.getY() + 6;
        externalSidechain.setBounds (x, y, 60, 21);
        showSidechain.setBounds (x + 64, y, 64, 21);
        audition.setBounds (x, y + 25, 128, 21);
        scStatus.setBounds (x + 2, y + 48, 124, 26);
        constexpr int cellWidth = 54;
        auto filters = content.removeFromLeft (cellWidth * 2);
        content.removeFromLeft (8);
        sidechainResponse.setBounds (content.withTrimmedTop (6).withTrimmedBottom (6));
        for (auto pair : std::initializer_list<std::pair<juce::Slider*, juce::Label*>> {
                 { &sideHighPass, &sideHighPassLabel }, { &sideLowPass, &sideLowPassLabel } })
        {
            auto cell = filters.removeFromLeft (cellWidth);
            pair.second->setBounds (cell.getX(), panelBounds.getY() + 7, cell.getWidth(), 11);
            pair.first->setTextBoxStyle (juce::Slider::TextBoxBelow, false, cellWidth, 14);
            pair.first->setBounds (cell.getX(), panelBounds.getY() + 18, cell.getWidth(), 62);
        }
        const juce::Rectangle<int> learningLeft (learning.getX(), y, 76, 21);
        const juce::Rectangle<int> learningRight (learning.getX() + 80, y, 46, 21);
        detectorMode.setBounds (learningLeft);
        detectorView.setBounds (learningRight);
        learn.setBounds (learningLeft.translated (0, 25));
        apply.setBounds (learningRight.translated (0, 25));
        learnStatus.setBounds (learning.getX() + 2, y + 48, 122, 26);
    }

    const int line = overlay.getY() + juce::roundToInt (overlay.getHeight() * 2.0f / 3.0f);
    const int controlTop = juce::jmax (overlay.getBottom() - 145, line + 6);
    display.setBadgeGeometry(juce::jmax(display.getHeight()-145,panelBounds.getBottom()-display.getY()+6),panelBounds.getY()-display.getY()-3);
    auto controls = juce::Rectangle<int> (overlay.getX() + 8, controlTop,
                                          overlay.getWidth() - 16, overlay.getBottom() - 11 - controlTop);
    const int cellWidth = controls.getWidth() / 6;
    for (auto pair : std::initializer_list<std::pair<juce::Slider*, juce::Label*>> {
             { &threshold, &thresholdLabel }, { &hysteresis, &hysteresisLabel },
             { &ratio, &ratioLabel }, { &range, &rangeLabel },
             { &attack, &attackLabel }, { &release, &releaseLabel } })
    {
        auto cell = controls.removeFromLeft (cellWidth).reduced (6, 0);
        pair.second->setBounds (cell.removeFromTop (15));
        pair.first->setBounds (pair.first->getParentComponent() == &display
            ? cell.translated (-display.getX(), -display.getY()) : cell);
    }
}

juce::Colour GateLookAndFeel::valueAccent(juce::Component& c)
{ if(auto* slider=c.findParentComponentOfClass<juce::Slider>()){
        const auto name=slider->getName();return name=="THRESHOLD"?GateTheme::magenta:name=="SC LP"?GateTheme::sidechain:GateTheme::violet;
    }return GateTheme::violet; }
void GateLookAndFeel::fillTextEditorBackground(juce::Graphics& g,int w,int h,juce::TextEditor& editor)
{
    if(OpenEditableValueStyle::isValueEditor(editor))valueStyle.observeEditor(editor);
    if(OpenEditableValueStyle::isValueEditor(editor))OpenEditableValueStyle::fillEditor(g,w,h,GateTheme::background);
    else juce::LookAndFeel_V4::fillTextEditorBackground(g,w,h,editor);
}
void GateLookAndFeel::drawTextEditorOutline(juce::Graphics& g,int w,int h,juce::TextEditor& editor)
{
    if(OpenEditableValueStyle::isValueEditor(editor))OpenEditableValueStyle::outlineEditor(g,w,h,editor,valueAccent(editor),GateTheme::edge);
    else juce::LookAndFeel_V4::drawTextEditorOutline(g,w,h,editor);
}
