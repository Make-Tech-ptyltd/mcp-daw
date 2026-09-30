#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Names and labels shared by the app (server), mcp-daw-bridge and the tests. Header-only, juce_core only.
#include <juce_core/juce_core.h>

namespace mcpdaw::mcp
{
/** Framing header for juce::InterprocessConnection messages ("MDAW"). */
inline constexpr juce::uint32 kPipeMagic = 0x4D444157;

/** Idle time after which a bridge gives the seat back in "take turns" mode. */
inline constexpr int kTakeTurnsIdleMs = 20000;

inline juce::String userTag()
{
    auto user = juce::SystemStats::getLogonName().toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789_-");
    return user.isEmpty() ? juce::String ("user") : user;
}

/** Per-user pipe name. On Windows this becomes \\.\pipe\<name>, which is local-machine only. */
inline juce::String pipeName() { return "mcp-daw-" + userTag(); }

/** Per-user name of the MCP seat. Bridges hold it (an OS lock) while they are connected, so exactly
    one AI client or script talks to MCP DAW at a time; the others wait their turn. */
inline juce::String seatName() { return "mcp-daw-seat-" + userTag(); }

/** Held by MCP DAW for as long as it runs: lets a bridge tell "not running" from "busy". */
inline juce::String appLockName() { return "mcp-daw-app-" + userTag(); }

/** Short, human label for an MCP client from the name it announces in `initialize`
    (clientInfo.name): "claude-ai" -> "Claude", "codex-mcp-client" -> "ChatGPT", a script's own
    name ("dilla-beat") stays as it is. Claude's Cowork and Code sessions connect as "local-agent-...". */
inline juce::String friendlyClientName (const juce::String& announced)
{
    const auto n = announced.trim().toLowerCase();
    if (n.isEmpty())                                               return "AI client";
    if (n.contains ("claude") || n.startsWith ("local-agent"))    return "Claude";   // Cowork/Code sessions announce "local-agent-..."
    if (n.contains ("codex") || n.contains ("openai") || n.contains ("chatgpt")) return "ChatGPT";
    if (n.contains ("cursor"))                                     return "Cursor";
    if (n.contains ("copilot") || n.contains ("visual studio"))    return "Copilot";
    if (n.contains ("gemini"))                                     return "Gemini";
    const auto raw = announced.trim();
    return raw.length() <= 24 ? raw : raw.substring (0, 23) + "~";
}
} // namespace mcpdaw::mcp
