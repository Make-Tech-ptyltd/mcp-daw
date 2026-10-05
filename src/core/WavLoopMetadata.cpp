// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "WavLoopMetadata.h"
#include "MusicalTime.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace mcpdaw
{
namespace
{
[[noreturn]] void fail (const juce::String& message) { throw std::runtime_error (message.toStdString()); }

constexpr juce::uint32 kOneShot = 0x01, kRootSet = 0x02, kStretch = 0x04, kDiskBased = 0x08, kHighOctave = 0x10;
constexpr juce::uint32 kAcidPayloadBytes = 24;

struct Chunk
{
    char id[5] {};             // NUL-terminated for convenience
    juce::int64 offset = 0;    // file offset of the 8-byte chunk header
    juce::uint32 size = 0;     // payload size as stored
    bool padPresent = false;   // odd-sized payloads are followed by one pad byte (absent on some final chunks)

    bool is (const char* name) const noexcept { return std::memcmp (id, name, 4) == 0; }
    juce::int64 payload() const noexcept { return offset + 8; }
};

struct RiffScan
{
    juce::String problem;      // empty = conventional RIFF/WAVE we can rewrite
    std::vector<Chunk> chunks;
    juce::int64 riffEnd = 0, fileSize = 0;
    int fmtIndex = -1, dataIndex = -1, acidIndex = -1;
};

juce::uint16 le16 (const juce::uint8* p) noexcept { return static_cast<juce::uint16> (p[0] | (p[1] << 8)); }
juce::uint32 le32 (const juce::uint8* p) noexcept
{
    return static_cast<juce::uint32> (p[0]) | (static_cast<juce::uint32> (p[1]) << 8) | (static_cast<juce::uint32> (p[2]) << 16)
         | (static_cast<juce::uint32> (p[3]) << 24);
}
void put16 (juce::uint8* p, juce::uint16 v) noexcept { p[0] = static_cast<juce::uint8> (v); p[1] = static_cast<juce::uint8> (v >> 8); }
void put32 (juce::uint8* p, juce::uint32 v) noexcept
{
    for (int i = 0; i < 4; ++i) p[i] = static_cast<juce::uint8> (v >> (8 * i));
}
float bitsToFloat (juce::uint32 bits) noexcept { float f; std::memcpy (&f, &bits, 4); return f; }
juce::uint32 floatToBits (float f) noexcept { juce::uint32 b; std::memcpy (&b, &f, 4); return b; }

bool readExactly (juce::InputStream& in, void* dest, int bytes) { return in.read (dest, bytes) == bytes; }

/** Walk the RIFF structure. Reports (does not throw) anything this editor will not touch. */
RiffScan scanRiff (juce::InputStream& in)
{
    RiffScan s;
    s.fileSize = in.getTotalLength();
    juce::uint8 header[12];
    if (s.fileSize < 12 || ! in.setPosition (0) || ! readExactly (in, header, 12)) { s.problem = "file is too small to be a WAV file"; return s; }

    if (std::memcmp (header, "RF64", 4) == 0 || std::memcmp (header, "BW64", 4) == 0)
    { s.problem = "RF64/BW64 WAV files (used for audio over 4 GB) are not supported"; return s; }
    if (std::memcmp (header, "riff", 4) == 0) { s.problem = "Sony Wave64 files are not supported"; return s; }
    if (std::memcmp (header, "RIFX", 4) == 0) { s.problem = "big-endian RIFX files are not supported"; return s; }
    if (std::memcmp (header, "RIFF", 4) != 0 || std::memcmp (header + 8, "WAVE", 4) != 0)
    { s.problem = "not a RIFF/WAVE file"; return s; }

    s.riffEnd = 8 + static_cast<juce::int64> (le32 (header + 4));
    if (s.riffEnd > s.fileSize) { s.problem = "the RIFF size is larger than the file (truncated or malformed file)"; return s; }

    juce::int64 pos = 12;
    while (pos + 8 <= s.riffEnd)
    {
        juce::uint8 ch[8];
        if (! in.setPosition (pos) || ! readExactly (in, ch, 8)) { s.problem = "read error while walking chunks"; return s; }
        Chunk c;
        std::memcpy (c.id, ch, 4);
        c.offset = pos;
        c.size = le32 (ch + 4);
        const juce::int64 payloadEnd = pos + 8 + static_cast<juce::int64> (c.size);
        if (payloadEnd > s.riffEnd)
        { s.problem = "chunk '" + juce::String (c.id) + "' runs past the end of the RIFF data (malformed file)"; return s; }
        juce::int64 next = payloadEnd;
        if ((c.size & 1u) != 0)
        {
            if (payloadEnd + 1 <= s.riffEnd) { c.padPresent = true; next = payloadEnd + 1; }
            else c.padPresent = false;   // final odd chunk written without its pad byte: tolerated
        }
        for (int i = 0; i < 4; ++i)
            if (static_cast<unsigned char> (c.id[i]) < 0x20 || static_cast<unsigned char> (c.id[i]) > 0x7e)
            { s.problem = "invalid chunk id at offset " + juce::String (pos) + " (malformed file)"; return s; }

        const int index = static_cast<int> (s.chunks.size());
        if (c.is ("fmt ")) { if (s.fmtIndex >= 0) { s.problem = "more than one 'fmt ' chunk"; return s; } s.fmtIndex = index; }
        if (c.is ("data")) { if (s.dataIndex >= 0) { s.problem = "more than one 'data' chunk"; return s; } s.dataIndex = index; }
        if (c.is ("acid") && s.acidIndex < 0) s.acidIndex = index;
        s.chunks.push_back (c);
        pos = next;
    }
    if (pos != s.riffEnd) { s.problem = "unexpected bytes at the end of the RIFF data (malformed file)"; return s; }
    if (s.fmtIndex < 0) { s.problem = "no 'fmt ' chunk"; return s; }
    if (s.dataIndex < 0) { s.problem = "no 'data' chunk"; return s; }
    return s;
}

LoopMetadata parseAcid (juce::InputStream& in, const Chunk& c)
{
    juce::uint8 p[kAcidPayloadBytes] {};   // short chunks read as zero-filled (as JUCE does)
    in.setPosition (c.payload());
    in.read (p, static_cast<int> (std::min<juce::uint32> (c.size, kAcidPayloadBytes)));
    LoopMetadata m;
    const auto flags = le32 (p);
    m.oneShot = (flags & kOneShot) != 0;
    m.stretch = (flags & kStretch) != 0;
    m.diskBased = (flags & kDiskBased) != 0;
    m.highOctave = (flags & kHighOctave) != 0;
    if ((flags & kRootSet) != 0) m.rootNote = static_cast<int> (le16 (p + 4));
    m.reserved1 = le16 (p + 6);
    m.reserved2 = bitsToFloat (le32 (p + 8));
    m.beats = static_cast<int> (le32 (p + 12));
    m.denominator = le16 (p + 16);
    m.numerator = le16 (p + 18);
    const float tempo = bitsToFloat (le32 (p + 20));
    m.bpm = std::isfinite (tempo) ? static_cast<double> (tempo) : 0.0;
    return m;
}

void encodeAcid (const LoopMetadata& m, juce::uint8 (&p)[kAcidPayloadBytes])
{
    juce::uint32 flags = 0;
    if (m.oneShot) flags |= kOneShot;
    if (m.rootNote) flags |= kRootSet;
    if (m.stretch && ! m.oneShot) flags |= kStretch;
    if (m.diskBased) flags |= kDiskBased;
    if (m.highOctave) flags |= kHighOctave;
    put32 (p, flags);
    put16 (p + 4, static_cast<juce::uint16> (m.rootNote.value_or (0)));
    put16 (p + 6, m.reserved1);
    put32 (p + 8, floatToBits (m.reserved2));
    put32 (p + 12, static_cast<juce::uint32> (std::max (0, m.beats)));
    put16 (p + 16, static_cast<juce::uint16> (m.denominator));
    put16 (p + 18, static_cast<juce::uint16> (m.numerator));
    put32 (p + 20, floatToBits (static_cast<float> (m.bpm)));
}

WavLoopInfo describe (juce::InputStream& in, const RiffScan& s)
{
    WavLoopInfo info;
    if (s.fmtIndex < 0 || s.dataIndex < 0)   // nothing more to say
    {
        info.problem = s.problem;
        return info;
    }
    info.supported = s.problem.isEmpty();
    info.problem = s.problem;
    const auto& fmt = s.chunks[static_cast<size_t> (s.fmtIndex)];
    juce::uint8 f[16] {};
    in.setPosition (fmt.payload());
    in.read (f, static_cast<int> (std::min<juce::uint32> (fmt.size, 16)));
    info.channels = le16 (f + 2);
    info.sampleRate = le32 (f + 4);
    const int blockAlign = le16 (f + 12);
    info.bitsPerSample = le16 (f + 14);
    info.dataBytes = s.chunks[static_cast<size_t> (s.dataIndex)].size;
    if (blockAlign > 0 && info.sampleRate > 0)
        info.durationSeconds = static_cast<double> (info.dataBytes / blockAlign) / info.sampleRate;
    if (s.acidIndex >= 0)
    {
        info.hasAcidChunk = true;
        info.metadata = parseAcid (in, s.chunks[static_cast<size_t> (s.acidIndex)]);
    }
    return info;
}

juce::String sha256Of (juce::InputStream& in, juce::int64 offset, juce::int64 bytes)
{
    if (! in.setPosition (offset)) fail ("read error while hashing audio data");
    return juce::SHA256 (in, bytes).toHexString();
}

/** Copy `bytes` from `in` (at `offset`) to `out`. */
void copyBytes (juce::InputStream& in, juce::int64 offset, juce::int64 bytes, juce::OutputStream& out)
{
    if (! in.setPosition (offset)) fail ("read error while copying");
    std::vector<char> buffer (1 << 20);
    while (bytes > 0)
    {
        const int n = static_cast<int> (std::min<juce::int64> (bytes, static_cast<juce::int64> (buffer.size())));
        if (in.read (buffer.data(), n) != n) fail ("read error while copying");
        if (! out.write (buffer.data(), static_cast<size_t> (n))) fail ("write error while copying (disk full?)");
        bytes -= n;
    }
}

bool sameMetadata (const LoopMetadata& a, const LoopMetadata& b)
{
    return a.oneShot == b.oneShot && a.stretch == (b.stretch && ! b.oneShot) && a.beats == b.beats && a.numerator == b.numerator
        && a.denominator == b.denominator && a.rootNote == b.rootNote && std::abs (a.bpm - static_cast<float> (b.bpm)) < 1e-3;
}
} // namespace

juce::String midiNoteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note > 127) return "?";
    return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
}

