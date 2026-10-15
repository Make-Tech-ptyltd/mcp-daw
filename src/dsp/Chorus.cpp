// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Chorus.h"

namespace mcpdaw
{
namespace
{
enum Param { Sync, Rate, Division, Restart, Phase, Depth, Delay, Feedback, Spread, Mix };
}

Chorus::Chorus (const EffectDescriptor& d) : EffectProcessor (d) {}

void Chorus::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    delay.setMaximumDelayInSamples (static_cast<int> (std::ceil (kMaxDelayMs * sr / 1000.0)) + 4);   // allocates
    delay.prepare ({ sr, static_cast<juce::uint32> (std::max (1, maxBlockSize)), 2 });
    centreMs.reset (sr, 0.05);   // a delay-time jump would click: glide over 50 ms
    depth.reset (sr, 0.02);
    feedback.reset (sr, 0.02);
    mix.reset (sr, 0.02);
    lfoStep = static_cast<float> (2.0 * 1000.0 / (kLfoSlewMs * sr));
    reset();
}

void Chorus::reset() noexcept
{
    delay.reset();
    clock.reset();
    centreMs.setCurrentAndTargetValue (getParameter (Delay));
    depth.setCurrentAndTargetValue (getParameter (Depth));
    feedback.setCurrentAndTargetValue (getParameter (Feedback));
    mix.setCurrentAndTargetValue (getParameter (Mix));
    fbL = fbR = 0.0f;
    lfoValid = false;
}

void Chorus::process (float* left, float* right, int n, const ProcessContext& ctx) noexcept
{
    const double cycle = sweepCycleSamples (getParameter (Sync) >= 0.5f, getParameter (Rate), getParameter (Division), ctx.bpm, sampleRate);
    const bool eventRestart = juce::roundToInt (getParameter (Restart)) == static_cast<int> (modulation::Restart::Event);
    const double phaseOffset = getParameter (Phase) / 360.0;
    const double spread = getParameter (Spread) / 360.0;
    centreMs.setTargetValue (getParameter (Delay));
    depth.setTargetValue (getParameter (Depth));
    feedback.setTargetValue (getParameter (Feedback));
    mix.setTargetValue (getParameter (Mix));

    const float msToSamples = static_cast<float> (sampleRate / 1000.0);
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    clock.run (ctx, n, cycle, phaseOffset, eventRestart, [&] (int i, double phase)
    {
        const auto targetL = static_cast<float> (std::sin (twoPi * phase));
        const auto targetR = static_cast<float> (std::sin (twoPi * (phase + spread)));
        if (! lfoValid) { lfoL = targetL; lfoR = targetR; lfoValid = true; }
        else { lfoL = slewTowards (lfoL, targetL, lfoStep); lfoR = slewTowards (lfoR, targetR, lfoStep); }

        const float centre = centreMs.getNextValue() * msToSamples, d = depth.getNextValue();
        const float fb = feedback.getNextValue(), wet = mix.getNextValue();
        const float delayL = std::max (2.0f, centre * (1.0f + d * lfoL));
        const float delayR = std::max (2.0f, centre * (1.0f + d * lfoR));

        const float inL = left[i], inR = right[i];
        delay.pushSample (0, inL + fb * fbL);
        delay.pushSample (1, inR + fb * fbR);
        fbL = delay.popSample (0, delayL);
        fbR = delay.popSample (1, delayR);
        left[i] = inL + wet * (fbL - inL);
        right[i] = inR + wet * (fbR - inR);
    });
}
} // namespace mcpdaw
