// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Reverb.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <numbers>

namespace mcpdaw
{
namespace
{
enum Param { Mix, Predelay, Decay, Size, Damping, LowCut, Width };

// Mutually prime-ish base delays (ms) for the FDN, scaled by the Size parameter.
constexpr float kFdnBaseMs[FdnReverb::kChannels] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
constexpr float kMaxSizeScale = 1.6f;
constexpr float kMinSizeScale = 0.35f;

float sizeScale (double size) { return static_cast<float> (kMinSizeScale + (kMaxSizeScale - kMinSizeScale) * size); }

int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

/** In-place fast Walsh-Hadamard transform for 8 channels, energy preserving. */
inline void hadamard8 (float* x) noexcept
{
    for (int h = 1; h < 8; h <<= 1)
        for (int i = 0; i < 8; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }
    constexpr float scale = 0.35355339059327373f; // 1/sqrt(8)
    for (int i = 0; i < 8; ++i) x[i] *= scale;
}

/** Householder reflection I - 2/N * 11^T, energy preserving and cheap. */
inline void householder8 (float* x) noexcept
{
    float sum = 0;
    for (int i = 0; i < 8; ++i) sum += x[i];
    sum *= -2.0f / 8.0f;
    for (int i = 0; i < 8; ++i) x[i] += sum;
}
} // namespace

void FdnReverb::DelayLine::allocate (int maxDelay)
{
    const int size = nextPow2 (std::max (4, maxDelay + 2));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    writePos = 0;
}

FdnReverb::FdnReverb (const EffectDescriptor& d) : EffectProcessor (d)
{
    // Deterministic "random" diffuser layout: identical sound on every run and in tests.
    juce::Random rng (0xAC1D);
    float stepMs = 24.0f;
    for (int s = 0; s < kDiffusionSteps; ++s)
    {
        diffuserBaseMs[static_cast<size_t> (s)] = stepMs;
        for (int c = 0; c < kChannels; ++c)
        {
            // Spread each channel's delay across its slot of the step range.
            const float lo = static_cast<float> (c) / kChannels, hi = static_cast<float> (c + 1) / kChannels;
            diffuserRatios[static_cast<size_t> (s)][static_cast<size_t> (c)] = lo + (hi - lo) * rng.nextFloat();
            diffuserFlips[static_cast<size_t> (s)][static_cast<size_t> (c)] = rng.nextBool();
        }
        stepMs *= 0.5f;
    }
}

void FdnReverb::prepare (double sr, int)
{
    sampleRate = sr;
    const auto ms = [sr] (double v) { return static_cast<int> (std::ceil (v * 0.001 * sr)); };
    predelayL.allocate (ms (260.0));
    predelayR.allocate (ms (260.0));
    for (int s = 0; s < kDiffusionSteps; ++s)
        for (auto& line : diffuserLines[static_cast<size_t> (s)])
            line.allocate (ms (diffuserBaseMs[static_cast<size_t> (s)] * kMaxSizeScale) + 2);
    for (int c = 0; c < kChannels; ++c)
        fdnLines[static_cast<size_t> (c)].allocate (ms (kFdnBaseMs[c] * kMaxSizeScale) + 2);
    seenGeneration = ~0u;
    lastSize = lastDecay = -1;
    updateParameters();
    wetGain = static_cast<float> (std::sin (getParameter (Mix) * std::numbers::pi / 2));
    dryGain = static_cast<float> (std::cos (getParameter (Mix) * std::numbers::pi / 2));
    reset();
}

void FdnReverb::reset() noexcept
{
    predelayL.clear();
    predelayR.clear();
    for (auto& step : diffuserLines)
        for (auto& line : step) line.clear();
    for (auto& line : fdnLines) line.clear();
    dampState.fill (0);
    lowCutStateL = lowCutStateR = 0;
}

void FdnReverb::updateParameters() noexcept
{
    const auto gen = paramGeneration.load (std::memory_order_acquire);
    if (gen == seenGeneration) return;
    seenGeneration = gen;

    const double size = getParameter (Size);
    const double decay = getParameter (Decay);
    const float scale = sizeScale (size);

    if (size != lastSize)
    {
        for (int s = 0; s < kDiffusionSteps; ++s)
            for (int c = 0; c < kChannels; ++c)
            {
                const double d = diffuserBaseMs[static_cast<size_t> (s)] * scale * diffuserRatios[static_cast<size_t> (s)][static_cast<size_t> (c)];
                diffuserDelays[static_cast<size_t> (s)][static_cast<size_t> (c)] = std::max (1, static_cast<int> (d * 0.001 * sampleRate));
            }
        for (int c = 0; c < kChannels; ++c)
            fdnDelays[static_cast<size_t> (c)] = std::max (1, static_cast<int> (kFdnBaseMs[c] * scale * 0.001 * sampleRate));
    }
    if (size != lastSize || decay != lastDecay)
        for (int c = 0; c < kChannels; ++c)
        {
            const double lineSeconds = fdnDelays[static_cast<size_t> (c)] / sampleRate;
            fdnGains[static_cast<size_t> (c)] = static_cast<float> (std::pow (10.0, -3.0 * lineSeconds / std::max (0.05, decay)));
        }
    lastSize = size;
    lastDecay = decay;

    predelaySamples = std::max (1, static_cast<int> (getParameter (Predelay) * 0.001 * sampleRate) + 1);
    const double dampCutoff = 1500.0 * std::pow (12.0, 1.0 - getParameter (Damping));
    dampCoeff = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * std::min (dampCutoff, sampleRate * 0.45) / sampleRate));
    lowCutCoeff = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * getParameter (LowCut) / sampleRate));
}

