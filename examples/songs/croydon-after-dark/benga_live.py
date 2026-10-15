"""Benga-style dubstep "live set" - 32 bars at 140 BPM, built live in MCP DAW over MCP.

Half-step drums (snare on 3), garage-shuffled hats, triplet rims, F-minor dub stabs, sub + reese
wobbles.  The wobble is tremolo BEFORE a wavefolder, so each cycle sweeps the tone from clean to
folded (a filter-like growl from amplitude movement), and the wobble rate changes by moving notes
between four wobble channels - the "live hand on the controls".

Live touches: rewind + pull-up after the first drop, hard cut before drops, delay throws, an eager
early bass entry, a fat-finger flam, a muted snare, a stab stutter, loose (not grid) timing.

Run it with MCP DAW installed (MCP DAW opens if it isn't running; an open project with unsaved
changes is never discarded - the script stops instead):

    python benga_live.py                 # the published song -> ./out/croydon-after-dark.mcpdaw + .wav
    python benga_live.py --seed 42       # a new variation with the same writing
    python benga_live.py --bridge "C:/path/to/mcp-daw-bridge.exe"
"""
import argparse, random, sys
from pathlib import Path

# Portable layout: this file is examples/songs/<song>/ in the repo, Examples/songs/<song>/ in the release zip.
HERE = Path(__file__).resolve().parent
SONGS = HERE.parent                       # shared samples/ for the example songs
TOP = SONGS.parent.parent                 # repo root, or the unzipped MCP DAW folder
for tools in (TOP / "tools", TOP / "Scripting"):   # mcp_daw.py: tools/ in the repo, Scripting/ in the zip
    if (tools / "mcp_daw.py").is_file():
        sys.path.insert(0, str(tools))
from mcp_daw import McpDaw, find_bridge  # noqa: E402

ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("--bridge", help="path to mcp-daw-bridge(.exe) (default: auto-detect, or MCP_DAW_BRIDGE)")
ap.add_argument("--out", type=Path, default=HERE / "out", help="where the render and project are saved (default: ./out)")
ap.add_argument("--seed", type=int, default=2008, help="humanisation / variation seed (2008 = the published song)")
ARGS = ap.parse_args()
OUT = ARGS.out
KIT, OWN = TOP / "Samples-Starter-Kit", SONGS / "samples"
SAMPLES = {
    "kick": KIT / "kick-deep-clean.wav", "kick2": KIT / "kick-mid-clean.wav",
    "snare": KIT / "snare-basic.wav", "clap": KIT / "clap.wav", "snare2": OWN / "snare-2.wav",
    "hat": OWN / "ch.wav", "ohat": OWN / "cs10-open-hihat1.wav", "rim": KIT / "reverse-tapping-stick.wav",
    "cym": OWN / "cy5010.wav", "tomh": KIT / "tom-high.wav", "toml": KIT / "tom-low.wav", "revcym": KIT / "reverse-percusion-cymbal.wav",
    "stab": OWN / "dub_stab_165bpm.wav", "reese": OWN / "reese_165bpm.wav", "pad": OWN / "dark_pad_165bpm.wav",
    "sine": KIT / "55hz-sine-wave_bass-note-A1.wav", "vinyl": OWN / "vinyl_165bpm.wav",
}
BPM, BARS = 140.0, 32
rng = random.Random(ARGS.seed)
def ms(m): return m / 1000.0 * BPM / 60.0
def sec(s): return s * BPM / 60.0
B165 = 60 / 165
STAB_OFF, STAB_LEN = 1.5 * B165 - 0.004, 0.40          # the F-minor stab in the dub stab loop
REESE_OFF, REESE_LEN = 0.05, 2.05                       # sustained F2 at the start of the reese loop
PAD_OFF = 0.12                                          # Fm9 window of the pad loop

# Notes: reese (F2 source) and sub (sine A1 source) transpositions.
WOB = {"F": 0, "Ab": 3, "Eb": -2, "Db": -4, "C": -5}
SUB = {"F": -4, "Ab": -1, "Eb": -6, "Db": -8, "C": -9}

