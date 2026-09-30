#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// A per-user, machine-wide lock with one holder at a time: an OS lock (a named mutex on Windows, a
// lock file elsewhere) plus an in-process guard, because POSIX file locks don't exclude two holders
// inside the same process. The OS frees it if the holder crashes.
//
// Used for the MCP seat (held by the bridge that is talking to MCP DAW) and for "MCP DAW is
// running" (held by the app for its lifetime), so a bridge can tell "busy" from "not running"
// without touching the pipe.
#include <juce_core/juce_core.h>

namespace mcpdaw::mcp
{
class NamedLock
{
public:
    explicit NamedLock (juce::String name);
    ~NamedLock();

    bool tryAcquire();
    void release();
    bool isHeld() const noexcept { return held; }

    /** True if anyone (this process or another) holds `name` right now. */
    static bool isHeldAnywhere (const juce::String& name);

private:
    const juce::String name;
    std::unique_ptr<juce::InterProcessLock> lock;
    bool held = false;
};
} // namespace mcpdaw::mcp
