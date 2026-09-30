#!/usr/bin/env python3
"""Generate original, royalty-free 90s-style jungle loops for MCP DAW (165 BPM, 48 kHz, 24-bit stereo).

Nothing here is sampled: every drum hit, bass and pad is synthesised from oscillators and noise,
then pushed through a crude "early-90s sampler" chain (sample-rate reduction, 12-bit quantising,
tape-ish saturation) for the grime. The break follows the well-known two-bar funk-break *rhythm*
(a rhythm pattern, not a recording). Tempo is in the filename ("_165bpm"), which MCP DAW reads.

Usage:  python make_jungle_loops.py <output folder>
"""
import sys
import wave
from pathlib import Path

import numpy as np
from scipy import signal

SR = 48000
BPM = 165.0
BEAT = 60.0 / BPM
STEP = BEAT / 4                      # one 16th
rng = np.random.default_rng(165)


# ----------------------------------------------------------------------------- helpers
def n_beats(beats):
    return int(round(beats * BEAT * SR))


def env(n, attack=0.001, decay=0.2):
    """Linear attack, exponential decay."""
    t = np.arange(n) / SR
    return np.clip(t / max(attack, 1e-6), 0, 1) * np.exp(-t / decay)


def filt(x, kind, freq, order=2):
    sos = signal.butter(order, freq, btype=kind, fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def place(buf, sound, beat, gain=1.0):
    """Mix `sound` into `buf` at a (possibly fractional) beat; wraps the tail to the loop start."""
    start = int(round(beat * BEAT * SR)) % len(buf)
    n = len(sound)
    first = min(n, len(buf) - start)
    buf[start:start + first] += gain * sound[:first]
    if first < n:                                   # seamless loop: tail continues at the top
        buf[: n - first] += gain * sound[first:]


def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def saw(freq_array):
    """Naive (aliasing) saw from a per-sample frequency array: a little aliasing is part of the grit."""
    phase = np.cumsum(freq_array) / SR
    return 2.0 * (phase % 1.0) - 1.0


def glide(freqs, time=0.04):
    """One-pole portamento over a per-sample frequency array."""
    a = np.exp(-1.0 / (time * SR))
    return signal.lfilter([1 - a], [1, -a], freqs, zi=[freqs[0] * a])[0]


def sampler_grit(x, rate=26040, bits=12, drive=1.6):
    """Early-90s sampler: sample & hold down to `rate`, quantise to `bits`, soft-clip, gentle anti-image LPF."""
    idx = np.floor(np.floor(np.arange(len(x)) * rate / SR) * SR / rate).astype(int)
    held = x[np.clip(idx, 0, len(x) - 1)]
    q = 2 ** (bits - 1)
    crushed = np.round(held * q) / q
    return filt(np.tanh(drive * crushed) / np.tanh(drive), "lowpass", rate * 0.45, order=2)


def room(x, amount=0.18, length=0.09):
    """Tiny 'drum room' so the synthetic kit sounds recorded rather than programmed."""
    n = int(length * SR)
    ir = rng.standard_normal(n) * np.exp(-np.arange(n) / (0.025 * SR))
    ir = filt(ir, "lowpass", 6000) / np.sqrt(np.sum(ir ** 2))
    return x + amount * signal.fftconvolve(x, ir)[: len(x)]


# ----------------------------------------------------------------------------- drum voices
def kick():
    n = int(0.28 * SR)
    t = np.arange(n) / SR
    freq = 52 + 120 * np.exp(-t / 0.028)
    body = np.sin(2 * np.pi * np.cumsum(freq) / SR) * env(n, 0.0008, 0.13)
    click = filt(rng.standard_normal(n), "highpass", 2500) * env(n, 0.0003, 0.004) * 0.5
    return np.tanh(2.2 * (body + click))


def snare(pitch=1.0, decay=0.13):
    """Bright, cracking break snare: two detuned shell modes plus band-passed noise."""
    n = int(0.32 * SR)
    t = np.arange(n) / SR
    tone = (np.sin(2 * np.pi * 196 * pitch * t) + 0.6 * np.sin(2 * np.pi * 347 * pitch * t)) * env(n, 0.0005, 0.045)
    noise = filt(rng.standard_normal(n), "bandpass", [1400 * pitch, 9500], order=2) * env(n, 0.0005, decay)
    return np.tanh(1.6 * (0.5 * tone + 0.9 * noise))


def ride(accent=1.0):
    """Metallic ride: six inharmonic square partials (classic cymbal trick), band-passed."""
    n = int(0.5 * SR)
    t = np.arange(n) / SR
    partials = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
    metal = sum(np.sign(np.sin(2 * np.pi * f * 2.1 * t + rng.uniform(0, 6.28))) for f in partials) / 6
    metal = filt(metal, "bandpass", [3200, 13000])
    ping = np.sin(2 * np.pi * 3950 * t) * 0.15
    return accent * 0.13 * (metal + ping) * env(n, 0.0005, 0.11)


def hat():
    n = int(0.05 * SR)
    return 0.22 * filt(rng.standard_normal(n), "highpass", 7000) * env(n, 0.0003, 0.012)


# ----------------------------------------------------------------------------- loops
def swing(step):
    """Push the off-beat 16ths slightly late: human break feel."""
    return step * 0.25 + (0.022 if step % 2 else 0.0)


def break_main():
    """Two-bar break in the classic funk-break rhythm (16th steps 0..31)."""
    buf = np.zeros(n_beats(8))
    K, S, G = kick(), snare(), snare(1.03, 0.06)
    kicks = [0, 2, 10, 11, 16, 18, 26]
    snares = [4, 12, 20, 28, 31]
    ghosts = [7, 9, 15, 23, 25, 30]
    for s in kicks:
        place(buf, K, swing(s), 0.95)
    for s in snares:
        place(buf, S, swing(s), 0.9 if s != 31 else 0.55)
    for s in ghosts:
        place(buf, G, swing(s), 0.32)
    for s in range(0, 32, 2):
        place(buf, ride(1.0 if s % 4 == 0 else 0.7), swing(s), 1.0)
    for s in range(1, 32, 4):
        place(buf, hat(), swing(s), 0.6)
    return sampler_grit(room(buf), drive=1.4)


def break_chop():
    """Two-bar re-edit of the break: displaced kicks, snare rushes, a stutter and a reversed snare into the next bar."""
    buf = np.zeros(n_beats(8))
    K, S = kick(), snare()
    S_up = snare(1.12, 0.1)
    G = snare(1.03, 0.05)
    for s in [0, 6, 10, 16, 18, 25]:
        place(buf, K, swing(s), 0.95)
    for s in [4, 12, 13, 20, 23]:
        place(buf, S, swing(s), 0.9 if s not in (13,) else 0.6)
    for s in [2, 9, 15, 22]:
        place(buf, G, swing(s), 0.32)
    for i, s in enumerate(np.arange(8.0, 9.0, 0.5)):          # 32nd stutter
        place(buf, S_up, s * 0.25, 0.45 + 0.1 * i)
    for i, s in enumerate(range(26, 30)):                     # rising 16th rush
        place(buf, snare(1.0 + 0.05 * i, 0.07), swing(s), 0.45 + 0.12 * i)
    rev = snare(0.94, 0.35)[: int(2 * STEP * SR)][::-1]          # reversed snare swelling into the downbeat
    place(buf, rev * np.linspace(0.2, 1.0, len(rev)), 30 * 0.25, 0.8)
    for s in range(0, 32, 2):
        if s not in (26, 28, 30):
            place(buf, ride(1.0 if s % 4 == 0 else 0.7), swing(s), 1.0)
    return sampler_grit(room(buf), drive=1.5)


def snare_roll():
    """One-bar build: 8ths -> 16ths -> 32nds, rising in level and pitch."""
    buf = np.zeros(n_beats(4))
    hits = [(b, 8) for b in np.arange(0, 2, 0.5)] + [(b, 16) for b in np.arange(2, 3, 0.25)] + \
           [(b, 32) for b in np.arange(3, 4, 0.125)]
    for i, (b, _) in enumerate(hits):
        progress = i / len(hits)
        place(buf, snare(1.0 + 0.25 * progress, 0.07), b, 0.3 + 0.7 * progress)
    return sampler_grit(room(buf, 0.25), drive=2.0)


BASSLINE = [(0, 6, 41), (6, 2, 44), (8, 6, 37), (14, 2, 39)]     # (start beat, beats, MIDI): F, Ab, Db, Eb


def note_track(total_beats, notes, transpose=0):
    freqs = np.zeros(n_beats(total_beats))
    for start, length, m in notes:
        a, b = n_beats(start), n_beats(start + length)
        freqs[a:b] = midi_hz(m + transpose)
    return freqs


def reese():
    """Classic Reese: detuned saws through a slowly breathing low-pass, then driven."""
    f = glide(note_track(16, BASSLINE), 0.05)
    detune = 2 ** (14 / 1200)
    a, b = saw(f * detune), saw(f / detune)
    low = saw(f * 0.5)
    left, right = a + 0.65 * b + 0.4 * low, b + 0.65 * a + 0.4 * low
    t = np.arange(len(f)) / SR
    cutoff = 260 + 520 * (0.5 - 0.5 * np.cos(2 * np.pi * t / (8 * BEAT)))    # breathes over two bars
    out = []
    for ch in (left, right):
        # Chamberlin state-variable low-pass with a moving cutoff (resonance ~ Q 1.8).
        y, lp, bp, q = np.zeros_like(ch), 0.0, 0.0, 1 / 1.8
        g = 2 * np.sin(np.pi * np.minimum(cutoff, SR / 6) / SR)
        for i in range(len(ch)):
            hp = ch[i] - lp - q * bp
            bp += g[i] * hp
            lp += g[i] * bp
            y[i] = lp
        out.append(filt(filt(np.tanh(1.7 * y), "highpass", 55), "lowpass", 3500))
    return out


def sub():
    f = glide(note_track(16, BASSLINE, -12), 0.03)
    s = np.sin(2 * np.pi * np.cumsum(f) / SR)
    edges = np.convolve(np.abs(np.diff(f, prepend=f[0])) > 0.5, np.ones(240), "same") > 0   # soften note joins
    s *= np.where(edges, 0.8, 1.0)
    return np.tanh(1.4 * s)


CHORDS = [[53, 56, 60, 63, 67], [49, 53, 56, 60], [46, 53, 56, 61], [48, 55, 58, 63]]    # Fm9, Dbmaj7, Bbm7, Cm7


def pad():
    """Dark detuned string pad with tape wow, one chord per bar, then lo-fi'd."""
    total = n_beats(16)
    t = np.arange(total) / SR
    wow = 1 + 0.0035 * np.sin(2 * np.pi * 0.55 * t) + 0.0015 * np.sin(2 * np.pi * 3.1 * t)
    left, right = np.zeros(total), np.zeros(total)
    bar = n_beats(4)
    for i, chord in enumerate(CHORDS):
        seg = np.zeros(total)
        seg[i * bar:(i + 1) * bar] = 1
        shape = signal.lfilter([1 - 0.9996], [1, -0.9996], seg)      # slow attack / release (~50 ms time constant x ramps)
        shape /= shape.max()
        for m in chord:
            f0 = midi_hz(m) * wow
            for cents, pan in ((-9, 0.8), (0, 0.5), (8, 0.2)):
                v = saw(f0 * 2 ** (cents / 1200)) * shape
                left += v * pan
                right += v * (1 - pan)
    out = []
    for ch in (left, right):
        ch = filt(filt(ch, "lowpass", 1700, order=4), "highpass", 140)
        out.append(sampler_grit(ch / np.max(np.abs(ch)), rate=22050, bits=12, drive=1.3))
    return out


def stab():
    """Dub/rave minor-chord stab (for the tempo-synced delay to throw around)."""
    total = n_beats(8)
    buf = np.zeros(total)
    n = int(0.35 * SR)
    t = np.arange(n) / SR
    chord = sum(saw(np.full(n, midi_hz(m) * 2 ** (c / 1200))) for m in (65, 68, 72, 77) for c in (-6, 6))
    sweep = 400 + 5200 * np.exp(-t / 0.05)
    # Filter envelope via short overlapping blocks of a band-pass (cheap and suitably crunchy).
    shaped = np.zeros(n)
    block = 256
    for i in range(0, n, block):
        sos = signal.butter(2, [max(200, sweep[i] * 0.5), min(sweep[i] * 1.6, 16000)], btype="bandpass", fs=SR, output="sos")
        shaped[i:i + block] = signal.sosfilt(sos, chord[max(0, i - 512):i + block])[-len(chord[i:i + block]):]
    s = np.tanh(1.5 * shaped / np.max(np.abs(shaped))) * env(n, 0.001, 0.12)
    place(buf, s, 1.5, 1.0)
    place(buf, s, 6.5, 0.85)
    place(buf, s, 7.25, 0.5)
    return sampler_grit(buf, rate=22050, bits=10, drive=1.8)


def vinyl():
    """Crackle, hiss and rumble. Independent L/R so it sits wide under everything."""
    total = n_beats(16)
    out = []
    for _ in range(2):
        hiss = filt(rng.standard_normal(total), "bandpass", [900, 9000]) * 0.03
        rumble = filt(rng.standard_normal(total), "lowpass", 35) * 0.25
        crackle = np.zeros(total)
        pops = rng.integers(0, total, size=int(total / SR * 38))
        crackle[pops] = rng.pareto(2.2, size=len(pops)) * rng.choice([-1, 1], size=len(pops))
        crackle = filt(crackle, "bandpass", [700, 7000]) * 0.9
        out.append(np.clip(hiss + rumble + crackle, -1, 1))
    return out


# ----------------------------------------------------------------------------- output
def write(path, left, right=None, peak=0.8):
    right = left if right is None else right
    data = np.stack([left, right], axis=1)
    data = data / max(1e-9, np.max(np.abs(data))) * peak
    ints = np.clip(np.round(data * (2 ** 23 - 1)), -(2 ** 23), 2 ** 23 - 1).astype("<i4")
    raw = ints.reshape(-1).view(np.uint8).reshape(-1, 4)[:, :3].tobytes()   # little-endian 24-bit
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(3)
        w.setframerate(SR)
        w.writeframes(raw)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "jungle-loops")
    out.mkdir(parents=True, exist_ok=True)
    write(out / "break_165bpm.wav", break_main(), peak=0.9)
    write(out / "break_chop_165bpm.wav", break_chop(), peak=0.9)
    write(out / "snare_roll_165bpm.wav", snare_roll(), peak=0.8)
    l, r = reese()
    write(out / "reese_165bpm.wav", l, r, peak=0.75)
    write(out / "sub_165bpm.wav", sub(), peak=0.85)
    l, r = pad()
    write(out / "dark_pad_165bpm.wav", l, r, peak=0.55)
    write(out / "dub_stab_165bpm.wav", stab(), peak=0.7)
    l, r = vinyl()
    write(out / "vinyl_165bpm.wav", l, r, peak=0.35)
    for f in sorted(out.glob("*.wav")):
        print(f"{f.name:28s} {f.stat().st_size:>9,d} bytes")


if __name__ == "__main__":
    main()
