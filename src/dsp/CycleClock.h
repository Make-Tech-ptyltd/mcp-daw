#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// The phase source shared by the cycling effects (tremolo, chorus, phaser).
//
// While the transport plays, the phase (0..1, one unit = one cycle) of timeline sample t is
//
//     phase (t) = frac ((t - anchor) / cycleSamples + phaseOffset)
//
//   anchor = 0 (the project start, beat 0) in Timeline mode, so cycles line up with the bar grid;
//          = the latest event onset <= t in Event mode (0 before the first onset). The master bus
//            has no onsets, so Event mode there behaves as Timeline.
//
// It is a pure function of the timeline position: block size, block boundaries, seeking, loop
// wraps, starting playback in the middle of an event and offline rendering all give the same phase
// for the same timeline sample. Nothing is accumulated across blocks, so nothing drifts.
//
// While stopped there is no timeline; the phase free-runs on from where it was, so reverb/delay
// tails running through the effect keep moving instead of freezing. The effects smooth the jump
// back onto the timeline when playback starts (and at restarts, seeks and loop wraps).

#include "EffectProcessor.h"
#include "../core/MusicalTime.h"
#include <algorithm>
#include <cmath>

namespace mcpdaw
{
class CycleClock
{
public:
    void reset() noexcept { freeCycles = 0.0; }

    /** Calls fn (bufferIndex, phase) for each of the block's samples, phase in [0, 1).
        Audio thread; no allocation. */
    template <typename Fn>
    void run (const ProcessContext& ctx, int numSamples, double cycleSamples, double phaseOffset, bool eventRestart, Fn&& fn) noexcept
    {
        const double inc = 1.0 / std::max (1.0, cycleSamples);
        if (ctx.numSpans <= 0 || ctx.spans == nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                freeCycles -= std::floor (freeCycles);
                double ph = freeCycles + phaseOffset;
                fn (i, ph - std::floor (ph));
                freeCycles += inc;
            }
            return;
        }

        const bool useOnsets = eventRestart && ctx.onsets != nullptr && ctx.numOnsets > 0;
        for (int s = 0; s < ctx.numSpans; ++s)
        {
            const auto& span = ctx.spans[s];
            // Latest onset at or before the span's first sample, then walk forwards sample by sample.
            int k = -1;
            if (useOnsets)
                k = static_cast<int> (std::upper_bound (ctx.onsets, ctx.onsets + ctx.numOnsets, span.timelineStart) - ctx.onsets) - 1;
            for (int i = 0; i < span.length; ++i)
            {
                const std::int64_t t = span.timelineStart + i;
                if (useOnsets)
                    while (k + 1 < ctx.numOnsets && ctx.onsets[k + 1] <= t) ++k;
                const std::int64_t anchor = k >= 0 ? ctx.onsets[k] : 0;
                double ph = static_cast<double> (t - anchor) * inc + phaseOffset;
                ph -= std::floor (ph);
                fn (span.bufferOffset + i, ph);
                freeCycles = ph - phaseOffset + inc;   // where a stop would continue from
            }
        }
    }

private:
    double freeCycles = 0.0;   // free-running position in cycles, without the phase offset
};

/** Cycle length in samples for the chorus/phaser "sync / rate / division" parameters. */
inline double sweepCycleSamples (bool sync, float rateHz, float divisionIndex, double bpm, double sampleRate) noexcept
{
    if (! sync) return sampleRate / std::max (0.001, static_cast<double> (rateHz));
    const auto& divisions = modulation::sweepDivisions();
    const auto i = static_cast<size_t> (juce::jlimit (0, static_cast<int> (divisions.size()) - 1, juce::roundToInt (divisionIndex)));
    return beatsToSeconds (divisions[i].second, bpm) * sampleRate;
}

/** Moves `state` towards `target` by at most `maxStep` (lands exactly on the target). */
inline float slewTowards (float state, float target, float maxStep) noexcept
{
    const float diff = target - state;
    if (diff > maxStep) return state + maxStep;
    if (diff < -maxStep) return state - maxStep;
    return target;
}
} // namespace mcpdaw
