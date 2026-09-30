// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Demo script player (mcp-daw-bridge --script): parsing, variables, the musical clock, and a whole
// run against a stand-in for MCP DAW with a simulated clock.
#include "mcp/ScriptPlayer.h"
#include <juce_core/juce_core.h>

namespace mcpdaw::test
{
namespace
{
juce::var obj (std::initializer_list<std::pair<juce::Identifier, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [k, v] : props) o->setProperty (k, v);
    return juce::var (o);
}

/** Pretends to be MCP DAW: a transport that really moves with the simulated clock. */
struct FakeApp
{
    double now = 1000;            // ms
    bool playing = false;
    double playStartMs = 0, beatAtStart = 0, bpm = 120;
    int tracks = 0;
    juce::StringArray calls;       // tool names in order
    juce::Array<double> callBeats; // beats played when each call arrived
    juce::Array<juce::var> callArgs;

    double beatNow() const { return playing ? beatAtStart + (now - playStartMs) * bpm / 60000.0 : beatAtStart; }

    juce::var request (const juce::var& msg)
    {
        now += 4;   // round trip
        const auto id = msg["id"];
        if (msg["method"] == "initialize")
            return obj ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", obj ({ { "protocolVersion", "2025-06-18" } }) } });
        const auto tool = msg["params"]["name"].toString();
        const auto args = msg["params"]["arguments"];
        calls.add (tool);
        callBeats.add (beatNow());
        callArgs.add (args);
        juce::var result;
        if (tool == "create_track") result = obj ({ { "trackId", "track-" + juce::String (++tracks) } });
        else if (tool == "play")
        {
            beatAtStart = static_cast<double> (args.getProperty ("fromBeat", 0.0));
            playStartMs = now;
            playing = true;
            result = obj ({ { "playing", true }, { "beat", beatAtStart }, { "bpm", bpm } });
        }
        else if (tool == "set_project_tempo")
        {
            beatAtStart = beatNow();
            playStartMs = now;
            bpm = static_cast<double> (args["bpm"]);
            result = obj ({ { "ok", true } });
        }
        else if (tool == "get_transport_state") result = obj ({ { "playing", playing }, { "beat", beatNow() }, { "bpm", bpm } });
        else if (tool == "execute_batch") result = obj ({ { "results", juce::Array<juce::var> { obj ({ { "id", "event-9" } }) } } });
        else if (tool == "boom")
            return obj ({ { "jsonrpc", "2.0" }, { "id", id },
                          { "result", obj ({ { "isError", true }, { "content", juce::Array<juce::var> { obj ({ { "type", "text" }, { "text", "Error: kaboom" } }) } } }) } });
        else result = obj ({ { "ok", true } });
        now += 4;
        return obj ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", obj ({ { "isError", false }, { "structuredContent", result } }) } });
    }
};

mcp::script::Player::Hooks hooksFor (FakeApp& app, juce::StringArray& narration, juce::StringArray& logs)
{
    mcp::script::Player::Hooks h;
    h.request = [&app] (const juce::var& m) { return app.request (m); };
    h.notify = [] (const juce::var&) {};
    h.nowMs = [&app] { return app.now; };
    h.sleep = [&app] (int ms) { app.now += std::max (1, ms); };
    h.narrate = [&narration] (const juce::String& t) { narration.add (t); };
    h.log = [&logs] (const juce::String& t) { logs.add (t); };
    return h;
}
} // namespace

class ScriptTests : public juce::UnitTest
{
public:
    ScriptTests() : juce::UnitTest ("Demo script player", "MCP") {}

