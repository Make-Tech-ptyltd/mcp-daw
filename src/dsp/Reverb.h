#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "EffectProcessor.h"
#include <array>
#include <vector>

namespace mcpdaw
{
/**
    Algorithmic reverb: pre-delay -> 4-step multichannel diffuser -> 8-line feedback delay network.

    The structure follows the well-known "diffuser + FDN" design (Jot/Stautner-Puckette FDN with a
    Householder feedback matrix, Hadamard diffusion steps as popularised by Geraint Luff). Decay is
    specified directly as RT60 seconds: each line's feedback gain is 10^(-3 * lineDelay / RT60),
    and a one-pole low-pass per line makes high frequencies decay faster ("damping").
    All buffers are allocated in prepare() for the maximum size.
*/
class FdnReverb final : public EffectProcessor
{
public:
    static constexpr int kChannels = 8;
    static constexpr int kDiffusionSteps = 4;

    explicit FdnReverb (const EffectDescriptor& d);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int numSamples, const ProcessContext&) noexcept override;

private:
    /** Minimal power-of-two ring buffer delay line (integer delays). */
    struct DelayLine
    {
        std::vector<float> buffer;
        int mask = 0, writePos = 0;
        void allocate (int maxDelay);
        void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }
        inline void push (float x) noexcept { buffer[static_cast<size_t> (writePos)] = x; writePos = (writePos + 1) & mask; }
        inline float read (int delay) const noexcept { return buffer[static_cast<size_t> ((writePos - delay) & mask)]; }
    };

    double sampleRate = 48000;
    DelayLine predelayL, predelayR;
    std::array<std::array<DelayLine, kChannels>, kDiffusionSteps> diffuserLines;
    std::array<std::array<int, kChannels>, kDiffusionSteps> diffuserDelays {};
    std::array<std::array<bool, kChannels>, kDiffusionSteps> diffuserFlips {};
    std::array<float, kDiffusionSteps> diffuserBaseMs {};
    std::array<std::array<float, kChannels>, kDiffusionSteps> diffuserRatios {};
    std::array<DelayLine, kChannels> fdnLines;
    std::array<int, kChannels> fdnDelays {};
    std::array<float, kChannels> fdnGains {}, dampState {};
    float dampCoeff = 0.5f;
    float lowCutStateL = 0, lowCutStateR = 0, lowCutCoeff = 0;
    float wetGain = 0, dryGain = 1;
    std::uint32_t seenGeneration = ~0u;
    int predelaySamples = 0;
    double lastSize = -1, lastDecay = -1;

    void updateParameters() noexcept;
};
} // namespace mcpdaw
