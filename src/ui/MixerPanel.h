#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "AppContext.h"
#include "Theme.h"

namespace mcpdaw::ui
{
/** One mixer channel strip (track or master). Continuous controls commit as one undo step per gesture. */
class ChannelStrip final : public juce::Component
{
public:
    ChannelStrip (AppContext&, juce::String trackId);   // trackId empty = master
    void refresh();                                     // pull values from the model (skips controls being dragged)
    void updateMeter();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    const juce::String trackId;

private:
    AppContext& app;
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton mute { "M" }, solo { "S" }, fx { "FX" };
    LevelMeter meter;
    bool isMaster() const { return trackId.isEmpty(); }
    const Track* track() const { return app.project().findTrack (trackId); }
};

class MixerPanel final : public juce::Component, private ProjectController::Listener, private juce::ChangeListener, private juce::Timer
{
public:
    explicit MixerPanel (AppContext&);
    ~MixerPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    AppContext& app;
    juce::Viewport viewport;
    juce::Component stripHolder;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    std::unique_ptr<ChannelStrip> master;
    juce::StringArray shownIds;

    void rebuildIfNeeded();
    void projectChanged (const Project&, bool) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { for (auto& s : strips) s->repaint(); }
    void timerCallback() override;
};
} // namespace mcpdaw::ui
