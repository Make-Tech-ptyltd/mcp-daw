# Scripting MCP DAW

MCP DAW can be driven by anything that speaks MCP (Claude, ChatGPT/Codex, Cursor…) — and by plain
Python scripts. This page is for the second case, and for AI models that can't use MCP directly but
can **write a script** for you to run.

Everything you need:

* `mcp_daw.py` — a small client (Python 3.9+, standard library only). Put your script next to it.
* [`TOOLS.md`](TOOLS.md) — every tool, its arguments, and every effect parameter.
* MCP DAW itself (`MCP-DAW.exe` + `mcp-daw-bridge.exe`). It doesn't need to be open: the first
  command opens it.

> **Tip for AI models:** give the model this file and `TOOLS.md` and ask it to write a script that
> uses `mcp_daw.py`. The rules below are all it needs.

## Hello, MCP DAW

```python
from mcp_daw import McpDaw

with McpDaw(client_name="hello-beat") as daw:          # "hello-beat" shows in MCP DAW's top bar
    daw.call("new_project", discardChanges=True)       # careful: discards the open project
    daw.call("set_project_tempo", bpm=100)

    kick = daw.call("import_audio", path="C:/Samples/kick.wav", place=False)["assetId"]
    track = daw.call("create_track", name="Kick")["trackId"]

    daw.batch("Four on the floor", [                   # one undo step
        {"action": "create_event",
         "arguments": {"trackId": track, "assetId": kick, "startBeat": beat, "looping": False}}
        for beat in range(16)])                        # 4 bars

    daw.call("set_loop_region", startBeat=0, endBeat=16, enabled=True)
    daw.call("play", fromBeat=0)
```

Run it with `python hello_beat.py` (or double-click, if `.py` files open with Python).

## The client

| Method | What it does |
| --- | --- |
| `McpDaw(bridge=None, client_name="python-script")` | Connects. Finds `mcp-daw-bridge` next to the script / MCP DAW, in `MCP_DAW_BRIDGE`, on `PATH`, or pass `bridge=`. |
| `daw.call(tool, **arguments)` | Runs one tool and returns its result (a dict, or a list for list-type tools). Raises `ToolError` with MCP DAW's explanation if it fails. |
| `daw.batch(label, actions)` | Runs up to 512 actions as **one undo step**, all-or-nothing. |
| `daw.tools()` | The tool list (names, descriptions, argument schemas). |
| `daw.close()` | Finishes (automatic with `with McpDaw(...) as daw:`). |

## The rules

**Time is in beats.** Positions and lengths are zero-based quarter notes. 4/4 only, so bar N starts
at beat `(N − 1) × 4`: bar 1 = beat 0, bar 2 = beat 4. A 16th note is 0.25 beats.

**Levels are dB** (−70 = silent, 0 = unchanged), **pan** is −1 (left) … +1 (right), **pitch** is
semitones (−12 … +12).

**Loops vs one-shots.** `create_event(..., looping=True)` repeats the sound to fill `lengthBeats`
(drum loops, pads). `looping=False` plays it once (kicks, snares, stabs). Files shorter than 2 s
without a tempo tag default to one-shots.

**No time-stretching (yet).** Audio plays at its own speed. Use loops recorded at the project tempo
(`set_project_tempo`), or build grooves from one-shots — those follow any tempo, even if you change it
while playing.

**Ids.** Tools return ids (`trackId`, `assetId`, `id` for events, `effectId`); keep them and pass
them back. `get_project_state` shows everything at any time.

**Batches.** `execute_batch` / `daw.batch()` runs many steps as one undo step. A step can use an
earlier step's result with `"$<step>.<field>"`:

```python
daw.batch("Snare track", [
    {"action": "create_track", "arguments": {"name": "Snare"}},
    {"action": "add_effect", "arguments": {"target": "$0.trackId", "type": "reverb",
                                           "parameters": {"mix": 0.2, "decay": 1.2}}},
])
```

