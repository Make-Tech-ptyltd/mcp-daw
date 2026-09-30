#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Plays a demo script through the MCP bridge (mcp-daw-bridge --script <file>), in time with the music.
//
// A script is plain text, one instruction per line:
//
//   # text                     narration, printed for the person watching
//   // text                    a comment (ignored)
//   tool {json args}           call a tool, e.g.  create_track {"name":"KICK"}
//   tool {json} => v=path      ...and keep parts of the result: => kick=trackId, first=results.0.id
//   ?tool {json}               optional call: a failure is reported but does not stop the script
//   {"jsonrpc":...}            a raw JSON-RPC request (as in watch mode)
//   @client <name>             the name MCP DAW shows in its top bar (before the first call)
//   @wait <ms>                 pause
//   @pace <ms>                 pause after every call from here on (default 0)
//   @at <beats>                wait until this many beats have played since `play`
//   @sync [offset]             ask MCP DAW where the playhead is and correct the script's clock:
//                              beats played = playhead + offset (offset = beats skipped by loop jumps)
//   @end                       stop here
//
// ${name} in a line is replaced by a kept value (JSON-escaped, so it can sit inside "..."). ${DEMO} is
// the folder the script is in. Any failed call stops the script (unless it starts with '?'), so a demo
// never carries on in a broken state.
//
// The musical clock: `play` starts it, `set_project_tempo` changes its speed from that moment, and
// @sync nudges it back onto MCP DAW's own transport. Loop jumps are the script's business: it knows
// where it put them, so @at always counts beats *played*, which only ever go up.

#include <juce_core/juce_core.h>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>

namespace mcpdaw::mcp::script
{
struct Line
{
    enum class Kind { Blank, Narration, Wait, Pace, At, Sync, Client, Call, Raw, End };
    Kind kind = Kind::Blank;
    juce::String text;          // narration / client name / raw JSON
    double number = 0;          // wait/pace ms, @at beats, @sync offset
    juce::String tool, args;    // Call: tool name and JSON arguments text ("{}" if none)
    bool optional = false;
    std::vector<std::pair<juce::String, juce::String>> captures;   // variable -> result path
};

/** Parses one line. Throws std::runtime_error on bad syntax. */
Line parseLine (const juce::String& line);

/** JSON string escaping (without the surrounding quotes). */
juce::String jsonEscape (const juce::String&);

/** Replaces ${name}; throws std::runtime_error for an unknown name. */
juce::String substitute (const juce::String& text, const std::map<juce::String, juce::String>& vars);

/** Follows "a.b.0.c" through objects and arrays. Returns void when the path does not exist. */
juce::var extract (const juce::var& value, const juce::String& path);

/** Beats played since `play`, from a wall clock (ms) and the tempo the script has set. */
class MusicalClock
{
public:
    void start (double nowMs, double bpm);
    bool running() const noexcept { return isRunning; }
    double bpm() const noexcept { return tempo; }
    double beatsAt (double nowMs) const noexcept;
    /** Milliseconds until `beats` have played (<= 0: already there). Assumes the tempo stays as it is. */
    double msUntil (double beats, double nowMs) const noexcept;
    void setTempo (double nowMs, double bpm);
    void resync (double nowMs, double beatsPlayed);

private:
    bool isRunning = false;
    double anchorMs = 0, anchorBeats = 0, tempo = 120;
};

class Player
{
public:
    struct Hooks
    {
        /** Sends a JSON-RPC request and returns the reply message (void = no reply / timed out). */
        std::function<juce::var (const juce::var& request)> request;
        /** Sends a JSON-RPC notification (no reply). */
        std::function<void (const juce::var& message)> notify;
        std::function<double()> nowMs;                 // monotonic wall clock
        std::function<void (int ms)> sleep;            // keep the bridge ticking while sleeping
        std::function<void (const juce::String&)> narrate, log;
    };
    struct Options
    {
        juce::String clientName = "MCP DAW demo";
        bool fast = false;             // tests: no waiting, no @sync
        bool verbose = false;          // log every call
        double syncToleranceBeats = 2.0;
        std::map<juce::String, juce::String> vars;   // e.g. DEMO
    };

    Player (Options, Hooks);

    /** Runs the whole script. False if it stopped on an error (see error()). */
    bool run (const juce::StringArray& lines);
    const juce::String& error() const noexcept { return failure; }
    const MusicalClock& clock() const noexcept { return musicalClock; }
    const std::map<juce::String, juce::String>& variables() const noexcept { return options.vars; }

private:
    Options options;
    Hooks hooks;
    MusicalClock musicalClock;
    juce::String failure;
    int nextId = 1, paceMs = 0;
    bool initialised = false, warnedSync = false;

    void ensureInitialised();
    /** Calls a tool; returns the result object (structuredContent), throws on failure unless optional. */
    juce::var callTool (const juce::String& tool, const juce::var& args, bool optional, int lineNumber);
    void waitUntilBeat (double beats);
    void sync (double offset);
};
} // namespace mcpdaw::mcp::script
