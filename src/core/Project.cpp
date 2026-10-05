// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Project.h"
#include <unordered_set>
#include <stdexcept>

namespace mcpdaw
{
const Asset* Project::findAsset (const juce::String& id) const noexcept
{
    for (const auto& a : assets)
        if (a.id == id) return &a;
    return nullptr;
}

Track* Project::findTrack (const juce::String& id) noexcept
{
    for (auto& t : tracks)
        if (t.id == id) return &t;
    return nullptr;
}

const Track* Project::findTrack (const juce::String& id) const noexcept
{
    return const_cast<Project*> (this)->findTrack (id);
}

int Project::trackIndex (const juce::String& id) const noexcept
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == id) return static_cast<int> (i);
    return -1;
}

Project::EventRef Project::findEvent (const juce::String& id) noexcept
{
    for (auto& t : tracks)
        for (auto& e : t.events)
            if (e.id == id) return { &t, &e };
    return { nullptr, nullptr };
}

Project::ConstEventRef Project::findEvent (const juce::String& id) const noexcept
{
    auto r = const_cast<Project*> (this)->findEvent (id);
    return { r.track, r.event };
}

std::vector<EffectInstance>* Project::findEffectChain (const juce::String& effectId) noexcept
{
    for (auto& fx : master.effects)
        if (fx.id == effectId) return &master.effects;
    for (auto& t : tracks)
        for (auto& fx : t.effects)
            if (fx.id == effectId) return &t.effects;
    return nullptr;
}

EffectInstance* Project::findEffect (const juce::String& effectId) noexcept
{
    if (auto* chain = findEffectChain (effectId))
        for (auto& fx : *chain)
            if (fx.id == effectId) return &fx;
    return nullptr;
}

const EffectInstance* Project::findEffect (const juce::String& effectId) const noexcept
{
    return const_cast<Project*> (this)->findEffect (effectId);
}

Tick Project::contentEnd() const noexcept
{
    Tick end = 0;
    for (const auto& t : tracks)
        for (const auto& e : t.events)
            end = std::max (end, e.end());
    return end;
}

bool Project::anySolo() const noexcept
{
    for (const auto& t : tracks)
        if (t.solo) return true;
    return false;
}

EffectInstance makeEffect (Project& p, const juce::String& type)
{
    const auto* d = findEffectType (type);
    if (d == nullptr)
    {
        juce::StringArray known;
        for (const auto& e : effectCatalogue()) known.add (e.type);
        throw std::runtime_error (("Unknown effect type '" + type + "'. Available: " + known.joinIntoString (", ")).toStdString());
    }
    EffectInstance fx;
    fx.id = p.allocateId ("fx");
    fx.type = type;
    fx.values = d->defaults();
    return fx;
}

Tick sourceCycleTicks (const Project& p, const Event& e)
{
    const auto* a = p.findAsset (e.assetId);
    if (a == nullptr || a->durationSeconds() <= 0) return 0;
    // Unstretched: the source plays at its (pitch) rate, so its musical length depends on tempo and pitch.
    return beatsToTicks (secondsToBeats (a->durationSeconds() / e.rate(), p.bpm));
}

// ================================================================================================
// Validation — every command result passes through here before it is committed, so an MCP client
// can never leave the project in an inconsistent state.
// ================================================================================================
namespace
{
struct IdHash { size_t operator() (const juce::String& s) const noexcept { return static_cast<size_t> (s.hash()); } };
using IdSet = std::unordered_set<juce::String, IdHash>;

[[noreturn]] void invalid (const juce::String& message) { throw std::runtime_error (message.toStdString()); }

// Validation runs after every edit, over every event: build the message only when a check fails.
#define require(ok, message) do { if (! (ok)) invalid (message); } while (false)

bool finiteIn (double v, double lo, double hi) { return std::isfinite (v) && v >= lo && v <= hi; }

void validateChain (const std::vector<EffectInstance>& chain, IdSet& ids, const juce::String& owner)
{
    require (chain.size() <= Project::kMaxEffectsPerChain, owner + ": too many effects (max " + juce::String (Project::kMaxEffectsPerChain) + ")");
    for (const auto& fx : chain)
    {
        require (fx.id.isNotEmpty() && ids.insert (fx.id).second, "Duplicate or empty effect id '" + fx.id + "'");
        const auto* d = fx.descriptor();
        require (d != nullptr, "Unknown effect type '" + fx.type + "'");
        require (fx.values.size() == d->params.size(), "Effect " + fx.id + " has wrong parameter count");
        for (size_t i = 0; i < fx.values.size(); ++i)
            require (std::isfinite (fx.values[i]) && fx.values[i] >= d->params[i].min && fx.values[i] <= d->params[i].max,
                     "Effect " + fx.id + " parameter " + d->params[i].id + " out of range");
    }
}
} // namespace

