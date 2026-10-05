// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// "Embed Loop Metadata": the RIFF/WAVE 'acid' chunk writer must never alter the audio.
#include "TestSupport.h"
#include "core/WavLoopMetadata.h"

namespace mcpdaw::test
{
namespace
{
/** Independent minimal chunk finder (deliberately not the code under test): payload of chunk `id`. */
juce::MemoryBlock chunkPayload (const juce::File& f, const char* id, int* count = nullptr)
{
    juce::MemoryBlock all;
    f.loadFileAsData (all);
    const auto* b = static_cast<const juce::uint8*> (all.getData());
    juce::MemoryBlock found;
    int n = 0;
    for (size_t pos = 12; pos + 8 <= all.getSize();)
    {
        const juce::uint32 size = b[pos + 4] | (b[pos + 5] << 8) | (b[pos + 6] << 16) | (static_cast<juce::uint32> (b[pos + 7]) << 24);
        if (std::memcmp (b + pos, id, 4) == 0)
        {
            if (n++ == 0) found = juce::MemoryBlock (b + pos + 8, std::min<size_t> (size, all.getSize() - pos - 8));
        }
        pos += 8 + size + (size & 1u);
    }
    if (count != nullptr) *count = n;
    return found;
}

/** A conventional 16-bit PCM WAV with a non-trivial signal, optionally with an odd-sized custom chunk. */
juce::File makePcmWav (const juce::String& name, int frames = 22050)
{
    auto file = scratchDir().getChildFile (name);
    file.deleteFile();
    juce::AudioBuffer<float> buffer (2, frames);
    juce::Random rng (1234);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i) buffer.setSample (c, i, 0.8f * (rng.nextFloat() * 2.0f - 1.0f));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate (44100).withNumChannels (2).withBitsPerSample (16);
    auto writer = wav.createWriterFor (stream, options);
    writer->writeFromAudioSampleBuffer (buffer, 0, frames);
    return file;
}

/** Append a chunk at the end of the RIFF (fixing the RIFF size), e.g. an odd-sized "zzzz". */
void appendChunk (const juce::File& f, const char* id, const juce::MemoryBlock& payload)
{
    juce::MemoryBlock all;
    f.loadFileAsData (all);
    juce::MemoryOutputStream out;
    out.write (all.getData(), all.getSize());
    out.write (id, 4);
    out.writeInt (static_cast<int> (payload.getSize()));
    out.write (payload.getData(), payload.getSize());
    if ((payload.getSize() & 1u) != 0) out.writeByte (0);
    auto data = out.getMemoryBlock();
    const auto riff = static_cast<juce::uint32> (data.getSize() - 8);
    auto* p = static_cast<juce::uint8*> (data.getData());
    for (int i = 0; i < 4; ++i) p[4 + i] = static_cast<juce::uint8> (riff >> (8 * i));
    f.replaceWithData (data.getData(), data.getSize());
}

LoopMetadata loop (double bpm, int beats)
{
    LoopMetadata m;
    m.bpm = bpm;
    m.beats = beats;
    return m;
}
} // namespace

class WavMetadataTests final : public juce::UnitTest
{
public:
    WavMetadataTests() : juce::UnitTest ("Embed loop metadata", "Editing") {}

