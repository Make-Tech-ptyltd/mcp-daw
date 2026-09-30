// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "PlanRenderer.h"

namespace mcpdaw
{
namespace
{
constexpr int kFadeSamples = 64;   // ~1.3 ms declick at 48 kHz

inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c = 0.5f * (x1 - xm1);
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + 0.5f * (x2 - x0);
    const float bNeg = w + a;
    return ((((a * t) - bNeg) * t + c) * t + x0);
}

inline void updatePeak (std::atomic<float>& meter, float value) noexcept
{
    // Single writer (audio thread) + UI reader doing exchange(0): a relaxed max-update is sufficient.
    if (value > meter.load (std::memory_order_relaxed)) meter.store (value, std::memory_order_relaxed);
}

inline float peakOf (const float* d, int n) noexcept
{
    float p = 0;
    for (int i = 0; i < n; ++i) p = std::max (p, std::abs (d[i]));
    return p;
}

/** Smoothly move the runtime's gain to the target over the block (no zipper noise on fader moves). */
void applyGain (TrackRuntime& rt, float* l, float* r, int n, float targetL, float targetR) noexcept
{
    if (! rt.gainInitialised)
    {
        rt.currentGainL = targetL;
        rt.currentGainR = targetR;
        rt.gainInitialised = true;
    }
    const float stepL = (targetL - rt.currentGainL) / static_cast<float> (n);
    const float stepR = (targetR - rt.currentGainR) / static_cast<float> (n);
    float gl = rt.currentGainL, gr = rt.currentGainR;
    for (int i = 0; i < n; ++i)
    {
        gl += stepL;
        gr += stepR;
        l[i] *= gl;
        r[i] *= gr;
    }
    rt.currentGainL = targetL;
    rt.currentGainR = targetR;
}
} // namespace

PlanRenderer::PlanRenderer()
    : trackBuffer (2, kMaxBlock), masterBuffer (2, kMaxBlock)
{
}

void PlanRenderer::renderEvents (const PlanChannel& ch, std::int64_t segStart, int segLen, int bufOffset) noexcept
{
    float* outL = trackBuffer.getWritePointer (0) + bufOffset;
    float* outR = trackBuffer.getWritePointer (1) + bufOffset;
    const std::int64_t segEnd = segStart + segLen;

    for (const auto& e : ch.events)
    {
        if (e.start >= segEnd) break;           // events are sorted by start
        if (e.end <= segStart) continue;

        const std::int64_t from = std::max (e.start, segStart), to = std::min (e.end, segEnd);
        const auto& pcm = e.data->pcm;
        const int frames = pcm.getNumSamples();
        const double loopFrames = frames;
        const float* srcL = pcm.getReadPointer (0);
        const float* srcR = pcm.getReadPointer (pcm.getNumChannels() > 1 ? 1 : 0);

        double pos = e.sourceStart + static_cast<double> (from - e.start) * e.step;
        if (e.looping) pos = std::fmod (pos, loopFrames);

        for (std::int64_t t = from; t < to; ++t)
        {
            if (! e.looping && pos >= frames - 1) break;   // one-shot finished

            // 4-point cubic Hermite: clean enough for pitched-up / pitched-down sampler playback,
            // far better than linear for low sines (bass) and cheap enough for many voices.
            const int i0 = static_cast<int> (pos);
            const float frac = static_cast<float> (pos - i0);
            auto at = [&] (int i)
            {
                if (i < 0) return e.looping ? i + frames : 0;
                if (i >= frames) return e.looping ? i - frames : frames - 1;
                return i;
            };
            const int im = at (i0 - 1), i1 = at (i0 + 1), i2 = at (i0 + 2);
            const float l = hermite (srcL[im], srcL[i0], srcL[i1], srcL[i2], frac);
            const float r = hermite (srcR[im], srcR[i0], srcR[i1], srcR[i2], frac);

            float g = e.gain;
            if (e.fadeIn && t - e.start < kFadeSamples) g *= static_cast<float> (t - e.start) / kFadeSamples;
            if (e.fadeOut && e.end - t <= kFadeSamples) g *= static_cast<float> (e.end - t - 1) / kFadeSamples;

            const auto o = static_cast<int> (t - segStart);
            outL[o] += l * g;
            outR[o] += r * g;

            pos += e.step;
            if (e.looping && pos >= loopFrames) pos -= loopFrames;
        }
    }
}

PlanRenderer::Result PlanRenderer::render (const PlaybackPlan& plan, std::int64_t position, bool playing,
                                           float* outL, float* outR, int n) noexcept
{
    jassert (n <= kMaxBlock);
    Result result;
    const ProcessContext ctx { plan.sampleRate, plan.bpm, playing };
    const bool loop = playing && plan.loopEnabled && plan.loopEnd > plan.loopStart;
    if (loop && position >= plan.loopEnd) position = plan.loopStart;   // ACID: playhead beyond loop jumps back

    // Split the block into timeline segments at loop boundaries (a very short loop may wrap several times).
    struct Segment { std::int64_t timelineStart; int bufferOffset, length; };
    Segment segments[16];
    int numSegments = 0;
    std::int64_t pos = position;
    for (int done = 0; done < n && numSegments < 16;)
    {
        int len = n - done;
        if (loop && pos < plan.loopEnd) len = static_cast<int> (std::min<std::int64_t> (len, plan.loopEnd - pos));
        if (numSegments == 15) len = n - done;   // pathological tiny loop: finish linearly
        segments[numSegments++] = { pos, done, len };
        done += len;
        pos += len;
        if (loop && pos >= plan.loopEnd) pos = plan.loopStart;
    }
    result.newPosition = playing ? pos : position;

    masterBuffer.clear (0, n);
    float* mL = masterBuffer.getWritePointer (0);
    float* mR = masterBuffer.getWritePointer (1);

    for (const auto& ch : plan.tracks)
    {
        if (! ch.audible)
        {
            ch.runtime->gainInitialised = false;   // re-enter without a ramp from stale gain
            continue;
        }
        trackBuffer.clear (0, n);
        if (playing)
            for (int s = 0; s < numSegments; ++s)
                renderEvents (ch, segments[s].timelineStart, segments[s].length, segments[s].bufferOffset);

        float* l = trackBuffer.getWritePointer (0);
        float* r = trackBuffer.getWritePointer (1);
        for (auto* fx : ch.effects) fx->process (l, r, n, ctx);   // FX run while stopped so tails decay naturally
        applyGain (*ch.runtime, l, r, n, ch.gainL, ch.gainR);
        updatePeak (ch.runtime->peakL, peakOf (l, n));
        updatePeak (ch.runtime->peakR, peakOf (r, n));
        juce::FloatVectorOperations::add (mL, l, n);
        juce::FloatVectorOperations::add (mR, r, n);
    }

    for (auto* fx : plan.master.effects) fx->process (mL, mR, n, ctx);
    applyGain (*plan.master.runtime, mL, mR, n, plan.master.gainL, plan.master.gainR);

    result.masterPeakL = peakOf (mL, n);
    result.masterPeakR = peakOf (mR, n);
    updatePeak (plan.master.runtime->peakL, result.masterPeakL);
    updatePeak (plan.master.runtime->peakR, result.masterPeakR);

    juce::FloatVectorOperations::copy (outL, mL, n);
    juce::FloatVectorOperations::copy (outR, mR, n);
    return result;
}
} // namespace mcpdaw
