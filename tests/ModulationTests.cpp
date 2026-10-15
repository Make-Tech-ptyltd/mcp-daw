// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Tremolo, chorus and phaser: waveform conventions, timeline/event phase, smoothing, block-size
// independence, engine integration (event onsets, split, seek, loop) and the MCP surface (presets,
// compact responses, persistence).
#include "TestSupport.h"
#include "core/EffectPresets.h"
#include "dsp/Chorus.h"
#include "dsp/Phaser.h"
#include "dsp/Tremolo.h"
#include "engine/PlanRenderer.h"

namespace mcpdaw::test
{
namespace
{
constexpr double kSr = 48000.0;
constexpr double kBpm = 120.0;
constexpr std::int64_t kBeat = 24000;   // samples per beat at 120 BPM, 48 kHz

float choiceIndex (const char* type, const char* param, const char* name)
{
    const auto* d = findEffectType (type);
    return static_cast<float> (d->params[static_cast<size_t> (d->indexOf (param))].choices.indexOf (name));
}

std::unique_ptr<EffectProcessor> makeFx (const char* type, std::initializer_list<std::pair<const char*, float>> values)
{
    auto fx = createEffectProcessor (type);
    for (const auto& [id, v] : values) fx->setParameter (fx->descriptor().indexOf (id), v);
    fx->prepare (kSr, 1024);
    return fx;
}

/** Run `in` through `fx` as if the transport played from timeline sample `start`, in blocks of `block`. */
juce::AudioBuffer<float> play (EffectProcessor& fx, const juce::AudioBuffer<float>& in, std::int64_t start, int block,
                               const std::vector<std::int64_t>& onsets = {})
{
    juce::AudioBuffer<float> out (in);
    for (int s = 0; s < out.getNumSamples(); s += block)
    {
        const int n = std::min (block, out.getNumSamples() - s);
        const TimelineSpan span { start + s, 0, n };
        const ProcessContext ctx { kSr, kBpm, true, &span, 1, onsets.data(), static_cast<int> (onsets.size()) };
        fx.process (out.getWritePointer (0) + s, out.getWritePointer (1) + s, n, ctx);
    }
    return out;
}

juce::AudioBuffer<float> constant (int frames, float value = 1.0f)
{
    juce::AudioBuffer<float> b (2, frames);
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (b.getWritePointer (c), value, frames);
    return b;
}

juce::AudioBuffer<float> noise (int frames, int seed = 1)
{
    juce::Random rng (seed);
    juce::AudioBuffer<float> b (2, frames);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i) b.setSample (c, i, rng.nextFloat() * 2.0f - 1.0f);
    return b;
}

float maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0, int bOffset = 0)
{
    float m = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < a.getNumSamples() && i - bOffset < b.getNumSamples(); ++i)
            m = std::max (m, std::abs (a.getSample (c, i) - b.getSample (c, i - bOffset)));
    return m;
}

/** Expected tremolo phase at timeline sample t. */
double phaseAt (std::int64_t t, std::int64_t anchor, double cycleBeats, double phaseDeg)
{
    const double ph = static_cast<double> (t - anchor) / (cycleBeats * kBeat) + phaseDeg / 360.0;
    return ph - std::floor (ph);
}
} // namespace

// ================================================================================================
class TremoloTests final : public juce::UnitTest
{
public:
    TremoloTests() : juce::UnitTest ("Tremolo", "DSP") {}

