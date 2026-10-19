#include "CompressorEditor.h"
#include "CompressorTheme.h"

CompressorSidechainPreview::CompressorSidechainPreview (OpenCompressorAudioProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (true, false);
    spectrumLevels.fill (-96.0f);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (fftSize - 1));
    setTooltip ("Filtered detector spectrum, 0 to -96 dBFS. Orange line = HP, mint line = LP; the tinted band marks the selected frequency range, not an EQ response. Drag lines/caps; Shift = fine, Alt prefers LP. Cutoffs stay at least 1/3 octave apart. HP 20 Hz / LP 20 kHz are Off. Freeze holds the spectrum.");
}

void CompressorSidechainPreview::visibilityChanged()
{
    processor.setSpectrumEnabled (displayActive && isVisible());
    if (! displayActive || ! isVisible()) endDrag();
    // Never replay queued audio from the previous page visit.
    OpenCompressorAudioProcessor::SpectrumFrame frame;
    for (int i = 0; i < OpenCompressorAudioProcessor::spectrumCapacity && processor.readSpectrumFrame (frame); ++i) {}
    spectrumReceived = spectrumHop = spectrumWrite = 0;
    lastSpectrumSequence = 0;
    spectrumLevels.fill (-96.0f);
}

void CompressorSidechainPreview::transformSpectrum()
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

void CompressorSidechainPreview::updateSpectrum (bool frozen)
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
    OpenCompressorAudioProcessor::SpectrumFrame frame;
    int received = 0;
    for (int count = 0; count < OpenCompressorAudioProcessor::spectrumCapacity && processor.readSpectrumFrame (frame); ++count)
    {
        if (! displayActive || ! isVisible() || frozen || frame.epoch != spectrumEpoch || frame.captureState != processor.getHistoryCaptureState()) continue;
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

float CompressorSidechainPreview::spectrumLevelAtFrequency (double hz) const noexcept
{
    if (hz > spectrumRate * 0.5 || hz < 0) return -96.0f;
    const auto bin = juce::jlimit (0, fftSize / 2, juce::roundToInt (hz * fftSize / spectrumRate));
    return spectrumLevels[(size_t) bin];
}

juce::Rectangle<float> CompressorSidechainPreview::plotBounds() const
{
    return getLocalBounds().toFloat().reduced (7, 0).withTrimmedLeft (17).withTrimmedTop (18).withTrimmedBottom (15);
}

juce::Point<float> CompressorSidechainPreview::handlePosition (bool highPass) const
{
    const auto plot = plotBounds();
    const auto cutoffs=processor.sidechainCutoffs();const double hz=highPass?cutoffs.highPass:cutoffs.lowPass;
    return { plot.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * plot.getWidth(),
             highPass?plot.getY()+3.5f:plot.getBottom()-3.5f };
}

int CompressorSidechainPreview::hitHandle (juce::Point<float> point, bool preferLowPass) const
{
    const auto hp=handlePosition(true),lp=handlePosition(false);
    const float hpDistance=point.getDistanceFrom(hp),lpDistance=point.getDistanceFrom(lp);
    if (preferLowPass && lpDistance <= 10) return 1;
    if (hpDistance <= 10 && hpDistance <= lpDistance) return 0;
    if(lpDistance<=10)return 1;
    if(point.y<plotBounds().getY()-3||point.y>plotBounds().getBottom()+3)return -1;
    const float hpLine=std::abs(point.x-hp.x),lpLine=std::abs(point.x-lp.x);
    if(preferLowPass&&lpLine<=6)return 1;
    if(hpLine<=6&&hpLine<=lpLine)return 0;
    return lpLine<=6?1:-1;
}

void CompressorSidechainPreview::mouseDown (const juce::MouseEvent& event)
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

void CompressorSidechainPreview::mouseDrag (const juce::MouseEvent& event)
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
    const auto current=processor.sidechainCutoffs();
    const auto constrained=OpenCompressorAudioProcessor::constrainSidechainCutoffs(draggingHighPass?(float)(20*std::pow(1000.0,position)):current.highPass,draggingHighPass?current.lowPass:(float)(20*std::pow(1000.0,position)),draggingHighPass);
    const float hz=range.snapToLegalValue(draggingHighPass?constrained.highPass:constrained.lowPass);
    dragged->setValueNotifyingHost (dragged->convertTo0to1 (hz));
    repaint();
}

