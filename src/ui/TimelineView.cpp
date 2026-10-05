// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "TimelineView.h"
#include "core/AudioAsset.h"
#include <limits>

namespace mcpdaw::ui
{
namespace
{
/** Small pencil cursor for "paint" in empty lanes (distinct from the Ctrl-select crosshair). */
const juce::MouseCursor& pencilCursor()
{
    static const juce::MouseCursor cursor = []
    {
        const int size = 24;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Graphics g (img);
        juce::Path body;                                  // drawn pointing up-right, tip at bottom-left
        body.startNewSubPath (3.0f, 21.0f);
        body.lineTo (6.0f, 14.0f);
        body.lineTo (17.0f, 3.0f);
        body.lineTo (21.0f, 7.0f);
        body.lineTo (10.0f, 18.0f);
        body.closeSubPath();
        g.setColour (juce::Colours::black);
        g.strokePath (body, juce::PathStrokeType (3.0f, juce::PathStrokeType::mitered));
        g.setColour (juce::Colours::white);
        g.fillPath (body);
        g.setColour (juce::Colour (0xff3fd0c9));            // teal tip
        juce::Path tip;
        tip.addTriangle (3.0f, 21.0f, 6.0f, 14.0f, 10.0f, 18.0f);
        g.fillPath (tip);
        return juce::MouseCursor (img, 3, 21);
    }();
    return cursor;
}

/** Fade cursor: a ramp, shown over an event's fade handles. */
const juce::MouseCursor& fadeCursor()
{
    static const juce::MouseCursor cursor = []
    {
        const int size = 24;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Graphics g (img);
        juce::Path ramp;                                  // rising fade shape with a baseline
        ramp.startNewSubPath (3.0f, 19.0f);
        ramp.quadraticTo (9.0f, 18.0f, 13.0f, 6.0f);
        ramp.lineTo (21.0f, 6.0f);
        juce::Path base;
        base.startNewSubPath (3.0f, 19.0f);
        base.lineTo (21.0f, 19.0f);
        g.setColour (juce::Colours::black);
        g.strokePath (ramp, juce::PathStrokeType (4.0f));
        g.strokePath (base, juce::PathStrokeType (3.0f));
        g.setColour (juce::Colours::white);
        g.strokePath (ramp, juce::PathStrokeType (2.0f));
        g.setColour (juce::Colour (0xff3fd0c9));
        g.strokePath (base, juce::PathStrokeType (1.2f));
        return juce::MouseCursor (img, 12, 12);
    }();
    return cursor;
}

/** Slip cursor: a waveform between left/right arrows (Alt over an event body). */
const juce::MouseCursor& slipCursor()
{
    static const juce::MouseCursor cursor = []
    {
        const int size = 24;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Graphics g (img);
        juce::Path arrows;
        arrows.addArrow ({ 12.0f, 12.0f, 1.0f, 12.0f }, 1.5f, 7.0f, 4.0f);
        arrows.addArrow ({ 12.0f, 12.0f, 23.0f, 12.0f }, 1.5f, 7.0f, 4.0f);
        juce::Path wave;
        wave.startNewSubPath (6.0f, 12.0f);
        for (int i = 0; i <= 12; ++i)
            wave.lineTo (6.0f + i, 12.0f + 5.0f * std::sin (i * 0.9f) * (i % 2 ? 1.0f : 0.4f));
        g.setColour (juce::Colours::black);
        g.strokePath (arrows, juce::PathStrokeType (2.5f));
        g.strokePath (wave, juce::PathStrokeType (3.0f));
        g.setColour (juce::Colours::white);
        g.fillPath (arrows);
        g.setColour (juce::Colour (0xff3fd0c9));
        g.strokePath (wave, juce::PathStrokeType (1.4f));
        return juce::MouseCursor (img, 12, 12);
    }();
    return cursor;
}

/** "Replace audio" menu entry that auditions its file after the pointer rests on it for 250 ms. */
class AudioChoiceItem final : public juce::PopupMenu::CustomComponent, private juce::Timer
{
public:
    AudioChoiceItem (AppContext& a, juce::File f, bool isCurrent) : app (a), file (std::move (f)), current (isCurrent) {}
    ~AudioChoiceItem() override { stopTimer(); }

    void getIdealSize (int& w, int& h) override
    {
        w = juce::jlimit (220, 520, juce::GlyphArrangement::getStringWidthInt (fonts::ui (14.0f), file.getFileName()) + 60);
        h = 24;
    }

    void paint (juce::Graphics& g) override
    {
        const bool highlighted = isItemHighlighted();
        if (highlighted != wasHighlighted)                          // highlight changes repaint the item
        {
            wasHighlighted = highlighted;
            if (highlighted) startTimer (250); else stopTimer();
        }
        auto r = getLocalBounds();
        if (highlighted) { g.setColour (colours::accentDim); g.fillRect (r.reduced (2, 1)); }
        g.setColour (current ? colours::accent : colours::text);
        g.setFont (fonts::ui (14.0f, current));
        if (current) drawIcon (g, Icon::Wave, r.removeFromLeft (26).toFloat().reduced (8.0f), colours::accent);
        else r.removeFromLeft (26);
        g.drawText (file.getFileName(), r.reduced (4, 0), juce::Justification::centredLeft, true);
    }

private:
    AppContext& app;
    juce::File file;
    bool current, wasHighlighted = false;

    void timerCallback() override
    {
        stopTimer();
        if (isItemHighlighted()) app.previewFile (file);
    }
};
} // namespace

namespace
{
constexpr float kEdgeGrab = 7.0f;          // px from an event edge that grabs a trim handle
constexpr int kTitleStrip = 15;            // event title strip height
constexpr double kMinPpb = 1.5, kMaxPpb = 800.0;

// Header gain slider: 0..1 <-> dB, with the far left = silence and 0 dB at 5/6 of the travel.
double gainDbForProportion (double p) { return p <= 0.005 ? kMinDb : -60.0 + 72.0 * std::clamp (p, 0.0, 1.0); }
double proportionForGainDb (double db) { return db <= kMinDb ? 0.0 : std::clamp ((db + 60.0) / 72.0, 0.0, 1.0); }

juce::String dbText (double db) { return db <= kMinDb ? juce::String ("-inf") : (db > 0 ? "+" : "") + juce::String (db, 1); }

juce::var beatsVar (Tick t) { return ticksToBeats (t); }

constexpr float kHandleSize = 7.0f;        // fade handle square
constexpr float kMinEditWidth = 28.0f;     // narrower events are all body (move only; zoom in to edit)

/** Fade length as text: "0.50 beats (250 ms)", or the declick note when there is no musical fade. */
juce::String fadeText (Tick ticks, double bpm, double declickMs)
{
    if (ticks <= 0) return "none (declick " + juce::String (declickMs, 1) + " ms)";
    return juce::String (ticksToBeats (ticks), 2) + " beats (" + juce::String (ticksToSeconds (ticks, bpm) * 1000.0, 0) + " ms)";
}
} // namespace

TimelineView::TimelineView (AppContext& a) : app (a)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);
    hScroll.addListener (this);
    vScroll.addListener (this);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (false);
    app.controller.addListener (this);
    app.selectionChanged.addChangeListener (this);
    app.preferencesChanged.addChangeListener (this);
    startTimerHz (30);
}

TimelineView::~TimelineView()
{
    app.controller.removeListener (this);
    app.selectionChanged.removeChangeListener (this);
    app.preferencesChanged.removeChangeListener (this);
}

// ================================================================================================
// Geometry
// ================================================================================================
float TimelineView::xForTick (Tick t) const noexcept
{
    return static_cast<float> (kHeaderWidth + (ticksToBeats (t) - scrollBeat) * pixelsPerBeat);
}

Tick TimelineView::tickForX (float x) const noexcept
{
    return std::max<Tick> (0, beatsToTicks (scrollBeat + (x - kHeaderWidth) / pixelsPerBeat));
}

int TimelineView::rowAt (int y) const noexcept
{
    if (y < kRulerHeight) return -1;
    const int row = (y - kRulerHeight + scrollY) / trackHeight;
    return row;
}

juce::Rectangle<float> TimelineView::eventRect (const Event& e, int row) const noexcept
{
    const float x1 = xForTick (e.start), x2 = xForTick (e.end());
    return { x1, static_cast<float> (rowTop (row) + 3), std::max (2.0f, x2 - x1), static_cast<float> (trackHeight - 6) };
}

juce::Rectangle<float> TimelineView::waveArea (juce::Rectangle<float> r) noexcept
{
    return r.withTrimmedTop (static_cast<float> (kTitleStrip)).reduced (0, 2);
}

float TimelineView::gainLineY (const Event& e, juce::Rectangle<float> wave) noexcept
{
    // Amplitude mapping: the top of the waveform area is unity (0 dB and above), half way down is
    // -6 dB, the bottom is silence.
    const auto lin = static_cast<float> (std::min (1.0, dbToGain (e.gainDb)));
    return wave.getY() + (1.0f - lin) * wave.getHeight();
}

juce::Rectangle<float> TimelineView::fadeHandle (const Event& e, int row, bool fadeIn) const noexcept
{
    const auto r = eventRect (e, row);
    const float y = gainLineY (e, waveArea (r));
    const float x = fadeIn ? xForTick (e.start + e.fadeInTicks) : xForTick (e.end() - e.fadeOutTicks);
    // The square sits inside the event, at the end of the fade, on the gain line.
    const float left = fadeIn ? std::min (x, r.getRight() - kHandleSize) : std::max (r.getX(), x - kHandleSize);
    return { left, y, kHandleSize, kHandleSize };
}

bool TimelineView::showsEditHandles (const Event& e, int row) const noexcept
{
    return eventRect (e, row).getWidth() >= kMinEditWidth;
}

float TimelineView::drawnEnvelope (const Event& e, double tick) noexcept
{
    const auto lin = static_cast<float> (std::min (1.0, dbToGain (e.gainDb)));
    const auto since = static_cast<std::int64_t> (std::floor (tick - static_cast<double> (e.start)));
    const auto until = static_cast<std::int64_t> (std::floor (static_cast<double> (e.end()) - tick));
    return lin * fadeInGain (e.fadeInCurve, since, e.fadeInTicks) * fadeOutGain (e.fadeOutCurve, until, e.fadeOutTicks);
}

Tick TimelineView::gridStep() const
{
    const auto g = gridTicks (app.snap);
    return g > 0 ? g : kTicksPerBeat / 4;
}

TimelineView::HeaderLayout TimelineView::headerLayout (int row) const
{
    const int top = rowTop (row);
    const int w = kHeaderWidth;
    HeaderLayout l;
    l.name = { 14, top + 6, w - 14 - 44, 18 };
    l.fx = { w - 42, top + 6, 32, 18 };
    l.mute = { 14, top + 30, 24, 20 };
    l.solo = { 42, top + 30, 24, 20 };
    l.gain = { 74, top + 30, w - 74 - 10, 20 };
    l.meter = { 14, top + 57, w - 24, 7 };
    return l;
}

TimelineView::HitResult TimelineView::hitTest (juce::Point<float> p) const
{
    HitResult r;
    if (p.x >= laneRight() || p.y >= lanesBottom()) return r;
    if (p.y < kRulerHeight)
    {
        if (p.x >= kHeaderWidth) r.kind = p.y < kLoopStripHeight ? Hit::LoopStrip : Hit::Ruler;
        return r;
    }
    const auto& tracks = app.project().tracks;
    r.row = rowAt (static_cast<int> (p.y));
    if (r.row < 0 || r.row >= static_cast<int> (tracks.size()))
    {
        r.kind = p.x >= kHeaderWidth ? Hit::EmptyLane : Hit::Nothing;
        r.row = -1;
        return r;
    }
    if (p.x < kHeaderWidth) { r.kind = Hit::Header; return r; }

    const auto& events = tracks[static_cast<size_t> (r.row)].events;
    for (auto it = events.rbegin(); it != events.rend(); ++it)
    {
        const auto rect = eventRect (*it, r.row);
        if (! rect.contains (p)) continue;
        r.event = &*it;
        // Priority: fade corner, gain line, trim edge, body. Narrow clips (one-shot hits at normal zoom)
        // are all body so they can be grabbed and moved; zoom in to edit them.
        if (rect.getWidth() >= kMinEditWidth)
        {
            if (fadeHandle (*it, r.row, true).expanded (5.0f, 5.0f).contains (p)) { r.kind = Hit::EventFadeIn; return r; }
            if (fadeHandle (*it, r.row, false).expanded (5.0f, 5.0f).contains (p)) { r.kind = Hit::EventFadeOut; return r; }
            const float gy = gainLineY (*it, waveArea (rect));
            if (std::abs (p.y - gy) <= 4.0f && p.x > rect.getX() + kEdgeGrab && p.x < rect.getRight() - kEdgeGrab)
            {
                r.kind = Hit::EventGain;
                return r;
            }
        }
        const float grab = rect.getWidth() < 20.0f ? 0.0f : std::min (kEdgeGrab, rect.getWidth() / 4.0f);
        if (grab <= 0.0f) r.kind = Hit::EventBody;
        else if (p.x >= rect.getRight() - grab) r.kind = Hit::EventEnd;
        else if (p.x <= rect.getX() + grab) r.kind = Hit::EventStart;
        else r.kind = Hit::EventBody;
        return r;
    }
    r.kind = Hit::EmptyLane;
    return r;
}