References only reach back within the same batch. More than 512 steps: pass a list of lists and
each part becomes its own undo step.

**Effects.** `add_effect(target=<trackId or "master">, type=..., parameters={...})`, then
`set_effect_parameters(effectId, values={...})`. Types: `eq`, `compressor`, `reverb`,
`multitap_delay`, `distortion`. Parameter ids and ranges are in [`TOOLS.md`](TOOLS.md#effect-parameters);
choice parameters accept names (`"tap1.division": "1/8D"`, `"type": "Warm"`).

**Taking turns.** One AI or script uses MCP DAW at a time. If another holds it, your call waits up
to ~25 s for its turn, then raises `ToolError("MCP DAW is in use by …")`. Your script gives the
connection back automatically 20 s after its last command (in the default *Take turns* mode), so a
script that pauses for longer simply reconnects on its next call.

**Undo.** Every edit is undoable — by you with Ctrl+Z, or with `daw.call("undo")`.

## Useful patterns

```python
# Import once, place many times (import_audio with place=False only loads the file).
snare = daw.call("import_audio", path=path, place=False)["assetId"]

# A drum pattern from a 16-step string.
def pattern(track, asset, bar, steps, gain_db=0):
    acts = []
    for i, c in enumerate(steps):
        if c == "x":
            acts.append({"action": "create_event", "arguments": {
                "trackId": track, "assetId": asset, "startBeat": (bar - 1) * 4 + i * 0.25, "looping": False}})
    return acts

daw.batch("Snare bar 1", pattern(snare_track, snare, 1, "....x.......x..."))

# Copy bars 1-4 to bars 5-8 on every track.
daw.call("duplicate_range", startBeat=0, endBeat=16, destBeat=16)

# Render the result.
daw.call("render_mix", path="C:/Music/my-beat.wav", overwrite=True)
```

The example scripts in this folder (`dilla_beat.py`, `dnb_rollercoaster.py`, `sine_bass.py`, …) are
complete, commented pieces you can copy from.

## Timed scripts without Python (`--script`)

`mcp-daw-bridge --script <file>` plays a plain-text script in time with the music - it is what
`Demo/Run Demo.cmd` runs. One instruction per line:

| Line | Meaning |
| --- | --- |
| `# text` | Narration, printed in the console window. |
| `// text` | Comment. |
| `tool {json}` | Call a tool, e.g. `create_track {"name":"KICK"}`. |
| `tool {json} => kick=trackId` | ...and keep a value from the result (`results.3.id` reaches into a batch). |
| `?tool {json}` | A call whose failure is reported but does not stop the script. |
| `${kick}` | A kept value (JSON-escaped). `${DEMO}` is the script's folder. |
| `@at 32` | Wait until 32 beats have played since `play` (tempo changes are taken into account). |
| `@sync 32` | Check the clock against MCP DAW's playhead; the number is how many beats the playhead has been sent back by loop jumps. |
| `@wait 500` / `@pace 60` | Pause; pause after every call. |
| `@client Name` / `@end` | The name shown in MCP DAW's top bar; stop. |

Any failed call stops the script, so it never carries on in a broken state. Add `--fast` to run a
script without waiting (for testing) and `--verbose` to list every call with its round-trip time.

The demo's own script is generated by `tools/make_demo.py` from the three projects that ship in
`demo/` (`kens-demo-2.mcpdaw`, `kens-demo-3.mcpdaw`, `kens-demo-1.mcpdaw`, in playing order). Their
samples live in `demo/samples` and are referenced relatively, so the projects open anywhere:

```bash
python tools/make_demo.py build                  # demo/kens-demo-*.mcpdaw -> demo/demo-script.txt
# Edited a song and saved it somewhere else, or added samples from elsewhere? Re-ship it first:
python tools/make_demo.py ship "my demo 2.mcpdaw" "my demo 3.mcpdaw" "my demo 1.mcpdaw"
#   copies their samples to demo/samples and the projects to demo/kens-demo-N.mcpdaw (paths -> samples/...)
```

