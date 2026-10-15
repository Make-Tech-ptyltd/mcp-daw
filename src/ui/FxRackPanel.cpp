// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "FxRackPanel.h"
#include "core/EffectPresets.h"
#include "dsp/Distortion.h"
#include "dsp/MultiTapDelay.h"
#include "dsp/ParametricEq.h"
#include "dsp/Tremolo.h"

namespace mcpdaw::ui
{
namespace
{
const EffectInstance* findFx (const AppContext& app, const juce::String& id) { return app.project().findEffect (id); }

float valueOf (const AppContext& app, const juce::String& effectId, int index)
{
    if (const auto* fx = findFx (app, effectId); fx != nullptr && index >= 0 && static_cast<size_t> (index) < fx->values.size())
        return fx->values[static_cast<size_t> (index)];
    return 0.0f;
}

void setParam (AppContext& app, const juce::String& effectId, const juce::String& paramId, double value)
{
    app.run ("set_effect_parameter", makeObject ({ { "effectId", effectId }, { "parameter", paramId }, { "value", value } }));
}

juce::Colour panColour (float pan)
{
    // left = blue-ish, centre = accent, right = amber: stereo placement readable at a glance.
    return pan < 0 ? colours::accent.interpolatedWith (colours::track (1), -pan) : colours::accent.interpolatedWith (colours::amber, pan);
}
} // namespace

// ================================================================================================
// Parameter controls bound to (effectId, param index)
// ================================================================================================
class ParamControl : public juce::Component
{
public:
    ParamControl (AppContext& a, juce::String fxId, int idx)
        : app (a), effectId (std::move (fxId)), index (idx),
          spec (findFx (a, effectId)->descriptor()->params[static_cast<size_t> (idx)]) {}
    virtual void refresh() = 0;
    const ParamSpec& paramSpec() const noexcept { return spec; }

protected:
    AppContext& app;
    const juce::String effectId;
    const int index;
    const ParamSpec spec;
};

class ParamKnob final : public ParamControl
{
public:
    ParamKnob (AppContext& a, juce::String fxId, int idx, juce::String labelOverride = {})
        : ParamControl (a, std::move (fxId), idx), label (labelOverride.isNotEmpty() ? labelOverride : spec.name)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (spec.min, spec.max, 0.0);
        if (spec.skewCentre > spec.min && spec.skewCentre < spec.max) slider.setSkewFactorFromMidPoint (spec.skewCentre);
        slider.setDoubleClickReturnValue (true, spec.def);
        slider.setScrollWheelEnabled (false);   // wheel scrolls the rack; it must never nudge a parameter by accident
        slider.setTooltip (spec.help.isNotEmpty() ? spec.name + ": " + spec.help : spec.name);
        slider.onDragStart = [this] { app.beginGesture (spec.name); };
        slider.onDragEnd = [this] { app.endGesture(); };
        slider.onValueChange = [this] { setParam (app, effectId, spec.id, slider.getValue()); repaint(); };
        slider.setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
        addAndMakeVisible (slider);
        refresh();
    }

    void refresh() override
    {
        if (! slider.isMouseButtonDown()) slider.setValue (valueOf (app, effectId, index), juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (11.0f, true));
        g.drawText (label, r.removeFromTop (14), juce::Justification::centred);
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (11.0f));
        g.drawText (spec.format (static_cast<float> (slider.getValue())), r.removeFromBottom (14), juce::Justification::centred);
    }

    void resized() override { slider.setBounds (getLocalBounds().withTrimmedTop (14).withTrimmedBottom (14).reduced (2, 0)); }

private:
    juce::String label;
    juce::Slider slider;
};

class ParamToggle final : public ParamControl
{
public:
    ParamToggle (AppContext& a, juce::String fxId, int idx, juce::String text, juce::Colour onColour = colours::accentDim)
        : ParamControl (a, std::move (fxId), idx)
    {
        button.setButtonText (text);
        button.setClickingTogglesState (false);
        button.setWantsKeyboardFocus (false);
        button.setColour (juce::TextButton::buttonOnColourId, onColour);
        button.setTooltip (spec.name + (spec.help.isNotEmpty() ? ": " + spec.help : juce::String()));
        button.onClick = [this] { setParam (app, effectId, spec.id, button.getToggleState() ? 0.0 : 1.0); };
        addAndMakeVisible (button);
        refresh();
    }
    void refresh() override { button.setToggleState (valueOf (app, effectId, index) >= 0.5f, juce::dontSendNotification); }
    void resized() override { button.setBounds (getLocalBounds()); }

private:
    juce::TextButton button;
};

class ParamChoice final : public ParamControl
{
public:
    ParamChoice (AppContext& a, juce::String fxId, int idx) : ParamControl (a, std::move (fxId), idx)
    {
        // EQ band types get compact labels (the model/MCP keep the full names).
        if (spec.id.endsWith (".type") && spec.choices.size() == 6) box.addItemList ({ "HP", "Lo shelf", "Bell", "Hi shelf", "LP", "Notch" }, 1);
        else box.addItemList (spec.choices, 1);
        box.setWantsKeyboardFocus (false);
        box.setTooltip (spec.name);
        box.onChange = [this] { setParam (app, effectId, spec.id, box.getSelectedId() - 1); };
        addAndMakeVisible (box);
        refresh();
    }
    void refresh() override { box.setSelectedId (juce::roundToInt (valueOf (app, effectId, index)) + 1, juce::dontSendNotification); }
    void resized() override { box.setBounds (getLocalBounds()); }

private:
    juce::ComboBox box;
};

// ================================================================================================
// EQ response display with draggable band nodes
// ================================================================================================
class EqGraph final : public juce::Component
{
public:
    EqGraph (AppContext& a, juce::String id) : app (a), effectId (std::move (id)) {}