    void runTest() override
    {
        using modulation::Shape;
        beginTest ("waveform conventions: phase 0 loudest, 180 deg quietest, linear gain from -depth dB to unity");
        for (auto shape : { Shape::Sine, Shape::Triangle, Shape::Square })
        {
            expectWithinAbsoluteError (Tremolo::waveform (shape, 0.0), 1.0f, 1e-6f);
            expectWithinAbsoluteError (Tremolo::waveform (shape, 0.5), 0.0f, 1e-6f);
            expectWithinAbsoluteError (Tremolo::waveform (shape, 1.0), 1.0f, 1e-6f);
            expectWithinAbsoluteError (Tremolo::gainAt (shape, 60.0f, 0.5), 0.001f, 1e-6f);
            expectWithinAbsoluteError (Tremolo::gainAt (shape, 20.0f, 0.0), 1.0f, 1e-6f);
            expectEquals (Tremolo::gainAt (shape, 0.0f, 0.5), 1.0f);
        }
        expectWithinAbsoluteError (Tremolo::waveform (Shape::Sine, 0.25), 0.5f, 1e-6f);
        expectWithinAbsoluteError (Tremolo::waveform (Shape::Triangle, 0.25), 0.5f, 1e-6f);
        expectEquals (Tremolo::waveform (Shape::Square, 0.25), 1.0f);
        expectEquals (Tremolo::waveform (Shape::Square, 0.75), 0.0f);
        // depth 6 dB: quietest point is half amplitude
        expectWithinAbsoluteError (Tremolo::gainAt (Shape::Sine, 6.0206f, 0.5), 0.5f, 1e-4f);

        const float div16 = choiceIndex ("tremolo", "division", "1/16"), div4 = choiceIndex ("tremolo", "division", "1/4");
        const float sine = 0.0f, square = 2.0f, event = 1.0f;

        beginTest ("follows the timeline: gain at every sample is the analytic waveform (1/16, sine)");
        {
            auto fx = makeFx ("tremolo", { { "division", div16 }, { "shape", sine }, { "depth", 24.0f } });
            auto out = play (*fx, constant (96000), 0, 512);
            float worst = 0;
            for (int i = 0; i < out.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (out.getSample (0, i) - Tremolo::gainAt (Shape::Sine, 24.0f, phaseAt (i, 0, 0.25, 0))));
            expectLessThan (worst, 1e-5f);
            expectEquals (out.getSample (0, 0), out.getSample (1, 0));
        }

        beginTest ("starting playback mid-cycle reconstructs the phase (no restart at the play position)");
        {
            auto fx = makeFx ("tremolo", { { "division", div4 }, { "shape", sine }, { "depth", 30.0f }, { "phase", 90.0f } });
            auto out = play (*fx, constant (4800), 12345, 256);
            for (int i : { 0, 1, 1000, 4799 })
                expectWithinAbsoluteError (out.getSample (0, i), Tremolo::gainAt (Shape::Sine, 30.0f, phaseAt (12345 + i, 0, 1.0, 90)), 1e-5f);
        }

        beginTest ("event restart: each onset starts a new cycle; timeline mode ignores onsets");
        {
            const std::vector<std::int64_t> onsets { 0, 36000 };   // the second onset falls mid-cycle (timeline phase 0.5)
            auto ev = makeFx ("tremolo", { { "division", div4 }, { "shape", square }, { "depth", 60.0f }, { "restart", event } });
            auto tl = makeFx ("tremolo", { { "division", div4 }, { "shape", square }, { "depth", 60.0f } });
            auto evOut = play (*ev, constant (60000), 0, 1024, onsets);
            auto tlOut = play (*tl, constant (60000), 0, 1024, onsets);
            expectWithinAbsoluteError (evOut.getSample (0, 36000 + 200), 1.0f, 1e-6f);     // restarted: loud
            expectWithinAbsoluteError (tlOut.getSample (0, 36000 + 200), 0.001f, 1e-6f);   // timeline: quiet half
            expectWithinAbsoluteError (evOut.getSample (0, 36000 + 12200), 0.001f, 1e-6f); // half a cycle after the onset
            expectWithinAbsoluteError (evOut.getSample (0, 30000), tlOut.getSample (0, 30000), 1e-7f);   // same before it

            // Starting playback inside the second event: the phase comes from that event's onset.
            auto mid = makeFx ("tremolo", { { "division", div4 }, { "shape", sine }, { "depth", 30.0f }, { "restart", event } });
            auto midOut = play (*mid, constant (2000), 40000, 333, onsets);
            expectWithinAbsoluteError (midOut.getSample (0, 0), Tremolo::gainAt (Shape::Sine, 30.0f, phaseAt (40000, 36000, 1.0, 0)), 1e-6f);
        }

        beginTest ("square edges, restarts and parameter jumps are slew-limited to ~1.5 ms ramps");
        {
            auto fx = makeFx ("tremolo", { { "division", div16 }, { "shape", square }, { "depth", 60.0f }, { "restart", event } });
            auto out = play (*fx, constant (48000), 0, 512, { 0, 7777, 20011 });
            const float limit = static_cast<float> (1000.0 / (Tremolo::kEdgeMs * kSr)) + 1e-6f;
            float steepest = 0;
            for (int i = 1; i < out.getNumSamples(); ++i)
                steepest = std::max (steepest, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            expectLessThan (steepest, limit);
            expectGreaterThan (steepest, limit * 0.9f);   // ...but the edge is still sharp
            expectWithinAbsoluteError (out.getSample (0, 4500), 0.001f, 1e-6f);   // phase 0.75: reaches the trough mid-half
        }

        beginTest ("depth 0 is a bit-exact pass-through");
        {
            auto fx = makeFx ("tremolo", { { "depth", 0.0f }, { "shape", square } });
            const auto in = noise (20000);
            expectEquals (maxDiff (play (*fx, in, 0, 512), in), 0.0f);
        }

        beginTest ("a loop wrap inside a block follows the timeline on both sides");
        {
            auto fx = makeFx ("tremolo", { { "division", div4 }, { "shape", sine }, { "depth", 30.0f } });
            auto b = constant (1024);
            const TimelineSpan spans[] = { { 5 * kBeat - 100, 0, 100 }, { 3 * kBeat + 7000, 100, 924 } };
            const ProcessContext ctx { kSr, kBpm, true, spans, 2 };
            fx->process (b.getWritePointer (0), b.getWritePointer (1), 1024, ctx);
            expectWithinAbsoluteError (b.getSample (0, 50), Tremolo::gainAt (Shape::Sine, 30.0f, phaseAt (5 * kBeat - 50, 0, 1.0, 0)), 1e-5f);
            expectWithinAbsoluteError (b.getSample (0, 400), Tremolo::gainAt (Shape::Sine, 30.0f, phaseAt (3 * kBeat + 7300, 0, 1.0, 0)), 1e-5f);
        }

        beginTest ("stopped (no timeline): free-runs, still modulating");
        {
            auto fx = makeFx ("tremolo", { { "division", div16 }, { "depth", 24.0f } });
            auto b = constant (12000);
            const ProcessContext stopped { kSr, kBpm, false };
            fx->process (b.getWritePointer (0), b.getWritePointer (1), 6000, stopped);   // one 1/16 cycle
            float lo = 1;
            for (int i = 0; i < 6000; ++i) lo = std::min (lo, b.getSample (0, i));
            expectLessThan (lo, 0.2f);
        }
    }
};

// ================================================================================================
class ChorusPhaserTests final : public juce::UnitTest
{
public:
    ChorusPhaserTests() : juce::UnitTest ("Chorus and phaser", "DSP") {}

