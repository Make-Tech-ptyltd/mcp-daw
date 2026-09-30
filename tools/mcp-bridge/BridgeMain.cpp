// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// mcp-daw-bridge - stdio <-> named-pipe bridge between an MCP client and MCP DAW.
//
// An MCP client (Claude Desktop, Codex, a Python script...) launches this executable as a "stdio"
// server. It is LAZY: starting it, initialising it and listing its tools never touches MCP DAW
// (see src/mcp/Bridge.h). Only a real tool call takes the MCP seat, connects - starting MCP DAW
// if it isn't running - and forwards. In take-turns mode the seat is handed back after 20 s idle.
//
// Nothing but JSON-RPC is ever written to stdout; diagnostics go to stderr, as MCP requires.
//
// Watch mode (--pace <ms>) is for scripted demos a person watches: requests are sent one at a
// time, each waits for its reply plus <ms>, and a one-line summary of every request and reply is
// printed to stderr. In scripts, lines starting with '#' are narration (printed, never sent) and
// '@wait <ms>' pauses - handy for letting a loop play before the next edit.
//
// Script mode (--script <file>) plays a demo script in time with the music: tool calls in a compact
// form, values kept from results, and waits measured in beats. See src/mcp/ScriptPlayer.h.

#include "mcp/Bridge.h"
#include "mcp/McpManifest.h"
#include "mcp/ScriptPlayer.h"
#include <map>
#include <condition_variable>
#include <deque>
#include <thread>
#include <juce_events/juce_events.h>
#include <iostream>
#include <mutex>
#include <string>

#if JUCE_WINDOWS
 #include <fcntl.h>
 #include <io.h>
#endif

namespace
{
std::mutex outputMutex;

void writeLine (const std::string& line)
{
    const std::lock_guard<std::mutex> lock (outputMutex);
    std::cout << line << '\n';
    std::cout.flush();
}

std::mutex consoleMutex;
bool watchMode = false;

void log (const juce::String& message)
{
    const std::lock_guard<std::mutex> lock (consoleMutex);
    std::cerr << "[mcp-daw-bridge] " << message.toStdString() << std::endl;
}

void console (const juce::String& text)
{
    const std::lock_guard<std::mutex> lock (consoleMutex);
    std::cerr << text.toStdString() << std::endl;
}

juce::String clip (juce::String s, int maxChars = 92)
{
    s = s.replace ("\n", " ").replace ("\r", " ");
    return s.length() <= maxChars ? s : s.substring (0, maxChars - 3) + "...";
}

/** "> import_audio {path: ...}" - what a watcher needs to follow the session. */
juce::String describeRequest (const juce::var& msg)
{
    const auto method = msg["method"].toString();
    if (method != "tools/call") return "  > " + method;
    const auto params = msg["params"];
    auto args = juce::JSON::toString (params["arguments"], true);
    // Paths are long and mostly noise on screen: keep only the file name.
    args = args.replaceCharacter ('\\', '/');
    juce::String compact;
    for (int i = 0;;)
    {
        const int q = args.indexOfChar (i, '"'), e = q < 0 ? -1 : args.indexOfChar (q + 1, '"');
        if (e < 0) { compact << args.substring (i); break; }
        const auto token = args.substring (q + 1, e);
        compact << args.substring (i, q + 1)
                << (token.containsChar ('/') && token.length() > 24 ? ".../" + token.fromLastOccurrenceOf ("/", false, false) : token)
                << '"';
        i = e + 1;
    }
    return "  > " + params["name"].toString() + "  " + compact;
}

/** "ok {...}" or "ERROR ..." for a reply line. */
juce::String describeReply (const juce::var& msg)
{
    if (msg.hasProperty ("error")) return "    ERROR " + msg["error"]["message"].toString();
    const auto result = msg["result"];
    if (result.hasProperty ("tools")) return "    ok  " + juce::String (result["tools"].size()) + " tools";
    if (result.hasProperty ("serverInfo"))
        return "    ok  " + result["serverInfo"]["name"].toString() + " " + result["serverInfo"]["version"].toString()
               + ", protocol " + result["protocolVersion"].toString();
    if (static_cast<bool> (result["isError"]))
        return "    ERROR " + result["content"][0]["text"].toString();
    if (result.hasProperty ("structuredContent"))
        return "    ok  " + juce::JSON::toString (result["structuredContent"], true);
    return "    ok";
}

juce::File findApplication (const juce::StringArray& args)
{
    const int i = args.indexOf ("--app");
    if (i >= 0 && i + 1 < args.size()) return juce::File (args[i + 1]);
    const auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
   #if JUCE_WINDOWS
    for (const auto& candidate : { dir.getChildFile ("MCP-DAW.exe"),
                                   dir.getSiblingFile ("MCPDAW_artefacts").getChildFile ("Release/MCP-DAW.exe") })
   #else
    for (const auto& candidate : { dir.getChildFile ("MCP-DAW"), dir.getChildFile ("MCP-DAW.app") })
   #endif
        if (candidate.exists()) return candidate;
    return {};
}

/** Reads stdin on its own thread so the main thread can release the seat on time while idle. */
class StdinReader
{
public:
    StdinReader() : thread ([this] { run(); }) { thread.detach(); }   // blocked in getline at exit: let the OS reap it

