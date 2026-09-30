// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "ProjectController.h"
#include <stdexcept>

namespace mcpdaw
{
ProjectController::ProjectController()
{
    validateProject (current);
}

juce::var ProjectController::apply (const juce::String& label, const std::function<juce::var (Project&)>& edit)
{
    if (transactionDepth > 0 && transactionAtomic)
    {
        // Atomic transactions (MCP batches) edit in place: transactionStart holds the state to roll back
        // to if any step fails, and nobody is notified until the end, so no per-step copy is needed.
        if (! transactionHasEdit)
        {
            undoStack.push_back ({ transactionStart.project, transactionStart.label, transactionStart.revision });
            transactionHasEdit = true;
            redoStack.clear();
            if (undoStack.size() > kMaxUndo) undoStack.erase (undoStack.begin());
        }
        juce::var result = edit (current);  // may throw -> the caller rolls the whole transaction back
        revision = nextRevision++;          // validated once, when the transaction commits (validateTransaction)
        notifyPending = true;
        return result;
    }

    Project draft = current;               // cheap: audio data is shared
    juce::var result = edit (draft);       // may throw -> nothing changed
    validateProject (draft);               // may throw -> nothing changed

    if (transactionDepth > 0)
    {
        // The whole transaction becomes one undo entry, pushed when the first edit lands.
        if (! transactionHasEdit)
        {
            undoStack.push_back ({ transactionStart.project, transactionStart.label, transactionStart.revision });
            transactionHasEdit = true;
            redoStack.clear();
        }
    }
    else
    {
        undoStack.push_back ({ current, label, revision });
        redoStack.clear();
    }
    if (undoStack.size() > kMaxUndo) undoStack.erase (undoStack.begin());

    current = std::move (draft);
    revision = nextRevision++;
    if (transactionDepth > 0 && transactionAtomic) notifyPending = true;
    else notify (true);
    return result;
}

void ProjectController::replaceProject (Project p, juce::File f)
{
    validateProject (p);
    current = std::move (p);
    undoStack.clear();
    redoStack.clear();
    transactionDepth = 0;
    transactionHasEdit = false;
    file = f;
    revision = nextRevision++;
    savedRevision = revision;   // a freshly opened or new project is clean
    ++loadCount;
    notify (true);
}

bool ProjectController::undo()
{
    if (undoStack.empty() || transactionDepth > 0) return false;
    auto snap = std::move (undoStack.back());
    undoStack.pop_back();
    redoStack.push_back ({ current, snap.label, revision });
    current = std::move (snap.project);
    revision = snap.revision;
    notify (true);
    return true;
}

bool ProjectController::redo()
{
    if (redoStack.empty() || transactionDepth > 0) return false;
    auto snap = std::move (redoStack.back());
    redoStack.pop_back();
    undoStack.push_back ({ current, snap.label, revision });
    current = std::move (snap.project);
    revision = snap.revision;
    notify (true);
    return true;
}

juce::StringArray ProjectController::historyLabels() const
{
    juce::StringArray labels;
    for (const auto& s : undoStack) labels.add (s.label);
    return labels;
}

void ProjectController::beginTransaction (const juce::String& label, bool atomic)
{
    if (transactionDepth++ == 0)
    {
        transactionStart = { current, label, revision };
        transactionHasEdit = false;
        transactionAtomic = atomic;
        notifyPending = false;
    }
}

void ProjectController::endTransaction()
{
    jassert (transactionDepth > 0);
    if (transactionDepth > 0 && --transactionDepth == 0 && notifyPending)
    {
        notifyPending = false;
        notify (true);
    }
}

void ProjectController::validateTransaction() const
{
    validateProject (current);
}

void ProjectController::rollbackTransaction()
{
    if (transactionDepth == 0) return;
    transactionDepth = 0;
    notifyPending = false;
    if (transactionHasEdit)
    {
        undoStack.pop_back();   // the entry pushed for this transaction
        current = transactionStart.project;
        revision = transactionStart.revision;
        transactionHasEdit = false;
        notify (true);
    }
}

void ProjectController::notify (bool structural)
{
    listeners.call ([&] (Listener& l) { l.projectChanged (current, structural); });
}
} // namespace mcpdaw
