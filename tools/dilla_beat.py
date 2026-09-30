#!/usr/bin/env python3
"""Dilla-style beat from one-shots - drives a running MCP DAW over MCP.

Every WAV is treated as a sample to chop: the script finds the hits in each file (onsets), then
places individual hits as one-shot events with deliberately "drunk" timing - late, lazy kicks,
behind-the-beat snares with a flam layer, heavily swung hats, a chopped and re-sequenced keys loop
and bassline, a pad swell, vocal chops and vinyl. The current project is wiped WITHOUT saving.

    python dilla_beat.py                 # uses outputs/demo-loops + outputs/jungle-loops
    python dilla_beat.py --seed 3        # a different humanisation / variation
    python dilla_beat.py --bpm 84 --swing 0.66 --drunk 1.4

Everything that shapes the feel is in the FEEL block below - that is the bit to tweak between
runs ("lazier kick", "more swing", "busier hats").

Standard library only (Python 3.9+).
"""
from __future__ import annotations

import argparse
import random
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # mcp_daw.py lives next to this script
from mcp_daw import McpDaw, ToolError, find_bridge  # noqa: E402

# ============================================================================ FEEL (tweak me)

FEEL = {
    "bpm": 88.0,
    "swing": 0.62,             # 0.5 = straight 16ths, 0.66 = triplet swing (applies to hats, keys, bass)
    "kick_late_ms": 28,        # kicks sit behind the grid ...
    "kick_wander_ms": 18,      # ... by a different amount every hit
    "snare_late_ms": 14,       # snares lean back slightly
    "flam_ms": 11,             # dusty break snare layered just after the main snare
    "hat_wander_ms": 9,
    "drunk": 1.0,              # scales every timing offset above (0 = quantised MPC, 1.5 = very loose)
    "bars": 16,
}

# Kick / snare / hat patterns per bar, 16 steps (x = hit, g = ghost / soft, . = rest).
# Two-bar phrases; the last bar of every 4 gets the variation.
KICK = ["x.....x...x..g..", "x..x......x....g", "x.....x...x..g..", "x..x...g.x...x.."]
SNARE = ["....x.......x...", "....x.......x..g", "....x.......x...", "....x...g...x.gx"]
HATS = ["x.x.x.x.x.x.x.x.", "x.x.xgx.x.x.xgx.", "x.x.x.x.x.x.x.x.", "x.xgx.x.xgx.x.xx"]

# Keys chop: (start beat in the bar, chop index, length in beats) - the loop's chords, re-ordered.
# With the demo keys loop: 0 = Em7, 1 = Dm7, 2 = Cmaj7, 3 = Bm7b5.
KEYS = [
    [(0.0, 0, 1.5), (1.5, 1, 1.0), (2.75, 0, 0.75), (3.5, 2, 0.5)],
    [(0.0, 2, 1.0), (1.0, 3, 1.25), (2.5, 1, 1.5)],
]
# Bass chop: (start beat, note index, length). Demo bass notes: 0 E, 1 E, 2 G, 3 A, 4 G, 5 D.
BASS = [
    [(0.0, 0, 1.25), (1.5, 5, 0.75), (2.75, 1, 0.5), (3.5, 3, 0.5)],
    [(0.0, 3, 0.75), (1.0, 2, 1.25), (2.5, 5, 0.75), (3.5, 1, 0.5)],
]

# ============================================================================ onset detection (stdlib)


