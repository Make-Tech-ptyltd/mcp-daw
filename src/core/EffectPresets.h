#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Factory preset chains: small named recipes that insert ordinary, fully editable effects.
//
// A preset is only a starting point. Inserting one creates normal effect instances with every
// parameter value stored in the project, so changing a recipe in a later version never alters an
// existing song. There is no rack/macro object: after insertion the effects are edited, reordered,
// bypassed and removed individually, exactly like effects added one at a time.

#include "Project.h"

namespace mcpdaw
{
struct EffectPreset
{
    juce::String id;            // stable, machine-facing ("dubstep_pulse")
    juce::String name;          // "Dubstep pulse"
    juce::String description;
    struct Step
    {
        juce::String type;
        std::vector<std::pair<juce::String, juce::var>> values;   // parameter id -> number or choice name
    };
    std::vector<Step> chain;
};

const std::vector<EffectPreset>& effectPresets();
const EffectPreset* findEffectPreset (const juce::String& id) noexcept;

/** New effect instances for a preset (ids allocated from the project). Throws if a recipe is invalid. */
std::vector<EffectInstance> instantiatePreset (Project&, const EffectPreset&);
} // namespace mcpdaw
