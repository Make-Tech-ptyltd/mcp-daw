// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// mcp-daw-bridge behaviour: lazy start-up, launch on a real command, the single seat, taking turns,
// lock mode, reconnect-after-release. The bridge talks to a stand-in for the app over a real named
// pipe (unique per test run) so the connection, seat lock and handshake replay are all genuine.
#include "TestSupport.h"
#include "mcp/Bridge.h"
#include "mcp/CommandFeed.h"
#include "mcp/McpManifest.h"
#include "mcp/McpSession.h"
#include <thread>

namespace mcpdaw::test
{
namespace
{
juce::String uniqueName (const char* what)
{
    return juce::String ("mcp-daw-test-") + what + "-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
}

/** Thread-safe record of what the bridge wrote to its client. */
struct ClientSide
{
    std::mutex lock;
    juce::StringArray lines;

    void add (const juce::String& l) { const std::lock_guard<std::mutex> g (lock); lines.add (l); }
    int count() { const std::lock_guard<std::mutex> g (lock); return lines.size(); }
    bool anyContains (const juce::String& s)
    {
        const std::lock_guard<std::mutex> g (lock);
        for (const auto& l : lines) if (l.contains (s)) return true;
        return false;
    }
    /** Waits for the reply with this id (JSON text of the id, e.g. "7" or "\"a\""). */
    juce::var reply (const juce::String& id, int timeoutMs = 5000)
    {
        for (const auto until = juce::Time::currentTimeMillis() + timeoutMs; juce::Time::currentTimeMillis() < until;)
        {
            {
                const std::lock_guard<std::mutex> g (lock);
                for (const auto& l : lines)
                {
                    const auto v = juce::JSON::parse (l);
                    if (juce::JSON::toString (v["id"], true) == id) return v;
                }
            }
            juce::Thread::sleep (10);
        }
        return {};
    }
};

/** Stand-in for MCP DAW's pipe server: a real McpSession for the handshake (so the seat-mode _meta
    is exactly what the app sends), canned results for tool calls. Re-opens its pipe after each client. */
class FakeApp final : private juce::Thread
{
public:
    FakeApp (juce::String pipeName, juce::String appLockName)
        : juce::Thread ("fake app"), pipe (std::move (pipeName)), running (std::move (appLockName)) {}
    ~FakeApp() override { stop(); }

    void start (bool takeTurns, int idleMs)
    {
        turns = takeTurns;
        idle = idleMs;
        running.tryAcquire();   // like the app: "MCP DAW is running"
        startThread();
        for (int i = 0; i < 100 && ! listening; ++i) juce::Thread::sleep (10);
    }
    void stop()
    {
        stopThread (4000);
        const std::lock_guard<std::mutex> g (connLock);
        conn.reset();
        listening = false;
        running.release();
    }
    bool isRunning() const { return isThreadRunning(); }
    /** Simulates a pipe left behind by a crashed app: the pipe answers, but nobody holds "running". */
    void dropRunningLock() { running.release(); }

    int handshakes() { const std::lock_guard<std::mutex> g (logLock); return handshakeCount; }
    juce::StringArray calls() { const std::lock_guard<std::mutex> g (logLock); return toolCalls; }
    juce::StringArray clientNames() { const std::lock_guard<std::mutex> g (logLock); return names; }

    /** What the app does when the user flips the setting while a client is connected. */
    void announceSeatMode (bool takeTurns)
    {
        turns = takeTurns;
        const auto note = juce::JSON::toString (makeObject ({ { "jsonrpc", "2.0" }, { "method", "notifications/mcpdaw/seat" },
                                                              { "params", makeObject ({ { "takeTurns", takeTurns }, { "idleMs", idle.load() } }) } }), true);
        const std::lock_guard<std::mutex> g (connLock);
        if (conn != nullptr) conn->sendMessage (juce::MemoryBlock (note.toRawUTF8(), note.getNumBytesAsUTF8()));
    }

private:
    struct Conn final : juce::InterprocessConnection
    {
        explicit Conn (FakeApp& a) : juce::InterprocessConnection (false, mcp::kPipeMagic), app (a) {}
        ~Conn() override { disconnect(); }
        void connectionMade() override {}
        void connectionLost() override { lost = true; }
        void messageReceived (const juce::MemoryBlock& b) override
        {
            const auto reply = app.handle (b.toString());
            if (reply.isNotEmpty()) sendMessage (juce::MemoryBlock (reply.toRawUTF8(), reply.getNumBytesAsUTF8()));
        }
        FakeApp& app;
        std::atomic<bool> lost { false };
    };

    const juce::String pipe;
    mcp::NamedLock running;
    std::atomic<bool> turns { true }, listening { false };
    std::atomic<int> idle { 300 };
    std::mutex connLock, logLock;
    std::unique_ptr<Conn> conn;
    Session session;
    int handshakeCount = 0;
    juce::StringArray toolCalls, names;

