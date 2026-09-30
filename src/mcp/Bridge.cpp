// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Bridge.h"

namespace mcpdaw::mcp
{
namespace
{
constexpr const char* kHandshakeId = "mcpdaw-bridge-handshake";
constexpr const char* kSeatNotification = "notifications/mcpdaw/seat";
constexpr int kMethodNotFound = -32601, kParseError = -32700;

juce::int64 nowMs() { return juce::Time::currentTimeMillis(); }

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [k, v] : props) o->setProperty (k, v);
    return juce::var (o);
}

juce::String oneLine (const juce::var& v) { return juce::JSON::toString (v, true); }

} // namespace

juce::File Bridge::holderFile (const juce::String& seatName)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (seatName + ".holder");
}

// =================================================================================================
// Link: the pipe connection. Callbacks run on its own thread (the bridge has no message loop).
// =================================================================================================
class Bridge::Link final : public juce::InterprocessConnection
{
public:
    explicit Link (Bridge& b) : juce::InterprocessConnection (false, kPipeMagic), bridge (b) {}
    ~Link() override { disconnect(); }

    void connectionMade() override {}
    void connectionLost() override { bridge.connectionDropped(); }
    void messageReceived (const juce::MemoryBlock& block) override { bridge.receivedFromApp (block.toString()); }

private:
    Bridge& bridge;
};

// =================================================================================================
// Bridge
// =================================================================================================
Bridge::Bridge (BridgeOptions o, Output out, Log l)
    : options (std::move (o)), output (std::move (out)), log (std::move (l)), seat (options.seat)
{
}

Bridge::~Bridge() { shutdown(); }

int Bridge::pendingReplies() const
{
    const std::lock_guard<std::mutex> guard (pendingLock);
    return static_cast<int> (pending.size());
}

void Bridge::replyLocally (const juce::var& id, const juce::var& result)
{
    output (oneLine (object ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } })));
}

void Bridge::replyError (const juce::var& id, int code, const juce::String& message)
{
    output (oneLine (object ({ { "jsonrpc", "2.0" }, { "id", id },
                               { "error", object ({ { "code", code }, { "message", message } }) } })));
}

void Bridge::replyToolError (const juce::var& id, const juce::String& message)
{
    // A tool result (not a protocol error) so the model reads the message and can act on it.
    juce::Array<juce::var> content;
    content.add (object ({ { "type", "text" }, { "text", message } }));
    replyLocally (id, object ({ { "content", content }, { "isError", true } }));
}

juce::var Bridge::localInitializeResult (const juce::String& requested) const
{
    const auto& versions = options.manifest["protocolVersions"];
    juce::String version = versions.size() > 0 ? versions[0].toString() : juce::String ("2025-06-18");
    for (int i = 0; i < versions.size(); ++i)
        if (versions[i].toString() == requested) version = requested;

    auto result = juce::JSON::parse (oneLine (options.manifest["initialize"]));   // deep copy
    if (! result.isObject()) result = object ({});
    result.getDynamicObject()->setProperty ("protocolVersion", version);
    return result;
}

// -------------------------------------------------------------------------------------------------
void Bridge::handleLine (const juce::String& rawLine)
{
    const auto line = rawLine.trim();
    if (line.isEmpty()) return;

    juce::var message;
    if (juce::JSON::parse (line, message).failed())
    {
        replyError ({}, kParseError, "Parse error");
        return;
    }

    if (message.isArray())   // JSON-RPC batch: rare; treat it as real work
    {
        if (const auto problem = ensureConnected(); problem.isNotEmpty())
        {
            for (const auto& m : *message.getArray())
                if (m.hasProperty ("id")) replyToolError (m["id"], problem);
            return;
        }
        forward (line, message);
        return;
    }
    if (! message.isObject()) return;

    const auto method = message["method"].toString();
    if (method.isEmpty()) return;   // a response to a server request: we never send any

    if (! message.hasProperty ("id"))   // notification
    {
        if (method == "notifications/initialized") clientSaidInitialized = true;
        if (connected && ! lost) sendToApp (line);
        return;
    }

    const auto id = message["id"];
    if (method == "initialize")
    {
        storedInitialize = message;
        clientLabel = friendlyClientName (message["params"]["clientInfo"]["name"].toString());
        replyLocally (id, localInitializeResult (message["params"]["protocolVersion"].toString()));
        if (connected) release ("client re-initialised");   // next call replays the new handshake
        return;
    }
    if (method == "ping")                     { replyLocally (id, object ({})); return; }
    if (method == "tools/list")               { replyLocally (id, options.manifest["tools"]); return; }
    if (method == "resources/list")           { replyLocally (id, object ({ { "resources", juce::Array<juce::var>() } })); return; }
    if (method == "resources/templates/list") { replyLocally (id, object ({ { "resourceTemplates", juce::Array<juce::var>() } })); return; }
    if (method == "prompts/list")             { replyLocally (id, object ({ { "prompts", juce::Array<juce::var>() } })); return; }
    if (method != "tools/call")               { replyError (id, kMethodNotFound, "Method not found: " + method); return; }

    // A real command: this is the only thing that may connect to (or launch) MCP DAW.
    if (const auto problem = ensureConnected(); problem.isNotEmpty())
    {
        replyToolError (id, problem);
        return;
    }
    forward (line, message);
}

