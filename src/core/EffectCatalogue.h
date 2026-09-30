#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Declarative description of every native effect type and its parameters.
//
// This lives in core (not dsp) because the project model, JSON validation, MCP schemas and the
// generic FX-rack editor all need parameter metadata without depending on DSP code. Processors in
// src/dsp read their parameters by the *index* in this list; everything external addresses them
// by the stable string id (e.g. "band3.freq", "tap2.division").

#include <juce_core/juce_core.h>
#include <vector>

namespace mcpdaw
{
enum class ParamKind { Continuous, Toggle, Choice };

struct ParamSpec
{
    juce::String id;          // stable, machine-facing ("band1.gain")
    juce::String name;        // human label ("Gain")
    juce::String unit;        // "dB", "Hz", "ms", "s", "%", "" ...
    ParamKind kind = ParamKind::Continuous;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    float skewCentre = 0.0f;  // >0: value shown at the knob's centre (log-like mapping for UI)
    juce::StringArray choices;// for ParamKind::Choice; value is the zero-based index
    juce::String group;       // UI grouping ("Band 1", "Tap 3", "Global")
    juce::String help;        // short description for MCP/tooltips

    float clamp (float v) const noexcept;
    juce::String format (float v) const;
};

struct EffectDescriptor
{
    juce::String type;        // "eq", "compressor", "reverb", "multitap_delay"
    juce::String name;        // "Parametric EQ"
    juce::String description;
    std::vector<ParamSpec> params;

    int indexOf (const juce::String& paramId) const noexcept;
    std::vector<float> defaults() const;
};

const std::vector<EffectDescriptor>& effectCatalogue();
const EffectDescriptor* findEffectType (const juce::String& type) noexcept;

// --- effect-specific constants shared by DSP, UI and tests -----------------------------------------
namespace eq
{
    inline constexpr int kNumBands = 6;
    enum class BandType { HighPass = 0, LowShelf, Peak, HighShelf, LowPass, Notch };
    juce::StringArray bandTypeNames();
}

namespace delay
{
    inline constexpr int kNumTaps = 8;
    inline constexpr double kMaxDelaySeconds = 8.0;
    /** Tempo-synced divisions, expressed in beats (quarter notes). */
    const std::vector<std::pair<juce::String, double>>& divisions();
}
namespace distortion
{
    enum class Type { Soft = 0, Warm, Hard, Fuzz, Fold };
    juce::StringArray typeNames();
}
} // namespace mcpdaw
