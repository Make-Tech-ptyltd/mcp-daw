#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Model Context Protocol (JSON-RPC 2.0) session over the action registry.
//
// Transport-agnostic: feed it parsed JSON-RPC messages, get responses back. The same class is
// used by the named-pipe server inside the app and directly by the tests. Tools are generated
// from ActionRegistry, so MCP always mirrors exactly what the GUI can do.

#include "../core/Actions.h"

namespace mcpdaw
{
class McpSession
{
public:
    McpSession (ActionRegistry&, ProjectController&);

    /** Handle one JSON-RPC message (or a batch array). Returns void for notifications. */
    juce::var handle (const juce::var& message);

    /** Parse + handle + serialise one line of JSON text. Returns an empty string when there is no reply. */
    juce::String handleText (const juce::String& jsonText);

    bool isInitialised() const noexcept { return initialised; }
    /** The client's self-reported name from `initialize` (clientInfo.name), e.g. "claude-ai". */
    const juce::String& clientName() const noexcept { return announcedClientName; }

    /** Extra data returned as `_meta` in the initialize result (the app puts its seat mode here). */
    juce::var initializeMeta;
    /** Called just before a tool runs (name + arguments). Used for the command ticker. */
    std::function<void (const juce::String& tool, const juce::var& arguments)> onToolCall;
    static juce::StringArray supportedProtocolVersions();
    static juce::String serverInstructions();

private:
    ActionRegistry& registry;
    ProjectController& controller;
    bool initialised = false;
    juce::String protocolVersion, announcedClientName;

    juce::var handleSingle (const juce::var& message);
    juce::var toolsList() const;
    juce::var callTool (const juce::var& params);
};
} // namespace mcpdaw