    void run() override
    {
        while (! threadShouldExit())
        {
            {
                const std::lock_guard<std::mutex> g (connLock);
                if (conn == nullptr || conn->lost)
                {
                    conn.reset();
                    auto c = std::make_unique<Conn> (*this);
                    if (c->createPipe (pipe, -1, false)) conn = std::move (c);
                    listening = conn != nullptr;
                }
            }
            wait (20);
        }
    }

    juce::String handle (const juce::String& text)
    {
        const auto m = juce::JSON::parse (text);
        const auto method = m["method"].toString();
        if (method == "initialize")
        {
            McpSession s (session.registry, session.controller);
            s.initializeMeta = makeObject ({ { "mcpdaw/seat", makeObject ({ { "takeTurns", turns.load() }, { "idleMs", idle.load() } }) } });
            {
                const std::lock_guard<std::mutex> g (logLock);
                ++handshakeCount;
                names.add (m["params"]["clientInfo"]["name"].toString());
            }
            return s.handleText (text);
        }
        if (method == "tools/call")
        {
            {
                const std::lock_guard<std::mutex> g (logLock);
                toolCalls.add (m["params"]["name"].toString());
            }
            juce::Array<juce::var> content;
            content.add (makeObject ({ { "type", "text" }, { "text", "{\"ok\":true}" } }));
            return juce::JSON::toString (makeObject ({ { "jsonrpc", "2.0" }, { "id", m["id"] },
                                                       { "result", makeObject ({ { "content", content }, { "isError", false } }) } }), true);
        }
        return {};   // notifications
    }
};

juce::var liveManifest()
{
    Session s;
    registerAppActions (s.registry, nullptr);
    McpSession mcp (s.registry, s.controller);
    auto init = mcp.handle (juce::JSON::parse (R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"))["result"];
    init.getDynamicObject()->removeProperty ("protocolVersion");
    juce::Array<juce::var> versions;
    for (const auto& v : McpSession::supportedProtocolVersions()) versions.add (v);
    return makeObject ({ { "initialize", init }, { "protocolVersions", versions },
                         { "tools", mcp.handle (juce::JSON::parse (R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"))["result"] } });
}

juce::String initLine (const char* clientName)
{
    return juce::String (R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":")")
         + clientName + R"(","version":"1"}}})";
}
juce::String callLine (int id, const char* tool = "get_project_state")
{
    return R"({"jsonrpc":"2.0","id":)" + juce::String (id) + R"(,"method":"tools/call","params":{"name":")" + tool + R"(","arguments":{}}})";
}

/** Keeps calling tick() on the calling thread (the bridge's owner) for `ms`. */
void tickFor (mcp::Bridge& b, int ms)
{
    for (const auto until = juce::Time::currentTimeMillis() + ms; juce::Time::currentTimeMillis() < until;)
    {
        b.tick();
        juce::Thread::sleep (10);
    }
}
} // namespace

class BridgeTests final : public juce::UnitTest
{
public:
    BridgeTests() : juce::UnitTest ("MCP bridge (lazy, one seat, take turns)", "MCP") {}

    mcp::BridgeOptions options (const juce::String& pipe, const juce::String& seat)
    {
        mcp::BridgeOptions o;
        o.pipe = pipe;
        o.seat = seat;
        o.appLock = appLock;
        o.manifest = manifest;
        o.seatWaitMs = 3000;
        o.pipeRetryMs = 300;
        o.launchWaitMs = 3000;
        o.handshakeWaitMs = 2000;
        return o;
    }

    void runTest() override
    {
        manifest = liveManifest();
        const auto pipe = uniqueName ("pipe"), seat = uniqueName ("seat");
        appLock = uniqueName ("app");
        FakeApp app (pipe, appLock);
        auto quiet = [] (const juce::String&) {};

        beginTest ("the built-in manifest (served while MCP DAW is closed) matches the app's live tool list");
        {
            const auto built = juce::JSON::parse (mcp::manifestJson());
            expect (built.isObject(), "generated manifest parses");
            expectEquals (juce::JSON::toString (built["tools"], true), juce::JSON::toString (manifest["tools"], true));
            expectEquals (juce::JSON::toString (built["initialize"], true), juce::JSON::toString (manifest["initialize"], true));
        }

        beginTest ("start-up traffic (initialize, tools/list, ping...) never contacts or launches MCP DAW");
        int launches = 0;
        ClientSide claude;
        auto o = options (pipe, seat);
        o.launchApp = [&] { ++launches; app.start (true, 300); return true; };
        mcp::Bridge a (o, [&] (const juce::String& l) { claude.add (l); }, quiet);
        a.handleLine (initLine ("claude-ai"));
        a.handleLine (R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        a.handleLine (R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
        a.handleLine (R"({"jsonrpc":"2.0","id":2,"method":"ping"})");
        a.handleLine (R"({"jsonrpc":"2.0","id":3,"method":"resources/list"})");
        a.handleLine (R"({"jsonrpc":"2.0","id":4,"method":"logging/setLevel","params":{"level":"info"}})");
        tickFor (a, 100);
        expectEquals (launches, 0);
        expect (! app.isRunning() && ! a.isConnected());
        const auto init = claude.reply ("0");
        expectEquals (init["result"]["protocolVersion"].toString(), juce::String ("2025-06-18"));
        expectEquals (init["result"]["serverInfo"]["name"].toString(), juce::String ("mcp-daw"));
        expect (! init["result"].hasProperty ("_meta"), "seat details are for the bridge only");
        expectEquals (claude.reply ("1")["result"]["tools"].size(), manifest["tools"]["tools"].size());
        expect (claude.reply ("2")["result"].isObject());
        expectEquals (static_cast<int> (claude.reply ("4")["error"]["code"]), -32601);

        beginTest ("a real command launches MCP DAW, replays the handshake, then runs");
        a.handleLine (callLine (10, "create_track"));
        auto r = claude.reply ("10");
        expect (r.isObject() && ! static_cast<bool> (r["result"]["isError"]), juce::JSON::toString (r));
        expectEquals (launches, 1);
        expectEquals (app.handshakes(), 1);
        expectEquals (app.clientNames()[0], juce::String ("claude-ai"));
        expect (app.calls().contains ("create_track"));
        expect (! claude.anyContains ("mcpdaw-bridge-handshake"), "handshake replies never reach the client");
        expect (a.isConnected());

        beginTest ("take turns: the seat is handed back after idle, and the next command reconnects quietly");
        tickFor (a, 500);   // idle 300 ms in this test
        expect (! a.isConnected(), "released after idle");
        {
            mcp::NamedLock probe (seat);
            expect (probe.tryAcquire(), "seat is free for others");
        }
        a.handleLine (callLine (11));
        expect (! static_cast<bool> (claude.reply ("11")["result"]["isError"]));
        expectEquals (app.handshakes(), 2);
        expectEquals (launches, 1, "already running: no second launch");

        beginTest ("two clients take turns on the one seat");
        ClientSide codex;
        mcp::Bridge b (options (pipe, seat), [&] (const juce::String& l) { codex.add (l); }, quiet);
        b.handleLine (initLine ("codex-mcp-client"));
        const auto started = juce::Time::currentTimeMillis();
        std::thread other ([&] { b.handleLine (callLine (20)); });   // waits for the seat
        tickFor (a, 900);                                            // Claude goes quiet -> releases
        other.join();
        const auto rb = codex.reply ("20");
        expect (rb.isObject() && ! static_cast<bool> (rb["result"]["isError"]), juce::JSON::toString (rb));
        expect (juce::Time::currentTimeMillis() - started < 3000, "got the seat within the wait window");
        expect (! a.isConnected() && b.isConnected());
        expectEquals (app.clientNames()[app.clientNames().size() - 1], juce::String ("codex-mcp-client"));
        tickFor (b, 500);
        expect (! b.isConnected());

        beginTest ("a command landing right at the idle boundary still succeeds");
        {
            app.stop();
            app.start (true, 60);
            int failures = 0;
            for (int i = 0; i < 12; ++i)
            {
                a.handleLine (callLine (100 + i));
                if (static_cast<bool> (claude.reply (juce::String (100 + i))["result"]["isError"])) ++failures;
                tickFor (a, 50 + (i % 3) * 10);   // 50..70 ms: just before, at and after the 60 ms release
            }
            expectEquals (failures, 0);
            tickFor (a, 200);
        }

        beginTest ("lock mode: the first AI keeps the seat; others are told who has it");
        app.stop();
        app.start (false, 200);
        a.handleLine (callLine (30));
        expect (! static_cast<bool> (claude.reply ("30")["result"]["isError"]));
        tickFor (a, 500);
        expect (a.isConnected(), "locked: no idle release");
        {
            auto ob = options (pipe, seat);
            ob.seatWaitMs = 400;
            ClientSide script;
            mcp::Bridge c (ob, [&] (const juce::String& l) { script.add (l); }, quiet);
            c.handleLine (initLine ("dilla-beat"));
            c.handleLine (callLine (31));
            const auto busy = script.reply ("31");
            expect (static_cast<bool> (busy["result"]["isError"]));
            expect (busy["result"]["content"][0]["text"].toString().contains ("locked to Claude"), juce::JSON::toString (busy));
        }

        beginTest ("switching to take turns while connected applies at once (and is not shown to the model)");
        app.announceSeatMode (true);
        tickFor (a, 600);
        expect (! a.isConnected(), "released after idle once taking turns");
        expect (! claude.anyContains ("notifications/mcpdaw"), "seat notices are bridge-only");

        beginTest ("a pipe left behind without a running app is never used");
        {
            app.stop();
            app.start (true, 300);
            app.dropRunningLock();
            ClientSide lone;
            auto os = options (pipe, uniqueName ("seat2"));
            mcp::Bridge s2 (os, [&] (const juce::String& l) { lone.add (l); }, quiet);
            s2.handleLine (initLine ("claude-ai"));
            const auto before = app.handshakes();
            s2.handleLine (callLine (50));
            expect (lone.reply ("50")["result"]["content"][0]["text"].toString().contains ("isn't running"));
            expectEquals (app.handshakes(), before, "did not connect to the orphaned pipe");
        }

        beginTest ("MCP DAW closed and no way to launch it: a clear message, no hang");
        app.stop();
        juce::Thread::sleep (100);
        {
            ClientSide lone;
            auto on = options (uniqueName ("nopipe"), uniqueName ("noseat"));
            on.appLock = uniqueName ("noapp");
            mcp::Bridge d (on, [&] (const juce::String& l) { lone.add (l); }, quiet);
            d.handleLine (initLine ("claude-ai"));
            d.handleLine (callLine (40));
            const auto msg = lone.reply ("40")["result"]["content"][0]["text"].toString();
            expect (msg.contains ("isn't running"), msg);
        }
        a.shutdown();
        b.shutdown();

        beginTest ("friendly client names");
        expectEquals (mcp::friendlyClientName ("claude-ai"), juce::String ("Claude"));
        expectEquals (mcp::friendlyClientName ("local-agent-mode"), juce::String ("Claude"));
        expectEquals (mcp::friendlyClientName ("codex-mcp-client"), juce::String ("ChatGPT"));
        expectEquals (mcp::friendlyClientName ("dilla-beat"), juce::String ("dilla-beat"));
        expectEquals (mcp::friendlyClientName (""), juce::String ("AI client"));

        beginTest ("command ticker feed: repeats collapse, batches summarise, clients start new runs");
        {
            mcp::CommandFeed feed;
            expect (feed.push ("Claude", "create_event", {}, 1));
            for (int i = 0; i < 11; ++i) expect (! feed.push ("Claude", "create_event", {}, 2 + static_cast<juce::uint32> (i)));
            expect (feed.push ("Claude", "set_event_gain", {}, 20));
            juce::Array<juce::var> actions;
            for (int i = 0; i < 24; ++i)
                actions.add (makeObject ({ { "action", i % 2 ? "set_event_gain" : "create_event" }, { "arguments", makeObject ({}) } }));
            expect (feed.push ("Claude", "execute_batch", makeObject ({ { "actions", actions } }), 30));
            expect (feed.push ("Claude", "execute_batch", makeObject ({ { "actions", actions } }), 31), "batches never merge");
            expect (feed.push ("ChatGPT", "play", {}, 40));
            const auto& e = feed.entries();
            expectEquals (static_cast<int> (e.size()), 5);
            expectEquals (e[0].text(), juce::String ("create_event ") + juce::String::charToString (0x00D7) + "12");
            expectEquals (e[2].text(), juce::String ("execute_batch (24) create_event, set_event_gain"));
            expectEquals (e[4].client, juce::String ("ChatGPT"));
            for (int i = 0; i < 100; ++i) feed.push ("x", "t" + juce::String (i), {}, 50);
            expectEquals (static_cast<int> (feed.entries().size()), static_cast<int> (mcp::CommandFeed::kMaxEntries));
        }

        beginTest ("the app side: client name, seat mode in the handshake, tool-call hook");
        {
            Session s;
            McpSession session (s.registry, s.controller);
            session.initializeMeta = makeObject ({ { "mcpdaw/seat", makeObject ({ { "takeTurns", false }, { "idleMs", 20000 } }) } });
            juce::StringArray seen;
            session.onToolCall = [&] (const juce::String& tool, const juce::var&) { seen.add (tool); };
            const auto reply = juce::JSON::parse (session.handleText (initLine ("claude-ai")));
            expectEquals (session.clientName(), juce::String ("claude-ai"));
            expect (! static_cast<bool> (reply["result"]["_meta"]["mcpdaw/seat"]["takeTurns"]));
            session.handleText (callLine (1, "create_track"));
            expect (seen.contains ("create_track"));
        }
    }

private:
    juce::var manifest;
    juce::String appLock;
};

static BridgeTests bridgeTests;
} // namespace mcpdaw::test
