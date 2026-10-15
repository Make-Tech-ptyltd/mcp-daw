# MCP DAW tools

Generated from MCP DAW by Make Tech 0.5.0 by `tools/gen_tool_reference.py` — 62 tools. Positions and lengths are **beats** (zero-based quarter notes; bar N starts at beat (N−1)×4), levels are **dB**, pan is −1…+1. From Python: `daw.call("<tool>", arg=value, ...)`.

## Inspect

### `get_project_state`

Complete semantic project state: tempo, loop region, assets (with tempo metadata), tracks with events (positions in zero-based beats plus bar.beat.tick strings, loop repetitions, occupied bars) and effect chains. detail="compact" lists each effect's non-default values only (much smaller with EQs and delays).

- `detail` *(optional)* — `full` | `compact`. "full" (default) or "compact": effects as {id, type, bypassed, values} with only non-default values.

### `list_tracks`

Summary of all tracks (id, name, mix settings, occupied bars, event count).

- *(no arguments)*

### `get_track`

Full detail for one track.

- `trackId` — string. Track id.
- `detail` *(optional)* — `full` | `compact`. "full" (default) or "compact": effects as {id, type, bypassed, values} with only non-default values.

### `list_assets`

Imported audio sources with duration, sample rate and tempo metadata.

- *(no arguments)*

### `list_effect_types`

Available native effect types with every parameter (id, range, unit, choices). Pass type for one effect's specification, or brief=true for just the types, names and descriptions.

- `type` *(optional)* — `eq` | `compressor` | `reverb` | `multitap_delay` | `distortion` | `chorus` | `phaser` | `tremolo`. Only this effect type.
- `brief` *(optional)* — boolean. Types, names and descriptions only (no parameters).

### `get_effect_parameters`

Current parameter values of an effect, with specs and display strings. detail="values" omits the specs; "compact" returns only the values that differ from the defaults.

- `effectId` — string. Effect id.
- `detail` *(optional)* — `full` | `values` | `compact`. "full" (default), "values" or "compact".

### `get_history`

Undo/redo availability and labels (oldest first).

- *(no arguments)*

### `get_transport_state`

Playback state, position (beats and bar.beat), device, meters.

- *(no arguments)*

## Project and files

### `new_project`

Start a new empty project. Requires discardChanges=true if there are unsaved changes.

- `discardChanges` *(optional)* — boolean. Confirm losing unsaved changes.

### `open_project`

Open a .mcpdaw project. Requires discardChanges=true if there are unsaved changes.

- `path` — string. Absolute path.
- `discardChanges` *(optional)* — boolean. Confirm losing unsaved changes.

### `save_project`

Save the project as JSON (.mcpdaw). Without path, saves to the current file. Writing over a different existing file requires overwrite=true.

- `path` *(optional)* — string. Absolute path.
- `overwrite` *(optional)* — boolean. Allow replacing an existing file.

### `set_project_name`

Rename the project.

- `name` — string. New name.

### `set_project_tempo`

Set project tempo in BPM. Audio plays unstretched (no time-stretch yet): events keep their beat positions, so one-shots follow the new tempo while loops keep their own speed.

- `bpm` — number (20.0..300.0). Tempo.

### `render_mix`

Bounce the mix to a WAV file (offline, deterministic, independent of live playback). Default range: start to end of content plus a tail for reverb/delay.

- `path` — string. Absolute .wav path.
- `startBeat` *(optional)* — number (0.0..). Range start (default 0).
- `endBeat` *(optional)* — number (0.0..). Range end (default: end of content).
- `sampleRate` *(optional)* — number (8000.0..384000.0). Default 48000.
- `bitDepth` *(optional)* — integer (16.0..32.0). 16, 24 (default) or 32 float.
- `tailSeconds` *(optional)* — number (0.0..60.0). Extra seconds after the range (default 2).
- `overwrite` *(optional)* — boolean. Allow replacing an existing file.

## Tracks and mixing

### `create_track`

Create an empty audio track. Returns its id.

- `name` *(optional)* — string. Track name.
- `index` *(optional)* — integer (0.0..). Insert position (0 = top).

### `delete_track`

Delete a track with all of its events and effects.

- `trackId` — string. Track id.

### `rename_track`

Rename a track.

- `trackId` — string. Track id.
- `name` — string. New name.

### `move_track`

Reorder a track.

- `trackId` — string. Track id.
- `index` — integer (0.0..). New index (0 = top).

### `set_track_colour`

Set a track's colour (palette index 0..7).

- `trackId` — string. Track id.
- `colour` — integer (0.0..7.0). Palette index.

### `set_track_gain`

Set track fader level in dB (-70 = silent, max +12).

- `trackId` — string. Track id.
- `db` — number (-70.0..12.0). Gain in dB.

### `set_track_pan`

Set track pan (-1 = left, 0 = centre, +1 = right).

- `trackId` — string. Track id.
- `pan` — number (-1.0..1.0). Pan position.

### `mute_track`

Mute or unmute a track.