void FdnReverb::process (float* left, float* right, int n, const ProcessContext&) noexcept
{
    updateParameters();
    const float targetWet = static_cast<float> (std::sin (getParameter (Mix) * std::numbers::pi / 2));
    const float targetDry = static_cast<float> (std::cos (getParameter (Mix) * std::numbers::pi / 2));
    const float wetStep = (targetWet - wetGain) / static_cast<float> (std::max (1, n));
    const float dryStep = (targetDry - dryGain) / static_cast<float> (std::max (1, n));
    const float width = getParameter (Width);

    float ch[kChannels];
    for (int i = 0; i < n; ++i)
    {
        predelayL.push (left[i]);
        predelayR.push (right[i]);
        const float inL = predelayL.read (predelaySamples);
        const float inR = predelayR.read (predelaySamples);
        for (int c = 0; c < kChannels; ++c) ch[c] = (c & 1) ? inR : inL;

        // Diffusion: delay each channel differently, flip some polarities, mix orthogonally.
        for (int s = 0; s < kDiffusionSteps; ++s)
        {
            auto& lines = diffuserLines[static_cast<size_t> (s)];
            for (int c = 0; c < kChannels; ++c)
            {
                lines[static_cast<size_t> (c)].push (ch[c]);
                const float v = lines[static_cast<size_t> (c)].read (diffuserDelays[static_cast<size_t> (s)][static_cast<size_t> (c)]);
                ch[c] = diffuserFlips[static_cast<size_t> (s)][static_cast<size_t> (c)] ? -v : v;
            }
            hadamard8 (ch);
        }

        // Feedback delay network.
        float delayed[kChannels], feedback[kChannels];
        for (int c = 0; c < kChannels; ++c)
        {
            delayed[c] = fdnLines[static_cast<size_t> (c)].read (fdnDelays[static_cast<size_t> (c)]);
            auto& d = dampState[static_cast<size_t> (c)];
            d += dampCoeff * (delayed[c] - d);
            feedback[c] = d * fdnGains[static_cast<size_t> (c)];
        }
        householder8 (feedback);
        for (int c = 0; c < kChannels; ++c)
            fdnLines[static_cast<size_t> (c)].push (ch[c] + feedback[c]);

        float wetL = 0.5f * (delayed[0] + delayed[2] + delayed[4] + delayed[6]);
        float wetR = 0.5f * (delayed[1] + delayed[3] + delayed[5] + delayed[7]);

        // Low cut on the wet signal (one-pole high-pass), then width via mid/side.
        lowCutStateL += lowCutCoeff * (wetL - lowCutStateL);
        lowCutStateR += lowCutCoeff * (wetR - lowCutStateR);
        wetL -= lowCutStateL;
        wetR -= lowCutStateR;
        const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * width;

        wetGain += wetStep;
        dryGain += dryStep;
        left[i] = left[i] * dryGain + (mid + side) * wetGain;
        right[i] = right[i] * dryGain + (mid - side) * wetGain;
    }
    wetGain = targetWet;
    dryGain = targetDry;
}
} // namespace mcpdaw
