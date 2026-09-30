// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Theme.h"
#include <BinaryData.h>

namespace mcpdaw::ui
{
juce::Colour colours::track (int index)
{
    static const juce::Colour palette[kNumTrackColours] = {
        juce::Colour (0xff3fb8af), juce::Colour (0xff5b8def), juce::Colour (0xff9b7be0), juce::Colour (0xffd9728f),
        juce::Colour (0xffd9a441), juce::Colour (0xff6cc08b), juce::Colour (0xff4fb3d9), juce::Colour (0xffe08a55) };
    return palette[((index % kNumTrackColours) + kNumTrackColours) % kNumTrackColours];
}

// ================================================================================================
// Fonts
// ================================================================================================
namespace
{
juce::Typeface::Ptr audiowideTypeface()
{
    static juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (BinaryData::AudiowideRegular_ttf,
                                                                               BinaryData::AudiowideRegular_ttfSize);
    return face;
}
} // namespace

juce::Font fonts::display (float h) { return juce::Font (juce::FontOptions (audiowideTypeface()).withHeight (h)); }

juce::Font fonts::ui (float h, bool bold)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font fonts::mono (float h)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

// ================================================================================================
// LookAndFeel
// ================================================================================================
MakeTechLookAndFeel::MakeTechLookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, window);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::TextButton::buttonColourId, control);
    setColour (juce::TextButton::buttonOnColourId, accentDim);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, textDim);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accentDim);
    setColour (juce::Slider::backgroundColourId, window);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, control);
    setColour (juce::Slider::textBoxTextColourId, textDim);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, control);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, window);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accentDim.withAlpha (0.6f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, controlHi);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::AlertWindow::backgroundColourId, raised);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, line);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, accentDim);
    setColour (juce::DirectoryContentsDisplayComponent::textColourId, text);
    setColour (juce::CaretComponent::caretColourId, accent);

    auto scheme = getCurrentColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, window);
    scheme.setUIColour (ColourScheme::widgetBackground, control);
    scheme.setUIColour (ColourScheme::menuBackground, raised);
    scheme.setUIColour (ColourScheme::outline, line);
    scheme.setUIColour (ColourScheme::defaultText, text);
    scheme.setUIColour (ColourScheme::defaultFill, accentDim);
    scheme.setUIColour (ColourScheme::highlightedText, juce::Colours::white);
    scheme.setUIColour (ColourScheme::highlightedFill, accentDim);
    scheme.setUIColour (ColourScheme::menuText, text);
    setColourScheme (scheme);
}

void MakeTechLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto base = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (down) base = base.brighter (0.15f);
    else if (highlighted) base = base.brighter (0.08f);
    if (! b.isEnabled()) base = base.withMultipliedAlpha (0.5f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colours::line.withAlpha (0.7f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void MakeTechLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour ((b.getToggleState() ? b.findColour (juce::TextButton::textColourOnId) : b.findColour (juce::TextButton::textColourOffId))
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

void MakeTechLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // Pill switch + label.
    auto r = b.getLocalBounds().toFloat();
    auto pill = r.removeFromLeft (28.0f).withSizeKeepingCentre (26.0f, 14.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? colours::accentDim : colours::control.brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (pill, 7.0f);
    g.setColour (on ? colours::accent : colours::textFaint);
    const float d = 10.0f;
    g.fillEllipse (on ? pill.getRight() - d - 2 : pill.getX() + 2, pill.getCentreY() - d / 2, d, d);
    g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (fonts::ui (13.0f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft, 1);
}

void MakeTechLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float track = std::max (2.5f, radius * 0.16f);

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, start, end, true);
    g.setColour (colours::window);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters (range symmetric around 0) fill from the centre.
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? start + (end - start) * static_cast<float> (s.valueToProportionOfLength (0.0)) : start;
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, std::min (from, angle), std::max (from, angle), true);
    g.setColour (s.isEnabled() ? s.findColour (juce::Slider::rotarySliderFillColourId) : colours::textFaint);
    g.strokePath (arc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knob = radius - track * 2.2f;
    g.setColour (colours::control);
    g.fillEllipse (centre.x - knob, centre.y - knob, knob * 2, knob * 2);
    g.setColour (colours::line);
    g.drawEllipse (centre.x - knob, centre.y - knob, knob * 2, knob * 2, 1.0f);
    juce::Line<float> needle (centre, centre.getPointOnCircumference (knob * 0.85f, angle));
    g.setColour (colours::text);
    g.drawLine (needle.withShortenedStart (knob * 0.25f), 2.0f);
}

void MakeTechLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                            juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    if (style == juce::Slider::LinearVertical)
    {
        // Mixer fader: slot + cap.
        const auto slot = juce::Rectangle<float> (r.getCentreX() - 2.0f, r.getY(), 4.0f, r.getHeight());
        g.setColour (colours::window);
        g.fillRoundedRectangle (slot, 2.0f);
        g.setColour (colours::accentDim);
        g.fillRoundedRectangle (slot.withTop (pos), 2.0f);
        const auto cap = juce::Rectangle<float> (r.getCentreX() - 11.0f, pos - 6.0f, 22.0f, 12.0f);
        g.setColour (colours::controlHi);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (colours::text);
        g.drawHorizontalLine (juce::roundToInt (pos), cap.getX() + 3, cap.getRight() - 3);
        return;
    }
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
    {
        const auto slot = juce::Rectangle<float> (r.getX(), r.getCentreY() - 2.0f, r.getWidth(), 4.0f);
        g.setColour (colours::window);
        g.fillRoundedRectangle (slot, 2.0f);
        const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        const float zero = bipolar ? r.getX() + r.getWidth() * static_cast<float> (s.valueToProportionOfLength (0.0)) : r.getX();
        g.setColour (colours::accentDim);
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (zero, pos), slot.getY(), std::abs (pos - zero), 4.0f), 2.0f);
        g.setColour (colours::text);
        g.fillEllipse (pos - 6.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f);
        return;
    }
    LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
}

void MakeTechLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, static_cast<float> (w), static_cast<float> (h)).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent : colours::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float cx = static_cast<float> (w) - 12.0f, cy = static_cast<float> (h) * 0.5f;
    arrow.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour (colours::textDim);
    g.fillPath (arrow);
}

void MakeTechLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                         int thumbStart, int thumbSize, bool over, bool down)
{
    g.setColour (colours::window);
    g.fillRect (x, y, w, h);
    auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize) : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (colours::controlHi.brighter (down ? 0.25f : over ? 0.12f : 0.0f));
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

void MakeTechLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (colours::raised);
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
    g.setColour (colours::text);
    g.setFont (fonts::ui (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 3), juce::Justification::centredLeft, 4);
}

void MakeTechLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::raised);
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
}

void MakeTechLookAndFeel::drawMenuBarBackground (juce::Graphics& g, int w, int h, bool, juce::MenuBarComponent&)
{
    g.fillAll (colours::window);
    g.setColour (colours::lineSoft);
    g.drawHorizontalLine (h - 1, 0.0f, static_cast<float> (w));
}

void MakeTechLookAndFeel::drawMenuBarItem (juce::Graphics& g, int w, int h, int index, const juce::String& text, bool over,
                                           bool open, bool isMouseOverBar, juce::MenuBarComponent& bar)
{
    if (text != kHeartMenuName)
    {
        LookAndFeel_V4::drawMenuBarItem (g, w, h, index, text, over, open, isMouseOverBar, bar);
        return;
    }
    if (open || over) { g.setColour (colours::control); g.fillRect (0, 0, w, h); }
    // Heart from two lobes and a point, sized to the menu-bar text height.
    const float s = std::min ((float) h * 0.52f, (float) w * 0.6f);
    const float cx = (float) w * 0.5f, top = ((float) h - s) * 0.5f + s * 0.05f;
    juce::Path heart;
    heart.startNewSubPath (cx, top + s * 0.28f);
    heart.cubicTo (cx - s * 0.05f, top - s * 0.02f, cx - s * 0.55f, top - s * 0.02f, cx - s * 0.52f, top + s * 0.33f);
    heart.cubicTo (cx - s * 0.5f, top + s * 0.58f, cx - s * 0.15f, top + s * 0.75f, cx, top + s * 0.95f);
    heart.cubicTo (cx + s * 0.15f, top + s * 0.75f, cx + s * 0.5f, top + s * 0.58f, cx + s * 0.52f, top + s * 0.33f);
    heart.cubicTo (cx + s * 0.55f, top - s * 0.02f, cx + s * 0.05f, top - s * 0.02f, cx, top + s * 0.28f);
    heart.closeSubPath();
    g.setColour (open || over ? colours::red.brighter (0.2f) : colours::red);
    g.fillPath (heart);
}

int MakeTechLookAndFeel::getMenuBarItemWidth (juce::MenuBarComponent& bar, int index, const juce::String& text)
{
    return text == kHeartMenuName ? 36 : LookAndFeel_V4::getMenuBarItemWidth (bar, index, text);
}