    void runTest() override
    {
        using mcp::script::Line;
        beginTest ("parsing lines");
        {
            auto l = mcp::script::parseLine ("create_track {\"name\":\"KICK\"} => kick=trackId, x = a.0.b");
            expect (l.kind == Line::Kind::Call);
            expectEquals (l.tool, juce::String ("create_track"));
            expectEquals (l.args, juce::String ("{\"name\":\"KICK\"}"));
            expectEquals (static_cast<int> (l.captures.size()), 2);
            expectEquals (l.captures[1].second, juce::String ("a.0.b"));
            expect (mcp::script::parseLine ("play").args == "{}");
            expect (mcp::script::parseLine ("?delete_track {\"trackId\":\"t\"}").optional);
            expect (mcp::script::parseLine ("  # Hello  ").kind == Line::Kind::Narration);
            expectEquals (mcp::script::parseLine ("@at 12.5").number, 12.5);
            expectEquals (mcp::script::parseLine ("@sync").number, 0.0);
            expectEquals (mcp::script::parseLine ("@sync -64").number, -64.0);
            expect (mcp::script::parseLine ("{\"jsonrpc\":\"2.0\"}").kind == Line::Kind::Raw);
            expect (mcp::script::parseLine ("").kind == Line::Kind::Blank);
            // A "=>" inside a JSON string is not a capture.
            const auto quoted = mcp::script::parseLine ("rename_track {\"name\":\"a => b\"}");
            expect (quoted.captures.empty());
            expectEquals (quoted.args, juce::String ("{\"name\":\"a => b\"}"));
            for (const char* bad : { "@nonsense 1", "@at", "@wait soon", "bad tool {}", "tool [1,2]", "tool {} => nope" })
            {
                bool threw = false;
                try { mcp::script::parseLine (bad); } catch (const std::exception&) { threw = true; }
                expect (threw, bad);
            }
        }

        beginTest ("variables are JSON-escaped; paths reach into results");
        {
            std::map<juce::String, juce::String> vars { { "DEMO", "C:\\Demo \"x\"" }, { "k", "track-3" } };
            expectEquals (mcp::script::substitute ("{\"p\":\"${DEMO}/a.wav\",\"t\":\"${k}\"}", vars),
                          juce::String ("{\"p\":\"C:\\\\Demo \\\"x\\\"/a.wav\",\"t\":\"track-3\"}"));
            bool threw = false;
            try { mcp::script::substitute ("${missing}", vars); } catch (const std::exception&) { threw = true; }
            expect (threw);
            const auto v = juce::JSON::parse ("{\"results\":[{\"id\":\"e1\"},null,{\"id\":\"e3\",\"n\":[5]}]}");
            expectEquals (mcp::script::extract (v, "results.2.id").toString(), juce::String ("e3"));
            expectEquals (static_cast<int> (mcp::script::extract (v, "results.2.n.0")), 5);
            expect (mcp::script::extract (v, "results.9.id").isVoid());
            expect (mcp::script::extract (v, "nope").isVoid());
        }

        beginTest ("the musical clock follows tempo changes");
        {
            mcp::script::MusicalClock c;
            expect (! c.running());
            c.start (0, 120);                          // 2 beats per second
            expectWithinAbsoluteError (c.beatsAt (1000), 2.0, 1e-9);
            c.setTempo (1000, 60);                     // then 1 beat per second
            expectWithinAbsoluteError (c.beatsAt (3000), 4.0, 1e-9);
            expectWithinAbsoluteError (c.msUntil (6.0, 3000), 2000.0, 1e-6);
            c.resync (3000, 4.5);
            expectWithinAbsoluteError (c.beatsAt (4000), 5.5, 1e-9);
        }

        beginTest ("a whole script: captures, beat waits, tempo changes, sync, stop on error");
        {
            FakeApp app;
            juce::StringArray narration, logs;
            mcp::script::Player::Options options;
            options.vars["DEMO"] = "/demo";
            mcp::script::Player player (options, hooksFor (app, narration, logs));
            juce::StringArray lines;
            lines.add ("@client Test demo");
            lines.add ("# Building in ${DEMO}");
            lines.add ("create_track {\"name\":\"KICK\"} => kick=trackId");
            lines.add ("rename_track {\"trackId\":\"${kick}\",\"name\":\"BOOM\"}");
            lines.add ("execute_batch {\"actions\":[]} => ev=results.0.id");
            lines.add ("set_project_tempo {\"bpm\":120}");
            lines.add ("play {\"fromBeat\":0}");
            lines.add ("@at 8");
            lines.add ("mute_track {\"trackId\":\"${kick}\",\"enabled\":true}");
            lines.add ("set_project_tempo {\"bpm\":60}");
            lines.add ("@at 10");
            lines.add ("@sync");
            lines.add ("mute_track {\"trackId\":\"${ev}\",\"enabled\":false}");
            lines.add ("?boom {}");
            lines.add ("boom {}");
            lines.add ("create_track {}");   // never reached
            expect (! player.run (lines));
            expect (player.error().contains ("kaboom"));
            expect (player.error().contains ("line 15"));
            expectEquals (narration[0], juce::String ("Building in /demo"));
            expectEquals (player.variables().at ("kick"), juce::String ("track-1"));
            expectEquals (player.variables().at ("ev"), juce::String ("event-9"));
            expectEquals (app.callArgs[1]["trackId"].toString(), juce::String ("track-1"));
            expectEquals (app.calls.indexOf ("mute_track"), 5);
            // The mute lands on beat 8 (within a few ms of simulated latency), the next one on beat 10 at the new tempo.
            expectWithinAbsoluteError (app.callBeats[5], 8.0, 0.05);
            expectWithinAbsoluteError (app.callBeats[8], 10.0, 0.05);
            expect (logs.joinIntoString ("\n").contains ("(skipped)"));
            expectEquals (app.calls.joinIntoString (",").indexOf (1, "create_track"), -1);   // the last line never ran
            expectEquals (app.calls[app.calls.size() - 1], juce::String ("boom"));
        }

        beginTest ("sync resumes a stopped transport and ignores wild readings");
        {
            FakeApp app;
            juce::StringArray narration, logs;
            mcp::script::Player player ({}, hooksFor (app, narration, logs));
            juce::StringArray lines { "play {\"fromBeat\":0}", "@at 4", "stop {}", "@sync" };
            app.bpm = 120;
            // The fake "stop" does nothing to `playing`, so emulate a stopped transport directly.
            lines.set (2, "@wait 1");
            expect (player.run (lines));
            app.playing = false;
            juce::StringArray more { "@at 6", "@sync" };
            expect (player.run (more));
            expectEquals (app.calls[app.calls.size() - 1], juce::String ("play"));
            expectWithinAbsoluteError (static_cast<double> (app.callArgs.getLast()["fromBeat"]), 6.0, 0.1);
        }

        beginTest ("fast mode never waits");
        {
            FakeApp app;
            juce::StringArray narration, logs;
            mcp::script::Player::Options options;
            options.fast = true;
            mcp::script::Player player (options, hooksFor (app, narration, logs));
            const double before = app.now;
            expect (player.run ({ "play {}", "@at 400", "@wait 60000", "@sync 5", "create_track {}" }));
            expect (app.now - before < 100.0);
        }
    }
};

static ScriptTests scriptTests;
} // namespace mcpdaw::test
