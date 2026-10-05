#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Event edge fades - ONE mechanism for both anti-click "declick" ramps and musical fades.
//
// Every event stores a fade-in and a fade-out (ticks, default 0) and a curve for each. The
// application has one "Default declick time" preference (milliseconds, default 1.5 ms). The renderer
// applies, per edge:
//
//     effectiveFade = max (eventFade, defaultDeclick)
//
// so a fade handle dragged to zero still gets the tiny anti-click ramp, a deliberate fade simply
// replaces it, and setting the preference to 0 ms gives genuinely hard edges. There is no second,
// parallel declick envelope anywhere.
//
// The envelope functions below are pure, deterministic and allocation-free: the audio thread, the
// offline renderer, the timeline drawing and the tests all use exactly the same maths.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>

namespace mcpdaw
{
enum class FadeCurve : int
{
    Linear = 0,   // straight line
    Smooth = 1,   // S-curve (smoothstep): gentle at both ends
    Fast   = 2,   // level changes quickly at the start of the fade, then settles
    Slow   = 3    // level changes slowly at first, then quickly at the end
};

inline constexpr double kDefaultDeclickMs = 1.5;   // application default
inline constexpr double kMaxDeclickMs = 20.0;      // preference range is 0..20 ms

inline const char* fadeCurveName (FadeCurve c) noexcept
{
    switch (c)
    {
        case FadeCurve::Smooth: return "smooth";
        case FadeCurve::Fast:   return "fast";
        case FadeCurve::Slow:   return "slow";
        case FadeCurve::Linear: break;
    }
    return "linear";
}

/** Case-insensitive name -> curve ("linear", "smooth", "fast", "slow"); nullopt when unknown. */
inline std::optional<FadeCurve> fadeCurveFromName (const char* name) noexcept
{
    auto eq = [] (const char* a, const char* b)
    {
        for (; *a != 0 && *b != 0; ++a, ++b)
            if (std::tolower (static_cast<unsigned char> (*a)) != *b) return false;
        return *a == 0 && *b == 0;
    };
    if (name == nullptr) return std::nullopt;
    if (eq (name, "linear")) return FadeCurve::Linear;
    if (eq (name, "smooth")) return FadeCurve::Smooth;
    if (eq (name, "fast"))   return FadeCurve::Fast;
    if (eq (name, "slow"))   return FadeCurve::Slow;
    return std::nullopt;
}

/** The rising 0..1 shape of a curve: p = progress through a fade-in (0 = silent edge, 1 = full level). */
inline float fadeShape (FadeCurve c, float p) noexcept
{
    p = std::clamp (p, 0.0f, 1.0f);
    switch (c)
    {
        case FadeCurve::Smooth: return p * p * (3.0f - 2.0f * p);
        case FadeCurve::Fast:   return 1.0f - (1.0f - p) * (1.0f - p);
        case FadeCurve::Slow:   return p * p;
        case FadeCurve::Linear: break;
    }
    return p;
}

/** Fade-in gain for the sample `k` samples after the event start, over a fade of `n` samples.
    k = 0 (the first sample) is silent; k >= n is unity. n <= 0 means no fade. */
inline float fadeInGain (FadeCurve c, std::int64_t k, std::int64_t n) noexcept
{
    if (n <= 0 || k >= n) return 1.0f;
    if (k <= 0) return 0.0f;
    return fadeShape (c, static_cast<float> (static_cast<double> (k) / static_cast<double> (n)));
}

/** Fade-out gain for the sample with `k` samples still to come before the event end (the last
    sample has k = 0 and is silent). The curve is the time-mirror of the fade-in: "Fast" drops the
    level quickly as the fade-out begins, "Slow" holds it and drops late. */
inline float fadeOutGain (FadeCurve c, std::int64_t k, std::int64_t n) noexcept
{
    if (n <= 0 || k >= n) return 1.0f;
    if (k <= 0) return 0.0f;
    const float progress = 1.0f - static_cast<float> (static_cast<double> (k) / static_cast<double> (n));
    return 1.0f - fadeShape (c, progress);
}

/** Declick preference (milliseconds) -> whole samples at a sample rate. */
inline std::int64_t declickSamples (double declickMs, double sampleRate) noexcept
{
    if (! (declickMs > 0.0) || ! (sampleRate > 0.0)) return 0;
    return static_cast<std::int64_t> (std::llround (sampleRate * declickMs / 1000.0));
}

/** The unified rule: the declick time is the minimum effective fade. */
inline std::int64_t effectiveFadeSamples (std::int64_t eventFadeSamples, std::int64_t declick) noexcept
{
    return std::max<std::int64_t> ({ 0, eventFadeSamples, declick });
}
} // namespace mcpdaw
