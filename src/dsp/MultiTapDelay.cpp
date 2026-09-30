// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "MultiTapDelay.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <numbers>

namespace mcpdaw
{
namespace
{
// Per-tap parameter layout (see EffectCatalogue.cpp): enabled, sync, division, time, level, pan, feedback
constexpr int kPerTap = 7;
constexpr int tapIndex (int tap, int field) { return tap * kPerTap + field; }
constexpr int kMixIndex = delay::kNumTaps * kPerTap;
constexpr int kLowCutIndex = kMixIndex + 1, kHighCutIndex = kMixIndex + 2, kDriveIndex = kMixIndex + 3;

double tapSecondsFrom (float enabledUnused, float sync, float division, float timeMs, double bpm)
{
    juce::ignoreUnused (enabledUnused);
    if (sync >= 0.5f)
    {
        const auto& divs = delay::divisions();
        const auto i = static_cast<size_t> (juce::jlimit (0, static_cast<int> (divs.size()) - 1, juce::roundToInt (division)));
        return divs[i].second * 60.0 / std::max (1.0, bpm);
    }
    return timeMs / 1000.0;
}
} // namespace

MultiTapDelay::MultiTapDelay (const EffectDescriptor& d) : EffectProcessor (d) {}

double MultiTapDelay::tapSeconds (const std::vector<float>& v, int t, double bpm)
{
    if (v.size() <= static_cast<size_t> (tapIndex (t, 6))) return 0;
    return std::min (delay::kMaxDelaySeconds,
                     tapSecondsFrom (v[static_cast<size_t> (tapIndex (t, 0))], v[static_cast<size_t> (tapIndex (t, 1))],
                                     v[static_cast<size_t> (tapIndex (t, 2))], v[static_cast<size_t> (tapIndex (t, 3))], bpm));
}

void MultiTapDelay::prepare (double sr, int)
{
    sampleRate = sr;
    int size = 1;
    while (size < static_cast<int> (delay::kMaxDelaySeconds * sr) + 8) size <<= 1;
    bufferL.assign (static_cast<size_t> (size), 0.0f);
    bufferR.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    reset();
}

void MultiTapDelay::reset() noexcept
{
    std::fill (bufferL.begin(), bufferL.end(), 0.0f);
    std::fill (bufferR.begin(), bufferR.end(), 0.0f);
    writePos = 0;
    lowCutState = highCutState = 0;
    firstBlock = true;    // snap smoothed values to their targets on the next block
}

void MultiTapDelay::readParameters (double bpm) noexcept
{
    const double maxDelay = delay::kMaxDelaySeconds * sampleRate - 4;
    for (int t = 0; t < delay::kNumTaps; ++t)
    {
        auto& tap = taps[static_cast<size_t> (t)];
        tap.enabled = getParameter (tapIndex (t, 0)) >= 0.5f;
        const double seconds = tapSecondsFrom (0, getParameter (tapIndex (t, 1)), getParameter (tapIndex (t, 2)), getParameter (tapIndex (t, 3)), bpm);
        tap.targetDelay = std::clamp (seconds * sampleRate, 1.0, maxDelay);
        const float levelDb = getParameter (tapIndex (t, 4));
        tap.targetLevel = tap.enabled ? (levelDb <= -59.9f ? 0.0f : std::pow (10.0f, levelDb / 20.0f)) : 0.0f;
        const double theta = (getParameter (tapIndex (t, 5)) + 1.0) * std::numbers::pi / 4.0;   // constant-power pan
        tap.gainL = static_cast<float> (std::cos (theta));
        tap.gainR = static_cast<float> (std::sin (theta));
        tap.feedback = tap.enabled ? getParameter (tapIndex (t, 6)) : 0.0f;
    }
    lowCutCoeff = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * getParameter (kLowCutIndex) / sampleRate));
    highCutCoeff = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * std::min<double> (getParameter (kHighCutIndex), sampleRate * 0.45) / sampleRate));
    drive = 1.0f + 5.0f * getParameter (kDriveIndex);
}

void MultiTapDelay::process (float* left, float* right, int n, const ProcessContext& ctx) noexcept
{
    if (bufferL.empty()) return;
    readParameters (ctx.bpm);

    const float mix = getParameter (kMixIndex);
    const float targetWet = static_cast<float> (std::sin (mix * std::numbers::pi / 2));
    const float targetDry = static_cast<float> (std::cos (mix * std::numbers::pi / 2));
    if (firstBlock)
    {
        for (auto& t : taps) { t.delaySamples = t.targetDelay; t.level = t.targetLevel; }
        wetGain = targetWet;
        dryGain = targetDry;
        firstBlock = false;
    }
    const float wetStep = (targetWet - wetGain) / static_cast<float> (std::max (1, n));
    const float dryStep = (targetDry - dryGain) / static_cast<float> (std::max (1, n));
    const double delayGlide = 1.0 - std::exp (-1.0 / (0.03 * sampleRate));   // ~30 ms time constant
    const float levelGlide = static_cast<float> (1.0 - std::exp (-1.0 / (0.005 * sampleRate)));

    for (int i = 0; i < n; ++i)
    {
        float wetL = 0, wetR = 0, fb = 0;
        for (auto& tap : taps)
        {
            tap.level += levelGlide * (tap.targetLevel - tap.level);
            if (tap.level < 1e-6f && ! tap.enabled) continue;
            tap.delaySamples += delayGlide * (tap.targetDelay - tap.delaySamples);

            // Fractional read with linear interpolation.
            const double readPos = static_cast<double> (writePos) - tap.delaySamples;
            const double floorPos = std::floor (readPos);
            const float frac = static_cast<float> (readPos - floorPos);
            const int i0 = static_cast<int> (static_cast<long long> (floorPos) & mask);
            const int i1 = (i0 + 1) & mask;
            const float l = bufferL[static_cast<size_t> (i0)] + frac * (bufferL[static_cast<size_t> (i1)] - bufferL[static_cast<size_t> (i0)]);
            const float r = bufferR[static_cast<size_t> (i0)] + frac * (bufferR[static_cast<size_t> (i1)] - bufferR[static_cast<size_t> (i0)]);
            const float mono = 0.5f * (l + r);

            wetL += mono * tap.gainL * tap.level;
            wetR += mono * tap.gainR * tap.level;
            fb += mono * tap.feedback;
        }

        // Feedback tone path: HP -> LP -> bounded saturation.
        lowCutState += lowCutCoeff * (fb - lowCutState);
        float shaped = fb - lowCutState;
        highCutState += highCutCoeff * (shaped - highCutState);
        shaped = std::tanh (highCutState * drive) / drive;

        bufferL[static_cast<size_t> (writePos)] = left[i] + shaped;
        bufferR[static_cast<size_t> (writePos)] = right[i] + shaped;
        writePos = (writePos + 1) & mask;

        wetGain += wetStep;
        dryGain += dryStep;
        left[i] = left[i] * dryGain + wetL * wetGain;
        right[i] = right[i] * dryGain + wetR * wetGain;
    }
    wetGain = targetWet;
    dryGain = targetDry;
}
} // namespace mcpdaw