    void refresh() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto* fx = findFx (app, effectId);
        if (fx == nullptr) return;
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::window);
        g.fillRoundedRectangle (r, 4.0f);

        // Grid: decades and +-6 dB lines.
        g.setFont (fonts::ui (10.0f));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xForFreq (f);
            g.setColour (colours::lineSoft);
            g.drawVerticalLine (static_cast<int> (x), r.getY(), r.getBottom());
            g.setColour (colours::textFaint);
            g.drawText (f >= 1000 ? juce::String (f / 1000.0, 0) + "k" : juce::String (f, 0), juce::Rectangle<float> (x + 2, r.getBottom() - 13, 30, 12),
                        juce::Justification::centredLeft);
        }
        for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
        {
            g.setColour (db == 0.0 ? colours::line : colours::lineSoft);
            g.drawHorizontalLine (static_cast<int> (yForDb (db)), r.getX(), r.getRight());
        }

        // Individual bands (faint) then the combined curve.
        const double sr = 48000.0;
        for (int b = 0; b < eq::kNumBands; ++b)
        {
            if (fx->values[static_cast<size_t> (b * 5)] < 0.5f) continue;
            juce::Path band;
            for (int x = 0; x < getWidth(); x += 2)
            {
                const float y = yForDb (ParametricEq::bandResponseDb (fx->values, b, freqForX (static_cast<float> (x)), sr));
                if (x == 0) band.startNewSubPath (static_cast<float> (x), y); else band.lineTo (static_cast<float> (x), y);
            }
            g.setColour (colours::track (b).withAlpha (0.35f));
            g.strokePath (band, juce::PathStrokeType (1.0f));
        }
        juce::Path curve;
        for (int x = 0; x < getWidth(); ++x)
        {
            const float y = yForDb (ParametricEq::responseDb (fx->values, freqForX (static_cast<float> (x)), sr));
            if (x == 0) curve.startNewSubPath (static_cast<float> (x), y); else curve.lineTo (static_cast<float> (x), y);
        }
        juce::Path fill (curve);
        fill.lineTo (r.getRight(), yForDb (0)); fill.lineTo (0, yForDb (0)); fill.closeSubPath();
        g.setColour (colours::accent.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (fx->bypassed ? colours::textFaint : colours::accent);
        g.strokePath (curve, juce::PathStrokeType (2.0f));

        // Nodes
        for (int b = 0; b < eq::kNumBands; ++b)
        {
            const auto p = nodePosition (*fx, b);
            const bool on = fx->values[static_cast<size_t> (b * 5)] >= 0.5f;
            g.setColour (on ? colours::track (b) : colours::control);
            g.fillEllipse (p.x - 8, p.y - 8, 16, 16);
            g.setColour (on ? colours::window : colours::textFaint);
            g.setFont (fonts::ui (10.5f, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (p.x - 8, p.y - 8, 16, 16), juce::Justification::centred);
            if (b == activeBand)
            {
                g.setColour (colours::text);
                g.drawEllipse (p.x - 9, p.y - 9, 18, 18, 1.5f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* fx = findFx (app, effectId);
        activeBand = -1;
        if (fx == nullptr) return;
        float best = 14.0f;
        for (int b = 0; b < eq::kNumBands; ++b)
        {
            const float d = nodePosition (*fx, b).getDistanceFrom (e.position);
            if (d < best) { best = d; activeBand = b; }
        }
        if (activeBand >= 0) app.beginGesture ("EQ band " + juce::String (activeBand + 1));
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (activeBand < 0) return;
        const auto prefix = "band" + juce::String (activeBand + 1) + ".";
        const double freq = std::clamp (freqForX (e.position.x), 20.0, 20000.0);
        auto values = makeObject ({ { juce::Identifier (prefix + "freq"), std::round (freq) } });
        const auto type = static_cast<eq::BandType> (juce::roundToInt (valueOf (app, effectId, activeBand * 5 + 1)));
        if (type == eq::BandType::Peak || type == eq::BandType::LowShelf || type == eq::BandType::HighShelf)
            values.getDynamicObject()->setProperty (prefix + "gain", std::round (std::clamp (dbForY (e.position.y), -18.0, 18.0) * 10.0) / 10.0);
        if (valueOf (app, effectId, activeBand * 5) < 0.5f) values.getDynamicObject()->setProperty (prefix + "enabled", true);
        app.run ("set_effect_parameters", makeObject ({ { "effectId", effectId }, { "values", values } }));
    }

    void mouseUp (const juce::MouseEvent&) override { if (activeBand >= 0) app.endGesture(); }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (activeBand < 0) return;
        const auto id = "band" + juce::String (activeBand + 1) + ".enabled";
        setParam (app, effectId, id, valueOf (app, effectId, activeBand * 5) >= 0.5f ? 0.0 : 1.0);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        mouseDown (e);
        if (activeBand < 0) return;
        app.endGesture();
        const float q = valueOf (app, effectId, activeBand * 5 + 4);
        setParam (app, effectId, "band" + juce::String (activeBand + 1) + ".q", std::clamp (q * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15), 0.1, 18.0));
    }

private:
    AppContext& app;
    juce::String effectId;
    int activeBand = -1;

    float xForFreq (double f) const { return static_cast<float> (std::log (f / 20.0) / std::log (1000.0) * getWidth()); }
    double freqForX (float x) const { return 20.0 * std::pow (1000.0, x / std::max (1, getWidth())); }
    float yForDb (double db) const { return static_cast<float> (getHeight() * (0.5 - db / 40.0)); }
    double dbForY (float y) const { return (0.5 - y / std::max (1, getHeight())) * 40.0; }

    juce::Point<float> nodePosition (const EffectInstance& fx, int b) const
    {
        const auto type = static_cast<eq::BandType> (juce::roundToInt (fx.values[static_cast<size_t> (b * 5 + 1)]));
        const double gain = (type == eq::BandType::Peak || type == eq::BandType::LowShelf || type == eq::BandType::HighShelf)
                                ? fx.values[static_cast<size_t> (b * 5 + 3)] : 0.0;
        return { xForFreq (fx.values[static_cast<size_t> (b * 5 + 2)]), yForDb (gain) };
    }
};

// ================================================================================================
// Distortion transfer curve: input level (x) -> output level (y) with drive, bias and auto-gain.
// A clean amp would be the faint diagonal; the further the curve bends away, the dirtier the sound.
// ================================================================================================
class DistortionCurve final : public juce::Component
{
public:
    DistortionCurve (AppContext& a, juce::String id) : app (a), effectId (std::move (id)) {}
    void refresh() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto* fx = findFx (app, effectId);
        const auto* d = fx != nullptr ? fx->descriptor() : nullptr;
        if (d == nullptr) return;
        auto value = [&] (const char* id) { return valueOf (app, effectId, d->indexOf (id)); };
        const auto type = static_cast<distortion::Type> (juce::jlimit (0, 4, juce::roundToInt (value ("type"))));
        const float drive = value ("drive"), bias = value ("bias");
        const float gain = (value ("autogain") >= 0.5f ? Distortion::autoGain (type, drive, bias) : 1.0f)
                           * juce::Decibels::decibelsToGain (value ("output"));
        const float levels = value ("bits") < 15.95f ? std::pow (2.0f, value ("bits") - 1.0f) : 0.0f;

        auto r = getLocalBounds().toFloat();
        g.setColour (colours::window);
        g.fillRoundedRectangle (r, 4.0f);
        auto plot = r.reduced (8.0f);
        const float cx = plot.getCentreX(), cy = plot.getCentreY(), hw = plot.getWidth() * 0.5f, hh = plot.getHeight() * 0.5f;

        // Axes and unity reference.
        g.setColour (colours::lineSoft);
        g.drawHorizontalLine (juce::roundToInt (cy), plot.getX(), plot.getRight());
        g.drawVerticalLine (juce::roundToInt (cx), plot.getY(), plot.getBottom());
        g.setColour (colours::line);
        g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);

        // The curve (clamped to the box; output > 0 dBFS is drawn at the edge).
        juce::Path curve;
        const int steps = std::max (64, juce::roundToInt (plot.getWidth() * 2));
        for (int i = 0; i <= steps; ++i)
        {
            const float x = -1.0f + 2.0f * static_cast<float> (i) / static_cast<float> (steps);
            float y = Distortion::transfer (type, drive, bias, x) * gain;
            if (levels > 0) y = std::round (y * levels) / levels;   // show the bit-crush staircase too
            const float px = cx + x * hw, py = cy - juce::jlimit (-1.0f, 1.0f, y) * hh;
            if (i == 0) curve.startNewSubPath (px, py); else curve.lineTo (px, py);
        }
        g.setColour (colours::amber.withAlpha (0.25f));
        g.strokePath (curve, juce::PathStrokeType (4.0f));
        g.setColour (colours::amber);
        g.strokePath (curve, juce::PathStrokeType (1.6f));

        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (10.0f));
        g.drawText ("IN", plot.removeFromBottom (12).removeFromRight (20), juce::Justification::centredRight);
        g.drawText ("OUT", getLocalBounds().toFloat().reduced (8.0f).removeFromTop (12).removeFromLeft (26), juce::Justification::centredLeft);
    }