void CompressorSidechainPreview::endDrag()
{
    if (dragged == nullptr) return;
    auto* parameter = dragged; dragged = nullptr;
    parameter->endChangeGesture();
    processor.parameters.copyState(); processor.undoManager.beginNewTransaction();
    hovered = -1; setMouseCursor (juce::MouseCursor::NormalCursor); repaint();
}

void CompressorSidechainPreview::mouseMove (const juce::MouseEvent& event)
{
    if (dragged != nullptr) return;
    hovered = hitHandle (event.position, event.mods.isAltDown());
    setMouseCursor (hovered >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void CompressorSidechainPreview::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (T::panel.withAlpha (0.45f));
    g.fillRoundedRectangle (bounds, 4);
    const auto plot = plotBounds();
    if (plot.getWidth() <= 0 || plot.getHeight() <= 0) return;
    const auto xFor = [&] (double hz) { return plot.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * plot.getWidth(); };
    const auto yFor = [&] (double db) { return plot.getY() - (float) juce::jmin (0.0, db) * plot.getHeight() / 96; };
    const auto cutoffs=processor.sidechainCutoffs();const double hp=cutoffs.highPass,lp=cutoffs.lowPass;
    // Match Gate's soft selected-range backdrop, in Compressor's palette.
    // This is neither an EQ curve nor measured energy; draw it beneath both.
    g.setGradientFill ({ T::amber.withAlpha (0.18f), plot.getTopLeft(),
                         T::lime.withAlpha (0.02f), plot.getBottomLeft(), false });
    g.fillRect (juce::Rectangle<float> (xFor (hp), plot.getY(), xFor (lp) - xFor (hp), plot.getHeight()));
    g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
    g.setColour (T::dim);
    g.drawText ("SC SPECTRUM", 7, 2, getWidth() - 55, 12, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (7.0f));
    g.drawText ("24 dB/oct", getWidth() - 48, 2, 41, 12, juce::Justification::centredRight);
    for (int db : { 0, -24, -48, -72, -96 })
    {
        g.setColour (T::dim.withAlpha (db == 0 ? 0.25f : 0.10f));
        if (db > -96) g.drawHorizontalLine (juce::roundToInt (yFor (db)), plot.getX(), plot.getRight());
        if (db == 0 || db == -48 || db == -96)
        {
            g.setColour (T::dim.withAlpha (0.75f));
            g.drawText (juce::String ((int) db), 0, juce::roundToInt (yFor (db)) - 5, 20, 10, juce::Justification::centredRight);
        }
    }
    for (double hz : { 20.0, 100.0, 1000.0, 10000.0, 20000.0 })
    {
        const float x = xFor (hz);
        g.setColour (T::dim.withAlpha (0.10f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        // Ease the crowded 10k caption out as the RMS spectrum narrows, rather
        // than popping it off at a width threshold. Grid positions stay exact.
        const float labelAlpha=hz==10000.0?juce::jlimit(0.f,1.f,(plot.getWidth()-245.f)/30.f):1.f;
        if(labelAlpha<=0)continue;
        g.setColour (T::dim.withAlpha (0.85f*labelAlpha));
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
        g.setGradientFill ({ T::amber.withAlpha (0.16f), plot.getTopLeft(),
                             T::amber.withAlpha (0.025f), plot.getBottomLeft(), false });
        g.fillPath (spectrumFill);
        // Only the contour is stroked, not the fill's bottom/side closing edges.
        g.setColour (T::amber.withAlpha (0.48f));
        g.strokePath (trace, juce::PathStrokeType (0.8f));
    }
    // Only frequency markers overlay the measured spectrum. No filter-response
    // curve/fill: the spectrum itself already includes the audible filtering.
    for (bool highPass : { true, false })
    {
        const bool off = highPass ? hp <= 20.5 : lp >= 19999;
        const auto point = handlePosition (highPass);
        const float x = point.x, y = point.y;
        const auto colour = highPass ? T::amber : T::lime;
        const bool focused = hovered == (highPass ? 0 : 1) || (dragged != nullptr && draggingHighPass == highPass);
        g.setColour(colour.withAlpha(focused?.95f:off?.28f:.72f));g.drawLine(x,plot.getY(),x,plot.getBottom(),focused?1.6f:1.1f);
        if (focused) { g.setColour (colour.withAlpha (0.18f)); g.fillEllipse (x - 6, y - 6, 12, 12); }
        g.setColour (T::panel); g.fillEllipse (x - 3.5f, y - 3.5f, 7, 7);
        g.setColour (colour.withAlpha (off && ! focused ? 0.55f : 1.0f));
        g.drawEllipse (x - 3, y - 3, 6, 6, focused ? 1.7f : 1.3f);
    }
}
