// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Engine tests: sample-accurate placement, looping, mixing, transport — all without an audio device.
#include "TestSupport.h"
#include "engine/AudioEngine.h"

namespace mcpdaw::test
{
namespace
{
int firstNonZero (const juce::AudioBuffer<float>& b, int ch = 0, float threshold = 1e-6f)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (ch, i)) > threshold) return i;
    return -1;
}

/** Placement tests render with hard edges (Default declick time 0 ms) so an impulse on an event's
    very first sample survives; the declick/fade envelope itself is covered by FadeTests. */
RenderOptions opts (double sr = 48000, double tail = 0)
{
    RenderOptions o;
    o.sampleRate = sr;
    o.tailSeconds = tail;
    o.declickMs = 0.0;
    return o;
}
} // namespace

class RenderTests final : public juce::UnitTest
{
public:
    RenderTests() : juce::UnitTest ("Offline render", "Engine") {}

    void runTest() override
    {
        const auto impulse = impulseWav ("impulse.wav");   // 1 s, impulse at 0

        beginTest ("an event at beat 3 starts exactly at 1.5 s (120 BPM)");
        Session s;
        s.controller.setDefaultDeclickMs (0.0);   // render_mix below uses the session preference
        s.run ("import_audio", args ({ { "path", impulse.getFullPathName() }, { "startBeat", 3 }, { "lengthBeats", 1 } }));
        auto out = renderToBuffer (s.project(), opts());
        expectEquals (firstNonZero (out), 72000);
        expectWithinAbsoluteError (out.getSample (0, 72000), 1.0f, 1e-6f);
        expectWithinAbsoluteError (out.getSample (1, 72000), 1.0f, 1e-6f);   // mono source feeds both sides

        beginTest ("looping: a 1 s loop stretched over 4 s repeats 4 times");
        Session l;
        l.run ("import_audio", args ({ { "path", impulse.getFullPathName() }, { "lengthBeats", 8 } }));
        l.run ("set_event_looping", args ({ { "eventId", "event-3" }, { "enabled", true } }));   // 1 s untagged = one-shot by default
        auto loops = renderToBuffer (l.project(), opts());
        int hits = 0;
        for (int i = 0; i < loops.getNumSamples(); ++i)
            if (loops.getSample (0, i) > 0.5f) { expectEquals (i % 48000, 0); ++hits; }
        expectEquals (hits, 4);

        beginTest ("non-looping event plays once");
        l.run ("set_event_looping", args ({ { "eventId", "event-3" }, { "enabled", false } }));
        auto once = renderToBuffer (l.project(), opts());
        hits = 0;
        for (int i = 0; i < once.getNumSamples(); ++i) if (once.getSample (0, i) > 0.5f) ++hits;
        expectEquals (hits, 1);

        beginTest ("sample-rate conversion: 44.1 kHz source placed at 48 kHz");
        auto imp441 = writeWav ("impulse441.wav", 44100, 1, 44100, [] (int, int i) { return i == 0 ? 1.0f : 0.0f; });
        Session r;
        r.run ("import_audio", args ({ { "path", imp441.getFullPathName() }, { "startBeat", 2 }, { "lengthBeats", 1 } }));
        auto conv = renderToBuffer (r.project(), opts());
        expectEquals (firstNonZero (conv), 48000);

        beginTest ("pitch: +12 plays an octave up (half the period) and a looped impulse repeats twice as often");
        {
            // 100 Hz sine, 1 s. At +12 the rendered sine should be 200 Hz -> 400 zero crossings per second.
            auto sine = writeWav ("sine100.wav", 48000, 1, 48000, [] (int, int i)
                                  { return 0.5f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * i / 48000.0)); });
            Session ps;
            ps.run ("import_audio", args ({ { "path", sine.getFullPathName() }, { "lengthBeats", 1 } }));   // 0.5 s window
            ps.run ("set_event_pitch", args ({ { "eventIds", juce::Array<juce::var> { "event-3" } }, { "semitones", 12 } }));
            auto pitched = renderToBuffer (ps.project(), opts());
            int crossings = 0;
            for (int i = 1000; i < 23000; ++i)   // skip the declick edges; 22000 samples = 0.4583 s
                if ((pitched.getSample (0, i - 1) < 0) != (pitched.getSample (0, i) < 0)) ++crossings;
            expectWithinAbsoluteError (crossings, static_cast<int> (22000.0 / 48000.0 * 400.0), 2);

