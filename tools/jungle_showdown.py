#!/usr/bin/env python3
"""JUNGLE SHOWDOWN - Claude builds a filthy 90s jungle track, live, in a running MCP DAW.

A narrated, screen-recordable MCP demo. The track is assembled while it plays: a 4-bar loop of the
drop is built layer by layer (hats, kick, snare, ghosts, chops, bass, vocal, filth), with A/B
comparisons and a deliberate mistake undone on camera; then the whole arrangement is laid out,
mix-checked, rendered, saved and played from the top.

Everything is made from ONE-SHOTS already in the open project's sample pool (the samples you
loaded), found by name: kick / snare / clap / hat / open hat / cymbal / bass / voice / scratch.
Cue points you chose in your own project (start offsets inside long samples) are reused for the
vocal and scratch chops, so the edit is built from the moments you picked.

    python jungle_showdown.py              # the full show (about 5 minutes)
    python jungle_showdown.py --fast       # no pauses, for a quick test
    python jungle_showdown.py --bpm 172    # tempo (default 168)

The open project is backed up first (a copy next to it, "<name>-before-claude.mcpdaw"), then its
tracks are cleared; the sample pool stays. Standard library only (Python 3.9+).
"""
from __future__ import annotations

import argparse
import json
import os
import random
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # mcp_daw.py lives next to this script
from mcp_daw import McpDaw, ToolError, find_bridge  # noqa: E402

# ============================================================================ console "presentation"

TEAL, AMBER, RED, DIM, BOLD, RESET = "\033[96m", "\033[93m", "\033[91m", "\033[90m", "\033[1m", "\033[0m"
FAST = False


def enable_ansi() -> None:
    if os.name == "nt":
        os.system("")                     # turns on VT escape processing in the Windows console


def pause(seconds: float) -> None:
    if not FAST:
        time.sleep(seconds)


def say(text: str, colour: str = "", speed: float = 0.018) -> None:
    """Typewriter narration."""
    sys.stdout.write(colour)
    for ch in text:
        sys.stdout.write(ch)
        sys.stdout.flush()
        if not FAST and ch not in " ":
            time.sleep(speed)
    sys.stdout.write(RESET + "\n")
    sys.stdout.flush()


def act(text: str) -> None:
    """A tool call being made - shown dim, like a stage direction."""
    print(f"{DIM}    > {text}{RESET}", flush=True)


def chapter(number: int, title: str, subtitle: str = "") -> None:
    bar = "=" * 64
    print(f"\n{TEAL}{bar}\n  CHAPTER {number}  //  {title.upper()}{RESET}")
    if subtitle:
        print(f"{DIM}  {subtitle}{RESET}")
    print(f"{TEAL}{bar}{RESET}")
    pause(1.2)


def listen(seconds: float, note: str = "listen") -> None:
    if FAST:
        return
    print(f"{AMBER}    ~ {note} ", end="", flush=True)
    steps = max(1, int(seconds / 0.5))
    for _ in range(steps):
        time.sleep(seconds / steps)
        print(".", end="", flush=True)
    print(RESET)


TITLE = r"""
   ___  _   _ _   _  ____ _     _____   ____  _   _  _____        ______   _____        ___   _
  |_  || | | | \ | |/ ___| |   | ____| / ___|| | | |/ _ \ \      / /  _ \ / _ \ \      / / \ | |
    | || | | |  \| | |  _| |   |  _|   \___ \| |_| | | | \ \ /\ / /| | | | | | \ \ /\ / /|  \| |
 /\_/ /| |_| | |\  | |_| | |___| |___   ___) |  _  | |_| |\ V  V / | |_| | |_| |\ V  V / | |\  |
 \___/  \___/|_| \_|\____|_____|_____| |____/|_| |_|\___/  \_/\_/  |____/ \___/  \_/\_/  |_| \_|
"""

# ============================================================================ the crate (sample roles)

