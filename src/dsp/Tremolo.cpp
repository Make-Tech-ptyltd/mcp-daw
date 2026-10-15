// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Tremolo.h"

namespace mcpdaw
{
namespace
{
enum Param { Division, Shape, Depth, Restart, Phase };
}

Tremolo::Tremolo (const EffectDescriptor& d) : EffectProcessor (d) {}

float Tremolo::waveform (modulation::Shape shape, double phase) noexcept
{
    phase -= std::floor (phase);
    switch (shape)
    {
        case modulation::Shape::Triangle: return static_cast<float> (std::abs (1.0 - 2.0 * phase));
        case modulation::Shape::Square:   return phase < 0.5 ? 1.0f : 0.0f;
        case modulation::Shape::Sine:     break;
    }
    return static_cast<float> (0.5 + 0.5 * std::cos (juce::MathConstants<double>::twoPi * phase));
}

float Tremolo::gainAt (modulation::Shape shape, float depthDb, double phase) noexcept
{
    if (depthDb <= 0.0f) return 1.0f;
    const float minGain = std::pow (10.0f, -depthDb / 20.0f);
    return minGain + (1.0f - minGain) * waveform (shape, phase);
}

void Tremolo::prepare (double sr, int)
{
    sampleRate = sr;
    maxStep = static_cast<float> (1000.0 / (kEdgeMs * sr));
    reset();
}

void Tremolo::reset() noexcept
{
    clock.reset();
    gain = 1.0f;
    gainValid = false;
}

void Tremolo::process (float* left, float* right, int n, const ProcessContext& ctx) noexcept
{
    const auto& divisions = modulation::tremoloDivisions();
    const auto division = divisions[static_cast<size_t> (juce::jlimit (0, static_cast<int> (divisions.size()) - 1,
                                                                       juce::roundToInt (getParameter (Division))))].second;
    const auto shape = static_cast<modulation::Shape> (juce::jlimit (0, 2, juce::roundToInt (getParameter (Shape))));
    const float depthDb = getParameter (Depth);
    const bool eventRestart = juce::roundToInt (getParameter (Restart)) == static_cast<int> (modulation::Restart::Event);
    const double phaseOffset = getParameter (Phase) / 360.0;

    // Depth 0 and already at unity: bit-exact pass-through.
    if (depthDb <= 0.0f && gainValid && gain == 1.0f) return;

    const double cycleSamples = beatsToSeconds (division, ctx.bpm) * sampleRate;
    const float minGain = depthDb <= 0.0f ? 1.0f : std::pow (10.0f, -depthDb / 20.0f);
    const float range = 1.0f - minGain;

    clock.run (ctx, n, cycleSamples, phaseOffset, eventRestart, [&] (int i, double phase)
    {
        const float target = minGain + range * waveform (shape, phase);
        if (! gainValid) { gain = target; gainValid = true; }
        else gain = slewTowards (gain, target, maxStep);
        left[i] *= gain;
        right[i] *= gain;
    });
}
} // namespace mcpdaw
