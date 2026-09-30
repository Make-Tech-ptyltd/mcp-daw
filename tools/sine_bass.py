#!/usr/bin/env python3
"""Sine bass demo (MCP DAW 0.2) - one 55 Hz sine sample becomes a distorted bassline.

Shows the two v0.2 features over MCP:
  * per-event pitch (set via create_event's pitchSemitones / set_event_pitch), varispeed like a sampler
  * the Distortion effect, walked from clean -> subtle/warm -> gritty -> brutal/crusty while it loops

The script writes sine_A1_55hz.wav next to itself if it isn't there (exactly 55 cycles in 1 s, so it
loops seamlessly at any pitch), wipes the current project WITHOUT saving, builds a 2-bar A-minor
bassline, loops it and plays.

    python sine_bass.py                  # 100 BPM, 6 s per distortion stage
    python sine_bass.py --bpm 120 --stage-seconds 4
    python sine_bass.py --no-play        # just build it

Standard library only (Python 3.9+).
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
import time
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # mcp_daw.py lives next to this script
from mcp_daw import McpDaw, ToolError, find_bridge  # noqa: E402

# ============================================================================ the tune (tweak me)

# (start beat, semitones from A1, length in beats). A1 = 0, C2 = +3, D2 = +5, E2 = +7, G1 = -2, F1 = -4.
BASSLINE = [
    (0.00, 0, 0.75), (0.75, 0, 0.25), (1.50, 12, 0.50), (2.00, 3, 0.75), (3.00, 5, 0.50), (3.50, 7, 0.50),
    (4.00, 0, 0.75), (4.75, 0, 0.25), (5.50, -2, 0.50), (6.00, -4, 1.00), (7.00, 7, 0.50), (7.50, 3, 0.50),
]

# Distortion stages: (caption, parameters). Parameter ids are those from list_effect_types.
STAGES = [
    ("clean sine (distortion mix 0)",          {"mix": 0.0}),
    ("subtle & warm: Warm, 8 dB, a little bias", {"type": "Warm", "drive": 8, "bias": 0.2, "mix": 0.6, "tone": 9000}),
    ("gritty: Warm driven harder, full wet",    {"type": "Warm", "drive": 22, "bias": 0.35, "mix": 1.0, "tone": 6000}),
    ("fuzz bass: Fuzz 30 dB, tone rolled off", {"type": "Fuzz", "drive": 30, "bias": 0.2, "tone": 3500}),
    ("brutal & crusty: Fold + bit crusher",     {"type": "Fold", "drive": 26, "bias": 0.1, "bits": 6, "downsample": 4, "tone": 5000}),
    ("back to warm",                            {"type": "Warm", "drive": 12, "bias": 0.2, "bits": 16, "downsample": 1,
                                                 "mix": 0.8, "tone": 8000}),
]

# ============================================================================ helpers


def make_sine(path: Path, freq: float = 55.0, seconds: float = 1.0, rate: int = 48000, level: float = 0.7) -> None:
    """16-bit mono sine with a whole number of cycles, so it loops without a click."""
    n = int(rate * seconds)
    frames = b"".join(struct.pack("<h", int(32767 * level * math.sin(2 * math.pi * freq * i / rate))) for i in range(n))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(frames)


def say(msg: str) -> None:
    print(f"  > {msg}", flush=True)


# ============================================================================ main


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bpm", type=float, default=100.0)
    ap.add_argument("--stage-seconds", type=float, default=6.0, help="how long each distortion stage plays")
    ap.add_argument("--no-play", action="store_true")
    ap.add_argument("--bridge", type=Path, help="path to mcp-daw-bridge(.exe)")
    a = ap.parse_args()

    bridge = find_bridge(a.bridge)
    if bridge is None:
        print("Can't find mcp-daw-bridge. Pass --bridge or set MCP_DAW_BRIDGE.", file=sys.stderr)
        return 2

    sine = Path(__file__).resolve().parent / "sine_A1_55hz.wav"
    if not sine.exists():
        make_sine(sine)
        say(f"wrote {sine.name}")

    daw = McpDaw(bridge, client_name="sine-bass")
    try:
        daw.call("stop")
        daw.call("new_project", discardChanges=True)          # wiped, not saved
        daw.call("set_project_tempo", bpm=a.bpm)
        daw.call("set_project_name", name="Sine bass demo")

        # First note comes from the import itself; the rest in one atomic batch.
        first = BASSLINE[0]
        r = daw.call("import_audio", path=str(sine), startBeat=first[0], lengthBeats=first[2], pitchSemitones=first[1])
        track, asset = r["trackId"], r["assetId"]
        daw.call("rename_track", trackId=track, name="SINE BASS")
        daw.call("set_event_looping", eventId=r["eventId"], enabled=True)   # sustain: the sine loops seamlessly
        steps = [{"action": "create_event", "arguments": {"trackId": track, "assetId": asset, "startBeat": b,
                                                          "lengthBeats": ln, "looping": True, "pitchSemitones": st}}
                 for b, st, ln in BASSLINE[1:]]
        daw.call("execute_batch", label="Bassline", actions=steps)
        say(f"bassline: {len(BASSLINE)} notes from ONE 55 Hz sample, pitched {min(n[1] for n in BASSLINE):+d}..{max(n[1] for n in BASSLINE):+d} st")

        fx = daw.call("add_effect", target=track, type="distortion", parameters=STAGES[0][1])
        daw.call("set_track_gain", trackId=track, db=-3.0)
        daw.call("set_loop_region", startBeat=0, endBeat=8, enabled=True)
        daw.call("zoom_to_fit")

        if a.no_play:
            say("built - press Space in MCP DAW to listen")
            return 0
        daw.call("play", fromBeat=0)
        for caption, params in STAGES:
            daw.call("set_effect_parameters", effectId=fx["id"], values=params)
            say(caption)
            time.sleep(a.stage_seconds)
        say("done - still looping; tweak the DISTORTION knobs in the FX rack, or right-click a note -> Pitch")
        return 0
    except (ToolError, ConnectionError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    finally:
        daw.close()


if __name__ == "__main__":
    sys.exit(main())