juce::String TimelineView::trackAssetFor (const Track& t, Tick near) const
{
    // The asset of the nearest event to the left (ACID: a track "is" its loop); else any event's.
    const Event* best = nullptr;
    for (const auto& e : t.events)
        if (e.start <= near && (best == nullptr || e.start > best->start)) best = &e;
    if (best == nullptr && ! t.events.empty()) best = &t.events.front();
    return best != nullptr ? best->assetId : juce::String();
}

juce::StringArray TimelineView::selectedIdsSorted() const
{
    std::vector<std::pair<Tick, juce::String>> items;
    for (const auto& id : app.selectedEvents)
        if (const auto ref = app.project().findEvent (id); ref.event != nullptr) items.emplace_back (ref.event->start, id);
    std::sort (items.begin(), items.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    juce::StringArray out;
    for (const auto& [t, id] : items) out.add (id);
    return out;
}

// ================================================================================================
// Model / timer notifications
// ================================================================================================
void TimelineView::projectChanged (const Project& p, bool)
{
    // Drop selection entries that no longer exist (deleted, undone...).
    juce::StringArray valid;
    for (const auto& id : app.selectedEvents)
        if (p.findEvent (id).event != nullptr) valid.add (id);
    if (valid.size() != app.selectedEvents.size()) app.selectedEvents = valid;
    if (app.selectedTrack.isNotEmpty() && p.findTrack (app.selectedTrack) == nullptr)
        app.selectedTrack = p.tracks.empty() ? juce::String() : p.tracks.front().id;
    updateScrollbars();
    // A freshly opened/new project (GUI or MCP) gets a zoom that shows the whole arrangement.
    if (app.controller.getLoadCount() != seenLoadCount && getWidth() > kHeaderWidth)
    {
        seenLoadCount = app.controller.getLoadCount();
        zoomToFit();
    }
    repaint();
}

void TimelineView::changeListenerCallback (juce::ChangeBroadcaster*) { repaint(); }

void TimelineView::timerCallback()
{
    const double beat = app.engine.positionBeats();
    const float x = xForTick (beatsToTicks (beat));

    if (followPlayhead && app.engine.isPlaying() && drag.kind == DragKind::None)
    {
        const float right = static_cast<float> (laneRight()) - 30.0f;
        if (x > right || x < kHeaderWidth)
        {
            // Page-style follow keeps the view stable between jumps (easier to read than smooth scrolling).
            scrollBeat = std::max (0.0, beat - 2.0);
            updateScrollbars();
            repaint();
            lastPlayheadX = xForTick (beatsToTicks (beat));
            return;
        }
    }
    if (std::abs (x - lastPlayheadX) >= 0.5f)
    {
        repaint (juce::Rectangle<float> (lastPlayheadX - 7, 0, 14, static_cast<float> (lanesBottom())).getSmallestIntegerContainer());
        repaint (juce::Rectangle<float> (x - 7, 0, 14, static_cast<float> (lanesBottom())).getSmallestIntegerContainer());
        lastPlayheadX = x;
    }
    // Header meters.
    const auto& tracks = app.project().tracks;
    for (int i = 0; i < static_cast<int> (tracks.size()); ++i)
    {
        const int top = rowTop (i);
        if (top + trackHeight < kRulerHeight || top > lanesBottom()) continue;
        repaint (headerLayout (i).meter.expanded (1));
    }
}

void TimelineView::updateScrollbars()
{
    const double visibleBeats = std::max (1.0, (laneRight() - kHeaderWidth) / pixelsPerBeat);
    const double contentBeats = ticksToBeats (app.project().contentEnd());
    const double total = std::max ({ contentBeats + visibleBeats * 0.5, visibleBeats + scrollBeat, 64.0 });
    hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScroll.setCurrentRange (scrollBeat, visibleBeats, juce::dontSendNotification);

    const int visibleHeight = std::max (1, lanesBottom() - kRulerHeight);
    const int contentHeight = static_cast<int> (app.project().tracks.size() + 1) * trackHeight;
    scrollY = juce::jlimit (0, std::max (0, contentHeight - visibleHeight), scrollY);
    vScroll.setRangeLimits (0.0, std::max (contentHeight, visibleHeight), juce::dontSendNotification);
    vScroll.setCurrentRange (scrollY, visibleHeight, juce::dontSendNotification);
}

void TimelineView::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hScroll) scrollBeat = std::max (0.0, start);
    else scrollY = static_cast<int> (start);
    repaint();
}

void TimelineView::resized()
{
    hScroll.setBounds (kHeaderWidth, getHeight() - kScrollbarSize, getWidth() - kHeaderWidth - kScrollbarSize, kScrollbarSize);
    vScroll.setBounds (getWidth() - kScrollbarSize, kRulerHeight, kScrollbarSize, getHeight() - kRulerHeight - kScrollbarSize);
    updateScrollbars();
}

void TimelineView::zoomBy (double factor, float anchorX)
{
    if (anchorX < kHeaderWidth) anchorX = kHeaderWidth + (laneRight() - kHeaderWidth) * 0.5f;
    const double anchorBeat = scrollBeat + (anchorX - kHeaderWidth) / pixelsPerBeat;
    pixelsPerBeat = std::clamp (pixelsPerBeat * factor, kMinPpb, kMaxPpb);
    scrollBeat = std::max (0.0, anchorBeat - (anchorX - kHeaderWidth) / pixelsPerBeat);
    updateScrollbars();
    repaint();
}

void TimelineView::zoomToFit()
{
    const double beats = std::max (16.0, ticksToBeats (app.project().contentEnd()) + 2.0);
    pixelsPerBeat = std::clamp ((laneRight() - kHeaderWidth - 10) / beats, kMinPpb, kMaxPpb);
    scrollBeat = 0;
    updateScrollbars();
    repaint();
}

void TimelineView::fitAll()
{
    const int tracks = std::max (1, static_cast<int> (app.project().tracks.size()));
    trackHeight = juce::jlimit (64, 110, (lanesBottom() - kRulerHeight) / tracks);   // 64 keeps the header meter inside the row
    scrollY = 0;
    zoomToFit();
}

void TimelineView::fitRange (double startBeat, double endBeat)
{
    if (endBeat <= startBeat) { fitAll(); return; }
    const int tracks = std::max (1, static_cast<int> (app.project().tracks.size()));
    trackHeight = juce::jlimit (64, 110, (lanesBottom() - kRulerHeight) / tracks);
    scrollY = 0;
    pixelsPerBeat = std::clamp ((laneRight() - kHeaderWidth - 10) / (endBeat - startBeat), kMinPpb, kMaxPpb);
    scrollBeat = std::max (0.0, startBeat);
    updateScrollbars();
    repaint();
}

// ================================================================================================
// Painting
// ================================================================================================
void TimelineView::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    paintLanes (g);
    paintHeaders (g);
    paintRuler (g);

    // Corner above the headers: zoom + snap readout.
    auto corner = juce::Rectangle<int> (0, 0, kHeaderWidth, kRulerHeight);
    g.setColour (colours::raised);
    g.fillRect (corner);
    g.setColour (colours::textFaint);
    g.setFont (fonts::display (11.0f));
    g.drawText ("TRACKS", corner.reduced (14, 0).removeFromTop (kRulerHeight / 2 + 4), juce::Justification::bottomLeft);
    g.setFont (fonts::ui (11.0f));
    g.drawText (juce::String (app.project().tracks.size()) + " tracks  |  " + juce::String (app.project().bpm, 2) + " BPM",
                corner.reduced (14, 0).removeFromBottom (kRulerHeight / 2 - 2), juce::Justification::topLeft);
    g.setColour (colours::line);
    g.drawVerticalLine (kHeaderWidth - 1, 0.0f, static_cast<float> (lanesBottom()));
    g.drawHorizontalLine (kRulerHeight - 1, 0.0f, static_cast<float> (laneRight()));

    if (drag.kind == DragKind::Marquee && drag.moved)
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (kHeaderWidth, kRulerHeight, laneRight() - kHeaderWidth, lanesBottom() - kRulerHeight);
        const auto band = marqueeRect();
        g.setColour (colours::accent.withAlpha (0.12f));
        g.fillRect (band);
        g.setColour (colours::accent.withAlpha (0.8f));
        g.drawRect (band, 1.0f);
    }

    // Bottom-left corner under the headers.
    g.setColour (colours::window);
    g.fillRect (0, lanesBottom(), kHeaderWidth, kScrollbarSize);
    g.fillRect (laneRight(), 0, kScrollbarSize, kRulerHeight);
}

void TimelineView::paintRuler (juce::Graphics& g)
{
    const auto area = juce::Rectangle<int> (kHeaderWidth, 0, laneRight() - kHeaderWidth, kRulerHeight);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    g.setColour (colours::raised);
    g.fillRect (area);

    const auto& p = app.project();
    // Loop strip
    Tick loopA = p.loopStart, loopB = p.loopEnd;
    bool loopPreview = false;
    if (drag.kind == DragKind::LoopCreate || drag.kind == DragKind::LoopMove || drag.kind == DragKind::LoopStart || drag.kind == DragKind::LoopEnd)
    {
        loopA = drag.a; loopB = drag.b; loopPreview = true;
    }
    g.setColour (colours::window.withAlpha (0.5f));
    g.fillRect (area.withHeight (kLoopStripHeight));
    const float lx1 = xForTick (loopA), lx2 = xForTick (loopB);
    const auto loopColour = (p.loopEnabled || loopPreview) ? colours::accent : colours::textFaint;
    g.setColour (loopColour.withAlpha (0.75f));
    g.fillRect (juce::Rectangle<float> (lx1, 3.0f, lx2 - lx1, kLoopStripHeight - 6.0f));
    g.setColour (loopColour);
    g.fillRect (juce::Rectangle<float> (lx1, 1.0f, 2.0f, kLoopStripHeight - 2.0f));
    g.fillRect (juce::Rectangle<float> (lx2 - 2.0f, 1.0f, 2.0f, kLoopStripHeight - 2.0f));

    // Bar/beat ticks with density adapted to zoom.
    const double firstBeat = std::floor (scrollBeat);
    const double lastBeat = scrollBeat + (laneRight() - kHeaderWidth) / pixelsPerBeat + 1;
    int barLabelEvery = 1;
    const double barPx = pixelsPerBeat * kBeatsPerBar;
    while (barPx * barLabelEvery < 56) barLabelEvery *= 2;
    g.setFont (fonts::ui (12.0f, true));
    for (double b = firstBeat; b <= lastBeat; b += 1.0)
    {
        const int beat = static_cast<int> (b);
        const float x = static_cast<float> (kHeaderWidth + (b - scrollBeat) * pixelsPerBeat);
        const bool isBar = beat % kBeatsPerBar == 0;
        if (isBar)
        {
            const int bar = beat / kBeatsPerBar;
            const bool labelled = bar % barLabelEvery == 0;
            g.setColour (labelled ? colours::textDim : colours::line);
            g.drawVerticalLine (static_cast<int> (x), labelled ? kLoopStripHeight + 2.0f : kRulerHeight - 12.0f, static_cast<float> (kRulerHeight));
            if (labelled)
            {
                g.setColour (colours::text);
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4, kLoopStripHeight + 2.0f, 60, 16), juce::Justification::centredLeft);
            }
        }
        else if (pixelsPerBeat >= 10)
        {
            g.setColour (colours::line);
            g.drawVerticalLine (static_cast<int> (x), kRulerHeight - 8.0f, static_cast<float> (kRulerHeight));
            if (pixelsPerBeat >= 60)
            {
                g.setColour (colours::textFaint);
                g.setFont (fonts::ui (10.0f));
                g.drawText (juce::String (beat / kBeatsPerBar + 1) + "." + juce::String (beat % kBeatsPerBar + 1),
                            juce::Rectangle<float> (x + 3, kRulerHeight - 20.0f, 40, 12), juce::Justification::centredLeft);
                g.setFont (fonts::ui (12.0f, true));
            }
        }
    }

    // Playhead marker
    const float px = xForTick (beatsToTicks (app.engine.positionBeats()));
    juce::Path tri;
    tri.addTriangle (px - 6, kRulerHeight - 10.0f, px + 6, kRulerHeight - 10.0f, px, static_cast<float> (kRulerHeight));
    g.setColour (app.engine.isPlaying() ? colours::accent : colours::playhead);
    g.fillPath (tri);
}