juce::String validateLoopMetadata (const LoopMetadata& m)
{
    if (! m.oneShot)
    {
        if (! std::isfinite (m.bpm) || m.bpm < kMinBpm || m.bpm > kMaxBpm)
            return "Tempo must be between " + juce::String (kMinBpm, 0) + " and " + juce::String (kMaxBpm, 0) + " BPM for a loop";
        if (m.beats < 1 || m.beats > 65535) return "Length must be 1 to 65535 beats for a loop";
    }
    else
    {
        if (! std::isfinite (m.bpm) || m.bpm < 0.0 || m.bpm > kMaxBpm) return "Tempo must be 0 to " + juce::String (kMaxBpm, 0) + " BPM";
        if (m.beats < 0 || m.beats > 65535) return "Length must be 0 to 65535 beats";
    }
    if (m.numerator < 1 || m.numerator > 32) return "Time signature numerator must be 1 to 32";
    if (! (m.denominator == 1 || m.denominator == 2 || m.denominator == 4 || m.denominator == 8 || m.denominator == 16 || m.denominator == 32))
        return "Time signature denominator must be 1, 2, 4, 8, 16 or 32";
    if (m.rootNote && (*m.rootNote < 0 || *m.rootNote > 127)) return "Root note must be a MIDI note 0 to 127";
    return {};
}