- `trackId` — string. Track id.
- `enabled` — boolean. Muted.

### `solo_track`

Solo or unsolo a track (any solo silences non-soloed tracks).

- `trackId` — string. Track id.
- `enabled` — boolean. Soloed.

### `set_master_gain`

Set master bus level in dB.

- `db` — number (-70.0..12.0). Gain in dB.

## Audio and events

### `import_audio`

Import an audio file (WAV; AIFF/FLAC also decode) and by default place it as an event. Without trackId a new track named after the file is created. If the project is empty and the file carries tempo (ACID chunk or '90bpm' in the name) the project adopts that tempo. Returns assetId, trackId, eventId.

- `path` — string. Absolute file path.
- `trackId` *(optional)* — string. Existing track to place on.
- `startBeat` *(optional)* — number (0.0..). Placement (zero-based beats, default 0).
- `lengthBeats` *(optional)* — number (0.0..). Event length (default: the file's natural length).
- `sourceBpm` *(optional)* — number (0.0..300.0). Override the file's tempo (0 = not tempo-based).
- `place` *(optional)* — boolean. Create an event (default true). false = only add to the asset pool.
- `pitchSemitones` *(optional)* — number (-12.0..12.0). Transpose the placed event, -12..+12 (varispeed: also changes its speed).

### `replace_track_audio`

Swap the sound file used by every event on a track (positions, lengths, looping and gains stay; a track still named after its old file is renamed). By default each event restarts from the top of the new file; keepOffsets=true keeps each event's start point inside the source (useful when swapping for a similar take). Returns the new assetId.

- `trackId` — string. Track id.
- `path` — string. Absolute path of the new audio file.
- `keepOffsets` *(optional)* — boolean. Keep each event's source offset (default false).

### `set_asset_bpm`

Declare the native tempo of an asset (0 = not tempo-based / one-shot).

- `assetId` — string. Asset id.
- `bpm` — number (0.0..300.0). Tempo or 0.

### `create_event`

Place an asset on a track. Overlapped parts of existing events on that track are replaced (loop-arranger overwrite, like painting over tape). Optional fades are set in the same step, e.g. a vocal at bar 9 with a two-beat fade-in -> {startBeat: 32, fadeInBeats: 2}. Without fades the event still gets the tiny anti-click fade set by the Default declick time preference.

- `trackId` — string. Track id.
- `assetId` — string. Asset id.
- `startBeat` — number (0.0..). Start (zero-based beats).
- `lengthBeats` *(optional)* — number (0.0..). Length in beats (default: natural length). Longer than the source repeats it when looping.
- `looping` *(optional)* — boolean. Repeat the source to fill the event (default: true for loops, false for one-shots / files under 2 s without tempo).
- `sourceOffsetSeconds` *(optional)* — number (0.0..). Start point inside the source.
- `pitchSemitones` *(optional)* — number (-12.0..12.0). Transpose, -12..+12 semitones (varispeed like a sampler: +12 = octave up, twice as fast).
- `gainDb` *(optional)* — number (-70.0..12.0). Event gain in dB (default 0).
- `reversed` *(optional)* — boolean. Play the event backwards (non-destructive; default false).
- `fadeInBeats` *(optional)* — number (0.0..). Fade-in length in beats (default 0 = only the declick; clipped to the event length).
- `fadeOutBeats` *(optional)* — number (0.0..). Fade-out length in beats (default 0 = only the declick; clipped to the event length).
- `fadeInCurve` *(optional)* — `linear` | `smooth` | `fast` | `slow`. Fade-in curve (default linear).
- `fadeOutCurve` *(optional)* — `linear` | `smooth` | `fast` | `slow`. Fade-out curve (default linear).

### `move_event`

Move an event to a new start (absolute startBeat or relative deltaBeats) and/or another track.

- `eventId` — string. Event id.
- `startBeat` *(optional)* — number (0.0..). New start (zero-based beats).
- `deltaBeats` *(optional)* — number. Relative move in beats (alternative to startBeat).
- `trackId` *(optional)* — string. Destination track.

### `move_events`

Move (or copy, with copy=true) several events at once as one edit. All sources are read before anything is placed, so events moved together never overwrite each other; they only overwrite other audio at their destinations. Returns the resulting event ids in order.

- `moves` — array. [{eventId, startBeat, trackId?}].
- `copy` *(optional)* — boolean. Leave the originals in place (default false).

### `resize_event`

Move an event's edges. endBeat sets the right edge (extending a looping event repeats the loop). startBeat trims/extends the left edge while keeping audio in place (source offset adjusts).

- `eventId` — string. Event id.
- `startBeat` *(optional)* — number (0.0..). New left edge.
- `endBeat` *(optional)* — number (0.0..). New right edge.

### `split_event`

Split an event at an absolute beat. Returns the id of the new right-hand event.

- `eventId` — string. Event id.
- `beat` — number (0.0..). Split position (zero-based beats).

### `duplicate_event`

Duplicate an event. Default places the copy immediately after the original; count > 1 creates back-to-back copies.

- `eventId` — string. Event id.
- `startBeat` *(optional)* — number (0.0..). Start of the first copy.
- `trackId` *(optional)* — string. Destination track (default: same).
- `count` *(optional)* — integer (1.0..256.0). Number of copies (default 1).

### `delete_event`

Delete one event.

- `eventId` — string. Event id.

### `set_event_looping`

Enable/disable looping (repetition of the source) for an event.

- `eventId` — string. Event id.
- `enabled` — boolean. Looping on/off.

### `set_event_gain`

Set event gain in dB (per-event volume; -70 = silent, max +12) on one event (eventId) or several (eventIds) as one undo step.

- `eventId` *(optional)* — string. Event id.
- `eventIds` *(optional)* — array. Several events (alternative to eventId).
- `db` — number (-70.0..12.0). Gain in dB.

### `set_event_pitch`

Transpose one or more events by -12..+12 semitones (fractions allowed for detune). Varispeed, like a hardware sampler: pitch and speed change together, so a +12 one-shot plays an octave higher in half the time. Great for basslines from a single note sample, e.g. a 55 Hz (A1) sine: +3 = C2, +7 = E2, -5 = E1. Positions and source offsets stay. Looping events keep their length; a one-shot that was playing its whole sample is refitted to the sample's new duration (never past the next event on its track).

- `eventIds` — array. Events to transpose.
- `semitones` — number (-12.0..12.0). Pitch in semitones (0 = original).

### `set_event_fades`

Set event fades on one or more events: fade-in/fade-out length in beats and curve (linear, smooth, fast, slow) for each edge. Every argument is optional, so you can change one property and leave the rest, e.g. {eventIds:[...], fadeOutBeats:2}. Fade-in and fade-out may overlap (they multiply). 0 means no deliberate fade: the edge still gets the tiny anti-click fade set by the Default declick time preference (see get_project_state editing.defaultDeclickMs). Lengths beyond an event are clipped to it.

- `eventIds` — array. Events to change.
- `fadeInBeats` *(optional)* — number (0.0..). Fade-in length in beats.
- `fadeOutBeats` *(optional)* — number (0.0..). Fade-out length in beats.
- `fadeInCurve` *(optional)* — `linear` | `smooth` | `fast` | `slow`. Fade-in curve.
- `fadeOutCurve` *(optional)* — `linear` | `smooth` | `fast` | `slow`. Fade-out curve.

### `set_event_reverse`

Reverse event playback on or off for one or more events (explicit state). Non-destructive: the audio file is not changed and other events using it are unaffected. A reversed event plays the same stretch of audio it shows, back to front; fades, gain, looping, trimming and pitch all still apply.

- `eventIds` — array. Events to change.
- `enabled` — boolean. true = reversed, false = forwards.

### `slip_event`

Slip edit: move the source audio underneath an event while the event stays where it is (start, length and track unchanged; only sourceOffsetSeconds changes). deltaBeats moves the audio later (positive = to the right) by that many beats of timeline; or give the absolute sourceOffsetSeconds. Pitch (varispeed) and reverse are accounted for. Looping events wrap around their source; one-shots are kept within their source (a relative move stops at the edge).

- `eventId` — string. Event id.
- `deltaBeats` *(optional)* — number. Relative slip in beats (positive = audio moves right / later).
- `sourceOffsetSeconds` *(optional)* — number (0.0..). Absolute source offset (alternative to deltaBeats).

## Loop metadata (WAV files)

### `get_wav_loop_metadata`

Inspect a WAV file's ACID-compatible loop metadata (type loop/one-shot, tempo, beats, time signature, root note, stretch) and check it against the file's real duration (expected = beats * 60 / bpm). Also says whether write_wav_loop_metadata can safely edit the file (conventional RIFF/WAVE only).

- `path` — string. Absolute .wav path.

### `write_wav_loop_metadata`

Embed Loop Metadata: write ACID-compatible loop metadata into a WAV file without touching its audio (the PCM data chunk is copied byte for byte and verified by SHA-256). Safest: give outputPath to save a copy. Updating the file in place needs overwrite=true. Unspecified fields keep the file's existing values. Loops need bpm (20..300) and beats; check durationMatches in the result. Conventional RIFF/WAVE only (RF64/Wave64 are refused). Assets already in the project keep their current tempo (use set_asset_bpm).

- `path` — string. Absolute path of the source .wav.
- `outputPath` *(optional)* — string. Write a copy here instead of updating the source.
- `overwrite` *(optional)* — boolean. Required to update the source in place, or to replace an existing outputPath.
- `type` *(optional)* — `loop` | `one-shot`. loop (tempo-based, may be stretched) or one-shot.
- `bpm` *(optional)* — number (0.0..300.0). Tempo the material was played at.
- `beats` *(optional)* — integer (0.0..65535.0). Musical length in beats.
- `numerator` *(optional)* — integer (1.0..32.0). Time signature numerator (default 4).
- `denominator` *(optional)* — integer (1.0..32.0). Time signature denominator (default 4).
- `rootNote` *(optional)* — integer (-1.0..127.0). Root key as a MIDI note (60 = C4); -1 = none.
- `stretch` *(optional)* — boolean. Loop may be tempo-matched (default true for loops).

## Preferences

### `set_default_declick`

Application preference (not part of the project, not undoable): the Default declick time in milliseconds, 0..20 (default 1.5). Every event fade renders at least this long, so edges never click; 0 gives genuinely hard event edges. Current value: get_project_state editing.defaultDeclickMs.

- `ms` — number (0.0..20.0). Declick time in milliseconds.

## Arrangement

### `clear_range`

Remove audio between two beats on the given tracks (all tracks if omitted). Events crossing the edges are trimmed/split. Example: remove the kick for bars 1-2 -> {trackIds:[kick], startBeat:0, endBeat:8}.

- `startBeat` — number (0.0..). Range start.
- `endBeat` — number (0.0..). Range end.
- `trackIds` *(optional)* — array. Tracks to affect (default all).

### `duplicate_range`

Copy everything between startBeat and endBeat to destBeat on the given tracks (all if omitted), replacing what was there. Example: duplicate bars 17-24 after bar 24 -> {startBeat:64, endBeat:96, destBeat:96}.

- `startBeat` — number (0.0..). Source start.
- `endBeat` — number (0.0..). Source end.
- `destBeat` — number (0.0..). Destination start.
- `trackIds` *(optional)* — array. Tracks to affect (default all).

## Effects

### `add_effect`

Insert a native effect into a track's chain or the master chain. Optionally set initial parameters (object of parameterId -> number, or choice name). Returns the effect with its id.

- `target` — string. Track id, or "master".
- `type` — `eq` | `compressor` | `reverb` | `multitap_delay` | `distortion` | `chorus` | `phaser` | `tremolo`. Effect type.
- `index` *(optional)* — integer (0.0..). Chain position (default: end).
- `parameters` *(optional)* — object. Initial parameter values.
- `detail` *(optional)* — `full` | `compact`. "full" (default) or "compact": effects as {id, type, bypassed, values} with only non-default values.

### `add_effect_preset`

Insert a factory preset chain - ordinary effects, each fully editable afterwards and saved with all of its values - into a track's chain or the master chain, as one undo step. Presets: dubstep_pulse (distortion -> tremolo); dubstep_swell (tremolo -> distortion); triphop_drum_crunch (compressor -> distortion -> eq -> reverb); cinematic_synth_ambience (eq -> chorus -> reverb). Returns the new effect ids in chain order.

- `target` — string. Track id, or "master".
- `preset` — `dubstep_pulse` | `dubstep_swell` | `triphop_drum_crunch` | `cinematic_synth_ambience`. Preset id.
- `index` *(optional)* — integer (0.0..). Chain position of the first effect (default: end).

### `remove_effect`

Remove an effect from its chain.

- `effectId` — string. Effect id.

### `move_effect`

Reorder an effect within its chain.

- `effectId` — string. Effect id.
- `index` — integer (0.0..). New position (0 = first).

### `set_effect_bypass`

Bypass or re-enable an effect.

- `effectId` — string. Effect id.
- `bypassed` — boolean. Bypassed.

### `set_effect_parameters`

Set one or more effect parameters: values is an object of parameterId -> number (choice parameters also accept their name, e.g. {"tap1.division": "1/8D"}). Use get_effect_parameters or list_effect_types for ids and ranges.

- `effectId` — string. Effect id.
- `values` — object. parameterId -> value.
- `detail` *(optional)* — `full` | `compact`. "full" (default: every parameter value) or "compact" (only the values set by this call).

### `set_effect_parameter`

Set a single effect parameter (see set_effect_parameters).

- `effectId` — string. Effect id.
- `parameter` — string. Parameter id, e.g. band3.gain.
- `value` — ['number', 'string', 'boolean']. Value (number, bool, or choice name).

## Transport

### `play`

Start playback (optionally from a beat).

- `fromBeat` *(optional)* — number (0.0..). Start position.

### `stop`

Stop playback and return to where playback started.

- *(no arguments)*

### `pause`

Stop playback and keep the current position.

- *(no arguments)*

### `seek`

Move the playhead (zero-based beats).

- `beat` — number (0.0..). Position.

### `set_loop_region`

Set the loop region (zero-based beats) and/or enable looped playback.

- `startBeat` *(optional)* — number (0.0..). Loop start.
- `endBeat` *(optional)* — number (0.0..). Loop end.
- `enabled` *(optional)* — boolean. Loop playback on/off.

## Edits as one step, undo

### `execute_batch`

Run several actions atomically as ONE undo step. If any action fails, all changes are rolled back and the error names the failing step. Results are returned in order. Use "$N.field" in a string argument to reference field of the result of step N (0-based), e.g. {"trackId": "$0.trackId"}.

- `actions` — array. Steps: [{action, arguments}].
- `label` *(optional)* — string. Undo label.
- `detail` *(optional)* — `full` | `compact`. "full" (default: every step's result) or "compact" (each result reduced to id, type and *Id/*Ids fields; $N references still see the full results).

### `undo`

Undo the last edit.

- *(no arguments)*

### `redo`

Redo the last undone edit.

- *(no arguments)*

## Window

### `zoom_to_fit`

View only (not an edit, not undoable): fit the whole arrangement into the timeline horizontally and size tracks so they all show. With startBeat/endBeat, fit that range instead (a 'camera' for demos: e.g. 0-32 shows 8 bars; the view still follows the playhead page by page).

- `startBeat` *(optional)* — number (0.0..). First beat to show (default: 0).
- `endBeat` *(optional)* — number (0.0..). Last beat to show (default: end of the arrangement).

### `capture_screenshot`

Save a PNG screenshot of the MCP DAW main window (for visual verification). Returns the path and size.

- `path` — string. Absolute .png path.

## Effect parameters

For `add_effect` (`parameters`) and `set_effect_parameters` (`values`). Choice parameters also accept their name, e.g. `"type": "Warm"`.

### `eq` — Parametric EQ

Six-band parametric EQ: high/low-pass, shelves, bells and notch (RBJ biquads, 12 dB/oct pass filters).

| Parameter | Range | Default | Notes |
| --- | --- | --- | --- |
| `band1.enabled` | on / off | off | Enable this band |
| `band1.type` | High-pass / Low shelf / Bell / High shelf / Low-pass / Notch | High-pass | 0=high-pass 1=low shelf 2=bell 3=high shelf 4=low-pass 5=notch |
| `band1.freq` | 20 .. 20000 Hz | 30 Hz | Centre/corner frequency |
| `band1.gain` | -18 .. 18 dB | 0 dB | Boost/cut (shelves and bells) |
| `band1.q` | 0.1 .. 18 | 0.707 | Bandwidth / resonance |
| `band2.enabled` | on / off | on | Enable this band |
| `band2.type` | High-pass / Low shelf / Bell / High shelf / Low-pass / Notch | Low shelf | 0=high-pass 1=low shelf 2=bell 3=high shelf 4=low-pass 5=notch |
| `band2.freq` | 20 .. 20000 Hz | 100 Hz | Centre/corner frequency |
| `band2.gain` | -18 .. 18 dB | 0 dB | Boost/cut (shelves and bells) |
| `band2.q` | 0.1 .. 18 | 0.707 | Bandwidth / resonance |
| `band3.enabled` | on / off | on | Enable this band |
| `band3.type` | High-pass / Low shelf / Bell / High shelf / Low-pass / Notch | Bell | 0=high-pass 1=low shelf 2=bell 3=high shelf 4=low-pass 5=notch |
| `band3.freq` | 20 .. 20000 Hz | 400 Hz | Centre/corner frequency |
| `band3.gain` | -18 .. 18 dB | 0 dB | Boost/cut (shelves and bells) |
| `band3.q` | 0.1 .. 18 | 0.707 | Bandwidth / resonance |
| `band4.enabled` | on / off | on | Enable this band |
| `band4.type` | High-pass / Low shelf / Bell / High shelf / Low-pass / Notch | Bell | 0=high-pass 1=low shelf 2=bell 3=high shelf 4=low-pass 5=notch |
| `band4.freq` | 20 .. 20000 Hz | 2000 Hz | Centre/corner frequency |
| `band4.gain` | -18 .. 18 dB | 0 dB | Boost/cut (shelves and bells) |
| `band4.q` | 0.1 .. 18 | 0.707 | Bandwidth / resonance |
| `band5.enabled` | on / off | on | Enable this band |
| `band5.type` | High-pass / Low shelf / Bell / High shelf / Low-pass / Notch | High shelf | 0=high-pass 1=low shelf 2=bell 3=high shelf 4=low-pass 5=notch |
| `band5.freq` | 20 .. 20000 Hz | 8000 Hz | Centre/corner frequency |
| `band5.gain` | -18 .. 18 dB | 0 dB | Boost/cut (shelves and bells) |
| `band5.q` | 0.1 .. 18 | 0.707 | Bandwidth / resonance |
| `band6.enabled` | on / off | off | Enable this band |
| `band6.type` | High-pass / Low shelf / Bell / High shelf / Low-pass / Notch | Low-pass | 0=high-pass 1=low shelf 2=bell 3=high shelf 4=low-pass 5=notch |
| `band6.freq` | 20 .. 20000 Hz | 18000 Hz | Centre/corner frequency |
| `band6.gain` | -18 .. 18 dB | 0 dB | Boost/cut (shelves and bells) |
| `band6.q` | 0.1 .. 18 | 0.707 | Bandwidth / resonance |
| `output` | -24 .. 24 dB | 0 dB | Output trim |

### `compressor` — Compressor

Stereo-linked feed-forward compressor with soft knee and parallel mix.

| Parameter | Range | Default | Notes |
| --- | --- | --- | --- |
| `threshold` | -60 .. 0 dB | -18 dB | Threshold |
| `ratio` | 1 .. 20 :1 | 4 :1 | Ratio |
| `attack` | 0.1 .. 200 ms | 10 ms | Attack |
| `release` | 5 .. 2000 ms | 120 ms | Release |
| `knee` | 0 .. 24 dB | 6 dB | Knee |
| `makeup` | 0 .. 24 dB | 0 dB | Makeup |
| `mix` | 0 .. 1 % | 1 % | Parallel (New York) compression blend |

### `reverb` — Reverb

Algorithmic reverb: input diffusion into an 8-line feedback delay network with RT60 decay control.

| Parameter | Range | Default | Notes |
| --- | --- | --- | --- |
| `mix` | 0 .. 1 % | 0.25 % | Wet/dry balance |
| `predelay` | 0 .. 250 ms | 20 ms | Pre-delay |
| `decay` | 0.1 .. 20 s | 1.8 s | RT60 at mid frequencies |
| `size` | 0 .. 1 % | 0.5 % | Room dimension (delay-line scale) |
| `damping` | 0 .. 1 % | 0.5 % | High-frequency decay speed |
| `lowcut` | 20 .. 1000 Hz | 120 Hz | High-pass on the wet signal |
| `width` | 0 .. 1 % | 1 % | Stereo width of the wet signal |

### `multitap_delay` — Multi-Tap Delay

Eight independent taps (tempo-synced or free ms) with level, pan and per-tap feedback into a shared, filtered and saturated feedback path.

| Parameter | Range | Default | Notes |
| --- | --- | --- | --- |
| `tap1.enabled` | on / off | on | On |
| `tap1.sync` | on / off | on | Tempo-synced (division) or free (ms) |
| `tap1.division` | 1/64 / 1/32T / 1/32 / 1/16T / 1/32D / 1/16 / 1/8T / 1/16D / 1/8 / 1/4T / 1/8D / 1/4 / 1/2T / 1/4D / 1/2 / 1/2D / 1 bar / 2 bars | 1/8D | Note value when synced (T=triplet, D=dotted) |
| `tap1.time` | 1 .. 4000 ms | 250 ms | Delay time when not synced |
| `tap1.level` | -60 .. 6 dB | -6 dB | Level |
| `tap1.pan` | -1 .. 1 pan | -0.6 pan | Pan |
| `tap1.feedback` | 0 .. 0.95 % | 0.3 % | Amount of this tap fed back into the delay line |
| `tap2.enabled` | on / off | on | On |
| `tap2.sync` | on / off | on | Tempo-synced (division) or free (ms) |
| `tap2.division` | 1/64 / 1/32T / 1/32 / 1/16T / 1/32D / 1/16 / 1/8T / 1/16D / 1/8 / 1/4T / 1/8D / 1/4 / 1/2T / 1/4D / 1/2 / 1/2D / 1 bar / 2 bars | 1/4 | Note value when synced (T=triplet, D=dotted) |
| `tap2.time` | 1 .. 4000 ms | 250 ms | Delay time when not synced |
| `tap2.level` | -60 .. 6 dB | -9 dB | Level |
| `tap2.pan` | -1 .. 1 pan | 0.6 pan | Pan |
| `tap2.feedback` | 0 .. 0.95 % | 0 % | Amount of this tap fed back into the delay line |
| `tap3.enabled` | on / off | off | On |
| `tap3.sync` | on / off | on | Tempo-synced (division) or free (ms) |
| `tap3.division` | 1/64 / 1/32T / 1/32 / 1/16T / 1/32D / 1/16 / 1/8T / 1/16D / 1/8 / 1/4T / 1/8D / 1/4 / 1/2T / 1/4D / 1/2 / 1/2D / 1 bar / 2 bars | 1/4D | Note value when synced (T=triplet, D=dotted) |
| `tap3.time` | 1 .. 4000 ms | 250 ms | Delay time when not synced |
| `tap3.level` | -60 .. 6 dB | -12 dB | Level |
| `tap3.pan` | -1 .. 1 pan | -0.3 pan | Pan |
| `tap3.feedback` | 0 .. 0.95 % | 0 % | Amount of this tap fed back into the delay line |
| `tap4.enabled` | on / off | off | On |
| `tap4.sync` | on / off | on | Tempo-synced (division) or free (ms) |
| `tap4.division` | 1/64 / 1/32T / 1/32 / 1/16T / 1/32D / 1/16 / 1/8T / 1/16D / 1/8 / 1/4T / 1/8D / 1/4 / 1/2T / 1/4D / 1/2 / 1/2D / 1 bar / 2 bars | 1/2 | Note value when synced (T=triplet, D=dotted) |
| `tap4.time` | 1 .. 4000 ms | 250 ms | Delay time when not synced |
| `tap4.level` | -60 .. 6 dB | -12 dB | Level |
| `tap4.pan` | -1 .. 1 pan | 0.3 pan | Pan |
| `tap4.feedback` | 0 .. 0.95 % | 0 % | Amount of this tap fed back into the delay line |
| `tap5.enabled` | on / off | off | On |
| `tap5.sync` | on / off | on | Tempo-synced (division) or free (ms) |
| `tap5.division` | 1/64 / 1/32T / 1/32 / 1/16T / 1/32D / 1/16 / 1/8T / 1/16D / 1/8 / 1/4T / 1/8D / 1/4 / 1/2T / 1/4D / 1/2 / 1/2D / 1 bar / 2 bars | 1/16 | Note value when synced (T=triplet, D=dotted) |
| `tap5.time` | 1 .. 4000 ms | 250 ms | Delay time when not synced |
| `tap5.level` | -60 .. 6 dB | -12 dB | Level |
| `tap5.pan` | -1 .. 1 pan | 0 pan | Pan |
| `tap5.feedback` | 0 .. 0.95 % | 0 % | Amount of this tap fed back into the delay line |
| `tap6.enabled` | on / off | off | On |
| `tap6.sync` | on / off | on | Tempo-synced (division) or free (ms) |
| `tap6.division` | 1/64 / 1/32T / 1/32 / 1/16T / 1/32D / 1/16 / 1/8T / 1/16D / 1/8 / 1/4T / 1/8D / 1/4 / 1/2T / 1/4D / 1/2 / 1/2D / 1 bar / 2 bars | 1/8 | Note value when synced (T=triplet, D=dotted) |
| `tap6.time` | 1 .. 4000 ms | 250 ms | Delay time when not synced |
| `tap6.level` | -60 .. 6 dB | -12 dB | Level |
| `tap6.pan` | -1 .. 1 pan | -1 pan | Pan |
| `tap6.feedback` | 0 .. 0.95 % | 0 % | Amount of this tap fed back into the delay line |
| `tap7.enabled` | on / off | off | On |
| `tap7.sync` | on / off | on | Tempo-synced (division) or free (ms) |
| `tap7.division` | 1/64 / 1/32T / 1/32 / 1/16T / 1/32D / 1/16 / 1/8T / 1/16D / 1/8 / 1/4T / 1/8D / 1/4 / 1/2T / 1/4D / 1/2 / 1/2D / 1 bar / 2 bars | 1/8T | Note value when synced (T=triplet, D=dotted) |
| `tap7.time` | 1 .. 4000 ms | 250 ms | Delay time when not synced |
| `tap7.level` | -60 .. 6 dB | -12 dB | Level |
| `tap7.pan` | -1 .. 1 pan | 1 pan | Pan |
| `tap7.feedback` | 0 .. 0.95 % | 0 % | Amount of this tap fed back into the delay line |
| `tap8.enabled` | on / off | off | On |
| `tap8.sync` | on / off | on | Tempo-synced (division) or free (ms) |
| `tap8.division` | 1/64 / 1/32T / 1/32 / 1/16T / 1/32D / 1/16 / 1/8T / 1/16D / 1/8 / 1/4T / 1/8D / 1/4 / 1/2T / 1/4D / 1/2 / 1/2D / 1 bar / 2 bars | 1 bar | Note value when synced (T=triplet, D=dotted) |
| `tap8.time` | 1 .. 4000 ms | 250 ms | Delay time when not synced |
| `tap8.level` | -60 .. 6 dB | -15 dB | Level |
| `tap8.pan` | -1 .. 1 pan | 0 pan | Pan |
| `tap8.feedback` | 0 .. 0.95 % | 0 % | Amount of this tap fed back into the delay line |
| `mix` | 0 .. 1 % | 0.35 % | Wet/dry balance |
| `lowcut` | 20 .. 2000 Hz | 150 Hz | High-pass in the feedback path |
| `highcut` | 500 .. 20000 Hz | 6000 Hz | Low-pass in the feedback path (darker repeats) |
| `drive` | 0 .. 1 % | 0.1 % | Saturation in the feedback path (dirt) |

### `distortion` — Distortion

4x-oversampled waveshaper (juce::dsp): soft/warm/hard/fuzz/fold curves with bias for even harmonics, pre-drive low cut, post tone, bit/sample-rate crusher, auto-gain and parallel mix. Subtle and warm (Warm, drive 6-12 dB, mix 50%) to brutal and crusty (Fuzz/Fold, 30+ dB, bits 6, downsample 4).

| Parameter | Range | Default | Notes |
| --- | --- | --- | --- |
| `type` | Soft / Warm / Hard / Fuzz / Fold | Warm | Curve: Soft=tanh, Warm=asymmetric tube-ish, Hard=clip, Fuzz=square-ish, Fold=wavefolder |
| `drive` | 0 .. 48 dB | 12 dB | Gain into the shaper |
| `bias` | 0 .. 1 % | 0.1 % | Asymmetry: adds even harmonics (warmth) |
| `lowcut` | 20 .. 1000 Hz | 20 Hz | High-pass before the drive (tightens bass) |
| `tone` | 500 .. 20000 Hz | 12000 Hz | Low-pass after the drive (tames fizz) |
| `bits` | 2 .. 16 bit | 16 bit | Bit depth (16 = off) |
| `downsample` | 1 .. 32 x | 1 x | Sample-and-hold factor (1 = off) |
| `autogain` | on / off | on | Compensate the level so drive changes character, not loudness |
| `mix` | 0 .. 1 % | 1 % | Wet/dry balance (parallel distortion) |
| `output` | -24 .. 12 dB | 0 dB | Output trim |

### `chorus` — Chorus

Stereo chorus / flanger / vibrato: a modulated delay (JUCE DelayLine, Lagrange interpolation) with feedback. Chorus: delay 7-15 ms, depth 20-40%, mix 50%. Flanger: delay 1-3 ms, depth 70%+, feedback +/-50-80%. Vibrato: mix 100%, feedback 0, spread 0. Rate in Hz or synced to the tempo.

| Parameter | Range | Default | Notes |
| --- | --- | --- | --- |
| `sync` | on / off | off | Tempo-synced (division) or free (rate in Hz) |
| `rate` | 0.02 .. 10 Hz | 0.5 Hz | LFO speed when not synced |
| `division` | 1/16 / 1/8 / 1/4 / 1/2 / 1 bar / 2 bars / 4 bars / 8 bars | 1 bar | Length of one full cycle when synced |
| `restart` | Timeline / Event | Timeline | Timeline: cycles line up with the bar grid from beat 0. Event: each event onset on this track starts a new cycle (a seamless continuation, e.g. the right half of a split, does not) |
| `phase` | 0 .. 360 deg | 0 deg | Starting point of the cycle: 0 = centre delay, rising (vibrato: pitch dips first) |
| `depth` | 0 .. 1 % | 0.3 % | Modulation as a fraction of the centre delay (100% sweeps 0 to 2x) |
| `delay` | 1 .. 30 ms | 7 ms | Centre delay: 1-3 ms flanger, 5-15 ms chorus |
| `feedback` | -0.95 .. 0.95 % | 0 % | Delayed signal fed back (negative inverts; flanger resonance) |
| `spread` | 0 .. 180 deg | 90 deg | LFO phase offset of the right channel (stereo width) |
| `mix` | 0 .. 1 % | 0.5 % | Wet/dry balance (100% = vibrato) |

### `phaser` — Phaser

Six-stage phaser (first-order TPT all-passes, as juce::dsp::Phaser) with feedback, sweeping a log-spaced centre frequency. Rate in Hz or synced to the tempo; Restart=Event sweeps every note from the same point.

| Parameter | Range | Default | Notes |
| --- | --- | --- | --- |
| `sync` | on / off | off | Tempo-synced (division) or free (rate in Hz) |
| `rate` | 0.02 .. 10 Hz | 0.4 Hz | LFO speed when not synced |
| `division` | 1/16 / 1/8 / 1/4 / 1/2 / 1 bar / 2 bars / 4 bars / 8 bars | 1 bar | Length of one full cycle when synced |
| `restart` | Timeline / Event | Timeline | Timeline: cycles line up with the bar grid from beat 0. Event: each event onset on this track starts a new cycle (a seamless continuation, e.g. the right half of a split, does not) |
| `phase` | 0 .. 360 deg | 0 deg | Starting point of the sweep: 0 = centre, rising; 90 = top; 270 = bottom |
| `depth` | 0 .. 1 % | 0.5 % | Sweep width: 100% = +/-1.5 decades around the centre |
| `centre` | 50 .. 10000 Hz | 1000 Hz | Centre frequency of the sweep |
| `feedback` | -0.95 .. 0.95 % | 0.3 % | Resonance of the notches (negative inverts) |
| `spread` | 0 .. 180 deg | 0 deg | LFO phase offset of the right channel (stereo sweep) |
| `mix` | 0 .. 1 % | 0.5 % | Wet/dry balance (50% = deepest notches) |

### `tremolo` — Tremolo

Tempo-synced amplitude cycling. One cycle per division; the loudest point is unity gain, the quietest -depth dB (0 dB depth = no modulation). Restart=Event restarts the cycle at each event onset, for per-note pulses (e.g. distortion -> tremolo for a dubstep bass).

| Parameter | Range | Default | Notes |
| --- | --- | --- | --- |
| `division` | 1/64 / 1/32 / 1/16 / 1/8 / 1/4 | 1/8 | Length of one full cycle (1/4 = one cycle per beat) |
| `shape` | Sine / Triangle / Square | Sine | Sine, triangle or square (square edges are smoothed over ~1.5 ms) |
| `depth` | 0 .. 60 dB | 12 dB | Attenuation at the quietest point (0 = bypass) |
| `restart` | Timeline / Event | Timeline | Timeline: cycles line up with the bar grid from beat 0. Event: each event onset on this track starts a new cycle (a seamless continuation, e.g. the right half of a split, does not) |
| `phase` | 0 .. 360 deg | 0 deg | Starting point in the cycle: 0 = loudest (pulse starts loud), 180 = quietest (starts silent and swells), 90 = halfway down |