// ================================================================================================
// Icons
// ================================================================================================
void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> a, juce::Colour c)
{
    const float s = std::min (a.getWidth(), a.getHeight());
    a = a.withSizeKeepingCentre (s, s);
    const float x = a.getX(), y = a.getY();
    auto P = [&] (float fx, float fy) { return juce::Point<float> (x + fx * s, y + fy * s); };
    juce::Path p;
    const float stroke = std::max (1.5f, s * 0.1f);
    g.setColour (c);
    switch (icon)
    {
        case Icon::Play:   p.addTriangle (P (0.25f, 0.15f), P (0.25f, 0.85f), P (0.85f, 0.5f)); g.fillPath (p); break;
        case Icon::Stop:   g.fillRoundedRectangle (juce::Rectangle<float> (P (0.22f, 0.22f), P (0.78f, 0.78f)), s * 0.06f); break;
        case Icon::Pause:  g.fillRect (juce::Rectangle<float> (P (0.22f, 0.18f), P (0.42f, 0.82f)));
                           g.fillRect (juce::Rectangle<float> (P (0.58f, 0.18f), P (0.78f, 0.82f))); break;
        case Icon::Rewind: g.fillRect (juce::Rectangle<float> (P (0.18f, 0.2f), P (0.28f, 0.8f)));
                           p.addTriangle (P (0.82f, 0.2f), P (0.82f, 0.8f), P (0.32f, 0.5f)); g.fillPath (p); break;
        case Icon::Loop:
            p.addRoundedRectangle (juce::Rectangle<float> (P (0.15f, 0.3f), P (0.85f, 0.75f)), s * 0.15f);
            g.strokePath (p, juce::PathStrokeType (stroke));
            p.clear(); p.addTriangle (P (0.5f, 0.18f), P (0.5f, 0.42f), P (0.66f, 0.3f)); g.fillPath (p); break;
        case Icon::Undo:
        case Icon::Redo:
        {
            const bool redo = icon == Icon::Redo;
            auto X = [&] (float fx) { return redo ? 1.0f - fx : fx; };
            p.startNewSubPath (P (X (0.3f), 0.4f));
            p.cubicTo (P (X (0.55f), 0.4f), P (X (0.85f), 0.4f), P (X (0.85f), 0.62f));
            p.cubicTo (P (X (0.85f), 0.8f), P (X (0.6f), 0.82f), P (X (0.45f), 0.82f));
            g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            p.clear(); p.addTriangle (P (X (0.12f), 0.4f), P (X (0.36f), 0.22f), P (X (0.36f), 0.58f)); g.fillPath (p); break;
        }
        case Icon::Detach:
            g.drawRect (juce::Rectangle<float> (P (0.15f, 0.3f), P (0.7f, 0.85f)), stroke * 0.8f);
            p.startNewSubPath (P (0.45f, 0.55f)); p.lineTo (P (0.85f, 0.15f));
            p.startNewSubPath (P (0.55f, 0.15f)); p.lineTo (P (0.85f, 0.15f)); p.lineTo (P (0.85f, 0.45f));
            g.strokePath (p, juce::PathStrokeType (stroke)); break;
        case Icon::Dock:
            g.drawRect (juce::Rectangle<float> (P (0.15f, 0.15f), P (0.85f, 0.85f)), stroke * 0.8f);
            g.fillRect (juce::Rectangle<float> (P (0.15f, 0.55f), P (0.85f, 0.85f))); break;
        case Icon::Plus:
            g.fillRect (juce::Rectangle<float> (P (0.44f, 0.18f), P (0.56f, 0.82f)));
            g.fillRect (juce::Rectangle<float> (P (0.18f, 0.44f), P (0.82f, 0.56f))); break;
        case Icon::Close:
            p.startNewSubPath (P (0.25f, 0.25f)); p.lineTo (P (0.75f, 0.75f));
            p.startNewSubPath (P (0.75f, 0.25f)); p.lineTo (P (0.25f, 0.75f));
            g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded)); break;
        case Icon::Up:    p.addTriangle (P (0.2f, 0.68f), P (0.8f, 0.68f), P (0.5f, 0.3f)); g.fillPath (p); break;
        case Icon::Down:  p.addTriangle (P (0.2f, 0.32f), P (0.8f, 0.32f), P (0.5f, 0.7f)); g.fillPath (p); break;
        case Icon::Power:
            p.addCentredArc (a.getCentreX(), a.getCentreY() + s * 0.04f, s * 0.3f, s * 0.3f, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
            p.startNewSubPath (P (0.5f, 0.12f)); p.lineTo (P (0.5f, 0.5f));
            g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); break;
        case Icon::Folder:
            p.startNewSubPath (P (0.12f, 0.28f)); p.lineTo (P (0.42f, 0.28f)); p.lineTo (P (0.5f, 0.36f)); p.lineTo (P (0.88f, 0.36f));
            p.lineTo (P (0.88f, 0.8f)); p.lineTo (P (0.12f, 0.8f)); p.closeSubPath(); g.fillPath (p); break;
        case Icon::Wave:
            p.startNewSubPath (P (0.08f, 0.5f));
            for (int i = 1; i <= 16; ++i)
            {
                const float t = static_cast<float> (i) / 16.0f;
                const float amp = 0.32f * std::sin (t * juce::MathConstants<float>::pi) * ((i % 2) ? 1.0f : -0.6f);
                p.lineTo (P (0.08f + 0.84f * t, 0.5f + amp));
            }
            g.strokePath (p, juce::PathStrokeType (stroke * 0.7f)); break;
        case Icon::Speaker:
            p.startNewSubPath (P (0.15f, 0.38f)); p.lineTo (P (0.32f, 0.38f)); p.lineTo (P (0.55f, 0.18f)); p.lineTo (P (0.55f, 0.82f));
            p.lineTo (P (0.32f, 0.62f)); p.lineTo (P (0.15f, 0.62f)); p.closeSubPath(); g.fillPath (p);
            p.clear(); p.addCentredArc (P (0.55f, 0.5f).x, P (0.55f, 0.5f).y, s * 0.22f, s * 0.22f, 0.0f, 0.6f, 2.5f, true);
            g.strokePath (p, juce::PathStrokeType (stroke * 0.8f)); break;
    }
}

