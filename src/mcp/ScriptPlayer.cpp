// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "ScriptPlayer.h"
#include <cmath>

namespace mcpdaw::mcp::script
{
namespace
{
[[noreturn]] void fail (const juce::String& message) { throw std::runtime_error (message.toStdString()); }

bool isIdentifier (const juce::String& s)
{
    if (s.isEmpty()) return false;
    for (auto c : s)
        if (! (juce::CharacterFunctions::isLetterOrDigit (c) || c == '_' || c == '.' || c == '-')) return false;
    return true;
}

juce::var makeObj (std::initializer_list<std::pair<juce::Identifier, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [k, v] : props) o->setProperty (k, v);
    return juce::var (o);
}
} // namespace

// ================================================================================================
Line parseLine (const juce::String& raw)
{
    Line line;
    const auto s = raw.trim();
    if (s.isEmpty() || s.startsWith ("//")) return line;   // blank or a comment for script authors

    if (s.startsWithChar ('#'))
    {
        line.kind = Line::Kind::Narration;
        line.text = s.substring (1).trim();
        return line;
    }
    if (s.startsWithChar ('{'))
    {
        line.kind = Line::Kind::Raw;
        line.text = s;
        return line;
    }
    if (s.startsWithChar ('@'))
    {
        const auto word = s.upToFirstOccurrenceOf (" ", false, false).substring (1).toLowerCase();
        const auto rest = s.fromFirstOccurrenceOf (" ", false, false).trim();
        auto number = [&] (bool required, double fallback)
        {
            if (rest.isEmpty())
            {
                if (required) fail ("@" + word + " needs a number");
                return fallback;
            }
            if (! rest.containsOnly ("0123456789.-+eE")) fail ("@" + word + ": '" + rest + "' is not a number");
            return rest.getDoubleValue();
        };
        if (word == "wait")        { line.kind = Line::Kind::Wait;  line.number = number (true, 0); }
        else if (word == "pace")   { line.kind = Line::Kind::Pace;  line.number = number (true, 0); }
        else if (word == "at")     { line.kind = Line::Kind::At;    line.number = number (true, 0); }
        else if (word == "sync")   { line.kind = Line::Kind::Sync;  line.number = number (false, 0); }
        else if (word == "end")    { line.kind = Line::Kind::End; }
        else if (word == "client")
        {
            if (rest.isEmpty()) fail ("@client needs a name");
            line.kind = Line::Kind::Client;
            line.text = rest;
        }
        else fail ("unknown directive @" + word);
        return line;
    }

    // tool [json] [=> captures]
    line.kind = Line::Kind::Call;
    auto body = s;
    if (body.startsWithChar ('?'))
    {
        line.optional = true;
        body = body.substring (1).trimStart();
    }
    line.tool = body.upToFirstOccurrenceOf (" ", false, false).trim();
    if (! isIdentifier (line.tool)) fail ("expected a tool name, got '" + line.tool + "'");
    auto rest = body.fromFirstOccurrenceOf (" ", false, false).trim();
    if (line.tool == body) rest = {};

    // Captures follow the JSON: find "=>" after the last closing brace.
    const int close = rest.lastIndexOfChar ('}');
    const int arrow = rest.indexOf (juce::jmax (0, close), "=>");
    juce::String captures;
    if (arrow >= 0)
    {
        captures = rest.substring (arrow + 2).trim();
        rest = rest.substring (0, arrow).trim();
    }
    line.args = rest.isEmpty() ? juce::String ("{}") : rest;
    if (! line.args.startsWithChar ('{')) fail ("arguments must be a JSON object: " + line.args.substring (0, 40));

    for (auto item : juce::StringArray::fromTokens (captures, ",", ""))
    {
        item = item.trim();
        if (item.isEmpty()) continue;
        const auto name = item.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto path = item.fromFirstOccurrenceOf ("=", false, false).trim();
        if (! item.containsChar ('=') || ! isIdentifier (name) || path.isEmpty()) fail ("bad capture '" + item + "' (use name=path)");
        line.captures.emplace_back (name, path);
    }
    return line;
}

juce::String jsonEscape (const juce::String& s)
{
    const auto quoted = juce::JSON::toString (juce::var (s));   // "..."
    return quoted.substring (1, quoted.length() - 1);
}

