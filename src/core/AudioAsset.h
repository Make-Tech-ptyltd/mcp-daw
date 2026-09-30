#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Decoded audio sources and their cached waveform peaks.
//
// AudioData is immutable once built and shared (std::shared_ptr<const AudioData>) between the
// project model, undo snapshots, the realtime playback plan and the UI. Because it never changes
// after construction, all of those can read it concurrently without locks.
//
// MCP DAW decodes the whole file into memory (fine for loops and one-shots). The loader goes through
// juce::AudioFormatManager so FLAC/AIFF/MP3 only require registering formats; long-file streaming
// is a later concern and would live behind the same AudioData read interface.

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace mcpdaw
{
/** Min/max envelope at several resolutions so waveform drawing never touches raw PCM when zoomed out. */
class PeakPyramid
{
public:
    struct MinMax { float lo = 0.0f, hi = 0.0f; };
    static constexpr int kBaseStride = 32;       // samples per bin at level 0
    static constexpr int kLevelFactor = 8;       // each level is 8x coarser
    static constexpr int kNumLevels = 4;         // 32, 256, 2048, 16384 samples per bin

    void build (const juce::AudioBuffer<float>& pcm);

    /** Min/max of all channels over [startSample, endSample). Uses the coarsest level whose bins are
        no wider than the span, so the result may include up to one span of neighbouring audio at the
        edges — fine for drawing one pixel column, not for sample-exact analysis. */
    MinMax query (double startSample, double endSample) const noexcept;

    size_t memoryBytes() const noexcept;

private:
    std::vector<MinMax> levels[kNumLevels];
    int numSamples = 0;
};

/** Tempo information found in the file (ACID chunk, filename) or estimated. */
struct TempoInfo
{
    double bpm = 0.0;           // 0 = unknown / not tempo-based (one-shot)
    double beats = 0.0;         // musical length in beats when known
    bool oneShot = false;
    juce::String source;        // "acid-chunk", "filename", "estimate", "user", or empty
};

/** Files shorter than this with no tempo metadata are treated as one-shots (placed without looping). */
inline constexpr double kOneShotMaxSeconds = 2.0;

struct AudioData
{
    juce::String path;
    double sampleRate = 0.0;
    juce::AudioBuffer<float> pcm;   // 1 or 2 channels
    PeakPyramid peaks;
    TempoInfo detectedTempo;

    int numFrames() const noexcept   { return pcm.getNumSamples(); }
    int numChannels() const noexcept { return pcm.getNumChannels(); }
    double durationSeconds() const noexcept { return sampleRate > 0 ? numFrames() / sampleRate : 0.0; }
};

using AudioDataPtr = std::shared_ptr<const AudioData>;

/** Limits that keep a malformed or huge file from exhausting memory. */
struct AudioLoadLimits
{
    juce::int64 maxFrames = 48000LL * 60 * 20;  // 20 minutes at 48 kHz
    int maxChannels = 2;
};

/** Decode a file. Throws std::runtime_error with a human/AI-readable message on failure.
    Must NOT be called on the audio thread. */
AudioDataPtr loadAudioFile (const juce::File& file, const AudioLoadLimits& limits = {});

/** Build AudioData from an in-memory buffer (tests, generated material). */
AudioDataPtr makeAudioData (juce::AudioBuffer<float> pcm, double sampleRate, juce::String path = {});

/** File extensions the loader accepts ("wav;aif;aiff;flac" ...). */
juce::StringArray supportedAudioExtensions();
bool isSupportedAudioFile (const juce::File&);

/** Tempo heuristics (exposed for tests). */
double parseBpmFromName (const juce::String& fileName);
TempoInfo estimateLoopTempo (double durationSeconds);
} // namespace mcpdaw
