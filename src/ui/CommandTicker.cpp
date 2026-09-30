// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "CommandTicker.h"

namespace mcpdaw::ui
{
namespace
{
float textWidth (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); }
} // namespace

CommandTicker::CommandTicker()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
}

juce::Font CommandTicker::font() { return fonts::ui (12.0f); }
juce::String CommandTicker::separator() { return juce::String ("  ") + juce::String::charToString (0x00B7) + "  "; }   // " . "
juce::String CommandTicker::prefixFor (const juce::String& client) { return client + " " + juce::String::charToString (0x25B8) + " "; }   // "Claude > "

juce::Colour CommandTicker::clientColour (const juce::String& client)
{
    if (client == "Claude") return colours::accent;
    if (client == "ChatGPT") return colours::amber;
    return colours::track (static_cast<int> (static_cast<juce::uint32> (client.hashCode()) % static_cast<juce::uint32> (colours::kNumTrackColours)));
}

void CommandTicker::push (const juce::String& client, const juce::String& tool, const juce::var& arguments)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (! active) feed.clear();   // a new burst starts from a clean line
    const bool appended = feed.push (client, tool, arguments, now);
    if (appended)
    {
        // Slide the row left by the width the new entry takes (it enters from the right edge).
        const auto& entries = feed.entries();
        const auto& e = entries.back();
        const bool newRun = entries.size() < 2 || entries[entries.size() - 2].client != e.client;
        float w = textWidth (font(), e.text()) + textWidth (font(), newRun ? prefixFor (e.client) : juce::String()) + textWidth (font(), separator());
        slide = juce::jmin (slide + w, static_cast<float> (getWidth()));
    }
    dirty = true;
    setActive (true);
    if (! isTimerRunning()) startTimerHz (60);
}

void CommandTicker::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive) return;
    active = shouldBeActive;
    if (! active) feed.clear();
    if (onActiveChanged) onActiveChanged();
    repaint();
}

void CommandTicker::timerCallback()
{
    const auto quietFor = juce::Time::getMillisecondCounter() - feed.lastTime();
    if (slide > 0.0f)
    {
        slide = slide < 0.5f ? 0.0f : slide * 0.8f;   // ease-out, ~200 ms
        dirty = true;
    }
    if (quietFor > kQuietMs) dirty = true;   // fading out
    if (quietFor > kQuietMs + kFadeMs)
    {
        setActive (false);
        stopTimer();
        return;
    }
    if (dirty)
    {
        dirty = false;
        repaint();   // at most 60 fps however fast commands arrive (a 512-step batch is one entry anyway)
    }
}

void CommandTicker::paint (juce::Graphics& g)
{
    if (! active || feed.entries().empty()) return;
    const auto f = font();
    g.setFont (f);
    const auto bounds = getLocalBounds().toFloat();
    const float fadeZone = bounds.getWidth() * 0.35f;   // entries dim as they approach the left edge
    const auto quietFor = juce::Time::getMillisecondCounter() - feed.lastTime();
    const float globalAlpha = quietFor <= kQuietMs ? 1.0f : juce::jlimit (0.0f, 1.0f, 1.0f - static_cast<float> (quietFor - kQuietMs) / kFadeMs);

    auto drawPiece = [&] (float& right, const juce::String& text, juce::Colour colour)
    {
        const float w = textWidth (f, text);
        const float x = right - w;
        const float positional = juce::jlimit (0.0f, 1.0f, (x + w * 0.5f - bounds.getX()) / fadeZone);
        if (x + w > bounds.getX())
        {
            g.setColour (colour.withMultipliedAlpha (globalAlpha * positional));
            g.drawText (text, juce::Rectangle<float> (x, bounds.getY(), w + 2.0f, bounds.getHeight()), juce::Justification::centredLeft, false);
        }
        right = x;
    };

    const auto& entries = feed.entries();
    float right = bounds.getRight() + slide;
    for (int i = static_cast<int> (entries.size()) - 1; i >= 0 && right > bounds.getX(); --i)
    {
        const auto& e = entries[static_cast<size_t> (i)];
        drawPiece (right, e.text(), colours::textDim);
        const bool runStart = i == 0 || entries[static_cast<size_t> (i - 1)].client != e.client;
        if (runStart) drawPiece (right, prefixFor (e.client), clientColour (e.client));
        if (i > 0) drawPiece (right, separator(), colours::textFaint);
    }
}
} // namespace mcpdaw::ui