void validateProject (const Project& p)
{
    require (finiteIn (p.bpm, kMinBpm, kMaxBpm), "Tempo must be between " + juce::String (kMinBpm) + " and " + juce::String (kMaxBpm) + " BPM");
    require (p.loopStart >= 0 && p.loopEnd > p.loopStart && p.loopEnd <= kMaxProjectTicks, "Loop region must have start >= 0 and end > start");
    require (finiteIn (p.master.gainDb, kMinDb, kMaxDb), "Master gain out of range");
    require (p.tracks.size() <= Project::kMaxTracks, "Too many tracks (max " + juce::String (Project::kMaxTracks) + ")");
    require (p.nextSerial > 0, "Invalid ID counter");

    IdSet ids;
    for (const auto& a : p.assets)
    {
        require (a.id.isNotEmpty() && ids.insert (a.id).second, "Duplicate or empty asset id '" + a.id + "'");
        require (a.data != nullptr, "Asset " + a.id + " has no audio data");
        require (a.sourceBpm == 0.0 || finiteIn (a.sourceBpm, kMinBpm, kMaxBpm), "Asset " + a.id + " has invalid source BPM");
    }

    validateChain (p.master.effects, ids, "Master");
    for (const auto& t : p.tracks)
    {
        require (t.id.isNotEmpty() && ids.insert (t.id).second, "Duplicate or empty track id '" + t.id + "'");
        require (t.name.isNotEmpty() && t.name.length() <= 128, "Track name must be 1..128 characters");
        require (finiteIn (t.gainDb, kMinDb, kMaxDb), "Track " + t.id + " gain out of range (" + juce::String (kMinDb) + ".." + juce::String (kMaxDb) + " dB)");
        require (finiteIn (t.pan, -1.0, 1.0), "Track " + t.id + " pan must be -1..1");
        require (t.events.size() <= Project::kMaxEventsPerTrack, "Track " + t.id + " has too many events");
        for (const auto& e : t.events)
        {
            require (e.id.isNotEmpty() && ids.insert (e.id).second, "Duplicate or empty event id '" + e.id + "'");
            require (p.findAsset (e.assetId) != nullptr, "Event " + e.id + " references missing asset '" + e.assetId + "'");
            require (e.start >= 0 && e.length > 0 && e.end() <= kMaxProjectTicks, "Event " + e.id + " must have start >= 0 and length > 0");
            require (finiteIn (e.sourceOffsetSeconds, 0.0, 1.0e6), "Event " + e.id + " source offset must be >= 0");
            require (finiteIn (e.gainDb, kMinDb, kMaxDb), "Event " + e.id + " gain out of range");
            require (finiteIn (e.pitchSemitones, -kMaxPitchSemitones, kMaxPitchSemitones), "Event " + e.id + " pitch must be -12..+12 semitones");
            require (e.fadeInTicks >= 0 && e.fadeInTicks <= e.length && e.fadeOutTicks >= 0 && e.fadeOutTicks <= e.length,
                     "Event " + e.id + " fades must be between 0 and the event length");
        }
        validateChain (t.effects, ids, "Track " + t.id);
    }
}
} // namespace mcpdaw
#undef require
