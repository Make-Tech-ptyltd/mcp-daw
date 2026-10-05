#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// The arrangement timeline — the heart of the ACID workflow.
//
// Everything is painted and hit-tested by this one component (track headers, ruler, lanes) so
// scrolling, zooming and drag previews stay perfectly in sync and cheap to repaint.
//
// Interaction summary (ACID-style, minimal modes; modifiers instead of tools):
//   drag event body        move                               Ctrl+drag body    copy
//   Alt+drag event body    slip edit (audio moves under the stationary event)
//   Shift while dragging   temporarily bypass snapping (any drag)
//   drag event right edge  trim / extend (loops repeat)       drag left edge    trim start (audio stays in place)
//   drag top-left corner   event fade-in                      top-right corner  event fade-out (right-click: curves)
//   drag top line          event gain (whole selection when the event is selected; double-click = 0 dB)
//   U                      reverse selected events (non-destructive)
//   drag in empty lane     paint the track's loop over the dragged range
//   click empty lane       move cursor, clear selection      ruler             click/drag to seek
//   Ctrl+drag empty lane   rubber-band select (Shift adds)   Ctrl+click event  add/remove from selection
//   middle-button drag     grab and pan the view (like Shift+wheel, both axes)
//   ruler top strip        drag = new loop region; drag inside = move it; edges = resize; double-click = loop on/off
//   double-click event     loop region = event               wheel             scroll (Shift = horizontal, Ctrl = zoom)
// Hit-test priority on an event: fade corner, gain line, trim edge, body. The cursor shows which
// operation a drag will perform before the button goes down.
// Edits are previewed while dragging and committed as a single undoable action on mouse-up.

#include "AppContext.h"
#include "Theme.h"

