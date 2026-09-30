#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// JSON representations of the project.
//
//  * File format ("mcp-daw-project", version 1): human-readable, diff-friendly, versioned.
//    Asset paths are stored relative to the project file when possible.
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
Project projectFromFileJson (const juce::var& json, const juce::File& projectDirectory, const AssetLoader& loader = {});

// --- state (MCP / inspection) --------------------------------------------------------------------
juce::var projectStateJson (const Project&);
juce::var trackJson (const Project&, const Track&);
juce::var eventJson (const Project&, const Event&);
juce::var assetJson (const Asset&);
juce::var effectJson (const EffectInstance&, bool includeSpecs = false);
juce::var effectTypeJson (const EffectDescriptor&);

/** Convenience builder for juce::var objects. */
juce::var makeObject (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties);

/** "3.2.480" (1-based bar.beat.tick) */
juce::String formatBarBeat (Tick t);

/** Compact description of which bars a track has audio in, e.g. "1-4, 9-16". */
juce::String occupiedBars (const Track&);

/** Save/load helpers with atomic write (temp file + rename). Throw on failure. */
void saveProjectFile (const Project&, const juce::File&);
Project loadProjectFile (const juce::File&, const AssetLoader& loader = {});
} // namespace mcpdaw
