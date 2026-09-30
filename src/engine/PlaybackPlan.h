#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// A PlaybackPlan is an immutable, render-ready compilation of a Project snapshot at one sample
// rate: event positions in samples, gains as linear factors, effect chains as raw processor
// pointers. The message thread builds it; the audio thread only reads it.
//
// Stateful realtime objects (effect processors, smoothed track gains, meters) live in *runtimes*
// that persist across plans, keyed by track/effect id. Rebuilding the plan when a fader moves
// therefore never resets a reverb tail or a delay line.

#include "../core/Project.h"
#include "../dsp/EffectProcessor.h"
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace mcpdaw
{
struct TrackRuntime
{
    std::atomic<float> peakL { 0 }, peakR { 0 };   // max |x| since the UI last read (exchange(0))
    float currentGainL = 0, currentGainR = 0;      // audio-thread smoothing state
    bool gainInitialised = false;
};

struct EffectRuntime
{
    juce::String type;
    std::unique_ptr<EffectProcessor> processor;
};

/** Owns runtimes for one sample rate. Control thread only. */
class RuntimeRegistry
{
public:
    explicit RuntimeRegistry (double sampleRate, int maxBlockSize);

    double sampleRate() const noexcept { return rate; }
    std::shared_ptr<TrackRuntime> track (const juce::String& id);
    std::shared_ptr<TrackRuntime> master() { return masterRuntime; }
    /** Gets (or creates and prepares) the processor for an instance and pushes its parameter values. */
    std::shared_ptr<EffectRuntime> effect (const EffectInstance&);
    /** Forget runtimes for ids not in `live` (plans still holding them keep them alive). */
    void retainOnly (const std::vector<juce::String>& liveIds);

    std::shared_ptr<TrackRuntime> findTrack (const juce::String& id) const;
    std::shared_ptr<EffectRuntime> findEffect (const juce::String& id) const;

private:
    double rate;
    int maxBlock;
    std::shared_ptr<TrackRuntime> masterRuntime = std::make_shared<TrackRuntime>();
    std::map<juce::String, std::shared_ptr<TrackRuntime>> tracks;
    std::map<juce::String, std::shared_ptr<EffectRuntime>> effects;
};

struct PlanEvent
{
    const AudioData* data = nullptr;
    std::int64_t start = 0, end = 0;   // timeline samples [start, end)
    double sourceStart = 0;            // source frame at `start`
    double step = 1;                   // source frames per output sample (rate conversion x pitch; stretch later)
    bool looping = true;
    float gain = 1;
    bool fadeIn = false, fadeOut = false;
};

struct PlanChannel
{
    TrackRuntime* runtime = nullptr;
    float gainL = 1, gainR = 1;
    bool audible = true;
    std::vector<PlanEvent> events;           // sorted by start
    std::vector<EffectProcessor*> effects;   // active (non-bypassed) processors in order
};

struct PlaybackPlan
{
    double sampleRate = 48000, bpm = 120;
    std::int64_t loopStart = 0, loopEnd = 0;
    bool loopEnabled = false;
    std::int64_t contentEnd = 0;
    std::vector<PlanChannel> tracks;
    PlanChannel master;

    // Ownership (released on the control thread when the plan is retired).
    std::vector<AudioDataPtr> audio;
    std::vector<std::shared_ptr<TrackRuntime>> trackRuntimes;
    std::vector<std::shared_ptr<EffectRuntime>> effectRuntimes;

    std::int64_t beatsToSamples (double beats) const noexcept
    {
        return static_cast<std::int64_t> (std::llround (beatsToSeconds (beats, bpm) * sampleRate));
    }
    double samplesToBeats (std::int64_t s) const noexcept { return mcpdaw::samplesToBeats (s, bpm, sampleRate); }

    static std::unique_ptr<PlaybackPlan> build (const Project&, RuntimeRegistry&);
};
} // namespace mcpdaw