namespace mcpdaw::ui
{
class TimelineView final : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           public juce::DragAndDropTarget,
                           private ProjectController::Listener,
                           private juce::ChangeListener,
                           private juce::Timer,
                           private juce::ScrollBar::Listener
{
public:
    explicit TimelineView (AppContext&);
    ~TimelineView() override;

    // --- editing commands (menu / keyboard) ------------------------------------------------------
    void splitAtCursor();
    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void selectAll();
    void nudgeSelection (int direction);
    void loopToSelection();
    void toggleSelectionLooping();
    /** U: reverse the selection (all reversed -> all forwards, otherwise all reversed). One undo step. */
    void toggleSelectionReverse();
    /** Asks the host to open the Embed Loop Metadata dialog for a WAV (set by MainComponent). */
    std::function<void (const juce::File&)> onEmbedLoopMetadata;
    bool hasSelection() const { return ! app.selectedEvents.isEmpty(); }
    bool canPaste() const { return ! clipboard.empty(); }

    // --- view ------------------------------------------------------------------------------------
    void zoomBy (double factor, float anchorX = -1.0f);
    void zoomToFit();
    /** zoomToFit plus track heights shrunk (down to a readable minimum) so every track is visible. */
    void fitAll();
    /** fitAll for the beat range [startBeat, endBeat) (endBeat <= startBeat: the whole arrangement). */
    void fitRange (double startBeat, double endBeat);
    bool followPlayhead = true;

    // --- Component --------------------------------------------------------------------------------
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void modifierKeysChanged (const juce::ModifierKeys&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;

    // --- drag & drop (Explorer files and the browser panel) ----------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    static constexpr int kHeaderWidth = 216;
    static constexpr int kRulerHeight = 46;
    static constexpr int kLoopStripHeight = 16;
    static constexpr int kScrollbarSize = 12;

private:
    AppContext& app;
    juce::ScrollBar hScroll { false }, vScroll { true };
    double pixelsPerBeat = 36.0, scrollBeat = 0.0;
    int scrollY = 0, trackHeight = 78;
    float lastPlayheadX = -1.0f;
    int seenLoadCount = -1;
    std::unique_ptr<juce::TextEditor> renameEditor;
    std::unique_ptr<juce::FileChooser> audioChooser;   // kept alive while the async dialog is open
    juce::String renameTrackId;

    // --- drag state ------------------------------------------------------------------------------
    enum class DragKind { None, Move, TrimStart, TrimEnd, Slip, EventGain, FadeIn, FadeOut,
                          Paint, Marquee, Pan, Seek, LoopCreate, LoopMove, LoopStart, LoopEnd, HeaderGain };
    struct Ghost { Event event; int row; };
    struct DragState
    {
        DragKind kind = DragKind::None;
        juce::Point<float> down;
        Tick downTick = 0;
        bool moved = false, copy = false, noSnap = false;
        bool wasSelected = false;                   // Ctrl+click toggle: event was selected before mouse-down
        juce::Point<float> current;                 // marquee corner (view coords)
        int downScrollY = 0;
        double downScrollBeat = 0.0;                // middle-button pan: view position at mouse-down                        // marquee anchor stays on the content while auto-scrolling
        juce::StringArray baseSelection;            // selection kept when Shift+Ctrl extends a marquee
        juce::String eventId;                       // the event under the mouse (primary)
        int downRow = -1, rowDelta = 0;
        Tick delta = 0;
        std::vector<Ghost> originals;               // selected events at mouse-down
        std::vector<Ghost> previews;                // what would be committed
        juce::String trackId, assetId;              // paint target
        Tick a = 0, b = 0;                          // paint / loop range
        double gainStart = 0;
        bool slipClamped = false;                   // slip preview reached the edge of a one-shot's source
    } drag;
    juce::String hoverEventId;                      // event under the pointer: shows its fade/gain handles

    int dropRow = -2;                               // row highlighted during file drops (-1 = new track)
    Tick dropTick = -1;

    struct ClipItem { Event event; int rowOffset; };
    std::vector<ClipItem> clipboard;
    Tick clipboardSpan = 0;

    // --- geometry ------------------------------------------------------------------------------------
    int laneRight() const noexcept { return getWidth() - kScrollbarSize; }
    int lanesBottom() const noexcept { return getHeight() - kScrollbarSize; }
    float xForTick (Tick t) const noexcept;
    Tick tickForX (float x) const noexcept;
    int rowTop (int row) const noexcept { return kRulerHeight + row * trackHeight - scrollY; }
    int rowAt (int y) const noexcept;
    juce::Rectangle<float> eventRect (const Event&, int row) const noexcept;
    /** The waveform area of an event (below its title strip). */
    static juce::Rectangle<float> waveArea (juce::Rectangle<float> eventBounds) noexcept;
    /** y of the event gain line: the top of the waveform area at 0 dB, lower as the gain drops. */
    static float gainLineY (const Event&, juce::Rectangle<float> wave) noexcept;
    /** Hit box of the fade-in (true) or fade-out handle. */
    juce::Rectangle<float> fadeHandle (const Event&, int row, bool fadeIn) const noexcept;
    bool showsEditHandles (const Event&, int row) const noexcept;
    Tick snapped (Tick t, bool bypass) const { return app.snapTicksFor (t, bypass); }
    Tick gridStep() const;

    struct HeaderLayout { juce::Rectangle<int> name, mute, solo, fx, gain, meter; };
    HeaderLayout headerLayout (int row) const;

    enum class Hit { Nothing, EventBody, EventStart, EventEnd, EventFadeIn, EventFadeOut, EventGain, EmptyLane, Header, Ruler, LoopStrip };
    struct HitResult { Hit kind = Hit::Nothing; int row = -1; const Event* event = nullptr; };
    HitResult hitTest (juce::Point<float>) const;

    // --- painting ---------------------------------------------------------------------------------
    void paintRuler (juce::Graphics&);
    void paintHeaders (juce::Graphics&);
    void paintLanes (juce::Graphics&);
    void paintEvent (juce::Graphics&, const Event&, int row, bool selected, bool ghost);
    void paintWaveform (juce::Graphics&, const Event&, const Asset&, juce::Rectangle<float> body, juce::Colour);
    void paintEdgeOverlays (juce::Graphics&, const Event&, int row, juce::Rectangle<float> wave, bool handles);
    void paintDragReadout (juce::Graphics&);
    /** Envelope (gain x musical fades, as drawn) at a timeline tick inside the event. */
    static float drawnEnvelope (const Event&, double tick) noexcept;

    // --- helpers -------------------------------------------------------------------------------------
    void updateScrollbars();
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void projectChanged (const Project&, bool) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showEventMenu (const Event&, int row);
    void showHeaderMenu (int row);
    void startRename (int row);
    void replaceTrackAudio (const juce::String& trackId, const juce::File&);
    void commitDrag();
    void buildMovePreview();
    void updateMarqueeSelection();
    void updateCursor (juce::Point<float> position, juce::ModifierKeys mods);
    juce::Rectangle<float> marqueeRect() const;
    juce::String trackAssetFor (const Track&, Tick near) const;
    void importFiles (const juce::StringArray&, int x, int y);
    juce::StringArray selectedIdsSorted() const;
};
} // namespace mcpdaw::ui