    /** Next line, or false on timeout. `eof` becomes true once stdin has closed and all lines are taken. */
    bool next (std::string& line, int timeoutMs, bool& eof)
    {
        std::unique_lock<std::mutex> lock (mutex);
        ready.wait_for (lock, std::chrono::milliseconds (timeoutMs), [this] { return ! lines.empty() || closed; });
        eof = lines.empty() && closed;
        if (lines.empty()) return false;
        line = std::move (lines.front());
        lines.pop_front();
        return true;
    }

private:
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> lines;
    bool closed = false;
    std::thread thread;

    void run()
    {
        std::string line;
        while (std::getline (std::cin, line))
        {
            if (! line.empty() && line.back() == '\r') line.pop_back();
            const std::lock_guard<std::mutex> lock (mutex);
            lines.push_back (std::move (line));
            ready.notify_one();
        }
        const std::lock_guard<std::mutex> lock (mutex);
        closed = true;
        ready.notify_one();
    }
};

/** --script: plays a demo script through an in-process bridge (replies stay here, never on stdout). */
int runScript (const juce::File& file, mcpdaw::mcp::BridgeOptions options, bool fast, bool verbose)
{
    if (! file.existsAsFile())
    {
        std::cerr << "Script not found: " << file.getFullPathName().toStdString() << std::endl;
        return 2;
    }

    std::mutex repliesLock;
    std::map<juce::String, juce::var> replies;   // JSON-RPC id (as text) -> reply
    mcpdaw::mcp::Bridge bridge (std::move (options),
                                [&] (const juce::String& line)
                                {
                                    const auto msg = juce::JSON::parse (line);
                                    if (! msg.hasProperty ("id")) return;
                                    const std::lock_guard<std::mutex> lock (repliesLock);
                                    replies[juce::JSON::toString (msg["id"], true)] = msg;
                                },
                                [] (const juce::String& message) { log (message); });

    auto sleepTicking = [&bridge] (int ms)
    {
        for (const auto until = juce::Time::getMillisecondCounterHiRes() + ms;;)
        {
            const double left = until - juce::Time::getMillisecondCounterHiRes();
            if (left <= 0) break;
            juce::Thread::sleep (static_cast<int> (std::min (left, 20.0)));
            bridge.tick();
        }
    };

    mcpdaw::mcp::script::Player::Hooks hooks;
    hooks.request = [&] (const juce::var& request) -> juce::var
    {
        const auto id = juce::JSON::toString (request["id"], true);
        bridge.handleLine (juce::JSON::toString (request, true));
        for (const auto started = juce::Time::getMillisecondCounterHiRes();
             juce::Time::getMillisecondCounterHiRes() - started < 180000.0;)   // renders can take a while
        {
            {
                const std::lock_guard<std::mutex> lock (repliesLock);
                if (auto it = replies.find (id); it != replies.end())
                {
                    auto reply = it->second;
                    replies.erase (it);
                    return reply;
                }
            }
            juce::Thread::sleep (1);
            bridge.tick();
        }
        return {};
    };
    hooks.notify = [&] (const juce::var& message) { bridge.handleLine (juce::JSON::toString (message, true)); };
    hooks.nowMs = [] { return juce::Time::getMillisecondCounterHiRes(); };
    hooks.sleep = sleepTicking;
    hooks.narrate = [] (const juce::String& text) { console ("\n  " + text); };
    hooks.log = [] (const juce::String& text) { console (text); };

    mcpdaw::mcp::script::Player::Options playerOptions;
    playerOptions.fast = fast;
    playerOptions.verbose = verbose;
    playerOptions.vars["DEMO"] = file.getParentDirectory().getFullPathName();

    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());
    mcpdaw::mcp::script::Player player (std::move (playerOptions), std::move (hooks));
    const bool ok = player.run (lines);
    if (! ok) console ("\n  The demo stopped: " + player.error());
    bridge.shutdown();
    return ok ? 0 : 1;
}
} // namespace

