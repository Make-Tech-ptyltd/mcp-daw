# Changelog

## 0.5.0 — event editing, modulation effects, loop metadata, project upgrade

**Tremolo, chorus and phaser** (FX rack → *+ MOD*; details in `docs/MODULATION.md`)
* **Tremolo:** one cycle per 1/64 … 1/4 note; sine, triangle or square; depth 0–60 dB (the loudest
  point is unity, the quietest −depth dB; 0 dB = untouched); phase sets where each cycle starts
  (0° loud, 180° silent). Square edges and every jump are smoothed into ~1.5 ms ramps.
* **Chorus** (also flanger and vibrato) and **phaser**: rate in Hz or synced (1/16 … 8 bars), depth,
  feedback, mix, centre delay / centre frequency, stereo spread. The algorithms and controls follow
  `juce::dsp::Chorus` / `juce::dsp::Phaser`; the LFO is MCP DAW's own, because JUCE's free-runs from
  reset with a private phase.
* **Restart = Timeline or Event.** Timeline: cycles line up with the bar grid. Event: every note
  (event onset) on the track restarts the cycle; starting playback, seeking or looping into the middle
  of an event picks up the phase from that event's start, and the two halves of a split event stay
  one note. The phase depends only on the timeline position, so playback, bounces and any audio
  buffer size give the same result.
* **Preset chains:** *Dubstep pulse*, *Dubstep swell*, *Trip-hop drum crunch*, *Cinematic synth
  ambience* insert ordinary, editable effects; every value is saved in the project.
* The modulation runs in the audio engine: it never edits the project, marks it modified or adds undo
  steps.

**Example songs** (`examples/songs/`, and *Examples* in the download)
* *Tears in the Dust* (Blade Runner-style pads over a Jay Dilla-style break, 16 bars) and *Croydon
  After Dark* (a Benga-style dubstep live set, 32 bars): each project plus the Python script that
  builds it live over MCP. `--seed` makes new variations; the scripts are written for people and
  AIs to learn from.


**Event editing**
* **Event gain** by dragging an event's top line (0 dB at the top, down to silence). The waveform is
  drawn at the new level and a dB readout follows the drag; on a selected event the change applies to
  the whole selection (relative, one undo step). Double-click the line for 0 dB.
* **Event fades:** drag an event's top-left / top-right corner for a fade-in / fade-out, drawn over
  the waveform. Each edge has its own curve — Linear, Smooth, Fast or Slow (right-click → *Fades*).
  Fades may overlap (they multiply). Double-click a handle to remove that fade.
* **One fade system with a built-in declick.** Edit → *Editing preferences* → **Default declick
  time** (default 1.5 ms, 0–20 ms) is the minimum length of every event fade, so edges never click.
  A fade handle at zero stores no fade and still gets the 1.5 ms anti-click ramp; set 0 ms for truly
  hard edges. The preference is not stored in projects. (This replaces 0.4.0's fixed ~1.3 ms
  declick, which only applied where an event cut into the audio — see *Behaviour changes*.)
* **Reverse event:** press **U** (or right-click → *Reverse*). Non-destructive: the audio file is not
  changed and other events using it stay forwards. Works with trimming, source offsets, looping,
  pitch, gain, fades and Render; the waveform is drawn reversed.
* **Slip edit:** **Alt+drag** an event to move the audio underneath it while the event stays put
  (looping events wrap, one-shots stop at the start/end of their asset).
* **Modifier change:** **Shift** during any drag now bypasses snapping (it used to be Alt). Alt+drag
  is slip; Ctrl+drag is still copy.
* Paste now keeps each event's gain, fades and reverse.

**Embed Loop Metadata**
* File → **Embed Loop Metadata…** (or right-click an event → *Embed Loop Metadata in source WAV…*)
  writes ACID-compatible loop metadata into a WAV: type (loop / one-shot), length in beats, tempo,
  time signature, root note, stretch. It shows the calculated duration (beats × 60 / BPM) next to
  the file's real duration and flags any mismatch rather than guessing; *Tempo from length* fixes
  the tempo on request.
* The audio is never decoded or re-encoded: a small RIFF/WAVE chunk editor replaces or inserts the
  `acid` chunk, copies every other chunk (including the PCM `data` chunk) byte for byte, checks the
  result (SHA-256 of the audio data, and the metadata read back), and only then replaces the file.
  *Save Copy…* leaves the original untouched. RF64/Wave64 files are refused.

**Projects**
* Project format **v2** (adds event fades, fade curves and reverse; projects may also contain the new
  effect types). v1 projects from 0.4.0 open
  normally and are upgraded in memory; MCP DAW offers **Save Upgraded Copy…** (`<name>-v2.mcpdaw`)
  and never silently overwrites the older file — Save asks for a new name until the upgraded project
  has been saved to one. Projects from newer versions are refused as before.

**AI control (MCP)** — 62 tools
* New: `set_event_fades`, `set_event_reverse`, `slip_event`, `get_wav_loop_metadata`,
  `write_wav_loop_metadata`, `set_default_declick`, `add_effect_preset`.
* Effect types `tremolo`, `chorus`, `phaser` for `add_effect`.
* Smaller responses on request, existing responses unchanged by default: `detail: "compact"` on
  `get_project_state` / `get_track` (each effect as its non-default values only), `add_effect`,
  `set_effect_parameters` (only the values just set) and `execute_batch` (each step's ids only;
  `$N` references still see full results); `get_effect_parameters` takes `detail: "values"` or
  `"compact"`; `list_effect_types` takes `type` (one effect) or `brief`.
* `create_event` takes `fadeInBeats`, `fadeOutBeats`, `fadeInCurve`, `fadeOutCurve`, `gainDb` and
  `reversed`; `set_event_gain` takes `eventIds` for several events at once.
* Project state shows each event's `reversed`, fades and curves, and the effective (rendered) fade
  lengths; `editing.defaultDeclickMs` shows the preference.
* `open_project` reports a `formatUpgrade`; `save_project` without a path refuses to overwrite an
  older-format original and names the suggested copy.

**Behaviour changes**
* Every event edge now gets the declick (default 1.5 ms), including edges that start or end exactly
  at the asset's start/end. Very sharp transients (a kick's first millisecond) are softened by a
  1.5 ms ramp, and back-to-back copies of a loop get a ~3 ms dip at each join. Setting the Default
  declick time to 0 ms removes the automatic ramp everywhere (hard edges; draw fades where needed).
* Effect displays show a + sign only on boost/cut ranges: Drive, Makeup and Knee read "12.0 dB",
  not "+12.0 dB".

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
