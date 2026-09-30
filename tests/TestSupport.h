#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Shared fixtures for the headless test suite.
#include "core/Actions.h"
#include "core/ProjectJson.h"
#include "engine/OfflineRenderer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <numbers>

namespace mcpdaw::test
{
/** A scratch directory unique to this test run, deleted at exit. */
inline juce::File scratchDir()
{
    static juce::TemporaryFile dir ("mcpdaw-tests");
    static bool created = dir.getFile().createDirectory().wasOk();
    juce::ignoreUnused (created);
    return dir.getFile();
}

/** Write a WAV file. `fill(channel, index)` supplies samples. Optional ACID tempo metadata. */
template <typename Fn>
juce::File writeWav (const juce::String& name, double sampleRate, int channels, int frames, Fn&& fill, double acidTempo = 0, double acidBeats = 0)
{
    auto file = scratchDir().getChildFile (name);
    file.deleteFile();
    juce::AudioBuffer<float> buffer (channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i) buffer.setSample (c, i, fill (c, i));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (channels).withBitsPerSample (32)
                       .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    if (acidTempo > 0)
        options = options.withMetadata (juce::WavAudioFormat::acidTempo, juce::String (acidTempo))
                         .withMetadata (juce::WavAudioFormat::acidBeats, juce::String (static_cast<int> (acidBeats)))
                         .withMetadata (juce::WavAudioFormat::acidNumerator, "4")
                         .withMetadata (juce::WavAudioFormat::acidDenominator, "4")
                         .withMetadata (juce::WavAudioFormat::acidStretch, "1");
    auto writer = wav.createWriterFor (stream, options);
    jassert (writer != nullptr);
    writer->writeFromAudioSampleBuffer (buffer, 0, frames);
    return file;
}

/** One second of a constant value (DC) — makes sample-position assertions trivial. */
inline juce::File dcWav (const juce::String& name, float value, double sampleRate = 48000, int frames = 48000)
{
    return writeWav (name, sampleRate, 2, frames, [value] (int, int) { return value; });
}

/** Mono impulse at index 0 followed by silence. */
inline juce::File impulseWav (const juce::String& name, double sampleRate = 48000, int frames = 48000)
{
    return writeWav (name, sampleRate, 1, frames, [] (int, int i) { return i == 0 ? 1.0f : 0.0f; });
}

inline juce::var args (std::initializer_list<std::pair<juce::Identifier, juce::var>> p) { return makeObject (p); }

/** Controller + registry with all actions, no audio device. */
struct Session
{
    ProjectController controller;
    ActionRegistry registry;
    Session()
    {
        registerCoreActions (registry);
        registerEngineActions (registry);
    }
    juce::var run (const juce::String& name, const juce::var& a = {}) { return registry.execute (controller, name, a); }
    const Project& project() const { return controller.project(); }
};

inline double rms (const float* d, int n)
{
    double s = 0;
    for (int i = 0; i < n; ++i) s += static_cast<double> (d[i]) * d[i];
    return n > 0 ? std::sqrt (s / n) : 0.0;
}

inline void sine (float* d, int n, double freq, double sr, float amp = 0.5f)
{
    for (int i = 0; i < n; ++i) d[i] = amp * static_cast<float> (std::sin (2.0 * std::numbers::pi * freq * i / sr));
}
} // namespace mcpdaw::test
