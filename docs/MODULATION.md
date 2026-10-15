# Tremolo, chorus and phaser

Three cycling effects, added in 0.5.0. They are ordinary insert effects (FX rack → **+ MOD**, or
`add_effect` with type `tremolo`, `chorus` or `phaser`) and they cycle **inside the audio engine**:
you set a few parameters once and the engine does the rest. No MCP traffic, project edits, undo
steps or "modified" flags are produced while they move.

## Tremolo

| Parameter | Values | Meaning |
| --- | --- | --- |
| `division` | `1/64` `1/32` `1/16` `1/8` `1/4` | Length of **one complete cycle**. `1/4` = one cycle per quarter-note beat; `1/64` = sixteen per beat. |
| `shape` | `Sine` `Triangle` `Square` | Waveform. |
| `depth` | 0 – 60 dB | Attenuation at the quietest point. 0 dB = no modulation (bit-exact pass-through). |
| `restart` | `Timeline` `Event` | Where cycles start (see below). |
| `phase` | 0 – 360° | Starting point in the cycle. |

**Waveform and phase conventions.** For every shape, phase 0° is the **loudest** point and 180° the
**quietest**:

    sine      w = 0.5 + 0.5·cos(2π·p)
    triangle  w = |1 − 2p|
    square    w = 1 for p < 0.5, else 0          (p = phase in cycles, 0 ≤ p < 1)

    gain = g_min + (1 − g_min)·w,   g_min = 10^(−depth/20)

So the waveform is mapped linearly onto gain, from the depth-derived minimum up to unity; the
loudest point is never louder than the input. `phase` shifts the start: 0° = a pulse that starts
loud, 180° = starts silent and swells in, 90° = starts halfway down.

**Smoothing.** The gain may move at most a full 0 → 1 swing per 1.5 ms. Sines and triangles never
come near that limit at any division or tempo, so they are untouched; square edges become 1.5 ms
ramps (rhythmically sharp, no click), and the same limit catches every other jump — restarts,
seeks, loop wraps, parameter changes.

## Chorus

A modulated delay with feedback — the same controls as `juce::dsp::Chorus`:

| Parameter | Meaning |
| --- | --- |
| `sync`, `rate`, `division` | Free rate in Hz (0.02–10), or synced: `1/16` … `8 bars` per cycle. |
| `restart`, `phase` | As for the tremolo. Phase 0° = centre delay, rising. |
| `depth` | Modulation as a fraction of the centre delay: delay = centre × (1 + depth × sin). |
| `delay` | Centre delay, 1–30 ms. |
| `feedback` | −95 … +95 %. Positive adds the delayed signal back in. |
| `spread` | LFO phase offset of the right channel (0–180°): stereo width. |
| `mix` | Wet/dry. |

One processor covers three effects: **chorus** (delay 7–15 ms, depth 20–40 %, mix 50 %),
**flanger** (delay 1–3 ms, depth 70 %+, feedback ±50–80 %) and **vibrato** (mix 100 %, feedback 0,
spread 0).

## Phaser

Six first-order all-pass stages with feedback, as `juce::dsp::Phaser`: `sync`/`rate`/`division`,
`restart`, `phase` (0° = centre, sweeping up; 90° = top; 270° = bottom), `depth` (100 % = ±1.5
decades around the centre), `centre` (50 Hz – 10 kHz), `feedback`, `spread`, `mix` (50 % = deepest
notches).

## Why not juce::dsp::Chorus / Phaser directly

Both keep their LFO (`juce::dsp::Oscillator`) private: it starts at phase 0 on `reset()` and then
free-runs. That makes it impossible to line cycles up with the bar grid, to restart them on each
event, or to make a bounce sound like playback (the offline renderer builds fresh processors).
Both also modulate the left and right channels identically. MCP DAW therefore keeps their
algorithms and controls — the chorus uses `juce::dsp::DelayLine`; the phaser reproduces JUCE's
TPT all-pass cascade and depth/centre mapping — but drives them from one shared clock
(`src/dsp/CycleClock.h`) and adds a stereo `spread`.

## Timeline and event restart

While the transport plays, the phase of timeline sample *t* is

    phase(t) = frac( (t − anchor) / cycleLength + phase/360 )

