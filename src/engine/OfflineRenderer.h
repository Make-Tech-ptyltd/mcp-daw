#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Offline (faster-than-realtime) rendering using the same PlanRenderer as live playback, with its
// own fresh effect runtimes, so a bounce is deterministic and independent of the live session.

#include "../core/Actions.h"
#include "PlaybackPlan.h"

namespace mcpdaw
{
struct RenderOptions
{
    double sampleRate = 48000.0;
    Tick start = 0;
    Tick end = 0;                 // 0 = end of content
    double tailSeconds = 2.0;     // extra time for reverb/delay tails
    int bitDepth = 24;            // 16, 24 or 32 (float)
};

struct RenderStats
{
    std::int64_t frames = 0;
    double seconds = 0;
    float peak = 0;               // linear
    bool clipped = false;
};

/** Render to memory (tests, analysis). */
juce::AudioBuffer<float> renderToBuffer (const Project&, const RenderOptions&, RenderStats* stats = nullptr);

/** Render to a WAV file. Throws on failure. */
RenderStats renderToWavFile (const Project&, const RenderOptions&, const juce::File&);

/** Registers render_mix (needs the engine layer, so it is not in core). */
void registerEngineActions (ActionRegistry&);
} // namespace mcpdaw