            Session pl;
            pl.run ("import_audio", args ({ { "path", impulse.getFullPathName() }, { "lengthBeats", 8 } }));
            pl.run ("set_event_looping", args ({ { "eventId", "event-3" }, { "enabled", true } }));
            pl.run ("set_event_pitch", args ({ { "eventIds", juce::Array<juce::var> { "event-3" } }, { "semitones", 12 } }));
            auto fast = renderToBuffer (pl.project(), opts());
            int peaks = 0;
            for (int i = 0; i < fast.getNumSamples(); ++i)
                if (fast.getSample (0, i) > 0.3f) { expectEquals (i % 24000, 0); ++peaks; }
            expectEquals (peaks, 8, "1 s loop at +12 = 0.5 s cycle over 4 s");
        }

        beginTest ("gain, pan, mute and solo");
        const auto dc = dcWav ("dc.wav", 0.5f);
        Session m;
        m.run ("import_audio", args ({ { "path", dc.getFullPathName() }, { "lengthBeats", 2 } }));
        m.run ("import_audio", args ({ { "path", dc.getFullPathName() }, { "lengthBeats", 2 } }));
        const auto t1 = m.project().tracks[0].id, t2 = m.project().tracks[1].id;
        auto sampleAt = [&] (int ch) { auto b = renderToBuffer (m.project(), opts()); return b.getSample (ch, 24000); };
        expectWithinAbsoluteError (sampleAt (0), 1.0f, 1e-5f);                        // two tracks summed
        m.run ("set_track_gain", args ({ { "trackId", t2 }, { "db", -6.0206 } }));
        expectWithinAbsoluteError (sampleAt (0), 0.75f, 1e-4f);
        m.run ("set_track_pan", args ({ { "trackId", t1 }, { "pan", 1.0 } }));        // balance law: L of t1 silent
        expectWithinAbsoluteError (sampleAt (0), 0.25f, 1e-4f);
        expectWithinAbsoluteError (sampleAt (1), 0.75f, 1e-4f);
        m.run ("solo_track", args ({ { "trackId", t2 }, { "enabled", true } }));
        expectWithinAbsoluteError (sampleAt (1), 0.25f, 1e-4f);
        m.run ("mute_track", args ({ { "trackId", t2 }, { "enabled", true } }));
        expectWithinAbsoluteError (sampleAt (1), 0.0f, 1e-6f);

        beginTest ("render to WAV file via the render_mix action");
        auto wavOut = scratchDir().getChildFile ("mix.wav");
        auto res = s.run ("render_mix", args ({ { "path", wavOut.getFullPathName() }, { "tailSeconds", 0.5 } }));
        expect (wavOut.existsAsFile());
        auto back = loadAudioFile (wavOut);
        expectEquals (back->numFrames(), 48000 * 2 + 24000);   // 4 beats at 120 + 0.5 s tail
        expectThrows (s.run ("render_mix", args ({ { "path", wavOut.getFullPathName() } })));   // exists, no overwrite
        expectWithinAbsoluteError (static_cast<double> (res["peakDb"]), 0.0, 0.01);

        beginTest ("render is deterministic (effects included)");
        s.run ("add_effect", args ({ { "target", "master" }, { "type", "reverb" } }));
        s.run ("add_effect", args ({ { "target", "master" }, { "type", "distortion" } }));
        s.run ("add_effect", args ({ { "target", s.project().tracks[0].id }, { "type", "multitap_delay" } }));
        auto a = renderToBuffer (s.project(), opts (48000, 1.0));
        auto b = renderToBuffer (s.project(), opts (48000, 1.0));
        expect (a.getNumSamples() == b.getNumSamples()
                && std::memcmp (a.getReadPointer (0), b.getReadPointer (0), sizeof (float) * static_cast<size_t> (a.getNumSamples())) == 0
                && std::memcmp (a.getReadPointer (1), b.getReadPointer (1), sizeof (float) * static_cast<size_t> (a.getNumSamples())) == 0);
    }
};

