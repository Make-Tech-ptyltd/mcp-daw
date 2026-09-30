#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// The mixing core shared by the live audio callback and the offline renderer.
// Deterministic: the same plan + position always produces bit-identical output, which is what
// makes engine behaviour testable without an audio device.

#include "PlaybackPlan.h"

namespace mcpdaw
{
class PlanRenderer
{
public:
    static constexpr int kMaxBlock = 1024;   // callers split larger device buffers into chunks

    PlanRenderer();

    struct Result
    {
        std::int64_t newPosition = 0;
        float masterPeakL = 0, masterPeakR = 0;
    };

    /** Render `numSamples` (<= kMaxBlock) into outL/outR (overwritten). `position` is the timeline
        sample at the first output sample. Loop wrapping is applied when plan.loopEnabled && playing. */
    Result render (const PlaybackPlan&, std::int64_t position, bool playing, float* outL, float* outR, int numSamples) noexcept;

private:
    juce::AudioBuffer<float> trackBuffer, masterBuffer;

    void renderEvents (const PlanChannel&, std::int64_t segmentStart, int segmentLength, int bufferOffset) noexcept;
};
} // namespace mcpdaw
