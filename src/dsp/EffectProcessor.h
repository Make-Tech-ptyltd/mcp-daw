#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Generic realtime effect interface.
//
// Lifecycle / threading contract:
//   construct + prepare()   control (message) thread; may allocate. Called once per sample rate.
//   setParameter()          any thread; lock-free (relaxed atomics). Processors pick up new values
//                           at the start of each block and smooth where audible.
//   process()/reset()       audio thread only; must not allocate, lock or do I/O.
//   destruction             control thread (the engine retires plans back to it).
//
// Processors are stereo in / stereo out. Mono sources are duplicated upstream. This keeps every
// effect simple and lets future VST3 hosting wrap a plugin in the same interface.

#include "../core/EffectCatalogue.h"
#include <atomic>
#include <memory>
#include <vector>

namespace mcpdaw
{
struct ProcessContext
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    bool playing = false;
};

class EffectProcessor
{
public:
    explicit EffectProcessor (const EffectDescriptor&);
    virtual ~EffectProcessor() = default;

    EffectProcessor (const EffectProcessor&) = delete;
    EffectProcessor& operator= (const EffectProcessor&) = delete;

    const EffectDescriptor& descriptor() const noexcept { return desc; }

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() noexcept = 0;
    virtual void process (float* left, float* right, int numSamples, const ProcessContext&) noexcept = 0;

    void setParameter (int index, float value) noexcept;
    void setAllParameters (const std::vector<float>& values) noexcept;
    float getParameter (int index) const noexcept { return params[static_cast<size_t> (index)].load (std::memory_order_relaxed); }

    /** Optional realtime telemetry for the UI (e.g. gain reduction in dB). */
    virtual float telemetry (int /*channel*/) const noexcept { return 0.0f; }

protected:
    const EffectDescriptor& desc;
    std::unique_ptr<std::atomic<float>[]> params;
    size_t numParams = 0;
    /** Incremented on every parameter change so processors can skip recomputing coefficients. */
    std::atomic<std::uint32_t> paramGeneration { 0 };
};

/** Creates the processor for a descriptor type. Returns nullptr for unknown types. */
std::unique_ptr<EffectProcessor> createEffectProcessor (const juce::String& type);
} // namespace mcpdaw
