# Example songs

Two complete songs, each with the Python script that **builds it live in MCP DAW over MCP**: the
same tools Claude, ChatGPT or your own AI use. Open a project to hear and pick it apart, or run a
script to watch the song being made, then change it. These were made by Claude in MCP DAW with
Ken Graham, as a starting point for you and your AI.

| Folder | Song | Style | Length |
| --- | --- | --- | --- |
| `tears-in-the-dust/` | **Tears in the Dust** | Blade Runner-style synth pads over a Jay Dilla-style break | 16 bars, 86 BPM, F minor |
| `croydon-after-dark/` | **Croydon After Dark** | Early dubstep in the style of a Benga live set | 32 bars, 140 BPM, F minor |

Each folder has the script (`*.py`) and the finished project (`*.mcpdaw`). Shared samples are in
`samples/`; the rest come from `Samples-Starter-Kit/` at the top of the repo (or of the app folder).

## Listen

Open a `.mcpdaw` in MCP DAW (File → Open, or `open_project` over MCP) and press Space. The sample
paths are relative, so the projects open wherever this folder is - as long as `samples/` and the
`Samples-Starter-Kit` folder stay where they are. (If you save a copy somewhere else, MCP DAW
stores the paths of samples outside the project's own folder in full; that copy keeps working on
your machine.)

## Rebuild or remix

```
python tears-in-the-dust/blade_dilla.py              # builds the published song in MCP DAW
python croydon-after-dark/benga_live.py --seed 42    # same writing, new human details
```

* MCP DAW opens if it isn't running. A script never throws away unsaved work: if the open project
  has unsaved changes it stops and tells you.
* The render and the rebuilt project go to the song's `out/` folder (`--out` to change it).
* The bridge is found automatically next to the app; otherwise pass `--bridge <path to
  mcp-daw-bridge>` or set `MCP_DAW_BRIDGE`.
* `mcp_daw.py` (standard library only, Python 3.9+) is in `tools/` in the repo and in
  `Scripting/` in the app download; the scripts look in both.
* `--seed` changes only the human feel (timing drift, ghost notes, fills, which wobble rate a note
  gets); the harmony, structure and mix stay. The published songs are seed 7 and 2008.

## For AIs: what to learn from these

The scripts are written to be read. The ideas worth borrowing:

**Tears in the Dust** (`blade_dilla.py`)
* *Non-repeating drums*: every bar is generated; kick and snare lateness follow a **random walk**
  (the groove leans back and recovers) rather than random per-hit jitter; swing changes per beat.
* *Chopping*: hits are taken from a breakbeat by `sourceOffsetSeconds`, pitched down and crushed
  (Distortion: bits 11, downsample 2) on their own track, layered just after the main snare.
* *Sustained pads from a loop*: each chord is a short window of a pad loop, chained as overlapping
  events with smooth fades on two alternating tracks (events on one track never overlap - a new
  event trims the one it overlaps).
* *Rhodes shimmer*: a gentle per-note tremolo (`restart: Event`).

**Croydon After Dark** (`benga_live.py`)
* *Filter-like wobble without a filter*: tremolo **before** a wavefolder (Distortion type Fold),
  so each cycle sweeps the tone from clean to folded growl.
* *Changing the wobble rate*: four wobble tracks (1/8, 1/16, 1/4, 1/32) - a note's rate is chosen by
  which track it goes on, like a hand on the controls.
* *Live-set moves*: a rewind (reversed, pitched-up spin-back through a flanger - Chorus with 2 ms
  delay and high feedback), hard cuts to silence, a delay "throw" track, an eager early bass entry,
  a flam, a muted snare, a stab stutter.

Both build everything with `execute_batch` (one undo step per part) and set every effect in one
`add_effect` call; nothing is animated with repeated calls - tremolo, chorus and phaser cycle inside
the engine (see `docs/MODULATION.md`).

## Samples

See `samples/CREDITS.txt`. The loops were synthesised by MCP DAW's own `tools/make_jungle_loops.py`
and `tools/make_demo_loops.py`; the one-shots are copies of the demo's samples.
