#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Status-bar ticker of incoming MCP commands (tool names only), e.g.
//     Claude > create_event x12 . set_event_gain . play
// Newest on the right; older entries slide left and fade out towards the left edge. It shows while
// commands stream and fades away after ~5 s of quiet, when the shortcut hints come back.
#include "Theme.h"
#include "mcp/CommandFeed.h"

namespace mcpdaw::ui
{
class CommandTicker final : public juce::Component, private juce::Timer
{
public:
    CommandTicker();

    /** Message thread. Cheap: painting is throttled to the ticker's own frame rate. */
    void push (const juce::String& client, const juce::String& tool, const juce::var& arguments);
    /** True while there is something to show (the status bar hides its hints meanwhile). */
    bool isActive() const noexcept { return active; }
    std::function<void()> onActiveChanged;

    void paint (juce::Graphics&) override;

    static constexpr juce::uint32 kQuietMs = 5000;   // hints return after this much silence
    static constexpr juce::uint32 kFadeMs = 600;

private:
    mcp::CommandFeed feed;
    float slide = 0.0f;           // px the row is still shifted right; eases to 0 as a new entry slides in
    bool active = false, dirty = false;

    void timerCallback() override;
    void setActive (bool);
    static juce::Font font();
    static juce::Colour clientColour (const juce::String& client);
    static juce::String prefixFor (const juce::String& client);
    static juce::String separator();
};
} // namespace mcpdaw::ui
