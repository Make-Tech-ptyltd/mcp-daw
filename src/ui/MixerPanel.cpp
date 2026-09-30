// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "MixerPanel.h"

namespace mcpdaw::ui
{
namespace
{
constexpr int kStripWidth = 88;

void configureFader (juce::Slider& s)
{
    // Fader law: linear in dB between -60 and +12 with -inf at the bottom; skewed so 0 dB sits high.
    s.setRange (kMinDb, kMaxDb, 0.1);
    s.setSkewFactorFromMidPoint (-12.0);
    s.setDoubleClickReturnValue (true, 0.0);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
    s.textFromValueFunction = [] (double v) { return v <= kMinDb ? juce::String ("-inf") : (v > 0 ? "+" : "") + juce::String (v, 1); };
    s.valueFromTextFunction = [] (const juce::String& t) { return t.trim().startsWithIgnoreCase ("-inf") ? kMinDb : t.getDoubleValue(); };
    s.setColour (juce::Slider::textBoxTextColourId, colours::textDim);
}
} // namespace

// ================================================================================================
// ChannelStrip
// ================================================================================================
ChannelStrip::ChannelStrip (AppContext& a, juce::String id) : trackId (std::move (id)), app (a)
{
    configureFader (fader);
    addAndMakeVisible (fader);
    addAndMakeVisible (meter);

    fader.onDragStart = [this] { app.beginGesture (isMaster() ? "Master gain" : "Track gain"); };
    fader.onDragEnd = [this] { app.endGesture(); };
    fader.onValueChange = [this]
    {
        if (isMaster()) app.run ("set_master_gain", makeObject ({ { "db", fader.getValue() } }));
        else app.run ("set_track_gain", makeObject ({ { "trackId", trackId }, { "db", fader.getValue() } }));
    };

    if (isMaster())
    {
        meter.onClick = [this] { app.engine.resetClip(); meter.setClipped (false); };
        meter.setTooltip ("Master peak meter - click to reset the clip indicator");
        return;
    }

    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setTooltip ("Pan (double-click = centre)");
    pan.onDragStart = [this] { app.beginGesture ("Track pan"); };
    pan.onDragEnd = [this] { app.endGesture(); };
    pan.onValueChange = [this] { app.run ("set_track_pan", makeObject ({ { "trackId", trackId }, { "pan", pan.getValue() } })); };
    addAndMakeVisible (pan);

    for (auto* b : { &mute, &solo, &fx })
    {
        addAndMakeVisible (*b);
        b->setWantsKeyboardFocus (false);
    }
    mute.setColour (juce::TextButton::buttonOnColourId, colours::amber);
    solo.setColour (juce::TextButton::buttonOnColourId, colours::accent);
    mute.setColour (juce::TextButton::textColourOnId, colours::window);
    solo.setColour (juce::TextButton::textColourOnId, colours::window);
    mute.onClick = [this] { if (auto* t = track()) app.run ("mute_track", makeObject ({ { "trackId", trackId }, { "enabled", ! t->mute } })); };
    solo.onClick = [this] { if (auto* t = track()) app.run ("solo_track", makeObject ({ { "trackId", trackId }, { "enabled", ! t->solo } })); };
    fx.onClick = [this] { app.fxTarget = {}; app.selectTrack (trackId); app.selectionChanged.sendChangeMessage(); };
    fx.setTooltip ("Show this track's effects in the FX rack");
}

void ChannelStrip::refresh()
{
    if (isMaster())
    {
        if (! fader.isMouseButtonDown()) fader.setValue (app.project().master.gainDb, juce::dontSendNotification);
        repaint();
        return;
    }
    const auto* t = track();
    if (t == nullptr) return;
    if (! fader.isMouseButtonDown()) fader.setValue (t->gainDb, juce::dontSendNotification);
    if (! pan.isMouseButtonDown()) pan.setValue (t->pan, juce::dontSendNotification);
    mute.setToggleState (t->mute, juce::dontSendNotification);
    solo.setToggleState (t->solo, juce::dontSendNotification);
    fx.setToggleState (! t->effects.empty(), juce::dontSendNotification);
    pan.setColour (juce::Slider::rotarySliderFillColourId, colours::track (t->colour));
    repaint();
}

void ChannelStrip::updateMeter()
{
    if (isMaster())
    {
        meter.setLevels (app.masterMeter.left, app.masterMeter.right);
        meter.setClipped (app.engine.clipped());
    }
    else
    {
        const auto m = app.meterFor (trackId);
        meter.setLevels (m.left, m.right);
    }
}

void ChannelStrip::mouseDown (const juce::MouseEvent&)
{
    if (! isMaster()) app.selectTrack (trackId);
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (2, 0);
    const bool selected = ! isMaster() && trackId == app.selectedTrack;
    g.setColour (isMaster() ? colours::raised.brighter (0.03f) : selected ? colours::controlHi.withAlpha (0.5f) : colours::raised);
    g.fillRoundedRectangle (r.toFloat(), 4.0f);
    const auto colour = isMaster() ? colours::accent : (track() ? colours::track (track()->colour) : colours::textFaint);
    g.setColour (colour);
    g.fillRect (r.removeFromTop (3));
    g.setColour (colours::text);
    g.setFont (isMaster() ? fonts::display (12.0f) : fonts::ui (12.0f, true));
    const auto name = isMaster() ? juce::String ("MASTER") : (track() ? track()->name : juce::String());
    g.drawFittedText (name, r.removeFromTop (22).reduced (4, 0), juce::Justification::centred, 1);
    if (! isMaster())
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (10.5f));
        const double p = pan.getValue();
        g.drawText (std::abs (p) < 0.005 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (p) * 100)),
                    getLocalBounds().withTop (pan.getBottom() - 2).withHeight (12), juce::Justification::centred);
    }
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    r.removeFromTop (24);
    if (! isMaster())
    {
        pan.setBounds (r.removeFromTop (32).withSizeKeepingCentre (32, 32));
        r.removeFromTop (10);
        auto buttons = r.removeFromBottom (22);
        const int bw = buttons.getWidth() / 3;
        mute.setBounds (buttons.removeFromLeft (bw).reduced (1, 0));
        solo.setBounds (buttons.removeFromLeft (bw).reduced (1, 0));
        fx.setBounds (buttons.reduced (1, 0));
        r.removeFromBottom (4);
    }
    else r.removeFromTop (54);
    meter.setBounds (r.removeFromRight (14).withTrimmedBottom (20));
    r.removeFromRight (2);
    fader.setBounds (r);
}