void Bridge::forward (const juce::String& line, const juce::var& message)
{
    auto track = [this] (const juce::var& m)
    {
        if (m.isObject() && m.hasProperty ("id"))
        {
            const std::lock_guard<std::mutex> guard (pendingLock);
            pending.insert (oneLine (m["id"]));
        }
    };
    if (message.isArray()) for (const auto& m : *message.getArray()) track (m);
    else track (message);
    lastActivity = nowMs();

    if (sendToApp (line)) return;

    // The app went away between calls (closed, restarted): reconnect once and resend.
    release ("connection lost");
    if (const auto problem = ensureConnected(); problem.isEmpty() && sendToApp (line)) return;
    failPending ("MCP DAW closed the connection. Please try again.");
}

bool Bridge::sendToApp (const juce::String& text)
{
    return link != nullptr && ! lost && link->sendMessage (juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8()));
}

// -------------------------------------------------------------------------------------------------
juce::String Bridge::busyMessage() const
{
    const auto info = juce::StringArray::fromLines (holderFile (options.seat).loadFileAsString());
    const auto who = info.size() > 0 && info[0].trim().isNotEmpty() ? info[0].trim() : juce::String ("another AI or script");
    if (info.size() > 1 && info[1].trim() == "lock")
        return "MCP DAW is locked to " + who + " until it disconnects. Ask the user, or try again later.";
    return "MCP DAW is in use by " + who + " right now. It frees up after "
         + juce::String (idleMs.load() / 1000) + " s without commands - please try again shortly.";
}

void Bridge::writeHolderFile() const
{
    holderFile (options.seat).replaceWithText (clientLabel + "\n" + (takeTurns ? "turns" : "lock") + "\n");
}

juce::String Bridge::ensureConnected()
{
    if (connected && ! lost) return {};
    if (connected) release ("connection lost");

    // 1. The seat: one client at a time. Another client in take-turns mode frees it after idle.
    for (const auto giveUp = nowMs() + options.seatWaitMs; ! seat.tryAcquire();)
    {
        if (nowMs() >= giveUp) return busyMessage();
        juce::Thread::sleep (200);
    }
    writeHolderFile();

    // 2. The pipe. The app re-opens it a moment after the previous client leaves, so retry briefly
    //    before concluding MCP DAW isn't running.
    // Note: connectToPipe's timeout is the RECEIVE timeout per message, not a connect timeout; it must
    // be infinite (-1) or a long render_mix would look like a dropped connection.
    link = std::make_unique<Link> (*this);
    lost = false;
    auto tryConnect = [this] (int forMs)
    {
        for (const auto until = nowMs() + forMs;;)
        {
            // Only touch the pipe while the app is up: a stale pipe file (POSIX, after a crash) would
            // otherwise accept the connection and block the first write forever.
            if (NamedLock::isHeldAnywhere (options.appLock) && link->connectToPipe (options.pipe, -1)) return true;
            if (nowMs() >= until) return false;
            juce::Thread::sleep (150);
        }
    };
    if (! NamedLock::isHeldAnywhere (options.appLock) || ! tryConnect (options.pipeRetryMs))
    {
        if (options.launchApp == nullptr)
        {
            release ({});
            return "MCP DAW isn't running. Please ask the user to open MCP DAW, then try again.";
        }
        log ("MCP DAW isn't running - starting it for " + clientLabel);
        if (! options.launchApp())
        {
            release ({});
            return "MCP DAW isn't running and could not be started automatically. Please ask the user to open it.";
        }
        if (! tryConnect (options.launchWaitMs))
        {
            release ({});
            return "MCP DAW was started but was not ready within " + juce::String (options.launchWaitMs / 1000)
                 + " s. Please try again in a moment.";
        }
    }

    // 3. Replay the client's handshake so the app knows who this is and which protocol to speak.
    auto init = storedInitialize.isObject() ? juce::JSON::parse (oneLine (storedInitialize))
                                            : object ({ { "jsonrpc", "2.0" }, { "method", "initialize" },
                                                        { "params", object ({ { "protocolVersion", "2025-06-18" },
                                                                              { "clientInfo", object ({ { "name", "unknown" } }) } }) } });
    init.getDynamicObject()->setProperty ("id", kHandshakeId);
    handshakeReply.reset();
    if (! sendToApp (oneLine (init)))
    {
        release ({});
        return "Could not talk to MCP DAW. Please try again.";
    }
    handshakeReply.wait (options.handshakeWaitMs);   // carries the seat mode; late replies are still filtered
    sendToApp (R"({"jsonrpc":"2.0","method":"notifications/initialized"})");

    connected = true;
    lastActivity = nowMs();
    writeHolderFile();   // now with the mode the app asked for
    log ("connected to MCP DAW for " + clientLabel + (takeTurns ? " (taking turns)" : " (seat locked)"));
    return {};
}

