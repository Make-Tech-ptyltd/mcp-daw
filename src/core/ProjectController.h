#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Owns the authoritative Project and its undo history.
//
// Threading: message thread only. The GUI and the MCP server both run their requests on the
// message thread, so there is exactly one writer and no locking. The realtime engine never reads
// this object; it receives immutable playback plans built from committed snapshots.
//
// Every mutation goes through apply(): copy -> edit -> validate -> commit. If the edit throws or
// validation fails, the live project is untouched (strong exception guarantee), which is exactly
// the behaviour an AI client needs when it sends a bad command.

#include "Project.h"
#include <functional>

namespace mcpdaw
{
/** Transport operations the command layer can invoke; implemented by AudioEngine. */
class TransportControl
{
public:
    virtual ~TransportControl() = default;
    virtual void play() = 0;
    virtual void stop() = 0;                     // stop and return to where playback started
    virtual void pause() = 0;                    // stop and stay here
    virtual void seekBeats (double beat) = 0;
    virtual bool isPlaying() const = 0;
    virtual double positionBeats() const = 0;
    virtual juce::var describeState() const = 0; // device, meters, CPU etc. for MCP
};

class ProjectController
{
public:
    ProjectController();

    const Project& project() const noexcept { return current; }

    /** Apply an edit as one undoable step (or as part of the open transaction).
        Returns whatever the edit function returns. Throws on failure; nothing is changed then. */
    juce::var apply (const juce::String& label, const std::function<juce::var (Project&)>& edit);

    /** Replace the whole project (new/open). Clears undo history. */
    void replaceProject (Project, juce::File file = {});

    // --- undo -----------------------------------------------------------------------------------
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    juce::String undoLabel() const { return canUndo() ? undoStack.back().label : juce::String(); }
    juce::String redoLabel() const { return canRedo() ? redoStack.back().label : juce::String(); }
    bool undo();
    bool redo();
    juce::StringArray historyLabels() const;

    /** Group several apply() calls into one undo step (slider drags, MCP batches). Nestable.
        `atomic`: listeners (audio engine, views) hear about the result once, when the outermost
        transaction ends - a batch is applied as one change, and its cost stays linear. Slider drags
        leave it false so the sound follows the drag. */
    void beginTransaction (const juce::String& label, bool atomic = false);
    void endTransaction();
    /** Atomic transactions skip the per-edit validation: call this before endTransaction (throws;
        roll back on failure). */
    void validateTransaction() const;
    /** Abort the outermost transaction: restore the state from before it began. */
    void rollbackTransaction();
    bool inTransaction() const noexcept { return transactionDepth > 0; }

    // --- file state -----------------------------------------------------------------------------
    juce::File currentFile() const { return file; }
    void markSaved (const juce::File& f) { file = f; savedRevision = revision; notify (false); }
    bool isDirty() const noexcept { return revision != savedRevision; }
    juce::int64 getRevision() const noexcept { return revision; }
    /** Increments whenever the whole project is replaced (new/open) - lets views reset zoom etc. */
    int getLoadCount() const noexcept { return loadCount; }

    // --- collaborators --------------------------------------------------------------------------
    TransportControl* transport = nullptr;       // null in headless tests

    /** Called after every committed change. `structural` is false for pure save-state changes. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void projectChanged (const Project&, bool structural) = 0;
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

private:
    struct Snapshot { Project project; juce::String label; juce::int64 revision; };

    Project current;
    std::vector<Snapshot> undoStack, redoStack;
    static constexpr size_t kMaxUndo = 256;

    int transactionDepth = 0;
    bool transactionAtomic = false, notifyPending = false;
    bool transactionHasEdit = false;
    Snapshot transactionStart;

    juce::int64 revision = 0, savedRevision = 0, nextRevision = 1;
    juce::File file;
    int loadCount = 0;
    juce::ListenerList<Listener> listeners;

    void notify (bool structural);
};
} // namespace mcpdaw