class LiveEngineTests final : public juce::UnitTest
{
public:
    LiveEngineTests() : juce::UnitTest ("Live engine", "Engine") {}

    /** Drive the device callback by hand, like a 48 kHz device with `block`-sized buffers. */
    static void pump (AudioEngine& engine, juce::AudioBuffer<float>& out, int block)
    {
        juce::AudioIODeviceCallbackContext ctx;
        for (int s = 0; s < out.getNumSamples(); s += block)
        {
            const int n = std::min (block, out.getNumSamples() - s);
            float* chans[2] = { out.getWritePointer (0) + s, out.getWritePointer (1) + s };
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, chans, 2, n, ctx);
        }
    }

    void runTest() override
    {
        const auto impulse = impulseWav ("impulse_live.wav");
        Session s;
        s.controller.setDefaultDeclickMs (0.0);   // hard edges: impulses on an event's first sample stay intact
        AudioEngine engine (s.controller);   // registers as transport + listener
        s.run ("import_audio", args ({ { "path", impulse.getFullPathName() }, { "startBeat", 1 }, { "lengthBeats", 1 } }));

        beginTest ("stopped engine outputs silence, playing engine outputs the event at the right time");
        juce::AudioBuffer<float> out (2, 48000);
        pump (engine, out, 256);
        expectEquals (out.getMagnitude (0, out.getNumSamples()), 0.0f);
        s.run ("play");
        pump (engine, out, 441);                        // odd block size on purpose
        expectEquals (firstNonZero (out), 24000);
        expectWithinAbsoluteError (engine.positionBeats(), 2.0, 1e-9);

        beginTest ("stop returns to where playback started; seek is sample accurate");
        s.run ("stop");
        pump (engine, out, 256);
        expectWithinAbsoluteError (engine.positionBeats(), 0.0, 1e-9);
        s.run ("seek", args ({ { "beat", 0.5 } }));
        s.run ("play");
        pump (engine, out, 2048);                       // larger than the renderer block: split internally
        expectEquals (firstNonZero (out), 12000);

        beginTest ("loop region wraps playback");
        s.run ("stop");
        s.run ("set_loop_region", args ({ { "startBeat", 1 }, { "endBeat", 1.5 }, { "enabled", true } }));
        s.run ("seek", args ({ { "beat", 1 } }));
        s.run ("play");
        juce::AudioBuffer<float> looped (2, 48000);
        pump (engine, looped, 512);
        int hits = 0;
        for (int i = 0; i < looped.getNumSamples(); ++i)
            if (looped.getSample (0, i) > 0.5f) { expectEquals (i % 12000, 0); ++hits; }
        expectEquals (hits, 4);                         // 0.25 s loop in 1 s

        beginTest ("tempo change keeps the musical position");
        s.run ("pause");
        s.run ("seek", args ({ { "beat", 3 } }));
        pump (engine, out, 256);
        s.run ("set_project_tempo", args ({ { "bpm", 90 } }));
        pump (engine, out, 256);
        expectWithinAbsoluteError (engine.positionBeats(), 3.0, 1e-6);

        beginTest ("meters report track and master peaks");
        s.run ("set_loop_region", args ({ { "enabled", false } }));
        s.run ("seek", args ({ { "beat", 0 } }));
        s.run ("play");
        engine.takeMasterPeak();
        pump (engine, out, 512);
        const auto peak = engine.takeTrackPeak (s.project().tracks[0].id);
        expectWithinAbsoluteError (peak.left, 1.0f, 1e-6f);
        expectWithinAbsoluteError (engine.takeMasterPeak().right, 1.0f, 1e-6f);
        expect (! engine.clipped());

        beginTest ("describeState is machine readable");
        auto st = s.run ("get_transport_state");
        expect (static_cast<bool> (st["playing"]));
        expectEquals (st["bpm"].operator double(), 90.0);
    }
};

static RenderTests renderTests;
static LiveEngineTests liveEngineTests;
} // namespace mcpdaw::test