void TimelineView::paintHeaders (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (0, kRulerHeight, kHeaderWidth, lanesBottom() - kRulerHeight);
    const auto& p = app.project();
    const bool anySolo = p.anySolo();

    for (int i = 0; i < static_cast<int> (p.tracks.size()); ++i)
    {
        const int top = rowTop (i);
        if (top + trackHeight < kRulerHeight || top > lanesBottom()) continue;
        const auto& t = p.tracks[static_cast<size_t> (i)];
        const bool selected = t.id == app.selectedTrack;
        const auto row = juce::Rectangle<int> (0, top, kHeaderWidth, trackHeight);

        g.setColour (selected ? colours::controlHi.withAlpha (0.55f) : colours::raised);
        g.fillRect (row.withTrimmedBottom (1));
        g.setColour (colours::track (t.colour).withAlpha (anySolo && ! t.solo ? 0.35f : 1.0f));
        g.fillRect (row.withWidth (5).withTrimmedBottom (1));
        g.setColour (colours::lineSoft);
        g.drawHorizontalLine (top + trackHeight - 1, 0.0f, static_cast<float> (kHeaderWidth));

        const auto l = headerLayout (i);
        if (renameTrackId != t.id)
        {
            g.setColour (t.mute ? colours::textFaint : colours::text);
            g.setFont (fonts::ui (13.5f, true));
            g.drawText (juce::String (i + 1) + "  " + t.name, l.name, juce::Justification::centredLeft, true);
        }

        auto pill = [&] (juce::Rectangle<int> r, const juce::String& text, bool on, juce::Colour onColour)
        {
            g.setColour (on ? onColour : colours::control);
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (on ? colours::window : colours::textDim);
            g.setFont (fonts::ui (11.5f, true));
            g.drawText (text, r, juce::Justification::centred);
        };
        pill (l.mute, "M", t.mute, colours::amber);
        pill (l.solo, "S", t.solo, colours::accent);
        pill (l.fx, "FX", ! t.effects.empty(), colours::accentDim.brighter (0.2f));

        // Gain mini-fader
        const auto gr = l.gain.toFloat();
        const float prop = static_cast<float> (proportionForGainDb (t.gainDb));
        g.setColour (colours::window);
        g.fillRoundedRectangle (gr.withSizeKeepingCentre (gr.getWidth(), 4.0f), 2.0f);
        g.setColour (colours::track (t.colour).withAlpha (0.8f));
        g.fillRoundedRectangle (gr.withSizeKeepingCentre (gr.getWidth(), 4.0f).withWidth (gr.getWidth() * prop), 2.0f);
        const float zero = gr.getX() + gr.getWidth() * static_cast<float> (proportionForGainDb (0.0));
        g.setColour (colours::textFaint);
        g.drawVerticalLine (static_cast<int> (zero), gr.getCentreY() - 5, gr.getCentreY() + 5);
        g.setColour (colours::text);
        g.fillEllipse (gr.getX() + gr.getWidth() * prop - 5, gr.getCentreY() - 5, 10, 10);
        if (drag.kind == DragKind::HeaderGain && drag.trackId == t.id)
        {
            g.setColour (colours::text);
            g.setFont (fonts::ui (11.0f));
            g.drawText (dbText (t.gainDb) + " dB", l.name, juce::Justification::centredRight);
        }

        // Meter (peak, horizontal)
        const auto m = app.meterFor (t.id);
        const auto mr = l.meter.toFloat();
        g.setColour (colours::window);
        g.fillRoundedRectangle (mr, 2.0f);
        auto meterProp = [] (float lin) { return lin <= 0.001f ? 0.0f : juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (lin) + 60.0f) / 66.0f); };
        const float ml = meterProp (m.left), mrp = meterProp (m.right);
        g.setColour (std::max (m.left, m.right) > 1.0f ? colours::red : colours::accent);
        g.fillRect (mr.withHeight (mr.getHeight() / 2 - 0.5f).withWidth (mr.getWidth() * ml));
        g.fillRect (mr.withTrimmedTop (mr.getHeight() / 2 + 0.5f).withWidth (mr.getWidth() * mrp));
    }
}

void TimelineView::paintLanes (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    const auto lanes = juce::Rectangle<int> (kHeaderWidth, kRulerHeight, laneRight() - kHeaderWidth, lanesBottom() - kRulerHeight);
    g.reduceClipRegion (lanes);
    const auto clip = g.getClipBounds();
    const auto& p = app.project();

    // Row backgrounds
    g.setColour (colours::panel.darker (0.12f));
    g.fillRect (lanes);
    for (int i = 0; i < static_cast<int> (p.tracks.size()); ++i)
    {
        const int top = rowTop (i);
        if (top + trackHeight < clip.getY() || top > clip.getBottom()) continue;
        const bool selected = p.tracks[static_cast<size_t> (i)].id == app.selectedTrack;
        g.setColour (selected ? colours::panel.brighter (0.05f) : (i % 2 ? colours::panel : colours::panel.darker (0.04f)));
        g.fillRect (kHeaderWidth, top, lanes.getWidth(), trackHeight - 1);
    }

    // Grid
    const double firstBeat = std::floor (scrollBeat);
    const double lastBeat = scrollBeat + lanes.getWidth() / pixelsPerBeat + 1;
    const bool showBeats = pixelsPerBeat >= 10, showSixteenths = pixelsPerBeat >= 70;
    for (double b = firstBeat; b <= lastBeat; b += showSixteenths ? 0.25 : 1.0)
    {
        const float x = static_cast<float> (kHeaderWidth + (b - scrollBeat) * pixelsPerBeat);
        if (x < clip.getX() - 1 || x > clip.getRight() + 1) continue;
        const double whole = std::floor (b);
        const bool onBeat = std::abs (b - whole) < 1e-9;
        const bool isBar = onBeat && static_cast<int> (whole) % kBeatsPerBar == 0;
        if (isBar) g.setColour (colours::line.withAlpha (0.9f));
        else if (onBeat && showBeats) g.setColour (colours::lineSoft);
        else if (! onBeat) g.setColour (colours::lineSoft.withAlpha (0.45f));
        else continue;
        g.drawVerticalLine (static_cast<int> (x), static_cast<float> (kRulerHeight), static_cast<float> (lanesBottom()));
    }

    // Loop region shading
    {
        Tick a = p.loopStart, b = p.loopEnd;
        bool show = p.loopEnabled;
        if (drag.kind == DragKind::LoopCreate || drag.kind == DragKind::LoopMove || drag.kind == DragKind::LoopStart || drag.kind == DragKind::LoopEnd)
        {
            a = drag.a; b = drag.b; show = true;
        }
        if (show)
        {
            g.setColour (colours::accent.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float> (xForTick (a), static_cast<float> (kRulerHeight), xForTick (b) - xForTick (a), static_cast<float> (lanes.getHeight())));
        }
    }

    // Events
    const bool movePreview = drag.kind == DragKind::Move && drag.moved;
    // In-place edits (trim, slip, gain, fades) replace the event with its preview while dragging.
    const bool editPreview = drag.moved && (drag.kind == DragKind::TrimStart || drag.kind == DragKind::TrimEnd || drag.kind == DragKind::Slip
                                            || drag.kind == DragKind::EventGain || drag.kind == DragKind::FadeIn || drag.kind == DragKind::FadeOut);
    for (int i = 0; i < static_cast<int> (p.tracks.size()); ++i)
    {
        const int top = rowTop (i);
        if (top + trackHeight < clip.getY() || top > clip.getBottom()) continue;
        for (const auto& e : p.tracks[static_cast<size_t> (i)].events)
        {
            const bool dragged = (movePreview && ! drag.copy) || editPreview
                                     ? std::any_of (drag.originals.begin(), drag.originals.end(), [&] (const Ghost& o) { return o.event.id == e.id; })
                                     : false;
            if (dragged) continue;
            const auto r = eventRect (e, i);
            if (r.getRight() < clip.getX() || r.getX() > clip.getRight()) continue;
            paintEvent (g, e, i, app.selectedEvents.contains (e.id), false);
        }
    }
    if (movePreview || editPreview)
        for (const auto& ghost : drag.previews) paintEvent (g, ghost.event, ghost.row, true, movePreview);
    if (editPreview) paintDragReadout (g);

    // Paint (draw-loop) preview
    if (drag.kind == DragKind::Paint && drag.moved && drag.b > drag.a)
    {
        Event e;
        e.assetId = drag.assetId;
        e.start = drag.a;
        e.length = drag.b - drag.a;
        paintEvent (g, e, drag.downRow, true, true);
    }

    // File-drop indicator
    if (dropRow >= -1 && dropTick >= 0)
    {
        const float x = xForTick (dropTick);
        const int top = dropRow >= 0 ? rowTop (dropRow) : rowTop (static_cast<int> (p.tracks.size()));
        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (x, static_cast<float> (top + 3), 120.0f, static_cast<float> (trackHeight - 6)));
        g.setColour (colours::accent);
        g.fillRect (juce::Rectangle<float> (x - 1, static_cast<float> (top), 2.0f, static_cast<float> (trackHeight)));
        g.setFont (fonts::ui (12.0f, true));
        g.drawText (dropRow >= 0 ? "Drop onto track" : "Drop: new track", juce::Rectangle<float> (x + 6, static_cast<float> (top + 6), 160, 16),
                    juce::Justification::centredLeft);
    }

    // Empty-project hint
    if (p.tracks.empty())
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::display (18.0f));
        g.drawText ("DROP LOOPS HERE", lanes.withTrimmedTop (lanes.getHeight() / 3).withHeight (30), juce::Justification::centred);
        g.setFont (fonts::ui (13.0f));
        g.drawText ("Drag WAV files from the browser or Explorer. Drag an event's right edge to repeat it; drag in an empty lane to paint the loop.",
                    lanes.withTrimmedTop (lanes.getHeight() / 3 + 36).withHeight (20), juce::Justification::centred);
    }

    // Playhead
    const float px = xForTick (beatsToTicks (app.engine.positionBeats()));
    g.setColour ((app.engine.isPlaying() ? colours::accent : colours::playhead).withAlpha (0.9f));
    g.fillRect (juce::Rectangle<float> (px - 0.75f, static_cast<float> (kRulerHeight), 1.5f, static_cast<float> (lanes.getHeight())));
}

