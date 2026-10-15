// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "EffectProcessor.h"
#include "Chorus.h"
#include "Compressor.h"
#include "Distortion.h"
#include "MultiTapDelay.h"
#include "ParametricEq.h"
#include "Phaser.h"
#include "Reverb.h"
#include "Tremolo.h"

namespace mcpdaw
{
EffectProcessor::EffectProcessor (const EffectDescriptor& d)
    : desc (d),
      params (std::make_unique<std::atomic<float>[]> (d.params.size())),
      numParams (d.params.size())
{
    for (size_t i = 0; i < numParams; ++i)
        params[i].store (d.params[i].def, std::memory_order_relaxed);
}

void EffectProcessor::setParameter (int index, float value) noexcept
{
    if (index < 0 || static_cast<size_t> (index) >= numParams) return;
    const float v = desc.params[static_cast<size_t> (index)].clamp (value);
    if (params[static_cast<size_t> (index)].exchange (v, std::memory_order_relaxed) != v)
        paramGeneration.fetch_add (1, std::memory_order_release);
}

void EffectProcessor::setAllParameters (const std::vector<float>& values) noexcept
{
    for (size_t i = 0; i < values.size() && i < numParams; ++i)
        setParameter (static_cast<int> (i), values[i]);
}

std::unique_ptr<EffectProcessor> createEffectProcessor (const juce::String& type)
{
    const auto* d = findEffectType (type);
    if (d == nullptr) return nullptr;
    if (type == "eq")             return std::make_unique<ParametricEq> (*d);
    if (type == "compressor")     return std::make_unique<Compressor> (*d);
    if (type == "reverb")         return std::make_unique<FdnReverb> (*d);
    if (type == "multitap_delay") return std::make_unique<MultiTapDelay> (*d);
    if (type == "distortion")     return std::make_unique<Distortion> (*d);
    if (type == "chorus")         return std::make_unique<Chorus> (*d);
    if (type == "phaser")         return std::make_unique<Phaser> (*d);
    if (type == "tremolo")        return std::make_unique<Tremolo> (*d);
    return nullptr;
}
} // namespace mcpdaw