def section(bar):
    if bar < 4: return "intro"
    if bar < 12: return "dropA"
    if bar < 20: return "dropA2"
    if bar < 24: return "break"
    return "dropB"

events = {}
def ev(track, sample, beat, length, offset=0.0, gain=0.0, pitch=0.0, fin=0.0, fout=0.0, curve="smooth", looping=False, rev=False):
    a = {"assetId": sample, "startBeat": round(max(0.0, beat), 5), "lengthBeats": round(length, 5), "looping": looping,
         "sourceOffsetSeconds": round(offset, 5)}
    if gain: a["gainDb"] = round(gain, 2)
    if pitch: a["pitchSemitones"] = round(pitch, 3)
    if fin: a["fadeInBeats"] = round(fin, 4); a["fadeInCurve"] = curve
    if fout: a["fadeOutBeats"] = round(fout, 4); a["fadeOutCurve"] = curve
    if rev: a["reversed"] = True
    events.setdefault(track, []).append(a)

def j(width=4.0): return ms(rng.uniform(-width, width))     # small live looseness

# ------------------------------------------------------------------ bass riffs (2-bar phrases)
# (beat, length, note, channel)  channel: w8 / w16 / w4 / w32 = wobble rate; None = sub only
RIFFS = {
    "A": [(0, 1.75, "F", "w8"), (2, 0.5, "F", "w16"), (2.75, 1.25, "Eb", "w8"), (4, 1.5, "Db", "w4"), (6, 1.0, "Ab", "w16"), (7.5, 0.5, "Ab", "w32")],
    "B": [(0, 0.75, "F", "w16"), (1, 0.75, "F", "w8"), (2, 2.0, "Ab", "w4"), (4.5, 1.25, "Eb", "w8"), (6, 0.5, "Db", "w16"), (6.75, 1.25, "F", "w8")],
    "C": [(0, 0.5, "F", "w16"), (0.75, 0.5, "F", "w16"), (1.5, 1.5, "F", "w8"), (3.25, 0.75, "Ab", "w32"), (4, 1.0, "Eb", "w16"),
          (5, 1.0, "Db", "w8"), (6.5, 1.5, "C", "w4")],
}
WOB_TRACK = {"w8": "Wobble 1/8", "w16": "Wobble 1/16", "w4": "Wobble 1/4", "w32": "Wobble 1/32"}

def bass_phrase(start_bar, riff, early=0.0, mutate=0.3, skip_last=False):
    notes = list(RIFFS[riff])
    if skip_last: notes = [n for n in notes if n[0] < 6.5]
    for i, (b, length, note, ch) in enumerate(notes):
        if rng.random() < mutate:                       # the hand moves to another wobble channel
            ch = rng.choice(["w8", "w16", "w4", "w32"])
        t = start_bar * 4 + b + j(5) - (early if i == 0 else 0.0)
        ev("Sub", "sine", t, length, pitch=SUB[note], gain=0, fout=0.12)
        if ch:
            ev(WOB_TRACK[ch], "reese", t, length, offset=REESE_OFF, pitch=WOB[note], gain=rng.uniform(-1.5, 0), fout=0.1)

