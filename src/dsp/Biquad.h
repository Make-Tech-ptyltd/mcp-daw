#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// RBJ "Audio EQ Cookbook" biquads.
//
// Written out here (rather than juce::dsp::IIR::Coefficients) because coefficient updates must
// happen on the audio thread without allocation, and because the EQ display needs the exact same
// maths to draw its response curve.

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace mcpdaw
{
struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   // normalised (a0 == 1)

    enum class Shape { HighPass, LowShelf, Peak, HighShelf, LowPass, Notch };

    static BiquadCoeffs make (Shape shape, double sampleRate, double freq, double q, double gainDb) noexcept
    {
        freq = std::clamp (freq, 10.0, sampleRate * 0.49);
        q = std::max (q, 0.025);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * freq / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;

        switch (shape)
        {
            case Shape::HighPass:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Shape::LowPass:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Shape::Notch:
                b0 = 1; b1 = -2 * cw; b2 = 1;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Shape::Peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
            case Shape::LowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s; break;
            }
            case Shape::HighShelf:
            default:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s; break;
            }
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    /** |H(e^jw)| at `freq`. */
    double magnitude (double freq, double sampleRate) const noexcept
    {
        const double w = 2.0 * std::numbers::pi * freq / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return std::abs (num / den);
    }
};

/** Transposed direct form II, one channel. */
struct BiquadState
{
    double z1 = 0, z2 = 0;

    inline float process (const BiquadCoeffs& c, float x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return static_cast<float> (y);
    }
    void reset() noexcept { z1 = z2 = 0; }
};

/** One-pole smoothing coefficient for a time constant in seconds. */
inline float onePoleCoeff (double seconds, double sampleRate) noexcept
{
    return seconds <= 0.0 ? 0.0f : static_cast<float> (std::exp (-1.0 / (seconds * sampleRate)));
}
} // namespace mcpdaw
