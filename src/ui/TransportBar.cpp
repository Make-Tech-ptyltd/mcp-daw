// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "TransportBar.h"
#include "../app/AppLinks.h"

namespace mcpdaw::ui
{
// ================================================================================================
// BrandByline
// ================================================================================================
BrandByline::BrandByline()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (juce::String (links::website).fromFirstOccurrenceOf ("//", false, false));
    setWantsKeyboardFocus (false);
}

juce::Font BrandByline::font() { return fonts::display (9.5f); }

int BrandByline::preferredWidth()
{
    return juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (font(), kText))) + 4;
}

void BrandByline::paint (juce::Graphics& g)
{
    g.setFont (font());
    g.setColour (hover ? colours::accent : colours::textFaint);
    auto r = getLocalBounds();
    g.drawText (kText, r, juce::Justification::centred, false);
    if (hover)   // underline under the text only
    {
        const float w = juce::GlyphArrangement::getStringWidth (font(), kText);
        const float x = (static_cast<float> (getWidth()) - w) * 0.5f;
        g.fillRect (x, static_cast<float> (getHeight()) - 1.5f, w, 1.0f);
    }
}

void BrandByline::mouseUp (const juce::MouseEvent& e)
{
    if (hover && ! e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition()))
        juce::URL (links::website).launchInDefaultBrowser();
}

// ================================================================================================
// DragNumber
// ================================================================================================
DragNumber::DragNumber (double mn, double mx, double st, int dec) : min (mn), max (mx), step (st), decimals (dec)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setWantsKeyboardFocus (false);
}

void DragNumber::setValue (double v)
{
    v = juce::jlimit (min, max, v);
    if (std::abs (v - value) > 1e-9) { value = v; repaint(); }
}

void DragNumber::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::window);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (dragging ? colours::accent : colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    auto area = getLocalBounds().reduced (8, 2);
    if (label.isNotEmpty())
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (11.0f, true));
        g.drawText (label, area.removeFromLeft (30), juce::Justification::centredLeft);
    }
    if (editor == nullptr)
    {
        g.setColour (colours::text);
        g.setFont (fonts::display (17.0f));
        g.drawText (juce::String (value, decimals), area, juce::Justification::centredRight);
    }
}

void DragNumber::mouseDown (const juce::MouseEvent&)
{
    dragStartValue = value;
    dragging = true;
    if (onDragStart) onDragStart (value);
    repaint();
}

void DragNumber::mouseDrag (const juce::MouseEvent& e)
{
    // 1 BPM per 4 px; Shift for 0.01 steps.
    const double perPixel = e.mods.isShiftDown() ? step : 0.25;
    double v = dragStartValue - e.getDistanceFromDragStartY() * perPixel;
    v = std::round (v / (e.mods.isShiftDown() ? step : 1.0)) * (e.mods.isShiftDown() ? step : 1.0);
    v = juce::jlimit (min, max, v);
    if (std::abs (v - value) > 1e-9)
    {
        value = v;
        repaint();
        if (onChange) onChange (value);
    }
}

void DragNumber::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
    if (onCommit) onCommit (value);
}

void DragNumber::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const double delta = (w.deltaY > 0 ? 1.0 : -1.0) * (e.mods.isShiftDown() ? step * 10 : 1.0);
    if (onDragStart) onDragStart (value);
    setValue (value + delta);
    if (onChange) onChange (value);
    if (onCommit) onCommit (value);
}

void DragNumber::mouseDoubleClick (const juce::MouseEvent&)
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (fonts::display (16.0f));
    editor->setJustification (juce::Justification::centredRight);
    editor->setInputRestrictions (8, "0123456789.");
    editor->setText (juce::String (value, decimals), false);
    editor->addListener (this);
    addAndMakeVisible (*editor);
    editor->setBounds (getLocalBounds().reduced (label.isNotEmpty() ? 36 : 2, 2).withTrimmedLeft (label.isNotEmpty() ? 0 : 0));
    editor->selectAll();
    editor->grabKeyboardFocus();
    repaint();
}

void DragNumber::closeEditor (bool commit)
{
    if (editor == nullptr) return;
    const auto text = editor->getText();
    editor->removeListener (this);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DragNumber> (this)] { if (safe != nullptr) { safe->editor.reset(); safe->repaint(); } });
    if (commit && text.containsOnly ("0123456789.") && text.isNotEmpty())
    {
        if (onDragStart) onDragStart (value);
        setValue (text.getDoubleValue());
        if (onChange) onChange (value);
        if (onCommit) onCommit (value);
    }
}