private:
    AppContext& app;
    const juce::String effectId;
};

// ================================================================================================
// Tremolo: the gain over two cycles from the starting point (Phase), linear gain bottom-to-top.
// ================================================================================================
class TremoloDisplay final : public juce::Component
{
public:
    TremoloDisplay (AppContext& a, juce::String id) : app (a), effectId (std::move (id)) {}
    void refresh() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto* fx = findFx (app, effectId);
        const auto* d = fx != nullptr ? fx->descriptor() : nullptr;
        if (d == nullptr) return;
        auto value = [&] (const char* id) { return valueOf (app, effectId, d->indexOf (id)); };
        const auto shape = static_cast<modulation::Shape> (juce::jlimit (0, 2, juce::roundToInt (value ("shape"))));
        const float depth = value ("depth");
        const double phase = value ("phase") / 360.0;

        auto r = getLocalBounds().toFloat();
        g.setColour (colours::window);
        g.fillRoundedRectangle (r, 4.0f);
        auto plot = r.reduced (8.0f, 6.0f);
        const auto caption = plot.removeFromBottom (13.0f);
        plot.removeFromBottom (3.0f);
        g.setColour (colours::lineSoft);
        g.drawHorizontalLine (juce::roundToInt (plot.getY()), plot.getX(), plot.getRight());        // unity
        g.drawVerticalLine (juce::roundToInt (plot.getCentreX()), plot.getY(), plot.getBottom());   // cycle boundary

        juce::Path curve;
        const int steps = std::max (64, juce::roundToInt (plot.getWidth()));
        for (int i = 0; i <= steps; ++i)
        {
            const double x = 2.0 * i / steps;   // cycles from the start point
            const float gain = Tremolo::gainAt (shape, depth, x + phase);
            const float px = plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (steps);
            const float py = plot.getBottom() - gain * plot.getHeight();
            if (i == 0) curve.startNewSubPath (px, py); else curve.lineTo (px, py);
        }
        g.setColour (colours::accent.withAlpha (0.25f));
        g.strokePath (curve, juce::PathStrokeType (4.0f));
        g.setColour (colours::accent);
        g.strokePath (curve, juce::PathStrokeType (1.6f));

        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (10.0f));
        const auto restart = juce::roundToInt (value ("restart")) == static_cast<int> (modulation::Restart::Event) ? "each event" : "the bar grid";
        g.drawText ("2 cycles from " + juce::String (restart), caption, juce::Justification::centredRight);
    }

private:
    AppContext& app;
    const juce::String effectId;
};

