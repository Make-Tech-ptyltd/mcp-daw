// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "ParametricEq.h"
#include <juce_core/juce_core.h>

namespace mcpdaw
{
namespace
{
// Parameter layout per band: enabled, type, freq, gain, q  (see EffectCatalogue.cpp)
constexpr int kParamsPerBand = 5;
constexpr int idx (int band, int field) { return band * kParamsPerBand + field; }
constexpr int kOutputIndex = eq::kNumBands * kParamsPerBand;

BiquadCoeffs::Shape shapeFor (float typeValue)
{
    switch (static_cast<eq::BandType> (juce::jlimit (0, 5, juce::roundToInt (typeValue))))
    {
        case eq::BandType::HighPass:  return BiquadCoeffs::Shape::HighPass;
        case eq::BandType::LowShelf:  return BiquadCoeffs::Shape::LowShelf;
        case eq::BandType::Peak:      return BiquadCoeffs::Shape::Peak;
        case eq::BandType::HighShelf: return BiquadCoeffs::Shape::HighShelf;
        case eq::BandType::LowPass:   return BiquadCoeffs::Shape::LowPass;
        case eq::BandType::Notch:     return BiquadCoeffs::Shape::Notch;
    }
    return BiquadCoeffs::Shape::Peak;
}
} // namespace

ParametricEq::ParametricEq (const EffectDescriptor& d) : EffectProcessor (d) {}

void ParametricEq::prepare (double sr, int)
{
    sampleRate = sr;
    seenGeneration = ~0u;
    readParameters();
    for (auto& b : bands)       // start exactly at target, no initial glide
    {
        b.freq = b.targetFreq; b.gainDb = b.targetGain; b.q = b.targetQ;
        b.coeffs = BiquadCoeffs::make (b.shape, sampleRate, b.freq, b.q, b.gainDb);
    }
    outputGain = targetOutputGain;
    reset();
}

void ParametricEq::reset() noexcept
{
    for (auto& b : bands) { b.state[0].reset(); b.state[1].reset(); }
}

void ParametricEq::readParameters() noexcept
{
    const auto gen = paramGeneration.load (std::memory_order_acquire);
    if (gen == seenGeneration) return;
    seenGeneration = gen;
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        auto& band = bands[static_cast<size_t> (b)];
        const bool wasEnabled = band.enabled;
        const auto oldShape = band.shape;
        band.enabled = getParameter (idx (b, 0)) >= 0.5f;
        band.shape = shapeFor (getParameter (idx (b, 1)));
        band.targetFreq = getParameter (idx (b, 2));
        band.targetGain = getParameter (idx (b, 3));
        band.targetQ = getParameter (idx (b, 4));
        if (band.enabled && (! wasEnabled || oldShape != band.shape))
        {
            // A band switching on or changing shape starts from clean state to avoid a transient.
            band.freq = band.targetFreq; band.gainDb = band.targetGain; band.q = band.targetQ;
            band.coeffs = BiquadCoeffs::make (band.shape, sampleRate, band.freq, band.q, band.gainDb);
            band.state[0].reset(); band.state[1].reset();
        }
    }
    targetOutputGain = static_cast<float> (std::pow (10.0, getParameter (kOutputIndex) / 20.0));
}

void ParametricEq::process (float* left, float* right, int numSamples, const ProcessContext&) noexcept
{
    readParameters();
    // Smoothing: move ~15 % of the way (log-frequency, linear gain/Q) per 32-sample step (~5 ms time constant).
    constexpr double k = 0.15;
    for (int start = 0; start < numSamples; start += kSmoothStep)
    {
        const int n = std::min (kSmoothStep, numSamples - start);
        for (auto& b : bands)
        {
            if (! b.enabled) continue;
            if (b.freq != b.targetFreq || b.gainDb != b.targetGain || b.q != b.targetQ)
            {
                b.freq = std::exp (std::log (b.freq) + k * (std::log (b.targetFreq) - std::log (b.freq)));
                b.gainDb += k * (b.targetGain - b.gainDb);
                b.q += k * (b.targetQ - b.q);
                if (std::abs (b.freq - b.targetFreq) < 0.01 && std::abs (b.gainDb - b.targetGain) < 0.001 && std::abs (b.q - b.targetQ) < 0.0001)
                { b.freq = b.targetFreq; b.gainDb = b.targetGain; b.q = b.targetQ; }
                b.coeffs = BiquadCoeffs::make (b.shape, sampleRate, b.freq, b.q, b.gainDb);
            }
            for (int i = start; i < start + n; ++i)
            {
                left[i] = b.state[0].process (b.coeffs, left[i]);
                right[i] = b.state[1].process (b.coeffs, right[i]);
            }
        }
        const float g0 = outputGain;
        outputGain += 0.2f * (targetOutputGain - outputGain);
        if (std::abs (outputGain - targetOutputGain) < 1e-5f) outputGain = targetOutputGain;
        const float step = (outputGain - g0) / static_cast<float> (n);
        float g = g0;
        for (int i = start; i < start + n; ++i)
        {
            g += step;
            left[i] *= g;
            right[i] *= g;
        }
    }
}

double ParametricEq::bandResponseDb (const std::vector<float>& v, int b, double freq, double sr)
{
    if (v.size() <= static_cast<size_t> (kOutputIndex) || v[static_cast<size_t> (idx (b, 0))] < 0.5f) return 0.0;
    const auto c = BiquadCoeffs::make (shapeFor (v[static_cast<size_t> (idx (b, 1))]), sr, v[static_cast<size_t> (idx (b, 2))],
                                       v[static_cast<size_t> (idx (b, 4))], v[static_cast<size_t> (idx (b, 3))]);
    return 20.0 * std::log10 (std::max (1e-9, c.magnitude (freq, sr)));
}

double ParametricEq::responseDb (const std::vector<float>& v, double freq, double sr)
{
    double db = v.size() > static_cast<size_t> (kOutputIndex) ? v[static_cast<size_t> (kOutputIndex)] : 0.0;
    for (int b = 0; b < eq::kNumBands; ++b) db += bandResponseDb (v, b, freq, sr);
    return db;
}
} // namespace mcpdaw
