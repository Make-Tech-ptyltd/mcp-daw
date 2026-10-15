"""Blade Runner pads x Jay Dilla break - 16 bars, built live in MCP DAW over MCP.

Every drum bar is generated (no repeated bars): drifting late kicks/snares (random walk, not
per-hit noise), per-beat swing, ghost notes, displaced snares, chopped + crushed break layer.
Pads: chord windows chopped from a detuned-saw string pad, sustained as crossfaded chains.

Run it with MCP DAW installed (MCP DAW opens if it isn't running; an open project with unsaved
changes is never discarded - the script stops instead):

    python blade_dilla.py                 # the published song -> ./out/tears-in-the-dust.mcpdaw + .wav
    python blade_dilla.py --seed 42       # a new variation with the same writing
    python blade_dilla.py --bridge "C:/path/to/mcp-daw-bridge.exe"
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
ap.add_argument("--seed", type=int, default=7, help="humanisation / variation seed (7 = the published song)")
ARGS = ap.parse_args()
OUT = ARGS.out
KIT, OWN = TOP / "Samples-Starter-Kit", SONGS / "samples"
SAMPLES = {
    "kick": KIT / "kick-slight-distrotion.wav", "kick2": KIT / "kick-deep-clean.wav",
    "snare": KIT / "snare-basic.wav", "snare2": OWN / "snare-2.wav",
    "hat": OWN / "ch.wav", "ohat": OWN / "cs10-open-hihat1.wav",
    "clap": KIT / "kens-clap.wav", "rim": KIT / "reverse-tapping-stick.wav", "cym": KIT / "cymbal-little1.wav",
    "tomh": KIT / "tom-high.wav", "toml": KIT / "tom-low.wav", "revcym": KIT / "reverse-percusion-cymbal.wav",
    "brk": OWN / "break_165bpm.wav", "pad": OWN / "dark_pad_165bpm.wav", "keys": OWN / "keys_90bpm.wav",
    "sine": KIT / "55hz-sine-wave_bass-note-A1.wav", "vinyl": KIT / "end-of-song-vinyl-silent-crackle_loop.wav",
}

BPM, BARS = 86.0, 16
rng = random.Random(ARGS.seed)
def ms(m): return m / 1000.0 * BPM / 60.0          # milliseconds -> beats
def sec(s): return s * BPM / 60.0                   # seconds -> beats

# Harmony, one chord per bar.  pad chord index: Fm9 0, Dbmaj7 1, Bbm7 2, Cm7 3
CHORDS = ["F", "F", "Db", "Db", "Bb", "Bb", "C", "C", "F", "F", "Db", "Db", "Bb", "Bb", "C", "F"]
PAD_IDX = {"F": 0, "Db": 1, "Bb": 2, "C": 3}
BASS_PITCH = {"F": -4, "Db": 4, "Bb": 1, "C": 3}               # from A1 (55 Hz): F1, Db2, Bb1, C2
KEYS_SRC = {"F": 0.0, "Db": 8 / 3.0}                            # keys loop (90 BPM) +1 st: Em7->Fm7, Cmaj7->Dbmaj7
PAD_CHORD_S = 4 * 60 / 165                                      # one chord per bar at 165 BPM
BREAK_BEAT_S = 60 / 165
BRK = {"snare": 4 * 0.25 * BREAK_BEAT_S, "ghost": 7 * 0.25 * BREAK_BEAT_S + 0.022 * BREAK_BEAT_S,
       "ghost2": 9 * 0.25 * BREAK_BEAT_S + 0.022 * BREAK_BEAT_S, "kick": 0.0}

def section(bar):
    if bar < 2: return "intro"
    if bar in (8, 9): return "break"
    if bar == 15: return "outro"
    return "beat" if bar < 8 else "beat2"

# ------------------------------------------------------------------------------- event list
events = {}                                      # track -> list of create_event argument dicts
def ev(track, sample, beat, length, offset=0.0, gain=0.0, pitch=0.0, fin=0.0, fout=0.0, curve="smooth", looping=False):
    if beat < 0: beat = 0.0
    a = {"assetId": sample, "startBeat": round(beat, 5), "lengthBeats": round(length, 5), "looping": looping,
         "sourceOffsetSeconds": round(offset, 5)}
    if gain: a["gainDb"] = round(gain, 2)
    if pitch: a["pitchSemitones"] = round(pitch, 3)
    if fin: a["fadeInBeats"] = round(fin, 4); a["fadeInCurve"] = curve
    if fout: a["fadeOutBeats"] = round(fout, 4); a["fadeOutCurve"] = curve
    events.setdefault(track, []).append(a)

class Drift:
    """Random walk around a centre (ms): the groove leans and recovers instead of jittering."""
    def __init__(self, centre, spread, step):
        self.c, self.s, self.st, self.v = centre, spread, step, centre
    def next(self):
        self.v += rng.gauss(0, self.st) + 0.25 * (self.c - self.v)
        self.v = max(self.c - 2.5 * self.s, min(self.c + 2.5 * self.s, self.v))
        return self.v

kick_late, snare_late, hat_late = Drift(16, 10, 7), Drift(30, 8, 6), Drift(8, 6, 4)
KICK_POOL = [[0, 10], [0, 7, 10], [0, 3, 10, 14], [0, 6, 10, 11], [0, 10, 13], [0, 2, 10], [0, 9, 11]]
GHOST_STEPS = [2, 6, 7, 9, 11, 14, 15]

seen_patterns = set()
for bar in range(BARS):
    b0, sec_ = bar * 4.0, section(bar)
    chord = CHORDS[bar]
    full = sec_ in ("beat", "beat2")

    # ---- hats: per-beat swing, lagging "ands", velocity shape, dropped hits, open hat on 4&
    swing_by_beat = [rng.uniform(0.54, 0.70) for _ in range(4)]
    open_step = 14 if (full and rng.random() < 0.35) else None
    hat_steps = []
    for step in range(16):
        if sec_ == "intro" and (bar == 0 or step % 4): continue
        if sec_ == "break" and step % 4 and rng.random() < 0.5: continue
        if sec_ == "outro" and step >= 8: continue
        on = step % 2 == 0 and rng.random() < 0.93
        sixteenth = step % 2 == 1 and rng.random() < (0.32 if sec_ == "beat2" else 0.22)
        if not (on or sixteenth): continue
        if open_step is not None and step in (15,): continue
        hat_steps.append(step)
    for step in hat_steps:
        beat_in_bar = step // 4
        pos = (step // 2) * 0.5 + (step % 2) * 0.5 * swing_by_beat[beat_in_bar]
        if step % 4 == 2: pos += ms(rng.uniform(0, 18))                 # lazy "ands"
        t = b0 + pos + ms(hat_late.next() + rng.uniform(-6, 6))
        g = (0 if step % 4 == 0 else -4 if step % 2 == 0 else -9) + rng.uniform(-2.5, 2.0)
        if step == open_step:
            ev("Hats", "ohat", t, 0.9, gain=g - 1)
        else:
            ev("Hats", "hat", t, 0.22, gain=g)

    # ---- kick: pool + mutation, never the same pattern twice in a row
    if full or (sec_ == "outro"):
        while True:
            k = list(rng.choice(KICK_POOL))
            if rng.random() < 0.35: k.append(rng.choice([3, 6, 7, 13, 14, 15]))
            if rng.random() < 0.2 and len(k) > 2: k.remove(rng.choice(k[1:]))
            k = sorted(set(k))
            if sec_ == "outro": k = [s for s in k if s < 8]
            key = (tuple(k), bar % 2)
            if key not in seen_patterns or rng.random() < 0.1: break
        seen_patterns.add(key)
        for s in k:
            early = -12 if s % 2 else 0                                  # pushed 16ths land a hair early
            t = b0 + s * 0.25 + ms(kick_late.next() + early)
            g = 0 if s in (0, 10) else -3 + rng.uniform(-2, 0)
            ev("Kick", "kick2" if (s == 0 and rng.random() < 0.25) else "kick", t, 0.9, gain=g)

    # ---- snare 2 & 4 (late, wandering), displaced 4s, ghosts, break-snare flam layer
    if sec_ != "intro":
        snare_steps = [4, 12]
        if full and rng.random() < 0.18: snare_steps[1] = rng.choice([13, 14])
        if sec_ == "break": snare_steps = [12]
        if sec_ == "outro": snare_steps = [4]
        ghosts = rng.sample(GHOST_STEPS, rng.randint(1, 3 if sec_ == "beat2" else 2))
        hits = sorted([(s, False) for s in snare_steps] + [(s, True) for s in ghosts if s not in snare_steps])
        for s, ghost in hits:
            if ghost:
                t = b0 + s * 0.25 + ms(rng.uniform(-10, 22))
                ev("Snare", "snare2", t, 0.3, gain=rng.uniform(-22, -16))
                if rng.random() < 0.5:                                    # dusty break ghost on the chop track
                    ev("Break Chop", "brk", t + ms(rng.uniform(-8, 8)), 0.3, offset=BRK[rng.choice(["ghost", "ghost2"])],
                       gain=rng.uniform(-6, -2), pitch=-4)
            else:
                t = b0 + s * 0.25 + ms(snare_late.next())
                ev("Snare", "snare", t, 0.9, gain=rng.uniform(-1.5, 0))
                ev("Break Chop", "brk", t + ms(rng.uniform(7, 15)), 0.55, offset=BRK["snare"], gain=rng.uniform(-3, 0), pitch=-4)
                if sec_ == "beat2" and s in (4, 12):
                    ev("Clap", "clap", t - ms(rng.uniform(4, 12)), 0.5, gain=rng.uniform(-9, -6))

    # ---- percussion colour
    if bar in (2, 10):
        ev("Perc", "cym", b0 + ms(kick_late.v), 1.0, gain=-6)
    if full and rng.random() < 0.45:
        s = rng.choice([3, 11, 15])
        ev("Perc", "rim", b0 + s * 0.25 + ms(rng.uniform(-5, 15)), 0.25, gain=rng.uniform(-14, -9))
    if bar in (7, 14):                                                   # tom fill, last beat
        for i, (s, tom) in enumerate([(12, "tomh"), (13, "tomh"), (14, "toml"), (15, "toml")]):
            if rng.random() < 0.85:
                ev("Perc", tom, b0 + s * 0.25 + ms(rng.uniform(5, 25)), 0.3, gain=-6 + i)
    if bar in (1, 7, 14):                                                # reverse swell into the next section
        ev("Perc", "revcym", b0 + 3.4, 0.6, gain=-4, pitch=-7)

    # ---- bass (roots, lazy), not in the intro's first bar
    if bar >= 1 and sec_ != "outro" or bar == 15:
        p = BASS_PITCH[chord]
        first_len = rng.choice([1.5, 2.0, 2.5]) if sec_ != "break" else 3.5
        ev("Bass", "sine", b0 + ms(rng.uniform(18, 34)), first_len, pitch=p, gain=-1, fout=0.3)
        if sec_ in ("beat", "beat2") and rng.random() < 0.75:
            s = rng.choice([10, 11, 14])
            oct_ = 12 if (rng.random() < 0.3 and p + 12 <= 12) else 0
            ev("Bass", "sine", b0 + s * 0.25 + ms(rng.uniform(10, 30)), rng.choice([0.5, 0.75, 1.0]),
               pitch=p + oct_ - (12 if p + oct_ > 12 else 0), gain=-3, fout=0.2)

    # ---- Rhodes stabs (F and Db bars), off the grid
    if chord in KEYS_SRC and sec_ != "intro" or bar == 1:
        src = KEYS_SRC.get(chord, 0.0)
        for s in rng.sample([2, 3, 6, 10, 11, 14], rng.randint(1, 2)):
            ev("Keys", "keys", b0 + s * 0.25 + ms(rng.uniform(10, 40)), rng.choice([0.75, 1.0, 1.5]), offset=src, pitch=1,
               gain=rng.uniform(-3, 0), fout=0.3)

# ---- pads: crossfaded chains of chord windows (A/B alternate so no event trims another)
def pad_chain(tracks, pitch, base_gain, win_s=1.26):
    rate = 2 ** (pitch / 12)
    L = sec(win_s / rate)                       # event length in beats for the steady chord window
    F, S = 0.45 * L, 0.55 * L
    t, k = 0.0, 0
    while t < BARS * 4:
        mid = min(BARS * 4 - 0.01, t + L / 2)
        chord = CHORDS[int(mid // 4)]
        g = base_gain
        if t < 3: g += -12 + 12 * min(1, t / 3)                         # intro swell (first 3 beats)
        if t > BARS * 4 - 6: g += -18 * min(1, (t - (BARS * 4 - 6)) / 6)  # outro fade
        ev(tracks[k % 2], "pad", t, L, offset=PAD_IDX[chord] * PAD_CHORD_S + 0.12, pitch=pitch, gain=g,
           fin=F, fout=F)
        t += S
        k += 1

pad_chain(["Pad A", "Pad B"], -0.06, 0.0)
pad_chain(["Pad Low A", "Pad Low B"], -11.93, -3.0)
ev("Vinyl", "vinyl", 0, BARS * 4, looping=True, gain=8, fin=1.0, fout=2.0)

# ------------------------------------------------------------------------------- build it
TRACKS = [  # name, colour, gain dB, pan
    ("Kick", 0, -1, 0.0), ("Snare", 1, -3, 0.05), ("Break Chop", 1, -7, -0.1), ("Clap", 2, -6, 0.15),
    ("Hats", 6, -9, 0.25), ("Perc", 7, -6, -0.3), ("Bass", 4, -6, 0.0), ("Keys", 5, -9, -0.2),
    ("Pad A", 3, -5, -0.15), ("Pad B", 3, -5, 0.15), ("Pad Low A", 3, -8, 0.0), ("Pad Low B", 3, -8, 0.0),
    ("Vinyl", 7, -10, 0.0)]

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    total = sum(len(v) for v in events.values())
    print(f"{total} events planned on {len(events)} tracks")
    bridge = find_bridge(ARGS.bridge)
    if bridge is None:
        sys.exit("Can't find mcp-daw-bridge: pass --bridge <path> or set MCP_DAW_BRIDGE")
    with McpDaw(bridge=bridge, client_name="Example: Tears in the Dust") as daw:
        daw.call("new_project", discardChanges=False)   # refuses if the open project has unsaved changes
        daw.call("set_project_name", name="Tears in the Dust (Blade Runner x Dilla)")
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
        fx("Kick", "eq", **{"band1.enabled": True, "band1.freq": 32, "band2.freq": 65, "band2.gain": 3, "band3.freq": 320, "band3.gain": -4})
        fx("Snare", "eq", **{"band1.enabled": True, "band1.freq": 140, "band4.freq": 3000, "band4.gain": 3})
        fx("Snare", "compressor", threshold=-20, ratio=3, attack=10, release=100, makeup=3)
        fx("Snare", "reverb", mix=0.14, decay=0.9, size=0.35, damping=0.7, predelay=8)
        # The dusty sampler layer: band-limited and crushed (SP-1200-ish).
        fx("Break Chop", "eq", **{"band1.enabled": True, "band1.freq": 250, "band6.enabled": True, "band6.freq": 6500})
        fx("Break Chop", "distortion", type="Warm", drive=8, bits=11, downsample=2, mix=0.7, tone=7000)
        fx("Clap", "reverb", mix=0.22, decay=1.2, size=0.4)
        fx("Hats", "eq", **{"band1.enabled": True, "band1.freq": 450, "band5.gain": -2.5})
        fx("Perc", "reverb", mix=0.3, decay=2.5, size=0.6, damping=0.6)
        fx("Bass", "distortion", type="Warm", drive=9, bias=0.2, tone=1800, mix=0.45)
        fx("Bass", "eq", **{"band1.enabled": True, "band1.freq": 30, "band6.enabled": True, "band6.freq": 1200})
        fx("Bass", "compressor", threshold=-16, ratio=3, attack=20, release=150, makeup=2)
        # Rhodes: per-note tremolo shimmer, dotted-quarter echoes, room.
        fx("Keys", "eq", **{"band1.enabled": True, "band1.freq": 180, "band6.enabled": True, "band6.freq": 4500})
        fx("Keys", "tremolo", division="1/8", shape="Sine", depth=4, restart="Event")
        fx("Keys", "multitap_delay", **{"tap1.division": "1/4D", "tap1.feedback": 0.35, "tap2.enabled": False, "mix": 0.22, "highcut": 3000})
        fx("Keys", "reverb", mix=0.3, decay=3.0, size=0.7, damping=0.6)
        for t in ("Pad A", "Pad B"):
            daw.call("add_effect_preset", target=ids[t], preset="cinematic_synth_ambience")
        for t in ("Pad Low A", "Pad Low B"):
            fx(t, "eq", **{"band1.enabled": True, "band1.freq": 45, "band6.enabled": True, "band6.freq": 1800})
            fx(t, "phaser", sync=True, division="4 bars", depth=0.6, centre=700, feedback=0.35, mix=0.4)
            fx(t, "chorus", rate=0.12, depth=0.25, delay=14, spread=150, mix=0.4)
            fx(t, "reverb", mix=0.35, decay=7.5, size=0.95, predelay=40, damping=0.55, lowcut=90)
        fx("Vinyl", "eq", **{"band1.enabled": True, "band1.freq": 120})
        fx("master", "compressor", threshold=-14, ratio=2, attack=30, release=200, knee=8, makeup=2)
        fx("master", "eq", **{"band6.enabled": True, "band6.freq": 13000})
        daw.call("set_master_gain", db=-3)

        steps = []
        for track, lst in events.items():
            for a in sorted(lst, key=lambda a: a["startBeat"]):
                steps.append({"action": "create_event", "arguments": dict(a, trackId=ids[track], assetId=assets[a["assetId"]])})
        for i in range(0, len(steps), 400):
            daw.batch(f"Song events {i // 400 + 1}", steps[i:i + 400])
        print(f"placed {len(steps)} events")
        daw.call("set_loop_region", startBeat=0, endBeat=BARS * 4, enabled=True)
        try: daw.call("zoom_to_fit")
        except Exception: pass
        r = daw.call("render_mix", path=str(OUT / "tears-in-the-dust.wav"), overwrite=True, tailSeconds=5)
        print("render:", r)
        daw.call("save_project", path=str(OUT / "tears-in-the-dust.mcpdaw"), overwrite=True)
        daw.call("seek", beat=0)

if __name__ == "__main__":
    main()