// ================================================================================================
// MixerPanel
// ================================================================================================
MixerPanel::MixerPanel (AppContext& a) : app (a)
{
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    master = std::make_unique<ChannelStrip> (app, juce::String());
    addAndMakeVisible (*master);
    app.controller.addListener (this);
    app.selectionChanged.addChangeListener (this);
    rebuildIfNeeded();
    startTimerHz (30);
}

MixerPanel::~MixerPanel()
{
    app.controller.removeListener (this);
    app.selectionChanged.removeChangeListener (this);
}

void MixerPanel::rebuildIfNeeded()
{
    juce::StringArray ids;
    for (const auto& t : app.project().tracks) ids.add (t.id);
    if (ids != shownIds)
    {
        // Structure changed: rebuild strips (reusing none keeps this trivially correct; strips are cheap).
        strips.clear();
        for (const auto& id : ids)
        {
            strips.push_back (std::make_unique<ChannelStrip> (app, id));
            stripHolder.addAndMakeVisible (*strips.back());
        }
        shownIds = ids;
        resized();
        repaint();   // the "No tracks yet" hint lives in this component's own paint()
    }
    for (auto& s : strips) s->refresh();
    master->refresh();
}

void MixerPanel::projectChanged (const Project&, bool) { rebuildIfNeeded(); }

void MixerPanel::timerCallback()
{
    for (auto& s : strips) s->updateMeter();
    master->updateMeter();
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    if (strips.empty())
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (13.0f));
        g.drawText ("No tracks yet", viewport.getBounds(), juce::Justification::centred);
    }
}

void MixerPanel::resized()
{
    auto r = getLocalBounds().reduced (4);
    master->setBounds (r.removeFromRight (kStripWidth + 10));
    r.removeFromRight (6);
    viewport.setBounds (r);
    const int h = std::max (0, r.getHeight() - (viewport.isHorizontalScrollBarShown() ? viewport.getScrollBarThickness() : 0));
    stripHolder.setSize (std::max (r.getWidth(), static_cast<int> (strips.size()) * kStripWidth), h);
    for (size_t i = 0; i < strips.size(); ++i) strips[i]->setBounds (static_cast<int> (i) * kStripWidth, 0, kStripWidth, h);
}
} // namespace mcpdaw::ui