ROLES = [   # (role, keywords, excluded keywords) - first matching pool sample wins
    ("kick", ["kick", "bd"], []),
    ("snare", ["snare", "sd"], ["roll"]),
    ("clap", ["clap"], []),
    ("openhat", ["open"], []),
    ("hat", ["ch", "hat", "hh"], ["open", "chord", "scratch"]),
    ("cymbal", ["cy", "crash", "ride", "cym"], []),
    ("bass", ["bass", "reese", "sub"], []),
    ("vox", ["voice", "vox", "vocal", "voc"], []),
    ("scratch", ["scratch", "fx", "pipe"], []),
]


def assign_roles(pool: list[dict]) -> dict[str, list[dict]]:
    """Every pool sample gets at most one role; roles can hold several samples (e.g. 3 basses)."""
    roles: dict[str, list[dict]] = {r: [] for r, _, _ in ROLES}
    for asset in sorted(pool, key=lambda a: a["name"].lower()):
        name = asset["name"].lower()
        for role, words, excluded in ROLES:
            if any(w in name for w in words) and not any(x in name for x in excluded):
                roles[role].append(asset)
                break
    return roles


def harvest_cues(project_file: Path | None) -> dict[str, list[float]]:
    """Start offsets the human used inside long samples, keyed by sample file stem (lower case)."""
    cues: dict[str, list[float]] = {}
    if not project_file or not project_file.exists():
        return cues
    try:
        data = json.loads(project_file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return cues
    stems = {a["id"]: Path(a["path"].replace("\\", "/")).stem.lower() for a in data.get("assets", [])}
    for track in data.get("tracks", []):
        for ev in track.get("events", []):
            off = float(ev.get("sourceOffsetSeconds", 0.0))
            stem = stems.get(ev.get("assetId"))
            if stem and off > 0.05:
                bucket = cues.setdefault(stem, [])
                if all(abs(off - c) > 0.25 for c in bucket):
                    bucket.append(round(off, 4))
    return cues


# ============================================================================ the music

# 16 steps per bar. X = full, x = -4 dB, g = ghost (-11 dB), r = 32nd roll (two hits), . = rest.
DRUMS = {
    #          KICK                 SNARE                GHOST (2nd snare)    HAT
    "A":  ("X.X.......Xx....", "....X.......X...", ".......g.g.....g", "X.x.X.x.X.x.X.x."),   # the classic
    "B":  ("X.X.......X.....", "....X.......X..x", ".......g.g...g..", "X.x.X.x.X.x.X.xx"),
    "C":  ("X.....X...X.....", "....X..X....XxXx", "..g......g.g....", "X.x.X.x.X...X..."),   # chopped
    "D":  ("X.........X.....", "....X...x.x.XrXr", "................", "X.x.X.x........."),   # rush
    "S":  ("X.X...X...X.X...", "....X..x..X.X.x.", "...g.......g...g", "X.xxX.x.X.xxX.x."),   # skippy roller
    "H":  ("X.........X.....", "........X.......", "...g.g.....g.g.g", "X.x.X.x.X.x.X.x."),   # half-time
    "G":  ("................", "................", "..g..g.g..g..g.g", "x.x.x.x.x.x.x.x."),   # ghosts only
    "K":  ("X.......X.......", "....X.......X...", "................", "x.x.x.x.x.x.x.x."),   # simple build
    "R8": ("X...............", "X.X.X.X.X.X.X.X.", "................", "................"),   # 8th roll
    "R16": ("X...............", "XXXXXXXXXXXXXXXX", "................", "................"),  # 16th roll
    "R32": ("X...............", "rrrrrrrrrrrrrrrr", "................", "................"),  # 32nd roll
}
GAIN = {"X": 0.0, "x": -4.0, "g": -11.0, "r": -3.0}

# Bassline over two bars: (beat, which bass sample, length in beats).
BASSLINE = [(0.0, 2, 3.0), (3.25, 0, 0.75), (4.5, 1, 1.0), (6.0, 0, 0.5), (6.75, 1, 1.25)]


class Song:
    """Collects actions per section; knows the tracks, samples and feel."""

    def __init__(self, bpm: float, tracks: dict[str, str], samples: dict, cues: dict, rng: random.Random):
        self.bpm, self.t, self.s, self.cues, self.rng = bpm, tracks, samples, cues, rng
        self.actions: list[dict] = []
        self.gains: list[tuple[int, float]] = []

    # --- low level ------------------------------------------------------------------------------
    def one(self, role: str) -> dict | None:
        return self.s[role][0] if self.s.get(role) else None

    def hit(self, track: str, asset: dict | None, beat: float, length: float, db: float = 0.0, offset: float = 0.0):
        if asset is None or track not in self.t:
            return
        self.actions.append({"action": "create_event", "arguments": {
            "trackId": self.t[track], "assetId": asset["id"], "startBeat": round(max(0.0, beat), 5),
            "lengthBeats": round(length, 5), "looping": False, "sourceOffsetSeconds": round(offset, 5)}})
        if db:
            self.gains.append((len(self.actions) - 1, db))

    def flush(self, limit: int = 480) -> list[list[dict]]:
        """Pending hits as execute_batch chunks. Each hit's gain tweak stays in the same chunk as its
        create_event, because "$N.id" references only reach steps of the same batch."""
        gain_of = dict(self.gains)
        chunks, creates, gains = [], [], []
        for i, action in enumerate(self.actions):
            extra = 1 if i in gain_of else 0
            if len(creates) + len(gains) + 1 + extra > limit:
                chunks.append(creates + gains)
                creates, gains = [], []
            creates.append(action)
            if extra:
                gains.append({"action": "set_event_gain", "arguments": {"eventId": f"${len(creates) - 1}.id", "db": gain_of[i]}})
        if creates:
            chunks.append(creates + gains)
        self.actions, self.gains = [], []
        return chunks

    def ms(self, m: float) -> float:
        return m / 1000.0 * self.bpm / 60.0

    # --- musical building blocks ---------------------------------------------------------------
    def drums(self, bar: int, variant: str, parts: str = "KSGHC"):
        """One bar of the break from one-shots. parts: K kick, S snare, G ghosts, H hats, C clap layer."""
        kick, snare, ghost, hat = DRUMS[variant]
        b0 = 4.0 * (bar - 1)
        snare_a = self.s["snare"][0] if self.s["snare"] else None
        ghost_a = self.s["snare"][1] if len(self.s["snare"]) > 1 else snare_a
        for step in range(16):
            t = b0 + step * 0.25
            if "K" in parts and kick[step] != ".":
                self.hit("KICK", self.one("kick"), t, 0.5, GAIN[kick[step]])
            if "S" in parts and snare[step] != ".":
                if snare[step] == "r":
                    self.hit("SNARE", snare_a, t, 0.125, -5.0)
                    self.hit("SNARE", snare_a, t + 0.125, 0.125, -2.0)
                else:
                    self.hit("SNARE", snare_a, t + self.ms(4), 0.5, GAIN[snare[step]])
                    if "C" in parts and snare[step] == "X":
                        self.hit("CLAP", self.one("clap"), t + self.ms(9), 0.5)
            if "G" in parts and ghost[step] != ".":
                self.hit("GHOSTS", ghost_a, t + self.ms(self.rng.uniform(0, 8)), 0.25, self.rng.uniform(-3, 0))
            if "H" in parts and hat[step] != ".":
                self.hit("HATS", self.one("hat"), t + self.ms(self.rng.uniform(-4, 4)), 0.25,
                         GAIN.get(hat[step], 0.0) + self.rng.choice([0, -1.5, -3]))
        if "H" in parts and variant in ("B", "S") and self.one("openhat"):
            self.hit("OPEN HAT", self.one("openhat"), b0 + 3.5, 0.5)

    def crash(self, bar: int):
        self.hit("CRASH", self.one("cymbal"), 4.0 * (bar - 1), 4.0)

    def bass(self, bar: int, drone: bool = False):
        """Two-bar bassline starting at `bar` (or a long drone note when drone=True)."""
        basses = self.s["bass"]
        if not basses:
            return
        b0 = 4.0 * (bar - 1)
        names = ["BASS 1", "BASS 2", "BASS 3"]
        if drone:
            longest = max(range(len(basses)), key=lambda i: basses[i]["durationSeconds"])
            self.hit(names[longest], basses[longest], b0, 7.5, -2.0)
            return
        for beat, idx, length in BASSLINE:
            idx = min(idx, len(basses) - 1)
            self.hit(names[idx], basses[idx], b0 + beat, length)

    def vox_chop(self, bar: int, beat: float, which: int, length: float = 1.0, db: float = 0.0):
        v = self.s["vox"][0] if self.s["vox"] else None
        if v is None:
            return
        cues = self.cues.get(v["name"].lower()) or [0.0]
        self.hit("VOX", v, 4.0 * (bar - 1) + beat, length, db, cues[which % len(cues)])

    def vox_bed(self, bar: int, bars: int):
        v = self.s["vox"][0] if self.s["vox"] else None
        if v is None:
            return
        cue = (self.cues.get(v["name"].lower()) or [0.0])[0]
        length = min(4.0 * bars, (v["durationSeconds"] - cue) * self.bpm / 60.0)
        self.hit("VOX BED", v, 4.0 * (bar - 1), length, -3.0, cue)

    def scratch(self, bar: int, beat: float):
        sc = self.s["scratch"][0] if self.s["scratch"] else None
        if sc is None:
            return
        cue = (self.cues.get(sc["name"].lower()) or [0.0])[0]
        self.hit("SCRATCH", sc, 4.0 * (bar - 1) + beat, 0.625, 0.0, cue)


# ============================================================================ the show


def main() -> int:
    global FAST
    ap = argparse.ArgumentParser(description="Narrated live build of a filthy 90s jungle track in MCP DAW")
    ap.add_argument("--bpm", type=float, default=168.0)
    ap.add_argument("--fast", action="store_true", help="no pauses (testing)")
    ap.add_argument("--seed", type=int, default=90)
    ap.add_argument("--bridge", type=Path, default=None)
    ap.add_argument("--cues-from", type=Path, default=None, help="project file to take cue points from (default: the open one)")
    args = ap.parse_args()
    FAST = args.fast
    enable_ansi()
    rng = random.Random(args.seed)

    bridge = find_bridge(args.bridge)
    if bridge is None:
        print("Can't find mcp-daw-bridge - pass --bridge <path> or set MCP_DAW_BRIDGE.")
        return 2

    print(TEAL + TITLE + RESET)
    say("  Claude  vs  ChatGPT.   Round one:  a FILTHY 90s JUNGLE track.", BOLD)
    say("  Built live, over MCP, in MCP DAW - from the samples already in the pool.", DIM)
    say("  Nothing pre-rendered. Every sound you hear is placed while you watch.", DIM)
    pause(2.5)

    daw = McpDaw(bridge, client_name="jungle-showdown")
    try:
        # ---------------------------------------------------------------- 1. the crate
        chapter(1, "Dig the crate", "What did Ken leave us?")
        history = daw.call("get_history")
        project_file = Path(history["file"]) if history.get("file") else None
        pool = daw.call("list_assets")
        tracks_now = daw.call("list_tracks")
        if not pool:
            say("  The sample pool is empty - load some samples into MCP DAW first.", RED)
            return 2
        if tracks_now:
            backup = (project_file.with_name(project_file.stem + "-before-claude.mcpdaw") if project_file
                      else Path.cwd() / "before-claude.mcpdaw")
            act(f"save_project  {backup.name}   (safety copy of what was open)")
            daw.call("save_project", path=backup.as_posix(), overwrite=True)
            act(f"delete_track x{len(tracks_now)}   (the pool of samples stays)")
            daw.batch("Clear the stage", [[{"action": "delete_track", "arguments": {"trackId": t["id"]}} for t in tracks_now]])
        daw.call("stop")
        cues = harvest_cues(args.cues_from or project_file)
        roles = assign_roles(pool)
        say(f"  {len(pool)} samples in the pool. Casting them:")
        for role, items in roles.items():
            if items:
                label = ", ".join(f"{a['name']} ({a['durationSeconds']:.2f}s)" for a in items)
                print(f"    {TEAL}{role.upper():>8}{RESET}  {label}")
                pause(0.25)
        if cues:
            say(f"  And the cue points you picked inside the long samples: "
                + "; ".join(f"{k} @ {', '.join(str(c) + 's' for c in v)}" for k, v in cues.items()), DIM)
        missing = [r for r in ("kick", "snare", "hat") if not roles[r]]
        if missing:
            say(f"  Can't build a break without: {', '.join(missing)}.", RED)
            return 2
        say("  No breakbeat loop in here. Just one-shots. So we build the Amen from scratch, hit by hit.", AMBER)
        pause(1.5)

        # ---------------------------------------------------------------- 2. the stage
        chapter(2, "Set the stage", f"{args.bpm:g} BPM. Tracks. A four-bar loop. Hit play on silence.")
        daw.call("set_project_name", name="Claude - Filthy 90s Jungle")
        act(f"set_project_tempo {args.bpm:g}")
        daw.call("set_project_tempo", bpm=args.bpm)
        daw.call("set_master_gain", db=-6)
        layout = [("CRASH", 3, -13, 0.0, "cymbal"), ("OPEN HAT", 6, -15, 0.3, "openhat"), ("HATS", 6, -9, 0.2, "hat"),
                  ("GHOSTS", 1, -10, -0.15, "snare"), ("SNARE", 1, -1, 0.0, "snare"), ("CLAP", 1, -14, 0.0, "clap"),
                  ("KICK", 0, -1, 0.0, "kick"), ("BASS 1", 4, -5, 0.0, "bass"), ("BASS 2", 4, -5, 0.0, "bass"),
                  ("BASS 3", 4, -7, 0.0, "bass"), ("VOX", 2, -7, -0.2, "vox"), ("VOX BED", 2, -12, 0.0, "vox"),
                  ("SCRATCH", 5, -11, 0.35, "scratch")]
        tracks: dict[str, str] = {}
        bass_count = len(roles["bass"])
        for name, colour, db, pan, role in layout:
            if not roles[role] or (name.startswith("BASS ") and int(name[-1]) > bass_count):
                continue
            tid = daw.call("create_track", name=name)["trackId"]
            daw.call("set_track_colour", trackId=tid, colour=colour)
            daw.call("set_track_gain", trackId=tid, db=db)
            if pan:
                daw.call("set_track_pan", trackId=tid, pan=pan)
            tracks[name] = tid
            act(f"create_track {name}")
            pause(0.15)
        samples = {r: v for r, v in roles.items()}
        song = Song(args.bpm, tracks, samples, cues, rng)
        DROP = 17                                             # the drop is bars 17-32; we build it first
        daw.call("set_loop_region", startBeat=4.0 * (DROP - 1), endBeat=4.0 * (DROP + 3), enabled=True)
        daw.call("play", fromBeat=4.0 * (DROP - 1))
        say("  Loop is running on bars 17-20 - the drop. Right now it's silence. Let's fix that.")
        listen(3, "silence")

        def build(label, fn, note, wait):
            fn()
            chunks = song.flush()
            act(f"execute_batch '{label}'  ({sum(map(len, chunks))} steps)")
            daw.batch(label, chunks)
            say(f"  {note}")
            listen(wait)

        # ---------------------------------------------------------------- 3. the break, hit by hit
        chapter(3, "Build the break", "Hats first. Then the kick. Then the snare. Then the ghosts.")
        phrase = ["A", "B", "A", "C"]
        build("Hats", lambda: [song.drums(DROP + i, v, "H") for i, v in enumerate(phrase)],
              "Hats: eighths, a little loose, with an open hat to lift the second bar.", 6)
        build("Kick", lambda: [song.drums(DROP + i, v, "K") for i, v in enumerate(phrase)],
              "808 kick in the Amen positions - one, the 'and', then the double on the three-and.", 6)
        build("Snare", lambda: [song.drums(DROP + i, v, "SC") for i, v in enumerate(phrase)],
              "Snare on two and four, pushed 4 ms late, with the clap smeared underneath.", 6)
        build("Ghosts", lambda: [song.drums(DROP + i, v, "G") for i, v in enumerate(phrase)],
              "Ghost notes from the second snare. THIS is what makes it roll instead of march.", 8)

        # ---------------------------------------------------------------- 4. the filth
        chapter(4, "Make it filthy", "Compression you can hear. Then an honest A/B.")
        fx = {}
        fx["snare_comp"] = daw.call("add_effect", target=tracks["SNARE"], type="compressor",
                                     parameters={"threshold": -26, "ratio": 8, "attack": 0.5, "release": 70, "knee": 2,
                                                 "makeup": 9, "mix": 0.85})["id"]
        daw.call("add_effect", target=tracks["SNARE"], type="eq",
                  parameters={"band1.enabled": True, "band1.freq": 140, "band3.freq": 450, "band3.gain": -4,
                              "band4.freq": 2600, "band4.gain": 5, "band4.q": 0.8})
        daw.call("add_effect", target=tracks["SNARE"], type="reverb", parameters={"mix": 0.14, "decay": 0.7, "size": 0.3})
        daw.call("add_effect", target=tracks["KICK"], type="compressor",
                  parameters={"threshold": -20, "ratio": 5, "attack": 8, "release": 60, "makeup": 5})
        daw.call("add_effect", target=tracks["KICK"], type="eq",
                  parameters={"band1.enabled": True, "band1.freq": 35, "band2.freq": 65, "band2.gain": 3,
                              "band3.freq": 300, "band3.gain": -5})
        daw.call("add_effect", target=tracks["HATS"], type="eq", parameters={"band1.enabled": True, "band1.freq": 600})
        daw.call("add_effect", target=tracks["GHOSTS"], type="eq", parameters={"band1.enabled": True, "band1.freq": 350})
        fx["master_comp"] = daw.call("add_effect", target="master", type="compressor",
                                      parameters={"threshold": -16, "ratio": 4, "attack": 12, "release": 90, "knee": 4,
                                                  "makeup": 4})["id"]
        daw.call("add_effect", target="master", type="eq",
                  parameters={"band2.freq": 70, "band2.gain": 2, "band5.freq": 9000, "band5.gain": -2})
        act("add_effect x9  (snare comp+EQ+room, kick comp+EQ, hat/ghost high-pass, master glue+EQ)")
        say("  Snare: 8:1 at half a millisecond attack, +9 dB makeup. Master: pumping glue.")
        listen(6, "FILTHY")
        act("set_effect_bypass  snare comp + master comp  -> OFF")
        daw.call("set_effect_bypass", effectId=fx["snare_comp"], bypassed=True)
        daw.call("set_effect_bypass", effectId=fx["master_comp"], bypassed=True)
        say("  A/B - compressors OFF. Polite. Clean. Boring.", DIM)
        listen(5, "clean")
        act("set_effect_bypass  -> ON")
        daw.call("set_effect_bypass", effectId=fx["snare_comp"], bypassed=False)
        daw.call("set_effect_bypass", effectId=fx["master_comp"], bypassed=False)
        say("  ...and back ON. That's the sound of 1994.", AMBER)
        listen(5)

        # ---------------------------------------------------------------- 5. the bass
        chapter(5, "Bassline", "Three bass one-shots, played as a two-bar line.")
        for name in ("BASS 1", "BASS 2", "BASS 3"):
            if name in tracks:
                daw.call("add_effect", target=tracks[name], type="eq",
                          parameters={"band2.freq": 80, "band2.gain": 3, "band6.enabled": True, "band6.freq": 2500})
                daw.call("add_effect", target=tracks[name], type="compressor",
                          parameters={"threshold": -20, "ratio": 4, "attack": 5, "release": 150, "makeup": 4})
        build("Bassline", lambda: [song.bass(DROP), song.bass(DROP + 2)],
              "Long note on the one, then short stabs pushing against the break.", 9)

        # ---------------------------------------------------------------- 6. the voice
        chapter(6, "The voice", "Chopped from the exact moments Ken picked in his own session.")
        if "VOX" in tracks:
            daw.call("add_effect", target=tracks["VOX"], type="multitap_delay",
                      parameters={"tap1.division": "1/8D", "tap1.feedback": 0.5, "tap1.level": -4, "tap2.division": "1/4",
                                  "tap2.level": -9, "mix": 0.45, "drive": 0.7, "highcut": 3000, "lowcut": 300})
            daw.call("add_effect", target=tracks["VOX"], type="reverb", parameters={"mix": 0.3, "decay": 2.8, "size": 0.8})
        if "SCRATCH" in tracks:
            daw.call("add_effect", target=tracks["SCRATCH"], type="eq", parameters={"band1.enabled": True, "band1.freq": 250})
        build("Vox chops + scratch", lambda: [song.vox_chop(DROP, 0.0, 0), song.vox_chop(DROP + 2, 2.5, 1, 0.75),
                                              song.scratch(DROP + 3, 3.375)],
              "A vocal stab on the one, a dotted-eighth delay throw, and a scratch into the turnaround.", 9)

        # ---------------------------------------------------------------- 7. a mistake, on purpose
        chapter(7, "A deliberate mistake", "What if we put the kick on every beat?")
        for bar in range(DROP, DROP + 4):
            for beat in range(4):
                song.hit("KICK", samples["kick"][0], 4.0 * (bar - 1) + beat, 0.5)
        chunks = song.flush()
        act("execute_batch 'Four-to-the-floor (bad idea)'")
        daw.batch("Four-to-the-floor (bad idea)", chunks)
        say("  Four-to-the-floor. Every beat. Hear it turn into house music?", RED)
        listen(6, "nope")
        act("undo")
        daw.call("undo")
        say("  Ctrl+Z. One step, gone. Every AI edit is undoable - the human stays in charge.", TEAL)
        listen(5)

        # ---------------------------------------------------------------- 8. the arrangement
        chapter(8, "Arrange the whole tune", "Intro, build, drop, breakdown, second drop, outro. 60 bars.")
        daw.call("set_loop_region", enabled=False)

        def section(label, fn):
            fn()
            chunks = song.flush()
            act(f"execute_batch '{label}'  ({sum(map(len, chunks))} steps)")
            daw.batch(label, chunks)
            try:
                daw.call("zoom_to_fit")
            except ToolError:
                pass
            pause(1.2)

        # Intro 1-8: atmosphere, hats, drone.
        section("Intro", lambda: [song.vox_bed(1, 8), song.crash(1), song.bass(1, drone=True), song.bass(5, drone=True),
                                  [song.drums(b, "G", "H") for b in range(5, 9)]])
        say("  Intro: the long vocal as a bed, a bass drone, hats creeping in.")
        # Build 9-16: simple beat, then rolls.
        section("Build", lambda: [[song.drums(b, "K", "KSH") for b in range(9, 13)],
                                  [song.drums(b, v, "KSGH") for b, v in zip(range(13, 15), ["A", "B"])],
                                  song.drums(15, "R16", "KS"), song.drums(16, "R32", "KS"), song.scratch(16, 3.375)])
        say("  Build: a plain beat, then the snare rolls - 16ths, then 32nds.")
        # Drop 1 17-32: the loop we built (17-20) + variations.
        drop_rest = ["A", "B", "A", "D", "A", "B", "S", "C", "A", "B", "A", "C"]
        section("Drop 1", lambda: [song.crash(17), [song.drums(21 + i, v) for i, v in enumerate(drop_rest)],
                                   [song.bass(b) for b in range(21, 33, 2)],
                                   [song.vox_chop(b, 0.0, i) for i, b in enumerate(range(21, 33, 4))],
                                   [song.scratch(b, 3.375) for b in (24, 28)]])
        say("  Drop one: sixteen bars, a new edit every phrase, fills on the fourth bar.")
        # Breakdown 33-40
        section("Breakdown", lambda: [song.vox_bed(33, 8), song.bass(33, drone=True), song.bass(37, drone=True),
                                      [song.drums(b, "G", "GH") for b in range(33, 37)],
                                      [song.drums(b, "H", "KSGH") for b in range(37, 39)],
                                      song.drums(39, "R8", "KS"), song.drums(40, "R32", "KS")])
        say("  Breakdown: drums out, ghosts only, then half-time and a 32nd roll back in.")
        # Drop 2 41-56
        drop2 = ["A", "S", "A", "C", "A", "B", "S", "D", "S", "B", "A", "C", "S", "S", "A", "R32"]
        section("Drop 2", lambda: [song.crash(41), [song.drums(41 + i, v) for i, v in enumerate(drop2)],
                                   [song.bass(b) for b in range(41, 57, 2)],
                                   [song.vox_chop(b, 0.0 if i % 2 == 0 else 2.5, i + 1, 1.0 if i % 2 == 0 else 0.75)
                                    for i, b in enumerate(range(41, 57, 2))],
                                   [song.scratch(b, 3.375) for b in (44, 48, 52)]])
        say("  Drop two: busier - skippy rollers, vocal every two bars, scratches on every turnaround.")
        # Outro 57-60
        section("Outro", lambda: [[song.drums(b, v) for b, v in zip(range(57, 59), ["A", "B"])],
                                  song.drums(59, "G", "GH"), song.crash(60), song.vox_chop(60, 0.0, 0, 2.0)])
        daw.call("clear_range", startBeat=4.0 * 32 - 1.0, endBeat=4.0 * 32,
                  trackIds=[tracks[n] for n in ("BASS 1", "BASS 2", "BASS 3") if n in tracks])
        daw.call("clear_range", startBeat=4.0 * 56 - 2.0, endBeat=4.0 * 56,
                  trackIds=[tracks[n] for n in ("BASS 1", "BASS 2", "BASS 3", "KICK") if n in tracks])
        say("  Outro, plus the classic jungle trick: the bass drops out right before each drop hits.")

        # ---------------------------------------------------------------- 9. mix check
        chapter(9, "Mix check", "Render it, read the peak, set the level. Like an engineer would.")
        out_dir = project_file.parent if project_file else Path.cwd()
        check = out_dir / "claude-filthy-jungle-mixcheck.wav"
        act("render_mix  (mix check)")
        r = daw.call("render_mix", path=check.as_posix(), overwrite=True, tailSeconds=2)
        peak = r.get("peakDb", -6.0)
        master = -6.0 + max(-12.0, min(6.0, -1.0 - peak))
        say(f"  Peak {peak:+.1f} dBFS{'  - CLIPPING' if r.get('clipped') else ''}. Setting master to {master:+.1f} dB "
            f"for a -1 dB ceiling.", AMBER if peak > -1 else "")
        daw.call("set_master_gain", db=round(master, 1))
        try:
            check.unlink()
        except OSError:
            pass

        # ---------------------------------------------------------------- 10. the reveal
        chapter(10, "The reveal", "From the top. Sixty bars. Turn it up.")
        final_wav = out_dir / "claude-filthy-jungle.wav"
        final_prj = out_dir / "claude-filthy-jungle.mcpdaw"
        r = daw.call("render_mix", path=final_wav.as_posix(), overwrite=True, tailSeconds=3)
        daw.call("save_project", path=final_prj.as_posix(), overwrite=True)
        act(f"render_mix -> {final_wav.name}  ({r['seconds']:.0f} s, peak {r.get('peakDb', 0):+.1f} dBFS)")
        act(f"save_project -> {final_prj.name}")
        try:
            daw.call("zoom_to_fit")
        except ToolError:
            pass
        daw.call("play", fromBeat=0)
        pause(1.0)
        count = sum(1 for _ in daw.call("list_tracks"))
        n_cues = sum(len(v) for v in cues.values())
        say(f"  {count} tracks, built from the samples in your pool"
            + (f" and {n_cues} cue points you chose." if n_cues else "."), TEAL)
        say("  Every hit placed through the same command layer the mouse uses. Every step undoable.", TEAL)
        pause(1.0)
        print()
        say("  Your move, ChatGPT.", BOLD + AMBER, speed=0.08)
        print()
        return 0
    except (ToolError, ConnectionError) as e:
        print(f"\n{RED}FAILED: {e}{RESET}")
        return 1
    finally:
        daw.close()


if __name__ == "__main__":
    sys.exit(main())
