#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// JSON representations of the project.
//
//  * File format ("mcp-daw-project", version Project::kFormatVersion): human-readable, diff-friendly,
//    versioned. Asset paths are stored relative to the project file when possible. Older versions load
//    with defaults for fields they lack (the caller is told which version was read, so the UI can offer
//    to save an upgraded copy); newer versions are refused.
//  * State format: what MCP get_project_state returns. Same core fields plus derived, *semantic*
//    information (bar/beat positions, source lengths, loop repetitions, occupied bars, effect
//    parameters by name) so an agent never needs to infer anything from pixels.

#include "Project.h"
#include <functional>

namespace mcpdaw
{
using AssetLoader = std::function<AudioDataPtr (const juce::File&)>;

juce::var projectToFileJson (const Project&, const juce::File& projectDirectory);

/** Parses and validates; loads audio through `loader` (defaults to loadAudioFile).
    Throws std::runtime_error with a specific message on any problem (nothing partially applied). */
Project projectFromFileJson (const juce::var& json, const juce::File& projectDirectory, const AssetLoader& loader = {},
                             int* formatVersionRead = nullptr);

// --- state (MCP / inspection) --------------------------------------------------------------------
// `declickMs` >= 0 adds the effective (rendered) fade durations to each event, computed with that
// Default declick time; < 0 leaves them out.
// `compactEffects` lists each effect as effectCompactJson (identity + non-default values) instead of
// every parameter value.
juce::var projectStateJson (const Project&, double declickMs = -1.0, bool compactEffects = false);
juce::var trackJson (const Project&, const Track&, double declickMs = -1.0, bool compactEffects = false);
juce::var eventJson (const Project&, const Event&, double declickMs = -1.0);
juce::var assetJson (const Asset&);
juce::var effectJson (const EffectInstance&, bool includeSpecs = false);
/** { id, type, bypassed, values } where values holds only the parameters that differ from the type's
    defaults - choices by name, toggles as booleans. The compact form for routine reads. */
juce::var effectCompactJson (const EffectInstance&);
/** A parameter value as compact JSON: a choice's name, a toggle's boolean, or a tidy number. */
juce::var paramValueJson (const ParamSpec&, float value);
juce::var effectTypeJson (const EffectDescriptor&);
/** { type, name, description } without the parameter specs. */
juce::var effectTypeBriefJson (const EffectDescriptor&);

/** Convenience builder for juce::var objects. */
juce::var makeObject (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties);

/** "3.2.480" (1-based bar.beat.tick) */
juce::String formatBarBeat (Tick t);

/** Compact description of which bars a track has audio in, e.g. "1-4, 9-16". */
juce::String occupiedBars (const Track&);

/** Save/load helpers with atomic write (temp file + rename). Throw on failure. */
void saveProjectFile (const Project&, const juce::File&);
Project loadProjectFile (const juce::File&, const AssetLoader& loader = {}, int* formatVersionRead = nullptr);
} // namespace mcpdaw
