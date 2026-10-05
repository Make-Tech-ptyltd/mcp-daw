#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// "Embed Loop Metadata": read and write ACID-compatible loop metadata (the RIFF 'acid' chunk) in WAV
// files WITHOUT decoding or re-encoding the audio.
//
// The writer is a small RIFF/WAVE chunk editor:
//   1. validate a conventional RIFF/WAVE file (RF64, BW64, Wave64 and anything malformed are refused);
//   2. walk its chunks;
//   3. replace the payload of an existing 'acid' chunk, or insert one just before 'data';
//   4. copy every other chunk - including the PCM 'data' chunk - byte for byte;
//   5. write the RIFF size correctly;
//   6. write to a temporary file next to the destination, re-open it, check the metadata reads back
//      (with this parser AND the JUCE decoder the app uses) and that the 'data' chunk's SHA-256 is
//      unchanged, and only then move it into place.
// A failure at any step leaves the destination untouched.
//
// Chunk layout (24 bytes, little endian), as used by ACID-compatible tools and read by JUCE:
//   uint32 flags  (0x01 one-shot, 0x02 root note set, 0x04 stretch, 0x08 disk-based, 0x10 high octave)
//   uint16 rootNote (MIDI note)   uint16 reserved   float reserved
//   uint32 numBeats   uint16 meterDenominator   uint16 meterNumerator   float tempo (BPM)

#include <juce_core/juce_core.h>
#include <optional>

namespace mcpdaw
{
struct LoopMetadata
{
    bool oneShot = false;          // one-shot: plays once at its own speed, no tempo
    bool stretch = true;           // loop may be tempo-matched by the host
    double bpm = 0.0;              // tempo the material was played at (0 for one-shots)
    int beats = 0;                 // musical length in beats
    int numerator = 4, denominator = 4;
    std::optional<int> rootNote;   // MIDI note 0..127, or none
    // Flags we don't edit but preserve when updating an existing chunk.
    bool diskBased = false, highOctave = false;
    juce::uint16 reserved1 = 0x8000;
    float reserved2 = 0.0f;

    /** beats * 60 / bpm (0 when not a tempo-based loop). */
    double expectedSeconds() const noexcept { return (! oneShot && bpm > 0.0 && beats > 0) ? beats * 60.0 / bpm : 0.0; }
};

struct WavLoopInfo
{
    bool supported = false;        // conventional RIFF/WAVE that this editor can rewrite
    juce::String problem;          // why not, when ! supported
    bool hasAcidChunk = false;
    LoopMetadata metadata;         // the chunk's contents (defaults when absent)
    double durationSeconds = 0.0;  // from 'fmt ' + 'data' sizes
    double sampleRate = 0.0;
    int channels = 0, bitsPerSample = 0;
    juce::int64 dataBytes = 0;
};

/** Inspect a WAV file. Never throws for format problems (they are reported in `problem`); throws
    std::runtime_error only if the file cannot be read at all. */
WavLoopInfo readWavLoopMetadata (const juce::File&);

struct WavLoopWriteResult
{
    juce::File file;
    bool createdChunk = false;     // false = an existing 'acid' chunk was replaced
    juce::String dataSha256;       // of the PCM 'data' chunk payload, identical before and after
    juce::int64 bytesBefore = 0, bytesAfter = 0;
    WavLoopInfo written;           // read back from the result
};

/** Write `meta` into `source`, saving the result as `destination` (which may be `source` itself for
    an in-place update; the replacement is atomic). Validates `meta` (tempo 20..300 for loops, beats
    1..65535, meter, root note). Throws std::runtime_error with a specific message on any failure. */
WavLoopWriteResult writeWavLoopMetadata (const juce::File& source, const juce::File& destination, const LoopMetadata& meta);

/** Validation used by the writer (exposed for the dialog). Empty string = valid. */
juce::String validateLoopMetadata (const LoopMetadata&);

/** Human readable MIDI note, e.g. 60 -> "C4". */
juce::String midiNoteName (int note);
} // namespace mcpdaw