void TimelineView::paintEvent (juce::Graphics& g, const Event& e, int row, bool selected, bool ghost)
{
    const auto& p = app.project();
    const auto r = eventRect (e, row);
    const int colourIndex = row >= 0 && row < static_cast<int> (p.tracks.size()) ? p.tracks[static_cast<size_t> (row)].colour : 0;
    const auto base = colours::track (colourIndex);
    const auto* asset = p.findAsset (e.assetId);

    const float alpha = ghost ? 0.75f : 1.0f;
    g.setColour (base.withMultipliedBrightness (selected ? 0.55f : 0.4f).withMultipliedSaturation (0.8f).withAlpha (alpha));
    g.fillRoundedRectangle (r, 3.0f);

    auto title = r.withHeight (static_cast<float> (kTitleStrip));
    g.setColour (base.withMultipliedBrightness (selected ? 0.95f : 0.75f).withAlpha (alpha));
    g.fillRoundedRectangle (title, 3.0f);
    g.fillRect (title.withTrimmedTop (4.0f));

    // Waveform (drawn reversed for reversed events, scaled by event gain and musical fades)
    auto body = waveArea (r);
    if (asset != nullptr && asset->data != nullptr && body.getHeight() > 6)
        paintWaveform (g, e, *asset, body, base.brighter (0.9f).withAlpha (0.9f * alpha));
    if (body.getHeight() > 6 && ! ghost) paintEdgeOverlays (g, e, row, body, e.id.isNotEmpty() && (selected || e.id == hoverEventId));

    // Loop cycle notches: where the source wraps inside the event.
    if (e.looping && asset != nullptr)
    {
        const Tick cycle = sourceCycleTicks (p, e);
        if (cycle > 0 && cycle * pixelsPerBeat / kTicksPerBeat > 6)
        {
            const Tick phase = beatsToTicks (secondsToBeats (e.sourceOffsetSeconds / e.rate(), p.bpm)) % cycle;
            // Forwards the source wraps (cycle - phase) after the start; reversed it wraps (cycle - phase)
            // before the end, walking backwards.
            const Tick first = e.reversed ? e.end() - (cycle - phase) - ((e.length - (cycle - phase)) / cycle) * cycle
                                          : e.start + (cycle - phase);
            for (Tick t = first; t < e.end(); t += cycle)
            {
                if (t <= e.start) continue;
                const float x = xForTick (t);
                juce::Path notch;
                notch.addTriangle (x - 4, r.getY() + kTitleStrip, x + 4, r.getY() + kTitleStrip, x, r.getY() + kTitleStrip + 5);
                g.setColour (colours::window.withAlpha (0.8f));
                g.fillPath (notch);
                g.setColour (colours::window.withAlpha (0.35f));
                g.drawVerticalLine (static_cast<int> (x), r.getY() + kTitleStrip, r.getBottom());
            }
        }
    }

    // Title text
    if (r.getWidth() > 24)
    {
        g.setColour (colours::window);
        g.setFont (fonts::ui (11.5f, true));
        const auto name = asset != nullptr ? asset->displayName() : juce::String ("?");
        juce::String label = name;
        if (e.reversed) label = juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x80 ")) + label + "  (reversed)";   // "◀ name"
        if (e.pitchSemitones != 0.0)   // e.g. "bass_A1  +7 st"
            label << "  " << (e.pitchSemitones > 0 ? "+" : "") << juce::String (e.pitchSemitones, 0) << " st";
        if (e.gainDb != 0.0) label << "  " << dbText (e.gainDb) << " dB";
        if (! e.looping) label << "  (one-shot)";
        g.drawText (label, title.reduced (5, 0), juce::Justification::centredLeft, true);
    }

    g.setColour (selected ? colours::text : base.withMultipliedBrightness (0.9f).withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, selected ? 1.6f : 1.0f);
}

void TimelineView::paintWaveform (juce::Graphics& g, const Event& e, const Asset& asset, juce::Rectangle<float> body, juce::Colour colour)
{
    const auto& data = *asset.data;
    const double sr = data.sampleRate;
    const double frames = data.numFrames();
    const double bpm = app.project().bpm;
    const double secondsPerPixel = 60.0 / (bpm * pixelsPerBeat);
    const double framesPerPixel = secondsPerPixel * sr * e.rate();   // pitched events consume source faster/slower
    const auto clip = g.getClipBounds();
    const int x0 = std::max ({ static_cast<int> (body.getX()), kHeaderWidth, clip.getX() });
    const int x1 = std::min ({ static_cast<int> (std::ceil (body.getRight())), laneRight(), clip.getRight() });
    const float cy = body.getCentreY(), half = body.getHeight() * 0.48f;
    const double startX = xForTick (e.start);
    const double offsetFrames = e.sourceOffsetSeconds * sr;
    const double eventFrames = ticksToSeconds (e.length, bpm) * sr * e.rate();   // source frames the window spans
    const double ticksPerPixel = kTicksPerBeat / pixelsPerBeat;

    g.setColour (colour);
    for (int x = x0; x < x1; ++x)
    {
        // Same mapping as the renderer: forwards the pixel shows offset + t; reversed the window is
        // read back to front, so the pixel shows offset + (window - t) and the drawing is mirrored.
        const double along = (x - startX) * framesPerPixel;
        double f = e.reversed ? offsetFrames + eventFrames - along - framesPerPixel : offsetFrames + along;
        if (e.looping)
        {
            f = std::fmod (f, frames);
            if (f < 0) f += frames;
        }
        else if (f >= frames)
        {
            if (e.reversed) continue;   // reversed one-shot: silence until the window reaches the source
            break;
        }
        if (f < 0) { if (e.reversed) break; continue; }
        const auto mm = data.peaks.query (f, std::min (frames, f + std::max (1.0, framesPerPixel)));
        // Event gain and musical fades scale the drawing exactly as they scale the sound.
        const float env = drawnEnvelope (e, static_cast<double> (e.start) + (x + 0.5 - startX) * ticksPerPixel);
        const float top = cy - juce::jlimit (0.0f, 1.0f, mm.hi * env) * half;
        const float bottom = cy - juce::jlimit (-1.0f, 0.0f, mm.lo * env) * half;
        g.fillRect (static_cast<float> (x), top, 1.0f, std::max (1.0f, bottom - top));
    }
}

void TimelineView::paintEdgeOverlays (juce::Graphics& g, const Event& e, int row, juce::Rectangle<float> wave, bool handles)
{
    if (! showsEditHandles (e, row)) return;
    const float gy = gainLineY (e, wave);
    const float bottom = wave.getBottom();
    const auto line = colours::text;

    // Gain line: faint at 0 dB, clearer when the gain is down or the event is hovered/selected.
    if (handles || e.gainDb != 0.0)
    {
        g.setColour (line.withAlpha (handles ? 0.65f : 0.35f));
        g.drawHorizontalLine (juce::roundToInt (gy), xForTick (e.start + e.fadeInTicks), xForTick (e.end() - e.fadeOutTicks));
    }

    // Musical fades: shade what the fade removes and draw its curve from the silent corner to the gain line.
    auto fadePath = [&] (bool fadeIn)
    {
        const Tick len = fadeIn ? e.fadeInTicks : e.fadeOutTicks;
        if (len <= 0) return;
        const float xa = xForTick (fadeIn ? e.start : e.end() - len), xb = xForTick (fadeIn ? e.start + len : e.end());
        const int steps = juce::jlimit (2, 64, static_cast<int> ((xb - xa) / 3.0f));
        juce::Path curve, shade;
        for (int i = 0; i <= steps; ++i)
        {
            const float frac = static_cast<float> (i) / static_cast<float> (steps);
            const float x = xa + frac * (xb - xa);
            const float gfade = fadeIn ? fadeShape (e.fadeInCurve, frac) : 1.0f - fadeShape (e.fadeOutCurve, frac);
            const float y = bottom - gfade * (bottom - gy);
            if (i == 0) { curve.startNewSubPath (x, y); shade.startNewSubPath (x, y); }
            else { curve.lineTo (x, y); shade.lineTo (x, y); }
        }
        shade.lineTo (xb, wave.getY());   // close along the top: the shaded area is what the fade removes
        shade.lineTo (xa, wave.getY());
        shade.closeSubPath();
        g.setColour (colours::window.withAlpha (0.35f));
        g.fillPath (shade);
        g.setColour (line.withAlpha (0.85f));
        g.strokePath (curve, juce::PathStrokeType (1.4f));
    };
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (wave.getSmallestIntegerContainer());
        fadePath (true);
        fadePath (false);
    }

    if (! handles) return;
    for (bool in : { true, false })
    {
        const auto h = fadeHandle (e, row, in);
        g.setColour (colours::window.withAlpha (0.9f));
        g.fillRect (h.expanded (1.0f));
        g.setColour ((in ? e.fadeInTicks : e.fadeOutTicks) > 0 ? colours::accent : line.withAlpha (0.9f));
        g.fillRect (h);
    }
}

void TimelineView::paintDragReadout (juce::Graphics& g)
{
    if (drag.previews.empty()) return;
    const auto& p = app.project();
    const auto primary = std::find_if (drag.previews.begin(), drag.previews.end(), [&] (const Ghost& gh) { return gh.event.id == drag.eventId; });
    const auto& gh = primary != drag.previews.end() ? *primary : drag.previews.front();
    const auto r = eventRect (gh.event, gh.row);
    juce::String text;
    switch (drag.kind)
    {
        case DragKind::EventGain:
            text = "Event gain " + dbText (gh.event.gainDb) + " dB" + (drag.previews.size() > 1 ? "  (" + juce::String (drag.previews.size()) + " events)" : juce::String());
            break;
        case DragKind::FadeIn:  text = "Fade in " + fadeText (gh.event.fadeInTicks, p.bpm, app.controller.defaultDeclickMs()); break;
        case DragKind::FadeOut: text = "Fade out " + fadeText (gh.event.fadeOutTicks, p.bpm, app.controller.defaultDeclickMs()); break;
        case DragKind::Slip:
        {
            const double moved = ticksToBeats (drag.delta);
            text = "Slip " + juce::String (moved > 0 ? "+" : "") + juce::String (moved, 3) + " beats  (source offset "
                 + juce::String (gh.event.sourceOffsetSeconds, 3) + " s)" + (drag.slipClamped ? "  - start/end of the sample" : "");
            break;
        }
        case DragKind::TrimStart:
        case DragKind::TrimEnd:
            text = "Length " + juce::String (ticksToBeats (gh.event.length), 3) + " beats";
            break;
        default: return;
    }
    g.setFont (fonts::ui (12.0f, true));
    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 14.0f;
    auto box = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() - 22.0f, w, 18.0f);
    if (box.getY() < kRulerHeight) box.setY (r.getBottom() + 4.0f);
    g.setColour (colours::window.withAlpha (0.92f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (colours::accent);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    g.setColour (colours::text);
    g.drawText (text, box.reduced (7.0f, 0.0f), juce::Justification::centredLeft, false);
}

// ================================================================================================
// Mouse
// ================================================================================================
void TimelineView::mouseMove (const juce::MouseEvent& e)
{
    updateCursor (e.position, e.mods);
    // Track the hovered event so its fade/gain handles appear (kept out of the way otherwise).
    const auto hit = hitTest (e.position);
    const auto id = hit.event != nullptr ? hit.event->id : juce::String();
    if (id != hoverEventId)
    {
        auto repaintEvent = [this] (const juce::String& eventId)
        {
            const auto& p = app.project();
            for (int row = 0; row < static_cast<int> (p.tracks.size()); ++row)
                for (const auto& ev : p.tracks[static_cast<size_t> (row)].events)
                    if (ev.id == eventId) repaint (eventRect (ev, row).expanded (2.0f).getSmallestIntegerContainer());
        };
        repaintEvent (hoverEventId);
        hoverEventId = id;
        repaintEvent (hoverEventId);
    }
}

void TimelineView::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    // Pressing / releasing Ctrl while the pointer rests: swap pencil <-> selector cursor immediately.
    if (drag.kind == DragKind::None && isMouseOver (true))
        updateCursor (getMouseXYRelative().toFloat(), mods);
}

