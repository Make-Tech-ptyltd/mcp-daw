// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "McpPipeServer.h"
#include "../core/ProjectJson.h"

namespace mcpdaw
{
McpPipeServer::McpPipeServer (ActionRegistry& r, ProjectController& c, juce::String pipeName)
    : juce::InterprocessConnection (true, mcp::kPipeMagic), registry (r), controller (c), pipe (std::move (pipeName))
{
}

McpPipeServer::~McpPipeServer()
{
    stopTimer();
    disconnect();
}

void McpPipeServer::timerCallback()
{
    if (runningLock.tryAcquire()) stopTimer();
}

bool McpPipeServer::start()
{
    // Bridges check this lock before touching the pipe. If a bridge happens to be probing it at this
    // very moment, try again shortly (the timer stops once we hold it).
    if (! runningLock.tryAcquire()) startTimer (500);
    newSession();
    // The app is single-instance (JUCEApplication::moreThanOneInstanceAllowed() == false), so we can
    // always (re)claim the name. mustNotExist=false also recovers from stale POSIX FIFOs after a crash.
    return createPipe (pipe, -1, false);
}

juce::var McpPipeServer::seatMeta() const
{
    // Read by mcp-daw-bridge (never shown to the model): how it should share the seat.
    return makeObject ({ { "mcpdaw/seat", makeObject ({ { "takeTurns", takeTurns }, { "idleMs", mcp::kTakeTurnsIdleMs } }) } });
}

void McpPipeServer::newSession()
{
    session = std::make_unique<McpSession> (registry, controller);   // fresh MCP handshake per client
    session->initializeMeta = seatMeta();
    session->onToolCall = [this] (const juce::String& tool, const juce::var& args)
    {
        if (onToolCall) onToolCall (clientLabel(), tool, args);
    };
}

void McpPipeServer::setTakeTurns (bool shouldTakeTurns)
{
    takeTurns = shouldTakeTurns;
    if (session != nullptr) session->initializeMeta = seatMeta();
    if (clientConnected)
    {
        // Tell the connected bridge right away (it filters this out; the model never sees it).
        const auto note = juce::JSON::toString (makeObject ({ { "jsonrpc", "2.0" }, { "method", "notifications/mcpdaw/seat" },
                                                              { "params", seatMeta()["mcpdaw/seat"] } }), true);
        sendMessage (juce::MemoryBlock (note.toRawUTF8(), note.getNumBytesAsUTF8()));
    }
}

juce::String McpPipeServer::clientLabel() const
{
    return clientConnected ? mcp::friendlyClientName (session != nullptr ? session->clientName() : juce::String()) : juce::String();
}

void McpPipeServer::connectionMade()
{
    // NB: JUCE calls this as soon as the pipe is CREATED, not when a client opens it, so it must
    // never spawn further pipes (doing so looped forever and froze Windows). A client is known to
    // be present only once it sends something - see messageReceived().
    newSession();
}

void McpPipeServer::connectionLost()
{
    const bool wasConnected = clientConnected;
    clientConnected = false;
    if (wasConnected && onStatusChanged) onStatusChanged();
    // Re-open the pipe for the next client once this callback has unwound.
    juce::MessageManager::callAsync ([weak = juce::WeakReference<McpPipeServer> (this)]
    {
        if (auto* self = weak.get())
        {
            self->disconnect();
            self->createPipe (self->pipe, -1, false);
        }
    });
}

void McpPipeServer::messageReceived (const juce::MemoryBlock& block)
{
    const auto before = session->clientName();
    const bool wasConnected = clientConnected;
    clientConnected = true;

    const auto reply = session->handleText (block.toString());
    if (reply.isNotEmpty())
        sendMessage (juce::MemoryBlock (reply.toRawUTF8(), reply.getNumBytesAsUTF8()));

    if (session->clientName().isNotEmpty()) lastLabel = clientLabel();
    if ((! wasConnected || session->clientName() != before) && onStatusChanged) onStatusChanged();
}
} // namespace mcpdaw
