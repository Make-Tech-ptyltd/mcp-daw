// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "EffectCatalogue.h"
#include <cmath>

namespace mcpdaw
{
float ParamSpec::clamp (float v) const noexcept
{
    if (! std::isfinite (v)) return def;
    v = juce::jlimit (min, max, v);
    if (kind == ParamKind::Toggle) return v >= 0.5f ? 1.0f : 0.0f;
    if (kind == ParamKind::Choice) return static_cast<float> (juce::roundToInt (v));
    return v;
}

juce::String ParamSpec::format (float v) const
{
    switch (kind)
    {
        case ParamKind::Toggle: return v >= 0.5f ? "On" : "Off";
        case ParamKind::Choice: return choices[juce::jlimit (0, choices.size() - 1, juce::roundToInt (v))];
        case ParamKind::Continuous: break;
    }
    if (unit == "Hz")  return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    if (unit == "bit") return v >= 15.95f ? juce::String ("Off") : juce::String (v, 1) + " bit";
    if (unit == "x")   return v <= 1.01f ? juce::String ("Off") : juce::String (v, 1) + "x";
    if (unit == "%")   return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    if (unit == "ms")  return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
    if (unit == "dB")  return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
    if (unit == ":1")  return juce::String (v, 1) + ":1";
    if (unit == "pan") return std::abs (v) < 0.005f ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
    return juce::String (v, 2) + (unit.isEmpty() ? "" : " " + unit);
}

int EffectDescriptor::indexOf (const juce::String& paramId) const noexcept
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].id == paramId) return static_cast<int> (i);
    return -1;
}

std::vector<float> EffectDescriptor::defaults() const
{
    std::vector<float> v;
    v.reserve (params.size());
    for (const auto& p : params) v.push_back (p.def);
    return v;
}

juce::StringArray eq::bandTypeNames() { return { "High-pass", "Low shelf", "Bell", "High shelf", "Low-pass", "Notch" }; }