# ------------------------------------------------------------------ drums, bar by bar
KICKS = [[0], [0, 10], [0, 7], [0, 14], [0, 3, 10], [0, 11]]
def hats(bar, density, shuffle):
    for step in range(16):
        p = 0.9 if step % 2 == 0 else density
        if step % 4 == 0 and rng.random() < 0.15: continue          # live: sometimes the 8th just isn't there
        if rng.random() > p: continue
        pos = (step // 2) * 0.5 + (step % 2) * 0.5 * shuffle         # garage shuffle on the off-16ths
        g = (-3 if step % 4 == 2 else -6 if step % 2 == 0 else -10) + rng.uniform(-2, 1.5)
        ev("Hats", "hat", bar * 4 + pos + j(6), 0.2, gain=g)

def triplet_rims(bar, n):
    slots = rng.sample([1, 2, 4, 5, 7, 8, 10, 11], n)               # 8th-note triplet grid, avoid the downbeats
    for s in sorted(slots):
        ev("Perc", "rim", bar * 4 + s / 3.0 + j(5), 0.3, gain=rng.uniform(-12, -6), pitch=rng.choice([0, 0, -2, 3]))

throw_bars = set(rng.sample(range(5, 31), 5))
for bar in range(BARS):
    sec_, b0 = section(bar), bar * 4.0
    drop = sec_ in ("dropA", "dropA2", "dropB")
    rewind_bar = bar == 11                                          # first drop ends in a rewind
    cut_bar = bar in (3, 23)                                         # hard DJ cut on the last half beat
    last = bar == 31
    if drop:
        k = list(rng.choice(KICKS)) if bar % 2 else [0] + ([10] if rng.random() < 0.4 else [])
        if sec_ == "dropB" and rng.random() < 0.5: k.append(rng.choice([6, 13, 15]))
        for s in sorted(set(k)):
            if (rewind_bar or last) and s >= 8: continue
            early = ms(-9) if s == 0 and bar in (4, 12, 24) else 0.0   # drop kicks land a touch early (adrenaline)
            ev("Kick", "kick", b0 + s * 0.25 + early + j(3), 1.2, gain=0 if s == 0 else -3)
        # snare + clap on 3 (half-step); live mute in bar 17; fat-finger flam in bar 27
        if bar != 17 and not rewind_bar:
            t = b0 + 2 + ms(rng.uniform(2, 9))
            ev("Snare", "snare", t, 1.0, gain=rng.uniform(-1, 0))
            ev("Clap", "clap", t + ms(rng.uniform(-3, 3)), 1.4, gain=rng.uniform(-3, -1))
            if bar == 27:
                ev("Snare", "snare2", t + ms(23), 0.5, gain=-7)        # flam: hit twice by accident
            if bar in throw_bars:
                ev("Throw", "clap", t, 1.0, gain=-2)                  # send that one to the delay
        if rng.random() < 0.35 and not rewind_bar:                    # ghost snare / pickup
            s = rng.choice([13, 14, 15])
            ev("Snare", "snare2", b0 + s * 0.25 + j(6), 0.3, gain=rng.uniform(-16, -11))
        if not rewind_bar:
            hats(bar, 0.3 if sec_ == "dropA" else 0.45, rng.uniform(0.56, 0.64))
        else:
            hats(bar, 0.3, 0.6)
            events["Hats"] = [e for e in events["Hats"] if e["startBeat"] < b0 + 2]
        triplet_rims(bar, rng.randint(1, 3))
        if rng.random() < 0.3 and not rewind_bar:
            ev("Hats", "ohat", b0 + rng.choice([1.5, 3.5]) + j(5), 0.9, gain=-6)
    elif sec_ == "intro":
        if bar >= 1: hats(bar, 0.15, 0.6)
        triplet_rims(bar, rng.randint(1, 2) if bar else 1)
        if bar >= 2:
            ev("Kick", "kick", b0 + j(4), 1.0, gain=-6)
            ev("Snare", "snare2", b0 + 2 + ms(6), 0.5, gain=-10)
    elif sec_ == "break":
        hats(bar, 0.2, 0.6)
        triplet_rims(bar, 2)
        if bar == 23:                                                   # snare roll into dropB (accelerating, live)
            for i, s in enumerate(range(0, 14)):
                if s < 8 and s % 2: continue
                ev("Snare", "snare2", b0 + s * 0.25 + j(4), 0.25, gain=-16 + i * 1.1)
    if cut_bar:                                                           # hard cut: remove everything in the last half beat
        for lst in events.values():
            lst[:] = [e for e in lst if not (b0 + 3.5 <= e["startBeat"] < b0 + 4)]

    # crashes and risers
    if bar in (4, 12, 24):
        ev("Perc", "cym", b0 + j(3), 4.0, gain=-5)
    if bar in (3, 23):
        ev("Perc", "revcym", b0 + 2.6, 0.9, gain=-2, pitch=-10)
    if bar in (10, 19, 30):                                              # tom fills
        for i, s in enumerate([12, 13, 14, 15]):
            ev("Perc", "tomh" if i < 2 else "toml", b0 + s * 0.25 + j(6), 0.35, gain=-7 + i)

# ------------------------------------------------------------------ bass
ev("Sub", "sine", 3 * 4 + 2, 1.5, pitch=SUB["F"], gain=-4, fin=1.0)       # intro sub tease
for ph, riff in [(4, "A"), (6, "B"), (8, "A"), (10, "B"), (12, "A"), (14, "B"), (16, "A"), (18, "C"),
                 (24, "C"), (26, "B"), (28, "C"), (30, "C")]:
    bass_phrase(ph, riff, early=ms(110) if ph == 24 else 0.0,           # dropB: the bass comes in a 16th early, eager
                mutate=0.15 if ph < 12 else 0.35, skip_last=ph in (10, 30))
# Rewind at the end of bar 11: drop bass after beat 2, then reversed pitched-up spin-back.
ev("Rewind Stab", "stab", 11 * 4 + 2.2, 1.2, offset=STAB_OFF, pitch=12, rev=True, gain=0, fout=0.2)
# Breakdown: slow swelling notes only on the 1/4 wobble
for i, (bar, note) in enumerate([(20, "F"), (21, "Db"), (22, "Eb"), (23, "C")]):
    ev("Sub", "sine", bar * 4, 3.4 if bar < 23 else 3.4, pitch=SUB[note], gain=-3, fin=0.5, fout=0.6)
    ev("Wobble 1/4", "reese", bar * 4 + j(5), 3.4, offset=REESE_OFF, pitch=WOB[note], gain=-3, fin=0.4, fout=0.6)
for lst in events.values():                                            # bar-23 cut applies to the bass too
    lst[:] = [e for e in lst if not (23 * 4 + 3.5 <= e["startBeat"] < 24 * 4 - ms(150))]
# Final hit: sub F and a stab ring out after the cut on beat 3 of bar 32

# ------------------------------------------------------------------ dub stabs + atmosphere
STAB_BEATS = [(1.5,), (3.25,), (0.75, 2.75)]
for bar in range(2, 31):
    if section(bar) == "intro" and bar < 2: continue
    if bar == 11 or (bar % 2 == 0 and rng.random() < 0.55) or section(bar) == "break":
        for b in rng.choice(STAB_BEATS):
            ev("Stab", "stab", bar * 4 + b + j(8), 0.95, offset=STAB_OFF, gain=rng.uniform(-3, 0))
for i in range(4):                                                       # live stutter: stab x4 in bar 20, dying away
    ev("Stab", "stab", 20 * 4 + 3 + i * 0.25 + j(3), 0.25, offset=STAB_OFF, gain=-1 - 3 * i)
for start, length in [(0, 16), (80, 16)]:                                # Fm9 atmosphere: intro and breakdown
    for k in range(int(length / 2)):
        t = start + k * 2.0
        ev("Atmos A" if k % 2 == 0 else "Atmos B", "pad", t, 3.0, offset=PAD_OFF, pitch=-12, gain=-2 if k else -10,
           fin=1.2, fout=1.2)
ev("Vinyl", "vinyl", 0, BARS * 4, looping=True, gain=0, fin=2, fout=3)

def silence(start, end, keep=("Atmos A", "Atmos B", "Vinyl", "Rewind", "Rewind Stab")):
    """Live cut: nothing new starts in [start, end), and anything still sounding stops at start."""
    for t in events:
        if t in keep: continue
        events[t] = [e for e in events[t] if not (start <= e["startBeat"] < end)]
        for e in events[t]:
            if e["startBeat"] < start < e["startBeat"] + e["lengthBeats"]:
                e["lengthBeats"] = round(start - e["startBeat"], 5)
silence(11 * 4 + 2, 12 * 4)                    # rewind: the DJ pulls it up mid-bar ...
ev("Rewind", "reese", 11 * 4 + 2, 1.5, offset=REESE_OFF, pitch=12, rev=True, gain=-2, fout=0.3)   # ... spin-back
silence(31 * 4 + 2, 32 * 4)                    # the end: cut on beat 3, one last sub + stab ring out
ev("Sub", "sine", 31 * 4 + 2, 2.0, pitch=SUB["F"], gain=-1, fout=1.5)
ev("Stab", "stab", 31 * 4 + 2 + ms(4), 1.0, offset=STAB_OFF, gain=0)

TRACKS = [  # name, colour, gain, pan
    ("Kick", 0, -2, 0.0), ("Snare", 1, -5, 0.0), ("Clap", 1, -7, 0.0), ("Hats", 6, -8, 0.2), ("Perc", 7, -8, -0.25),
    ("Throw", 2, -9, 0.0), ("Sub", 4, -5, 0.0), ("Wobble 1/8", 5, -7, -0.1), ("Wobble 1/16", 5, -8, 0.1),
    ("Wobble 1/4", 5, -7, 0.0), ("Wobble 1/32", 5, -10, 0.0), ("Stab", 3, -8, 0.15), ("Atmos A", 3, -6, -0.2),
    ("Atmos B", 3, -6, 0.2), ("Rewind", 2, -8, 0.0), ("Rewind Stab", 2, -9, 0.0), ("Vinyl", 7, -18, 0.0)]

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    print(sum(len(v) for v in events.values()), "events planned")
    bridge = find_bridge(ARGS.bridge)
    if bridge is None:
        sys.exit("Can't find mcp-daw-bridge: pass --bridge <path> or set MCP_DAW_BRIDGE")
    with McpDaw(bridge=bridge, client_name="Example: Croydon After Dark") as daw:
        daw.call("new_project", discardChanges=False)
        daw.call("set_project_name", name="Croydon After Dark (live set)")
        assets = {k: daw.call("import_audio", path=str(p), place=False, sourceBpm=0)["assetId"] for k, p in SAMPLES.items()}
        daw.call("set_project_tempo", bpm=BPM)
        ids = {}
        for name, colour, gain, pan in TRACKS:
            tid = daw.call("create_track", name=name)["trackId"]
            ids[name] = tid
            daw.batch(f"Mix {name}", [
                {"action": "set_track_colour", "arguments": {"trackId": tid, "colour": colour}},
                {"action": "set_track_gain", "arguments": {"trackId": tid, "db": gain}},
                {"action": "set_track_pan", "arguments": {"trackId": tid, "pan": pan}}])

        def fx(target, kind, **p): daw.call("add_effect", target=ids.get(target, target), type=kind, parameters=p, detail="compact")
        fx("Kick", "eq", **{"band1.enabled": True, "band1.freq": 30, "band2.freq": 55, "band2.gain": 3, "band3.freq": 280, "band3.gain": -5})
        fx("Kick", "compressor", threshold=-12, ratio=3, attack=15, release=80)
        fx("Snare", "eq", **{"band1.enabled": True, "band1.freq": 150, "band4.freq": 2500, "band4.gain": 2})
        fx("Snare", "reverb", mix=0.18, decay=1.1, size=0.45, damping=0.6)
        fx("Clap", "eq", **{"band1.enabled": True, "band1.freq": 300})
        fx("Clap", "reverb", mix=0.25, decay=1.6, size=0.55, predelay=12)
        fx("Hats", "eq", **{"band1.enabled": True, "band1.freq": 600})
        fx("Perc", "eq", **{"band1.enabled": True, "band1.freq": 200})
        fx("Perc", "multitap_delay", **{"tap1.division": "1/8T", "tap1.feedback": 0.3, "tap2.enabled": False, "mix": 0.25, "highcut": 4000})
        fx("Perc", "reverb", mix=0.2, decay=1.5, size=0.5)
        # The throw: a dub delay send the DJ slams on now and then.
        fx("Throw", "eq", **{"band1.enabled": True, "band1.freq": 400, "band6.enabled": True, "band6.freq": 3500})
        fx("Throw", "multitap_delay", **{"tap1.division": "1/8D", "tap1.feedback": 0.72, "tap1.pan": -0.5, "tap2.division": "1/4",
                                         "tap2.pan": 0.6, "tap2.level": -6, "mix": 0.85, "highcut": 2500, "drive": 0.4})
        fx("Throw", "reverb", mix=0.35, decay=3.5, size=0.8)
        fx("Sub", "eq", **{"band6.enabled": True, "band6.freq": 160, "band1.enabled": True, "band1.freq": 25})
        fx("Sub", "compressor", threshold=-10, ratio=2.5, attack=25, release=120)
        # Wobbles: tremolo BEFORE the wavefolder -> the fold (brightness) follows the cycle, like a filter wobble.
        for name, div, shape, depth in [("Wobble 1/8", "1/8", "Sine", 30), ("Wobble 1/16", "1/16", "Sine", 30),
                                        ("Wobble 1/4", "1/4", "Triangle", 26), ("Wobble 1/32", "1/32", "Square", 36)]:
            fx(name, "tremolo", division=div, shape=shape, depth=depth, restart="Event", phase=0)
            fx(name, "distortion", type="Fold", drive=20, bias=0.15, lowcut=60, tone=5500, autogain=True, mix=1.0)
            fx(name, "eq", **{"band1.enabled": True, "band1.freq": 110, "band3.freq": 600, "band3.gain": 3, "band3.q": 0.9,
                              "band6.enabled": True, "band6.freq": 7000})
            fx(name, "compressor", threshold=-16, ratio=4, attack=5, release=80, makeup=3)
        fx("Stab", "eq", **{"band1.enabled": True, "band1.freq": 250, "band6.enabled": True, "band6.freq": 5000})
        fx("Stab", "multitap_delay", **{"tap1.division": "1/4D", "tap1.feedback": 0.55, "tap2.enabled": False, "mix": 0.4, "highcut": 2500, "drive": 0.3})
        fx("Stab", "reverb", mix=0.35, decay=4.0, size=0.8, damping=0.6)
        for t in ("Atmos A", "Atmos B"):
            daw.call("add_effect_preset", target=ids[t], preset="cinematic_synth_ambience")
            fx(t, "phaser", sync=True, division="2 bars", depth=0.7, centre=600, feedback=0.4, mix=0.45)
        # Rewind whoosh: a deep flanger on the reversed spin-back.
        for t in ("Rewind", "Rewind Stab"):
            fx(t, "chorus", sync=True, division="1/4", delay=2, depth=0.9, feedback=0.75, mix=0.6, spread=90)
            fx(t, "reverb", mix=0.3, decay=2.0, size=0.6)
        fx("Vinyl", "eq", **{"band1.enabled": True, "band1.freq": 150})
        fx("master", "compressor", threshold=-12, ratio=2.5, attack=20, release=150, knee=8, makeup=1)
        fx("master", "eq", **{"band1.enabled": True, "band1.freq": 22})
        daw.call("set_master_gain", db=-3)

        steps = []
        for track, lst in events.items():
            for a in sorted(lst, key=lambda a: a["startBeat"]):
                steps.append({"action": "create_event", "arguments": dict(a, trackId=ids[track], assetId=assets[a["assetId"]])})
        for i in range(0, len(steps), 400):
            daw.batch(f"Set events {i // 400 + 1}", steps[i:i + 400])
        print("placed", len(steps), "events")
        daw.call("set_loop_region", startBeat=0, endBeat=BARS * 4, enabled=True)
        try: daw.call("zoom_to_fit")
        except Exception: pass
        r = daw.call("render_mix", path=str(OUT / "croydon-after-dark.wav"), overwrite=True, tailSeconds=5)
        print("render:", r)
        daw.call("save_project", path=str(OUT / "croydon-after-dark.mcpdaw"), overwrite=True)
        daw.call("seek", beat=0)

if __name__ == "__main__":
    main()
