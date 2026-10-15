// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Phaser.h"

namespace mcpdaw
{
namespace
{
enum Param { Sync, Rate, Division, Restart, Phase, Depth, Centre, Feedback, Spread, Mix };

constexpr double kLowHz = 20.0, kDecades = 3.0;   // the log axis: 20 Hz .. 20 kHz

double toNorm (double hz) { return std::log10 (std::max (kLowHz, hz) / kLowHz) / kDecades; }
} // namespace

Phaser::Phaser (const EffectDescriptor& d) : EffectProcessor (d) {}

double Phaser::cutoffHz (double centreHz, double d, double lfo) noexcept
{
    const double norm = juce::jlimit (0.0, 1.0, toNorm (centreHz) + 0.5 * d * lfo);
    return kLowHz * std::pow (10.0, kDecades * norm);
}

void Phaser::prepare (double sr, int)
{
    sampleRate = sr;
    normCentre.reset (sr, 0.05);
    depth.reset (sr, 0.02);
    feedback.reset (sr, 0.02);
    mix.reset (sr, 0.02);
    lfoStep = static_cast<float> (2.0 * 1000.0 / (kLfoSlewMs * sr));
    reset();
}

void Phaser::reset() noexcept
{
    clock.reset();
    normCentre.setCurrentAndTargetValue (static_cast<float> (toNorm (getParameter (Centre))));
    depth.setCurrentAndTargetValue (getParameter (Depth));
    feedback.setCurrentAndTargetValue (getParameter (Feedback));
    mix.setCurrentAndTargetValue (getParameter (Mix));
    std::fill (std::begin (stateL), std::end (stateL), 0.0f);
    std::fill (std::begin (stateR), std::end (stateR), 0.0f);
    fbL = fbR = 0.0f;
    lfoValid = false;
}

float Phaser::cascade (float x, float G, float* state) const noexcept
{
    // juce::dsp::FirstOrderTPTFilter, all-pass output, with G shared by all stages.
    for (int s = 0; s < kStages; ++s)
    {
        const float v = G * (x - state[s]);
        const float y = v + state[s];
        state[s] = y + v;
        x = 2.0f * y - x;
    }
    return x;
}

void Phaser::process (float* left, float* right, int n, const ProcessContext& ctx) noexcept
{
    const double cycle = sweepCycleSamples (getParameter (Sync) >= 0.5f, getParameter (Rate), getParameter (Division), ctx.bpm, sampleRate);
    const bool eventRestart = juce::roundToInt (getParameter (Restart)) == static_cast<int> (modulation::Restart::Event);
    const double phaseOffset = getParameter (Phase) / 360.0;
    const double spread = getParameter (Spread) / 360.0;
    normCentre.setTargetValue (static_cast<float> (toNorm (getParameter (Centre))));
    depth.setTargetValue (getParameter (Depth));
    feedback.setTargetValue (getParameter (Feedback));
    mix.setTargetValue (getParameter (Mix));

    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    const double nyquistLimit = 0.49 * sampleRate;
    const double piOverRate = juce::MathConstants<double>::pi / sampleRate;
    auto coefficient = [&] (float norm)
    {
        const double hz = std::min (nyquistLimit, kLowHz * std::pow (10.0, kDecades * juce::jlimit (0.0f, 1.0f, norm)));
        const double g = std::tan (hz * piOverRate);
        return static_cast<float> (g / (1.0 + g));
    };

    clock.run (ctx, n, cycle, phaseOffset, eventRestart, [&] (int i, double phase)
    {
        const auto targetL = static_cast<float> (std::sin (twoPi * phase));
        const auto targetR = static_cast<float> (std::sin (twoPi * (phase + spread)));
        if (! lfoValid) { lfoL = targetL; lfoR = targetR; lfoValid = true; }
        else { lfoL = slewTowards (lfoL, targetL, lfoStep); lfoR = slewTowards (lfoR, targetR, lfoStep); }

        const float centre = normCentre.getNextValue(), sweep = 0.5f * depth.getNextValue();
        const float fb = feedback.getNextValue(), wet = mix.getNextValue();
        const float inL = left[i], inR = right[i];
        fbL = cascade (inL + fb * fbL, coefficient (centre + sweep * lfoL), stateL);
        fbR = cascade (inR + fb * fbR, coefficient (centre + sweep * lfoR), stateR);
        left[i] = inL + wet * (fbL - inL);
        right[i] = inR + wet * (fbR - inR);
    });
}
} // namespace mcpdaw
