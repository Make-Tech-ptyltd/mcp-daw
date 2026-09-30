# Changelog

## 0.4.0 — first public beta

**Arranging**
* Loop and one-shot arranger: drag WAVs in, drag a clip's right edge to repeat it, paint a track's
  sound along a lane, move/copy/trim/split, snap, zoom, Ctrl+drag box select, clipboard.
* Tempo detection from ACID-chunk WAV metadata and `90bpm`-style file names; short untagged files
  are placed as one-shots.
* Per-clip pitch −12…+12 semitones (sampler-style varispeed).
* *Replace audio* on a track, with hover-to-audition from the browser folder.
* Mixer: gain, pan, mute, solo, meters, master with clip indicator.
* Effects: 6-band parametric EQ, compressor, FDN reverb, 8-tap tempo-synced delay, 4×-oversampled
  distortion (Soft/Warm/Hard/Fuzz/Fold, crusher).
* Offline render to WAV; `.mcpdaw` JSON projects; full undo/redo.
* Dockable panels that can move to a second monitor.

**Demo**
* `Demo/Run Demo.cmd`: a four-minute show built live through MCP - three songs written, performed,
  re-patched and tempo-braked while they play, ending with the last one looping. No Python needed.
* The three demo songs ship as projects (`Demo/kens-demo-1/2/3.mcpdaw`) to open, pull apart and remix.
* `Samples-Starter-Kit`: kicks, snares, claps, toms, cymbals, reverse FX, 55 Hz bass notes and vinyl
  crackle. The browser opens it on first run (and lists it under *Places*).
* Help → *Source code on GitHub*; the About box shows where the source is.

**AI control (MCP)**
* 55 MCP tools that mirror everything the GUI can do, plus atomic `execute_batch` (one undo step,
  `$N.field` references).
* `mcp-daw-bridge`: AI apps starting up never open MCP DAW; the first real command does.
* One seat, shared by taking turns (released after 20 s idle) or locked to the first AI.
* Top bar shows who is connected; the status bar shows each command as it arrives.
* Works with Claude Desktop (chat, Cowork, Code), ChatGPT/Codex, Cursor/VS Code and Python scripts
  (`mcp_daw.py`, see `docs/SCRIPTING.md`).
* `mcp-daw-bridge --script`: timed scripts without Python - calls waited on in beats, values kept
  from results, clock checked against the playhead.
* `execute_batch` is applied in one go: the audio engine never plays half a batch, and large
  batches are much faster. `zoom_to_fit` can frame a beat range.

**Platforms**
* Windows 10/11 (release zip). Linux (Ubuntu 22.04+) builds from source.
