#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
"""Builds MCP DAW's one-click demo from three finished projects.

The demo is a timed MCP script (demo/demo-script.txt) that mcp-daw-bridge plays with
`--script`: it starts from an empty project and, while the music plays, builds, performs and
morphs through three songs:

    Kens Demo 2  "Sheep Dog"      liquid drum & bass, 174 BPM   - built live, bar by bar
    Kens Demo 3  "Courier Dub"    halftime dub at the same tempo - a seamless switch
    Kens Demo 1  "Filthy 90s Jungle"  the Dilla-style beat, 92 BPM - reached by braking the
                                   tempo from 174 to 92, then left looping exactly as saved

Everything musical comes from the three project files, so re-run `build` after editing them.
The projects ship in demo/ (kens-demo-1/2/3.mcpdaw) with their samples in demo/samples, so they
open in MCP DAW on any machine and are the default input for every command:

    python tools/make_demo.py build                                  # write demo/demo-script.txt
    python tools/make_demo.py ship     [<demo2> <demo3> <demo1>]    # after editing: copy their samples to
                                                                     # demo/samples and the projects to
                                                                     # demo/kens-demo-N.mcpdaw, pointing at samples/
    python tools/make_demo.py collect  [<demo2> <demo3> <demo1>]    # just copy their samples to demo/samples
    python tools/make_demo.py check                                  # every sample present, exact spelling

Projects may be .mcpdaw files, or JSON saved from get_project_state / open_project.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

TPB = 960                                   # ticks per beat
REPO = Path(__file__).resolve().parent.parent
DEMO_DIR = REPO / "demo"
# The shipped projects, in playing order (2, 3, 1). Their sample paths are relative: samples/<name>.wav
SHIPPED = [DEMO_DIR / "kens-demo-2.mcpdaw", DEMO_DIR / "kens-demo-3.mcpdaw", DEMO_DIR / "kens-demo-1.mcpdaw"]


# ================================================================================================
# Project model
# ================================================================================================
def tick(beats: float) -> float:
    """Snap to the 960-PPQ grid the app stores (so re-created events land exactly)."""
    return round(beats * TPB) / TPB


@dataclass
class Event:
    asset: str
    start: float
    length: float
    looping: bool
    offset: float
    gain: float
    pitch: float

    @property
    def end(self) -> float:
        return self.start + self.length


@dataclass
class Effect:
    id: str
    type: str
    bypassed: bool
    params: dict


@dataclass
class Track:
    id: str
    name: str
    colour: int
    gain: float
    pan: float
    effects: list
    events: list


@dataclass
class Project:
    name: str
    bpm: float
    loop: tuple                 # (enabled, start, end)
    master_gain: float
    master_fx: list
    assets: dict                # asset id -> source path
    tracks: list

    def track(self, tid: str) -> Track:
        return next(t for t in self.tracks if t.id == tid)


def load_project(path: Path) -> Project:
    data = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(data, list):                         # a saved MCP tool result: [{type:text, text:...}]
        data = json.loads(data[0]["text"])
    is_file = data.get("format") == "mcp-daw-project"

    def beats(obj, key):
        return tick(obj[key + "Ticks"] / TPB) if is_file else tick(obj[key + "Beat"])

    def effects(chain):
        return [Effect(f["id"], f["type"], bool(f.get("bypassed", False)), dict(f.get("parameters", {}))) for f in chain]

    tracks = []
    for t in data["tracks"]:
        events = []
        for e in t["events"]:
            start = beats(e, "start")
            length = tick(e["lengthTicks"] / TPB) if is_file else tick(e["lengthBeats"])
            events.append(Event(e["assetId"], start, length, bool(e["looping"]), float(e["sourceOffsetSeconds"]),
                                float(e.get("gainDb", 0)), float(e.get("pitchSemitones", 0))))
        events.sort(key=lambda ev: ev.start)
        tracks.append(Track(t["id"], t["name"], int(t.get("colour", 0)), float(t["gainDb"]), float(t["pan"]),
                            effects(t.get("effects", [])), events))
    loop = data["loop"]
    loop_t = (bool(loop["enabled"]), beats(loop, "start"), beats(loop, "end"))
    assets = {}
    for a in data["assets"]:
        p = a["path"]
        if is_file and not re.match(r"^([A-Za-z]:[\\/]|/)", p):
            p = str((path.parent / p).resolve())
        assets[a["id"]] = p
    return Project(data["name"], float(data["bpm"]), loop_t, float(data["master"]["gainDb"]),
                   effects(data["master"].get("effects", [])), assets, tracks)


def used_assets(projects) -> dict:
    """asset id -> source path, for every asset any event uses (asset ids are shared by the three songs)."""
    used = {}
    for p in projects:
        for t in p.tracks:
            for e in t.events:
                used[e.asset] = p.assets[e.asset]
    return used


def sample_name(source: str) -> str:
    """'808 Kick_short.WAV' -> '808-kick-short.wav': plain, portable file names for demo/samples."""
    stem = re.split(r"[\\/]", source)[-1].rsplit(".", 1)[0].lower()
    stem = re.sub(r"[^a-z0-9]+", "-", stem).strip("-")
    return (stem or "sample") + ".wav"


# ================================================================================================
# Script writer
# ================================================================================================
def js(obj) -> str:
    return json.dumps(obj, separators=(",", ":"))


class Batch:
    """execute_batch steps with $N references and captures for ids the rest of the script needs."""

    def __init__(self):
        self.steps, self.captures = [], []

    def add(self, action: str, **arguments) -> int:
        self.steps.append({"action": action, "arguments": arguments})
        return len(self.steps) - 1

    def keep(self, var: str, index: int, field_name: str = "id"):
        self.captures.append(f"{var}=results.{index}.{field_name}")

    def line(self) -> str:
        out = "execute_batch " + js({"actions": self.steps})
        if self.captures:
            out += " => " + ", ".join(self.captures)
        return out

    def __len__(self):
        return len(self.steps)


class Timeline:
    """Script lines placed at 'beats played' (monotonic) and written out in order."""

    def __init__(self):
        self.items = []          # (beat, seq, line)
        self.seq = 0

    def at(self, beat: float, *lines: str):
        for line in lines:
            self.items.append((round(beat, 4), self.seq, line))
            self.seq += 1

    def render(self, preamble: list) -> str:
        out = list(preamble)
        current = None
        for beat, _, line in sorted(self.items):
            if beat != current:
                out.append(f"@at {beat:g}")
                current = beat
            out.append(line)
        return "\n".join(out) + "\n"


# ================================================================================================
# Live state: what the demo's own project looks like at any moment (for morphs and restores)
# ================================================================================================
@dataclass
class LiveFx:
    var: str           # script variable holding the app's effect id
    src: str           # effect id in the source projects (shared by the three songs)
    type: str
    bypassed: bool
    params: dict


@dataclass
class LiveTrack:
    var: str
    src: str
    name: str
    colour: int
    gain: float
    pan: float
    fx: list = field(default_factory=list)
    mute: bool = False
    solo: bool = False


class Live:
    def __init__(self):
        self.tracks = {}         # source track id -> LiveTrack
        self.master_gain = 0.0
        self.master_fx = []
        self.fx_serial = 0

    def fx_var(self, src_id: str) -> str:
        self.fx_serial += 1
        return f"FX{self.fx_serial}_{src_id.replace('fx-', '')}"

    def find_fx(self, src_id: str):
        for chain in [self.master_fx] + [t.fx for t in self.tracks.values()]:
            for f in chain:
                if f.src == src_id:
                    return f
        return None


def ref(var: str) -> str:
    return "${" + var + "}"


def close(a, b) -> bool:
    return abs(float(a) - float(b)) < 1e-6


def morph(live: Live, target: Project, batch: Batch, tracks=None, master=True, fx=True, mixer=True, only_fx_tracks=None):
    """Steps that turn the live project's mixer and effects into `target`'s (by shared source ids)."""
    for t in target.tracks:
        if tracks is not None and t.id not in tracks:
            continue
        lt = live.tracks[t.id]
        if mixer:
            if lt.name != t.name:
                batch.add("rename_track", trackId=ref(lt.var), name=t.name); lt.name = t.name
            if lt.colour != t.colour:
                batch.add("set_track_colour", trackId=ref(lt.var), colour=t.colour); lt.colour = t.colour
            if not close(lt.gain, t.gain):
                batch.add("set_track_gain", trackId=ref(lt.var), db=t.gain); lt.gain = t.gain
            if not close(lt.pan, t.pan):
                batch.add("set_track_pan", trackId=ref(lt.var), pan=t.pan); lt.pan = t.pan
            if lt.mute:
                batch.add("mute_track", trackId=ref(lt.var), enabled=False); lt.mute = False
            if lt.solo:
                batch.add("solo_track", trackId=ref(lt.var), enabled=False); lt.solo = False
        if fx and (only_fx_tracks is None or t.id in only_fx_tracks):
            morph_chain(live, lt.fx, t.effects, ref(lt.var), batch)
    if master:
        if mixer and not close(live.master_gain, target.master_gain):
            batch.add("set_master_gain", db=target.master_gain); live.master_gain = target.master_gain
        if fx:
            morph_chain(live, live.master_fx, target.master_fx, "master", batch)


def morph_chain(live: Live, chain: list, target: list, owner: str, batch: Batch):
    wanted = {f.id for f in target}
    for f in list(chain):
        if f.src not in wanted:
            batch.add("remove_effect", effectId=ref(f.var))
            chain.remove(f)
    for index, tf in enumerate(target):
        have = next((f for f in chain if f.src == tf.id), None)
        handle = None
        if have is None:
            var = live.fx_var(tf.id)
            i = batch.add("add_effect", target=owner, type=tf.type, index=index, parameters=tf.params)
            batch.keep(var, i)
            handle = f"${i}.id"                  # its id is only known inside this batch
            have = LiveFx(var, tf.id, tf.type, False, dict(tf.params))
            chain.insert(index, have)
        else:
            changed = {k: v for k, v in tf.params.items() if not close(have.params.get(k, math.nan), v)}
            if changed:
                batch.add("set_effect_parameters", effectId=ref(have.var), values=changed)
                have.params.update(changed)
            if chain.index(have) != index:
                batch.add("move_effect", effectId=ref(have.var), index=index)
                chain.remove(have); chain.insert(index, have)
        if have.bypassed != tf.bypassed:
            batch.add("set_effect_bypass", effectId=handle or ref(have.var), bypassed=tf.bypassed)
            have.bypassed = tf.bypassed


def event_steps(batch: Batch, track_var: str, events, shift: float, asset_var):
    for e in events:
        args = {"trackId": ref(track_var), "assetId": ref(asset_var(e.asset)), "startBeat": round(e.start + shift, 6),
                "lengthBeats": round(e.length, 6), "looping": e.looping}
        if e.offset > 0:
            args["sourceOffsetSeconds"] = round(e.offset, 6)
        if e.pitch != 0:
            args["pitchSemitones"] = e.pitch
        i = batch.add("create_event", **args)
        if e.gain != 0:
            batch.add("set_event_gain", eventId=f"${i}.id", db=round(e.gain, 2))


def clip(events, start: float, end: float, stop_at=None):
    """Events starting in [start, end); lengths cut at `stop_at` (song coordinates) if given."""
    out = []
    for e in events:
        if start <= e.start < end:
            if stop_at is not None and e.end > stop_at:
                e = Event(e.asset, e.start, tick(stop_at - e.start), e.looping, e.offset, e.gain, e.pitch)
            out.append(e)
    return out


# ================================================================================================
# The show
# ================================================================================================
# Timeline positions (beats on MCP DAW's timeline) and "beats played" (what the script waits on).
REWIND_FROM, REWIND_TO = 96, 64            # the first drop is played twice ("rewind!")
REWOUND = REWIND_FROM - REWIND_TO           # 32 extra beats played after the rewind
SONG2_END = 256                             # Demo 2 plays in full (its end hit is at 256)...
SONG3_AT = 256                              # ...and Demo 3 starts on that same downbeat
SONG3_END = 200                             # Demo 3 beats 0..200, i.e. timeline 256..456
BRAKE_AT = SONG3_AT + SONG3_END             # 456: Demo 2's drums return and the tempo falls
BRAKE_BEATS, STEADY_BEATS, STOP_BEATS = 24, 4, 4
WRAP_AT = BRAKE_AT + BRAKE_BEATS + STOP_BEATS   # 484: the loop jumps from here to Demo 1 at 0
DILLA_PLAYED = WRAP_AT + REWOUND            # beats played when Demo 1 starts
LEAD = 6                                    # content is written this many beats before it plays

# Source track ids (shared by the three projects - Demo 2 and 3 were made from Demo 1).
OPEN, HATS, GHOSTS, SNARE, CLAP, KICK = "track-710", "track-711", "track-712", "track-713", "track-714", "track-715"
CRASH, KEYS_LO, KEYS_MID, KEYS_HI = "track-1487", "track-716", "track-717", "track-718"
PLUCK, SUB, SCRATCH, VOX = "track-1488", "track-1183", "track-721", "track-1117"
KEYS = [KEYS_LO, KEYS_MID, KEYS_HI]
DRUMS = [OPEN, HATS, GHOSTS, SNARE, CLAP, KICK]
MASTER_EQ, MASTER_COMP = "fx-812", "fx-811"
VOX_DELAY, DUB_DELAY, KEYS_DELAY = "fx-1489", "fx-3308", "fx-3312"


def played_a(t: float) -> float:
    """Beats played at timeline beat t, first time through (before the rewind jump)."""
    return t


def played_b(t: float) -> float:
    """...after the rewind (timeline 64..412)."""
    return t + REWOUND


def played_d(t: float) -> float:
    """...once Demo 1 is looping at 0..64."""
    return t + DILLA_PLAYED


class Show:
    def __init__(self, song2: Project, song3: Project, song1: Project, sample_files: dict):
        self.s2, self.s3, self.s1 = song2, song3, song1
        self.samples = sample_files                    # asset id -> file name in demo/samples
        self.tl = Timeline()
        self.pre = []                                  # lines before `play`
        self.live = Live()
        self.var_serial = 0
        self.asset_vars = {aid: "A" + aid.replace("asset-", "") for aid in sample_files}

    # --- helpers --------------------------------------------------------------------------------
    def asset_var(self, aid: str) -> str:
        return self.asset_vars[aid]

    def tv(self, src: str) -> str:
        return ref(self.live.tracks[src].var)

    def say(self, at, text):
        self.tl.at(at, "# " + text)

    def call(self, at, line):
        self.tl.at(at, line)

    def batch(self, at, b: Batch):
        if len(b):
            self.tl.at(at, b.line())

    def fx_set(self, at, src_fx: str, **values):
        f = self.live.find_fx(src_fx)
        f.params.update(values)
        self.call(at, "set_effect_parameters " + js({"effectId": ref(f.var), "values": values}))

    def fx_restore(self, at, src_fx: str, song: Project, *keys):
        target = next(f for chain in [song.master_fx] + [t.effects for t in song.tracks] for f in chain if f.id == src_fx)
        self.fx_set(at, src_fx, **{k: target.params[k] for k in keys})

    def mute(self, at, tracks, on: bool):
        b = Batch()
        for t in tracks:
            b.add("mute_track", trackId=self.tv(t), enabled=on)
            self.live.tracks[t].mute = on
        self.batch(at, b) if len(tracks) > 1 else self.call(at, "mute_track " + js(b.steps[0]["arguments"]))

    def solo(self, at, tracks, on: bool):
        b = Batch()
        for t in tracks:
            b.add("solo_track", trackId=self.tv(t), enabled=on)
            self.live.tracks[t].solo = on
        self.batch(at, b) if len(tracks) > 1 else self.call(at, "solo_track " + js(b.steps[0]["arguments"]))

    def zoom(self, at, start, end):
        self.call(at, "zoom_to_fit " + js({"startBeat": start, "endBeat": end}))

    def sweep(self, at_list, src_fx, param, values):
        for at, v in zip(at_list, values):
            self.fx_set(at, src_fx, **{param: round(v, 1)})

    def sync(self, at, offset):
        self.tl.at(at, f"@sync {offset:g}")

    def write_song(self, song: Project, tracks, song_from, song_to, shift, played, per=4, stop_at=None, edit=None):
        """Writes a song's events bar by bar, each bar LEAD beats before it plays."""
        for bar in range(song_from, song_to, per):
            b = Batch()
            for t in song.tracks:
                if t.id not in tracks:
                    continue
                events = clip(t.events, bar, bar + per, stop_at)
                if edit:
                    events = edit(t.id, events)
                event_steps(b, self.live.tracks[t.id].var, events, shift, self.asset_var)
            at = played(bar + shift) - LEAD + 0.5
            if at < 0:
                self.pre.append(b.line()) if len(b) else None
            else:
                self.batch(at, b)

    # --- setup: from nothing to 14 tracks, 13 samples and a mixer ---------------------------------
    def setup(self):
        s2 = self.s2
        pre = self.pre
        pre += ["@client MCP DAW demo",
                "# MCP DAW - the demo. Nothing you are about to hear is pre-recorded.",
                "# Every sample, clip, fader, effect and tempo change is an MCP command, sent live",
                "# while the music plays - the same commands Claude, ChatGPT or your own script can send.",
                "# About 4 minutes. Hands off the app until it says 'Done' (close this window to stop).",
                "@pace 60",
                'new_project {"discardChanges":false}',
                "set_project_name " + js({"name": s2.name}),
                "set_project_tempo " + js({"bpm": s2.bpm}),
                "# Loading the samples...",
                "@pace 40"]
        for aid, file_name in self.samples.items():
            pre.append("import_audio " + js({"path": "${DEMO}/samples/" + file_name, "place": False})
                       + f" => {self.asset_vars[aid]}=assetId")
        pre.append("# Building the band: 14 tracks, their mixer settings and effects...")
        pre.append("@pace 70")
        for t in s2.tracks:
            var = "T" + t.id.replace("track-", "")
            self.live.tracks[t.id] = LiveTrack(var, t.id, t.name, -1, 0.0, 0.0)   # colour -1: always set it
            pre.append("create_track " + js({"name": t.name}) + f" => {var}=trackId")
        pre.append("zoom_to_fit " + js({"startBeat": 0, "endBeat": 32}))
        for t in s2.tracks:
            b = Batch()
            morph(self.live, s2, b, tracks={t.id}, master=False)
            pre.append(b.line())
        b = Batch()
        morph(self.live, s2, b, tracks=set(), master=True)
        pre.append(b.line())
        # Intro filter: the song starts muffled and opens up over 4 bars.
        eq = self.live.find_fx(MASTER_EQ)
        eq.params.update({"band6.enabled": 1, "band6.freq": 250})
        pre.append("set_effect_parameters " + js({"effectId": ref(eq.var), "values": {"band6.enabled": 1, "band6.freq": 250}}))
        pre.append("# Writing the first two bars. From here on each bar is written just before it plays.")
        pre.append("@pace 0")

    # --- Act 1: Kens Demo 2, "Sheep Dog" --------------------------------------------------------
    def act_one(self):
        s2 = self.s2
        a, b = played_a, played_b
        all_tracks = [t.id for t in s2.tracks]

        def played_first(t):
            return a(t) if t < REWIND_FROM else b(t)
        # Content: all of Demo 2, bar by bar (the first drop is written once and played twice).
        self.write_song(s2, all_tracks, 0, SONG2_END + 8, 0, played_first)

        self.pre.append("play " + js({"fromBeat": 0}))
        self.say(0.2, "[Demo 2 'Sheep Dog' - liquid drum & bass, 174 BPM] The filter opens while the AI builds.")
        self.sync(2, 0)
        self.sweep([a(t) for t in range(1, 16)], MASTER_EQ, "band6.freq",
                   [250 * (72 ** ((i + 1) / 15)) for i in range(15)])
        self.fx_restore(a(16), MASTER_EQ, s2, "band6.enabled", "band6.freq")
        self.say(a(16), "Bar 5: the MC. Every bar is written a moment before the playhead gets there.")
        self.sync(a(16.5), 0)
        self.say(a(30), "Drums in 2 bars...")
        self.say(a(32), "Bar 9: drums.")
        self.mute(a(44), KEYS, True)
        self.mute(a(46), KEYS, False)
        self.sync(a(48.5), 0)
        self.solo(a(52), DRUMS, True)
        self.solo(a(54), DRUMS, False)
        self.say(a(56), "Build-up: a high-pass riser on the master EQ, one command every half beat...")
        self.fx_set(a(56), MASTER_EQ, **{"band1.enabled": 1, "band1.freq": 40})
        self.sweep([a(56.5 + i / 2) for i in range(15)], MASTER_EQ, "band1.freq", [40 * (22 ** ((i + 1) / 15)) for i in range(15)])
        self.fx_restore(a(64) - 0.03, MASTER_EQ, s2, "band1.enabled", "band1.freq")
        self.say(a(64), "DROP.")
        self.pan_sweep(a(70), HATS, s2, 3)
        self.fx_set(a(74.5), VOX_DELAY, **{"tap1.feedback": 0.72})
        self.fx_restore(a(78), VOX_DELAY, s2, "tap1.feedback")
        self.sync(a(80.5), 0)
        self.solo(a(84), DRUMS, True)
        self.solo(a(86), DRUMS, False)
        self.say(a(88), "Now what a DJ does when a drop lands...")
        self.call(a(90), "set_loop_region " + js({"startBeat": REWIND_TO, "endBeat": REWIND_FROM, "enabled": True}))
        self.crush(a(92), a(94), "CRUSH1")
        self.fx_set(a(95), MASTER_EQ, **{"band6.enabled": 1, "band6.freq": 5000})
        self.sweep([a(95.25), a(95.5), a(95.75)], MASTER_EQ, "band6.freq", [1800, 700, 300])
        self.fx_restore(b(REWIND_TO) - 0.03, MASTER_EQ, s2, "band6.enabled", "band6.freq")
        self.say(b(REWIND_TO), "REWIND! (a loop region threw the playhead back to the drop - once more)")
        self.call(b(REWIND_TO + 1), "set_loop_region " + js({"enabled": False}))
        self.sync(b(66.5), REWOUND)
        self.mute(b(68), [SUB, PLUCK], True)
        self.mute(b(69), [SUB, PLUCK], False)
        self.fx_set(b(74.5), VOX_DELAY, **{"tap1.feedback": 0.72})
        self.fx_restore(b(78), VOX_DELAY, s2, "tap1.feedback")
        self.mute(b(80), [HATS, OPEN], True)
        self.mute(b(82), [HATS, OPEN], False)
        self.sync(b(88.5), REWOUND)
        self.mute(b(92), [KICK], True)
        self.mute(b(94), [KICK], False)
        self.say(b(96), "Bar 25, new ground. Mutes, solos and effect moves keep coming, like a live mix.")
        self.fx_set(b(106.5), VOX_DELAY, **{"tap1.feedback": 0.72})
        self.fx_restore(b(110), VOX_DELAY, s2, "tap1.feedback")
        self.sync(b(112.5), REWOUND)
        self.say(b(116), "Zooming out: the whole song, written in the last minute.")
        self.zoom(b(116), 0, 264)
        self.zoom(b(126), 124, 156)
        self.say(b(128), "Bar 33: the bridge. The master filter dives underwater and comes back up.")
        self.fx_set(b(132), MASTER_EQ, **{"band6.enabled": 1, "band6.freq": 12000})
        self.sweep([b(133 + i) for i in range(7)], MASTER_EQ, "band6.freq", [12000 * (0.04 ** ((i + 1) / 7)) for i in range(7)])
        self.sweep([b(141 + i) for i in range(7)], MASTER_EQ, "band6.freq", [480 * (37 ** ((i + 1) / 7)) for i in range(7)])
        self.fx_restore(b(148), MASTER_EQ, s2, "band6.enabled", "band6.freq")
        self.sync(b(144.5), REWOUND)
        self.solo(b(152), [SCRATCH] + KEYS, True)
        self.solo(b(156), [SCRATCH] + KEYS, False)
        self.say(b(160), "Build 2: four to the floor, snare roll, another riser...")
        self.fx_set(b(168), MASTER_EQ, **{"band1.enabled": 1, "band1.freq": 40})
        self.sweep([b(168.5 + i / 2) for i in range(15)], MASTER_EQ, "band1.freq", [40 * (22 ** ((i + 1) / 15)) for i in range(15)])
        self.mute(b(174), KEYS, True)
        self.fx_restore(b(176) - 0.03, MASTER_EQ, s2, "band1.enabled", "band1.freq")
        self.mute(b(176), KEYS, False)
        self.say(b(176), "DROP 2.")
        self.sync(b(178.5), REWOUND)
        self.mute(b(188), [SUB, PLUCK], True)
        self.mute(b(189), [SUB, PLUCK], False)
        self.fx_set(b(202.5), VOX_DELAY, **{"tap1.feedback": 0.72})
        self.fx_restore(b(206), VOX_DELAY, s2, "tap1.feedback")
        self.sync(b(208.5), REWOUND)
        self.crush(b(212), b(214), "CRUSH2")
        self.pan_sweep(b(220), HATS, s2, 4)
        self.say(b(232), "To the right, the next song is being written already...")
        self.zoom(b(232), 224, 320)
        self.sync(b(240.5), REWOUND)
        self.say(b(240), "Outro. Next: Demo 3 at the same 174 BPM, starting on the final hit.")
        self.fx_set(b(246.5), VOX_DELAY, **{"tap1.feedback": 0.72})
        self.fx_restore(b(250), VOX_DELAY, s2, "tap1.feedback")

    # --- Act 2: Kens Demo 3, "Courier Dub" --------------------------------------------------------
    def act_two(self):
        s3, b = self.s3, played_b

        def t3(s):
            return b(s + SONG3_AT)
        tracks = [t.id for t in s3.tracks]
        self.write_song(s3, tracks, 0, SONG3_END + 8, SONG3_AT, played_b)

        # Half a beat before Demo 2's final hit, the mixer becomes Demo 3's: renamed tracks, a dub delay on the
        # old clap track (so the last clap of Demo 2 echoes into Demo 3), reverbs on the keys...
        m = Batch()
        morph(self.live, s3, m)
        self.batch(t3(-0.5), m)
        self.call(t3(-0.4), "set_project_name " + js({"name": s3.name}))
        self.say(t3(0), "[Demo 3 'Courier Dub' - halftime dub] Same key, same tempo - the mixer was just re-patched.")
        self.sync(t3(1.5), REWOUND)

        # Tear down Demo 2 and write Demo 1 in its place, in plain sight, while Demo 3's intro plays.
        self.say(t3(2), "Watch the left: the AI deletes the first song and writes the last one in its place.")
        self.zoom(t3(2), 0, WRAP_AT)
        wipe = Batch()
        wipe.add("clear_range", startBeat=0, endBeat=SONG3_AT)
        self.batch(t3(2.5), wipe)
        s1 = self.s1
        for i, bar in enumerate(range(0, 64, 8)):
            batch = Batch()
            for t in s1.tracks:
                event_steps(batch, self.live.tracks[t.id].var, clip(t.events, bar, bar + 8), 0, self.asset_var)
            self.batch(t3(3.5 + 3 * i), batch)
        self.say(t3(4), "Bars 1-16: Demo 1, a 92 BPM hip-hop beat, being written while this plays at 174.")
        self.sync(t3(17.5), REWOUND)
        self.say(t3(28), "Halftime drop in one bar...")
        self.zoom(t3(31), SONG3_AT + 30, SONG3_AT + 62)
        self.say(t3(32), "Halftime: drum & bass hats over a hip-hop kick and snare. Dub echoes, pushed by hand.")
        self.fx_set(t3(43), VOX_DELAY, **{"tap1.feedback": 0.75})
        self.fx_restore(t3(46), VOX_DELAY, s3, "tap1.feedback")
        self.sync(t3(48.5), REWOUND)
        self.fx_set(t3(54), KEYS_DELAY, **{"tap1.feedback": 0.78})
        self.fx_restore(t3(58), KEYS_DELAY, s3, "tap1.feedback")
        self.say(t3(64), "Dub drop: drums out for a bar...")
        self.mute(t3(64), [KICK, SNARE, HATS, GHOSTS, OPEN], True)
        self.mute(t3(68), [KICK, SNARE, HATS, GHOSTS, OPEN], False)
        self.sync(t3(80.5), REWOUND)
        self.say(t3(96), "Break: into the filter...")
        self.fx_set(t3(96), MASTER_EQ, **{"band6.enabled": 1, "band6.freq": 9000})
        self.sweep([t3(97 + i) for i in range(7)], MASTER_EQ, "band6.freq", [9000 * (0.07 ** ((i + 1) / 7)) for i in range(7)])
        self.sweep([t3(104 + i) for i in range(6)], MASTER_EQ, "band6.freq", [630 * (28 ** ((i + 1) / 6)) for i in range(6)])
        self.fx_restore(t3(110), MASTER_EQ, s3, "band6.enabled", "band6.freq")
        self.say(t3(112), "...and back into drum & bass for 8 bars: Demo 2's groove under Demo 3's bass and keys.")
        self.sync(t3(112.5), REWOUND)
        self.solo(t3(128), [KICK, SNARE, SUB], True)
        self.solo(t3(130), [KICK, SNARE, SUB], False)
        self.pan_sweep(t3(136), HATS, s3, 3)
        self.say(t3(144), "Halftime again...")
        self.sync(t3(144.5), REWOUND)
        self.fx_set(t3(157), DUB_DELAY, **{"tap1.feedback": 0.85})
        self.fx_restore(t3(164), DUB_DELAY, s3, "tap1.feedback")
        self.fx_set(t3(160), KEYS_DELAY, **{"tap1.feedback": 0.78})
        self.fx_restore(t3(164), KEYS_DELAY, s3, "tap1.feedback")
        self.say(t3(176), "Outro: the MC has the last word...")
        self.sync(t3(176.5), REWOUND)

    # --- Act 3: the brake, 174 -> 92 BPM ------------------------------------------------------------
    def act_three(self):
        b = played_b
        s2, s3, s1 = self.s2, self.s3, self.s1
        self.say(b(BRAKE_AT - 16), "...but the AI is writing one more thing: 6 bars that slow drum & bass down to hip-hop.")
        # Drums: Demo 2's drop groove (no claps: their dub echo would smear).
        brake = Batch()
        for t in s2.tracks:
            if t.id in (OPEN, HATS, GHOSTS, SNARE, KICK):
                event_steps(brake, self.live.tracks[t.id].var, clip(t.events, 64, 64 + BRAKE_BEATS), BRAKE_AT - 64, self.asset_var)
        # Bass, sub and keys: short notes only (so tempo changes land between them), Demo 3's chords.
        chords = ["Dm", "Dm", "Gm", "Gm", "Bb", "A"]
        triads = {"Dm": (4, 9, 12), "Gm": (2, 5, 9), "Bb": (0, 5, 9), "A": (-1, 4, 8)}   # string bass fat 4 = F2
        pluck = {"Dm": 0, "Gm": -7, "Bb": -4, "A": -5}                                    # string bass fat 2 = D2
        sub = {"Dm": 5, "Gm": -2, "Bb": 1, "A": 0}                                        # 55 Hz sine = A1
        fat4, fat2 = self.asset_by_name("string bass fat 4"), self.asset_by_name("string bass fat 2")
        sine, snare, kick = self.asset_by_name("55hz sine wave"), self.asset_by_name("Snare (1)"), self.asset_by_name("808 Kick_short")
        notes = []
        for i, c in enumerate(chords):
            bar = BRAKE_AT + 4 * i
            for off, ln in ((0, 1.0), (2.5, 0.9)):
                for trk, p in zip(KEYS, triads[c]):
                    notes.append((trk, Event(fat4, bar + off, ln, True, 0, -2, p)))
                notes.append((PLUCK, Event(fat2, bar + off, ln, False, 0, 0, pluck[c])))
                notes.append((SUB, Event(sine, bar + off, ln - 0.1, True, 0, 0, sub[c])))
        stop = WRAP_AT - STOP_BEATS
        notes += [(KICK, Event(kick, stop, 0.8, False, 0, 0, 0)), (SNARE, Event(snare, stop, 0.55, False, 0, 0, 0)),
                  (CLAP, Event(snare, stop + 0.03, 0.55, False, 0, 0, 0)), (SUB, Event(sine, stop, 1.5, True, 0, 0, 5))]
        notes += [(trk, Event(fat4, stop, 2.0, True, 0, 0, p)) for trk, p in zip(KEYS, triads["Dm"])]
        for trk, e in notes:
            event_steps(brake, self.live.tracks[trk].var, [e], 0, self.asset_var)
        self.batch(b(BRAKE_AT - 12), brake)

        self.say(b(BRAKE_AT), "RED LIGHT. Brakes on: the tempo drops one step every beat, 174 -> 92 BPM.")
        self.zoom(b(BRAKE_AT), BRAKE_AT - 4, WRAP_AT + 4)
        self.sync(b(BRAKE_AT + 1.5), REWOUND)
        self.call(b(BRAKE_AT + 2), "set_loop_region " + js({"startBeat": 0, "endBeat": WRAP_AT, "enabled": True}))
        self.fx_set(b(BRAKE_AT + STEADY_BEATS), MASTER_EQ, **{"band6.enabled": 1, "band6.freq": 16000})
        steps = BRAKE_BEATS - STEADY_BEATS
        for i in range(1, steps + 1):
            bpm = round(s3.bpm - (s3.bpm - s1.bpm) * i / steps, 2)
            at = b(BRAKE_AT + STEADY_BEATS + i)
            self.call(at, "set_project_tempo " + js({"bpm": bpm}))
            self.fx_set(at + 0.01, MASTER_EQ, **{"band6.freq": round(16000 * (0.075 ** (i / steps)))})
        self.say(b(WRAP_AT - STOP_BEATS), f"{s1.bpm:g} BPM. The same drums you heard at the start - now it's hip-hop.")

    # --- Act 4: Kens Demo 1, looping ------------------------------------------------------------------
    def act_four(self):
        b, d = played_b, played_d
        s1 = self.s1
        # Just before the jump: names, faders and effects become Demo 1's - except the two tracks still
        # echoing (they change after the jump) and the master EQ, which is still closing the filter.
        keep_for_later = {CLAP, VOX}
        m = Batch()
        morph(self.live, s1, m, tracks={t.id for t in s1.tracks}, master=False,
              only_fx_tracks={t.id for t in s1.tracks} - keep_for_later)
        m.add("set_master_gain", db=s1.master_gain)
        self.live.master_gain = s1.master_gain
        self.batch(b(WRAP_AT - 1.5), m)
        self.call(b(WRAP_AT - 1.4), "set_project_name " + js({"name": s1.name}))
        mm = Batch()
        morph(self.live, s1, mm, tracks=set(), master=True, mixer=False)
        self.batch(b(WRAP_AT) - 0.04, mm)

        self.say(d(0.1), "[Demo 1 'Filthy 90s Jungle' - the Dilla-style beat, exactly as saved] Looping.")
        self.call(d(0.5), "set_loop_region " + js({"startBeat": s1.loop[1], "endBeat": s1.loop[2], "enabled": s1.loop[0]}))
        self.sync(d(1.5), DILLA_PLAYED)
        late = Batch()
        morph(self.live, s1, late, tracks=keep_for_later, master=False)
        self.batch(d(2.5), late)
        self.say(d(4), "Tidying up: the other two songs and their extra tracks go.")
        cleanup = Batch()
        cleanup.add("clear_range", startBeat=math.ceil(max(e.end for t in s1.tracks for e in t.events)), endBeat=2000)
        self.batch(d(4), cleanup)
        for i, trk in enumerate((CRASH, PLUCK)):
            self.call(d(5 + i / 2), "delete_track " + js({"trackId": self.tv(trk)}))
            del self.live.tracks[trk]
        self.zoom(d(6), 0, 64)
        self.say(d(8), "Drums on their own for a bar...")
        self.solo(d(8), DRUMS, True)
        self.solo(d(12), DRUMS, False)
        self.sync(d(16.5), DILLA_PLAYED)
        self.say(d(20), "All of that was MCP commands - {commands} of them - the kind Claude, ChatGPT or your script can send.")
        self.fx_set(d(28), MASTER_EQ, **{"band6.enabled": 1, "band6.freq": 900})
        self.sweep([d(29), d(30), d(31)], MASTER_EQ, "band6.freq", [2200, 5500, 12000])
        self.fx_restore(d(32) - 0.03, MASTER_EQ, s1, "band6.enabled", "band6.freq")
        self.sync(d(33.5), DILLA_PLAYED)
        self.solo(d(40), [SUB, KICK], True)
        self.solo(d(42), [SUB, KICK], False)
        self.say(d(48), "It is all undoable, too: Ctrl+Z walks it back one step at a time.")
        final = Batch()
        morph(self.live, s1, final)
        self.batch(d(56), final)
        self.say(d(58), "Done. Demo 1 is yours now - play with it, or ask your AI to change it.")
        self.tl.at(d(60), "@end")

    def pan_sweep(self, at, track, song, beats):
        for i in range(beats):
            self.call(at + i, "set_track_pan " + js({"trackId": self.tv(track), "pan": -0.9 if i % 2 == 0 else 0.9}))
        self.call(at + beats, "set_track_pan " + js({"trackId": self.tv(track), "pan": song.track(track).pan}))

    def crush(self, on, off, var):
        self.call(on, 'add_effect {"target":"master","type":"distortion","parameters":'
                      '{"type":"Fuzz","drive":16,"bits":7,"downsample":4,"mix":0.6}} => ' + var + "=id")
        self.call(off, "remove_effect " + js({"effectId": "${" + var + "}"}))

    def asset_by_name(self, name: str) -> str:
        # Compared by demo sample name, so "Snare (1)" matches both E:\...\Snare (1).wav and samples/snare-1.wav.
        for aid, path in self.s2.assets.items():
            if sample_name(path) == sample_name(name) and aid in self.samples:
                return aid
        raise KeyError(f"no sample called '{name}' in the demo projects")

    def build(self) -> str:
        self.setup()
        self.act_one()
        self.act_two()
        self.act_three()
        self.act_four()
        header = ["// MCP DAW demo script - generated by tools/make_demo.py from three projects; re-generate, don't edit.",
                  "// Played by: mcp-daw-bridge --script demo-script.txt   (the format: src/mcp/ScriptPlayer.h)", ""]
        text = self.tl.render(header + self.pre)
        edits = sum(1 for line in text.splitlines() if line and line[0] not in "#@")
        edits += sum(line.count('"action"') for line in text.splitlines() if line.startswith("execute_batch"))
        return text.replace("{commands}", f"{round(edits, -2):,}")


