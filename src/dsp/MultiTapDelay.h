#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "EffectProcessor.h"
#include <array>
#include <vector>

namespace mcpdaw
{
/**
    Eight-tap stereo delay.

    One shared delay line; each tap reads it at its own (tempo-synced or free) time, is mono-summed,
    panned (constant power) and scaled by its level into the wet bus. Each tap also sends its signal
    back into the line by its own feedback amount, through a shared tone-shaping path
    (high-pass -> low-pass -> soft saturation). Because every tap hears every other tap's repeats,
    a few taps with small feedback produce dense, evolving rhythmic patterns. The saturator also
    bounds the loop, so no parameter setting can make the delay run away.
*/
class MultiTapDelay final : public EffectProcessor
{
public:
    explicit MultiTapDelay (const EffectDescriptor& d);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int numSamples, const ProcessContext&) noexcept override;

    /** Delay time in seconds for a tap given parameter values and tempo (used by the UI and tests). */
    static double tapSeconds (const std::vector<float>& values, int tap, double bpm);

private:
    struct Tap
    {
        bool enabled = false;
        double delaySamples = 0, targetDelay = 0;
        float level = 0, targetLevel = 0;
        float gainL = 0.707f, gainR = 0.707f;
        float feedback = 0;
    };

    std::array<Tap, 8> taps;
    std::vector<float> bufferL, bufferR;
    int mask = 0, writePos = 0;
    double sampleRate = 48000;
    float lowCutState = 0, highCutState = 0, lowCutCoeff = 0, highCutCoeff = 1, drive = 1;
    float wetGain = 0, dryGain = 1;
    bool firstBlock = true;

    void readParameters (double bpm) noexcept;
};
} // namespace mcpdaw