// ================================================================================================
// Multi-tap pattern display: taps as bars on a beat grid; drag to retime / relevel.
// ================================================================================================
class TapDisplay final : public juce::Component
{
public:
    TapDisplay (AppContext& a, juce::String id) : app (a), effectId (std::move (id)) {}
    void refresh() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto* fx = findFx (app, effectId);
        if (fx == nullptr) return;
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::window);
        g.fillRoundedRectangle (r, 4.0f);
        const double bpm = app.project().bpm;
        const double spanBeats = visibleBeats (*fx, bpm);

        for (int b = 0; b <= static_cast<int> (spanBeats * 4); ++b)
        {
            const float x = static_cast<float> (b / 4.0 / spanBeats * getWidth());
            g.setColour (b % 16 == 0 ? colours::line : b % 4 == 0 ? colours::lineSoft : colours::lineSoft.withAlpha (0.35f));
            g.drawVerticalLine (static_cast<int> (x), r.getY(), r.getBottom());
        }
        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (10.0f));
        g.drawText ("dry", juce::Rectangle<float> (3, 2, 30, 12), juce::Justification::centredLeft);

        for (int t = 0; t < delay::kNumTaps; ++t)
        {
            const bool on = fx->values[static_cast<size_t> (t * 7)] >= 0.5f;
            if (! on) continue;
            const double secs = MultiTapDelay::tapSeconds (fx->values, t, bpm);
            const float x = static_cast<float> (secondsToBeats (secs, bpm) / spanBeats * getWidth());
            const float levelDb = fx->values[static_cast<size_t> (t * 7 + 4)];
            const float h = juce::jmap (juce::jlimit (-60.0f, 6.0f, levelDb), -60.0f, 6.0f, 0.05f, 1.0f) * (r.getHeight() - 18);
            const float pan = fx->values[static_cast<size_t> (t * 7 + 5)];
            const float fb = fx->values[static_cast<size_t> (t * 7 + 6)];
            // Feedback echoes as fading ghosts at multiples of the tap time.
            for (int k = 2; k <= 6 && fb > 0.01f; ++k)
            {
                const float gx = x * k;
                if (gx > getWidth()) break;
                g.setColour (panColour (pan).withAlpha (0.5f * std::pow (fb, static_cast<float> (k - 1))));
                g.fillRect (gx - 1.5f, r.getBottom() - h * std::pow (fb, static_cast<float> (k - 1)), 3.0f, h * std::pow (fb, static_cast<float> (k - 1)));
            }
            g.setColour (panColour (pan).withAlpha (t == activeTap ? 1.0f : 0.85f));
            g.fillRoundedRectangle (x - 4, r.getBottom() - h, 8, h, 2.0f);
            g.setColour (colours::text);
            g.setFont (fonts::ui (10.0f, true));
            g.drawText (juce::String (t + 1), juce::Rectangle<float> (x - 8, r.getBottom() - h - 14, 16, 12), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* fx = findFx (app, effectId);
        activeTap = -1;
        if (fx == nullptr) return;
        const double bpm = app.project().bpm, span = visibleBeats (*fx, bpm);
        float best = 12.0f;
        for (int t = 0; t < delay::kNumTaps; ++t)
        {
            if (fx->values[static_cast<size_t> (t * 7)] < 0.5f) continue;
            const float x = static_cast<float> (secondsToBeats (MultiTapDelay::tapSeconds (fx->values, t, bpm), bpm) / span * getWidth());
            if (std::abs (x - e.position.x) < best) { best = std::abs (x - e.position.x); activeTap = t; }
        }
        dragSpan = span;
        if (activeTap >= 0) app.beginGesture ("Delay tap " + juce::String (activeTap + 1));
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (activeTap < 0) return;
        const auto prefix = "tap" + juce::String (activeTap + 1) + ".";
        const double beats = std::max (0.0, e.position.x / std::max (1, getWidth()) * dragSpan);
        const double bpm = app.project().bpm;
        auto values = makeObject ({});
        if (valueOf (app, effectId, activeTap * 7 + 1) >= 0.5f)
        {
            // Synced: snap to the nearest musical division.
            const auto& divs = delay::divisions();
            size_t best = 0;
            for (size_t i = 1; i < divs.size(); ++i)
                if (std::abs (divs[i].second - beats) < std::abs (divs[best].second - beats)) best = i;
            values.getDynamicObject()->setProperty (prefix + "division", static_cast<int> (best));
        }
        else values.getDynamicObject()->setProperty (prefix + "time", std::clamp (std::round (beatsToSeconds (beats, bpm) * 1000.0), 1.0, 4000.0));
        const double levelDb = juce::jmap (juce::jlimit (0.05, 1.0, (getHeight() - e.position.y) / static_cast<double> (getHeight() - 18)), 0.05, 1.0, -60.0, 6.0);
        values.getDynamicObject()->setProperty (prefix + "level", std::round (levelDb * 2.0) / 2.0);
        app.run ("set_effect_parameters", makeObject ({ { "effectId", effectId }, { "values", values } }));
    }

    void mouseUp (const juce::MouseEvent&) override { if (activeTap >= 0) app.endGesture(); }

private:
    AppContext& app;
    juce::String effectId;
    int activeTap = -1;
    double dragSpan = 4.0;

    static double visibleBeats (const EffectInstance& fx, double bpm)
    {
        double longest = 1.0;
        for (int t = 0; t < delay::kNumTaps; ++t)
            if (fx.values[static_cast<size_t> (t * 7)] >= 0.5f)
                longest = std::max (longest, secondsToBeats (MultiTapDelay::tapSeconds (fx.values, t, bpm), bpm));
        return longest <= 2.0 ? 2.0 : longest <= 4.0 ? 4.0 : 8.0;
    }
};

