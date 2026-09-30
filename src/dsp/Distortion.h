#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "EffectProcessor.h"
#include <juce_dsp/juce_dsp.h>

namespace mcpdaw
{
/**
    Distortion built on the JUCE DSP module.

    Signal flow (per channel, stereo):
        dry ──────────────────────────────────────────────────────────────┐
        in → low cut (SVF HP) → 4x oversample ↑ → drive → shaper → 4x ↓ → DC block
           → crusher (bits + sample-and-hold, base rate: aliasing is the point) → tone (SVF LP)
           → auto-gain × output → DryWetMixer (dry delayed by the oversampler latency) → out

    Why these pieces:
      - juce::dsp::Oversampling (2 stages, polyphase IIR): hard curves (Hard/Fuzz/Fold) create
        harmonics far above Nyquist; 4x keeps the fold-back aliasing inaudible at low CPU. IIR keeps
        latency to a few samples (there is no plugin-delay compensation in the engine).
      - StateVariableTPTFilter: stable under fast cutoff changes (knob sweeps / MCP automation).
      - DryWetMixer: latency-aligned parallel blend, so mix < 100% doesn't comb-filter.

    The static curve `shape()` is public so the FX rack can draw the transfer function and tests can
    check it deterministically.
*/
class Distortion final : public EffectProcessor
{
public:
    explicit Distortion (const EffectDescriptor& d);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int numSamples, const ProcessContext&) noexcept override;

    /** The raw curve for a type: output bounded to about ±1; slope 1 at the origin except Fuzz (built-in gain). */
    static float shape (distortion::Type type, float x) noexcept;

    /** Full static transfer for a clean input x (no filters/crush): the curve with drive and bias applied, DC removed. */
    static float transfer (distortion::Type type, float driveDb, float bias, float x) noexcept;

    /** Gain that brings a -6 dBFS sine back to roughly its input RMS after the shaper (<= 1). */
    static float autoGain (distortion::Type type, float driveDb, float bias) noexcept;

    /** Oversampler latency in samples at the base rate (reported for tests / future PDC). */
    float latencySamples() const noexcept { return latency; }

private:
    void processChunk (float* left, float* right, int n) noexcept;
    void updateParameters() noexcept;

    static constexpr int kOversamplingStages = 2;   // 2^2 = 4x

    double sampleRate = 48000;
    int maxBlock = 512;
    float latency = 0;

    juce::dsp::Oversampling<float> oversampler { 2, kOversamplingStages,
                                                 juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    juce::dsp::StateVariableTPTFilter<float> lowCut, tone;
    juce::dsp::FirstOrderTPTFilter<float> dcBlock;
    juce::dsp::DryWetMixer<float> mixer { 256 };

    juce::SmoothedValue<float> driveGain, outputGain;   // linear, ramped (driveGain at the oversampled rate)

    // Cached parameter state (audio thread only)
    std::uint32_t seenGeneration = ~0u;
    distortion::Type type = distortion::Type::Warm;
    float bias = 0.1f, biasOffset = 0.0f, bits = 16.0f, downsample = 1.0f;

    // Crusher state
    float holdL = 0, holdR = 0, holdPhase = 0;
};
} // namespace mcpdaw
