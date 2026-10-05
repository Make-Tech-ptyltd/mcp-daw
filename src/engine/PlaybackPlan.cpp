// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "PlaybackPlan.h"
#include <set>

namespace mcpdaw
{
RuntimeRegistry::RuntimeRegistry (double sr, int maxBlockSize) : rate (sr), maxBlock (maxBlockSize) {}

std::shared_ptr<TrackRuntime> RuntimeRegistry::track (const juce::String& id)
{
    auto& rt = tracks[id];
    if (rt == nullptr) rt = std::make_shared<TrackRuntime>();
    return rt;
}

std::shared_ptr<EffectRuntime> RuntimeRegistry::effect (const EffectInstance& fx)
{
    auto& rt = effects[fx.id];
    if (rt == nullptr || rt->type != fx.type)
    {
        auto created = std::make_shared<EffectRuntime>();
        created->type = fx.type;
        created->processor = createEffectProcessor (fx.type);
        if (created->processor == nullptr) return nullptr;
        created->processor->setAllParameters (fx.values);
        created->processor->prepare (rate, maxBlock);   // allocation happens here, off the audio thread
        rt = std::move (created);
    }
    else
    {
        rt->processor->setAllParameters (fx.values);    // lock-free atomics; processor may be live
    }
    return rt;
}

void RuntimeRegistry::retainOnly (const std::vector<juce::String>& liveIds)
{
    const std::set<juce::String> live (liveIds.begin(), liveIds.end());
    std::erase_if (tracks, [&] (const auto& kv) { return live.count (kv.first) == 0; });
    std::erase_if (effects, [&] (const auto& kv) { return live.count (kv.first) == 0; });
}

std::shared_ptr<TrackRuntime> RuntimeRegistry::findTrack (const juce::String& id) const
{
    auto it = tracks.find (id);
    return it == tracks.end() ? nullptr : it->second;
}

std::shared_ptr<EffectRuntime> RuntimeRegistry::findEffect (const juce::String& id) const
{
    auto it = effects.find (id);
    return it == effects.end() ? nullptr : it->second;
}

// ================================================================================================
namespace
{
void buildChain (PlaybackPlan& plan, PlanChannel& channel, const std::vector<EffectInstance>& chain,
                 RuntimeRegistry& registry, std::vector<juce::String>& liveIds)
{
    for (const auto& fx : chain)
    {
        liveIds.push_back (fx.id);
        auto rt = registry.effect (fx);
        if (rt == nullptr) continue;
        plan.effectRuntimes.push_back (rt);
        if (! fx.bypassed) channel.effects.push_back (rt->processor.get());
    }
}

/** Balance pan law: unity at centre, the far side attenuates. Right for stereo loops. */
void panGains (double gainDb, double pan, float& l, float& r)
{
    const double g = dbToGain (gainDb);
    l = static_cast<float> (g * std::min (1.0, 1.0 - pan));
    r = static_cast<float> (g * std::min (1.0, 1.0 + pan));
}
} // namespace

std::unique_ptr<PlaybackPlan> PlaybackPlan::build (const Project& project, RuntimeRegistry& registry, double declickMs)
{
    auto plan = std::make_unique<PlaybackPlan>();
    plan->sampleRate = registry.sampleRate();
    plan->bpm = project.bpm;
    plan->loopEnabled = project.loopEnabled;
    plan->loopStart = ticksToSamples (project.loopStart, project.bpm, plan->sampleRate);
    plan->loopEnd = ticksToSamples (project.loopEnd, project.bpm, plan->sampleRate);
    plan->contentEnd = ticksToSamples (project.contentEnd(), project.bpm, plan->sampleRate);

    std::vector<juce::String> liveIds;
    for (const auto& a : project.assets) plan->audio.push_back (a.data);

    const bool solo = project.anySolo();
    const std::int64_t declick = declickSamples (declickMs, plan->sampleRate);
    plan->tracks.reserve (project.tracks.size());
    for (const auto& track : project.tracks)
    {
        liveIds.push_back (track.id);
        auto rt = registry.track (track.id);
        plan->trackRuntimes.push_back (rt);

        PlanChannel ch;
        ch.runtime = rt.get();
        ch.audible = ! track.mute && (! solo || track.solo);
        panGains (track.gainDb, track.pan, ch.gainL, ch.gainR);

        for (const auto& e : track.events)
        {
            const auto* asset = project.findAsset (e.assetId);
            if (asset == nullptr || asset->data == nullptr || asset->data->numFrames() < 2) continue;
            const auto& data = *asset->data;

            PlanEvent pe;
            pe.data = &data;
            pe.start = ticksToSamples (e.start, project.bpm, plan->sampleRate);
            pe.end = ticksToSamples (e.end(), project.bpm, plan->sampleRate);
            if (pe.end <= pe.start) continue;
            const double step = data.sampleRate / plan->sampleRate * e.rate();   // rate conversion x pitch (varispeed)
            pe.looping = e.looping;
            pe.gain = static_cast<float> (dbToGain (e.gainDb));

            // Source window: forwards, output sample k plays offset + k*step. Reversed, the same window
            // is played back to front: sample k plays offset + (L-1-k)*step, i.e. it starts at the far
            // end and walks down (negative step). Sample-exact mirror of the forward rendering.
            const double frames = data.numFrames();
            const double offsetFrames = e.sourceOffsetSeconds * data.sampleRate;
            const auto lengthSamples = pe.end - pe.start;
            if (e.reversed)
            {
                pe.sourceStart = offsetFrames + static_cast<double> (lengthSamples - 1) * step;
                pe.step = -step;
            }
            else
            {
                pe.sourceStart = offsetFrames;
                pe.step = step;
            }
            if (pe.looping)
            {
                pe.sourceStart = std::fmod (pe.sourceStart, frames);
                if (pe.sourceStart < 0) pe.sourceStart += frames;
            }

            // Unified edge fades: the event's own fade or the Default declick time, whichever is longer,
            // never longer than the event. One envelope per edge - there is no separate declick.
            auto edge = [&] (Tick fadeTicks)
            {
                const auto fade = fadeTicks > 0 ? ticksToSamples (fadeTicks, project.bpm, plan->sampleRate) : 0;
                return std::min<std::int64_t> (effectiveFadeSamples (fade, declick), lengthSamples);
            };
            pe.fadeInSamples = edge (e.fadeInTicks);
            pe.fadeOutSamples = edge (e.fadeOutTicks);
            pe.fadeInCurve = e.fadeInCurve;
            pe.fadeOutCurve = e.fadeOutCurve;

            ch.events.push_back (pe);
        }
        std::sort (ch.events.begin(), ch.events.end(), [] (const PlanEvent& a, const PlanEvent& b) { return a.start < b.start; });
        buildChain (*plan, ch, track.effects, registry, liveIds);
        plan->tracks.push_back (std::move (ch));
    }

    plan->master.runtime = registry.master().get();
    plan->trackRuntimes.push_back (registry.master());
    panGains (project.master.gainDb, 0.0, plan->master.gainL, plan->master.gainR);
    buildChain (*plan, plan->master, project.master.effects, registry, liveIds);

    registry.retainOnly (liveIds);
    return plan;
}
} // namespace mcpdaw