// ================================================================================================
// EffectModule: header + type-specific editor
// ================================================================================================
class EffectModule final : public juce::Component, private juce::Timer
{
public:
    EffectModule (AppContext& a, const EffectInstance& fx) : effectId (fx.id), type (fx.type), app (a)
    {
        const auto* d = fx.descriptor();
        title = d != nullptr ? d->name : fx.type;
        for (auto* b : { &power, &up, &down, &remove })
        {
            addAndMakeVisible (*b);
            b->setWantsKeyboardFocus (false);
        }
        power.onClick = [this] { if (auto* f = findFx (app, effectId)) app.run ("set_effect_bypass", makeObject ({ { "effectId", effectId }, { "bypassed", ! f->bypassed } })); };
        up.onClick = [this] { move (-1); };
        down.onClick = [this] { move (1); };
        remove.onClick = [this] { app.run ("remove_effect", makeObject ({ { "effectId", effectId } })); };

        auto knob = [this] (const char* id, juce::String label = {})
        {
            const auto* desc = findFx (app, effectId)->descriptor();
            auto k = std::make_unique<ParamKnob> (app, effectId, desc->indexOf (id), label);
            addAndMakeVisible (*k);
            auto* raw = k.get();
            controls.push_back (std::move (k));
            return raw;
        };

        if (type == "eq")
        {
            graph = std::make_unique<EqGraph> (app, effectId);
            addAndMakeVisible (*graph);
            for (int b = 0; b < eq::kNumBands; ++b)
            {
                const auto p = "band" + juce::String (b + 1) + ".";
                const auto* desc = findFx (app, effectId)->descriptor();
                auto on = std::make_unique<ParamToggle> (app, effectId, desc->indexOf (p + "enabled"), juce::String (b + 1), colours::track (b).darker (0.2f));
                auto ty = std::make_unique<ParamChoice> (app, effectId, desc->indexOf (p + "type"));
                addAndMakeVisible (*on);
                addAndMakeVisible (*ty);
                bandRows.push_back ({ on.get(), ty.get(), knob ((p + "freq").toRawUTF8()), knob ((p + "gain").toRawUTF8()), knob ((p + "q").toRawUTF8()) });
                controls.push_back (std::move (on));
                controls.push_back (std::move (ty));
            }
            globalKnobs.push_back (knob ("output"));
        }
        else if (type == "multitap_delay")
        {
            taps = std::make_unique<TapDisplay> (app, effectId);
            addAndMakeVisible (*taps);
            const auto* desc = findFx (app, effectId)->descriptor();
            for (int t = 0; t < delay::kNumTaps; ++t)
            {
                const auto p = "tap" + juce::String (t + 1) + ".";
                auto on = std::make_unique<ParamToggle> (app, effectId, desc->indexOf (p + "enabled"), juce::String (t + 1));
                auto sync = std::make_unique<ParamToggle> (app, effectId, desc->indexOf (p + "sync"), "SYNC");
                auto div = std::make_unique<ParamChoice> (app, effectId, desc->indexOf (p + "division"));
                addAndMakeVisible (*on); addAndMakeVisible (*sync); addAndMakeVisible (*div);
                tapRows.push_back ({ on.get(), sync.get(), div.get(), knob ((p + "time").toRawUTF8()), knob ((p + "level").toRawUTF8()),
                                     knob ((p + "pan").toRawUTF8()), knob ((p + "feedback").toRawUTF8(), "FB") });
                controls.push_back (std::move (on)); controls.push_back (std::move (sync)); controls.push_back (std::move (div));
            }
            for (auto* id : { "mix", "lowcut", "highcut", "drive" }) globalKnobs.push_back (knob (id));
        }
        else if (type == "distortion")
        {
            // Curve on the left; type + auto-gain beside it; the knobs in a grid below.
            curve = std::make_unique<DistortionCurve> (app, effectId);
            addAndMakeVisible (*curve);
            const auto* desc = findFx (app, effectId)->descriptor();
            auto ty = std::make_unique<ParamChoice> (app, effectId, desc->indexOf ("type"));
            auto ag = std::make_unique<ParamToggle> (app, effectId, desc->indexOf ("autogain"), "AUTO GAIN");
            addAndMakeVisible (*ty); addAndMakeVisible (*ag);
            distType = ty.get(); distAutoGain = ag.get();
            controls.push_back (std::move (ty)); controls.push_back (std::move (ag));
            for (auto* id : { "drive", "bias", "lowcut", "tone", "bits", "downsample", "mix", "output" }) globalKnobs.push_back (knob (id));
        }
        else
        {
            // Continuous parameters are knobs; choices and toggles sit in a row of selectors above them.
            if (const auto* desc = findFx (app, effectId)->descriptor())
                for (size_t i = 0; i < desc->params.size(); ++i)
                {
                    const auto& p = desc->params[i];
                    if (p.kind == ParamKind::Continuous) { globalKnobs.push_back (knob (p.id.toRawUTF8())); continue; }
                    std::unique_ptr<ParamControl> ctl;
                    if (p.kind == ParamKind::Choice) ctl = std::make_unique<ParamChoice> (app, effectId, static_cast<int> (i));
                    else ctl = std::make_unique<ParamToggle> (app, effectId, static_cast<int> (i), p.name.toUpperCase());
                    addAndMakeVisible (*ctl);
                    selectors.push_back (ctl.get());
                    controls.push_back (std::move (ctl));
                }
            if (type == "compressor") startTimerHz (30);
            if (type == "tremolo")
            {
                cycle = std::make_unique<TremoloDisplay> (app, effectId);
                addAndMakeVisible (*cycle);
            }
        }
        refresh();
    }

    int preferredHeight (int width) const
    {
        const int header = 30;
        if (type == "eq") return header + 150 + 8 + 20 + 24 + 3 * 60 + 10;
        if (type == "multitap_delay") return header + 96 + 8 + delay::kNumTaps * 58 + 76 + 10;
        const int perRow = std::max (1, (width - 16) / 70);
        const int rows = (static_cast<int> (globalKnobs.size()) + perRow - 1) / perRow;
        const int selectorRows = selectors.empty() ? 0 : (static_cast<int> (selectors.size()) + selectorsPerRow (width) - 1) / selectorsPerRow (width);
        return header + rows * 76 + (type == "compressor" ? 26 : 0) + (type == "distortion" ? kCurveHeight + 8 : 0)
               + (cycle != nullptr ? kCycleHeight + 8 : 0) + selectorRows * kSelectorHeight + 12;
    }

