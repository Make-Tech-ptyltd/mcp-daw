#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Biquad.h"
#include "EffectProcessor.h"
#include <array>

namespace mcpdaw
{
/** Six-band parametric EQ. Band parameters are smoothed in 32-sample steps to avoid zipper noise. */
class ParametricEq final : public EffectProcessor
{
public:
    explicit ParametricEq (const EffectDescriptor& d);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int numSamples, const ProcessContext&) noexcept override;

    /** Combined magnitude response in dB for a set of parameter values (used by the UI graph and tests). */
    static double responseDb (const std::vector<float>& values, double freq, double sampleRate);
    /** Magnitude response in dB of a single band (0 if disabled). */
    static double bandResponseDb (const std::vector<float>& values, int band, double freq, double sampleRate);

private:
    struct Band
    {
        bool enabled = false;
        BiquadCoeffs::Shape shape = BiquadCoeffs::Shape::Peak;
        double freq = 1000, gainDb = 0, q = 0.707;       // current (smoothed)
        double targetFreq = 1000, targetGain = 0, targetQ = 0.707;
        BiquadCoeffs coeffs;
        BiquadState state[2];
    };
    std::array<Band, eq::kNumBands> bands;
    double sampleRate = 48000;
    float outputGain = 1.0f, targetOutputGain = 1.0f;
    std::uint32_t seenGeneration = ~0u;

    void readParameters() noexcept;
    static constexpr int kSmoothStep = 32;
};
} // namespace mcpdaw
