#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Named-pipe endpoint inside the running application.
//
// Why a pipe: an MCP client (Claude Desktop, Codex, Cursor...) launches mcp-daw-bridge over stdio;
// the bridge forwards to this pipe, so the agent drives the SAME live session the user is working
// in, instead of a hidden second instance. Named pipes are local-only (no network listener, no
// port, nothing reachable from another machine) and per-user by name.
//
// One seat: one client at a time. Bridges queue for the seat (an OS lock they hold while connected)
// and, in "take turns" mode, hand it back after 20 s idle, so Claude, ChatGPT and scripts can take
// turns. The pipe is recreated after each client leaves. Messages are delivered on the message
// thread, exactly like GUI events, so actions never race with the UI.

#include "McpSession.h"
#include "NamedLock.h"
#include "PipeName.h"
#include <juce_events/juce_events.h>

namespace mcpdaw
{
class McpPipeServer final : private juce::InterprocessConnection, private juce::Timer
{
public:
    McpPipeServer (ActionRegistry&, ProjectController&, juce::String pipeName = mcp::pipeName());
    ~McpPipeServer() override;

    bool start();
    juce::String getPipeName() const { return pipe; }

    /** Take turns (default): the connected AI's bridge lets go after 20 s idle so another AI or
        script can connect. Off = lock: the first AI keeps the seat until it disconnects. Applies
        immediately to a connected client. */
    void setTakeTurns (bool);
    bool getTakeTurns() const noexcept { return takeTurns; }

    bool isClientConnected() const noexcept { return clientConnected; }
    /** Friendly name of the connected client ("Claude", "ChatGPT", "dilla-beat"...). */
    juce::String clientLabel() const;
    /** The most recent client, kept after it disconnects (empty if nobody has connected yet). */
    juce::String lastClientLabel() const { return lastLabel; }

    /** Message thread: a client connected/disconnected or identified itself. */
    std::function<void()> onStatusChanged;
    /** Message thread: a tool is about to run for `client`. */
    std::function<void (const juce::String& client, const juce::String& tool, const juce::var& arguments)> onToolCall;

private:
    ActionRegistry& registry;
    ProjectController& controller;
    const juce::String pipe;
    std::unique_ptr<McpSession> session;
    bool clientConnected = false, takeTurns = true;
    juce::String lastLabel;
    mcp::NamedLock runningLock { mcp::appLockName() };   // "MCP DAW is running", for the bridges

    void timerCallback() override;
    void newSession();
    juce::var seatMeta() const;
    void connectionMade() override;
    void connectionLost() override;
    void messageReceived (const juce::MemoryBlock&) override;

    JUCE_DECLARE_WEAK_REFERENCEABLE (McpPipeServer)
};
} // namespace mcpdaw