    void refresh()
    {
        const auto* fx = findFx (app, effectId);
        if (fx == nullptr) return;
        power.setToggleState (! fx->bypassed, juce::dontSendNotification);
        for (auto& c : controls) c->refresh();
        if (graph) graph->refresh();
        if (taps) taps->refresh();
        if (curve) curve->refresh();
        if (cycle) cycle->refresh();
        // Cycling effects: Rate applies when free-running, Division when synced.
        if (const auto* d = fx->descriptor(); d != nullptr && d->indexOf ("sync") >= 0)
        {
            const bool synced = fx->values[static_cast<size_t> (d->indexOf ("sync"))] >= 0.5f;
            bool changed = false;
            auto show = [&] (juce::Component* c, bool visible) { if (c != nullptr && c->isVisible() != visible) { c->setVisible (visible); changed = true; } };
            for (auto* k : globalKnobs) if (k->paramSpec().id == "rate") show (k, ! synced);
            for (auto* sel : selectors) if (sel->paramSpec().id == "division") show (sel, synced);
            if (changed) resized();
        }
        // Hide the parameter that does not apply (division when free-running, time when synced).
        for (size_t t = 0; t < tapRows.size(); ++t)
        {
            const bool synced = fx->values[static_cast<size_t> (t * 7 + 1)] >= 0.5f;
            tapRows[t].division->setVisible (synced);
            tapRows[t].time->setVisible (! synced);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto* fx = findFx (app, effectId);
        const bool bypassed = fx != nullptr && fx->bypassed;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colours::raised);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (bypassed ? colours::lineSoft : colours::line);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        auto header = getLocalBounds().removeFromTop (30).reduced (10, 0);
        g.setColour (bypassed ? colours::textFaint : colours::text);
        g.setFont (fonts::display (13.0f));
        g.drawText (title.toUpperCase(), header.withTrimmedLeft (26), juce::Justification::centredLeft);
        if (bypassed)
        {
            g.setColour (colours::amber);
            g.setFont (fonts::ui (11.0f, true));
            g.drawText ("BYPASSED", header.withTrimmedRight (90), juce::Justification::centredRight);
        }
        if (type == "compressor")
        {
            auto gr = grArea.toFloat();
            g.setColour (colours::window);
            g.fillRoundedRectangle (gr, 3.0f);
            const float prop = juce::jlimit (0.0f, 1.0f, grShown / 24.0f);
            g.setColour (colours::amber);
            g.fillRoundedRectangle (gr.withWidth (gr.getWidth() * prop), 3.0f);
            g.setColour (colours::textDim);
            g.setFont (fonts::ui (11.0f));
            g.drawText ("GR  " + juce::String (grShown, 1) + " dB", grArea.reduced (6, 0), juce::Justification::centredLeft);
        }
        if (type == "multitap_delay")
        {
            g.setColour (colours::textFaint);
            g.setFont (fonts::ui (10.5f, true));
            for (size_t t = 0; t < tapRows.size(); ++t)
                g.drawText ("TAP", tapRows[t].on->getBounds().translated (0, -13).withHeight (12), juce::Justification::centred);
        }
        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (11.0f, true));
        for (auto* sel : selectors)
            if (sel->isVisible() && sel->paramSpec().kind == ParamKind::Choice)
                g.drawText (sel->paramSpec().name, sel->getBounds().translated (0, -14).withHeight (13), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        auto header = r.removeFromTop (30);
        power.setBounds (header.removeFromLeft (24).reduced (2, 5));
        remove.setBounds (header.removeFromRight (24).reduced (2, 5));
        down.setBounds (header.removeFromRight (24).reduced (2, 5));
        up.setBounds (header.removeFromRight (24).reduced (2, 5));
        r.removeFromTop (2);

        if (type == "eq")
        {
            auto top = r.removeFromTop (150);
            globalKnobs[0]->setBounds (top.removeFromRight (64).withSizeKeepingCentre (64, 76));
            top.removeFromRight (4);
            graph->setBounds (top);
            r.removeFromTop (8);
            const int colW = r.getWidth() / eq::kNumBands;
            auto toggles = r.removeFromTop (20);
            auto types = r.removeFromTop (24);
            for (int b = 0; b < eq::kNumBands; ++b)
            {
                const auto& row = bandRows[static_cast<size_t> (b)];
                row.on->setBounds (toggles.removeFromLeft (colW).reduced (3, 0));
                row.type->setBounds (types.removeFromLeft (colW).reduced (2, 2));
            }
            for (int k = 0; k < 3; ++k)
            {
                auto line = r.removeFromTop (60);
                for (int b = 0; b < eq::kNumBands; ++b)
                {
                    const auto& row = bandRows[static_cast<size_t> (b)];
                    auto* ctl = k == 0 ? row.freq : k == 1 ? row.gain : row.q;
                    ctl->setBounds (line.removeFromLeft (colW));
                }
            }
            return;
        }
        if (type == "multitap_delay")
        {
            taps->setBounds (r.removeFromTop (96));
            r.removeFromTop (8);
            for (auto& row : tapRows)
            {
                auto line = r.removeFromTop (58);
                auto left = line.removeFromLeft (64).withTrimmedTop (14);
                row.on->setBounds (left.removeFromTop (20).reduced (2, 0));
                left.removeFromTop (2);
                row.sync->setBounds (left.removeFromTop (20).reduced (2, 0));
                auto timeArea = line.removeFromLeft (90);
                row.division->setBounds (timeArea.withSizeKeepingCentre (84, 24));
                row.time->setBounds (timeArea);
                const int kw = line.getWidth() / 3;
                row.level->setBounds (line.removeFromLeft (kw));
                row.pan->setBounds (line.removeFromLeft (kw));
                row.feedback->setBounds (line);
            }
            r.removeFromTop (6);
            auto line = r.removeFromTop (70);
            const int kw = line.getWidth() / static_cast<int> (globalKnobs.size());
            for (auto* k : globalKnobs) k->setBounds (line.removeFromLeft (kw));
            return;
        }
        if (type == "distortion")
        {
            auto top = r.removeFromTop (kCurveHeight);
            auto side = top.removeFromRight (std::min (120, top.getWidth() / 3));
            top.removeFromRight (6);
            curve->setBounds (top);
            distType->setBounds (side.removeFromTop (26).reduced (0, 1));
            side.removeFromTop (8);
            distAutoGain->setBounds (side.removeFromTop (22));
            r.removeFromTop (8);
        }
        if (cycle != nullptr)
        {
            cycle->setBounds (r.removeFromTop (kCycleHeight));
            r.removeFromTop (8);
        }
        if (! selectors.empty())
        {
            // Visible selectors only (Division hides while free-running), in rows: label above, control below.
            std::vector<ParamControl*> shown;
            for (auto* sel : selectors) if (sel->isVisible()) shown.push_back (sel);
            const int perRow = selectorsPerRow (getWidth());
            const int sw = r.getWidth() / perRow;
            const int rows = (static_cast<int> (selectors.size()) + perRow - 1) / perRow;
            auto area = r.removeFromTop (rows * kSelectorHeight);
            for (size_t i = 0; i < shown.size(); ++i)
            {
                const int row = static_cast<int> (i) / perRow, col = static_cast<int> (i) % perRow;
                shown[i]->setBounds (area.getX() + col * sw + 3, area.getY() + row * kSelectorHeight + 15, sw - 6, kSelectorHeight - 19);
            }
        }
        std::vector<ParamKnob*> knobs;
        for (auto* k : globalKnobs) if (k->isVisible()) knobs.push_back (k);
        const int perRow = std::max (1, (r.getWidth()) / 70);
        const int kw = r.getWidth() / perRow;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int row = static_cast<int> (i) / perRow, col = static_cast<int> (i) % perRow;
            knobs[i]->setBounds (r.getX() + col * kw, r.getY() + row * 76, kw, 72);
        }
        const int rows = (static_cast<int> (knobs.size()) + perRow - 1) / perRow;
        grArea = juce::Rectangle<int> (r.getX(), r.getY() + rows * 76 + 2, r.getWidth(), 18);
    }

    const juce::String effectId, type;