def onsets(path: Path, window_s: float = 0.01, rise_db: float = 6.0, floor_db: float = 35.0) -> tuple[list[float], float]:
    """Hit start times (seconds) and file length. Energy rise per 10 ms window; PCM 16/24-bit WAV."""
    with wave.open(str(path), "rb") as w:
        rate, width, ch, n = w.getframerate(), w.getsampwidth(), w.getnchannels(), w.getnframes()
        data = w.readframes(n)
    if width not in (2, 3):
        return [0.0], n / rate
    stride, full = width * ch, float(1 << (8 * width - 1))
    win = int(rate * window_s)
    hop = max(1, win // 48)                              # sub-sample each window: fast in pure Python
    levels = []
    for start in range(0, n - win, win):
        acc = 0.0
        for f in range(start, start + win, hop):
            v = int.from_bytes(data[f * stride:f * stride + width], "little", signed=True) / full
            acc += v * v
        levels.append(acc / (win // hop))
    import math
    db = [10 * math.log10(v + 1e-12) for v in levels]
    top = max(db)
    hits = []
    for i, d in enumerate(db):
        prev = sum(db[max(0, i - 3):i]) / max(1, min(3, i)) if i else -120.0
        if d > top - floor_db and d - prev > rise_db and (not hits or i - hits[-1] > 8):
            hits.append(i)
    return [max(0.0, i * window_s - 0.004) for i in hits] or [0.0], n / rate


# ============================================================================ helpers


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


def pick(files: dict[str, Path], *words: str) -> Path | None:
    for w in words:
        for name, p in files.items():
            if w in name:
                return p
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="Dilla-style one-shot beat in MCP DAW over MCP")
    ap.add_argument("folders", nargs="*", type=Path, help="folders of WAVs (default: demo-loops / jungle-loops near this script)")
    ap.add_argument("--bpm", type=float, default=FEEL["bpm"])
    ap.add_argument("--swing", type=float, default=FEEL["swing"])
    ap.add_argument("--drunk", type=float, default=FEEL["drunk"])
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--bridge", type=Path, default=None, help="path to mcp-daw-bridge(.exe) (default: auto-detect)")
    ap.add_argument("--out", type=Path, default=Path.cwd() / "dilla-out", help="render/save folder")
    args = ap.parse_args()

    folders = args.folders or default_sample_folders("demo-loops", "jungle-loops")
    bridge = find_bridge(args.bridge)
    files = {p.stem.lower(): p.resolve() for f in folders if f.is_dir() for p in sorted(f.glob("*.wav"))}
    rng = random.Random(args.seed)
    bpm, drunk = args.bpm, args.drunk
    ms = lambda m: m / 1000 * bpm / 60 * drunk            # noqa: E731  milliseconds -> beats, scaled by 'drunk'

    def log(tag, text):
        print(f"[{tag:>7}] {text}", flush=True)

    src = {
        "kick": pick(files, "kick"), "snare": pick(files, "snare_90", "snare"), "hat": pick(files, "hat"),
        "brk": pick(files, "break_1", "break"), "keys": pick(files, "keys", "chord"), "bass": pick(files, "bass_90", "bass"),
        "vox": pick(files, "vox"), "vinyl": pick(files, "vinyl", "crackle"),
    }
    missing = [k for k in ("kick", "snare", "hat") if src[k] is None]
    if missing or bridge is None:
        print("Missing:", missing or "mcp-daw-bridge bridge (pass --bridge <path> or set MCP_DAW_BRIDGE)")
        print("Sample folders searched:", *folders, sep="\n  ")
        return 2

    print("\n  MCP DAW x Claude - Dilla-style beat from one-shots\n")
    hits = {}
    for role, p in src.items():
        if p:
            hits[role] = onsets(p)
            log(role, f"{p.name}: {len(hits[role][0])} hits")

    daw = McpDaw(bridge, client_name="dilla-beat")
    try:
        daw.call("new_project", discardChanges=True)          # wiped, not saved
        daw.call("set_project_name", name="Dilla Dust")
        daw.call("set_master_gain", db=-5)
        assets = {}
        for role, p in src.items():
            if p:
                assets[role] = daw.call("import_audio", path=p.as_posix(), place=False, sourceBpm=0)["assetId"]
        daw.call("set_project_tempo", bpm=bpm)
        log("tempo", f"{bpm} BPM, swing {args.swing:.2f}, drunk x{drunk}")

        tracks = {}

        def track(name, colour, db, pan=0.0):
            tid = daw.call("create_track", name=name)["trackId"]
            daw.call("set_track_colour", trackId=tid, colour=colour)
            daw.call("set_track_gain", trackId=tid, db=db)
            if pan:
                daw.call("set_track_pan", trackId=tid, pan=pan)
            tracks[name] = tid
            return tid

        def fx(target, kind, **p):
            daw.call("add_effect", target=target, type=kind, parameters=p)

        t = track("Kick", 0, -3)
        fx(t, "eq", **{"band1.enabled": True, "band1.freq": 35, "band2.freq": 70, "band2.gain": 3, "band3.freq": 350, "band3.gain": -4})
        t = track("Snare", 1, 3)
        fx(t, "eq", **{"band1.enabled": True, "band1.freq": 120, "band4.freq": 2500, "band4.gain": 6, "band4.q": 0.8})
        fx(t, "compressor", threshold=-18, ratio=3, attack=8, release=90, makeup=3)
        fx(t, "reverb", mix=0.16, decay=0.8, size=0.35, damping=0.7)
        if "brk" in assets:
            t = track("Dust Snare", 1, 0, pan=0.1)
            fx(t, "eq", **{"band1.enabled": True, "band1.freq": 300, "band6.enabled": True, "band6.freq": 6000})
        t = track("Hats", 6, -1, pan=0.25)
        fx(t, "eq", **{"band1.enabled": True, "band1.freq": 500, "band5.gain": -3})
        if "keys" in assets:
            t = track("Keys Chop", 5, -5, pan=-0.15)
            fx(t, "eq", **{"band1.enabled": True, "band1.freq": 180, "band6.enabled": True, "band6.freq": 3200, "band6.q": 0.9})
            fx(t, "reverb", mix=0.25, decay=2.2, size=0.6, damping=0.7)
        if "bass" in assets:
            t = track("Bass Chop", 4, -7)
            fx(t, "eq", **{"band6.enabled": True, "band6.freq": 900, "band2.freq": 90, "band2.gain": 2})
            fx(t, "compressor", threshold=-16, ratio=3, attack=15, release=120, makeup=2)
        if "vox" in assets:
            t = track("Vox", 2, -9, pan=0.3)
            fx(t, "multitap_delay", **{"tap1.division": "1/8D", "tap1.feedback": 0.45, "mix": 0.4, "highcut": 3500})
        if "vinyl" in assets:
            track("Vinyl", 7, -14)
        fx("master", "compressor", threshold=-14, ratio=2.5, attack=30, release=180, knee=8, makeup=2)
        fx("master", "eq", **{"band6.enabled": True, "band6.freq": 11000})

        # --- event builder ------------------------------------------------------------------
        actions, notes, placed = [], [], 0

        def hit(tname, role, beat, length, index=0, gain_db=0.0, near=None):
            """One-shot of hit `index` (or the hit nearest `near` seconds) from a sample, at a humanised beat."""
            starts, _ = hits[role]
            offset = min(starts, key=lambda s: abs(s - near)) if near is not None else starts[index % len(starts)]
            actions.append({"action": "create_event", "arguments": {
                "trackId": tracks[tname], "assetId": assets[role], "startBeat": round(max(0.0, beat), 5),
                "lengthBeats": round(length, 5), "looping": False, "sourceOffsetSeconds": round(offset, 5)}})
            if gain_db:
                notes.append(({"action": "set_event_gain", "arguments": {"eventId": f"${len(actions) - 1}.id", "db": gain_db}}))

        def swung(step):
            """16th step -> beat with swing on the off-16ths."""
            return step * 0.25 + (args.swing - 0.5) * 0.5 * (step % 2)

        for bar in range(FEEL["bars"]):
            b0, v = bar * 4.0, bar % 4
            intro = bar < 2
            for step, c in enumerate(KICK[v]):
                if c != "." and not intro:
                    late = ms(FEEL["kick_late_ms"] + rng.uniform(-1, 1) * FEEL["kick_wander_ms"])
                    hit("Kick", "kick", b0 + swung(step) + late, 0.6, gain_db=-7 if c == "g" else 0)
            for step, c in enumerate(SNARE[v]):
                if c != "." and not intro:
                    t = b0 + step * 0.25 + ms(FEEL["snare_late_ms"] + rng.uniform(-4, 4))
                    hit("Snare", "snare", t, 0.75, gain_db=-10 if c == "g" else 0)
                    if "Dust Snare" in tracks and c == "x":
                        hit("Dust Snare", "brk", t + ms(FEEL["flam_ms"]), 0.5, near=60 / 165)   # the break's beat-2 snare
            for step, c in enumerate(HATS[v]):
                if c != "." and (not intro or step % 4 == 0):
                    t = b0 + swung(step) + ms(rng.uniform(-1, 1) * FEEL["hat_wander_ms"])
                    hit("Hats", "hat", t, 0.2, index=0, gain_db=-8 if c == "g" else rng.choice([0, -2, -4]))
            if "Keys Chop" in tracks:
                for beat, idx, length in KEYS[bar % 2]:
                    hit("Keys Chop", "keys", b0 + beat + ms(rng.uniform(0, 12)), length, index=idx)
            if "Bass Chop" in tracks and not intro and bar not in (7, 15):
                for beat, idx, length in BASS[bar % 2]:
                    hit("Bass Chop", "bass", b0 + beat + ms(FEEL["kick_late_ms"] * 0.6), length, index=idx)
            if "Vox" in tracks and bar % 4 == 3:
                hit("Vox", "vox", b0 + 2.5 + ms(20), 1.0, index=rng.randrange(3))
            if "Vinyl" in tracks and bar % 2 == 0:
                hit("Vinyl", "vinyl", b0, 8.0 if bar + 2 <= FEEL["bars"] else 4.0, index=0)
            if bar % 4 == 3:                          # one undo step per 4 bars ($N refs are per batch)
                daw.call("execute_batch", label=f"Beat bars {bar - 2}-{bar + 1}", actions=actions + notes)
                log("beat", f"bars {bar - 2:>2}-{bar + 1:<2}: {len(actions)} one-shots, {len(notes)} softer hits")
                placed += len(actions)
                actions.clear()
                notes.clear()
        try:
            daw.call("zoom_to_fit")
        except ToolError:
            pass
        daw.call("set_loop_region", startBeat=0, endBeat=FEEL["bars"] * 4.0, enabled=True)
        args.out.mkdir(parents=True, exist_ok=True)
        r = daw.call("render_mix", path=(args.out / "dilla-dust.wav").as_posix(), overwrite=True, tailSeconds=3)
        daw.call("save_project", path=(args.out / "dilla-dust.mcpdaw").as_posix(), overwrite=True)
        log("saved", f"{args.out} (render {r['seconds']:.0f} s)")
        daw.call("play", fromBeat=0)
        log("play", "looping - Space to stop")
        return 0
    except (ToolError, ConnectionError) as e:
        print("FAILED:", e)
        return 1
    finally:
        daw.close()


if __name__ == "__main__":
    sys.exit(main())
