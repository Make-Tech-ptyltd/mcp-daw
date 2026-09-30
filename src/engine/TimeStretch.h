#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Time-stretch / pitch-shift abstraction (not implemented yet — playback is unstretched).
//
// Architectural decision: stretching is an *offline preparation* step, not a realtime one.
// When an event needs a different rate (projectBpm / asset.sourceBpm) or pitch, a background
// worker renders a stretched rendition of the source with a TimeStretcher and publishes it as an
// ordinary immutable AudioData. The realtime engine then plays renditions exactly like plain
// files. Consequences:
//   * the audio callback stays trivially realtime-safe whatever algorithm is used;
//   * algorithms are swappable (and comparable in tests) behind this interface;
//   * tempo changes cost a short background render per affected asset (cached by ratio).
//
// Candidate libraries evaluated (licence is the deciding factor for a commercial product):
//   * Signalsmith Stretch  - MIT, header-only C++, good polyphonic quality.     <- recommended
//   * Rubber Band Library  - GPLv2+ or paid commercial licence; excellent quality.
//   * SoundTouch           - LGPL 2.1; fine for speech/simple material, weaker on drums.
//   * Bungee               - MPL-2.0; modern, good transient handling.
//   * zplane elastique     - commercial SDK, industry reference (what DAWs license).

#include "../core/AudioAsset.h"

namespace mcpdaw
{
struct StretchRequest
{
    AudioDataPtr source;
    double timeRatio = 1.0;        // output duration / input duration
    double pitchSemitones = 0.0;
    double outputSampleRate = 0.0; // 0 = keep source rate
};

class TimeStretcher
{
public:
    virtual ~TimeStretcher() = default;
    virtual juce::String name() const = 0;
    /** Runs on a worker thread. May allocate. Returns a new immutable rendition. */
    virtual AudioDataPtr render (const StretchRequest&) = 0;
};
} // namespace mcpdaw
