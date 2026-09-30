#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "EffectProcessor.h"

namespace mcpdaw
{
/**
    Stereo-linked feed-forward compressor.

    Detector: peak of |L|,|R| -> dB -> soft-knee static curve -> gain reduction smoothed with
    separate attack/release one-pole filters in the dB domain (branching smoother). The static
    curve is exposed for deterministic tests.
*/
class Compressor final : public EffectProcessor
{
public:
    explicit Compressor (const EffectDescriptor& d);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int numSamples, const ProcessContext&) noexcept override;

    /** Gain reduction currently applied (dB, >= 0). */
    float telemetry (int) const noexcept override { return gainReductionDb.load (std::memory_order_relaxed); }

    /** Static gain computer: gain reduction in dB (>= 0) for an input level in dBFS. */
    static double staticGainReductionDb (double inputDb, double thresholdDb, double ratio, double kneeDb) noexcept;

private:
    double sampleRate = 48000;
    double envelopeDb = 0;          // smoothed gain reduction (dB, >= 0)
    float makeupGain = 1, mix = 1;
    std::atomic<float> gainReductionDb { 0 };
};
} // namespace mcpdaw