void TimelineView::updateCursor (juce::Point<float> position, juce::ModifierKeys mods)
{
    struct { juce::Point<float> position; juce::ModifierKeys mods; } e { position, mods };
    const auto hit = hitTest (e.position);
    switch (hit.kind)
    {
        case Hit::EventStart:
        case Hit::EventEnd:   setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Hit::EventFadeIn:
        case Hit::EventFadeOut: setMouseCursor (fadeCursor()); break;
        case Hit::EventGain:  setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
        case Hit::EventBody:
            if (e.mods.isAltDown()) setMouseCursor (slipCursor());                         // Alt+drag = slip
            else if (e.mods.isCtrlDown()) setMouseCursor (juce::MouseCursor::CopyingCursor); // Ctrl+drag = copy
            else setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            break;
        case Hit::EmptyLane:
            if (e.mods.isCtrlDown()) { setMouseCursor (juce::MouseCursor::CrosshairCursor); break; }
            // Pencil = paint the track's sample here; crosshair (Ctrl) = rubber-band select.
            if (hit.row >= 0 && trackAssetFor (app.project().tracks[static_cast<size_t> (hit.row)], tickForX (e.position.x)).isNotEmpty())
                setMouseCursor (pencilCursor());
            else setMouseCursor (juce::MouseCursor::NormalCursor);
            break;
        case Hit::LoopStrip:
        {
            const auto& p = app.project();
            const float a = xForTick (p.loopStart), b = xForTick (p.loopEnd);
            setMouseCursor (std::abs (e.position.x - a) < 6 || std::abs (e.position.x - b) < 6 ? juce::MouseCursor::LeftRightResizeCursor
                                                                                                : juce::MouseCursor::PointingHandCursor);
            break;
        }
        case Hit::Ruler:      setMouseCursor (juce::MouseCursor::IBeamCursor); break;
        case Hit::Header:
        {
            const auto l = headerLayout (hit.row);
            setMouseCursor (l.gain.contains (e.position.toInt()) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
            break;
        }
        case Hit::Nothing:    setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void TimelineView::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    if (hoverEventId.isNotEmpty()) { hoverEventId.clear(); repaint(); }
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (renameEditor != nullptr) renameEditor->unfocusAllComponents();
    drag = {};
    drag.down = e.position;
    drag.downTick = tickForX (e.position.x);
    if (e.mods.isMiddleButtonDown())
    {
        // Grab the arrangement and drag it around (same job as Shift+wheel, but direct).
        drag.kind = DragKind::Pan;
        drag.downScrollBeat = scrollBeat;
        drag.downScrollY = scrollY;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        return;
    }
    drag.noSnap = e.mods.isShiftDown();   // Shift bypasses snapping (Alt is slip)
    const auto hit = hitTest (e.position);
    drag.downRow = hit.row;
    const auto& p = app.project();

    switch (hit.kind)
    {
        case Hit::LoopStrip:
        {
            const float a = xForTick (p.loopStart), b = xForTick (p.loopEnd);
            drag.a = p.loopStart;
            drag.b = p.loopEnd;
            if (std::abs (e.position.x - a) < 6) drag.kind = DragKind::LoopStart;
            else if (std::abs (e.position.x - b) < 6) drag.kind = DragKind::LoopEnd;
            else if (e.position.x > a && e.position.x < b) drag.kind = DragKind::LoopMove;
            else { drag.kind = DragKind::LoopCreate; drag.a = drag.b = snapped (drag.downTick, drag.noSnap); }
            break;
        }
        case Hit::Ruler:
            drag.kind = DragKind::Seek;
            app.run ("seek", makeObject ({ { "beat", beatsVar (snapped (drag.downTick, drag.noSnap)) } }));
            break;

        case Hit::Header:
        {
            const auto& t = p.tracks[static_cast<size_t> (hit.row)];
            app.selectTrack (t.id);
            if (e.mods.isPopupMenu()) { showHeaderMenu (hit.row); return; }
            const auto l = headerLayout (hit.row);
            const auto pos = e.getPosition();
            if (l.mute.contains (pos)) app.run ("mute_track", makeObject ({ { "trackId", t.id }, { "enabled", ! t.mute } }));
            else if (l.solo.contains (pos))
            {
                if (e.mods.isCtrlDown())    // exclusive solo
                {
                    app.beginGesture ("Exclusive solo");
                    for (const auto& other : p.tracks)
                        app.run ("solo_track", makeObject ({ { "trackId", other.id }, { "enabled", other.id == t.id } }));
                    app.endGesture();
                }
                else app.run ("solo_track", makeObject ({ { "trackId", t.id }, { "enabled", ! t.solo } }));
            }
            else if (l.fx.contains (pos)) { app.fxTarget = {}; app.selectionChanged.sendChangeMessage(); }
            else if (l.gain.expanded (0, 4).contains (pos))
            {
                drag.kind = DragKind::HeaderGain;
                drag.trackId = t.id;
                app.beginGesture ("Track gain");
                const double prop = (e.position.x - l.gain.getX()) / static_cast<double> (l.gain.getWidth());
                app.run ("set_track_gain", makeObject ({ { "trackId", t.id }, { "db", std::round (gainDbForProportion (prop) * 10.0) / 10.0 } }));
            }
            break;
        }

        case Hit::EventBody:
        case Hit::EventStart:
        case Hit::EventEnd:
        case Hit::EventFadeIn:
        case Hit::EventFadeOut:
        case Hit::EventGain:
        {
            const auto& ev = *hit.event;
            const auto& track = p.tracks[static_cast<size_t> (hit.row)];
            app.selectTrack (track.id);
            drag.wasSelected = app.selectedEvents.contains (ev.id);
            if (e.mods.isShiftDown())
            {
                auto sel = app.selectedEvents;
                if (sel.contains (ev.id)) sel.removeString (ev.id); else sel.add (ev.id);
                app.selectEvents (sel);
            }
            else if (e.mods.isCtrlDown())
            {
                // Ctrl adds to the selection (a click without dragging removes an already-selected
                // event again on mouse-up); Ctrl+drag copies the whole selection.
                if (! drag.wasSelected) { auto sel = app.selectedEvents; sel.add (ev.id); app.selectEvents (sel); }
            }
            else if (! drag.wasSelected) app.selectEvents ({ ev.id });

            if (e.mods.isPopupMenu()) { showEventMenu (ev, hit.row); return; }

            drag.eventId = ev.id;
            switch (hit.kind)
            {
                case Hit::EventStart:   drag.kind = DragKind::TrimStart; break;
                case Hit::EventEnd:     drag.kind = DragKind::TrimEnd; break;
                case Hit::EventFadeIn:  drag.kind = DragKind::FadeIn; break;
                case Hit::EventFadeOut: drag.kind = DragKind::FadeOut; break;
                case Hit::EventGain:    drag.kind = DragKind::EventGain; break;
                default:                drag.kind = e.mods.isAltDown() ? DragKind::Slip : DragKind::Move; break;
            }
            drag.copy = e.mods.isCtrlDown() && drag.kind == DragKind::Move;
            const bool wholeSelection = drag.kind == DragKind::Move || (drag.kind == DragKind::EventGain && app.selectedEvents.contains (ev.id));
            if (wholeSelection)
            {
                // Move drags the selection; the gain line adjusts every selected event (relative, in dB).
                for (int r = 0; r < static_cast<int> (p.tracks.size()); ++r)
                    for (const auto& x : p.tracks[static_cast<size_t> (r)].events)
                        if (app.selectedEvents.contains (x.id)) drag.originals.push_back ({ x, r });
            }
            else drag.originals.push_back ({ ev, hit.row });
            drag.previews = drag.originals;
            if (drag.kind == DragKind::Slip) setMouseCursor (slipCursor());
            break;
        }

        case Hit::EmptyLane:
        {
            if (e.mods.isCtrlDown() && ! e.mods.isPopupMenu())
            {
                // Ctrl turns the pencil into a selector: rubber-band select (Shift keeps the old selection).
                drag.kind = DragKind::Marquee;
                drag.baseSelection = e.mods.isShiftDown() ? app.selectedEvents : juce::StringArray();
                drag.current = e.position;
                drag.downScrollY = scrollY;
                if (hit.row >= 0) app.selectTrack (p.tracks[static_cast<size_t> (hit.row)].id);
                break;
            }
            if (! e.mods.isShiftDown()) app.selectEvents ({});
            if (hit.row >= 0)
            {
                const auto& t = p.tracks[static_cast<size_t> (hit.row)];
                app.selectTrack (t.id);
                drag.assetId = trackAssetFor (t, drag.downTick);
                drag.trackId = t.id;
            }
            drag.kind = drag.assetId.isNotEmpty() ? DragKind::Paint : DragKind::Seek;
            drag.a = drag.b = drag.downTick;
            break;
        }

        case Hit::Nothing:
            break;
    }
    repaint();
}

juce::Rectangle<float> TimelineView::marqueeRect() const
{
    // Anchor = the tick and content row under the mouse-down point, re-projected with the current scroll.
    const juce::Point<float> anchor (xForTick (drag.downTick), drag.down.y + static_cast<float> (drag.downScrollY - scrollY));
    return juce::Rectangle<float> (anchor, drag.current);
}

void TimelineView::updateMarqueeSelection()
{
    const auto band = marqueeRect();
    auto ids = drag.baseSelection;
    const auto& p = app.project();
    for (int row = 0; row < static_cast<int> (p.tracks.size()); ++row)
        for (const auto& ev : p.tracks[static_cast<size_t> (row)].events)
            if (eventRect (ev, row).intersects (band)) ids.addIfNotAlreadyThere (ev.id);
    if (ids != app.selectedEvents) app.selectEvents (ids);
}

void TimelineView::buildMovePreview()
{
    const auto& p = app.project();
    drag.previews.clear();
    for (const auto& o : drag.originals)
    {
        Ghost g = o;
        g.event.start = o.event.start + drag.delta;
        g.row = juce::jlimit (0, static_cast<int> (p.tracks.size()) - 1, o.row + drag.rowDelta);
        drag.previews.push_back (g);
    }
}

void TimelineView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.kind == DragKind::None) return;
    if (drag.kind == DragKind::Pan)
    {
        const auto d = e.position - drag.down;
        scrollBeat = std::max (0.0, drag.downScrollBeat - d.x / pixelsPerBeat);   // move right -> view heads to the start
        scrollY = drag.downScrollY - juce::roundToInt (d.y);
        drag.moved = true;
        updateScrollbars();
        repaint();
        return;
    }
    if (! drag.moved && e.getDistanceFromDragStart() < 3) return;
    drag.moved = true;
    drag.noSnap = e.mods.isShiftDown();   // Shift during any drag temporarily bypasses snapping

    // Edge auto-scroll while dragging horizontally.
    if (drag.kind != DragKind::HeaderGain && drag.kind != DragKind::EventGain)
    {
        if (e.position.x > laneRight() - 16) { scrollBeat += 8.0 / pixelsPerBeat; updateScrollbars(); }
        else if (e.position.x < kHeaderWidth + 8 && scrollBeat > 0) { scrollBeat = std::max (0.0, scrollBeat - 8.0 / pixelsPerBeat); updateScrollbars(); }
    }

    const auto& p = app.project();
    const Tick mouseTick = tickForX (e.position.x);
    const Tick rawDelta = mouseTick - drag.downTick;

    switch (drag.kind)
    {
        case DragKind::Move:
        {
            const auto primary = std::find_if (drag.originals.begin(), drag.originals.end(), [&] (const Ghost& g) { return g.event.id == drag.eventId; });
            if (primary == drag.originals.end()) break;
            const Tick newStart = std::max<Tick> (0, snapped (primary->event.start + rawDelta, drag.noSnap));
            Tick minStart = std::numeric_limits<Tick>::max();
            int minRow = 1 << 20, maxRow = -1;
            for (const auto& o : drag.originals) { minStart = std::min (minStart, o.event.start); minRow = std::min (minRow, o.row); maxRow = std::max (maxRow, o.row); }
            drag.delta = std::max (newStart - primary->event.start, -minStart);
            const int row = rowAt (static_cast<int> (e.position.y));
            drag.rowDelta = juce::jlimit (-minRow, static_cast<int> (p.tracks.size()) - 1 - maxRow, row - drag.downRow);
            buildMovePreview();
            setMouseCursor (drag.copy ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::DraggingHandCursor);
            break;
        }
        case DragKind::TrimEnd:
        {
            auto& g = drag.previews.front();
            const auto& o = drag.originals.front().event;
            const Tick minLen = drag.noSnap ? 1 : std::max<Tick> (1, gridTicks (app.snap));
            Tick newEnd = o.start + std::max (minLen, snapped (o.end() + rawDelta, drag.noSnap) - o.start);
            // A reversed one-shot's right edge carries its source offset: it can't reach before the source start.
            if (o.reversed && ! o.looping)
                newEnd = std::min (newEnd, o.end() + beatsToTicks (secondsToBeats (o.sourceOffsetSeconds / o.rate(), p.bpm)));
            g.event = o;
            g.event.sourceOffsetSeconds = std::max (0.0, offsetForRange (p, o, o.start, newEnd));
            g.event.length = newEnd - o.start;
            g.event.clampFades();
            break;
        }
        case DragKind::TrimStart:
        {
            auto& g = drag.previews.front();
            const auto& o = drag.originals.front().event;
            Tick newStart = std::clamp<Tick> (snapped (o.start + rawDelta, drag.noSnap), 0, o.end() - 1);
            if (! o.looping && ! o.reversed)
                newStart = std::max (newStart, o.start - beatsToTicks (secondsToBeats (o.sourceOffsetSeconds / o.rate(), p.bpm)));
            g.event = o;
            g.event.sourceOffsetSeconds = std::max (0.0, offsetForRange (p, o, newStart, o.end()));   // audio stays in place
            g.event.start = newStart;
            g.event.length = o.end() - newStart;
            g.event.clampFades();
            break;
        }
        case DragKind::Slip:
        {
            // The event stays put; the audio follows the mouse (snapped to the grid unless Shift).
            auto& g = drag.previews.front();
            const auto& o = drag.originals.front().event;
            drag.delta = drag.noSnap ? rawDelta : snapTicks (rawDelta, gridTicks (app.snap));
            g.event = o;
            g.event.sourceOffsetSeconds = slippedOffset (p, o, ticksToBeats (drag.delta), &drag.slipClamped);
            break;
        }
        case DragKind::EventGain:
        {
            // Amplitude mapping (see gainLineY): dragging down by the full waveform height = silence.
            const auto primary = std::find_if (drag.originals.begin(), drag.originals.end(), [&] (const Ghost& gh) { return gh.event.id == drag.eventId; });
            if (primary == drag.originals.end()) break;
            const auto wave = waveArea (eventRect (primary->event, primary->row));
            const double startLin = std::min (1.0, dbToGain (primary->event.gainDb));
            const double lin = std::clamp (startLin + (drag.down.y - e.position.y) / std::max (1.0f, wave.getHeight()), 0.0, 1.0);
            const bool unchangedAboveUnity = primary->event.gainDb > 0.0 && lin >= 1.0;   // don't snap +dB back to 0 at the top
            double db = unchangedAboveUnity ? primary->event.gainDb : (lin <= 0.0 ? kMinDb : std::round (gainToDb (lin) * 10.0) / 10.0);
            if (! e.mods.isShiftDown() && std::abs (db) < 0.3) db = 0.0;   // detent at unity
            const double change = db - primary->event.gainDb;
            for (size_t i = 0; i < drag.originals.size(); ++i)
            {
                const auto& o = drag.originals[i].event;
                drag.previews[i].event.gainDb = db <= kMinDb ? kMinDb : std::clamp (o.gainDb + change, kMinDb, kMaxDb);
            }
            break;
        }
        case DragKind::FadeIn:
        case DragKind::FadeOut:
        {
            auto& g = drag.previews.front();
            const auto& o = drag.originals.front().event;
            const Tick at = std::clamp<Tick> (snapped (mouseTick, drag.noSnap), o.start, o.end());
            if (drag.kind == DragKind::FadeIn) g.event.fadeInTicks = at - o.start;
            else g.event.fadeOutTicks = o.end() - at;
            break;
        }
        case DragKind::Paint:
        {
            const Tick grid = drag.noSnap ? 0 : gridTicks (app.snap);
            const Tick lo = std::min (drag.downTick, mouseTick), hi = std::max (drag.downTick, mouseTick);
            drag.a = grid > 0 ? snapTicksDown (lo, grid) : lo;
            drag.b = grid > 0 ? snapTicksDown (hi, grid) + grid : hi;
            if (drag.b - drag.a < kTicksPerBeat / 8) drag.b = drag.a + std::max<Tick> (grid, kTicksPerBeat / 8);
            break;
        }
        case DragKind::Seek:
            app.run ("seek", makeObject ({ { "beat", beatsVar (snapped (mouseTick, drag.noSnap)) } }));
            break;
        case DragKind::Marquee:
            drag.current = { juce::jlimit (static_cast<float> (kHeaderWidth), static_cast<float> (laneRight()), e.position.x),
                             juce::jlimit (static_cast<float> (kRulerHeight), static_cast<float> (lanesBottom()), e.position.y) };
            // Vertical auto-scroll so a selection can span more tracks than fit on screen.
            if (e.position.y > lanesBottom() - 10) { scrollY += 8; updateScrollbars(); }
            else if (e.position.y < kRulerHeight + 6 && scrollY > 0) { scrollY = std::max (0, scrollY - 8); updateScrollbars(); }
            updateMarqueeSelection();
            break;
        case DragKind::LoopCreate:
        {
            const Tick t = snapped (mouseTick, drag.noSnap);
            const Tick anchor = snapped (drag.downTick, drag.noSnap);
            drag.a = std::min (anchor, t);
            drag.b = std::max (anchor, t);
            break;
        }
        case DragKind::LoopMove:
        {
            const Tick len = p.loopEnd - p.loopStart;
            drag.a = std::max<Tick> (0, snapped (p.loopStart + rawDelta, drag.noSnap));
            drag.b = drag.a + len;
            break;
        }
        case DragKind::LoopStart: drag.a = std::min (snapped (mouseTick, drag.noSnap), p.loopEnd - 1); break;
        case DragKind::LoopEnd:   drag.b = std::max (snapped (mouseTick, drag.noSnap), p.loopStart + 1); break;
        case DragKind::HeaderGain:
        {
            const int row = p.trackIndex (drag.trackId);
            if (row < 0) break;
            const auto l = headerLayout (row);
            double db = gainDbForProportion ((e.position.x - l.gain.getX()) / static_cast<double> (l.gain.getWidth()));
            db = e.mods.isShiftDown() ? db : std::round (db * 2.0) / 2.0;   // 0.5 dB steps unless Shift
            if (std::abs (db) < 0.75 && ! e.mods.isShiftDown()) db = 0.0;   // detent at unity
            app.run ("set_track_gain", makeObject ({ { "trackId", drag.trackId }, { "db", db } }));
            break;
        }
        case DragKind::Pan:
        case DragKind::None: break;
    }
    repaint();
}