const std::vector<std::pair<juce::String, double>>& delay::divisions()
{
    // Ordered short -> long. T = triplet (2/3), D = dotted (3/2).
    static const std::vector<std::pair<juce::String, double>> list = {
        { "1/64", 1.0 / 16 }, { "1/32T", 1.0 / 12 }, { "1/32", 1.0 / 8 }, { "1/16T", 1.0 / 6 },
        { "1/32D", 3.0 / 16 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3 }, { "1/16D", 0.375 },
        { "1/8", 0.5 }, { "1/4T", 2.0 / 3 }, { "1/8D", 0.75 }, { "1/4", 1.0 },
        { "1/2T", 4.0 / 3 }, { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1/2D", 3.0 },
        { "1 bar", 4.0 }, { "2 bars", 8.0 } };
    return list;
}

namespace
{
ParamSpec cont (juce::String id, juce::String name, juce::String unit, float lo, float hi, float def,
                float skewCentre = 0.0f, juce::String group = {}, juce::String help = {})
{
    ParamSpec p;
    p.id = std::move (id); p.name = std::move (name); p.unit = std::move (unit);
    p.min = lo; p.max = hi; p.def = def; p.skewCentre = skewCentre;
    p.group = std::move (group); p.help = std::move (help);
    return p;
}

ParamSpec toggle (juce::String id, juce::String name, bool def, juce::String group = {}, juce::String help = {})
{
    auto p = cont (std::move (id), std::move (name), {}, 0.0f, 1.0f, def ? 1.0f : 0.0f, 0.0f, std::move (group), std::move (help));
    p.kind = ParamKind::Toggle;
    return p;
}

ParamSpec choice (juce::String id, juce::String name, juce::StringArray items, int def, juce::String group = {}, juce::String help = {})
{
    auto p = cont (std::move (id), std::move (name), {}, 0.0f, static_cast<float> (items.size() - 1), static_cast<float> (def), 0.0f,
                   std::move (group), std::move (help));
    p.kind = ParamKind::Choice;
    p.choices = std::move (items);
    return p;
}

EffectDescriptor makeEq()
{
    EffectDescriptor d { "eq", "Parametric EQ", "Six-band parametric EQ: high/low-pass, shelves, bells and notch (RBJ biquads, 12 dB/oct pass filters).", {} };
    struct BandDefault { eq::BandType type; float freq; bool on; };
    const BandDefault defaults[eq::kNumBands] = {
        { eq::BandType::HighPass, 30.0f, false }, { eq::BandType::LowShelf, 100.0f, true },
        { eq::BandType::Peak, 400.0f, true },     { eq::BandType::Peak, 2000.0f, true },
        { eq::BandType::HighShelf, 8000.0f, true },{ eq::BandType::LowPass, 18000.0f, false } };
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        const auto prefix = "band" + juce::String (b + 1) + ".";
        const auto group = "Band " + juce::String (b + 1);
        d.params.push_back (toggle (prefix + "enabled", "On", defaults[b].on, group, "Enable this band"));
        d.params.push_back (choice (prefix + "type", "Type", eq::bandTypeNames(), static_cast<int> (defaults[b].type), group,
                                    "0=high-pass 1=low shelf 2=bell 3=high shelf 4=low-pass 5=notch"));
        d.params.push_back (cont (prefix + "freq", "Freq", "Hz", 20.0f, 20000.0f, defaults[b].freq, 1000.0f, group, "Centre/corner frequency"));
        d.params.push_back (cont (prefix + "gain", "Gain", "dB", -18.0f, 18.0f, 0.0f, 0.0f, group, "Boost/cut (shelves and bells)"));
        d.params.push_back (cont (prefix + "q", "Q", "", 0.1f, 18.0f, 0.707f, 1.0f, group, "Bandwidth / resonance"));
    }
    d.params.push_back (cont ("output", "Output", "dB", -24.0f, 24.0f, 0.0f, 0.0f, "Global", "Output trim"));
    return d;
}

EffectDescriptor makeCompressor()
{
    EffectDescriptor d { "compressor", "Compressor", "Stereo-linked feed-forward compressor with soft knee and parallel mix.", {} };
    d.params = {
        cont ("threshold", "Threshold", "dB", -60.0f, 0.0f, -18.0f, 0.0f, "Dynamics"),
        cont ("ratio", "Ratio", ":1", 1.0f, 20.0f, 4.0f, 4.0f, "Dynamics"),
        cont ("attack", "Attack", "ms", 0.1f, 200.0f, 10.0f, 10.0f, "Dynamics"),
        cont ("release", "Release", "ms", 5.0f, 2000.0f, 120.0f, 150.0f, "Dynamics"),
        cont ("knee", "Knee", "dB", 0.0f, 24.0f, 6.0f, 0.0f, "Dynamics"),
        cont ("makeup", "Makeup", "dB", 0.0f, 24.0f, 0.0f, 0.0f, "Output"),
        cont ("mix", "Mix", "%", 0.0f, 1.0f, 1.0f, 0.0f, "Output", "Parallel (New York) compression blend") };
    return d;
}

EffectDescriptor makeReverb()
{
    EffectDescriptor d { "reverb", "Reverb", "Algorithmic reverb: input diffusion into an 8-line feedback delay network with RT60 decay control.", {} };
    d.params = {
        cont ("mix", "Mix", "%", 0.0f, 1.0f, 0.25f, 0.0f, "Output", "Wet/dry balance"),
        cont ("predelay", "Pre-delay", "ms", 0.0f, 250.0f, 20.0f, 40.0f, "Space"),
        cont ("decay", "Decay", "s", 0.1f, 20.0f, 1.8f, 2.0f, "Space", "RT60 at mid frequencies"),
        cont ("size", "Size", "%", 0.0f, 1.0f, 0.5f, 0.0f, "Space", "Room dimension (delay-line scale)"),
        cont ("damping", "Damping", "%", 0.0f, 1.0f, 0.5f, 0.0f, "Tone", "High-frequency decay speed"),
        cont ("lowcut", "Low cut", "Hz", 20.0f, 1000.0f, 120.0f, 150.0f, "Tone", "High-pass on the wet signal"),
        cont ("width", "Width", "%", 0.0f, 1.0f, 1.0f, 0.0f, "Output", "Stereo width of the wet signal") };
    return d;
}

EffectDescriptor makeMultiTap()
{
    EffectDescriptor d { "multitap_delay", "Multi-Tap Delay",
                         "Eight independent taps (tempo-synced or free ms) with level, pan and per-tap feedback into a shared, filtered and saturated feedback path.", {} };
    juce::StringArray divisionNames;
    for (const auto& [name, beats] : delay::divisions()) divisionNames.add (name);
    auto divisionIndex = [&] (const char* name) { return divisionNames.indexOf (name); };

    struct TapDefault { bool on; const char* division; float levelDb, pan, feedback; };
    const TapDefault defaults[delay::kNumTaps] = {
        { true,  "1/8D", -6.0f, -0.6f, 0.30f }, { true,  "1/4",  -9.0f,  0.6f, 0.00f },
        { false, "1/4D", -12.0f, -0.3f, 0.00f }, { false, "1/2", -12.0f,  0.3f, 0.00f },
        { false, "1/16", -12.0f, 0.0f, 0.00f },  { false, "1/8",  -12.0f, -1.0f, 0.00f },
        { false, "1/8T", -12.0f, 1.0f, 0.00f },  { false, "1 bar", -15.0f, 0.0f, 0.00f } };

    for (int t = 0; t < delay::kNumTaps; ++t)
    {
        const auto prefix = "tap" + juce::String (t + 1) + ".";
        const auto group = "Tap " + juce::String (t + 1);
        d.params.push_back (toggle (prefix + "enabled", "On", defaults[t].on, group));
        d.params.push_back (toggle (prefix + "sync", "Sync", true, group, "Tempo-synced (division) or free (ms)"));
        d.params.push_back (choice (prefix + "division", "Division", divisionNames, divisionIndex (defaults[t].division), group,
                                    "Note value when synced (T=triplet, D=dotted)"));
        d.params.push_back (cont (prefix + "time", "Time", "ms", 1.0f, 4000.0f, 250.0f, 300.0f, group, "Delay time when not synced"));
        d.params.push_back (cont (prefix + "level", "Level", "dB", -60.0f, 6.0f, defaults[t].levelDb, -12.0f, group));
        d.params.push_back (cont (prefix + "pan", "Pan", "pan", -1.0f, 1.0f, defaults[t].pan, 0.0f, group));
        d.params.push_back (cont (prefix + "feedback", "Feedback", "%", 0.0f, 0.95f, defaults[t].feedback, 0.0f, group,
                                  "Amount of this tap fed back into the delay line"));
    }
    d.params.push_back (cont ("mix", "Mix", "%", 0.0f, 1.0f, 0.35f, 0.0f, "Global", "Wet/dry balance"));
    d.params.push_back (cont ("lowcut", "FB low cut", "Hz", 20.0f, 2000.0f, 150.0f, 200.0f, "Global", "High-pass in the feedback path"));
    d.params.push_back (cont ("highcut", "FB high cut", "Hz", 500.0f, 20000.0f, 6000.0f, 4000.0f, "Global", "Low-pass in the feedback path (darker repeats)"));
    d.params.push_back (cont ("drive", "Drive", "%", 0.0f, 1.0f, 0.1f, 0.0f, "Global", "Saturation in the feedback path (dirt)"));
    return d;
}

EffectDescriptor makeDistortion()
{
    EffectDescriptor d { "distortion", "Distortion",
                         "4x-oversampled waveshaper (juce::dsp): soft/warm/hard/fuzz/fold curves with bias for even harmonics, "
                         "pre-drive low cut, post tone, bit/sample-rate crusher, auto-gain and parallel mix. Subtle and warm "
                         "(Warm, drive 6-12 dB, mix 50%) to brutal and crusty (Fuzz/Fold, 30+ dB, bits 6, downsample 4).", {} };
    d.params = {
        choice ("type", "Type", distortion::typeNames(), static_cast<int> (distortion::Type::Warm), "Drive",
                "Curve: Soft=tanh, Warm=asymmetric tube-ish, Hard=clip, Fuzz=square-ish, Fold=wavefolder"),
        cont ("drive", "Drive", "dB", 0.0f, 48.0f, 12.0f, 12.0f, "Drive", "Gain into the shaper"),
        cont ("bias", "Bias", "%", 0.0f, 1.0f, 0.1f, 0.0f, "Drive", "Asymmetry: adds even harmonics (warmth)"),
        cont ("lowcut", "Low cut", "Hz", 20.0f, 1000.0f, 20.0f, 150.0f, "Tone", "High-pass before the drive (tightens bass)"),
        cont ("tone", "Tone", "Hz", 500.0f, 20000.0f, 12000.0f, 3000.0f, "Tone", "Low-pass after the drive (tames fizz)"),
        cont ("bits", "Bits", "bit", 2.0f, 16.0f, 16.0f, 8.0f, "Crush", "Bit depth (16 = off)"),
        cont ("downsample", "Downsample", "x", 1.0f, 32.0f, 1.0f, 4.0f, "Crush", "Sample-and-hold factor (1 = off)"),
        toggle ("autogain", "Auto gain", true, "Output", "Compensate the level so drive changes character, not loudness"),
        cont ("mix", "Mix", "%", 0.0f, 1.0f, 1.0f, 0.0f, "Output", "Wet/dry balance (parallel distortion)"),
        cont ("output", "Output", "dB", -24.0f, 12.0f, 0.0f, 0.0f, "Output", "Output trim") };
    return d;
}
} // namespace

juce::StringArray distortion::typeNames() { return { "Soft", "Warm", "Hard", "Fuzz", "Fold" }; }

const std::vector<EffectDescriptor>& effectCatalogue()
{
    static const std::vector<EffectDescriptor> list = { makeEq(), makeCompressor(), makeReverb(), makeMultiTap(), makeDistortion() };
    return list;
}

const EffectDescriptor* findEffectType (const juce::String& type) noexcept
{
    for (const auto& d : effectCatalogue())
        if (d.type == type) return &d;
    return nullptr;
}
} // namespace mcpdaw