private:
    AppContext& app;
    juce::String title;
    IconButton power { "Enable", Icon::Power, "Enable / bypass" };
    IconButton up { "Move up", Icon::Up, "Move earlier in the chain" };
    IconButton down { "Move down", Icon::Down, "Move later in the chain" };
    IconButton remove { "Remove", Icon::Close, "Remove effect" };
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<ParamKnob*> globalKnobs;
    struct BandRow { ParamToggle* on; ParamChoice* type; ParamKnob* freq; ParamKnob* gain; ParamKnob* q; };
    std::vector<BandRow> bandRows;
    struct TapRow { ParamToggle* on; ParamToggle* sync; ParamChoice* division; ParamKnob* time; ParamKnob* level; ParamKnob* pan; ParamKnob* feedback; };
    std::vector<TapRow> tapRows;
    std::unique_ptr<EqGraph> graph;
    std::unique_ptr<TapDisplay> taps;
    std::unique_ptr<DistortionCurve> curve;
    std::unique_ptr<TremoloDisplay> cycle;
    std::vector<ParamControl*> selectors;   // generic editor: choice / toggle parameters
    static constexpr int kCycleHeight = 84, kSelectorHeight = 42;
    static int selectorsPerRow (int width) { return std::max (1, (width - 16) / 96); }
    ParamChoice* distType = nullptr;
    ParamToggle* distAutoGain = nullptr;
    static constexpr int kCurveHeight = 120;
    juce::Rectangle<int> grArea;
    float grShown = 0;

    void move (int direction)
    {
        const auto* chain = const_cast<Project&> (app.project()).findEffectChain (effectId);
        if (chain == nullptr) return;
        int index = 0;
        for (size_t i = 0; i < chain->size(); ++i) if ((*chain)[i].id == effectId) index = static_cast<int> (i);
        const int to = index + direction;
        if (to < 0 || to >= static_cast<int> (chain->size())) return;
        app.run ("move_effect", makeObject ({ { "effectId", effectId }, { "index", to } }));
    }

    void timerCallback() override
    {
        const float gr = app.engine.effectTelemetry (effectId);
        const float next = gr > grShown ? gr : grShown * 0.85f + gr * 0.15f;
        if (std::abs (next - grShown) > 0.05f) { grShown = next; repaint (grArea); }
    }
};

