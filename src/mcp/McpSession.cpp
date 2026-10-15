// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "McpSession.h"
#include "../core/ProjectJson.h"

namespace mcpdaw
{
namespace
{
constexpr int kParseError = -32700, kInvalidRequest = -32600, kMethodNotFound = -32601, kInvalidParams = -32602;
constexpr int kNotInitialised = -32002;

juce::var rpcError (const juce::var& id, int code, const juce::String& message)
{
    return makeObject ({ { "jsonrpc", "2.0" }, { "id", id }, { "error", makeObject ({ { "code", code }, { "message", message } }) } });
}

juce::var rpcResult (const juce::var& id, const juce::var& result)
{
    return makeObject ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } });
}

bool supportsStructuredContent (const juce::String& version) { return version >= "2025-06-18"; }
} // namespace

McpSession::McpSession (ActionRegistry& r, ProjectController& c) : registry (r), controller (c) {}

juce::StringArray McpSession::supportedProtocolVersions()
{
    return { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05" };
}

juce::String McpSession::serverInstructions()
{
    return "MCP DAW: loop-based audio arranger (DAW) built for human + AI collaboration. You are controlling the LIVE session the user also sees.\n"
           "Units: positions and lengths are zero-based beats (quarter notes); 4/4 only, so bar N starts at beat (N-1)*4. "
           "Levels are dB (-70 = silence), pan is -1..+1.\n"
           "Workflow: get_project_state first; import_audio places a loop on a new track; extend loops with resize_event(endBeat) "
           "or duplicate_event(count); use clear_range / duplicate_range for arrangement edits; effects via add_effect + "
           "set_effect_parameters (see list_effect_types for parameter ids; list_effect_types(type) for one effect), or "
           "add_effect_preset for a ready-made chain. Tremolo, chorus and phaser cycle inside the audio engine (synced to the "
           "tempo, optionally restarting on each event): set them once, never animate parameters with repeated calls. "
           "Use execute_batch to make multi-step edits one undo step. Pass detail=\"compact\" to get_project_state, "
           "add_effect, set_effect_parameters and execute_batch for smaller responses. "
           "Every edit is undoable (undo). Audio plays unstretched: match loops to the project tempo (sourceBpm).\n"
           "If MCP DAW is not open, your first tool call opens it (allow a few seconds). Another AI or script may be "
           "using it; if a call says it is in use, wait and retry.";
}

juce::String McpSession::handleText (const juce::String& text)
{
    juce::var message;
    const auto parsed = juce::JSON::parse (text, message);
    const juce::var reply = parsed.failed() ? rpcError ({}, kParseError, "Parse error: " + parsed.getErrorMessage()) : handle (message);
    return reply.isVoid() ? juce::String() : juce::JSON::toString (reply, true);
}

juce::var McpSession::handle (const juce::var& message)
{
    if (message.isArray())
    {
        juce::Array<juce::var> replies;
        for (const auto& m : *message.getArray())
            if (auto r = handleSingle (m); ! r.isVoid()) replies.add (r);
        return replies.isEmpty() ? juce::var() : juce::var (replies);
    }
    return handleSingle (message);
}

juce::var McpSession::handleSingle (const juce::var& m)
{
    if (! m.isObject() || m["jsonrpc"].toString() != "2.0" || ! m["method"].isString())
    {
        // Responses from the client (we never send requests) are ignored; anything else is invalid.
        if (m.isObject() && (m.hasProperty ("result") || m.hasProperty ("error"))) return {};
        return rpcError (m.isObject() ? m["id"] : juce::var(), kInvalidRequest, "Invalid JSON-RPC 2.0 request");
    }

    const auto method = m["method"].toString();
    const bool isNotification = ! m.hasProperty ("id");
    if (isNotification)
    {
        if (method == "notifications/initialized") initialised = true;
        return {};   // other notifications (cancelled, progress...) need no reply
    }

    const auto id = m["id"];
    if (! (id.isString() || id.isInt() || id.isInt64() || id.isDouble()))
        return rpcError ({}, kInvalidRequest, "Request id must be a string or number");

    if (method == "initialize")
    {
        const auto requested = m["params"]["protocolVersion"].toString();
        protocolVersion = supportedProtocolVersions().contains (requested) ? requested : supportedProtocolVersions()[0];
        announcedClientName = m["params"]["clientInfo"]["name"].toString();
        auto result = makeObject ({
            { "protocolVersion", protocolVersion },
            { "capabilities", makeObject ({ { "tools", makeObject ({ { "listChanged", false } }) } }) },
            { "serverInfo", makeObject ({ { "name", "mcp-daw" }, { "title", "MCP DAW by Make Tech" }, { "version", MCPDAW_VERSION_STRING } }) },
            { "instructions", serverInstructions() } });
        if (! initializeMeta.isVoid()) result.getDynamicObject()->setProperty ("_meta", initializeMeta);
        return rpcResult (id, result);
    }
    if (method == "ping") return rpcResult (id, makeObject ({}));

    // Lenient: some clients send tools/list before the initialized notification arrives.
    if (method == "tools/list") return rpcResult (id, toolsList());
    if (method == "tools/call")
    {
        if (! m["params"].isObject() || ! m["params"]["name"].isString())
            return rpcError (id, kInvalidParams, "tools/call needs params.name");
        return rpcResult (id, callTool (m["params"]));
    }
    if (method == "resources/list") return rpcResult (id, makeObject ({ { "resources", juce::Array<juce::var>() } }));
    if (method == "prompts/list") return rpcResult (id, makeObject ({ { "prompts", juce::Array<juce::var>() } }));
    if (! initialised && method != "initialize")
        return rpcError (id, kNotInitialised, "Session not initialised");
    return rpcError (id, kMethodNotFound, "Method not found: " + method);
}

juce::var McpSession::toolsList() const
{
    juce::Array<juce::var> tools;
    for (const auto& a : registry.all())
    {
        const bool readOnly = a.kind == ActionKind::Query;
        const bool destructive = a.name.startsWith ("delete") || a.name.startsWith ("remove") || a.name == "clear_range"
                              || a.name == "new_project" || a.name == "open_project";
        tools.add (makeObject ({ { "name", a.name }, { "description", a.description }, { "inputSchema", a.inputSchema },
                                 { "annotations", makeObject ({ { "readOnlyHint", readOnly }, { "destructiveHint", destructive },
                                                                { "idempotentHint", readOnly }, { "openWorldHint", false } }) } }));
    }
    return makeObject ({ { "tools", tools } });
}

juce::var McpSession::callTool (const juce::var& params)
{
    const auto name = params["name"].toString();
    const auto args = params.hasProperty ("arguments") ? params["arguments"] : juce::var();
    if (onToolCall) onToolCall (name, args);
    juce::Array<juce::var> content;
    try
    {
        auto value = registry.execute (controller, name, args);
        if (value.isVoid()) value = makeObject ({ { "ok", true } });
        content.add (makeObject ({ { "type", "text" }, { "text", juce::JSON::toString (value, true) } }));   // compact: fewer tokens for the model
        auto result = makeObject ({ { "content", content }, { "isError", false } });
        if (supportsStructuredContent (protocolVersion))
            // MCP requires structuredContent to be a JSON object. juce::var reports arrays as objects too,
            // so test for arrays explicitly: lists become {"items": [...]}, scalars {"result": x}.
            result.getDynamicObject()->setProperty ("structuredContent",
                value.isArray() ? makeObject ({ { "items", value } })
                                : value.isObject() ? value : makeObject ({ { "result", value } }));
        return result;
    }
    catch (const std::exception& e)
    {
        // Tool failures are results (isError), not protocol errors, so the model can read and correct them.
        content.add (makeObject ({ { "type", "text" }, { "text", juce::String ("Error: ") + e.what() } }));
        return makeObject ({ { "content", content }, { "isError", true } });
    }
}
} // namespace mcpdaw