void DragNumber::textEditorReturnKeyPressed (juce::TextEditor&) { closeEditor (true); }
void DragNumber::textEditorEscapeKeyPressed (juce::TextEditor&) { closeEditor (false); }
void DragNumber::textEditorFocusLost (juce::TextEditor&) { closeEditor (true); }

// ================================================================================================
// TransportBar
// ================================================================================================
TransportBar::TransportBar (AppContext& a) : app (a)
{
    addAndMakeVisible (byline);
    for (auto* b : { &rewind, &play, &stop, &loop, &undo, &redo })
    {
        addAndMakeVisible (*b);
        b->setWantsKeyboardFocus (false);
    }
    addAndMakeVisible (bpm);
    addAndMakeVisible (snap);
    addAndMakeVisible (audio);
    audio.setWantsKeyboardFocus (false);
    audio.setTooltip ("Audio device settings (driver, device, buffer size)");
    bpm.label = "BPM";

    rewind.onClick = [this] { app.run ("seek", makeObject ({ { "beat", 0 } })); };
    play.onClick = [this] { app.run (app.engine.isPlaying() ? "stop" : "play"); };
    stop.onClick = [this] { app.run ("stop"); };
    loop.onClick = [this] { app.run ("set_loop_region", makeObject ({ { "enabled", ! app.project().loopEnabled } })); };
    undo.onClick = [this] { app.run ("undo"); };
    redo.onClick = [this] { app.run ("redo"); };
    audio.onClick = [this] { if (onAudioSettings) onAudioSettings(); };

    bpm.onDragStart = [this] (double) { app.beginGesture ("Set tempo"); };
    bpm.onChange = [this] (double v) { app.run ("set_project_tempo", makeObject ({ { "bpm", v } })); };
    bpm.onCommit = [this] (double) { app.endGesture(); };

    snap.addItemList ({ "Snap: off", "Snap: bar", "Snap: beat", "Snap: 1/8", "Snap: 1/16", "Snap: 1/32" }, 1);
    snap.setTooltip ("Grid snapping for moves, trims and painting (hold Alt to bypass)");
    snap.setWantsKeyboardFocus (false);
    snap.onChange = [this] { app.setSnap (static_cast<SnapGrid> (snap.getSelectedId() - 1)); };

    app.controller.addListener (this);
    app.preferencesChanged.addChangeListener (this);
    syncFromProject();
    startTimerHz (30);
}

TransportBar::~TransportBar()
{
    app.controller.removeListener (this);
    app.preferencesChanged.removeChangeListener (this);
}

void TransportBar::setMcpStatus (McpState state, const juce::String& who)
{
    mcpState = state;
    mcpWho = who;
    repaint (statusArea);
}

void TransportBar::projectChanged (const Project&, bool) { syncFromProject(); }
void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*) { syncFromProject(); }

void TransportBar::syncFromProject()
{
    const auto& p = app.project();
    bpm.setValue (p.bpm);
    loop.setToggleState (p.loopEnabled, juce::dontSendNotification);
    undo.setEnabled (app.controller.canUndo());
    redo.setEnabled (app.controller.canRedo());
    undo.setTooltip (app.controller.canUndo() ? "Undo " + app.controller.undoLabel() + " (Ctrl+Z)" : "Nothing to undo");
    redo.setTooltip (app.controller.canRedo() ? "Redo " + app.controller.redoLabel() + " (Ctrl+Y)" : "Nothing to redo");
    snap.setSelectedId (static_cast<int> (app.snap) + 1, juce::dontSendNotification);
}