    void runTest() override
    {
        beginTest ("block size never changes the output (tremolo, chorus, phaser; timeline and event restart)");
        {
            const std::vector<std::int64_t> onsets { 1000, 20000, 20001, 47000 };
            const auto in = noise (60000, 7);
            struct Case { const char* type; std::initializer_list<std::pair<const char*, float>> values; };
            const Case cases[] = {
                { "tremolo", { { "shape", 2.0f }, { "depth", 40.0f }, { "restart", 1.0f }, { "phase", 45.0f } } },
                { "chorus", { { "sync", 1.0f }, { "division", 0.0f }, { "restart", 1.0f }, { "depth", 0.8f }, { "feedback", 0.6f } } },
                { "chorus", { { "rate", 3.7f }, { "spread", 120.0f } } },
                { "phaser", { { "sync", 1.0f }, { "division", 1.0f }, { "restart", 1.0f }, { "feedback", -0.7f }, { "spread", 90.0f } } },
                { "phaser", { { "rate", 1.3f }, { "depth", 1.0f }, { "centre", 300.0f } } } };
            for (const auto& c : cases)
            {
                auto a = makeFx (c.type, c.values), b = makeFx (c.type, c.values), d = makeFx (c.type, c.values);
                const auto ref = play (*a, in, 5000, 1024, onsets);
                expectEquals (maxDiff (play (*b, in, 5000, 1, onsets), ref), 0.0f, juce::String (c.type) + " block 1");
                expectEquals (maxDiff (play (*d, in, 5000, 37, onsets), ref), 0.0f, juce::String (c.type) + " block 37");
                float m = 0;
                for (int i = 0; i < ref.getNumSamples(); ++i) m = std::max (m, std::abs (ref.getSample (0, i)));
                expect (std::isfinite (m) && m < 20.0f, juce::String (c.type) + " stays bounded");
            }
        }

        beginTest ("chorus: mix 0 is transparent; vibrato at depth 0 is a pure centre delay");
        {
            auto dry = makeFx ("chorus", { { "mix", 0.0f }, { "feedback", 0.5f } });
            const auto in = noise (8000, 3);
            expectEquals (maxDiff (play (*dry, in, 0, 512), in), 0.0f);

            auto vib = makeFx ("chorus", { { "mix", 1.0f }, { "depth", 0.0f }, { "delay", 7.0f } });
            juce::AudioBuffer<float> imp (2, 4800);
            imp.clear();
            imp.setSample (0, 0, 1.0f);
            imp.setSample (1, 0, 1.0f);
            auto out = play (*vib, imp, 0, 512);
            expectWithinAbsoluteError (out.getSample (0, 336), 1.0f, 1e-5f);   // 7 ms at 48 kHz
            expectWithinAbsoluteError (out.getSample (0, 335) + out.getSample (0, 337), 0.0f, 1e-5f);
        }

        beginTest ("chorus: the same timeline position gives the same output (bounce == playback, seek)");
        {
            const auto in = noise (96000, 11);
            auto full = makeFx ("chorus", { { "sync", 1.0f }, { "division", 2.0f }, { "depth", 0.6f }, { "spread", 90.0f } });
            auto late = makeFx ("chorus", { { "sync", 1.0f }, { "division", 2.0f }, { "depth", 0.6f }, { "spread", 90.0f } });
            const auto a = play (*full, in, 0, 512);
            juce::AudioBuffer<float> tail (2, 96000 - 30000);
            for (int c = 0; c < 2; ++c) tail.copyFrom (c, 0, in, c, 30000, tail.getNumSamples());
            const auto b = play (*late, tail, 30000, 777);
            // Once the 65 ms delay line has filled with the same input, the outputs are identical.
            expectEquals (maxDiff (a, b, 30000 + 3200, 30000), 0.0f);
        }

        beginTest ("phaser: cutoff mapping matches juce::dsp::Phaser (log axis 20 Hz - 20 kHz)");
        expectWithinAbsoluteError (Phaser::cutoffHz (1000.0, 0.0, 1.0), 1000.0, 1e-6);
        expectWithinAbsoluteError (Phaser::cutoffHz (1000.0, 1.0, 0.5), 1000.0 * std::pow (10.0, 0.75), 1e-6);   // depth 1 = +/-1.5 decades
        expectWithinAbsoluteError (Phaser::cutoffHz (1000.0, 0.5, -1.0), 1000.0 / std::pow (10.0, 0.75), 1e-6);
        expectWithinAbsoluteError (Phaser::cutoffHz (10000.0, 1.0, 1.0), 20000.0, 1e-6);   // clamped to the axis

        beginTest ("phaser: a static sweep notches its centre frequency (6 all-passes = -540 deg there)");
        {
            auto fx = makeFx ("phaser", { { "depth", 0.0f }, { "centre", 1000.0f }, { "feedback", 0.0f }, { "mix", 0.5f } });
            juce::AudioBuffer<float> b (2, 48000);
            sine (b.getWritePointer (0), 48000, 1000.0, kSr);
            sine (b.getWritePointer (1), 48000, 1000.0, kSr);
            const double inRms = rms (b.getReadPointer (0) + 24000, 24000);
            const auto out = play (*fx, b, 0, 512);
            expectLessThan (20.0 * std::log10 (rms (out.getReadPointer (0) + 24000, 24000) / inRms), -40.0);

            auto dry = makeFx ("phaser", { { "mix", 0.0f } });
            const auto in = noise (8000, 5);
            expectEquals (maxDiff (play (*dry, in, 0, 512), in), 0.0f);
        }

        beginTest ("phaser: bounce == playback once the all-pass state has settled");
        {
            const auto in = noise (96000, 13);
            auto full = makeFx ("phaser", { { "sync", 1.0f }, { "feedback", 0.0f }, { "depth", 0.8f } });
            auto late = makeFx ("phaser", { { "sync", 1.0f }, { "feedback", 0.0f }, { "depth", 0.8f } });
            const auto a = play (*full, in, 0, 512);
            juce::AudioBuffer<float> tail (2, 96000 - 30000);
            for (int c = 0; c < 2; ++c) tail.copyFrom (c, 0, in, c, 30000, tail.getNumSamples());
            const auto b = play (*late, tail, 30000, 777);
            expectLessThan (maxDiff (a, b, 30000 + 24000, 30000), 1e-4f);
        }
    }
};

// ================================================================================================
class ModulationEngineTests final : public juce::UnitTest
{
public:
    ModulationEngineTests() : juce::UnitTest ("Modulation in the engine", "Engine") {}