juce::String substitute (const juce::String& text, const std::map<juce::String, juce::String>& vars)
{
    if (! text.contains ("${")) return text;
    juce::String out;
    int i = 0;
    for (;;)
    {
        const int open = text.indexOf (i, "${");
        if (open < 0) { out << text.substring (i); break; }
        const int close = text.indexOfChar (open, '}');
        if (close < 0) fail ("unterminated ${ in line");
        const auto name = text.substring (open + 2, close);
        const auto it = vars.find (name);
        if (it == vars.end()) fail ("unknown variable ${" + name + "}");
        out << text.substring (i, open) << jsonEscape (it->second);
        i = close + 1;
    }
    return out;
}

juce::var extract (const juce::var& value, const juce::String& path)
{
    juce::var v = value;
    for (const auto& part : juce::StringArray::fromTokens (path, ".", ""))
    {
        if (v.isArray())
        {
            if (! part.containsOnly ("0123456789")) return {};
            const int index = part.getIntValue();
            if (index < 0 || index >= v.size()) return {};
            v = v[index];
        }
        else if (auto* o = v.getDynamicObject())
        {
            if (! o->hasProperty (part)) return {};
            v = o->getProperty (part);
        }
        else return {};
    }
    return v;
}

// ================================================================================================
void MusicalClock::start (double nowMs, double bpm)
{
    isRunning = true;
    anchorMs = nowMs;
    anchorBeats = 0;
    tempo = bpm;
}

double MusicalClock::beatsAt (double nowMs) const noexcept
{
    if (! isRunning) return 0;
    return anchorBeats + (nowMs - anchorMs) * tempo / 60000.0;
}

double MusicalClock::msUntil (double beats, double nowMs) const noexcept
{
    if (! isRunning) return 0;
    return (beats - beatsAt (nowMs)) * 60000.0 / tempo;
}

void MusicalClock::setTempo (double nowMs, double bpm)
{
    if (isRunning)
    {
        anchorBeats = beatsAt (nowMs);
        anchorMs = nowMs;
    }
    tempo = bpm;
}

void MusicalClock::resync (double nowMs, double beatsPlayed)
{
    anchorMs = nowMs;
    anchorBeats = beatsPlayed;
}

// ================================================================================================
Player::Player (Options o, Hooks h) : options (std::move (o)), hooks (std::move (h)) {}

void Player::ensureInitialised()
{
    if (initialised) return;
    initialised = true;
    const auto init = makeObj ({ { "jsonrpc", "2.0" }, { "id", nextId++ }, { "method", "initialize" },
                                 { "params", makeObj ({ { "protocolVersion", "2025-06-18" }, { "capabilities", makeObj ({}) },
                                                        { "clientInfo", makeObj ({ { "name", options.clientName }, { "version", "1.0" } }) } }) } });
    const auto reply = hooks.request (init);
    if (! reply.hasProperty ("result")) fail ("the MCP bridge did not answer initialize");
    hooks.notify (makeObj ({ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } }));
}

juce::var Player::callTool (const juce::String& tool, const juce::var& args, bool optional, int lineNumber)
{
    ensureInitialised();
    const double sent = hooks.nowMs();
    const auto request = makeObj ({ { "jsonrpc", "2.0" }, { "id", nextId++ }, { "method", "tools/call" },
                                    { "params", makeObj ({ { "name", tool }, { "arguments", args } }) } });
    const auto reply = hooks.request (request);
    const double received = hooks.nowMs();

    juce::String problem;
    if (reply.isVoid()) problem = "no reply from MCP DAW";
    else if (reply.hasProperty ("error")) problem = reply["error"]["message"].toString();
    else if (static_cast<bool> (reply["result"]["isError"])) problem = reply["result"]["content"][0]["text"].toString();

    if (problem.isNotEmpty())
    {
        const auto where = "line " + juce::String (lineNumber) + " (" + tool + "): " + problem;
        if (optional)
        {
            if (hooks.log) hooks.log ("  (skipped) " + where);
            return {};
        }
        fail (where);
    }
    if (options.verbose && hooks.log) hooks.log ("  > " + tool + "  (" + juce::String (juce::roundToInt (received - sent)) + " ms)");

    auto result = reply["result"]["structuredContent"];
    if (result.isVoid()) result = juce::JSON::parse (reply["result"]["content"][0]["text"].toString());

    // The clock follows the transport commands the script itself sends.
    const double mid = 0.5 * (sent + received);
    if (tool == "play")
        musicalClock.start (mid, static_cast<double> (result["bpm"]) > 0 ? static_cast<double> (result["bpm"]) : musicalClock.bpm());
    else if (tool == "set_project_tempo")
        musicalClock.setTempo (mid, static_cast<double> (args["bpm"]));
    return result;
}