void TimelineView::mouseUp (const juce::MouseEvent& e)
{
    if (drag.kind == DragKind::None) return;
    if (drag.kind == DragKind::Pan) { drag = {}; updateCursor (e.position, e.mods); return; }
    if (e.mods.isPopupMenu() && ! drag.moved) { drag = {}; return; }
    commitDrag();
    drag = {};
    mouseMove (e);
    repaint();
}

void TimelineView::commitDrag()
{
    const auto& p = app.project();
    auto clickSeek = [&] { app.run ("seek", makeObject ({ { "beat", beatsVar (snapped (drag.downTick, drag.noSnap)) } })); };

    switch (drag.kind)
    {
        case DragKind::Move:
        {
            if (! drag.moved && drag.copy)                      // Ctrl+click: toggle membership
            {
                if (drag.wasSelected) { auto sel = app.selectedEvents; sel.removeString (drag.eventId); app.selectEvents (sel); }
                break;
            }
            if (! drag.moved || (drag.delta == 0 && drag.rowDelta == 0 && ! drag.copy)) { clickSeek(); break; }
            if (drag.delta == 0 && drag.rowDelta == 0 && drag.copy) break;
            juce::Array<juce::var> moves;
            for (const auto& g : drag.previews)
                moves.add (makeObject ({ { "eventId", g.event.id }, { "startBeat", beatsVar (g.event.start) },
                                         { "trackId", p.tracks[static_cast<size_t> (g.row)].id } }));
            const auto result = app.run ("move_events", makeObject ({ { "moves", moves }, { "copy", drag.copy } }));
            if (result.isObject())
            {
                juce::StringArray ids;
                for (const auto& id : *result["eventIds"].getArray()) ids.add (id.toString());
                app.selectEvents (ids);
            }
            break;
        }
        case DragKind::TrimEnd:
            if (drag.moved)
                app.run ("resize_event", makeObject ({ { "eventId", drag.eventId }, { "endBeat", beatsVar (drag.previews.front().event.end()) } }));
            else clickSeek();
            break;
        case DragKind::TrimStart:
            if (drag.moved)
                app.run ("resize_event", makeObject ({ { "eventId", drag.eventId }, { "startBeat", beatsVar (drag.previews.front().event.start) } }));
            else clickSeek();
            break;
        case DragKind::Slip:
            if (drag.moved && drag.delta != 0)
                app.run ("slip_event", makeObject ({ { "eventId", drag.eventId },
                                                     { "sourceOffsetSeconds", drag.previews.front().event.sourceOffsetSeconds } }));
            else if (! drag.moved) clickSeek();
            break;
        case DragKind::EventGain:
        {
            if (! drag.moved) break;
            // One undo step ("Adjust event gain") whether one or many events changed.
            std::map<double, juce::Array<juce::var>> byValue;   // events sharing a value go in one call
            for (size_t i = 0; i < drag.previews.size(); ++i)
                if (drag.previews[i].event.gainDb != drag.originals[i].event.gainDb)
                    byValue[drag.previews[i].event.gainDb].add (drag.previews[i].event.id);
            if (byValue.size() == 1)
                app.run ("set_event_gain", makeObject ({ { "eventIds", byValue.begin()->second }, { "db", byValue.begin()->first } }));
            else if (byValue.size() > 1)
            {
                juce::Array<juce::var> steps;
                for (const auto& [db, ids] : byValue)
                    steps.add (makeObject ({ { "action", "set_event_gain" }, { "arguments", makeObject ({ { "eventIds", ids }, { "db", db } }) } }));
                app.run ("execute_batch", makeObject ({ { "actions", steps }, { "label", "Adjust event gain" } }));
            }
            break;
        }
        case DragKind::FadeIn:
        case DragKind::FadeOut:
        {
            if (! drag.moved) break;
            const auto& g = drag.previews.front().event;
            const bool in = drag.kind == DragKind::FadeIn;
            if ((in ? g.fadeInTicks : g.fadeOutTicks) == (in ? drag.originals.front().event.fadeInTicks : drag.originals.front().event.fadeOutTicks)) break;
            app.run ("set_event_fades", makeObject ({ { "eventIds", juce::Array<juce::var> { g.id } },
                                                      { in ? "fadeInBeats" : "fadeOutBeats", beatsVar (in ? g.fadeInTicks : g.fadeOutTicks) } }));
            break;
        }
        case DragKind::Paint:
            if (! drag.moved) { clickSeek(); break; }
            if (drag.b > drag.a)
            {
                const auto r = app.run ("create_event", makeObject ({ { "trackId", drag.trackId }, { "assetId", drag.assetId },
                                                                      { "startBeat", beatsVar (drag.a) }, { "lengthBeats", beatsVar (drag.b - drag.a) } }));
                if (r.isObject()) app.selectEvents ({ r["id"].toString() });
            }
            break;
        case DragKind::LoopCreate:
            if (drag.moved && drag.b > drag.a)
                app.run ("set_loop_region", makeObject ({ { "startBeat", beatsVar (drag.a) }, { "endBeat", beatsVar (drag.b) }, { "enabled", true } }));
            break;
        case DragKind::LoopMove:
        case DragKind::LoopStart:
        case DragKind::LoopEnd:
            if (drag.moved && drag.b > drag.a)
                app.run ("set_loop_region", makeObject ({ { "startBeat", beatsVar (drag.a) }, { "endBeat", beatsVar (drag.b) } }));
            break;
        case DragKind::HeaderGain:
            app.endGesture();
            break;
        case DragKind::Pan: break;             // handled in mouseUp
        case DragKind::Marquee:
            if (! drag.moved) { app.selectEvents (drag.baseSelection); clickSeek(); }
            else if (! app.selectedEvents.isEmpty())
                app.status (juce::String (app.selectedEvents.size()) + " events selected - drag to move, Ctrl+drag to copy, Ctrl+C / Ctrl+V, Del");
            break;
        case DragKind::Seek:
        case DragKind::None:
            break;
    }
}