* **Timeline**: `anchor` = 0 — the start of the project, so cycles line up with the bar grid.
* **Event**: `anchor` = the start of the latest **event onset** on the same track at or before *t*
  (before the first onset, 0). Every note restarts the cycle the same way.

The phase is a function of the timeline position only. Consequences:

* **Block boundaries and block size** never change anything (tested bit-for-bit).
* **Seeking / starting playback in the middle of an event** reconstructs the phase from that event's
  onset — pressing play is not a new note.
* **Transport loop**: when playback wraps from the loop end to the loop start, the phase is the one
  that belongs to the loop start (in Event mode: measured from the latest onset at or before it,
  even if that event started before the loop region).
* **Bounce = playback**: `render_mix` produces the same modulation as live playback.
* **Stopped**: there is no timeline; the cycle free-runs from where it was, so reverb or delay tails
  passing through the effect keep moving. When playback starts it jumps to the timeline phase
  (smoothed).
* **Master bus**: it has no events of its own, so Event behaves like Timeline there.

### What counts as an event onset

An **event** is a placed audio clip. Notes or drum hits *inside* a looping WAV are not events and do
not restart anything — split the clip, or place one-shots, if each hit should restart the cycle.

Every event start is an onset, **except a seamless continuation**: an event that begins exactly where
another event on the track ends, plays the same asset at the same pitch, direction and looping mode
from the very source position where the first one stopped, with no deliberate fade on either side
of the join. A listener hears one note there, so the cycle carries on. In practice:

| Edit | Effect on the cycle (Event mode) |
| --- | --- |
| **Split** an event | No restart at the split point (the halves are a continuation) — even if you change one half's gain. Add a fade at the cut, or move or slip one half, and it becomes a new onset. |
| **Trim the start** (resize left edge) | The onset moves with the new start: the note now begins, and restarts, there. |
| **Trim the end** | No change to this event. (If the next event was its continuation, the gap now makes that one a new note.) |
| **Slip** (Alt+drag) | No change — the onset is the event's position on the timeline, not its audio. |
| **Move** | The onset moves with the event. |
| **Duplicate a one-shot** back-to-back | Each copy is a new hit and restarts the cycle. |
| **Duplicate a looping event** that is a whole number of loop cycles long, back-to-back | Sounds exactly like one longer event, so the copies continue the cycle. |
| Two events starting at the same time | One onset. |

## Recipes

**Preset chains** (FX rack → **+ MOD** → *Add preset chain*, or `add_effect_preset`) insert ordinary
effects — afterwards they are edited, reordered and removed individually, and the project stores
every value, so a later change to a recipe never alters an existing song.

| Preset | Chain |
| --- | --- |
| `dubstep_pulse` | Distortion → Tremolo (1/8 square, 40 dB, Event) |
| `dubstep_swell` | Tremolo (1/8 sine, 24 dB, Event, phase 180°) → Distortion (Fuzz) |
| `triphop_drum_crunch` | Compressor → Distortion (light crush) → EQ → small dark Reverb |
| `cinematic_synth_ambience` | EQ → Chorus (slow, wide) → long Reverb |

**Distorted, swelling or pulsing dubstep bass.** Use a sustained (or looping) bass sample, one event
per note, with event fades for the swell in/out. *Distortion → tremolo* keeps the tone uniformly
distorted and chops its amplitude; *tremolo → distortion* makes the amount of distortion follow the
pulse (cleaner, quieter troughs). Swap the order with the rack's up/down arrows or `move_effect`.
This is amplitude movement; a filter-cutoff wobble needs filter modulation, which MCP DAW does not
have yet.

**Blade Runner-style synth textures.** Start from a sustained analogue brass, string or pad sample;
layer a second event of it on another track detuned by ±0.05–0.15 semitones; use slow fade-ins and
long fade-outs; then `cinematic_synth_ambience`. Effects shape the source — they do not turn a
sample into an expressive synthesiser (no per-note filter envelopes, velocity or aftertouch).

## Saving

The effects use stable type and parameter ids and are saved like every other effect (project format
v2, all values). A project using them cannot be opened by MCP DAW 0.4.0, which refuses format v2
files, and any build that does not know an effect type refuses the project with a clear message
rather than silently dropping the effect.
