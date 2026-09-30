#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Make Tech visual language: charcoal/slate surfaces, off-white text, one restrained teal accent,
// amber for mute/warnings, red only for clipping. Audiowide is reserved for branding, headings and
// a few signature controls; dense labels use the platform UI font for legibility.

#include <juce_gui_extra/juce_gui_extra.h>

namespace mcpdaw::ui
{
namespace colours
{
    inline const juce::Colour window     { 0xff1c2129 };   // deepest background
    inline const juce::Colour panel      { 0xff252c36 };   // panel bodies
    inline const juce::Colour raised     { 0xff2f3743 };   // headers, strips
    inline const juce::Colour control    { 0xff3b4553 };   // buttons, fields
    inline const juce::Colour controlHi  { 0xff485465 };
    inline const juce::Colour line       { 0xff465161 };
    inline const juce::Colour lineSoft   { 0xff343d49 };
    inline const juce::Colour text       { 0xffedf0f4 };
    inline const juce::Colour textDim    { 0xffaab5c3 };
    inline const juce::Colour textFaint  { 0xff7b8797 };
    inline const juce::Colour accent     { 0xff3fd0c9 };   // Make Tech teal
    inline const juce::Colour accentDim  { 0xff2b8f8b };
    inline const juce::Colour amber      { 0xfff0b44c };
    inline const juce::Colour red        { 0xffe5534b };
    inline const juce::Colour playhead   { 0xfff4f7fa };

    /** Muted, distinguishable track colours (index = Track::colour). */
    juce::Colour track (int index);
    inline constexpr int kNumTrackColours = 8;
}

namespace fonts
{
    juce::Font display (float height);   // Audiowide
    juce::Font ui (float height, bool bold = false);
    juce::Font mono (float height);
}

class MakeTechLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MakeTechLookAndFeel();


    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return fonts::ui (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return fonts::ui (std::min (14.0f, h * 0.55f), true); }
    juce::Font getPopupMenuFont() override { return fonts::ui (14.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize,
                        bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 12; }
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawMenuBarBackground (juce::Graphics&, int w, int h, bool, juce::MenuBarComponent&) override;
    void drawMenuBarItem (juce::Graphics&, int w, int h, int index, const juce::String& text, bool over, bool open,
                          bool isMouseOverBar, juce::MenuBarComponent&) override;
    int getMenuBarItemWidth (juce::MenuBarComponent&, int index, const juce::String& text) override;
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&) override { return fonts::ui (14.0f); }
};

/** Draw a small vector icon inside `area`. */
/** Menu-bar title drawn as a heart (the feedback / donate menu). Drawn as a path: fonts may lack U+2665. */
inline const juce::String kHeartMenuName = juce::String::fromUTF8 ("\xe2\x99\xa5");

enum class Icon { Play, Stop, Pause, Rewind, Loop, Undo, Redo, Detach, Dock, Plus, Close, Up, Down, Power, Folder, Wave, Speaker };
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour);

/** A flat button that draws an Icon; optional toggle state lights it in the accent colour. */
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, Icon icon, const juce::String& tooltip = {});
    void setIcon (Icon i) { icon = i; repaint(); }
    void setOnColour (juce::Colour c) { onColour = c; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    juce::Colour onColour = colours::accent;
};

/** Stereo peak meter with decay and peak hold; values are linear gain. */
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setLevels (float left, float right);   // call ~30 Hz
    void setClipped (bool c) { if (clipped != c) { clipped = c; repaint(); } }
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    bool horizontal = false;

private:
    float level[2] {}, hold[2] {};
    int holdFrames[2] {};
    bool clipped = false;
};
} // namespace mcpdaw::ui