# ================================================================================================
def collect(projects, samples_dir: Path) -> dict:
    samples_dir.mkdir(parents=True, exist_ok=True)
    mapping = {}
    for aid, source in sorted(used_assets(projects).items(), key=lambda kv: int(kv[0].split("-")[1])):
        name = sample_name(source)
        src = Path(source)
        dst = samples_dir / name
        if not src.exists():
            print(f"  missing: {source}", file=sys.stderr)
            continue
        if not (dst.exists() and src.resolve() == dst.resolve()):   # shipped projects already point here
            shutil.copyfile(src, dst)
        mapping[aid] = name
        print(f"  {src.name:32s} -> samples/{name}")
    return mapping


def ship(sources, targets) -> None:
    """Copies .mcpdaw projects (possibly onto themselves) into demo/ with every sample path rewritten to samples/<name>.wav
    (relative to the project, so MCP DAW finds them wherever the demo folder is) and unused
    assets dropped (MCP DAW refuses to open a project whose sample files are missing)."""
    for src, dst in zip(sources, targets):
        data = json.loads(Path(src).read_text(encoding="utf-8"))
        if data.get("format") != "mcp-daw-project":
            sys.exit(f"{src}: not a .mcpdaw project file")
        used = {e["assetId"] for t in data["tracks"] for e in t["events"]}
        data["assets"] = [dict(a, path="samples/" + sample_name(a["path"])) for a in data["assets"] if a["id"] in used]
        Path(dst).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"  {Path(src).name:24s} -> {Path(dst).relative_to(REPO) if REPO in Path(dst).resolve().parents else dst}"
              f"  ({len(data['assets'])} samples)")


