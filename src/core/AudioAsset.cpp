// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "AudioAsset.h"
#include "MusicalTime.h"
#include <cmath>
#include <stdexcept>

namespace mcpdaw
{
// ================================================================================================
// PeakPyramid
// ================================================================================================
void PeakPyramid::build (const juce::AudioBuffer<float>& pcm)
{
    numSamples = pcm.getNumSamples();
    const int channels = pcm.getNumChannels();

    // Level 0 directly from PCM (all channels merged: the timeline draws a combined envelope).
    auto& base = levels[0];
    base.assign (static_cast<size_t> ((numSamples + kBaseStride - 1) / kBaseStride), {});
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* d = pcm.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            auto& bin = base[static_cast<size_t> (i / kBaseStride)];
            bin.lo = std::min (bin.lo, d[i]);
            bin.hi = std::max (bin.hi, d[i]);
        }
    }

    // Coarser levels reduce the previous level.
    for (int level = 1; level < kNumLevels; ++level)
    {
        const auto& fine = levels[level - 1];
        auto& coarse = levels[level];
        coarse.assign ((fine.size() + kLevelFactor - 1) / kLevelFactor, {});
        for (size_t i = 0; i < fine.size(); ++i)
        {
            auto& bin = coarse[i / kLevelFactor];
            bin.lo = std::min (bin.lo, fine[i].lo);
            bin.hi = std::max (bin.hi, fine[i].hi);
        }
    }
}

PeakPyramid::MinMax PeakPyramid::query (double startSample, double endSample) const noexcept
{
    MinMax out;
    if (numSamples <= 0 || levels[0].empty()) return out;

    startSample = std::clamp (startSample, 0.0, static_cast<double> (numSamples));
    endSample   = std::clamp (endSample, startSample, static_cast<double> (numSamples));
    const double span = std::max (1.0, endSample - startSample);

    // Pick the coarsest level whose bins are still no wider than the requested span.
    int level = 0;
    double stride = kBaseStride;
    while (level + 1 < kNumLevels && stride * kLevelFactor <= span)
    {
        stride *= kLevelFactor;
        ++level;
    }

    const auto& bins = levels[level];
    auto first = static_cast<size_t> (startSample / stride);
    auto last  = static_cast<size_t> (std::max (startSample, endSample - 1.0) / stride);
    last = std::min (last, bins.size() - 1);
    first = std::min (first, last);

    out = bins[first];
    for (size_t i = first + 1; i <= last; ++i)
    {
        out.lo = std::min (out.lo, bins[i].lo);
        out.hi = std::max (out.hi, bins[i].hi);
    }
    return out;
}

size_t PeakPyramid::memoryBytes() const noexcept
{
    size_t n = 0;
    for (const auto& l : levels) n += l.size() * sizeof (MinMax);
    return n;
}

// ================================================================================================
// Tempo heuristics
// ================================================================================================
double parseBpmFromName (const juce::String& fileName)
{
    // Accept "..._90bpm", "90 BPM", "bpm90", "-128-" style tokens. Prefer explicit "bpm" tokens.
    const auto lower = fileName.toLowerCase();
    const int idx = lower.indexOf ("bpm");
    auto numberAround = [&] (int pos, bool before) -> double
    {
        if (before)
        {
            int end = pos;
            while (end > 0 && lower[end - 1] == ' ') --end;
            int start = end;
            while (start > 0 && (juce::CharacterFunctions::isDigit (lower[start - 1]) || lower[start - 1] == '.')) --start;
            return start < end ? lower.substring (start, end).getDoubleValue() : 0.0;
        }
        int start = pos;
        while (start < lower.length() && (lower[start] == ' ' || lower[start] == '_' || lower[start] == '-')) ++start;
        int end = start;
        while (end < lower.length() && (juce::CharacterFunctions::isDigit (lower[end]) || lower[end] == '.')) ++end;
        return start < end ? lower.substring (start, end).getDoubleValue() : 0.0;
    };

    if (idx >= 0)
    {
        for (double v : { numberAround (idx, true), numberAround (idx + 3, false) })
            if (v >= 40.0 && v <= 250.0) return v;
    }
    return 0.0;
}

TempoInfo estimateLoopTempo (double durationSeconds)
{
    // Loops are overwhelmingly 1, 2, 4 or 8 bars of 4/4. Choose the bar count whose implied tempo is
    // closest to 110 BPM while staying within a plausible 70..180 BPM range.
    TempoInfo best;
    if (durationSeconds < 0.5 || durationSeconds > 60.0) return best;
    double bestScore = 1e9;
    for (int bars : { 1, 2, 4, 8, 16 })
    {
        const double beats = static_cast<double> (bars * kBeatsPerBar);
        const double bpm = beats * 60.0 / durationSeconds;
        if (bpm < 70.0 || bpm > 180.0) continue;
        const double score = std::abs (std::log2 (bpm / 110.0));
        if (score < bestScore) { bestScore = score; best = { bpm, beats, false, "estimate" }; }
    }
    return best;
}

