# MCP DAW by Make Tech

A loop and one-shot music arranger that you and an AI work on **together, in the same live
project**. Arrange by hand — drag, loop, pitch, mix, add effects — while Claude, ChatGPT or your own
script does the same through **MCP** (Model Context Protocol). You watch every command arrive, and
everything can be undone.

Built by Make Tech Pty Ltd — [maketech.com.au](https://maketech.com.au). Free and open source (AGPLv3).

**Version 0.4.0 — public beta.** Windows 10/11 app; Linux builds from source.

## Get it

* **Just the app:** download `MCP-DAW-0.4.0-win64.zip` from the
  [Releases](../../releases) page, unzip, and follow **[Quick Start](docs/QUICKSTART.md)**
  (includes the one-click demo and how to connect Claude or ChatGPT).
* **The source:** this repository — see **[Building](docs/BUILDING.md)** (Windows and Ubuntu 22.04+).
* **Sounds:** the zip (and this repo) include `Samples-Starter-Kit/`, which the browser opens on
  first run, and the demo's songs and samples in `demo/`.
* **Scripting:** control MCP DAW from Python with `tools/mcp_daw.py` — see
  **[Scripting](docs/SCRIPTING.md)** and the full **[tool reference](docs/TOOLS.md)**.

## How the AI connection works

MCP apps launch `mcp-daw-bridge` (it sits next to `MCP-DAW.exe`), which talks to the **running** MCP
DAW over a per-user local pipe — no network port, nothing reachable from another machine.

* **Lazy:** an AI app starting up never opens MCP DAW. The bridge answers start-up questions
  (`initialize`, `tools/list`) from a built-in copy of the tool list, generated from the app's own
  actions at build time. The first real command connects, and opens MCP DAW if it isn't open.
* **One seat, taking turns:** one AI or script works at a time. With *Take turns* (default, in
  Help → MCP / AI control setup) the connection is handed back after 20 s without commands and taken
  again on the next call, so Claude, ChatGPT and scripts can alternate; a call that arrives meanwhile
  waits up to 25 s. Untick it to lock MCP DAW to the first AI that connects.
* **You can see it:** the top bar shows who is connected (**MCP: Claude**), and the status bar shows
  each command as it arrives: `Claude ▸ create_event ×12 · set_event_gain · play`.
* **Same actions as the GUI:** the 55 tools are generated from the app's own action registry, plus
  `execute_batch` for atomic multi-step edits (one undo step, with `$N.field` result references).

Help → *MCP / AI control setup* copies ready-to-paste configs, e.g. for Claude Desktop:

```json
{ "mcpServers": { "mcp-daw": { "command": "C:\\path\\to\\mcp-daw-bridge.exe" } } }
```

## Using it

* Drag WAVs from the browser (or Explorer) onto the arrangement. The first tempo-tagged loop
  (`*_90bpm.wav`, or an ACID chunk) sets the project tempo.
* Drag an event's **right edge** to repeat the loop; drag in an **empty lane** to paint the
  track's loop; drag the body to move (Ctrl = copy, Alt = no snap).
* **Select:** hold **Ctrl** and drag in an empty lane to rubber-band select across tracks
  (Ctrl+Shift adds to the selection); Ctrl+click toggles single clips. Then drag to move, Ctrl+drag
  to copy, Ctrl+C / Ctrl+V to paste at the cursor, Del to delete. Short hits are grabbed as a
  whole at normal zoom (zoom in to trim their edges).
* Space play/stop (returns to start point), Enter pause, S split at cursor, Ctrl+D duplicate,
  L loop on/off, drag the ruler's top strip to set the loop region, Ctrl+wheel zoom, F fit.
* **Pan:** hold the mouse wheel (middle button) and drag to grab the arrangement and move it
  (same as Shift+wheel, both directions).
* View menu → *… in own window* tears panels off onto a second monitor; layout persists.
* Files under 2 seconds with no tempo tag (kicks, snares, stabs) are placed as **one-shots**;
  tagged loops loop. Toggle either way per event (right-click → Loop).
* **Swap sounds:** right-click a track header → *Replace audio*. It lists the audio files in the
  browser's current folder; rest the pointer on one for ¼ s to audition it, click to swap it in
  for every hit on the track (timing untouched, Ctrl+Z to undo). This is the human-in-the-loop
  step: an agent can lay down the pattern, you choose the sounds.
* **Pitch:** right-click an event → *Pitch* → −12…+12 semitones (applies to all selected events
  if the clicked one is selected). It's sampler-style varispeed: pitch and speed move together,
  so a one-shot pitched up gets shorter (its clip is refitted automatically). One sine sample makes
  a bassline — a 55 Hz (A1) sine at +3 = C2, +5 = D2, +7 = E2, −2 = G1.
* **Distortion** (FX rack → *+ DIST*): Soft / Warm / Hard / Fuzz / Fold curves, drive, bias (even
  harmonics), pre-drive low cut, post tone, bit/sample-rate crusher, auto gain and mix. The graph
  shows the transfer curve. Warm, 6–12 dB, bias 0.2, mix 60 % for a subtle warm bass; Fuzz or Fold
  at 30 dB+ with bits 6 / downsample 4 for brutal and crusty.

Positions are zero-based beats; bar N starts at beat (N−1)×4. Audio plays unstretched (no
time-stretching yet), so use loops at the project tempo, or one-shots, which follow any tempo.

## Architecture

```
src/core     Project model (value type), MusicalTime (960 PPQ ticks), JSON I/O, ProjectController
             (undo/transactions), Actions (registry + JSON-schema'd commands shared by GUI and MCP)
src/dsp      EffectProcessor interface + EQ, compressor, reverb, multi-tap delay, distortion (no allocation in process)
src/engine   PlaybackPlan (immutable, render-ready snapshot), PlanRenderer (shared by live + offline),
             AudioEngine (device callback, transport, lock-free plan swap, retire queue), OfflineRenderer
src/mcp      McpSession (JSON-RPC/MCP over the registry), McpPipeServer (named pipe in the app),
             Bridge (lazy stdio bridge: seat lock, take-turns, handshake replay), NamedLock, CommandFeed
src/ui       Theme, dockable Workspace, Timeline, Transport, Mixer, FX rack, Browser
tools        mcp-bridge (bridge exe + build-time manifest generator), mcp_daw.py client, example
             scripts, tool-reference and icon generators
tests        headless JUCE UnitTest suite (core, commands/undo, DSP signal tests, engine, MCP, bridge)
```

Realtime rules: the audio callback never allocates, locks or does I/O. The message thread builds
a `PlaybackPlan` per committed edit and hands it over with one atomic exchange; the callback
returns the old plan through a wait-free SPSC ring for deletion on the message thread. Effect
processors and gain smoothers live in runtimes that persist across plans (keyed by id), so moving
a fader never resets a reverb tail. Transport position is an integer sample counter.

## Licence

MCP DAW is Copyright (C) 2026 Make Tech Pty Ltd, licensed under the **GNU Affero General Public
License v3.0 or later** — see [`LICENSE`](LICENSE). It is built on JUCE (AGPLv3) and embeds the
Audiowide font (SIL OFL 1.1); see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

*ACID is a trademark of its owner. MCP DAW is an independent project, not affiliated with or
endorsed by Boris FX, MAGIX or Sony; "ACID" appears here only to describe file compatibility
(MCP DAW reads the tempo metadata in ACID-chunk WAV loops).*

## Support the project

MCP DAW is free. If it's useful to you:

* **Donate:** <https://www.paypal.com/ncp/payment/5C7RWQVSVMK4U> (PayPal: maketechptyltd@gmail.com)
* **Feedback / bugs:** ken@maketech.com.au

Both are also in the app under the ♥ menu. Changes are listed in [`CHANGELOG.md`](CHANGELOG.md).