void Player::waitUntilBeat (double beats)
{
    if (options.fast || ! musicalClock.running()) return;
    for (bool first = true;; first = false)
    {
        const double ms = musicalClock.msUntil (beats, hooks.nowMs());
        if (first && ms < -60.0 && options.verbose && hooks.log)   // the app (or this machine) is falling behind
            hooks.log ("  (late: beat " + juce::String (beats, 2) + " by " + juce::String (juce::roundToInt (-ms)) + " ms)");
        if (ms <= 0.5) return;
        hooks.sleep (static_cast<int> (std::min (ms, 50.0)));   // short slices: stays accurate and responsive
    }
}

void Player::sync (double offset)
{
    if (options.fast || ! musicalClock.running()) return;
    const double sent = hooks.nowMs();
    const auto state = callTool ("get_transport_state", makeObj ({}), true, 0);
    const double mid = 0.5 * (sent + hooks.nowMs());
    if (state.isVoid()) return;

    const double predicted = musicalClock.beatsAt (mid);
    if (! static_cast<bool> (state["playing"]))
    {
        // Someone pressed stop: carry on from where the music should be.
        if (hooks.log) hooks.log ("  (playback was stopped - resuming the demo)");
        const double from = std::max (0.0, predicted - offset);
        const auto reply = callTool ("play", makeObj ({ { "fromBeat", from } }), true, 0);
        if (! reply.isVoid()) musicalClock.resync (hooks.nowMs(), predicted);
        return;
    }
    const double measured = static_cast<double> (state["beat"]) + offset;
    if (std::abs (measured - predicted) <= options.syncToleranceBeats)
        musicalClock.resync (mid, measured);
    else if (! warnedSync)
    {
        warnedSync = true;   // e.g. no audio device (the transport isn't moving) or the playhead was moved by hand
        if (hooks.log)
            hooks.log ("  (the playhead is " + juce::String (measured - predicted, 1) + " beats from where the demo expects it - "
                       "keeping the demo's own timing)");
    }
}

bool Player::run (const juce::StringArray& lines)
{
    int number = 0;
    try
    {
        for (const auto& text : lines)
        {
            ++number;
            Line line;
            try { line = parseLine (text); }
            catch (const std::exception& e) { fail ("line " + juce::String (number) + ": " + e.what()); }

            switch (line.kind)
            {
                case Line::Kind::Blank: break;
                case Line::Kind::Narration: if (hooks.narrate) hooks.narrate (substitute (line.text, options.vars)); break;
                case Line::Kind::Client:    options.clientName = line.text; break;
                case Line::Kind::Pace:      paceMs = static_cast<int> (line.number); break;
                case Line::Kind::Wait:      if (! options.fast) hooks.sleep (static_cast<int> (line.number)); break;
                case Line::Kind::At:        waitUntilBeat (line.number); break;
                case Line::Kind::Sync:      sync (line.number); break;
                case Line::Kind::End:       return true;
                case Line::Kind::Raw:
                {
                    ensureInitialised();
                    const auto message = juce::JSON::parse (substitute (line.text, options.vars));
                    if (! message.isObject()) fail ("line " + juce::String (number) + ": invalid JSON");
                    if (message.hasProperty ("id")) hooks.request (message);
                    else hooks.notify (message);
                    break;
                }
                case Line::Kind::Call:
                {
                    const auto argsText = substitute (line.args, options.vars);
                    juce::var args;
                    if (juce::JSON::parse (argsText, args).failed() || ! args.isObject())
                        fail ("line " + juce::String (number) + ": arguments are not valid JSON");
                    const auto result = callTool (line.tool, args, line.optional, number);
                    for (const auto& [name, path] : line.captures)
                    {
                        const auto value = extract (result, path);
                        if (value.isVoid())
                        {
                            if (line.optional) continue;
                            fail ("line " + juce::String (number) + ": the result has no '" + path + "'");
                        }
                        options.vars[name] = value.isString() ? value.toString() : juce::JSON::toString (value, true);
                    }
                    if (paceMs > 0 && ! options.fast) hooks.sleep (paceMs);
                    break;
                }
            }
        }
        return true;
    }
    catch (const std::exception& e)
    {
        failure = e.what();
        return false;
    }
}
} // namespace mcpdaw::mcp::script
