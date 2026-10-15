#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "CycleClock.h"
#include <juce_dsp/juce_dsp.h>

namespace mcpdaw
{
/**
    Six-stage stereo phaser.

    The algorithm of juce::dsp::Phaser - six first-order TPT all-pass stages sharing one cutoff,
    feedback around the cascade, linear wet/dry, and the same mapping of depth and centre onto a
    log-frequency axis (20 Hz - 20 kHz):
        norm = clamp (log1000 (centre / 20) + 0.5 x depth x lfo, 0, 1),   cutoff = 20 x 1000^norm
    - but driven by CycleClock rather than juce::dsp::Oscillator (whose phase is private and
    free-running), so the sweep can follow the timeline or restart on each event, and bounces match
    playback. The cutoff is updated every sample (JUCE: every 4th) so the result does not depend on
    where block boundaries fall. The right channel's LFO is offset by Spread.

    Phase convention: phase 0 = centre, sweeping up; 90 deg = top; 270 deg = bottom.
*/
class Phaser final : public EffectProcessor
{
public:
    explicit Phaser (const EffectDescriptor& d);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int numSamples, const ProcessContext&) noexcept override;

    static constexpr int kStages = 6;
    static constexpr double kLfoSlewMs = 10.0;

    /** All-pass cutoff (Hz) for a centre frequency, depth (0..1) and LFO value (-1..1), before the Nyquist limit. */
    static double cutoffHz (double centreHz, double depth, double lfo) noexcept;

private:
    CycleClock clock;
    juce::SmoothedValue<float> normCentre, depth, feedback, mix;
    double sampleRate = 48000.0;
    float lfoStep = 1.0f;
    float lfoL = 0.0f, lfoR = 0.0f, fbL = 0.0f, fbR = 0.0f;
    bool lfoValid = false;
    float stateL[kStages] {}, stateR[kStages] {};

    float cascade (float x, float G, float* state) const noexcept;
};
} // namespace mcpdaw