void TimelineView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = hitTest (e.position);
    const auto& p = app.project();
    switch (hit.kind)
    {
        case Hit::EventGain:      // double-click the gain line: back to 0 dB
            app.run ("set_event_gain", makeObject ({ { "eventId", hit.event->id }, { "db", 0.0 } }));
            break;
        case Hit::EventFadeIn:    // double-click a fade handle: remove that fade (the declick remains)
        case Hit::EventFadeOut:
            app.run ("set_event_fades", makeObject ({ { "eventIds", juce::Array<juce::var> { hit.event->id } },
                                                      { hit.kind == Hit::EventFadeIn ? "fadeInBeats" : "fadeOutBeats", 0 } }));
            break;
        case Hit::EventBody:
        case Hit::EventStart:
        case Hit::EventEnd:
            app.run ("set_loop_region", makeObject ({ { "startBeat", beatsVar (hit.event->start) }, { "endBeat", beatsVar (hit.event->end()) }, { "enabled", true } }));
            break;
        case Hit::LoopStrip:
            app.run ("set_loop_region", makeObject ({ { "enabled", ! p.loopEnabled } }));
            break;
        case Hit::Header:
        {
            const auto l = headerLayout (hit.row);
            if (l.name.contains (e.getPosition())) startRename (hit.row);
            else if (l.gain.expanded (0, 4).contains (e.getPosition()))
                app.run ("set_track_gain", makeObject ({ { "trackId", p.tracks[static_cast<size_t> (hit.row)].id }, { "db", 0.0 } }));
            break;
        }
        case Hit::EmptyLane:
            if (hit.row < 0) app.run ("create_track");
            break;
        default: break;
    }
}

void TimelineView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        zoomBy (w.deltaY > 0 ? 1.2 : 1.0 / 1.2, e.position.x);
        return;
    }
    if (e.mods.isAltDown())
    {
        trackHeight = juce::jlimit (48, 200, trackHeight + (w.deltaY > 0 ? 8 : -8));
        updateScrollbars();
        repaint();
        return;
    }
    const bool horizontal = e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY);
    if (horizontal)
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        scrollBeat = std::max (0.0, scrollBeat - d * 400.0 / pixelsPerBeat);
    }
    else scrollY = std::max (0, scrollY - static_cast<int> (w.deltaY * 300.0f));
    updateScrollbars();
    repaint();
}

void TimelineView::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    zoomBy (scale, e.position.x);
}

// ================================================================================================
// Menus, rename
// ================================================================================================
void TimelineView::showEventMenu (const Event& ev, int)
{
    juce::PopupMenu m;
    const auto cursor = beatsToTicks (app.engine.positionBeats());
    m.addItem (1, "Split at cursor  (S)", cursor > ev.start && cursor < ev.end());
    m.addItem (2, "Duplicate  (Ctrl+D)");
    m.addItem (3, "Copy  (Ctrl+C)");
    m.addItem (4, "Delete  (Del)");
    m.addSeparator();
    m.addItem (5, "Loop (repeat source)", true, ev.looping);
    m.addItem (7, "Reverse  (U)", true, ev.reversed);
    m.addItem (6, "Set loop region to event");

    // Event fades: curve per edge (drag the top corners for the lengths). Like Pitch, these apply to the
    // whole selection when the clicked event is part of it.
    constexpr int kFadeInCurveBase = 200, kFadeOutCurveBase = 210;
    juce::PopupMenu fadeMenu;
    const char* curveLabels[] = { "Linear", "Smooth", "Fast", "Slow" };
    fadeMenu.addSectionHeader ("Fade-in curve");
    for (int c = 0; c < 4; ++c) fadeMenu.addItem (kFadeInCurveBase + c, curveLabels[c], true, static_cast<int> (ev.fadeInCurve) == c);
    fadeMenu.addSectionHeader ("Fade-out curve");
    for (int c = 0; c < 4; ++c) fadeMenu.addItem (kFadeOutCurveBase + c, curveLabels[c], true, static_cast<int> (ev.fadeOutCurve) == c);
    fadeMenu.addSeparator();
    fadeMenu.addItem (220, "Remove fades (keep the declick)", ev.fadeInTicks > 0 || ev.fadeOutTicks > 0);
    m.addSubMenu ("Fades", fadeMenu);

    // Pitch: -12..+12 semitones (varispeed, like a sampler). Applies to the whole selection
    // when the clicked event is part of it, otherwise just to the clicked event.
    juce::PopupMenu pitchMenu;
    constexpr int kPitchBase = 100;   // item id = kPitchBase + 12 + semitones  (100..124)
    for (int s = 12; s >= -12; --s)
    {
        const juce::String text = s == 0 ? juce::String ("0  (original)") : (s > 0 ? "+" : "") + juce::String (s) + (std::abs (s) == 1 ? " semitone" : " semitones");
        pitchMenu.addItem (kPitchBase + 12 + s, text, true, std::abs (ev.pitchSemitones - s) < 0.01);
        if (s == 1 || s == 0) pitchMenu.addSeparator();
    }
    m.addSubMenu ("Pitch" + (ev.pitchSemitones != 0.0 ? juce::String ("  (") + (ev.pitchSemitones > 0 ? "+" : "")
                                                         + juce::String (ev.pitchSemitones, 0) + " st)" : juce::String()),
                  pitchMenu);

    // Embed Loop Metadata: write ACID-compatible loop metadata into this event's source WAV.
    juce::File sourceFile;
    if (const auto* a = app.project().findAsset (ev.assetId)) sourceFile = juce::File (a->path);
    m.addSeparator();
    m.addItem (8, "Embed Loop Metadata in source WAV...", sourceFile.hasFileExtension ("wav") && sourceFile.existsAsFile() && onEmbedLoopMetadata != nullptr);

    const auto id = ev.id;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<TimelineView> (this), id, sourceFile] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& self = *safe;
        const auto ref = self.app.project().findEvent (id);
        if (ref.event == nullptr) return;
        const auto targets = self.app.selectedEvents.contains (id) ? self.app.selectedEvents : juce::StringArray (id);
        juce::Array<juce::var> ids;
        for (const auto& t : targets) ids.add (t);
        if (r >= 100 && r <= 124)
        {
            self.app.run ("set_event_pitch", makeObject ({ { "eventIds", ids }, { "semitones", r - 112 } }));
            return;
        }
        static const char* curveNames[] = { "linear", "smooth", "fast", "slow" };
        if (r >= 200 && r < 204) { self.app.run ("set_event_fades", makeObject ({ { "eventIds", ids }, { "fadeInCurve", curveNames[r - 200] } })); return; }
        if (r >= 210 && r < 214) { self.app.run ("set_event_fades", makeObject ({ { "eventIds", ids }, { "fadeOutCurve", curveNames[r - 210] } })); return; }
        if (r == 220) { self.app.run ("set_event_fades", makeObject ({ { "eventIds", ids }, { "fadeInBeats", 0 }, { "fadeOutBeats", 0 } })); return; }
        if (r == 7)
        {
            self.app.run ("set_event_reverse", makeObject ({ { "eventIds", ids }, { "enabled", ! ref.event->reversed } }));
            return;
        }
        if (r == 8) { if (self.onEmbedLoopMetadata) self.onEmbedLoopMetadata (sourceFile); return; }
        switch (r)
        {
            case 1: self.splitAtCursor(); break;
            case 2: self.duplicateSelection(); break;
            case 3: self.copySelection(); break;
            case 4: self.deleteSelection(); break;
            case 5: self.app.run ("set_event_looping", makeObject ({ { "eventId", id }, { "enabled", ! ref.event->looping } })); break;
            case 6: self.app.run ("set_loop_region", makeObject ({ { "startBeat", ticksToBeats (ref.event->start) },
                                                                   { "endBeat", ticksToBeats (ref.event->end()) }, { "enabled", true } })); break;
            default: break;
        }
    });
}

void TimelineView::showHeaderMenu (int row)
{
    const auto& t = app.project().tracks[static_cast<size_t> (row)];
    juce::PopupMenu m, colourMenu, fxMenu;
    for (int c = 0; c < colours::kNumTrackColours; ++c)
    {
        juce::PopupMenu::Item item ("Colour " + juce::String (c + 1));
        item.itemID = 100 + c;
        item.colour = colours::track (c);
        item.isTicked = t.colour == c;
        colourMenu.addItem (item);
    }
    int fxId = 200;
    for (const auto& d : effectCatalogue()) fxMenu.addItem (fxId++, d.name);

    // Replace audio: the audio files in the browser's current folder (hover 250 ms to audition), the
    // current sample's own folder when that differs, and Browse. This is the human-in-the-loop step:
    // an agent can place the hits, the person picks the sounds.
    juce::PopupMenu audioMenu;
    juce::File current;
    if (! t.events.empty())
        if (const auto* a = app.project().findAsset (t.events.front().assetId)) current = juce::File (a->path);
    std::vector<juce::File> choices;                               // index = item id - 400
    auto addFolder = [&] (juce::PopupMenu& menu, const juce::File& dir)
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*");
        files.removeIf ([] (const juce::File& f) { return ! isSupportedAudioFile (f); });
        files.sort();
        for (const auto& f : files)
        {
            if (choices.size() >= 500) break;                       // enough for any sane sample folder
            juce::PopupMenu::Item item (f.getFileName());
            item.itemID = 400 + static_cast<int> (choices.size());
            item.isTicked = f == current;
            item.customComponent = new AudioChoiceItem (app, f, f == current);
            menu.addItem (std::move (item));
            choices.push_back (f);
        }
        return files.size();
    };
    const auto browseDir = app.browserFolder;
    int listed = 0;
    if (browseDir.isDirectory())
    {
        audioMenu.addSectionHeader ("Browser: " + browseDir.getFileName());
        listed = addFolder (audioMenu, browseDir);
        if (listed == 0) audioMenu.addItem (399, "(no audio files in this folder)", false);
    }
    if (current.getParentDirectory().isDirectory() && current.getParentDirectory() != browseDir)
    {
        juce::PopupMenu here;
        if (addFolder (here, current.getParentDirectory()) > 0)
            audioMenu.addSubMenu ("Current sample's folder: " + current.getParentDirectory().getFileName(), here);
    }
    audioMenu.addSeparator();
    audioMenu.addItem (300, "Browse...");

    m.addItem (1, "Rename...");
    m.addSubMenu ("Colour", colourMenu);
    m.addSubMenu ("Add effect", fxMenu);
    m.addSubMenu ("Replace audio", audioMenu, ! t.events.empty());
    m.addSeparator();
    m.addItem (2, "Move up", row > 0);
    m.addItem (3, "Move down", row + 1 < static_cast<int> (app.project().tracks.size()));
    m.addSeparator();
    m.addItem (4, "Delete track");
    const auto id = t.id;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<TimelineView> (this), id, row, choices, current] (int r)
    {
        if (safe == nullptr) return;
        auto& self = *safe;
        self.app.engine.stopPreview();                             // menu closed: end any hover audition
        if (r == 0) return;
        if (r == 1) self.startRename (row);
        else if (r >= 400 && r - 400 < static_cast<int> (choices.size())) self.replaceTrackAudio (id, choices[static_cast<size_t> (r - 400)]);
        else if (r == 300)
        {
            const auto start = current.existsAsFile() ? current.getParentDirectory()
                                                      : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
            self.audioChooser = std::make_unique<juce::FileChooser> ("Replace audio on track", start, "*.wav;*.aif;*.aiff;*.flac");
            self.audioChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                            [safe, id] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult().existsAsFile()) safe->replaceTrackAudio (id, fc.getResult());
            });
        }
        else if (r == 2) self.app.run ("move_track", makeObject ({ { "trackId", id }, { "index", row - 1 } }));
        else if (r == 3) self.app.run ("move_track", makeObject ({ { "trackId", id }, { "index", row + 1 } }));
        else if (r == 4) self.app.run ("delete_track", makeObject ({ { "trackId", id } }));
        else if (r >= 100 && r < 100 + colours::kNumTrackColours) self.app.run ("set_track_colour", makeObject ({ { "trackId", id }, { "colour", r - 100 } }));
        else if (r >= 200)
        {
            const auto& list = effectCatalogue();
            const auto idx = static_cast<size_t> (r - 200);
            if (idx < list.size())
            {
                self.app.run ("add_effect", makeObject ({ { "target", id }, { "type", list[idx].type } }));
                self.app.fxTarget = {};
                self.app.selectTrack (id);
                self.app.selectionChanged.sendChangeMessage();
            }
        }
    });
}