def check(demo_dir: Path) -> list:
    """Problems that would break the shipped demo: every sample the script imports and every sample
    the shipped projects use must exist in demo/samples with exactly that spelling (Linux file names
    are case-sensitive, so 'ch.WAV' is not 'ch.wav'). Returns a list of problems (empty = all good)."""
    problems = []
    samples = demo_dir / "samples"
    present = set(p.name for p in samples.iterdir()) if samples.is_dir() else set()
    script = demo_dir / "demo-script.txt"
    if not script.exists():
        return [f"missing {script}"]
    for rel in sorted(set(re.findall(r"\$\{DEMO\}/samples/([^\"/]+)", script.read_text(encoding="utf-8")))):
        if rel not in present:
            problems.append(f"demo-script.txt imports samples/{rel}, which is not in {samples}")
    for project in SHIPPED:
        project = demo_dir / project.name
        if not project.exists():
            problems.append(f"missing {project}")
            continue
        for a in json.loads(project.read_text(encoding="utf-8"))["assets"]:
            if not a["path"].startswith("samples/") or a["path"][len("samples/"):] not in present:
                problems.append(f"{project.name} uses {a['path']}, which is not in {samples}")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["collect", "build", "ship", "check"])
    ap.add_argument("song2", type=Path, nargs="?", default=SHIPPED[0], help="Kens Demo 2 (drum & bass, played first)")
    ap.add_argument("song3", type=Path, nargs="?", default=SHIPPED[1], help="Kens Demo 3 (halftime, played second)")
    ap.add_argument("song1", type=Path, nargs="?", default=SHIPPED[2], help="Kens Demo 1 (the Dilla beat, played last and left looping)")
    ap.add_argument("--out", type=Path, default=DEMO_DIR)
    args = ap.parse_args()
    songs = (args.song2, args.song3, args.song1)
    if args.command == "check":                    # used by CI and the release packager
        problems = check(args.out)
        print("\n".join(problems) if problems else f"Demo OK: {args.out}")
        sys.exit(1 if problems else 0)
    for song in songs:
        if not song.exists():
            sys.exit(f"Project not found: {song}")

    if args.command == "ship":
        # Samples first (a project may have picked up a new sample from anywhere), then the projects.
        collect([load_project(p) for p in songs], args.out / "samples")
        ship(songs, [args.out / t.name for t in SHIPPED])
        return
    projects = [load_project(p) for p in songs]

    if args.command == "collect":
        collect(projects, args.out / "samples")
        return

    files = {aid: sample_name(src) for aid, src in used_assets(projects).items()}
    files = dict(sorted(files.items(), key=lambda kv: int(kv[0].split("-")[1])))
    missing = [f for f in files.values() if not (args.out / "samples" / f).exists()]
    if missing:
        print("Note: not in demo/samples yet (run 'collect'): " + ", ".join(missing), file=sys.stderr)
    text = Show(*projects, files).build()
    out = args.out / "demo-script.txt"
    out.write_text(text, encoding="utf-8", newline="\n")
    calls = sum(1 for line in text.splitlines() if line and line[0] not in "#@")
    steps = sum(line.count('"action"') for line in text.splitlines() if line.startswith("execute_batch"))
    print(f"Wrote {out} - {calls} MCP calls ({steps} batched edits), {len(text) // 1024} KB")


if __name__ == "__main__":
    main()