    static RenderOptions opts()
    {
        RenderOptions o;
        o.tailSeconds = 0;
        o.declickMs = 0;   // hard edges: the DC level shows the tremolo gain directly
        return o;
    }

    static std::vector<float> renderBlocks (const Project& p, int block, int total)
    {
        RuntimeRegistry registry (kSr, PlanRenderer::kMaxBlock);
        auto plan = PlaybackPlan::build (p, registry, 0.0);
        PlanRenderer renderer;
        juce::AudioBuffer<float> b (2, PlanRenderer::kMaxBlock);
        std::vector<float> out (static_cast<size_t> (total) * 2);   // left samples, then right samples
        std::int64_t pos = 0;
        for (int done = 0; done < total;)
        {
            const int n = std::min (block, total - done);
            pos = renderer.render (*plan, pos, true, b.getWritePointer (0), b.getWritePointer (1), n).newPosition;
            std::copy (b.getReadPointer (0), b.getReadPointer (0) + n, out.begin() + done);
            std::copy (b.getReadPointer (1), b.getReadPointer (1) + n, out.begin() + total + done);
            done += n;
        }
        return out;
    }

    void runTest() override
    {
        const auto dc = dcWav ("dc-8s.wav", 0.5f, 48000, 48000 * 8);
        Session s;
        s.controller.setDefaultDeclickMs (0.0);
        const auto imported = s.run ("import_audio", args ({ { "path", dc.getFullPathName() }, { "lengthBeats", 2.75 } }));
        const auto track = imported["trackId"].toString(), asset = imported["assetId"].toString();
        const auto e2 = s.run ("create_event", args ({ { "trackId", track }, { "assetId", asset }, { "startBeat", 2.75 },
                                                       { "lengthBeats", 3.0 }, { "looping", false } }))["id"].toString();
        const auto trem = s.run ("add_effect", args ({ { "target", track }, { "type", "tremolo" },
            { "parameters", args ({ { "division", "1/4" }, { "shape", "Sine" }, { "depth", 60 }, { "restart", "Event" } }) } }))["id"].toString();

        beginTest ("event onsets: each event start restarts the cycle in the bounce");
        auto full = renderToBuffer (s.project(), opts());
        const int onset = 66000;   // beat 2.75
        auto expected = [&] (int t, std::int64_t anchor) { return 0.5f * Tremolo::gainAt (modulation::Shape::Sine, 60.0f, phaseAt (t, anchor, 1.0, 0)); };
        expectWithinAbsoluteError (full.getSample (0, onset - 10), expected (onset - 10, 0), 1e-5f);
        expectWithinAbsoluteError (full.getSample (0, onset + 100), expected (onset + 100, onset), 1e-5f);
        expectWithinAbsoluteError (full.getSample (0, onset + 30000), expected (onset + 30000, onset), 1e-5f);

        beginTest ("plan onsets: split halves are one note; a back-to-back one-shot copy is a new one");
        {
            RuntimeRegistry registry (kSr, 1024);
            expect (PlaybackPlan::build (s.project(), registry)->tracks[0].onsets == std::vector<std::int64_t> { 0, onset });
        }
        s.run ("split_event", args ({ { "eventId", e2 }, { "beat", 4.1 } }));
        {
            RuntimeRegistry registry (kSr, 1024);
            expect (PlaybackPlan::build (s.project(), registry)->tracks[0].onsets == std::vector<std::int64_t> { 0, onset });
            const auto split = renderToBuffer (s.project(), opts());
            expectLessThan (maxDiff (split, full), 1e-6f);
        }
        s.run ("create_event", args ({ { "trackId", track }, { "assetId", asset }, { "startBeat", 5.75 }, { "lengthBeats", 1.0 }, { "looping", false } }));
        {
            RuntimeRegistry registry (kSr, 1024);
            expect (PlaybackPlan::build (s.project(), registry)->tracks[0].onsets == std::vector<std::int64_t> { 0, onset, 138000 });
        }
        s.run ("undo");
        s.run ("undo");

        beginTest ("a bounce starting mid-event equals the same samples of the full bounce");
        {
            auto o = opts();
            o.start = beatsToTicks (3.5);
            const auto part = renderToBuffer (s.project(), o);
            expectLessThan (maxDiff (full, part, 84000, 84000), 1e-7f);
        }

        beginTest ("live rendering: any block size, with loop wraps, gives identical audio (tremolo + chorus + phaser)");
        {
            s.run ("add_effect", args ({ { "target", track }, { "type", "chorus" }, { "parameters", args ({ { "restart", "Event" }, { "feedback", 0.4 } }) } }));
            s.run ("add_effect", args ({ { "target", "master" }, { "type", "phaser" }, { "parameters", args ({ { "sync", true }, { "division", "1/4" } }) } }));
            s.run ("set_loop_region", args ({ { "startBeat", 1.0 }, { "endBeat", 3.3 }, { "enabled", true } }));
            const auto a = renderBlocks (s.project(), 1024, 150000);
            const auto b = renderBlocks (s.project(), 61, 150000);
            size_t first = 0;
            while (first < a.size() && a[first] == b[first]) ++first;
            expect (first == a.size(), "block 61 == block 1024; first difference at index " + juce::String ((juce::int64) first)
                                       + " (" + juce::String (first < a.size() ? a[first] - b[first] : 0.0f) + ")");
        }

        beginTest ("the LFO never edits the project, dirties it or adds undo steps");
        {
            const auto before = juce::JSON::toString (projectToFileJson (s.project(), {}));
            const auto labels = s.controller.historyLabels();
            const bool dirty = s.controller.isDirty();
            renderToBuffer (s.project(), opts());
            renderBlocks (s.project(), 256, 50000);
            expectEquals (juce::JSON::toString (projectToFileJson (s.project(), {})), before);
            expect (s.controller.historyLabels() == labels);
            expect (s.controller.isDirty() == dirty);
        }
        juce::ignoreUnused (trem);
    }
};

// ================================================================================================
class ModulationCommandTests final : public juce::UnitTest
{
public:
    ModulationCommandTests() : juce::UnitTest ("Modulation commands", "Commands") {}

