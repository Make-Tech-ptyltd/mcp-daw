#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Recent MCP commands for the status-bar ticker. Tool names only (no arguments, no translation):
//   Claude > create_event x12 . set_event_gain . play . execute_batch (24) create_event, set_event_gain...
// Consecutive identical calls from the same client collapse into one entry with a count; a batch is
// one entry showing its size and first few action names. Pure data, so it is unit-tested.
#include <juce_core/juce_core.h>
#include <deque>

namespace mcpdaw::mcp
{
class CommandFeed
{
public:
    struct Entry
    {
        juce::String client, tool, detail;
        int count = 1;
        juce::uint32 time = 0;   // ms counter of the latest call merged into this entry

        juce::String text() const
        {
            juce::String t = tool;
            if (count > 1) t << " " << juce::String::charToString (0x00D7) << count;   // "x12"
            if (detail.isNotEmpty()) t << " " << detail;
            return t;
        }
    };

    static constexpr size_t kMaxEntries = 40;

    /** Adds a call. Returns true when a new entry was appended, false when it was merged into the newest. */
    bool push (const juce::String& client, const juce::String& tool, const juce::var& arguments, juce::uint32 nowMs)
    {
        const auto detail = tool == "execute_batch" ? describeBatch (arguments) : juce::String();
        if (! items.empty() && detail.isEmpty())
        {
            auto& last = items.back();
            if (last.client == client && last.tool == tool && last.detail.isEmpty())
            {
                ++last.count;
                last.time = nowMs;
                return false;
            }
        }
        items.push_back ({ client, tool, detail, 1, nowMs });
        while (items.size() > kMaxEntries) items.pop_front();
        return true;
    }

    const std::deque<Entry>& entries() const noexcept { return items; }
    juce::uint32 lastTime() const noexcept { return items.empty() ? 0 : items.back().time; }
    void clear() { items.clear(); }

    /** "(24) create_event, set_event_gain, play..." */
    static juce::String describeBatch (const juce::var& arguments)
    {
        const auto& actions = arguments["actions"];
        juce::StringArray names;   // distinct action names, in order of first use
        for (int i = 0; i < actions.size() && names.size() < 4; ++i)
            if (const auto n = actions[i]["action"].toString(); n.isNotEmpty()) names.addIfNotAlreadyThere (n);
        juce::String s = "(" + juce::String (actions.size()) + ")";
        if (! names.isEmpty())
        {
            s << " " << names.joinIntoString (", ", 0, 3);
            if (names.size() > 3) s << ", " << juce::String::charToString (0x2026);   // more kinds follow
        }
        return s;
    }

private:
    std::deque<Entry> items;
};
} // namespace mcpdaw::mcp