void Bridge::release (const juce::String& why)
{
    if (seat.isHeld()) holderFile (options.seat).deleteFile();
    if (link != nullptr)
    {
        auto old = std::move (link);   // destroying it disconnects; Link callbacks may still fire meanwhile
        old.reset();
    }
    if (connected && why.isNotEmpty()) log ("released MCP DAW (" + why + ")");
    connected = false;
    lost = false;
    seat.release();
}

void Bridge::failPending (const juce::String& message)
{
    std::set<juce::String> ids;
    {
        const std::lock_guard<std::mutex> guard (pendingLock);
        ids.swap (pending);
    }
    for (const auto& id : ids) replyToolError (juce::JSON::parse (id), message);
}

void Bridge::tick()
{
    if (connected && lost)
    {
        failPending ("MCP DAW closed the connection (it may have been closed). Please try again.");
        release ({});
        log ("MCP DAW closed the connection");
        return;
    }
    if (connected && takeTurns && pendingReplies() == 0 && nowMs() - lastActivity.load() >= idleMs.load())
        release (juce::String (idleMs.load() / 1000) + " s idle - seat free for other clients");
}

void Bridge::shutdown()
{
    release ({});
}

// -------------------------------------------------------------------------------------------------
// Connection thread
// -------------------------------------------------------------------------------------------------
void Bridge::receivedFromApp (const juce::String& text)
{
    // Bridge-only traffic: the handshake reply and seat-mode updates. Never shown to the client.
    if (text.contains (kHandshakeId) || text.contains (kSeatNotification))
    {
        const auto msg = juce::JSON::parse (text);
        if (msg["id"].toString() == kHandshakeId || msg["method"].toString() == kSeatNotification)
        {
            const auto seatInfo = msg.hasProperty ("method") ? msg["params"] : msg["result"]["_meta"]["mcpdaw/seat"];
            if (seatInfo.isObject())
            {
                takeTurns = static_cast<bool> (seatInfo["takeTurns"]);
                if (static_cast<int> (seatInfo["idleMs"]) > 0) idleMs = static_cast<int> (seatInfo["idleMs"]);
            }
            if (msg.hasProperty ("id")) handshakeReply.signal();
            else lastActivity = nowMs();   // a mode change restarts the idle clock
            return;
        }
    }

    // Replies are single lines already; guarantee it for the stdio framing.
    const auto line = text.replaceCharacters ("\r\n", "  ");
    const auto msg = juce::JSON::parse (line);
    {
        const std::lock_guard<std::mutex> guard (pendingLock);
        if (msg.isArray()) { for (const auto& m : *msg.getArray()) pending.erase (oneLine (m["id"])); }
        else if (msg.hasProperty ("id")) pending.erase (oneLine (msg["id"]));
    }
    lastActivity = nowMs();
    output (line);
}

void Bridge::connectionDropped()
{
    lost = true;
}
} // namespace mcpdaw::mcp