    void runTest() override
    {
        Session s;
        const auto track = s.run ("create_track", args ({ { "name", "Bass" } }))["trackId"].toString();

        beginTest ("list_effect_types: one type, or brief");
        const auto one = s.run ("list_effect_types", args ({ { "type", "tremolo" } }));
        expectEquals (one.size(), 1);
        expectEquals (one[0]["parameters"].size(), 5);
        const auto brief = s.run ("list_effect_types", args ({ { "brief", true } }));
        expectEquals (brief.size(), static_cast<int> (effectCatalogue().size()));
        expect (! brief[0].hasProperty ("parameters"));
        expectEquals (s.run ("list_effect_types").size(), static_cast<int> (effectCatalogue().size()));   // unchanged default

        beginTest ("add_effect by choice names; compact responses carry only what changed");
        const auto added = s.run ("add_effect", args ({ { "target", track }, { "type", "tremolo" }, { "detail", "compact" },
            { "parameters", args ({ { "division", "1/16" }, { "shape", "Square" }, { "restart", "Event" }, { "depth", 30 } }) } }));
        const auto fxId = added["id"].toString();
        expectEquals (added["values"]["division"].toString(), juce::String ("1/16"));
        expectEquals (added["values"]["restart"].toString(), juce::String ("Event"));
        expect (! added["values"].hasProperty ("phase"));   // default -> omitted
        expect (! added.hasProperty ("parameters"));

        const auto set = s.run ("set_effect_parameters", args ({ { "effectId", fxId }, { "values", args ({ { "phase", 180 } }) }, { "detail", "compact" } }));
        expectEquals (set["set"].getDynamicObject()->getProperties().size(), 1);
        expectEquals (static_cast<double> (set["set"]["phase"]), 180.0);
        const auto fullSet = s.run ("set_effect_parameters", args ({ { "effectId", fxId }, { "values", args ({ { "depth", 36 } }) } }));
        expectEquals (fullSet["parameters"].getDynamicObject()->getProperties().size(), 5);   // default: every value, as before

        const auto compactFx = s.run ("get_effect_parameters", args ({ { "effectId", fxId }, { "detail", "compact" } }));
        expectEquals (compactFx["values"].getDynamicObject()->getProperties().size(), 5);   // all five differ from defaults now
        expect (! s.run ("get_effect_parameters", args ({ { "effectId", fxId }, { "detail", "values" } })).hasProperty ("parameterDetails"));
        expect (s.run ("get_effect_parameters", args ({ { "effectId", fxId } })).hasProperty ("parameterDetails"));

        beginTest ("get_project_state detail=compact lists non-default values only");
        s.run ("add_effect", args ({ { "target", track }, { "type", "eq" } }));
        const auto compactState = s.run ("get_project_state", args ({ { "detail", "compact" } }));
        const auto fullState = s.run ("get_project_state");
        const auto& eqCompact = compactState["tracks"][0]["effects"][1];
        expectEquals (eqCompact["values"].getDynamicObject()->getProperties().size(), 0);
        expect (fullState["tracks"][0]["effects"][1].hasProperty ("parameters"));
        expectLessThan (juce::JSON::toString (compactState).length(), juce::JSON::toString (fullState).length());

        beginTest ("add_effect_preset: ordinary effects, one undo step, every value stored");
        const auto historyBefore = s.controller.historyLabels().size();
        const auto preset = s.run ("add_effect_preset", args ({ { "target", track }, { "preset", "dubstep_pulse" }, { "index", 0 } }));
        expectEquals (preset["effectIds"].size(), 2);
        const auto* chain = &s.project().findTrack (track)->effects;
        expectEquals (static_cast<int> (chain->size()), 4);
        expectEquals ((*chain)[0].type, juce::String ("distortion"));
        expectEquals ((*chain)[1].type, juce::String ("tremolo"));
        expectEquals ((*chain)[1].values[static_cast<size_t> (findEffectType ("tremolo")->indexOf ("restart"))], 1.0f);
        expectEquals (static_cast<int> (s.controller.historyLabels().size()), static_cast<int> (historyBefore) + 1);
        s.run ("undo");
        expectEquals (static_cast<int> (s.project().findTrack (track)->effects.size()), 2);
        s.run ("redo");

        beginTest ("every factory preset instantiates with valid parameters");
        for (const auto& p : effectPresets())
        {
            Project scratch;
            const auto fx = instantiatePreset (scratch, p);
            expectEquals (fx.size(), p.chain.size(), p.id);
        }

        beginTest ("execute_batch detail=compact: ids only, $N references still resolve");
        const auto batch = s.run ("execute_batch", args ({ { "detail", "compact" }, { "actions", juce::Array<juce::var> {
            args ({ { "action", "add_effect" }, { "arguments", args ({ { "target", "master" }, { "type", "phaser" } }) } }),
            args ({ { "action", "set_effect_parameters" }, { "arguments", args ({ { "effectId", "$0.id" }, { "values", args ({ { "sync", true } }) } }) } }) } } }));
        expectEquals (batch["results"][0].getDynamicObject()->getProperties().size(), 2);   // id, type
        expect (! batch["results"][1].hasProperty ("parameters"));
        expectEquals (s.project().master.effects.back().values[0], 1.0f);

        beginTest ("save/load round trip keeps every value; the format stays v2; unknown types fail loudly");
        s.run ("add_effect", args ({ { "target", track }, { "type", "chorus" }, { "parameters", args ({ { "spread", 137.5 }, { "sync", true } }) } }));
        const auto file = scratchDir().getChildFile ("modulation.mcpdaw");
        saveProjectFile (s.project(), file);
        int version = 0;
        const auto loaded = loadProjectFile (file, {}, &version);
        expectEquals (version, Project::kFormatVersion);
        expectEquals (Project::kFormatVersion, 2);
        const auto& a = s.project().findTrack (track)->effects;
        const auto& b = loaded.findTrack (track)->effects;
        expectEquals (b.size(), a.size());
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        {
            expectEquals (b[i].type, a[i].type);
            for (size_t k = 0; k < a[i].values.size(); ++k)
                expectWithinAbsoluteError (b[i].values[k], a[i].values[k], 1e-4f);
        }
        auto text = file.loadFileAsString().replace ("\"tremolo\"", "\"tremolo_v9\"");
        file.replaceWithText (text);
        bool threw = false;
        try { loadProjectFile (file); }
        catch (const std::exception& e) { threw = juce::String (e.what()).contains ("tremolo_v9"); }
        expect (threw, "an unknown effect type is refused, never silently dropped");
    }
};

static TremoloTests tremoloTests;
static ChorusPhaserTests chorusPhaserTests;
static ModulationEngineTests modulationEngineTests;
static ModulationCommandTests modulationCommandTests;
} // namespace mcpdaw::test
