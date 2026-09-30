// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Distortion.h"
#include <cmath>

namespace mcpdaw
{
namespace
{
enum Param { Type, Drive, Bias, LowCut, Tone, Bits, Downsample, AutoGain, Mix, Output };

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
} // namespace

Distortion::Distortion (const EffectDescriptor& d) : EffectProcessor (d) {}

// ------------------------------------------------------------------------------------------------
// Static curves
// ------------------------------------------------------------------------------------------------
float Distortion::shape (distortion::Type t, float x) noexcept
{
    using T = distortion::Type;
    switch (t)
    {
        case T::Soft: return std::tanh (x);
        // Tube-ish: positive half saturates like tanh, negative half with a softer exponential knee.
        // Both have slope 1 at 0, so the asymmetry only appears as you drive it (even harmonics).
        case T::Warm: return x >= 0.0f ? std::tanh (x) : -(1.0f - std::exp (x));
        case T::Hard: return juce::jlimit (-1.0f, 1.0f, x);
        // Fuzz: built-in extra gain (squares up early) and mismatched halves, like a transistor fuzz
        // with a lopsided bias: the kink at 0 adds gritty crossover harmonics even at low drive.
        case T::Fuzz: return x >= 0.0f ? std::tanh (3.0f * x) : 0.85f * std::tanh (2.4f * x);
        // Triangle wavefolder: identity up to ±1, then folds back instead of clipping.
        case T::Fold:
        {
            const float p = 0.25f * x + 0.25f;
            return 4.0f * std::abs (p - std::floor (p + 0.5f)) - 1.0f;
        }
    }
    return x;
}

float Distortion::transfer (distortion::Type t, float driveDb, float b, float x) noexcept
{
    return shape (t, dbToGain (driveDb) * x + b) - shape (t, b);
}

float Distortion::autoGain (distortion::Type t, float driveDb, float b) noexcept
{
    // RMS of a -6 dBFS sine through the curve, vs. its input RMS. 64 points over a period is plenty
    // for a static estimate; this runs only when parameters change.
    constexpr int kPoints = 64;
    constexpr float kAmp = 0.5f;
    double in = 0, out = 0;
    for (int i = 0; i < kPoints; ++i)
    {
        const float x = kAmp * std::sin (juce::MathConstants<float>::twoPi * static_cast<float> (i) / kPoints);
        const float y = transfer (t, driveDb, b, x);
        in += static_cast<double> (x) * x;
        out += static_cast<double> (y) * y;
    }
    if (out < 1e-12) return 1.0f;
    return static_cast<float> (juce::jlimit (1.0 / 64.0, 1.0, std::sqrt (in / out)));
}

// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------
void Distortion::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    maxBlock = std::max (1, maxBlockSize);

    oversampler.initProcessing (static_cast<size_t> (maxBlock));   // allocates: control thread only
    latency = oversampler.getLatencyInSamples();

    const juce::dsp::ProcessSpec spec { sr, static_cast<juce::uint32> (maxBlock), 2 };
    lowCut.prepare (spec);
    lowCut.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    tone.prepare (spec);
    tone.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    dcBlock.prepare (spec);
    dcBlock.setType (juce::dsp::FirstOrderTPTFilterType::highpass);
    dcBlock.setCutoffFrequency (8.0f);

    mixer.prepare (spec);
    mixer.setMixingRule (juce::dsp::DryWetMixingRule::linear);
    mixer.setWetLatency (latency);

    const double osRate = sr * (1 << kOversamplingStages);
    driveGain.reset (osRate, 0.02);
    outputGain.reset (sr, 0.02);
    reset();
}

void Distortion::reset() noexcept
{
    oversampler.reset();
    lowCut.reset();
    tone.reset();
    dcBlock.reset();
    mixer.reset();
    holdL = holdR = holdPhase = 0;
    seenGeneration = ~0u;
    updateParameters();
    driveGain.setCurrentAndTargetValue (driveGain.getTargetValue());
    outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
}

