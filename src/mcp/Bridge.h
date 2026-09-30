#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// The logic of mcp-daw-bridge, kept in a class so the tests can drive it in-process.
//
// Lazy by design. An MCP client (Claude Desktop, Codex...) starts the bridge when IT starts, and
// asks for the tool list straight away. Neither of those may touch MCP DAW: the bridge answers
// initialize / tools/list / ping from a built-in copy of the server's manifest (generated from the
// app's own action registry at build time). Only a real `tools/call` makes the bridge:
//
//   1. take the MCP seat (an OS-wide lock, one holder at a time), waiting up to ~25 s for it;
//   2. connect to MCP DAW's pipe - launching MCP DAW first if it isn't running (the app holds a
//      "running" lock, so this never mistakes a busy app, or a stale pipe file, for a closed one);
//   3. replay the client's initialize handshake to the app (its replies never reach the client);
//   4. forward the call.
//
// Seat sharing is decided by the app and sent in the handshake: in "take turns" mode the bridge
// disconnects and gives the seat back after 20 s with no traffic, and quietly reconnects on the
// client's next call; in "lock" mode it keeps the seat until the client exits.
//
// Threading: handleLine() and tick() are called from ONE owner thread (all seat and connection
// changes happen there, so a call can never race a release). Replies arrive on the connection's
// thread and go straight to `output`.

#include "NamedLock.h"
#include "PipeName.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <mutex>
#include <set>

namespace mcpdaw::mcp
{
struct BridgeOptions
{
    juce::String pipe = pipeName();
    juce::String seat = seatName();
    juce::String appLock = appLockName();   // held by MCP DAW while it runs
    /** { "initialize": <result without protocolVersion>, "protocolVersions": [...], "tools": <tools/list result> } */
    juce::var manifest;
    /** Starts MCP DAW. Null = never launch (the call reports that MCP DAW isn't running). */
    std::function<bool()> launchApp;
    /** How long a call waits for another client's turn to end. Longer than the idle release, so in
        take-turns mode a waiting AI normally just gets through instead of having to retry. */
    int seatWaitMs = kTakeTurnsIdleMs + 5000;
    int pipeRetryMs = 1500;       // the app re-opens its pipe a moment after each client leaves
    int launchWaitMs = 30000;     // first launch includes audio-device start-up
    int handshakeWaitMs = 10000;
};

class Bridge
{
public:
    using Output = std::function<void (const juce::String& jsonLine)>;
    using Log = std::function<void (const juce::String& message)>;

    Bridge (BridgeOptions, Output output, Log log);
    ~Bridge();

    /** One JSON-RPC line from the client. May block while waiting for the seat or for MCP DAW to start. */
    void handleLine (const juce::String& line);
    /** Call every ~100 ms: releases the seat after idle (take-turns mode) and tidies up lost connections. */
    void tick();
    /** Disconnect and free the seat (end of input / shutdown). */
    void shutdown();

    bool isConnected() const noexcept { return connected; }
    int pendingReplies() const;
    bool takesTurns() const noexcept { return takeTurns; }

private:
    class Link;
    BridgeOptions options;
    Output output;
    Log log;
    NamedLock seat;
    std::unique_ptr<Link> link;
    bool connected = false;

    juce::var storedInitialize;          // the client's initialize request, replayed on every connect
    bool clientSaidInitialized = false;
    juce::String clientLabel = "AI client";

    std::atomic<bool> takeTurns { true }, lost { false };
    std::atomic<int> idleMs { kTakeTurnsIdleMs };
    std::atomic<juce::int64> lastActivity { 0 };
    juce::WaitableEvent handshakeReply;
    mutable std::mutex pendingLock;
    std::set<juce::String> pending;      // ids (as JSON text) of calls awaiting a reply

    void replyLocally (const juce::var& id, const juce::var& result);
    void replyError (const juce::var& id, int code, const juce::String& message);
    void replyToolError (const juce::var& id, const juce::String& message);
    juce::var localInitializeResult (const juce::String& requestedVersion) const;

    /** Seat + pipe + handshake. Returns an empty string on success, else a message for the model. */
    juce::String ensureConnected();
    bool sendToApp (const juce::String& text);
    void forward (const juce::String& line, const juce::var& message);
    void release (const juce::String& why);
    void failPending (const juce::String& message);
    void writeHolderFile() const;
    /** "Claude\nturns": who holds the seat and how, for bridges waiting on it. */
    static juce::File holderFile (const juce::String& seatName);
    juce::String busyMessage() const;

    void receivedFromApp (const juce::String& text);
    void connectionDropped();
};
} // namespace mcpdaw::mcp
