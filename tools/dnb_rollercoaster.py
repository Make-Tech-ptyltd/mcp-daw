#!/usr/bin/env python3
"""DnB Rollercoaster - build a breakbeat-driven drum & bass track in a running MCP DAW over MCP.

A plain MCP client script (standard library only, Python 3.9+): it starts the `mcp-daw-bridge`
stdio bridge, scans a few folders of WAVs, works out what each file is from its name and tempo,
then builds an 88-bar arrangement - mostly by *chopping the break*: every drum bar is assembled
from 16th-note slices of the break loop (events with a source offset), the way jungle was cut on
an Akai. Then it renders, saves and plays the result.

    python dnb_rollercoaster.py                      # default sample folders next to the project
    python dnb_rollercoaster.py D:/Samples/Breaks D:/Samples/Bass --bpm 172 --seed 7

Each section is one atomic `execute_batch`, so Ctrl+Z in MCP DAW steps back a section at a time.
The currently open project is DISCARDED (not saved) - that is the point of the script.

Sample roles come from file names: break / chop / roll / sub / reese|bass / pad|keys|string /
stab|chord / vinyl|crackle|noise / vox|vocal / snare. Tempo comes from MCP DAW ("_165bpm" in the
name, the ACID chunk, or a length-based estimate). Loops at a different tempo from the project are
re-triggered bar by bar so they stay locked (MCP DAW does not time-stretch yet).
"""
from __future__ import annotations

import argparse
import random
import sys
import time
import wave
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # mcp_daw.py lives next to this script
from mcp_daw import McpDaw, ToolError, find_bridge  # noqa: E402

# ============================================================================ sample discovery

ROLE_KEYWORDS = [                       # first match wins, so specific words come first
    ("vox", ["vox", "vocal", "voice"]),  # before "chop": "vox_chop" is a vocal
    ("chop", ["chop"]),                 # a second, re-edited break
    ("roll", ["roll"]),
    ("break", ["break", "amen", "think", "funky", "breakbeat", "drumloop", "drum_loop"]),
    ("sub", ["sub"]),
    ("bass", ["reese", "bass", "wobble"]),
    ("pad", ["pad", "string", "atmos", "keys", "chord_pad"]),
    ("stab", ["stab", "chord", "hit", "horn"]),
    ("vinyl", ["vinyl", "crackle", "noise", "hiss"]),
    ("snare", ["snare", "clap"]),
]


def role_for(name: str) -> str | None:
    lower = name.lower()
    for role, words in ROLE_KEYWORDS:
        if any(w in lower for w in words):
            return role
    return None


