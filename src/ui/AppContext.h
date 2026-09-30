#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Services and shared UI state handed to every panel.
//
// UI components never mutate the Project directly: they call run("action", args) exactly like an
// MCP client, which keeps GUI and AI behaviour identical and every edit undoable. Continuous
// gestures (fader drags, knob turns) are wrapped in begin/endGesture so they form one undo step.

#include "core/Actions.h"
#include "core/ProjectJson.h"
#include "engine/AudioEngine.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace mcpdaw::ui
{
class AppContext
{
public:
    AppContext (ProjectController& c, ActionRegistry& r, AudioEngine& e, juce::ApplicationProperties& p)
        : controller (c), registry (r), engine (e), properties (p) {}

    ProjectController& controller;
    ActionRegistry& registry;
    AudioEngine& engine;
    juce::ApplicationProperties& properties;

    const Project& project() const noexcept { return controller.project(); }

    /** Execute an action; errors are reported to the status bar and return void. */
    juce::var run (const juce::String& action, const juce::var& args = {})
    {
        try
        {
            return registry.execute (controller, action, args.isObject() ? args : makeObject ({}));
        }
        catch (const std::exception& e)
        {
            status (e.what(), true);
            return {};
        }
    }

    void beginGesture (const juce::String& label) { controller.beginTransaction (label); }
    void endGesture() { if (controller.inTransaction()) controller.endTransaction(); }

    // --- status line ---------------------------------------------------------------------------
    std::function<void (const juce::String&, bool isError)> onStatus;
    void status (const juce::String& message, bool isError = false) { if (onStatus) onStatus (message, isError); }

    // --- browser / audition ---------------------------------------------------------------------
    juce::File browserFolder;            // folder shown in the browser panel ("Replace audio" lists it)
    /** Audition a file through the preview voice (never touches the project). */
    void previewFile (const juce::File& f)
    {
        if (f.getSize() > 64 * 1024 * 1024) { status ("File too large to audition"); return; }
        try { engine.startPreview (loadAudioFile (f)); status ("Audition: " + f.getFileName()); }
        catch (const std::exception& e) { status (e.what(), true); }
    }

    // --- selection (UI state, not part of the undoable model) -----------------------------------
    juce::String selectedTrack;
    juce::StringArray selectedEvents;
    juce::String fxTarget;               // track id or "master"; empty = follow selected track
    juce::ChangeBroadcaster selectionChanged;

    void selectTrack (const juce::String& id)
    {
        if (selectedTrack == id) return;
        selectedTrack = id;
        selectionChanged.sendChangeMessage();
    }
    void selectEvents (const juce::StringArray& ids)
    {
        selectedEvents = ids;
        selectionChanged.sendChangeMessage();
    }

    // --- meters: polled once per UI frame by the main component, read by any panel ---------------
    struct Peak { float left = 0, right = 0; };
    std::map<juce::String, Peak> trackMeters;
    Peak masterMeter;
    void pollMeters()
    {
        for (const auto& t : project().tracks)
        {
            const auto p = engine.takeTrackPeak (t.id);
            trackMeters[t.id] = { p.left, p.right };
        }
        const auto m = engine.takeMasterPeak();
        masterMeter = { m.left, m.right };
    }
    Peak meterFor (const juce::String& trackId) const
    {
        auto it = trackMeters.find (trackId);
        return it == trackMeters.end() ? Peak() : it->second;
    }

    // --- editing preferences shared by transport bar and timeline --------------------------------
    SnapGrid snap = SnapGrid::Beat;
    juce::ChangeBroadcaster preferencesChanged;
    void setSnap (SnapGrid g) { snap = g; preferencesChanged.sendChangeMessage(); }
    Tick snapTicksFor (Tick t, bool bypass = false) const { return bypass ? t : snapTicks (t, gridTicks (snap)); }
};
} // namespace mcpdaw::ui