// ================================================================================================
// Loading
// ================================================================================================
namespace
{
juce::AudioFormatManager& formatManager()
{
    // Only touched from the message thread / worker threads that load files, never the audio thread.
    static juce::AudioFormatManager manager;
    static const bool registered = [] { manager.registerBasicFormats(); return true; }();   // WAV, AIFF, FLAC, Ogg
    juce::ignoreUnused (registered);
    return manager;
}

[[noreturn]] void fail (const juce::String& message) { throw std::runtime_error (message.toStdString()); }

TempoInfo readTempo (const juce::AudioFormatReader& reader, const juce::File& file, double durationSeconds)
{
    const auto& meta = reader.metadataValues;

    // 1. ACID chunk (written by ACID, Sound Forge, many sample libraries).
    const double acidTempo = meta.getValue (juce::WavAudioFormat::acidTempo, "0").getDoubleValue();
    const double acidBeats = meta.getValue (juce::WavAudioFormat::acidBeats, "0").getDoubleValue();
    const bool acidOneShot = meta.getValue (juce::WavAudioFormat::acidOneShot, "0").getIntValue() != 0;
    if (acidOneShot)
        return { 0.0, 0.0, true, "acid-chunk" };
    if (acidTempo >= kMinBpm && acidTempo <= kMaxBpm)
        return { acidTempo, acidBeats > 0 ? acidBeats : secondsToBeats (durationSeconds, acidTempo), false, "acid-chunk" };

    // 2. Tempo written in the filename, e.g. "drums_90bpm.wav".
    if (const double named = parseBpmFromName (file.getFileNameWithoutExtension()); named > 0)
        return { named, secondsToBeats (durationSeconds, named), false, "filename" };

    // 3. Nothing reliable. Callers may surface estimateLoopTempo() as a suggestion only.
    return {};
}
} // namespace

juce::StringArray supportedAudioExtensions()
{
    return { "wav", "aif", "aiff", "flac" };
}

bool isSupportedAudioFile (const juce::File& f)
{
    for (const auto& ext : supportedAudioExtensions())
        if (f.hasFileExtension (ext)) return true;
    return false;
}

AudioDataPtr loadAudioFile (const juce::File& file, const AudioLoadLimits& limits)
{
    if (! file.existsAsFile())
        fail ("Audio file not found: " + file.getFullPathName());
    if (! isSupportedAudioFile (file))
        fail ("Unsupported audio file type (supported: " + supportedAudioExtensions().joinIntoString (", ") + "): " + file.getFileName());

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));
    if (reader == nullptr)
        fail ("Could not decode audio file: " + file.getFullPathName());
    if (reader->sampleRate < 8000.0 || reader->sampleRate > 384000.0)
        fail ("Unsupported sample rate " + juce::String (reader->sampleRate) + " Hz: " + file.getFileName());
    if (reader->numChannels < 1 || reader->numChannels > 8)
        fail ("Unsupported channel count " + juce::String (reader->numChannels) + ": " + file.getFileName());
    if (reader->lengthInSamples <= 0)
        fail ("Audio file is empty: " + file.getFileName());
    if (reader->lengthInSamples > limits.maxFrames)
        fail ("Audio file is too long for in-memory playback: " + file.getFileName());

    // Multichannel files are folded to their first two channels (stereo) for now.
    const int channels = std::min (static_cast<int> (reader->numChannels), limits.maxChannels);
    const int frames = static_cast<int> (reader->lengthInSamples);

    auto data = std::make_shared<AudioData>();
    data->path = file.getFullPathName();
    data->sampleRate = reader->sampleRate;
    data->pcm.setSize (channels, frames);
    if (! reader->read (&data->pcm, 0, frames, 0, true, channels > 1))
        fail ("Read error while decoding: " + file.getFileName());

    // Sanitise: NaN/Inf in a file must never reach the mix bus.
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* d = data->pcm.getWritePointer (ch);
        for (int i = 0; i < frames; ++i)
            if (! std::isfinite (d[i])) d[i] = 0.0f;
    }

    data->peaks.build (data->pcm);
    data->detectedTempo = readTempo (*reader, file, data->durationSeconds());
    // A short file with no tempo metadata is almost always a hit (kick, snare, stab): default it to a
    // one-shot. Tagged loops keep looping however short they are. Users can still toggle per event.
    if (! data->detectedTempo.oneShot && data->detectedTempo.bpm <= 0.0 && data->durationSeconds() < kOneShotMaxSeconds)
        data->detectedTempo.oneShot = true;
    return data;
}

AudioDataPtr makeAudioData (juce::AudioBuffer<float> pcm, double sampleRate, juce::String path)
{
    auto data = std::make_shared<AudioData>();
    data->path = std::move (path);
    data->sampleRate = sampleRate;
    data->pcm = std::move (pcm);
    data->peaks.build (data->pcm);
    return data;
}
} // namespace mcpdaw