int main (int argc, char* argv[])
{
   #if JUCE_WINDOWS
    _setmode (_fileno (stdout), _O_BINARY);   // '\n' framing, never "\r\n"
    _setmode (_fileno (stdin), _O_BINARY);
   #endif
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));

    if (args.contains ("--help") || args.contains ("-h"))
    {
        std::cerr << "mcp-daw-bridge: MCP stdio bridge for MCP DAW\n"
                     "  --app <path>   MCP DAW executable to launch if it is not running\n"
                     "  --no-launch    never start MCP DAW (tool calls report that it isn't running)\n"
                     "  --pace <ms>    watch mode: one request at a time, <ms> pause after each reply,\n"
                     "                 console summary; '# text' lines narrate, '@wait <ms>' pauses\n"
                     "  --script <file> play a demo script in time with the music (see src/mcp/ScriptPlayer.h)\n"
                     "                 --fast: no waiting (testing)   --verbose: show every call\n"
                     "  pipe: " << mcpdaw::mcp::pipeName().toStdString() << "   seat: " << mcpdaw::mcp::seatName().toStdString() << "\n"
                     "MCP DAW is only contacted (or started) when a tool is actually called.\n";
        return 0;
    }

    int paceMs = 0;
    if (const int p = args.indexOf ("--pace"); p >= 0 && p + 1 < args.size())
    {
        paceMs = juce::jmax (0, args[p + 1].getIntValue());
        watchMode = true;
    }

    mcpdaw::mcp::BridgeOptions options;
    options.manifest = juce::JSON::parse (mcpdaw::mcp::manifestJson());
    if (! args.contains ("--no-launch"))
    {
        const auto app = findApplication (args);
        if (app.exists()) options.launchApp = [app] { return app.startAsProcess(); };   // only ever used by a tool call
    }

    if (const int sc = args.indexOf ("--script"); sc >= 0)
    {
        if (sc + 1 >= args.size()) { std::cerr << "--script needs a file\n"; return 2; }
        return runScript (juce::File::getCurrentWorkingDirectory().getChildFile (args[sc + 1]), std::move (options),
                          args.contains ("--fast"), args.contains ("--verbose"));
    }

    mcpdaw::mcp::Bridge bridge (std::move (options),
                                [] (const juce::String& line)
                                {
                                    writeLine (line.toStdString());
                                    if (watchMode) console (clip (describeReply (juce::JSON::parse (line))));
                                },
                                [] (const juce::String& message) { log (message); });

    StdinReader input;
    std::string line;
    for (bool eof = false; ! eof;)
    {
        if (! input.next (line, 100, eof)) { bridge.tick(); continue; }
        if (line.empty()) continue;

        // Script directives (never sent to the app).
        if (line[0] == '#')
        {
            if (watchMode) console ("\n>> " + juce::String::fromUTF8 (line.c_str() + 1).trim());
            continue;
        }
        if (line.rfind ("@wait", 0) == 0)
        {
            const int ms = juce::String (line.substr (5)).trim().getIntValue();
            if (watchMode) console ("    (listening " + juce::String (ms / 1000.0, 1) + " s)");
            for (const auto until = juce::Time::currentTimeMillis() + ms; juce::Time::currentTimeMillis() < until;)
            {
                juce::Thread::sleep (100);
                bridge.tick();
            }
            continue;
        }

        const auto text = juce::String::fromUTF8 (line.c_str());
        if (watchMode)
            if (const auto parsed = juce::JSON::parse (text); parsed.isObject()) console (clip (describeRequest (parsed)));
        bridge.handleLine (text);
        bridge.tick();
        if (watchMode)
        {
            // One at a time: wait for this reply (renders can take a while), then give the eye a moment.
            for (int i = 0; i < 6000 && bridge.pendingReplies() > 0; ++i)
            {
                juce::Thread::sleep (20);
                if (i % 5 == 0) bridge.tick();
            }
            juce::Thread::sleep (paceMs);
        }
    }
    // stdin closed: let in-flight requests finish (e.g. piped scripts), up to 60 s.
    for (int i = 0; i < 600 && bridge.pendingReplies() > 0 && bridge.isConnected(); ++i)
    {
        juce::Thread::sleep (100);
        bridge.tick();
    }
    bridge.shutdown();
    return 0;
}
