// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "OfflineRenderer.h"
#include "../core/ProjectJson.h"
#include "PlanRenderer.h"
#include <stdexcept>

namespace mcpdaw
{
namespace
{
/** Drives PlanRenderer block by block, calling `sink` with each rendered block. */
template <typename Sink>
RenderStats runRender (const Project& project, const RenderOptions& options, Sink&& sink)
{
    if (options.sampleRate < 8000 || options.sampleRate > 384000) throw std::runtime_error ("Render sample rate out of range");
    RuntimeRegistry registry (options.sampleRate, PlanRenderer::kMaxBlock);
    auto plan = PlaybackPlan::build (project, registry);
    plan->loopEnabled = false;   // a bounce is linear

    const Tick endTick = options.end > 0 ? options.end : project.contentEnd();
    if (endTick <= options.start) throw std::runtime_error ("Nothing to render (range is empty; is the project empty?)");
    const auto startSample = ticksToSamples (options.start, project.bpm, options.sampleRate);
    const auto endSample = ticksToSamples (endTick, project.bpm, options.sampleRate)
                         + static_cast<std::int64_t> (std::max (0.0, options.tailSeconds) * options.sampleRate);

    PlanRenderer renderer;
    RenderStats stats;
    juce::AudioBuffer<float> block (2, PlanRenderer::kMaxBlock);
    std::int64_t pos = startSample;
    while (pos < endSample)
    {
        const int n = static_cast<int> (std::min<std::int64_t> (PlanRenderer::kMaxBlock, endSample - pos));
        const auto r = renderer.render (*plan, pos, true, block.getWritePointer (0), block.getWritePointer (1), n);
        stats.peak = std::max ({ stats.peak, r.masterPeakL, r.masterPeakR });
        sink (block, n);
        pos = r.newPosition;
        stats.frames += n;
    }
    stats.seconds = static_cast<double> (stats.frames) / options.sampleRate;
    stats.clipped = stats.peak > 1.0f;
    return stats;
}
} // namespace

juce::AudioBuffer<float> renderToBuffer (const Project& project, const RenderOptions& options, RenderStats* statsOut)
{
    std::vector<float> left, right;
    auto stats = runRender (project, options, [&] (const juce::AudioBuffer<float>& b, int n)
    {
        left.insert (left.end(), b.getReadPointer (0), b.getReadPointer (0) + n);
        right.insert (right.end(), b.getReadPointer (1), b.getReadPointer (1) + n);
    });
    juce::AudioBuffer<float> out (2, static_cast<int> (left.size()));
    if (! left.empty())
    {
        out.copyFrom (0, 0, left.data(), static_cast<int> (left.size()));
        out.copyFrom (1, 0, right.data(), static_cast<int> (right.size()));
    }
    if (statsOut != nullptr) *statsOut = stats;
    return out;
}

RenderStats renderToWavFile (const Project& project, const RenderOptions& options, const juce::File& file)
{
    if (! file.getParentDirectory().isDirectory())
        throw std::runtime_error (("Folder does not exist: " + file.getParentDirectory().getFullPathName()).toStdString());
    if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32)
        throw std::runtime_error ("bitDepth must be 16, 24 or 32");

    juce::TemporaryFile temp (file);
    RenderStats stats;
    {
        juce::WavAudioFormat wav;
        auto stream = temp.getFile().createOutputStream();
        if (stream == nullptr) throw std::runtime_error (("Cannot write " + temp.getFile().getFullPathName()).toStdString());
        const auto writerOptions = juce::AudioFormatWriterOptions{}
                                       .withSampleRate (options.sampleRate)
                                       .withNumChannels (2)
                                       .withBitsPerSample (options.bitDepth)
                                       .withSampleFormat (options.bitDepth == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                                 : juce::AudioFormatWriterOptions::SampleFormat::integral);
        std::unique_ptr<juce::OutputStream> out (stream.release());
        auto writer = wav.createWriterFor (out, writerOptions);
        if (writer == nullptr) throw std::runtime_error ("Could not create WAV writer");
        stats = runRender (project, options, [&] (const juce::AudioBuffer<float>& b, int n)
        {
            if (! writer->writeFromAudioSampleBuffer (b, 0, n)) throw std::runtime_error ("Write error while rendering");
        });
    }
    if (! temp.overwriteTargetFileWithTemporary())
        throw std::runtime_error (("Could not replace " + file.getFullPathName()).toStdString());
    return stats;
}

void registerEngineActions (ActionRegistry& r)
{
    r.add ({ "render_mix",
             "Bounce the mix to a WAV file (offline, deterministic, independent of live playback). Default range: start to end of "
             "content plus a tail for reverb/delay.",
             Schema().string ("path", "Absolute .wav path")
                     .number ("startBeat", "Range start (default 0)", false, 0.0)
                     .number ("endBeat", "Range end (default: end of content)", false, 0.0)
                     .number ("sampleRate", "Default 48000", false, 8000.0, 384000.0)
                     .integer ("bitDepth", "16, 24 (default) or 32 float", false, 16, 32)
                     .number ("tailSeconds", "Extra seconds after the range (default 2)", false, 0.0, 60.0)
                     .boolean ("overwrite", "Allow replacing an existing file", false).build(),
             ActionKind::File,
             [] (ActionContext& c)
             {
                 juce::File file (c.args.string ("path"));
                 if (! juce::File::isAbsolutePath (c.args.string ("path"))) c.args.fail ("path must be absolute");
                 if (! file.hasFileExtension ("wav")) file = file.withFileExtension ("wav");
                 if (file.existsAsFile() && ! c.args.optBoolean ("overwrite").value_or (false))
                     c.args.fail ("file exists: " + file.getFullPathName() + " (pass overwrite=true)");
                 RenderOptions o;
                 o.start = beatsToTicks (c.args.optNumber ("startBeat").value_or (0.0));
                 o.end = beatsToTicks (c.args.optNumber ("endBeat").value_or (0.0));
                 o.sampleRate = c.args.optNumber ("sampleRate").value_or (48000.0);
                 o.bitDepth = static_cast<int> (c.args.has ("bitDepth") ? c.args.integer ("bitDepth", 16, 32) : 24);
                 o.tailSeconds = c.args.optNumber ("tailSeconds").value_or (2.0);
                 const auto stats = renderToWavFile (c.project(), o, file);
                 return makeObject ({ { "path", file.getFullPathName() }, { "seconds", stats.seconds }, { "frames", static_cast<juce::int64> (stats.frames) },
                                      { "peakDb", gainToDb (stats.peak) }, { "clipped", stats.clipped } });
             } });
}
} // namespace mcpdaw
