#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include <cmath>
// The project model: a plain value type.
//
// Design intent:
//   * Copyable value semantics -> undo/redo is snapshot based and trivially correct.
//     The only heavy members (decoded audio) are shared, immutable AudioDataPtr, so a snapshot
//     costs a few vectors of small structs.
//   * Short deterministic IDs ("track-3", "event-17", "fx-2", "asset-1") from a per-project serial
//     counter. They are stable across save/load, readable in MCP transcripts, and make tests exact.
//   * All positions are integer ticks (see MusicalTime.h).
//
// The model knows nothing about GUI or audio devices.

#include "AudioAsset.h"
#include "EffectCatalogue.h"
#include "Fades.h"
#include "MusicalTime.h"
#include <optional>
#include <vector>

namespace mcpdaw
{
/** A decoded source file used by events. */
struct Asset
{
    juce::String id;
    juce::String path;            // absolute path at runtime; stored relative to project on save
    double sourceBpm = 0.0;       // 0 = not tempo-based (plays unstretched even when stretch arrives)
    juce::String tempoSource;     // where sourceBpm came from: acid-chunk / filename / user / ""
    AudioDataPtr data;            // shared, immutable

    juce::String displayName() const { return juce::File (path).getFileNameWithoutExtension(); }
    double durationSeconds() const noexcept { return data ? data->durationSeconds() : 0.0; }
    /** Musical length at its own tempo (0 if the tempo is unknown). */
    double sourceBeats() const noexcept { return sourceBpm > 0 ? secondsToBeats (durationSeconds(), sourceBpm) : 0.0; }
};

/**
    An audio event (clip) on a track.

    Timing model — deliberately explicit so time-stretching can slot in later:
      start/length          where the event sits on the project timeline (ticks)
      sourceOffsetSeconds   where playback begins inside the *source* (source-time seconds)
      looping               if true, reaching the end of the source wraps to its start (ACID loops)
      pitchSemitones        sampler-style (varispeed) transpose, -12..+12: the source plays at
                            rate() = 2^(n/12), so +12 is an octave up AND twice as fast (like an Akai /
                            MPC). sourceOffsetSeconds stays in source time; timeline time maps to
                            source time through rate().
      stretch               reserved: when the stretch subsystem lands, playback rate becomes
                            projectBpm / asset.sourceBpm for tempo-based assets. Currently plays at 1.0 (no stretching yet).

    Edges and direction:
      fadeInTicks/fadeOutTicks  musical edge fades (0 = none). The renderer never fades for less than the
                                application's Default declick time (see Fades.h); that minimum is NOT stored
                                here, so changing the preference changes every event's effective minimum.
      fadeInCurve/fadeOutCurve  independent curve per edge.
      reversed                  non-destructive reverse: the event plays the same window of source audio
                                it would play forwards, back to front ("reverse what you see"). The asset
                                is untouched; sourceOffsetSeconds is still the window's lowest source
                                position, which for a reversed event is heard at the event's END.
*/
struct Event
{
    juce::String id;
    juce::String assetId;
    Tick start = 0;
    Tick length = kTicksPerBar;
    double sourceOffsetSeconds = 0.0;
    bool looping = true;
    double gainDb = 0.0;
    double pitchSemitones = 0.0;
    Tick fadeInTicks = 0;
    Tick fadeOutTicks = 0;
    FadeCurve fadeInCurve = FadeCurve::Linear;
    FadeCurve fadeOutCurve = FadeCurve::Linear;
    bool reversed = false;

    Tick end() const noexcept { return start + length; }
    /** Source seconds consumed per timeline second (pitch as varispeed). */
    double rate() const noexcept { return std::pow (2.0, pitchSemitones / 12.0); }
    /** Keep each fade within the event (fades may overlap each other; they multiply). */
    void clampFades() noexcept
    {
        fadeInTicks = std::clamp<Tick> (fadeInTicks, 0, length);
        fadeOutTicks = std::clamp<Tick> (fadeOutTicks, 0, length);
    }
};

inline constexpr double kMaxPitchSemitones = 12.0;

/** One insert effect in a chain. `values` is index-aligned with the EffectDescriptor params. */
struct EffectInstance
{
    juce::String id;
    juce::String type;
    bool bypassed = false;
    std::vector<float> values;

    const EffectDescriptor* descriptor() const noexcept { return findEffectType (type); }
};

struct Track
{
    juce::String id;
    juce::String name;
    int colour = 0;               // palette index (UI)
    double gainDb = 0.0;
    double pan = 0.0;             // -1 (L) .. +1 (R)
    bool mute = false;
    bool solo = false;
    std::vector<Event> events;
    std::vector<EffectInstance> effects;
};

struct MasterBus
{
    double gainDb = 0.0;
    std::vector<EffectInstance> effects;
};

struct Project
{
    /** Project file format. v1 = MCP DAW 0.4.0. v2 adds event fades, fade curves and reverse.
        Older files load (new fields take their defaults); newer files are refused. */
    static constexpr int kFormatVersion = 2;
    static constexpr int kMaxTracks = 128;
    static constexpr int kMaxEventsPerTrack = 4096;
    static constexpr int kMaxEffectsPerChain = 16;

    juce::String name = "Untitled";
    double bpm = 120.0;
    Tick loopStart = 0;
    Tick loopEnd = 4 * kTicksPerBar;
    bool loopEnabled = false;
    std::vector<Asset> assets;
    std::vector<Track> tracks;
    MasterBus master;
    int nextSerial = 1;           // ID allocator; persisted so IDs never get reused

    // --- ID helpers ---
    juce::String allocateId (const char* prefix) { return juce::String (prefix) + "-" + juce::String (nextSerial++); }

    // --- lookup (nullptr when absent) ---
    const Asset* findAsset (const juce::String& id) const noexcept;
    Track* findTrack (const juce::String& id) noexcept;
    const Track* findTrack (const juce::String& id) const noexcept;
    int trackIndex (const juce::String& id) const noexcept;

    struct EventRef { Track* track; Event* event; };
    EventRef findEvent (const juce::String& id) noexcept;
    struct ConstEventRef { const Track* track; const Event* event; };
    ConstEventRef findEvent (const juce::String& id) const noexcept;

    /** Returns the chain containing effect `id` (track chain or master chain). */
    std::vector<EffectInstance>* findEffectChain (const juce::String& effectId) noexcept;
    EffectInstance* findEffect (const juce::String& effectId) noexcept;
    const EffectInstance* findEffect (const juce::String& effectId) const noexcept;

    /** End of the last event (ticks). */
    Tick contentEnd() const noexcept;
    bool anySolo() const noexcept;
};

/** Throws std::runtime_error describing the first invariant violation. */
void validateProject (const Project&);

/** Create a new effect instance with default parameter values. Throws for unknown types. */
EffectInstance makeEffect (Project&, const juce::String& type);

/** Length of one loop cycle of an event's source, in ticks at the current project tempo
    (playback is unstretched for now). Returns 0 if unknown. */
Tick sourceCycleTicks (const Project&, const Event&);
} // namespace mcpdaw