    void runTest() override
    {
        beginTest ("write to a copy: metadata reads back (ours and the decoder's); PCM data byte-identical");
        const auto source = makePcmWav ("meta_source.wav", 44100 / 2);   // 0.5 s
        appendChunk (source, "zzzz", juce::MemoryBlock ("abc", 3));      // odd-sized foreign chunk must survive
        const auto pcmBefore = chunkPayload (source, "data");
        const auto sourceBytes = source.loadFileAsString();
        juce::MemoryBlock sourceAll;
        source.loadFileAsData (sourceAll);
        expect (! readWavLoopMetadata (source).hasAcidChunk);
        expect (readWavLoopMetadata (source).supported);
        expectWithinAbsoluteError (readWavLoopMetadata (source).durationSeconds, 0.5, 1e-9);

        const auto copy = scratchDir().getChildFile ("meta_copy.wav");
        auto meta = loop (120.0, 1);   // 1 beat at 120 = 0.5 s
        meta.rootNote = 57;
        auto result = writeWavLoopMetadata (source, copy, meta);
        expect (result.createdChunk);
        expect (chunkPayload (copy, "data") == pcmBefore, "PCM data chunk byte-for-byte unchanged");
        expect (chunkPayload (copy, "zzzz") == juce::MemoryBlock ("abc", 3), "foreign chunks preserved");
        juce::MemoryBlock afterSource;
        source.loadFileAsData (afterSource);
        expect (afterSource == sourceAll, "Save Copy leaves the source untouched");

        const auto info = readWavLoopMetadata (copy);
        expect (info.hasAcidChunk);
        expectEquals (info.metadata.bpm, 120.0);
        expectEquals (info.metadata.beats, 1);
        expectEquals (info.metadata.numerator, 4);
        expectEquals (info.metadata.denominator, 4);
        expect (! info.metadata.oneShot && info.metadata.stretch);
        expectEquals (info.metadata.rootNote.value_or (-1), 57);
        expectWithinAbsoluteError (info.metadata.expectedSeconds(), info.durationSeconds, 1e-9);
        const auto decoded = loadAudioFile (copy);
        expectEquals (decoded->detectedTempo.bpm, 120.0);
        expectEquals (decoded->detectedTempo.source, juce::String ("acid-chunk"));
        const auto original = loadAudioFile (source);
        bool samePcm = decoded->numFrames() == original->numFrames();
        for (int c = 0; c < 2 && samePcm; ++c)
            samePcm = std::memcmp (decoded->pcm.getReadPointer (c), original->pcm.getReadPointer (c), sizeof (float) * static_cast<size_t> (original->numFrames())) == 0;
        expect (samePcm, "decoded audio identical");
        // RIFF size field is correct.
        juce::MemoryBlock all;
        copy.loadFileAsData (all);
        const auto* b = static_cast<const juce::uint8*> (all.getData());
        const juce::uint32 riff = b[4] | (b[5] << 8) | (b[6] << 16) | (static_cast<juce::uint32> (b[7]) << 24);
        expectEquals (static_cast<juce::int64> (riff) + 8, static_cast<juce::int64> (all.getSize()));
        juce::ignoreUnused (sourceBytes);

        beginTest ("update existing metadata in place: chunk replaced (not duplicated), PCM unchanged");
        auto changed = loop (92.0, 16);
        changed.oneShot = false;
        result = writeWavLoopMetadata (copy, copy, changed);
        expect (! result.createdChunk);
        int acidChunks = 0;
        chunkPayload (copy, "acid", &acidChunks);
        expectEquals (acidChunks, 1);
        expect (chunkPayload (copy, "data") == pcmBefore);
        expectEquals (result.bytesAfter, result.bytesBefore, "same-size replacement");
        expectEquals (readWavLoopMetadata (copy).metadata.bpm, 92.0);
        expect (readWavLoopMetadata (copy).metadata.rootNote == std::nullopt, "root note cleared");

        beginTest ("one-shot flag is honoured by the importer");
        LoopMetadata shot;
        shot.oneShot = true;
        writeWavLoopMetadata (copy, copy, shot);
        expect (loadAudioFile (copy)->detectedTempo.oneShot);
        expect (chunkPayload (copy, "data") == pcmBefore);

        beginTest ("safe failure: invalid metadata, unsupported or malformed files leave everything untouched");
        juce::MemoryBlock beforeFail;
        copy.loadFileAsData (beforeFail);
        expectThrows (writeWavLoopMetadata (copy, copy, loop (0.0, 4)));      // a loop needs a tempo
        expectThrows (writeWavLoopMetadata (copy, copy, loop (120.0, 0)));    // and a length
        auto badMeter = loop (120.0, 4);
        badMeter.denominator = 3;
        expectThrows (writeWavLoopMetadata (copy, copy, badMeter));
        juce::MemoryBlock afterFail;
        copy.loadFileAsData (afterFail);
        expect (afterFail == beforeFail);
        const auto neverMade = scratchDir().getChildFile ("never.wav");
        expectThrows (writeWavLoopMetadata (copy, neverMade, loop (0.0, 4)));
        expect (! neverMade.exists());

        auto rf64 = scratchDir().getChildFile ("big.wav");
        juce::MemoryBlock rf (beforeFail);
        std::memcpy (rf.getData(), "RF64", 4);
        rf64.replaceWithData (rf.getData(), rf.getSize());
        expect (! readWavLoopMetadata (rf64).supported);
        expect (readWavLoopMetadata (rf64).problem.contains ("RF64"));
        expectThrows (writeWavLoopMetadata (rf64, rf64, loop (120.0, 4)));
        juce::MemoryBlock rfAfter;
        rf64.loadFileAsData (rfAfter);
        expect (rfAfter == rf);

        auto truncated = scratchDir().getChildFile ("truncated.wav");
        truncated.replaceWithData (beforeFail.getData(), beforeFail.getSize() / 2);
        expect (! readWavLoopMetadata (truncated).supported);
        expectThrows (writeWavLoopMetadata (truncated, truncated, loop (120.0, 4)));
        auto text = scratchDir().getChildFile ("notawav.wav");
        text.replaceWithText ("hello, this is not audio at all");
        expectThrows (writeWavLoopMetadata (text, text, loop (120.0, 4)));
        expectEquals (text.loadFileAsString(), juce::String ("hello, this is not audio at all"));

        beginTest ("MCP: get/write_wav_loop_metadata with explicit update intent and a duration check");
        Session s;
        const auto mcpFile = makePcmWav ("meta_mcp.wav", 44100);         // 1.0 s
        auto info2 = s.run ("get_wav_loop_metadata", args ({ { "path", mcpFile.getFullPathName() } }));
        expect (! static_cast<bool> (info2["hasLoopMetadata"]));
        expect (static_cast<bool> (info2["supported"]));
        expectThrows (s.run ("write_wav_loop_metadata", args ({ { "path", mcpFile.getFullPathName() }, { "bpm", 120 }, { "beats", 2 } })));   // no overwrite
        auto written = s.run ("write_wav_loop_metadata", args ({ { "path", mcpFile.getFullPathName() }, { "bpm", 120 }, { "beats", 2 },
                                                                { "overwrite", true } }));
        expect (static_cast<bool> (written["updatedInPlace"]));
        expect (static_cast<bool> (written["durationMatches"]));
        expectEquals (written["metadata"]["type"].toString(), juce::String ("loop"));
        auto mismatch = s.run ("write_wav_loop_metadata", args ({ { "path", mcpFile.getFullPathName() }, { "bpm", 100 }, { "overwrite", true } }));
        expect (! static_cast<bool> (mismatch["durationMatches"]), "2 beats at 100 BPM = 1.2 s vs 1.0 s: reported, not 'fixed'");
        expectEquals (static_cast<int> (mismatch["metadata"]["beats"]), 2, "unspecified fields keep the file's values");
        expectWithinAbsoluteError (static_cast<double> (mismatch["durationDifferenceSeconds"]), -0.2, 1e-6);
        const auto mcpCopy = scratchDir().getChildFile ("meta_mcp_copy.wav");
        mcpCopy.deleteFile();
        auto copied = s.run ("write_wav_loop_metadata", args ({ { "path", mcpFile.getFullPathName() }, { "outputPath", mcpCopy.getFullPathName() },
                                                               { "type", "one-shot" }, { "rootNote", 60 } }));
        expect (! static_cast<bool> (copied["updatedInPlace"]));
        expectEquals (copied["metadata"]["rootNoteName"].toString(), juce::String ("C4"));
        expectThrows (s.run ("write_wav_loop_metadata", args ({ { "path", mcpFile.getFullPathName() }, { "outputPath", mcpCopy.getFullPathName() },
                                                                { "type", "one-shot" } })));   // copy exists, no overwrite
        expectThrows (s.run ("write_wav_loop_metadata", args ({ { "path", rf64.getFullPathName() }, { "bpm", 120 }, { "beats", 4 }, { "overwrite", true } })));
        expectEquals (midiNoteName (69), juce::String ("A4"));
    }
};

static WavMetadataTests wavMetadataTests;
} // namespace mcpdaw::test
