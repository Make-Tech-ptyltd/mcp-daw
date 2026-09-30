#!/usr/bin/env python3
"""Generate original, royalty-free demo loops for MCP DAW testing (90 BPM, 48 kHz, stereo, 24-bit).

Every sound is synthesised here from sine waves and noise, so the files carry no third-party rights.
Tempo is written into the filename ("_90bpm") which MCP DAW reads as the loop's source tempo.

Usage:  python make_demo_loops.py <output folder>
"""
import sys
import wave
from pathlib import Path

import numpy as np

SR = 48000
BPM = 90.0
BEAT = 60.0 / BPM
rng = np.random.default_rng(90)


def bars(n):
    return int(round(n * 4 * BEAT * SR))


def env(length, attack=0.002, decay=0.2):
    t = np.arange(length) / SR
    a = np.clip(t / attack, 0, 1)
    return a * np.exp(-t / decay)


def place(buf, sound, beat, gain=1.0):
    start = int(round(beat * BEAT * SR))
    end = min(len(buf), start + len(sound))
    buf[start:end] += gain * sound[: end - start]


def kick():
    n = int(0.45 * SR)
    t = np.arange(n) / SR
    freq = 45 + 90 * np.exp(-t / 0.035)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    body = np.sin(phase) * env(n, 0.001, 0.22)
    click = rng.standard_normal(n) * env(n, 0.0005, 0.004) * 0.3
    return np.tanh(1.8 * (body + click))


def snare():
    n = int(0.3 * SR)
    t = np.arange(n) / SR
    tone = np.sin(2 * np.pi * 190 * t) * env(n, 0.001, 0.06)
    noise = rng.standard_normal(n)
    noise = np.convolve(noise, [1, -0.85], mode="same") * env(n, 0.001, 0.11)   # crude high-pass
    return 0.55 * tone + 0.45 * noise


def hat(open_=False):
    n = int((0.25 if open_ else 0.06) * SR)
    noise = rng.standard_normal(n)
    noise = np.convolve(noise, [1, -1.6, 0.8], mode="same")
    return 0.25 * noise * env(n, 0.0005, 0.12 if open_ else 0.018)


def drums_stem(which):
    buf = np.zeros(bars(1))
    if which == "kick":
        for b, g in [(0, 1.0), (1.75, 0.8), (2.5, 0.95)]:
            place(buf, kick(), b, g)
    elif which == "snare":
        for b in (1, 3):
            place(buf, snare(), b, 0.9)
        place(buf, snare(), 3.75, 0.25)       # ghost note
    elif which == "hats":
        for i in range(8):
            swing = 0.06 if i % 2 else 0.0      # lazy 8th swing
            place(buf, hat(i == 7), i * 0.5 + swing, 0.9 if i % 2 == 0 else 0.6)
    return buf


def bass():
    buf = np.zeros(bars(2))
    notes = [(0, 1.5, 41.20), (1.75, 0.75, 41.20), (2.5, 1.5, 49.00), (4, 1.5, 55.00), (5.75, 0.75, 49.00), (6.5, 1.5, 36.71)]
    for beat, dur, f in notes:
        n = int(dur * BEAT * SR)
        t = np.arange(n) / SR
        tone = np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t)
        e = env(n, 0.004, dur * BEAT * 0.9) * np.clip((n - np.arange(n)) / (0.01 * SR), 0, 1)
        place(buf, np.tanh(1.5 * tone) * e, beat, 0.8)
    return buf


def keys():
    buf_l, buf_r = np.zeros(bars(2)), np.zeros(bars(2))
    chords = [(0, [164.81, 196.00, 246.94, 293.66]), (2.5, [146.83, 174.61, 220.00, 261.63]),
              (4, [130.81, 164.81, 196.00, 246.94]), (6.5, [123.47, 146.83, 185.00, 220.00])]
    for beat, freqs in chords:
        n = int(1.2 * SR)
        t = np.arange(n) / SR
        stab = sum(np.sin(2 * np.pi * f * t + 0.4 * np.sin(2 * np.pi * f * 2 * t) * np.exp(-t / 0.3)) for f in freqs) / len(freqs)
        stab *= env(n, 0.003, 0.45)
        trem = 0.5 + 0.5 * np.sin(2 * np.pi * 4.5 * t)
        place(buf_l, stab * (0.6 + 0.4 * trem), beat, 0.5)
        place(buf_r, stab * (0.6 + 0.4 * (1 - trem)), beat, 0.5)
    return buf_l, buf_r


def vox():
    """A synthetic 'ah' chop: sawtooth through three formant resonators."""
    buf = np.zeros(bars(1))
    for beat, f0 in [(0.5, 220.0), (1.5, 246.94), (2.0, 220.0), (3.0, 196.0)]:
        n = int(0.45 * SR)
        t = np.arange(n) / SR
        saw = 2 * ((f0 * t * (1 + 0.004 * np.sin(2 * np.pi * 5 * t))) % 1.0) - 1
        out = np.zeros(n)
        for fc, bw, g in [(700, 90, 1.0), (1220, 110, 0.5), (2600, 160, 0.25)]:
            r = np.exp(-np.pi * bw / SR)
            a1, a2 = -2 * r * np.cos(2 * np.pi * fc / SR), r * r
            y1 = y2 = 0.0
            y = np.empty(n)
            for i, x in enumerate(saw):
                v = (1 - r) * x - a1 * y1 - a2 * y2
                y2, y1 = y1, v
                y[i] = v
            out += g * y
        out *= env(n, 0.02, 0.25)
        place(buf, out / np.max(np.abs(out)) * 0.7, beat)
    return buf


def write(path, left, right=None, peak=0.8):
    right = left if right is None else right
    data = np.stack([left, right], axis=1)
    data = data / max(1e-9, np.max(np.abs(data))) * peak
    ints = np.clip(np.round(data * (2 ** 23 - 1)), -(2 ** 23), 2 ** 23 - 1).astype(np.int32)
    raw = bytearray()
    for sample in ints.reshape(-1):
        raw += int(sample).to_bytes(4, "little", signed=True)[:3]
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(3)
        w.setframerate(SR)
        w.writeframes(bytes(raw))


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "demo-loops")
    out.mkdir(parents=True, exist_ok=True)
    write(out / "kick_90bpm.wav", drums_stem("kick"), peak=0.9)
    write(out / "snare_90bpm.wav", drums_stem("snare"), peak=0.75)
    write(out / "hats_90bpm.wav", drums_stem("hats"), peak=0.5)
    write(out / "bass_90bpm.wav", bass(), peak=0.8)
    l, r = keys()
    write(out / "keys_90bpm.wav", l, r, peak=0.6)
    write(out / "vox_chop_90bpm.wav", vox(), peak=0.6)
    for f in sorted(out.glob("*.wav")):
        print(f.name, f.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
