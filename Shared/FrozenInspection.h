#pragma once
#include <JuceHeader.h>

// One layout for the frozen-history cursor and readout in the dynamics plugins.
// Widths use reserved values, so changing digits never makes the card wobble.
namespace FrozenInspection
{
struct Reading
{
    const char* label;
    juce::String value;
    const char* reservedValue = "-120.0 dB";
};

inline juce::String level(float gain, float floor)
{ return juce::String(juce::Decibels::gainToDecibels(gain, floor), 1) + " dB"; }

inline float textWidth(const juce::Font& font, const juce::String& text)
{ return juce::GlyphArrangement::getStringWidth(font, text); }

inline float readingWidth(const Reading& reading, const juce::Font& font)
{ return textWidth(font, juce::String(reading.label) + "  " + reading.reservedValue); }

inline juce::Rectangle<float> bounds(juce::Rectangle<float> plot, float cursorX,
                                    float controlsTop, std::initializer_list<Reading> primary,
                                    std::initializer_list<Reading> secondary)
{
    const juce::Font font(juce::FontOptions(9.5f));
    float primaryWidth = 20, headerWidth = 20 + textWidth(font, "8.00 s ago");
    for (const auto& reading : primary) primaryWidth += readingWidth(reading, font) + 16;
    for (const auto& reading : secondary) headerWidth += readingWidth(reading, font) + 16;
    const bool singleRow = secondary.size() == 0;
    if (singleRow) primaryWidth += textWidth(font, "8.00 s ago") + 16;
    const float height = singleRow ? 24.f : 42.f;
    const float left = plot.getX() + 22, right = plot.getRight() - 8;
    const float width = juce::jmin(right - left, juce::jmax(240.f, juce::jmax(primaryWidth, headerWidth)));
    const float top = juce::jlimit(plot.getY(), juce::jmax(plot.getY(), plot.getBottom() - height),
                                 controlsTop - height - 6.f);
    return { juce::jlimit(left, juce::jmax(left, right - width), cursorX - width * .5f), top, width, height };
}

inline void paint(juce::Graphics& g, juce::Rectangle<float> plot, float cursorX,
                  float controlsTop, double secondsAgo,
                  std::initializer_list<Reading> primary, std::initializer_list<Reading> secondary,
                  juce::Colour panel, juce::Colour text, juce::Colour dim)
{
    const auto card = bounds(plot, cursorX, controlsTop, primary, secondary);
    // Keep the cursor vertical, including when its card is clamped at an edge.
    g.setColour(text.withAlpha(.55f));
    g.drawLine(cursorX, plot.getY() + 36, cursorX, card.getY() - 6, .75f);
    g.setColour(panel); g.fillRoundedRectangle(card, 4.f);
    g.setColour(dim.withAlpha(.23f)); g.drawRoundedRectangle(card.reduced(.5f), 4.f, 1.f);
    const bool singleRow = secondary.size() == 0;
    if (!singleRow) g.drawHorizontalLine(juce::roundToInt(card.getY() + 21), card.getX() + 10, card.getRight() - 10);

    const juce::Font font(juce::FontOptions(9.5f));
    g.setFont(font);
    const auto time = juce::String(juce::jmax(0., secondsAgo), 2) + " s ago";
    auto drawRow = [&](juce::Rectangle<float> row, bool includeTime, std::initializer_list<Reading> readings)
    {
        const float timeWidth = includeTime ? textWidth(font, time) : 0.f;
        float width = timeWidth;
        int count = includeTime ? 1 : 0;
        for (const auto& reading : readings)
        { width += textWidth(font, juce::String(reading.label) + "  " + reading.value); ++count; }
        width += 16.f * (float)juce::jmax(0, count-1);
        row = row.withSizeKeepingCentre(width, row.getHeight());
        if (includeTime)
        {
            g.setColour(text); g.drawText(time, row.removeFromLeft(timeWidth), juce::Justification::centred);
            row.removeFromLeft(16);
        }
        for (const auto& reading : readings)
        {
            const float labelWidth = textWidth(font, juce::String(reading.label) + "  ");
            const float valueWidth = textWidth(font, reading.value);
            g.setColour(dim); g.drawText(reading.label, row.removeFromLeft(labelWidth), juce::Justification::centredLeft);
            g.setColour(text); g.drawText(reading.value, row.removeFromLeft(valueWidth), juce::Justification::centredLeft);
            row.removeFromLeft(16);
        }
    };
    if (singleRow)
    {
        drawRow(card.withSizeKeepingCentre(card.getWidth()-20, 16.f), true, primary);
        return;
    }
    drawRow(card.reduced(10, 0).withY(card.getY() + 4).withHeight(15), true, secondary);
    drawRow(card.reduced(10, 0).withY(card.getY() + 23).withHeight(15), false, primary);
}
}