// ================================================================================================
// FxRackPanel
// ================================================================================================
FxRackPanel::FxRackPanel (AppContext& a) : app (a)
{
    targetBox.setWantsKeyboardFocus (false);
    targetBox.setTooltip ("Which chain the rack shows: a track, or the master bus");
    targetBox.onChange = [this]
    {
        const int i = targetBox.getSelectedItemIndex();
        if (i < 0 || i >= targetIds.size()) return;
        const auto id = targetIds[i];
        if (id == "master") app.fxTarget = "master";
        else { app.fxTarget = {}; app.selectTrack (id); }
        rebuild (true);
    };

    static const juce::StringArray modulationTypes { "chorus", "phaser", "tremolo" };   // under "+ MOD"
    for (const auto& d : effectCatalogue())
    {
        if (modulationTypes.contains (d.type)) continue;
        const auto label = d.type == "eq" ? "+ EQ" : d.type == "compressor" ? "+ COMP" : d.type == "reverb" ? "+ VERB"
                         : d.type == "distortion" ? "+ DIST" : "+ DELAY";
        auto* b = addButtons.add (new juce::TextButton (label));
        b->setTooltip ("Add " + d.name + " to the end of this chain");
        b->setWantsKeyboardFocus (false);
        const auto type = d.type;
        b->onClick = [this, type]
        {
            const auto target = currentTarget();
            if (target.isEmpty()) { app.status ("Select a track (or Master) first", true); return; }
            app.run ("add_effect", makeObject ({ { "target", target }, { "type", type } }));
        };
        addAndMakeVisible (b);
    }
    auto* mod = addButtons.add (new juce::TextButton ("+ MOD"));
    mod->setTooltip ("Add chorus, phaser or tremolo - or a preset chain of ordinary effects");
    mod->setWantsKeyboardFocus (false);
    mod->onClick = [this, mod]
    {
        const auto target = currentTarget();
        if (target.isEmpty()) { app.status ("Select a track (or Master) first", true); return; }
        juce::PopupMenu menu, presets;
        for (int i = 0; i < modulationTypes.size(); ++i)
            menu.addItem (1 + i, "Add " + findEffectType (modulationTypes[i])->name);
        const auto& list = effectPresets();
        for (size_t i = 0; i < list.size(); ++i)
            presets.addItem (100 + static_cast<int> (i), list[i].name);
        menu.addSeparator();
        menu.addSubMenu ("Add preset chain", presets);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (mod), [this, target] (int result)
        {
            if (result >= 1 && result <= modulationTypes.size())
                app.run ("add_effect", makeObject ({ { "target", target }, { "type", modulationTypes[result - 1] } }));
            else if (result >= 100 && static_cast<size_t> (result - 100) < effectPresets().size())
                app.run ("add_effect_preset", makeObject ({ { "target", target }, { "preset", effectPresets()[static_cast<size_t> (result - 100)].id } }));
        });
    };
    addAndMakeVisible (mod);
    viewport.setViewedComponent (&moduleHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    app.controller.addListener (this);
    app.selectionChanged.addChangeListener (this);
    rebuild (true);
}

FxRackPanel::~FxRackPanel()
{
    app.controller.removeListener (this);
    app.selectionChanged.removeChangeListener (this);
}

juce::String FxRackPanel::currentTarget() const
{
    if (app.fxTarget == "master") return "master";
    if (app.selectedTrack.isNotEmpty() && app.project().findTrack (app.selectedTrack) != nullptr) return app.selectedTrack;
    return {};
}

void FxRackPanel::followNewEffect (const Project& p)
{
    std::set<juce::String> now;
    juce::String chainWithNewEffect;
    auto scan = [&] (const std::vector<EffectInstance>& chain, const juce::String& owner)
    {
        for (const auto& fx : chain)
        {
            now.insert (fx.id);
            if (knownEffects.count (fx.id) == 0) chainWithNewEffect = owner;
        }
    };
    for (const auto& t : p.tracks) scan (t.effects, t.id);
    scan (p.master.effects, "master");

    const bool freshLoad = app.controller.getLoadCount() != seenLoadCount;
    seenLoadCount = app.controller.getLoadCount();
    knownEffects = std::move (now);
    if (freshLoad || chainWithNewEffect.isEmpty() || chainWithNewEffect == currentTarget()) return;

    if (chainWithNewEffect == "master")
        app.fxTarget = "master";
    else
    {
        app.fxTarget = {};
        app.selectTrack (chainWithNewEffect);
    }
}

const std::vector<EffectInstance>* FxRackPanel::currentChain() const
{
    const auto target = currentTarget();
    if (target == "master") return &app.project().master.effects;
    if (const auto* t = app.project().findTrack (target)) return &t->effects;
    return nullptr;
}

void FxRackPanel::rebuild (bool force)
{
    // Target selector reflects tracks + master.
    const auto& p = app.project();
    juce::StringArray ids, names;
    for (const auto& t : p.tracks) { ids.add (t.id); names.add (t.name); }
    ids.add ("master"); names.add ("Master bus");
    if (ids != targetIds || force)
    {
        targetIds = ids;
        targetBox.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i) targetBox.addItem (names[i], i + 1);
    }
    else
        for (int i = 0; i < names.size(); ++i) targetBox.changeItemText (i + 1, names[i]);
    targetBox.setSelectedItemIndex (targetIds.indexOf (currentTarget()), juce::dontSendNotification);

    const auto* chain = currentChain();
    juce::String signature = currentTarget() + "|";
    if (chain != nullptr) for (const auto& fx : *chain) signature << fx.id << ":" << fx.type << ",";

    if (signature != shownSignature || force)
    {
        const bool targetChanged = ! shownSignature.startsWith (currentTarget() + "|");
        modules.clear();
        if (chain != nullptr)
            for (const auto& fx : *chain)
            {
                modules.push_back (std::make_unique<EffectModule> (app, fx));
                moduleHolder.addAndMakeVisible (*modules.back());
            }
        shownSignature = signature;
        layoutModules();
        if (targetChanged) viewport.setViewPosition (0, 0);
    }
    else
        for (auto& m : modules) m->refresh();
    repaint();
}

void FxRackPanel::layoutModules()
{
    const int width = std::max (200, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    int y = 0;
    for (auto& m : modules)
    {
        const int h = m->preferredHeight (width);
        m->setBounds (0, y, width, h);
        y += h + 8;
    }
    moduleHolder.setSize (width, std::max (y, viewport.getHeight()));
}

void FxRackPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    if (modules.empty())
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (13.0f));
        const auto msg = currentTarget().isEmpty() ? "Select a track to see its effects" : "No effects on this chain - add one above";
        g.drawFittedText (msg, viewport.getBounds().reduced (20), juce::Justification::centredTop, 3);
    }
}

void FxRackPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    // Widths follow the label lengths, so "+ DELAY" still fits beside the shorter labels.
    auto buttons = r.removeFromTop (28);
    int letters = 0;
    for (auto* b : addButtons) letters += b->getButtonText().length() + 2;
    const int total = buttons.getWidth();
    for (auto* b : addButtons)
        b->setBounds (buttons.removeFromLeft (total * (b->getButtonText().length() + 2) / std::max (1, letters)).reduced (2, 0));
    r.removeFromTop (6);
    viewport.setBounds (r);
    layoutModules();
}
} // namespace mcpdaw::ui
