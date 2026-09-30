// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "JsonArgs.h"
#include <cmath>
#include <stdexcept>

namespace mcpdaw
{
JsonArgs::JsonArgs (const juce::var& object, juce::String ctx) : value (object), context (std::move (ctx))
{
    if (! value.isObject())
        fail ("expected a JSON object");
}

void JsonArgs::fail (const juce::String& message) const
{
    throw std::runtime_error ((context.isEmpty() ? message : context + ": " + message).toStdString());
}

juce::String JsonArgs::where (const char* key) const { return "'" + juce::String (key) + "'"; }

bool JsonArgs::has (const char* key) const
{
    return value.hasProperty (key) && ! value[key].isVoid();
}

juce::String JsonArgs::string (const char* key) const
{
    if (! has (key)) fail ("missing required string " + where (key));
    const auto& v = value[key];
    if (! v.isString()) fail (where (key) + " must be a string");
    auto s = v.toString();
    if (s.length() > 32768) fail (where (key) + " is too long");
    return s;
}

std::optional<juce::String> JsonArgs::optString (const char* key) const
{
    if (! has (key)) return std::nullopt;
    return string (key);
}

double JsonArgs::number (const char* key) const
{
    if (! has (key)) fail ("missing required number " + where (key));
    const auto& v = value[key];
    if (! (v.isInt() || v.isInt64() || v.isDouble())) fail (where (key) + " must be a number");
    const double d = static_cast<double> (v);
    if (! std::isfinite (d)) fail (where (key) + " must be finite");
    return d;
}

double JsonArgs::number (const char* key, double lo, double hi) const
{
    const double d = number (key);
    if (d < lo || d > hi)
        fail (where (key) + " = " + juce::String (d) + " is out of range [" + juce::String (lo) + ", " + juce::String (hi) + "]");
    return d;
}

std::optional<double> JsonArgs::optNumber (const char* key) const
{
    if (! has (key)) return std::nullopt;
    return number (key);
}

std::optional<double> JsonArgs::optNumber (const char* key, double lo, double hi) const
{
    if (! has (key)) return std::nullopt;
    return number (key, lo, hi);
}

juce::int64 JsonArgs::integer (const char* key, juce::int64 lo, juce::int64 hi) const
{
    const double d = number (key, static_cast<double> (lo), static_cast<double> (hi));
    if (std::floor (d) != d) fail (where (key) + " must be an integer");
    return static_cast<juce::int64> (d);
}

bool JsonArgs::boolean (const char* key) const
{
    if (! has (key)) fail ("missing required boolean " + where (key));
    const auto& v = value[key];
    if (! v.isBool()) fail (where (key) + " must be true or false");
    return static_cast<bool> (v);
}

std::optional<bool> JsonArgs::optBoolean (const char* key) const
{
    if (! has (key)) return std::nullopt;
    return boolean (key);
}

const juce::Array<juce::var>& JsonArgs::array (const char* key) const
{
    if (! has (key) || ! value[key].isArray()) fail (where (key) + " must be an array");
    return *value[key].getArray();
}

JsonArgs JsonArgs::object (const char* key) const
{
    if (! has (key) || ! value[key].isObject()) fail (where (key) + " must be an object");
    return JsonArgs (value[key], context.isEmpty() ? juce::String (key) : context + "." + key);
}
} // namespace mcpdaw
