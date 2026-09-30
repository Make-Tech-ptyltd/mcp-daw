// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Compressor.h"
#include "Biquad.h"
#include <cmath>

namespace mcpdaw
{
namespace { enum Param { Threshold, Ratio, Attack, Release, Knee, Makeup, Mix }; }

Compressor::Compressor (const EffectDescriptor& d) : EffectProcessor (d) {}

void Compressor::prepare (double sr, int)
{
    sampleRate = sr;
    reset();
}

void Compressor::reset() noexcept
{
    envelopeDb = 0;
    gainReductionDb.store (0, std::memory_order_relaxed);
}

double Compressor::staticGainReductionDb (double x, double t, double r, double w) noexcept
{
    // Standard soft-knee curve (Giannoulis, Massberg & Reiss, JAES 2012).
    const double slope = 1.0 - 1.0 / std::max (1.0, r);
    const double over = x - t;
    if (w > 0 && 2.0 * std::abs (over) <= w)
        return slope * (over + w / 2.0) * (over + w / 2.0) / (2.0 * w);
    return over > 0 ? slope * over : 0.0;
}

void Compressor::process (float* left, float* right, int n, const ProcessContext&) noexcept
{
    const double threshold = getParameter (Threshold);
    const double ratio = getParameter (Ratio);
    const double knee = getParameter (Knee);
    const double attackCoeff = onePoleCoeff (getParameter (Attack) / 1000.0, sampleRate);
    const double releaseCoeff = onePoleCoeff (getParameter (Release) / 1000.0, sampleRate);
    const float targetMakeup = static_cast<float> (std::pow (10.0, getParameter (Makeup) / 20.0));
    const float targetMix = getParameter (Mix);

    float maxReduction = 0;
    const float makeupStep = (targetMakeup - makeupGain) / static_cast<float> (std::max (1, n));
    const float mixStep = (targetMix - mix) / static_cast<float> (std::max (1, n));

    for (int i = 0; i < n; ++i)
    {
        const float peak = std::max (std::abs (left[i]), std::abs (right[i]));
        const double inDb = peak > 1e-9f ? 20.0 * std::log10 (static_cast<double> (peak)) : -180.0;
        const double target = staticGainReductionDb (inDb, threshold, ratio, knee);
        // Attack when more reduction is needed, release otherwise.
        const double c = target > envelopeDb ? attackCoeff : releaseCoeff;
        envelopeDb = target + c * (envelopeDb - target);

        makeupGain += makeupStep;
        mix += mixStep;
        const float gain = static_cast<float> (std::pow (10.0, -envelopeDb / 20.0)) * makeupGain;
        const float wet = mix, dry = 1.0f - mix;
        left[i] = left[i] * (dry + wet * gain);
        right[i] = right[i] * (dry + wet * gain);
        maxReduction = std::max (maxReduction, static_cast<float> (envelopeDb));
    }
    makeupGain = targetMakeup;
    mix = targetMix;
    gainReductionDb.store (maxReduction, std::memory_order_relaxed);
}
} // namespace mcpdaw