def first_onset_seconds(path: Path, threshold: float = 0.3) -> float:
    """Where the first hit starts (for using a loop as a one-shot). Pure stdlib, PCM WAV only."""
    try:
        with wave.open(str(path), "rb") as w:
            rate, width, channels = w.getframerate(), w.getsampwidth(), w.getnchannels()
            frames = w.readframes(min(w.getnframes(), rate * 8))
    except (wave.Error, EOFError, OSError):
        return 0.0
    if width not in (2, 3):
        return 0.0
    stride = width * channels
    hop = max(1, rate // 4000)                       # ~4 kHz scan is plenty to find a transient
    full = float(1 << (8 * width - 1))
    values = []
    for i in range(0, len(frames) - stride + 1, stride * hop):
        raw = frames[i:i + width]
        values.append(abs(int.from_bytes(raw, "little", signed=True)) / full)
    if not values:
        return 0.0
    peak = max(values) or 1.0
    for idx, v in enumerate(values):
        if v >= threshold * peak:
            return max(0.0, idx * hop / rate - 0.003)   # a hair early so the attack is kept
    return 0.0


@dataclass
class Sample:
    path: Path
    role: str | None
    asset_id: str = ""
    bpm: float = 0.0
    seconds: float = 0.0
    beats: float = 0.0
    onset: float = 0.0


@dataclass
class Kit:
    """The files chosen for each role (None when the folders had nothing suitable)."""
    brk: Sample
    chop: Sample
    roll: Sample | None = None
    sub: Sample | None = None
    bass: Sample | None = None
    pad: Sample | None = None
    stab: Sample | None = None
    vinyl: Sample | None = None
    vox: Sample | None = None
    snare: Sample | None = None
    unused: list[Sample] = field(default_factory=list)


def choose_kit(samples: list[Sample], bpm: float) -> Kit:
    """Per role, prefer the file whose tempo is closest to the project tempo."""
    def best(role):
        pool = [s for s in samples if s.role == role]
        pool.sort(key=lambda s: abs(s.bpm - bpm) if s.bpm else 999)
        return pool[0] if pool else None

    brk = best("break") or best("chop")
    kit = Kit(brk=brk, chop=best("chop") or brk, roll=best("roll"), sub=best("sub"), bass=best("bass"),
              pad=best("pad"), stab=best("stab"), vinyl=best("vinyl"), vox=best("vox"), snare=best("snare"))
    used = {id(v) for v in vars(kit).values() if isinstance(v, Sample)}
    kit.unused = [s for s in samples if id(s) not in used]
    return kit


# ============================================================================ break chopping
#
# A bar is 16 tokens, one per 16th note:
#   A7    slice 7 of the main break (slices are 16ths of the source loop, 0-based)
#   B26   slice 26 of the second ("chop") break
#   A4:2  slice 4 re-triggered as two 32nds (":4" = four 64ths) - rolls and stutters
#   .     rest
# Consecutive slices (A0 A1 A2 ...) merge into one event, so straight playback stays one clip.
# Indices assume a 2-bar break with the classic funk-break layout (kick 0, 2, 10; snare 4, 12).

PATTERNS = {
    "straight_a": "A0 A1 A2 A3 A4 A5 A6 A7 A8 A9 A10 A11 A12 A13 A14 A15",
    "straight_b": "A16 A17 A18 A19 A20 A21 A22 A23 A24 A25 A26 A27 A28 A29 A30 A31",
    "twostep":    "A0 A1 A6 A7 A4 A5 A6 A7 A8 A9 A10 A13 A12 A13 A14 A15",
    "roller":     "A0 A1 A6 A7 A4 A5 A6 A9 A8 A7 A10 A15 A12 A13 A7 A15",
    "think":      "A0 A1 A2 A3 A4 A5 A6 A7 A4 A5 A10 A11 A12 A13 A4 A15",
    "skip":       "A0 . A6 A7 A4 A5 . A7 A8 A9 A10 . A12 A13 A14 A31",
    "amen_edit":  "A16 A17 A18 A19 A20 A21 A22 A23 A0 A1 A10 A11 A12 A13 A14 A15",
    "rush":       "A0 A1 A2 A3 A4 A5 A6 A7 A8 A9 A10 A11 A12:2 A12:2 A4:2 A4:4",
    "stutter":    "A0 A1 A4 A4 A12 A12 A4 A5 B8 B8 B26 B27 B28 B29 B30 B31",
    "chopfill":   "B16 B17 B18 B19 B20 B21 B22 B23 B24 B25 B26 B27 B28 B29 B30 B31",
    "halftime":   "A0 A1 A6 A7 A8 A9 A6 A15 A4 A5 A6 A7 A8 A13 A14 A15",
    "halftime_b": "A0 A1 A6 A3 A8 A9 A10 A7 A4 A5 A6 A7 A14 A13 A4:2 A4:2",
    "ghosts":     ". . A6 . . A5 A6 A7 . A9 . . . A13 A14 A15",
    "ghosts_b":   ". . A7 . . A9 A6 A7 . A9 . A15 . A13 A7:2 A15:2",
    "roll8":      "A4 . A4 . A12 . A4 . A12 . A4 . A12 . A4 .",
    "roll16":     "A4 A4 A12 A4 A12 A4 A12 A4 A4 A12 A4 A12 A4 A12 A4 A12",
    "roll32":     "A4:2 A12:2 A4:2 A12:2 A4:2 A12:2 A4:2 A12:2 A4:2 A12:2 A4:2 A12:2 A4:4 A12:4 A4:4 A12:4",
}
FILLS = ["stutter", "rush", "chopfill", "roll16"]


class Chopper:
    """Turns pattern strings into create_event actions for one track."""

    def __init__(self, track_id: str, sources: dict[str, Sample]):
        self.track_id = track_id
        self.sources = sources

    def slice_seconds(self, src: Sample) -> float:
        return 60.0 / src.bpm / 4.0

    def slices_in(self, src: Sample) -> int:
        return max(1, round(src.beats * 4))

    def bar(self, start_beat: float, pattern: str) -> list[dict]:
        tokens = PATTERNS.get(pattern, pattern).split()
        assert len(tokens) == 16, f"pattern {pattern!r} must have 16 steps"
        actions, run = [], None                     # run = [source key, first slice, first step, steps]

        def flush():
            if run:
                key, first, step, steps = run
                actions.append(self._event(key, first, start_beat + step * 0.25, steps * 0.25))

        for step, tok in enumerate(tokens):
            if tok == ".":
                flush()
                run = None
                continue
            key, _, rest = tok[0], None, tok[1:]
            index, _, hits = rest.partition(":")
            index = int(index)
            if hits:                                  # sub-divided re-triggers: always separate events
                flush()
                run = None
                n = int(hits)
                for h in range(n):
                    actions.append(self._event(key, index, start_beat + step * 0.25 + h * 0.25 / n, 0.25 / n))
                continue
            if run and run[0] == key and run[1] + run[3] == index:
                run[3] += 1                           # continues the previous slice: extend the clip
            else:
                flush()
                run = [key, index, step, 1]
        flush()
        return actions

    def _event(self, key: str, index: int, start: float, length: float) -> dict:
        src = self.sources[key]
        index %= self.slices_in(src)
        return {"action": "create_event", "arguments": {
            "trackId": self.track_id, "assetId": src.asset_id, "startBeat": round(start, 6),
            "lengthBeats": round(length, 6), "looping": False,
            "sourceOffsetSeconds": round(index * self.slice_seconds(src), 6)}}


# ============================================================================ arrangement helpers


def bar_beat(bar: int) -> float:
    """Zero-based beat where 1-based bar `bar` starts (4/4)."""
    return 4.0 * (bar - 1)


def loop_actions(track_id: str, s: Sample, first_bar: int, last_bar: int, project_bpm: float) -> list[dict]:
    """Loop a sample over bars first..last. Tempo-matched: one looping event. Otherwise each bar is
    re-triggered from the matching bar of the source so it stays in time without stretching."""
    if s is None or last_bar < first_bar:
        return []
    start, length = bar_beat(first_bar), 4.0 * (last_bar - first_bar + 1)
    if s.bpm and abs(s.bpm / project_bpm - 1) < 0.015:
        return [{"action": "create_event", "arguments": {"trackId": track_id, "assetId": s.asset_id,
                                                           "startBeat": start, "lengthBeats": length}}]
    bars_in_source = max(1, round(s.beats / 4)) if s.bpm else 1
    bar_seconds = 240.0 / s.bpm if s.bpm else s.seconds
    return [{"action": "create_event", "arguments": {
        "trackId": track_id, "assetId": s.asset_id, "startBeat": bar_beat(b), "lengthBeats": 4.0, "looping": False,
        "sourceOffsetSeconds": round(((b - first_bar) % bars_in_source) * bar_seconds, 6)}}
        for b in range(first_bar, last_bar + 1)]


def one_shot(track_id: str, s: Sample, beat: float, length_beats: float) -> dict:
    return {"action": "create_event", "arguments": {"trackId": track_id, "assetId": s.asset_id, "startBeat": beat,
                                                      "lengthBeats": length_beats, "looping": False,
                                                      "sourceOffsetSeconds": round(s.onset, 6)}}


# ============================================================================ the track


SECTIONS = [                     # (name, first bar, last bar) - 88 bars
    ("Intro", 1, 8), ("Build", 9, 16), ("Drop 1", 17, 32), ("Half-time switch", 33, 40),
    ("Drop 2", 41, 56), ("Breakdown", 57, 64), ("Drop 3", 65, 80), ("Outro", 81, 88),
]

DRUMS = {                        # bar -> pattern for the main chopped-break track
    **dict(zip(range(9, 17), ["ghosts", "ghosts_b", "twostep", "roller", "straight_a", "straight_b", "roll16", "roll32"])),
    **dict(zip(range(17, 33), ["straight_a", "straight_b", "twostep", "stutter", "roller", "think", "amen_edit", "rush",
                               "straight_a", "straight_b", "skip", "chopfill", "roller", "twostep", "think", "roll32"])),
    **dict(zip(range(33, 41), ["halftime", "halftime_b", "halftime", "halftime_b",
                               "halftime", "halftime_b", "halftime", "rush"])),
    **dict(zip(range(41, 57), ["think", "roller", "straight_b", "stutter", "amen_edit", "skip", "twostep", "rush",
                               "roller", "roller", "think", "chopfill", "straight_a", "stutter", "roll16", "roll32"])),
    **dict(zip(range(61, 65), ["ghosts", "ghosts_b", "roll8", "roll16"])),
    **dict(zip(range(65, 81), ["stutter", "roller", "think", "rush", "amen_edit", "stutter", "skip", "chopfill",
                               "roller", "think", "roller", "stutter", "twostep", "rush", "roll16", "roll32"])),
    **dict(zip(range(81, 85), ["straight_a", "straight_b", "straight_a", "ghosts"])),
}


def vary(rng: random.Random, drums: dict[int, str]) -> dict[int, str]:
    """--seed: swap some non-fill drop bars for other grooves so every run is its own edit."""
    grooves = ["straight_a", "straight_b", "twostep", "roller", "think", "skip", "amen_edit"]
    out = dict(drums)
    for bar, pat in drums.items():
        in_drop = 17 <= bar <= 32 or 41 <= bar <= 56 or 65 <= bar <= 80
        if in_drop and pat in grooves and rng.random() < 0.4:
            out[bar] = rng.choice(grooves)
        if in_drop and bar % 4 == 0 and pat not in ("roll16", "roll32") and rng.random() < 0.5:
            out[bar] = rng.choice(FILLS)
    return out


def build(daw: McpDaw, kit: Kit, bpm: float, drums: dict[int, str], pace: float, log) -> dict[str, str]:
    """Create tracks + effects, then one batch per section. Returns track ids by name."""
    tracks: dict[str, str] = {}

    def track(name: str, colour: int, gain_db: float, pan: float = 0.0) -> str:
        tid = daw.call("create_track", name=name)["trackId"]
        daw.call("set_track_colour", trackId=tid, colour=colour)
        if gain_db:
            daw.call("set_track_gain", trackId=tid, db=gain_db)
        if pan:
            daw.call("set_track_pan", trackId=tid, pan=pan)
        tracks[name] = tid
        return tid

    def fx(target: str, kind: str, **params):
        daw.call("add_effect", target=target, type=kind, parameters=params)

    # --- tracks, top to bottom, with their inserts (mix choices explained inline) ------------------
    log("tracks", "creating tracks and effect chains")
    if kit.pad:
        t = track("Pad", 5, -4)
        fx(t, "reverb", mix=0.45, decay=6.0, predelay=40, size=0.9, damping=0.6, lowcut=250)
    if kit.vinyl:
        track("Vinyl", 7, -12)
    t = track("Break Filtered", 1, -2)
    fx(t, "eq", **{"band1.enabled": True, "band1.freq": 180, "band6.enabled": True, "band6.freq": 1000, "band6.q": 1.2})
    t = track("Breaks", 0, 1)
    # Breaks, not house: trim the sub-boom from the kicks, push the snare crack, compress gently.
    fx(t, "eq", **{"band1.enabled": True, "band1.freq": 55, "band2.freq": 120, "band2.gain": -2.5,
                   "band3.freq": 500, "band3.gain": -2, "band4.freq": 2800, "band4.gain": 4, "band4.q": 0.9,
                   "band5.gain": -1.5})
    fx(t, "compressor", threshold=-20, ratio=3.5, attack=6, release=80, knee=6, makeup=4, mix=0.75)
    t = track("Break Layer", 0, -5)
    fx(t, "eq", **{"band1.enabled": True, "band1.freq": 450, "band4.freq": 5000, "band4.gain": 3})
    fx(t, "reverb", mix=0.18, decay=0.9, size=0.35)
    if kit.snare:
        t = track("Snare Layer", 1, -9)
        fx(t, "eq", **{"band1.enabled": True, "band1.freq": 220})
        fx(t, "reverb", mix=0.25, decay=1.4, size=0.5)
    if kit.roll:
        t = track("Rolls", 1, -3)
        fx(t, "reverb", mix=0.25, decay=1.5, size=0.5)
    if kit.stab:
        t = track("Stab", 3, -4, pan=0.1)
        fx(t, "multitap_delay", **{"tap1.division": "1/8D", "tap1.level": -3, "tap1.feedback": 0.5, "tap1.pan": -0.7,
                                    "tap2.division": "1/4", "tap2.level": -8, "tap2.pan": 0.7,
                                    "mix": 0.45, "drive": 0.5, "highcut": 3000, "lowcut": 350})
        fx(t, "reverb", mix=0.3, decay=3.0, size=0.8)
    if kit.vox:
        t = track("Vox", 6, -6)
        fx(t, "multitap_delay", **{"tap1.division": "1/4D", "tap1.feedback": 0.45, "mix": 0.4, "highcut": 4000})
    if kit.sub:
        track("Sub", 4, -9)
    if kit.bass:
        t = track("Reese", 2, -4)
        fx(t, "eq", **{"band1.enabled": True, "band1.freq": 110, "band3.freq": 800, "band3.gain": 3, "band3.q": 1.4})
        fx(t, "compressor", threshold=-18, ratio=3, attack=12, release=140, makeup=2)
    fx("master", "compressor", threshold=-12, ratio=2.5, attack=25, release=150, knee=6, makeup=1)
    fx("master", "eq", **{"band2.freq": 60, "band2.gain": 1, "band5.freq": 10000, "band5.gain": -1})

    chop = Chopper(tracks["Breaks"], {"A": kit.brk, "B": kit.chop})
    layer = Chopper(tracks["Break Layer"], {"A": kit.chop, "B": kit.brk})

    def loops(name, s, a, b):
        return loop_actions(tracks[name], s, a, b, bpm) if name in tracks else []

    def drums_for(a, b):
        acts = []
        for bar in range(a, b + 1):
            if bar in drums:
                acts += chop.bar(bar_beat(bar), drums[bar])
        return acts

    def roll_into(bar):
        return loops("Rolls", kit.roll, bar, bar)

    def stabs(bars):
        return [one_shot(tracks["Stab"], kit.stab, bar_beat(b) + 0.0, 8.0) for b in bars] if "Stab" in tracks else []

    def vox(bars):
        return [one_shot(tracks["Vox"], kit.vox, bar_beat(b), 2.0) for b in bars] if "Vox" in tracks else []

    def bass(a, b):
        return loops("Sub", kit.sub, a, b) + loops("Reese", kit.bass, a, b)

    plan = {
        "Intro": loops("Pad", kit.pad, 1, 16) + loops("Vinyl", kit.vinyl, 1, 88)
                 + loops("Break Filtered", kit.brk, 5, 12) + vox([5]),
        "Build": drums_for(9, 16) + roll_into(16) + stabs([13]),
        "Drop 1": drums_for(17, 32) + bass(17, 32) + loops("Stab", kit.stab, 17, 32) + vox([17]) + roll_into(32),
        "Half-time switch": drums_for(33, 40) + loops("Sub", kit.sub, 33, 40) + loops("Pad", kit.pad, 33, 40)
                            + stabs([33, 37]),
        "Drop 2": drums_for(41, 56) + bass(41, 56) + loops("Stab", kit.stab, 41, 56) + vox([41]),
        "Breakdown": loops("Pad", kit.pad, 57, 64) + loops("Break Filtered", kit.brk, 57, 60) + drums_for(61, 64)
                     + loops("Sub", kit.sub, 61, 64) + stabs([57, 61]) + roll_into(64),
        "Drop 3": drums_for(65, 80) + bass(65, 80) + loops("Stab", kit.stab, 65, 80) + vox([65, 73])
                  + [a for bar in range(65, 81) for a in layer.bar(bar_beat(bar), "straight_a" if bar % 2 else "straight_b")]
                  + ([one_shot(tracks["Snare Layer"], kit.snare, bar_beat(bar) + beat, 0.75)
                      for bar in range(65, 81) for beat in (1.0, 3.0)] if "Snare Layer" in tracks else [])
                  + roll_into(80),
        "Outro": drums_for(81, 84) + loops("Break Filtered", kit.brk, 81, 86) + loops("Pad", kit.pad, 81, 88)
                 + loops("Sub", kit.sub, 81, 84) + stabs([85]),
    }
    for name, first, last in SECTIONS:
        actions = plan[name]
        daw.batch(name, actions)
        log(name.lower(), f"bars {first}-{last}: {len(actions)} events")
        try:
            daw.call("zoom_to_fit")
        except ToolError:
            pass                                    # older builds: purely cosmetic
        time.sleep(pace)

    # Rollercoaster gaps: bass drops out for the last beats of phrase ends, drums cut before drop 3.
    gaps = [(bar_beat(24) + 2, bar_beat(25)), (bar_beat(48) + 2, bar_beat(49)), (bar_beat(56), bar_beat(57)),
            (bar_beat(72) + 2, bar_beat(73))]
    bass_tracks = [tracks[n] for n in ("Sub", "Reese") if n in tracks]
    for a, b in gaps:
        if bass_tracks:
            daw.call("clear_range", startBeat=a, endBeat=b, trackIds=bass_tracks)
    daw.call("clear_range", startBeat=bar_beat(64) + 3, endBeat=bar_beat(65),
              trackIds=[tracks["Breaks"]] + ([tracks["Rolls"]] if "Rolls" in tracks else []))
    log("edits", "bass drop-outs on phrase ends, one-beat silence before drop 3")
    return tracks


# ============================================================================ main


def default_sample_folders(*names: str) -> list[Path]:
    """Folders called e.g. 'demo-loops' next to this script or in an ancestor (or its 'outputs')."""
    here = Path(__file__).resolve().parent
    found = []
    for name in names:
        for base in [here, *here.parents][:4]:
            for cand in (base / name, base / "outputs" / name):
                if cand.is_dir() and cand not in found:
                    found.append(cand)
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description="Build a breakbeat drum & bass track in MCP DAW over MCP.")
    ap.add_argument("folders", nargs="*", type=Path, help="folders of WAVs (default: jungle-loops / demo-loops near this script)")
    ap.add_argument("--bpm", type=float, default=0, help="project tempo (default: the break's own tempo)")
    ap.add_argument("--seed", type=int, default=None, help="vary the drum edit (same seed = same track)")
    ap.add_argument("--pace", type=float, default=0.6, help="seconds between sections, so you can watch it build")
    ap.add_argument("--out", type=Path, default=Path.cwd() / "dnb-out", help="render/save folder")
    ap.add_argument("--bridge", type=Path, default=None, help="path to mcp-daw-bridge(.exe)")
    ap.add_argument("--no-render", action="store_true", help="skip the WAV render")
    args = ap.parse_args()

    folders = args.folders or default_sample_folders("jungle-loops", "demo-loops")
    bridge = find_bridge(args.bridge)
    wavs = sorted({p.resolve() for f in folders if f.is_dir() for p in f.glob("*.wav")})

    def log(tag, text):
        print(f"[{tag:>16}] {text}", flush=True)

    print("\n  MCP DAW  x  DnB Rollercoaster\n  " + "=" * 34)
    if bridge is None:
        print("Cannot find mcp-daw-bridge(.exe); pass --bridge <path> or set MCP_DAW_BRIDGE.")
        return 2
    if not wavs:
        print("No .wav files in:", *folders, sep="\n  ")
        return 2
    log("bridge", str(bridge))
    log("samples", f"{len(wavs)} WAVs from {len(folders)} folder(s)")

    daw = McpDaw(bridge, client_name="dnb-rollercoaster")
    try:
        log("connected", daw.server)
        daw.call("new_project", discardChanges=True)       # the open project is thrown away on purpose
        daw.call("set_project_name", name="DnB Rollercoaster")
        daw.call("set_master_gain", db=-6)

        # Import everything into the pool (no events yet) so MCP DAW reports tempo and length.
        samples = []
        for path in wavs:
            s = Sample(path, role_for(path.stem))
            if s.role is None:
                continue
            a = daw.call("import_audio", path=path.as_posix(), place=False)["asset"]
            s.asset_id, s.seconds = a["id"], a["durationSeconds"]
            s.bpm = a.get("sourceBpm") or (a.get("suggestedBpm") or {}).get("bpm", 0)
            if s.bpm and not a.get("sourceBpm"):
                daw.call("set_asset_bpm", assetId=s.asset_id, bpm=s.bpm)
            s.beats = s.seconds * s.bpm / 60 if s.bpm else 0
            if s.role in ("vox", "snare"):
                s.onset = first_onset_seconds(path)
            samples.append(s)

        breaks = [s for s in samples if s.role in ("break", "chop") and s.bpm]
        if not breaks:
            print("No break loop found (name a file with 'break' or 'amen', and include its BPM).")
            return 2
        bpm = args.bpm or next((s.bpm for s in breaks if s.role == "break"), breaks[0].bpm)
        daw.call("set_project_tempo", bpm=bpm)
        kit = choose_kit(samples, bpm)
        for role in ("brk", "chop", "roll", "sub", "bass", "pad", "stab", "vinyl", "vox", "snare"):
            s = getattr(kit, role)
            if s is not None:
                how = "loop" if s.bpm and abs(s.bpm / bpm - 1) < 0.015 else ("one-shot" if role in ("vox", "snare") else "re-triggered per bar")
                log(role, f"{s.path.name}  ({s.bpm:.1f} BPM, {how})")
        if kit.unused:
            log("not used", ", ".join(s.path.name for s in kit.unused))
        log("tempo", f"{bpm:.2f} BPM, 88 bars = {88 * 240 / bpm / 60:.1f} min")

        drums = vary(random.Random(args.seed), DRUMS) if args.seed is not None else DRUMS
        build(daw, kit, bpm, drums, args.pace, log)

        try:
            daw.call("zoom_to_fit")
        except ToolError:
            pass
        args.out.mkdir(parents=True, exist_ok=True)
        name = "dnb-rollercoaster" + (f"-seed{args.seed}" if args.seed is not None else "")
        if not args.no_render:
            r = daw.call("render_mix", path=(args.out / f"{name}.wav").as_posix(), overwrite=True, tailSeconds=4)
            log("render", f"{r['path']}  ({r['seconds']:.1f} s)")
        daw.call("set_loop_region", enabled=False)
        daw.call("save_project", path=(args.out / f"{name}.mcpdaw").as_posix(), overwrite=True)
        log("saved", str(args.out / f"{name}.mcpdaw"))
        daw.call("play", fromBeat=0)
        log("play", "from the top - Space in MCP DAW to stop")
        return 0
    except (ToolError, ConnectionError) as e:
        print("\nFAILED:", e)
        return 1
    finally:
        daw.close()


if __name__ == "__main__":
    sys.exit(main())
