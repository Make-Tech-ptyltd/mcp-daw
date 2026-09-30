#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Strict, message-rich access to juce::var objects.
//
// Used for command arguments (MCP + GUI) and for parsing project files. Every failure throws
// std::runtime_error naming the offending field, because the consumer is often an AI agent that
// needs a precise error to correct its next call.

#include <juce_core/juce_core.h>
#include <optional>

namespace mcpdaw
{
class JsonArgs
{
public:
    JsonArgs (const juce::var& object, juce::String context = {});

    bool has (const char* key) const;

    juce::String string (const char* key) const;
    std::optional<juce::String> optString (const char* key) const;

    double number (const char* key) const;
    double number (const char* key, double lo, double hi) const;
    std::optional<double> optNumber (const char* key) const;
    std::optional<double> optNumber (const char* key, double lo, double hi) const;

    juce::int64 integer (const char* key, juce::int64 lo, juce::int64 hi) const;

    bool boolean (const char* key) const;
    std::optional<bool> optBoolean (const char* key) const;

    const juce::Array<juce::var>& array (const char* key) const;
    JsonArgs object (const char* key) const;

    const juce::var& raw() const noexcept { return value; }
    [[noreturn]] void fail (const juce::String& message) const;

private:
    juce::var value;
    juce::String context;
    juce::String where (const char* key) const;
};
} // namespace mcpdaw
