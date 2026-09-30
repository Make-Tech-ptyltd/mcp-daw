#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "AppContext.h"
#include "Theme.h"

namespace mcpdaw::ui
{
/** A numeric readout you drag vertically to change (Shift = fine) or double-click to type. */
class DragNumber final : public juce::Component, private juce::TextEditor::Listener
{
public:
    DragNumber (double min, double max, double step, int decimals);
    void setValue (double v);
    double getValue() const noexcept { return value; }
    std::function<void (double value)> onDragStart, onChange, onCommit;
    juce::String label;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    double value = 0, min, max, step, dragStartValue = 0;
    int decimals;
    bool dragging = false;
    std::unique_ptr<juce::TextEditor> editor;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;
    void closeEditor (bool commit);
};

/** "by MAKE TECH" under the logo: dim, teal + underlined with a hand cursor on hover, opens the website. */
class BrandByline final : public juce::Component, public juce::SettableTooltipClient
{
public:
    BrandByline();
    static constexpr const char* kText = "by MAKE TECH";
    /** Width the text needs at the byline font (for tight hit-testing). */
    static int preferredWidth();

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool hover = false;
    static juce::Font font();
};

class TransportBar final : public juce::Component, private juce::Timer, private ProjectController::Listener, private juce::ChangeListener
{
public:
    explicit TransportBar (AppContext&);
    ~TransportBar() override;

    std::function<void()> onAudioSettings;
    enum class McpState { Off, Ready, Connected };
    /** `who`: the connected client (Connected) or the most recent one (Ready), e.g. "Claude". */
    void setMcpStatus (McpState, const juce::String& who);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& app;
    IconButton rewind { "Return to start", Icon::Rewind, "Return to start (Home)" };
    IconButton play { "Play", Icon::Play, "Play / stop (Space)" };
    IconButton stop { "Stop", Icon::Stop, "Stop and return to start point" };
    IconButton loop { "Loop", Icon::Loop, "Loop playback (L)" };
    IconButton undo { "Undo", Icon::Undo, "Undo (Ctrl+Z)" };
    IconButton redo { "Redo", Icon::Redo, "Redo (Ctrl+Y)" };
    DragNumber bpm { kMinBpm, kMaxBpm, 0.01, 2 };
    juce::ComboBox snap;
    juce::TextButton audio { "Audio" };
    BrandByline byline;
    static constexpr int kBylineHeight = 12;
    juce::Rectangle<int> brandArea, positionArea, statusArea;
    McpState mcpState = McpState::Off;
    juce::String mcpWho;
    double shownBeat = -1;
    bool shownPlaying = false;

    void timerCallback() override;
    void projectChanged (const Project&, bool) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void syncFromProject();
};
} // namespace mcpdaw::ui
