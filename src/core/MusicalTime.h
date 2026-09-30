#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Musical time primitives.
//
// Positions inside the project are stored as integer ticks (960 per quarter-note beat). Integer
// ticks make editing deterministic: moving, splitting and duplicating never accumulates floating
// point error, and equality comparisons in tests/MCP are exact. Seconds and samples are *derived*
// from ticks at the current tempo and sample rate and are never stored.
//
// Public APIs (MCP, GUI) speak in zero-based beats as doubles; bars are 1-based for display only.

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace mcpdaw
{
using Tick = long long;   // same type as juce::int64 on every platform (juce::var friendly)

inline constexpr Tick   kTicksPerBeat = 960;
inline constexpr int    kBeatsPerBar  = 4;          // 4/4 only (for now)
inline constexpr Tick   kTicksPerBar  = kTicksPerBeat * kBeatsPerBar;
inline constexpr double kMinBpm = 20.0, kMaxBpm = 300.0;
inline constexpr Tick   kMaxProjectTicks = kTicksPerBeat * 100000; // ~6.9 h at 60 BPM; sanity bound

// --- conversions --------------------------------------------------------------------------------
inline Tick   beatsToTicks (double beats) noexcept { return static_cast<Tick> (std::llround (beats * kTicksPerBeat)); }
inline double ticksToBeats (Tick t) noexcept       { return static_cast<double> (t) / kTicksPerBeat; }
inline double beatsToSeconds (double beats, double bpm) noexcept { return beats * 60.0 / bpm; }
inline double secondsToBeats (double seconds, double bpm) noexcept { return seconds * bpm / 60.0; }
inline double ticksToSeconds (Tick t, double bpm) noexcept { return beatsToSeconds (ticksToBeats (t), bpm); }

/** Sample index (at sampleRate) at which a tick position begins. Rounded to nearest sample. */
inline std::int64_t ticksToSamples (Tick t, double bpm, double sampleRate) noexcept
{
    return static_cast<std::int64_t> (std::llround (ticksToSeconds (t, bpm) * sampleRate));
}

inline double samplesToBeats (std::int64_t s, double bpm, double sampleRate) noexcept
{
    return secondsToBeats (static_cast<double> (s) / sampleRate, bpm);
}

// --- snapping -----------------------------------------------------------------------------------
/** Snap grid expressed in ticks; 0 means snapping disabled. */
enum class SnapGrid : int { Off = 0, Bar = 1, Beat = 2, Eighth = 3, Sixteenth = 4, ThirtySecond = 5 };

inline constexpr Tick gridTicks (SnapGrid g) noexcept
{
    switch (g)
    {
        case SnapGrid::Bar:          return kTicksPerBar;
        case SnapGrid::Beat:         return kTicksPerBeat;
        case SnapGrid::Eighth:       return kTicksPerBeat / 2;
        case SnapGrid::Sixteenth:    return kTicksPerBeat / 4;
        case SnapGrid::ThirtySecond: return kTicksPerBeat / 8;
        case SnapGrid::Off:          break;
    }
    return 0;
}

/** Round to the nearest grid line (ties round away from zero, matching std::llround). */
inline Tick snapTicks (Tick t, Tick grid) noexcept
{
    if (grid <= 0) return t;
    const auto q = static_cast<double> (t) / static_cast<double> (grid);
    return static_cast<Tick> (std::llround (q)) * grid;
}

inline Tick snapTicksDown (Tick t, Tick grid) noexcept
{
    if (grid <= 0) return t;
    const Tick q = t >= 0 ? t / grid : -((-t + grid - 1) / grid);
    return q * grid;
}

// --- display ------------------------------------------------------------------------------------
struct BarBeatTick { int bar; int beat; int tick; }; // bar & beat 1-based

inline BarBeatTick toBarBeatTick (Tick t) noexcept
{
    t = std::max<Tick> (0, t);
    return { static_cast<int> (t / kTicksPerBar) + 1,
             static_cast<int> ((t % kTicksPerBar) / kTicksPerBeat) + 1,
             static_cast<int> (t % kTicksPerBeat) };
}

// --- decibels -----------------------------------------------------------------------------------
inline constexpr double kMinDb = -70.0;  // treated as silence (-inf)
inline constexpr double kMaxDb = 12.0;

inline double dbToGain (double db) noexcept { return db <= kMinDb ? 0.0 : std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) noexcept  { return g <= 0.0 ? kMinDb : std::max (kMinDb, 20.0 * std::log10 (g)); }
} // namespace mcpdaw
