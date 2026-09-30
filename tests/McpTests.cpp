// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// MCP protocol tests: the exact JSON-RPC exchanges an agent performs.
#include "TestSupport.h"
#include "mcp/McpSession.h"

namespace mcpdaw::test
{
class McpTests final : public juce::UnitTest
{
public:
    McpTests() : juce::UnitTest ("MCP session", "MCP") {}

    static juce::var parse (const juce::String& s) { return juce::JSON::parse (s); }

    void runTest() override
    {
        Session s;
        McpSession mcp (s.registry, s.controller);

        beginTest ("initialize negotiates the protocol version");
        auto init = parse (mcp.handleText (R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})"));
        expectEquals (init["result"]["protocolVersion"].toString(), juce::String ("2025-06-18"));
        expectEquals (init["result"]["serverInfo"]["name"].toString(), juce::String ("mcp-daw"));
        expect (init["result"]["capabilities"]["tools"].isObject());
        auto unknownVersion = parse (mcp.handleText (R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"1999-01-01"}})"));
        expectEquals (unknownVersion["result"]["protocolVersion"].toString(), McpSession::supportedProtocolVersions()[0]);
        mcp.handleText (R"({"jsonrpc":"2.0","id":3,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})");

        beginTest ("notifications get no reply");
        expectEquals (mcp.handleText (R"({"jsonrpc":"2.0","method":"notifications/initialized"})"), juce::String());
        expect (mcp.isInitialised());

        beginTest ("tools/list mirrors the action registry with schemas and annotations");
        auto list = parse (mcp.handleText (R"({"jsonrpc":"2.0","id":4,"method":"tools/list"})"));
        const auto& tools = *list["result"]["tools"].getArray();
        expectEquals (tools.size(), static_cast<int> (s.registry.all().size()));
        bool sawImport = false;
        for (const auto& t : tools)
        {
            expect (t["inputSchema"]["type"].toString() == "object");
            if (t["name"].toString() == "get_project_state") expect (static_cast<bool> (t["annotations"]["readOnlyHint"]));
            if (t["name"].toString() == "import_audio") sawImport = true;
        }
        expect (sawImport);

        beginTest ("tools/call success returns text + structuredContent");
        auto call = parse (mcp.handleText (R"({"jsonrpc":"2.0","id":"a","method":"tools/call","params":{"name":"create_track","arguments":{"name":"Drums"}}})"));
        expectEquals (call["id"].toString(), juce::String ("a"));
        expect (! static_cast<bool> (call["result"]["isError"]));
        expectEquals (call["result"]["structuredContent"]["trackId"].toString(), juce::String ("track-1"));
        expect (parse (call["result"]["content"][0]["text"].toString())["trackId"].toString() == "track-1");

        beginTest ("tool failures are results with isError, not protocol errors");
        auto bad = parse (mcp.handleText (R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"set_project_tempo","arguments":{"bpm":"fast"}}})"));
        expect (static_cast<bool> (bad["result"]["isError"]));
        expect (bad["result"]["content"][0]["text"].toString().contains ("'bpm' must be a number"));

        beginTest ("protocol errors");
        expectEquals (static_cast<int> (parse (mcp.handleText ("{nope"))["error"]["code"]), -32700);
        expectEquals (static_cast<int> (parse (mcp.handleText (R"({"jsonrpc":"2.0","id":6,"method":"bogus/method"})"))["error"]["code"]), -32601);
        expectEquals (static_cast<int> (parse (mcp.handleText (R"({"jsonrpc":"1.0","id":7,"method":"ping"})"))["error"]["code"]), -32600);
        expect (parse (mcp.handleText (R"({"jsonrpc":"2.0","id":8,"method":"ping"})"))["result"].isObject());

        beginTest ("JSON-RPC batch arrays");
        auto batch = parse (mcp.handleText (R"([{"jsonrpc":"2.0","id":10,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/cancelled"},{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"list_tracks","arguments":{}}}])"));
        expectEquals (batch.size(), 2);

        beginTest ("structuredContent is always a JSON object (list results wrapped as items)");
        const auto listed = batch[1]["result"]["structuredContent"];
        expect (listed.isObject() && ! listed.isArray(), "array results must be wrapped");
        expect (listed["items"].isArray());

        beginTest ("an agent can build and inspect an arrangement end to end");
        const auto loop = dcWav ("mcp_loop_120bpm.wav", 0.25f, 48000, 96000);   // 2 s = 1 bar at 120
        const auto path = juce::JSON::toString (loop.getFullPathName());
        auto imported = parse (mcp.handleText (R"({"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":"import_audio","arguments":{"path":)" + path + R"(}}})"));
        const auto eventId = imported["result"]["structuredContent"]["eventId"].toString();
        expect (eventId.isNotEmpty());
        mcp.handleText (R"({"jsonrpc":"2.0","id":21,"method":"tools/call","params":{"name":"resize_event","arguments":{"eventId":")" + eventId + R"(","endBeat":16}}})");
        auto state = parse (mcp.handleText (R"({"jsonrpc":"2.0","id":22,"method":"tools/call","params":{"name":"get_project_state","arguments":{}}})"));
        const auto sc = state["result"]["structuredContent"];
        expectEquals (static_cast<double> (sc["lengthBars"]), 4.0);
        expectEquals (sc["tracks"][1]["occupiedBars"].toString(), juce::String ("1-4"));
        expectEquals (static_cast<double> (sc["tracks"][1]["events"][0]["repetitions"]), 4.0);
    }
};

static McpTests mcpTests;
} // namespace mcpdaw::test