void Distortion::updateParameters() noexcept
{
    const auto gen = paramGeneration.load (std::memory_order_relaxed);
    if (gen == seenGeneration) return;
    seenGeneration = gen;

    type = static_cast<distortion::Type> (juce::jlimit (0, 4, juce::roundToInt (getParameter (Type))));
    bias = getParameter (Bias);
    biasOffset = shape (type, bias);
    bits = getParameter (Bits);
    downsample = getParameter (Downsample);

    const float driveDb = getParameter (Drive);
    driveGain.setTargetValue (dbToGain (driveDb));
    const float comp = getParameter (AutoGain) >= 0.5f ? autoGain (type, driveDb, bias) : 1.0f;
    outputGain.setTargetValue (comp * dbToGain (getParameter (Output)));

    lowCut.setCutoffFrequency (juce::jlimit (20.0f, 1000.0f, getParameter (LowCut)));
    tone.setCutoffFrequency (juce::jlimit (500.0f, std::min (20000.0f, static_cast<float> (sampleRate * 0.45)), getParameter (Tone)));
    mixer.setWetMixProportion (getParameter (Mix));
}

// ------------------------------------------------------------------------------------------------
// Audio
// ------------------------------------------------------------------------------------------------
void Distortion::process (float* left, float* right, int numSamples, const ProcessContext&) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    updateParameters();
    for (int done = 0; done < numSamples;)   // the oversampler and mixer were sized for maxBlock
    {
        const int n = std::min (maxBlock, numSamples - done);
        processChunk (left + done, right + done, n);
        done += n;
    }
}

void Distortion::processChunk (float* left, float* right, int n) noexcept
{
    float* channels[] = { left, right };
    juce::dsp::AudioBlock<float> block (channels, 2, static_cast<size_t> (n));
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    mixer.pushDrySamples (block);

    // 1) Pre-drive low cut (skipped at the bottom of its range: keeps a 55 Hz bass fundamental untouched).
    if (getParameter (LowCut) > 20.5f) lowCut.process (ctx);

    // 2) Drive + shaper at 4x.
    auto up = oversampler.processSamplesUp (block);
    const auto upN = up.getNumSamples();
    float* ul = up.getChannelPointer (0);
    float* ur = up.getChannelPointer (1);
    for (size_t i = 0; i < upN; ++i)
    {
        const float g = driveGain.getNextValue();
        ul[i] = shape (type, g * ul[i] + bias) - biasOffset;
        ur[i] = shape (type, g * ur[i] + bias) - biasOffset;
    }
    oversampler.processSamplesDown (block);

    // 3) Remove DC left by asymmetric curves.
    dcBlock.process (ctx);

    // 4) Crusher at the base rate (aliasing is the desired "crust").
    const bool crushBits = bits < 15.95f;
    const bool crushRate = downsample > 1.01f;
    if (crushBits || crushRate)
    {
        const float levels = std::pow (2.0f, bits - 1.0f);
        for (int i = 0; i < n; ++i)
        {
            float l = left[i], r = right[i];
            if (crushRate)
            {
                if (holdPhase <= 0.0f) { holdL = l; holdR = r; holdPhase += downsample; }
                holdPhase -= 1.0f;
                l = holdL; r = holdR;
            }
            if (crushBits)
            {
                l = std::round (l * levels) / levels;
                r = std::round (r * levels) / levels;
            }
            left[i] = l; right[i] = r;
        }
    }

    // 5) Post tone (skipped when fully open).
    if (getParameter (Tone) < 19900.0f) tone.process (ctx);

    // 6) Level: auto-gain x output trim, ramped.
    for (int i = 0; i < n; ++i)
    {
        const float g = outputGain.getNextValue();
        left[i] *= g;
        right[i] *= g;
    }

    mixer.mixWetSamples (block);
}
} // namespace mcpdaw