WavLoopInfo readWavLoopMetadata (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk()) fail ("Cannot open " + file.getFullPathName());
    return describe (in, scanRiff (in));
}

WavLoopWriteResult writeWavLoopMetadata (const juce::File& source, const juce::File& destination, const LoopMetadata& metaIn)
{
    if (const auto problem = validateLoopMetadata (metaIn); problem.isNotEmpty()) fail (problem);
    if (! source.existsAsFile()) fail ("File not found: " + source.getFullPathName());
    if (! destination.getParentDirectory().isDirectory()) fail ("Folder does not exist: " + destination.getParentDirectory().getFullPathName());

    WavLoopWriteResult result;
    result.file = destination;
    juce::TemporaryFile temp (destination);
    {
        juce::FileInputStream in (source);
        if (! in.openedOk()) fail ("Cannot open " + source.getFullPathName());
        const auto scan = scanRiff (in);
        if (scan.problem.isNotEmpty()) fail ("Cannot write loop metadata to " + source.getFileName() + ": " + scan.problem);
        result.bytesBefore = scan.fileSize;

        // Unedited flags/reserved fields of an existing chunk are carried over.
        LoopMetadata meta = metaIn;
        if (scan.acidIndex >= 0)
        {
            const auto old = parseAcid (in, scan.chunks[static_cast<size_t> (scan.acidIndex)]);
            meta.diskBased = old.diskBased;
            meta.highOctave = old.highOctave;
            meta.reserved1 = old.reserved1;
            meta.reserved2 = old.reserved2;
        }
        juce::uint8 acid[kAcidPayloadBytes];
        encodeAcid (meta, acid);
        result.createdChunk = scan.acidIndex < 0;

        const auto& data = scan.chunks[static_cast<size_t> (scan.dataIndex)];
        result.dataSha256 = sha256Of (in, data.payload(), data.size);

        // New RIFF size: every chunk we keep (8-byte header + payload + pad) plus "WAVE".
        auto paddedSize = [] (juce::uint32 size) { return 8 + static_cast<juce::int64> (size) + (size & 1u); };
        juce::int64 riffSize = 4;
        for (int i = 0; i < static_cast<int> (scan.chunks.size()); ++i)
        {
            const auto& c = scan.chunks[static_cast<size_t> (i)];
            if (c.is ("acid") && i != scan.acidIndex) continue;   // duplicate acid chunks are dropped
            riffSize += c.is ("acid") ? paddedSize (kAcidPayloadBytes) : paddedSize (c.size);
        }
        if (result.createdChunk) riffSize += paddedSize (kAcidPayloadBytes);
        if (riffSize > 0xffffffffLL) fail ("The result would exceed the 4 GB RIFF limit");

        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk()) fail ("Cannot write " + temp.getFile().getFullPathName());
        juce::uint8 header[12];
        std::memcpy (header, "RIFF", 4);
        put32 (header + 4, static_cast<juce::uint32> (riffSize));
        std::memcpy (header + 8, "WAVE", 4);
        out.write (header, 12);

        auto writeAcid = [&]
        {
            juce::uint8 h[8];
            std::memcpy (h, "acid", 4);
            put32 (h + 4, kAcidPayloadBytes);
            out.write (h, 8);
            out.write (acid, kAcidPayloadBytes);
        };
        for (int i = 0; i < static_cast<int> (scan.chunks.size()); ++i)
        {
            const auto& c = scan.chunks[static_cast<size_t> (i)];
            if (c.is ("acid"))
            {
                if (i == scan.acidIndex) writeAcid();
                continue;
            }
            if (i == scan.dataIndex && result.createdChunk) writeAcid();   // new metadata goes before the audio, like ACID does
            copyBytes (in, c.offset, 8 + static_cast<juce::int64> (c.size), out);   // header + payload, verbatim
            if ((c.size & 1u) != 0)
            {
                juce::uint8 pad = 0;
                if (c.padPresent) { in.setPosition (c.payload() + c.size); in.read (&pad, 1); }
                out.write (&pad, 1);
            }
        }
        if (scan.fileSize > scan.riffEnd) copyBytes (in, scan.riffEnd, scan.fileSize - scan.riffEnd, out);   // trailing bytes, verbatim
        out.flush();
        if (out.getStatus().failed()) fail ("Write error: " + out.getStatus().getErrorMessage());
    }

    // Verify the temporary file before it replaces anything.
    {
        juce::FileInputStream check (temp.getFile());
        if (! check.openedOk()) fail ("Cannot re-open the written file for verification");
        const auto scan = scanRiff (check);
        if (scan.problem.isNotEmpty()) fail ("Verification failed (" + scan.problem + "); the original file was not changed");
        const auto& data = scan.chunks[static_cast<size_t> (scan.dataIndex)];
        if (sha256Of (check, data.payload(), data.size) != result.dataSha256)
            fail ("Verification failed: audio data changed; the original file was not changed");
        result.written = describe (check, scan);
        result.bytesAfter = scan.fileSize;
        if (! result.written.hasAcidChunk || ! sameMetadata (result.written.metadata, metaIn))
            fail ("Verification failed: metadata did not read back; the original file was not changed");
    }
    {
        // ...and the decoder MCP DAW itself uses must still open it and see the same tempo.
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (temp.getFile().createInputStream().release(), true));
        if (reader == nullptr) fail ("Verification failed: the written file does not decode; the original file was not changed");
        const auto tempo = reader->metadataValues.getValue (juce::WavAudioFormat::acidTempo, "0").getDoubleValue();
        if (std::abs (tempo - static_cast<float> (metaIn.bpm)) > 1e-3)
            fail ("Verification failed: decoder reads a different tempo; the original file was not changed");
    }

    if (! temp.overwriteTargetFileWithTemporary())
        fail ("Could not replace " + destination.getFullPathName() + " (is it open in another program?)");
    return result;
}
} // namespace mcpdaw
