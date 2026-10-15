// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "EffectPresets.h"
#include "Actions.h"

namespace mcpdaw
{
const std::vector<EffectPreset>& effectPresets()
{
    static const std::vector<EffectPreset> list = {
        { "dubstep_pulse", "Dubstep pulse",
          "Distortion -> tremolo: a consistently distorted bass chopped into hard 1/8 pulses that restart on every note. "
          "Use on a sustained or looping bass sample; shape each note with event fades. Try division 1/16 or 1/4, shape Sine "
          "for a rounder wobble.",
          { { "distortion", { { "type", "Warm" }, { "drive", 22.0 }, { "bias", 0.25 }, { "lowcut", 30.0 }, { "tone", 5000.0 },
                              { "autogain", 1.0 }, { "mix", 1.0 } } },
            { "tremolo", { { "division", "1/8" }, { "shape", "Square" }, { "depth", 40.0 }, { "restart", "Event" }, { "phase", 0.0 } } } } },

        { "dubstep_swell", "Dubstep swell",
          "Tremolo -> distortion: the pulse drives the distortion, so the grit swells and fades with each cycle (cleaner and "
          "quieter in the troughs). Starts each note from silence (phase 180) and swells in.",
          { { "tremolo", { { "division", "1/8" }, { "shape", "Sine" }, { "depth", 24.0 }, { "restart", "Event" }, { "phase", 180.0 } } },
            { "distortion", { { "type", "Fuzz" }, { "drive", 24.0 }, { "bias", 0.2 }, { "lowcut", 30.0 }, { "tone", 4500.0 },
                              { "autogain", 1.0 }, { "mix", 1.0 } } } } },

        { "triphop_drum_crunch", "Trip-hop drum crunch",
          "Compressor -> distortion -> EQ -> reverb: squashed, lightly crushed drums with a dark, small room. For breakbeats.",
          { { "compressor", { { "threshold", -24.0 }, { "ratio", 6.0 }, { "attack", 12.0 }, { "release", 150.0 }, { "knee", 6.0 },
                              { "makeup", 6.0 }, { "mix", 0.7 } } },
            { "distortion", { { "type", "Warm" }, { "drive", 10.0 }, { "bias", 0.15 }, { "lowcut", 40.0 }, { "tone", 7000.0 },
                              { "bits", 12.0 }, { "downsample", 2.0 }, { "autogain", 1.0 }, { "mix", 0.45 } } },
            { "eq", { { "band1.enabled", 1.0 }, { "band1.freq", 40.0 }, { "band2.gain", 2.0 }, { "band5.gain", -4.0 } } },
            { "reverb", { { "mix", 0.12 }, { "predelay", 10.0 }, { "decay", 0.7 }, { "size", 0.3 }, { "damping", 0.7 },
                          { "lowcut", 200.0 }, { "width", 0.8 } } } } },

        { "cinematic_synth_ambience", "Cinematic synth ambience",
          "EQ -> chorus -> reverb: wide, slow-moving pads and brass in a vast space (Blade Runner-style). Works on sustained "
          "analogue-style samples - layer a second, slightly detuned event (pitch +/-0.1) and use slow event fades. Effects "
          "shape the source; they do not turn a sample into an expressive synthesiser.",
          { { "eq", { { "band1.enabled", 1.0 }, { "band1.freq", 80.0 }, { "band3.freq", 350.0 }, { "band3.gain", -2.0 },
                      { "band3.q", 0.8 }, { "band5.gain", 2.5 } } },
            { "chorus", { { "rate", 0.25 }, { "depth", 0.35 }, { "delay", 12.0 }, { "feedback", 0.1 }, { "spread", 120.0 }, { "mix", 0.45 } } },
            { "reverb", { { "mix", 0.35 }, { "predelay", 45.0 }, { "decay", 7.0 }, { "size", 0.9 }, { "damping", 0.55 },
                          { "lowcut", 180.0 }, { "width", 1.0 } } } } },
    };
    return list;
}

const EffectPreset* findEffectPreset (const juce::String& id) noexcept
{
    for (const auto& p : effectPresets())
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<EffectInstance> instantiatePreset (Project& project, const EffectPreset& preset)
{
    std::vector<EffectInstance> chain;
    for (const auto& step : preset.chain)
    {
        auto fx = makeEffect (project, step.type);
        const auto* d = fx.descriptor();
        for (const auto& [id, value] : step.values)
        {
            const int i = d->indexOf (id);
            if (i < 0) throw std::runtime_error (("Preset " + preset.id + ": " + step.type + " has no parameter '" + id + "'").toStdString());
            fx.values[static_cast<size_t> (i)] = parseParamValue (d->params[static_cast<size_t> (i)], value);
        }
        chain.push_back (std::move (fx));
    }
    return chain;
}
} // namespace mcpdaw
