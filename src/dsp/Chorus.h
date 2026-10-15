#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "CycleClock.h"
#include <juce_dsp/juce_dsp.h>

namespace mcpdaw
{
/**
    Stereo chorus / flanger / vibrato.

    Same topology and controls as juce::dsp::Chorus (modulated delay line + feedback + linear wet/dry),
    but driven by CycleClock instead of juce::dsp::Oscillator. juce::dsp::Chorus keeps its LFO phase
    private and free-running from reset(), so it cannot follow the timeline, restart on events, or
    sound the same in a bounce as in playback; it also modulates both channels identically. The delay
    line itself is juce::dsp::DelayLine (3rd-order Lagrange interpolation).

    Per channel:  delay = centre x (1 + depth x lfo),  lfo = sin (2 pi phase), right channel offset by
    Spread. Depth 100% therefore sweeps from 0 to twice the centre delay and never needs clamping
    (apart from a 2-sample floor). Positive feedback adds the delayed signal back in.

    Phase convention: phase 0 = centre delay, rising (longer delay = pitch dips first in vibrato).
*/
class Chorus final : public EffectProcessor
{
public:
    explicit Chorus (const EffectDescriptor& d);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int numSamples, const ProcessContext&) noexcept override;

    static constexpr double kMaxDelayMs = 2.0 * 30.0 + 5.0;   // 2 x max centre + margin
    static constexpr double kLfoSlewMs = 10.0;                 // full -1..+1 swing; softens restarts/seeks only

private:
    CycleClock clock;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delay;
    juce::SmoothedValue<float> centreMs, depth, feedback, mix;
    double sampleRate = 48000.0;
    float lfoStep = 1.0f;
    float lfoL = 0.0f, lfoR = 0.0f, fbL = 0.0f, fbR = 0.0f;
    bool lfoValid = false;
};
} // namespace mcpdaw