void TimelineView::replaceTrackAudio (const juce::String& trackId, const juce::File& file)
{
    const auto result = app.run ("replace_track_audio", makeObject ({ { "trackId", trackId }, { "path", file.getFullPathName() } }));
    if (result.isObject())
        app.status ("Track now plays " + file.getFileName() + " (" + result["eventsChanged"].toString() + " events) - Ctrl+Z to undo");
}

void TimelineView::startRename (int row)
{
    const auto& t = app.project().tracks[static_cast<size_t> (row)];
    renameTrackId = t.id;
    renameEditor = std::make_unique<juce::TextEditor>();
    renameEditor->setFont (fonts::ui (13.5f, true));
    renameEditor->setText (t.name, false);
    renameEditor->selectAll();
    addAndMakeVisible (*renameEditor);
    renameEditor->setBounds (headerLayout (row).name.expanded (2, 1));
    auto finish = [this] (bool commit)
    {
        if (renameEditor == nullptr) return;
        const auto text = renameEditor->getText().trim();
        const auto id = renameTrackId;
        renameTrackId.clear();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TimelineView> (this)] { if (safe != nullptr) safe->renameEditor.reset(); });
        if (commit && text.isNotEmpty()) app.run ("rename_track", makeObject ({ { "trackId", id }, { "name", text } }));
        repaint();
    };
    renameEditor->onReturnKey = [finish] { finish (true); };
    renameEditor->onEscapeKey = [finish] { finish (false); };
    renameEditor->onFocusLost = [finish] { finish (true); };
    renameEditor->grabKeyboardFocus();
    repaint();
}

// ================================================================================================
// Editing commands
// ================================================================================================
void TimelineView::splitAtCursor()
{
    const Tick cursor = beatsToTicks (app.engine.positionBeats());
    const auto& p = app.project();
    juce::StringArray targets;
    for (const auto& t : p.tracks)
        for (const auto& e : t.events)
            if (e.start < cursor && e.end() > cursor && (app.selectedEvents.isEmpty() || app.selectedEvents.contains (e.id)))
                targets.add (e.id);
    if (targets.isEmpty()) { app.status ("Nothing to split at the cursor"); return; }
    app.beginGesture ("Split");
    juce::StringArray rights;
    for (const auto& id : targets)
    {
        const auto r = app.run ("split_event", makeObject ({ { "eventId", id }, { "beat", ticksToBeats (cursor) } }));
        if (r.isObject()) rights.add (r["rightEventId"].toString());
    }
    app.endGesture();
    app.selectEvents (rights);
}

void TimelineView::deleteSelection()
{
    const auto ids = app.selectedEvents;
    if (ids.isEmpty()) return;
    app.beginGesture ("Delete events");
    for (const auto& id : ids) app.run ("delete_event", makeObject ({ { "eventId", id } }));
    app.endGesture();
    app.selectEvents ({});
}

void TimelineView::duplicateSelection()
{
    const auto& p = app.project();
    Tick lo = std::numeric_limits<Tick>::max(), hi = 0;
    juce::Array<juce::var> moves;
    for (const auto& id : app.selectedEvents)
        if (const auto ref = p.findEvent (id); ref.event != nullptr) { lo = std::min (lo, ref.event->start); hi = std::max (hi, ref.event->end()); }
    if (hi <= lo) return;
    for (const auto& id : app.selectedEvents)
        if (const auto ref = p.findEvent (id); ref.event != nullptr)
            moves.add (makeObject ({ { "eventId", id }, { "startBeat", ticksToBeats (ref.event->start + (hi - lo)) } }));
    const auto r = app.run ("move_events", makeObject ({ { "moves", moves }, { "copy", true } }));
    if (r.isObject())
    {
        juce::StringArray ids;
        for (const auto& id : *r["eventIds"].getArray()) ids.add (id.toString());
        app.selectEvents (ids);
    }
}

void TimelineView::copySelection()
{
    const auto& p = app.project();
    clipboard.clear();
    Tick lo = std::numeric_limits<Tick>::max(), hi = 0;
    int minRow = 1 << 20;
    for (int r = 0; r < static_cast<int> (p.tracks.size()); ++r)
        for (const auto& e : p.tracks[static_cast<size_t> (r)].events)
            if (app.selectedEvents.contains (e.id)) { lo = std::min (lo, e.start); hi = std::max (hi, e.end()); minRow = std::min (minRow, r); }
    for (int r = 0; r < static_cast<int> (p.tracks.size()); ++r)
        for (const auto& e : p.tracks[static_cast<size_t> (r)].events)
            if (app.selectedEvents.contains (e.id))
            {
                Event copy = e;
                copy.start -= lo;
                clipboard.push_back ({ copy, r - minRow });
            }
    clipboardSpan = hi > lo ? hi - lo : 0;
    if (! clipboard.empty()) app.status ("Copied " + juce::String (clipboard.size()) + " event(s)");
}

void TimelineView::cutSelection()
{
    copySelection();
    deleteSelection();
}

void TimelineView::paste()
{
    const auto& p = app.project();
    if (clipboard.empty() || p.tracks.empty()) return;
    const Tick cursor = beatsToTicks (app.engine.positionBeats());
    int baseRow = std::max (0, p.trackIndex (app.selectedTrack));
    juce::StringArray ids;
    app.beginGesture ("Paste");
    for (const auto& item : clipboard)
    {
        const int row = baseRow + item.rowOffset;
        if (row >= static_cast<int> (p.tracks.size())) continue;
        const auto r = app.run ("create_event", makeObject ({ { "trackId", p.tracks[static_cast<size_t> (row)].id }, { "assetId", item.event.assetId },
                                                              { "startBeat", ticksToBeats (cursor + item.event.start) },
                                                              { "lengthBeats", ticksToBeats (item.event.length) },
                                                              { "looping", item.event.looping },
                                                              { "sourceOffsetSeconds", item.event.sourceOffsetSeconds },
                                                              { "pitchSemitones", item.event.pitchSemitones },
                                                              { "gainDb", item.event.gainDb },
                                                              { "reversed", item.event.reversed },
                                                              { "fadeInBeats", ticksToBeats (item.event.fadeInTicks) },
                                                              { "fadeOutBeats", ticksToBeats (item.event.fadeOutTicks) },
                                                              { "fadeInCurve", fadeCurveName (item.event.fadeInCurve) },
                                                              { "fadeOutCurve", fadeCurveName (item.event.fadeOutCurve) } }));
        if (r.isObject()) ids.add (r["id"].toString());
    }
    app.endGesture();
    app.selectEvents (ids);
    // Like ACID: the cursor moves to the end of the pasted block so repeated pastes chain.
    app.run ("seek", makeObject ({ { "beat", ticksToBeats (cursor + clipboardSpan) } }));
}

void TimelineView::selectAll()
{
    juce::StringArray ids;
    for (const auto& t : app.project().tracks)
        for (const auto& e : t.events) ids.add (e.id);
    app.selectEvents (ids);
}

void TimelineView::nudgeSelection (int direction)
{
    const auto& p = app.project();
    const Tick step = gridStep() * direction;
    juce::Array<juce::var> moves;
    for (const auto& id : app.selectedEvents)
        if (const auto ref = p.findEvent (id); ref.event != nullptr)
        {
            if (ref.event->start + step < 0) return;
            moves.add (makeObject ({ { "eventId", id }, { "startBeat", ticksToBeats (ref.event->start + step) } }));
        }
    if (! moves.isEmpty()) app.run ("move_events", makeObject ({ { "moves", moves } }));
}

void TimelineView::loopToSelection()
{
    const auto& p = app.project();
    Tick lo = std::numeric_limits<Tick>::max(), hi = 0;
    for (const auto& id : app.selectedEvents)
        if (const auto ref = p.findEvent (id); ref.event != nullptr) { lo = std::min (lo, ref.event->start); hi = std::max (hi, ref.event->end()); }
    if (hi > lo)
        app.run ("set_loop_region", makeObject ({ { "startBeat", ticksToBeats (lo) }, { "endBeat", ticksToBeats (hi) }, { "enabled", true } }));
}

void TimelineView::toggleSelectionReverse()
{
    const auto& p = app.project();
    juce::Array<juce::var> ids;
    bool allReversed = true;
    for (const auto& id : app.selectedEvents)
        if (const auto ref = p.findEvent (id); ref.event != nullptr)
        {
            ids.add (id);
            allReversed = allReversed && ref.event->reversed;
        }
    if (ids.isEmpty()) { app.status ("Select events to reverse (U)"); return; }
    // The shortcut toggles; the action itself takes an explicit state (as MCP clients use it).
    if (app.run ("set_event_reverse", makeObject ({ { "eventIds", ids }, { "enabled", ! allReversed } })).isObject())
        app.status (juce::String (ids.size()) + (ids.size() == 1 ? " event " : " events ") + (allReversed ? "forwards" : "reversed")
                    + " - the audio file is unchanged (Ctrl+Z to undo)");
}

void TimelineView::toggleSelectionLooping()
{
    const auto& p = app.project();
    app.beginGesture ("Toggle looping");
    for (const auto& id : app.selectedEvents)
        if (const auto ref = p.findEvent (id); ref.event != nullptr)
            app.run ("set_event_looping", makeObject ({ { "eventId", id }, { "enabled", ! ref.event->looping } }));
    app.endGesture();
}

// ================================================================================================
// Drag and drop
// ================================================================================================
bool TimelineView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (isSupportedAudioFile (juce::File (f))) return true;
    return false;
}

void TimelineView::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int row = rowAt (y);
    dropRow = row >= 0 && row < static_cast<int> (app.project().tracks.size()) ? row : -1;
    dropTick = snapped (tickForX (static_cast<float> (x)), false);
    repaint();
}

void TimelineView::fileDragExit (const juce::StringArray&) { dropRow = -2; dropTick = -1; repaint(); }

void TimelineView::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropRow = -2; dropTick = -1;
    importFiles (files, x, y);
}

bool TimelineView::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("file:") && isSupportedAudioFile (juce::File (d.description.toString().substring (5)));
}

void TimelineView::itemDragMove (const SourceDetails& d) { fileDragMove ({}, d.localPosition.x, d.localPosition.y); }
void TimelineView::itemDragExit (const SourceDetails&) { fileDragExit ({}); }

void TimelineView::itemDropped (const SourceDetails& d)
{
    dropRow = -2; dropTick = -1;
    importFiles ({ d.description.toString().substring (5) }, d.localPosition.x, d.localPosition.y);
}

void TimelineView::importFiles (const juce::StringArray& files, int x, int y)
{
    const auto& p = app.project();
    const int row = rowAt (y);
    const bool onTrack = row >= 0 && row < static_cast<int> (p.tracks.size()) && files.size() == 1;
    const Tick at = x >= kHeaderWidth ? snapped (tickForX (static_cast<float> (x)), false) : 0;
    juce::StringArray newEvents;
    app.beginGesture (files.size() == 1 ? "Import " + juce::File (files[0]).getFileName() : "Import " + juce::String (files.size()) + " files");
    for (const auto& f : files)
    {
        if (! isSupportedAudioFile (juce::File (f))) continue;
        auto args = makeObject ({ { "path", f }, { "startBeat", ticksToBeats (at) } });
        if (onTrack) args.getDynamicObject()->setProperty ("trackId", p.tracks[static_cast<size_t> (row)].id);
        const auto r = app.run ("import_audio", args);
        if (r.isObject())
        {
            newEvents.add (r["eventId"].toString());
            app.selectTrack (r["trackId"].toString());
            if (static_cast<bool> (r["tempoAdopted"]))
                app.status ("Project tempo set to " + juce::String (static_cast<double> (r["projectBpm"]), 2) + " BPM from " + juce::File (f).getFileName());
        }
    }
    app.endGesture();
    app.selectEvents (newEvents);
    grabKeyboardFocus();
    repaint();
}
} // namespace mcpdaw::ui
