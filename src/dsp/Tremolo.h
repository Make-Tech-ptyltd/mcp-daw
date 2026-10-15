#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "CycleClock.h"

namespace mcpdaw
{
/**
    Tempo-synced tremolo (amplitude cycling).

    Conventions (also what the FX rack draws):
      * One full cycle per division: 1/4 = one cycle per quarter-note beat, 1/64 = sixteen per beat.
      * waveform (shape, phase) runs from 1 (loudest) to 0 (quietest). Phase 0 is the loudest point
        for every shape and phase 0.5 (180 deg) the quietest:
            sine      0.5 + 0.5 cos (2 pi phase)
            triangle  |1 - 2 phase|
            square    1 for phase < 0.5, else 0
      * gain = minGain + (1 - minGain) * waveform, minGain = 10^(-depth/20): linear gain from the
        depth-derived minimum up to unity. Depth 0 dB is exact unity (no modulation).
      * The Phase parameter is the starting point: 0 = the cycle starts loud, 180 = starts silent and
        swells, 90 = halfway down. See CycleClock for Timeline/Event restart.

    Smoothing: the gain is slew-limited to a full 0..1 swing per kEdgeMs. Sines and triangles are far
    slower than that at every division and tempo, so only square edges, restarts, seeks, loop wraps and
    parameter jumps are softened - into short ramps, keeping the rhythmic edge.
*/
class Tremolo final : public EffectProcessor
{
public:
    explicit Tremolo (const EffectDescriptor& d);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int numSamples, const ProcessContext&) noexcept override;

    static constexpr double kEdgeMs = 1.5;

    /** 1 = loudest, 0 = quietest. `phase` in cycles (any real; only the fraction matters). */
    static float waveform (modulation::Shape, double phase) noexcept;
    /** Unsmoothed linear gain at a phase. */
    static float gainAt (modulation::Shape, float depthDb, double phase) noexcept;

private:
    CycleClock clock;
    double sampleRate = 48000.0;
    float maxStep = 1.0f;     // per sample
    float gain = 1.0f;        // current (smoothed) gain
    bool gainValid = false;   // false until the first sample after prepare/reset: start on target
};
} // namespace mcpdaw