IconButton::IconButton (const juce::String& name, Icon i, const juce::String& tooltip) : juce::Button (name), icon (i)
{
    setTooltip (tooltip.isNotEmpty() ? tooltip : name);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    g.setColour (on ? onColour.withAlpha (0.22f) : (down ? colours::controlHi : highlighted ? colours::control : juce::Colours::transparentBlack));
    g.fillRoundedRectangle (r, 4.0f);
    if (on)
    {
        g.setColour (onColour.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    const auto c = ! isEnabled() ? colours::textFaint.withAlpha (0.5f) : on ? onColour : highlighted ? colours::text : colours::textDim;
    drawIcon (g, icon, r.reduced (r.getHeight() * 0.2f), c);
}

// ================================================================================================
// LevelMeter
// ================================================================================================
void LevelMeter::setLevels (float l, float r)
{
    const float in[2] = { l, r };
    bool changed = false;
    for (int c = 0; c < 2; ++c)
    {
        // Instant attack, ~20 dB/s visual release at 30 Hz.
        const float decayed = level[c] * 0.86f;
        const float next = std::max (in[c], decayed < 1e-4f ? 0.0f : decayed);
        if (std::abs (next - level[c]) > 1e-5f) { level[c] = next; changed = true; }
        if (in[c] >= hold[c]) { hold[c] = in[c]; holdFrames[c] = 45; changed = true; }
        else if (--holdFrames[c] <= 0 && hold[c] > 0) { hold[c] = level[c]; changed = true; }
    }
    if (changed) repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::window);
    g.fillRoundedRectangle (r, 2.0f);
    // Map -60..+6 dB to the meter length.
    auto toProp = [] (float lin)
    {
        if (lin <= 0.001f) return 0.0f;
        const float db = 20.0f * std::log10 (lin);
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    };
    const float zeroDb = 60.0f / 66.0f;
    for (int c = 0; c < 2; ++c)
    {
        auto lane = horizontal ? r.withHeight (r.getHeight() / 2 - 1).withY (r.getY() + c * r.getHeight() / 2)
                               : r.withWidth (r.getWidth() / 2 - 1).withX (r.getX() + c * r.getWidth() / 2);
        lane = lane.reduced (1.0f);
        const float p = toProp (level[c]);
        auto fill = horizontal ? lane.withWidth (lane.getWidth() * p) : lane.withTop (lane.getBottom() - lane.getHeight() * p);
        const auto colour = level[c] > 1.0f ? colours::red : p > toProp (0.5f) ? colours::amber.interpolatedWith (colours::accent, 0.35f) : colours::accent;
        g.setColour (colour.withAlpha (0.9f));
        g.fillRect (fill);
        const float hp = toProp (hold[c]);
        if (hp > 0)
        {
            g.setColour (hold[c] > 1.0f ? colours::red : colours::text.withAlpha (0.8f));
            if (horizontal) g.fillRect (lane.getX() + lane.getWidth() * hp - 1, lane.getY(), 2.0f, lane.getHeight());
            else g.fillRect (lane.getX(), lane.getBottom() - lane.getHeight() * hp, lane.getWidth(), 2.0f);
        }
    }
    // 0 dB tick
    g.setColour (colours::textFaint.withAlpha (0.6f));
    if (horizontal) g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * zeroDb), r.getY(), r.getBottom());
    else g.drawHorizontalLine (juce::roundToInt (r.getBottom() - r.getHeight() * zeroDb), r.getX(), r.getRight());
    if (clipped)
    {
        g.setColour (colours::red);
        if (horizontal) g.fillRect (r.removeFromRight (4.0f));
        else g.fillRect (r.removeFromTop (4.0f));
    }
}
} // namespace mcpdaw::ui