void TransportBar::timerCallback()
{
    const double beat = app.engine.positionBeats();
    const bool playing = app.engine.isPlaying();
    if (playing != shownPlaying)
    {
        shownPlaying = playing;
        play.setIcon (playing ? Icon::Pause : Icon::Play);
        play.setToggleState (playing, juce::dontSendNotification);
    }
    if (std::abs (beat - shownBeat) > 1e-6)
    {
        shownBeat = beat;
        repaint (positionArea);
    }
    repaint (statusArea);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (colours::window);
    g.setColour (colours::lineSoft);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

    // Brand: "MCP·DAW" in Audiowide; the "by MAKE TECH" link (BrandByline) sits underneath.
    auto logo = brandArea.withTrimmedBottom (kBylineHeight);
    g.setFont (fonts::display (20.0f));
    g.setColour (colours::text);
    g.drawText ("MCP", logo.withTrimmedRight (logo.getWidth() / 2 + 5), juce::Justification::centredRight);
    g.setColour (colours::accent);
    g.drawText ("DAW", logo.withTrimmedLeft (logo.getWidth() / 2 + 5), juce::Justification::centredLeft);
    g.fillEllipse (static_cast<float> (logo.getCentreX()) - 2.0f, static_cast<float> (logo.getCentreY()) - 2.0f, 4.0f, 4.0f);

    // Position display: bars.beats.ticks (primary) and clock time (secondary)
    auto pos = positionArea.toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (pos, 5.0f);
    g.setColour (colours::lineSoft);
    g.drawRoundedRectangle (pos.reduced (0.5f), 5.0f, 1.0f);
    const auto bbt = toBarBeatTick (beatsToTicks (std::max (0.0, shownBeat)));
    const auto bbtText = juce::String (bbt.bar).paddedLeft (' ', 3) + "." + juce::String (bbt.beat) + "." + juce::String (bbt.tick).paddedLeft ('0', 3);
    const double secs = beatsToSeconds (std::max (0.0, shownBeat), app.project().bpm);
    const int m = static_cast<int> (secs / 60.0);
    const auto clock = juce::String (m) + ":" + juce::String (secs - m * 60.0, 3).paddedLeft ('0', 6);
    auto inner = positionArea.reduced (10, 2);
    g.setColour (shownPlaying ? colours::accent : colours::text);
    g.setFont (fonts::display (22.0f));
    g.drawText (bbtText, inner.removeFromLeft (inner.getWidth() * 3 / 5), juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (fonts::mono (14.0f));
    g.drawText (clock, inner, juce::Justification::centredRight);

    // Status: MCP + CPU
    auto st = statusArea;
    const auto dot = st.removeFromLeft (14).withSizeKeepingCentre (8, 8).toFloat();
    const bool on = mcpState == McpState::Connected;
    g.setColour (on ? colours::accent : mcpState == McpState::Ready ? colours::textFaint : colours::red);
    g.fillEllipse (dot);
    juce::String text;
    switch (mcpState)
    {
        case McpState::Connected: text = "MCP: " + mcpWho; break;
        case McpState::Ready:     text = mcpWho.isEmpty() ? juce::String ("MCP: ready")
                                                          : "MCP: ready " + juce::String::charToString (0x00B7) + " last " + mcpWho; break;
        case McpState::Off:       text = "MCP: off"; break;
    }
    g.setColour (on ? colours::text : colours::textDim.withAlpha (mcpState == McpState::Ready ? 0.8f : 1.0f));
    g.setFont (fonts::ui (12.0f, true));
    g.drawText (text, st.removeFromTop (st.getHeight() / 2).withTrimmedLeft (4), juce::Justification::centredLeft, true);
    g.setColour (colours::textFaint);
    g.setFont (fonts::ui (12.0f));
    const double cpu = app.engine.cpuLoad() * 100.0;
    g.drawText ("DSP " + juce::String (cpu, 1) + " %   " + juce::String (app.engine.deviceSampleRate() / 1000.0, 1) + " kHz",
                st.withTrimmedLeft (4), juce::Justification::centredLeft);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    brandArea = r.removeFromLeft (132);
    {
        // Byline directly under the logo, only as wide as its text so the hover target is honest.
        const auto line = brandArea.withTop (brandArea.getBottom() - kBylineHeight);
        byline.setBounds (line.withSizeKeepingCentre (std::min (line.getWidth(), BrandByline::preferredWidth()), kBylineHeight));
    }
    r.removeFromLeft (8);
    const int b = r.getHeight();
    rewind.setBounds (r.removeFromLeft (b));
    play.setBounds (r.removeFromLeft (b + 6));
    stop.setBounds (r.removeFromLeft (b));
    loop.setBounds (r.removeFromLeft (b));
    r.removeFromLeft (12);
    positionArea = r.removeFromLeft (260);
    r.removeFromLeft (12);
    bpm.setBounds (r.removeFromLeft (128));
    r.removeFromLeft (10);
    snap.setBounds (r.removeFromLeft (118).reduced (0, 3));
    r.removeFromLeft (10);
    undo.setBounds (r.removeFromLeft (b));
    redo.setBounds (r.removeFromLeft (b));
    audio.setBounds (r.removeFromRight (64).reduced (0, 3));
    r.removeFromRight (10);
    statusArea = r.removeFromRight (std::min (190, std::max (0, r.getWidth())));
}
} // namespace mcpdaw::ui
