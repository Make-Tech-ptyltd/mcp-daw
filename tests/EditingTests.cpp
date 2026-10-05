// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Event editing (vNext): the unified fade/declick envelope, fade curves, non-destructive reverse,
// slip editing, project-format migration and the MCP actions that expose them. Everything runs
// headless through the same action registry the GUI and MCP clients use.
#include "TestSupport.h"
#include "engine/AudioEngine.h"
#include "mcp/McpSession.h"

namespace mcpdaw::test
{
namespace
{
RenderOptions renderAt (double sr, double declickMs)
{
    RenderOptions o;
    o.sampleRate = sr;
    o.tailSeconds = 0.0;
    o.declickMs = declickMs;
    return o;
}

/** A copy of the event's current state (a copy, so later edits can't leave a dangling reference). */
Event eventIn (const Session& s, const juce::String& id)
{
    auto ref = s.project().findEvent (id);
    jassert (ref.event != nullptr);
    return ref.event != nullptr ? *ref.event : Event();
}

juce::Array<juce::var> ids (std::initializer_list<const char*> list)
{
    juce::Array<juce::var> a;
    for (auto* x : list) a.add (juce::String (x));
    return a;
}

/** Session with one track holding one event of the given file (returned id is always "event-3"). */
juce::String placeEvent (Session& s, const juce::File& wav, double lengthBeats, bool looping)
{
    auto r = s.run ("import_audio", args ({ { "path", wav.getFullPathName() }, { "lengthBeats", lengthBeats } }));
    const auto id = r["eventId"].toString();
    s.run ("set_event_looping", args ({ { "eventId", id }, { "enabled", looping } }));
    return id;
}
} // namespace

// ================================================================================================
class FadeTests final : public juce::UnitTest
{
public:
    FadeTests() : juce::UnitTest ("Event fades and declick", "Editing") {}

    void runTest() override
    {
        beginTest ("curve maths: deterministic 0..1 shapes, mirrored for fade-outs");
        for (auto c : { FadeCurve::Linear, FadeCurve::Smooth, FadeCurve::Fast, FadeCurve::Slow })
        {
            expectEquals (fadeShape (c, 0.0f), 0.0f);
            expectEquals (fadeShape (c, 1.0f), 1.0f);
            expectEquals (fadeInGain (c, 0, 100), 0.0f, "first sample of a fade-in is silent");
            expectEquals (fadeOutGain (c, 0, 100), 0.0f, "last sample of a fade-out is silent");
            expectEquals (fadeInGain (c, 100, 100), 1.0f);
            expectEquals (fadeInGain (c, 5, 0), 1.0f, "no fade");
            float previous = -1.0f;
            for (int k = 0; k <= 100; ++k) { const float g = fadeInGain (c, k, 100); expect (g >= previous); previous = g; }
            expect (fadeCurveFromName (fadeCurveName (c)).value_or (FadeCurve::Linear) == c);
        }
        expectWithinAbsoluteError (fadeShape (FadeCurve::Linear, 0.5f), 0.5f, 1e-6f);
        expectWithinAbsoluteError (fadeShape (FadeCurve::Smooth, 0.5f), 0.5f, 1e-6f);
        expectWithinAbsoluteError (fadeShape (FadeCurve::Fast, 0.5f), 0.75f, 1e-6f);
        expectWithinAbsoluteError (fadeShape (FadeCurve::Slow, 0.5f), 0.25f, 1e-6f);
        expectWithinAbsoluteError (fadeOutGain (FadeCurve::Fast, 50, 100), 0.25f, 1e-6f);   // fast fade-out drops early
        expectWithinAbsoluteError (fadeOutGain (FadeCurve::Slow, 50, 100), 0.75f, 1e-6f);   // slow fade-out holds
        expect (! fadeCurveFromName ("wobbly").has_value());
        expect (fadeCurveFromName ("SMOOTH").value_or (FadeCurve::Linear) == FadeCurve::Smooth);

        beginTest ("unified rule: effective fade = max (event fade, declick)");
        expectEquals (effectiveFadeSamples (0, declickSamples (1.5, 48000)), static_cast<std::int64_t> (72));
        expectEquals (effectiveFadeSamples (24000, declickSamples (1.5, 48000)), static_cast<std::int64_t> (24000));
        expectEquals (effectiveFadeSamples (0, declickSamples (0.0, 48000)), static_cast<std::int64_t> (0));
        expectEquals (effectiveFadeSamples (24, declickSamples (1.5, 48000)), static_cast<std::int64_t> (72));

        const auto dc = dcWav ("fade_dc.wav", 0.5f);           // 1 s of 0.5, stereo, 48 kHz

        beginTest ("default event: fade 0 + declick 1.5 ms -> 1.5 ms effective fade at both edges");
        {
            Session s;
            const auto id = placeEvent (s, dc, 1.0, false);    // 0.5 s = 24000 samples at 120 BPM
            expectEquals (eventIn (s, id).fadeInTicks, static_cast<Tick> (0), "nothing stored");
            auto out = renderToBuffer (s.project(), renderAt (48000, 1.5));
            expectEquals (out.getSample (0, 0), 0.0f);
            expectWithinAbsoluteError (out.getSample (0, 36), 0.25f, 1e-6f);   // half way up the 72-sample ramp
            expectWithinAbsoluteError (out.getSample (0, 72), 0.5f, 1e-6f);
            expect (out.getSample (0, 71) < 0.5f);
            expectWithinAbsoluteError (out.getSample (0, 23999), 0.0f, 1e-6f);  // last sample of the event
            expectWithinAbsoluteError (out.getSample (0, 23999 - 36), 0.25f, 1e-6f);
            expectWithinAbsoluteError (out.getSample (0, 23999 - 72), 0.5f, 1e-6f);
            auto state = s.run ("get_project_state");
            expectWithinAbsoluteError (static_cast<double> (state["tracks"][0]["events"][0]["effectiveFadeInMs"]), 1.5, 1e-9);
            expectWithinAbsoluteError (static_cast<double> (state["editing"]["defaultDeclickMs"]), 1.5, 1e-12);
        }

        beginTest ("musical fade: 500 ms fade + 1.5 ms declick -> 500 ms effective");
        {
            Session s;
            const auto id = placeEvent (s, dc, 2.0, false);    // 1 s
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 1.0 } }));   // 1 beat = 500 ms
            auto out = renderToBuffer (s.project(), renderAt (48000, 1.5));
            expectWithinAbsoluteError (out.getSample (0, 12000), 0.25f, 1e-5f);
            expectWithinAbsoluteError (out.getSample (0, 24000), 0.5f, 1e-6f);
            juce::ignoreUnused (id);
        }

        beginTest ("declick 0 ms and fade 0 -> a genuinely hard edge");
        {
            Session s;
            placeEvent (s, dc, 1.0, false);
            auto out = renderToBuffer (s.project(), renderAt (48000, 0.0));
            expectEquals (out.getSample (0, 0), 0.5f);
            expectEquals (out.getSample (0, 23999), 0.5f);
        }

        beginTest ("fade shorter than the declick (0.5 ms vs 1.5 ms) -> 1.5 ms effective");
        {
            Session s;
            s.run ("set_project_tempo", args ({ { "bpm", 125.0 } }));   // 1 tick = exactly 0.5 ms
            placeEvent (s, dc, 1.0, true);
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 1.0 / 960.0 } }));
            expectEquals (eventIn (s, "event-3").fadeInTicks, static_cast<Tick> (1));
            auto out = renderToBuffer (s.project(), renderAt (48000, 1.5));
            expectWithinAbsoluteError (out.getSample (0, 36), 0.25f, 1e-6f);   // the 72-sample declick, not a 24-sample fade
            expectWithinAbsoluteError (out.getSample (0, 72), 0.5f, 1e-6f);
        }

        beginTest ("declick is compiled per sample rate");
        const std::pair<double, int> rates[] = { { 44100.0, 66 }, { 48000.0, 72 }, { 96000.0, 144 } };
        for (const auto& [sr, expected] : rates)
        {
            Session s;
            placeEvent (s, dc, 1.0, true);
            auto out = renderToBuffer (s.project(), renderAt (sr, 1.5));
            expect (out.getSample (0, expected - 1) < 0.5f, "still ramping at " + juce::String (sr));
            expectWithinAbsoluteError (out.getSample (0, expected), 0.5f, 1e-5f);
            expectWithinAbsoluteError (out.getSample (0, expected / 2), 0.25f, 0.004f);
        }

        beginTest ("each curve renders its shape; fade-in and fade-out curves are independent");
        for (auto c : { FadeCurve::Linear, FadeCurve::Smooth, FadeCurve::Fast, FadeCurve::Slow })
        {
            Session s;
            placeEvent (s, dc, 4.0, true);                     // 2 s = 96000 samples
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 1.0 }, { "fadeOutBeats", 1.0 },
                                             { "fadeInCurve", fadeCurveName (c) }, { "fadeOutCurve", "slow" } }));
            auto out = renderToBuffer (s.project(), renderAt (48000, 1.5));
            expectWithinAbsoluteError (out.getSample (0, 12000), 0.5f * fadeShape (c, 0.5f), 1e-5f);
            expectWithinAbsoluteError (out.getSample (1, 95999 - 12000), 0.5f * fadeOutGain (FadeCurve::Slow, 12000, 24000), 1e-5f);
        }

        beginTest ("overlapping fades multiply");
        {
            Session s;
            placeEvent (s, dc, 2.0, true);                     // 48000 samples
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 2.0 }, { "fadeOutBeats", 2.0 } }));
            auto out = renderToBuffer (s.project(), renderAt (48000, 1.5));
            const float expected = 0.5f * fadeInGain (FadeCurve::Linear, 24000, 48000) * fadeOutGain (FadeCurve::Linear, 23999, 48000);
            expectWithinAbsoluteError (out.getSample (0, 24000), expected, 1e-6f);
            expectWithinAbsoluteError (out.getSample (0, 24000), 0.125f, 1e-4f);
        }

        beginTest ("event gain combines with the fade");
        {
            Session s;
            placeEvent (s, dc, 2.0, true);
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 1.0 } }));
            s.run ("set_event_gain", args ({ { "eventId", "event-3" }, { "db", gainToDb (0.5) } }));
            auto out = renderToBuffer (s.project(), renderAt (48000, 1.5));
            expectWithinAbsoluteError (out.getSample (0, 12000), 0.5f * 0.5f * 0.5f, 1e-5f);
        }

        beginTest ("pitch does not change fade lengths (they are timeline lengths); looping keeps fading across wraps");
        {
            Session s;
            placeEvent (s, dc, 8.0, true);                     // 1 s loop over 4 s
            s.run ("set_event_pitch", args ({ { "eventIds", ids ({ "event-3" }) }, { "semitones", 12 } }));
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeOutBeats", 4.0 } }));   // last 2 s
            auto out = renderToBuffer (s.project(), renderAt (48000, 1.5));
            expectWithinAbsoluteError (out.getSample (0, 72), 0.5f, 1e-5f);           // declick-only fade-in, unchanged by pitch
            const int last = 4 * 48000 - 1;
            expectWithinAbsoluteError (out.getSample (0, last - 48000), 0.25f, 1e-4f); // half way through the fade-out
        }

        beginTest ("fades survive save/load, split keeps the outer fades, and are clipped to the event");
        {
            Session s;
            placeEvent (s, dc, 8.0, true);
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 1.0 }, { "fadeOutBeats", 2.0 },
                                             { "fadeInCurve", "smooth" }, { "fadeOutCurve", "fast" } }));
            auto split = s.run ("split_event", args ({ { "eventId", "event-3" }, { "beat", 4.0 } }));
            const auto& left = eventIn (s, "event-3");
            const auto& right = eventIn (s, split["rightEventId"].toString());
            expectEquals (left.fadeInTicks, kTicksPerBeat);
            expectEquals (left.fadeOutTicks, static_cast<Tick> (0), "cut edge: declick only");
            expectEquals (right.fadeInTicks, static_cast<Tick> (0));
            expectEquals (right.fadeOutTicks, 2 * kTicksPerBeat);
            expect (right.fadeOutCurve == FadeCurve::Fast);

            auto r = s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 100.0 } }));
            expectEquals (eventIn (s, "event-3").fadeInTicks, eventIn (s, "event-3").length);
            expectEquals (r["clippedToEventLength"].size(), 1);
            s.run ("resize_event", args ({ { "eventId", "event-3" }, { "endBeat", 1.0 } }));   // shorter than its fade
            expectEquals (eventIn (s, "event-3").fadeInTicks, kTicksPerBeat, "fade follows the shorter event");

            const auto file = scratchDir().getChildFile ("fades.mcpdaw");
            s.run ("save_project", args ({ { "path", file.getFullPathName() } }));
            auto loaded = loadProjectFile (file);
            const auto* e = loaded.findEvent (split["rightEventId"].toString()).event;
            expect (e != nullptr && e->fadeOutTicks == 2 * kTicksPerBeat && e->fadeOutCurve == FadeCurve::Fast);
            expect (file.loadFileAsString().contains ("\"fadeOutCurve\": \"fast\""));
            expect (! file.loadFileAsString().contains ("declick"), "the declick preference is not stored in the project");
        }

        beginTest ("MCP: partial set_event_fades leaves other properties alone; zero = declick only");
        {
            Session s;
            placeEvent (s, dc, 4.0, true);
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 0.5 }, { "fadeInCurve", "smooth" } }));
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeOutBeats", 2.0 } }));
            const auto& e = eventIn (s, "event-3");
            expectEquals (e.fadeInTicks, kTicksPerBeat / 2, "unchanged");
            expect (e.fadeInCurve == FadeCurve::Smooth, "unchanged");
            expectEquals (e.fadeOutTicks, 2 * kTicksPerBeat);
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeOutBeats", 0 } }));
            expectEquals (eventIn (s, "event-3").fadeOutTicks, static_cast<Tick> (0));
            auto state = s.run ("get_track", args ({ { "trackId", s.project().tracks[0].id } }));
            expectWithinAbsoluteError (static_cast<double> (state["events"][0]["effectiveFadeOutMs"]), 1.5, 1e-9);
            expectEquals (state["events"][0]["fadeInCurve"].toString(), juce::String ("smooth"));
            expectThrows (s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) } })));                         // nothing to do
            expectThrows (s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInCurve", "wobbly" } })));
            expectThrows (s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", -1 } })));
            const auto labels = s.controller.historyLabels();
            expect (labels.contains ("Adjust event fade"));
            s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeOutCurve", "slow" } }));
            expectEquals (s.controller.undoLabel(), juce::String ("Change fade curve"));
        }

        beginTest ("MCP: create_event takes fades, curves, gain and reverse in one step");
        {
            Session s;
            s.run ("import_audio", args ({ { "path", dc.getFullPathName() }, { "place", false } }));
            s.run ("create_track", args ({ { "name", "Vox" } }));
            auto r = s.run ("create_event", args ({ { "trackId", "track-2" }, { "assetId", "asset-1" }, { "startBeat", 32 },
                                                    { "fadeInBeats", 0.25 }, { "fadeOutBeats", 0.5 }, { "fadeInCurve", "smooth" },
                                                    { "fadeOutCurve", "fast" }, { "gainDb", -3.0 }, { "reversed", true } }));
            const auto& e = eventIn (s, r["id"].toString());
            expectEquals (e.fadeInTicks, kTicksPerBeat / 4);
            expectEquals (e.fadeOutTicks, kTicksPerBeat / 2);
            expect (e.fadeInCurve == FadeCurve::Smooth);
            expect (e.fadeOutCurve == FadeCurve::Fast);
            expectEquals (e.gainDb, -3.0);
            expect (e.reversed);
            expectEquals (r["fadeOutCurve"].toString(), juce::String ("fast"));
            auto plain = s.run ("create_event", args ({ { "trackId", "track-2" }, { "assetId", "asset-1" }, { "startBeat", 0 } }));
            expectEquals (eventIn (s, plain["id"].toString()).fadeInTicks, static_cast<Tick> (0), "no fade given -> stored 0");
            expectWithinAbsoluteError (static_cast<double> (plain["effectiveFadeInMs"]), 1.5, 1e-9);
        }

        beginTest ("changing the declick preference changes every event's effective minimum (no project edit)");
        {
            Session s;
            placeEvent (s, dc, 1.0, false);
            const auto revision = s.controller.getRevision();
            s.run ("set_default_declick", args ({ { "ms", 10.0 } }));
            expectEquals (s.controller.getRevision(), revision, "not a project edit");
            expect (! s.controller.canUndo() || s.controller.undoLabel() != "Default declick");
            auto state = s.run ("get_project_state");
            expectWithinAbsoluteError (static_cast<double> (state["tracks"][0]["events"][0]["effectiveFadeInMs"]), 10.0, 1e-9);
            expectThrows (s.run ("set_default_declick", args ({ { "ms", 25.0 } })));
            s.controller.setDefaultDeclickMs (-3.0);
            expectEquals (s.controller.defaultDeclickMs(), 0.0, "clamped");
        }
    }
};

// ================================================================================================
class ReverseTests final : public juce::UnitTest
{
public:
    ReverseTests() : juce::UnitTest ("Reverse event", "Editing") {}

    static float at (const juce::AudioBuffer<float>& b, int i) { return b.getSample (0, i); }

    void runTest() override
    {
        // Deterministic ramp: sample i = i / 4800, 0.1 s at 48 kHz (= 0.2 beats = 192 ticks at 120 BPM).
        const int frames = 4800;
        const auto ramp = writeWav ("ramp.wav", 48000, 1, frames, [] (int, int i) { return static_cast<float> (i) / 4800.0f; });
        const auto hard = renderAt (48000, 0.0);

        beginTest ("normal playback vs reversed playback: an exact mirror of the same window");
        Session s;
        s.run ("import_audio", args ({ { "path", ramp.getFullPathName() } }));   // natural length, one-shot
        const auto fwd = renderToBuffer (s.project(), hard);
        for (int i = 0; i < frames - 1; ++i) expectWithinAbsoluteError (at (fwd, i), static_cast<float> (i) / frames, 1e-6f);
        s.run ("set_event_reverse", args ({ { "eventIds", ids ({ "event-3" }) }, { "enabled", true } }));
        expectEquals (s.controller.undoLabel(), juce::String ("Reverse event"));
        const auto rev = renderToBuffer (s.project(), hard);
        bool mirror = true;
        for (int k = 0; k < frames; ++k) mirror = mirror && at (rev, k) == at (fwd, frames - 1 - k);
        expect (mirror, "rev[k] == fwd[L-1-k] sample for sample");
        expectWithinAbsoluteError (at (rev, 1), static_cast<float> (frames - 2) / frames, 1e-6f);
        expect (s.project().assets[0].data->pcm.getSample (0, 10) == 10.0f / frames, "the asset is untouched");

        beginTest ("reverse + source offset: plays the same window backwards");
        Session o;
        o.run ("import_audio", args ({ { "path", ramp.getFullPathName() }, { "place", false } }));
        o.run ("create_track");
        o.run ("create_event", args ({ { "trackId", "track-2" }, { "assetId", "asset-1" }, { "startBeat", 0 }, { "lengthBeats", 0.05 },
                                       { "sourceOffsetSeconds", 0.05 }, { "looping", false }, { "reversed", true } }));   // 1200 samples from 2400
        const auto off = renderToBuffer (o.project(), hard);
        for (int k = 0; k < 1200; k += 97) expectWithinAbsoluteError (at (off, k), static_cast<float> (2400 + 1199 - k) / frames, 1e-6f);

        beginTest ("reverse + trim: audio stays in place when either edge moves");
        {
            const auto before = renderToBuffer (o.project(), hard);
            const auto id = o.project().tracks[0].events[0].id;
            // 1 tick = 25 samples at 120 BPM / 48 kHz: 40 ticks = 1000 samples, 10 ticks = 250.
            o.run ("resize_event", args ({ { "eventId", id }, { "endBeat", 40.0 / 960.0 } }));   // right edge in
            const auto rightTrim = renderToBuffer (o.project(), hard);
            for (int k = 0; k < 1000; k += 31) expectWithinAbsoluteError (at (rightTrim, k), at (before, k), 1e-6f);
            o.run ("resize_event", args ({ { "eventId", id }, { "startBeat", 10.0 / 960.0 } }));  // left edge in
            const auto leftTrim = renderToBuffer (o.project(), hard);
            for (int k = 250; k < 1000; k += 29) expectWithinAbsoluteError (at (leftTrim, k), at (before, k), 1e-6f);
            expectEquals (at (leftTrim, 100), 0.0f);
            expectThrows (o.run ("resize_event", args ({ { "eventId", id }, { "endBeat", 4.0 } })));   // past the source start (one-shot)
        }

        beginTest ("reverse + looping: a looped window mirrored across wraps");
        {
            Session l;
            placeEvent (l, ramp, 1.0, true);                   // 0.5 s = 5 cycles of the 0.1 s ramp
            l.run ("slip_event", args ({ { "eventId", "event-3" }, { "sourceOffsetSeconds", 0.0123 } }));
            const auto f = renderToBuffer (l.project(), hard);
            l.run ("set_event_reverse", args ({ { "eventIds", ids ({ "event-3" }) }, { "enabled", true } }));
            const auto r = renderToBuffer (l.project(), hard);
            const int n = 24000;
            float worst = 0;
            for (int k = 2; k < n - 2; ++k) worst = std::max (worst, std::abs (at (r, k) - at (f, n - 1 - k)));
            expectLessThan (worst, 1e-5f);
        }

        beginTest ("reverse + varispeed: mirror holds at +12 and at a fractional +7");
        for (double semis : { 12.0, 7.0 })
        {
            Session p;
            p.run ("import_audio", args ({ { "path", ramp.getFullPathName() } }));
            p.run ("set_event_pitch", args ({ { "eventIds", ids ({ "event-3" }) }, { "semitones", semis } }));
            const auto length = static_cast<int> (ticksToSamples (p.project().tracks[0].events[0].length, 120.0, 48000));
            const auto f = renderToBuffer (p.project(), hard);
            p.run ("set_event_reverse", args ({ { "eventIds", ids ({ "event-3" }) }, { "enabled", true } }));
            const auto r = renderToBuffer (p.project(), hard);
            float worst = 0;
            for (int k = 0; k < length; ++k) worst = std::max (worst, std::abs (at (r, k) - at (f, length - 1 - k)));
            expectLessThan (worst, 1e-4f, "pitch " + juce::String (semis));
        }

        beginTest ("reverse + fades: fades stay on the timeline edges");
        {
            Session fz;
            placeEvent (fz, ramp, 0.2, false);
            fz.run ("set_event_reverse", args ({ { "eventIds", ids ({ "event-3" }) }, { "enabled", true } }));
            fz.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 0.05 } }));   // 1200 samples
            const auto r = renderToBuffer (fz.project(), hard);
            for (int k = 1; k < 1200; k += 53)
                expectWithinAbsoluteError (at (r, k), static_cast<float> (frames - 1 - k) / frames * fadeInGain (FadeCurve::Linear, k, 1200), 1e-6f);
        }

        beginTest ("reverse is per event: other events on the same asset are unaffected; state round-trips");
        {
            Session two;
            two.run ("import_audio", args ({ { "path", ramp.getFullPathName() } }));
            two.run ("duplicate_event", args ({ { "eventId", "event-3" } }));
            two.run ("set_event_reverse", args ({ { "eventIds", ids ({ "event-3" }) }, { "enabled", true } }));
            expect (two.project().tracks[0].events[0].reversed);
            expect (! two.project().tracks[0].events[1].reversed);
            auto state = two.run ("get_project_state");
            expect (static_cast<bool> (state["tracks"][0]["events"][0]["reversed"]));
            two.run ("set_event_reverse", args ({ { "eventIds", ids ({ "event-3" }) }, { "enabled", false } }));
            expect (! two.project().tracks[0].events[0].reversed);
            expectThrows (two.run ("set_event_reverse", args ({ { "eventIds", ids ({ "event-99" }) }, { "enabled", true } })));
        }
    }
};

// ================================================================================================
class SlipTests final : public juce::UnitTest
{
public:
    SlipTests() : juce::UnitTest ("Slip editing", "Editing") {}

    void runTest() override
    {
        const auto impulse = impulseWav ("slip_impulse.wav");   // 1 s, impulse at 0

        beginTest ("slip moves the audio, never the event; looping wraps");
        Session s;
        placeEvent (s, impulse, 8.0, true);                 // 1 s loop over 4 s
        const auto before = eventIn (s, "event-3");
        auto r = s.run ("slip_event", args ({ { "eventId", "event-3" }, { "deltaBeats", 0.5 } }));   // audio 0.25 s later
        const auto& e = eventIn (s, "event-3");
        expectEquals (e.start, before.start);
        expectEquals (e.length, before.length);
        expectWithinAbsoluteError (e.sourceOffsetSeconds, 0.75, 1e-9, "0 - 0.25 wraps to 0.75");
        expect (! static_cast<bool> (r["clampedToSource"]));
        expectEquals (s.controller.undoLabel(), juce::String ("Slip event"));
        auto out = renderToBuffer (s.project(), renderAt (48000, 0.0));
        expect (out.getSample (0, 12000) > 0.5f, "impulse now 0.25 s in");
        expectLessThan (std::abs (out.getSample (0, 0)), 1e-6f);
        s.run ("slip_event", args ({ { "eventId", "event-3" }, { "deltaBeats", -0.5 } }));
        expectWithinAbsoluteError (eventIn (s, "event-3").sourceOffsetSeconds, 0.0, 1e-9, "negative slip");

        beginTest ("one-shots stay within their source");
        Session o;
        placeEvent (o, impulse, 1.0, false);
        r = o.run ("slip_event", args ({ { "eventId", "event-3" }, { "deltaBeats", 1.0 } }));   // would need audio before the file start
        expectWithinAbsoluteError (eventIn (o, "event-3").sourceOffsetSeconds, 0.0, 1e-12);
        expect (static_cast<bool> (r["clampedToSource"]));
        o.run ("slip_event", args ({ { "eventId", "event-3" }, { "deltaBeats", -0.5 } }));   // audio left: offset 0.25 s
        expectWithinAbsoluteError (eventIn (o, "event-3").sourceOffsetSeconds, 0.25, 1e-9);
        r = o.run ("slip_event", args ({ { "eventId", "event-3" }, { "deltaBeats", -100.0 } }));
        expect (static_cast<bool> (r["clampedToSource"]));
        expectLessThan (eventIn (o, "event-3").sourceOffsetSeconds, 1.0);
        expectThrows (o.run ("slip_event", args ({ { "eventId", "event-3" }, { "sourceOffsetSeconds", 1.5 } })));
        expectThrows (o.run ("slip_event", args ({ { "eventId", "event-3" } })));
        expectThrows (o.run ("slip_event", args ({ { "eventId", "event-3" }, { "deltaBeats", 1 }, { "sourceOffsetSeconds", 0.1 } })));

        beginTest ("absolute offsets; looping events wrap them");
        s.run ("slip_event", args ({ { "eventId", "event-3" }, { "sourceOffsetSeconds", 0.3 } }));
        expectWithinAbsoluteError (eventIn (s, "event-3").sourceOffsetSeconds, 0.3, 1e-12);
        r = s.run ("slip_event", args ({ { "eventId", "event-3" }, { "sourceOffsetSeconds", 1.5 } }));
        expectWithinAbsoluteError (eventIn (s, "event-3").sourceOffsetSeconds, 0.5, 1e-12);
        expect (static_cast<bool> (r["wrapped"]));

        beginTest ("pitch (varispeed) scales the slip in source time; reverse flips its direction");
        Session p;
        placeEvent (p, impulse, 8.0, true);
        p.run ("set_event_pitch", args ({ { "eventIds", ids ({ "event-3" }) }, { "semitones", 12 } }));
        p.run ("slip_event", args ({ { "eventId", "event-3" }, { "deltaBeats", -0.5 } }));   // 0.25 s timeline = 0.5 s source at 2x
        expectWithinAbsoluteError (eventIn (p, "event-3").sourceOffsetSeconds, 0.5, 1e-9);
        p.run ("set_event_reverse", args ({ { "eventIds", ids ({ "event-3" }) }, { "enabled", true } }));
        p.run ("slip_event", args ({ { "eventId", "event-3" }, { "deltaBeats", 0.25 } }));    // reversed: audio right = offset up
        expectWithinAbsoluteError (eventIn (p, "event-3").sourceOffsetSeconds, 0.75, 1e-9);

        beginTest ("slippedOffset helper (used by the GUI preview) agrees with the action");
        bool clamped = true;
        const auto& pe = eventIn (p, "event-3");
        expectWithinAbsoluteError (slippedOffset (p.project(), pe, -0.25, &clamped), 0.5, 1e-9);
        expect (! clamped);
    }
};

// ================================================================================================
class MigrationTests final : public juce::UnitTest
{
public:
    MigrationTests() : juce::UnitTest ("Project format migration", "Editing") {}

    void runTest() override
    {
        const auto dir = scratchDir().getChildFile ("migration");
        dir.createDirectory();
        const auto wav = dcWav ("migration/loop.wav", 0.5f);
        juce::ignoreUnused (wav);

        // A real MCP DAW 0.4.0 (format v1) project: no fade/reverse fields at all.
        const juce::String v1 = R"({
  "format": "mcp-daw-project", "version": 1, "generator": "MCP DAW 0.4.0", "ticksPerBeat": 960, "timeSignature": "4/4",
  "name": "Old song", "bpm": 120.0, "nextSerial": 10,
  "loop": { "enabled": false, "startTicks": 0, "endTicks": 15360 },
  "master": { "gainDb": 0.0, "effects": [] },
  "assets": [ { "id": "asset-1", "path": "loop.wav", "sourceBpm": 0.0, "tempoSource": "" } ],
  "tracks": [ { "id": "track-2", "name": "Loop", "colour": 0, "gainDb": 0.0, "pan": 0.0, "mute": false, "solo": false,
                "events": [ { "id": "event-3", "assetId": "asset-1", "startTicks": 0, "lengthTicks": 3840,
                              "sourceOffsetSeconds": 0.0, "looping": true, "gainDb": -2.0, "pitchSemitones": 0.0 } ],
                "effects": [] } ] })";
        const auto original = dir.getChildFile ("Old song.mcpdaw");
        original.replaceWithText (v1);
        const auto originalBytes = original.loadFileAsString();

        beginTest ("older project opens; new fields get their defaults; the file is untouched");
        Session s;
        auto opened = s.run ("open_project", args ({ { "path", original.getFullPathName() } }));
        const auto& e = *s.project().findEvent ("event-3").event;
        expectEquals (e.fadeInTicks, static_cast<Tick> (0));
        expectEquals (e.fadeOutTicks, static_cast<Tick> (0));
        expect (e.fadeInCurve == FadeCurve::Linear);
        expect (e.fadeOutCurve == FadeCurve::Linear);
        expect (! e.reversed);
        expectEquals (e.gainDb, -2.0, "existing state kept");
        expectEquals (s.controller.upgradedFromVersion(), 1);
        expect (s.controller.saveNeedsNewFile());
        expect (! s.controller.isDirty(), "nothing edited yet");
        expectEquals (static_cast<int> (opened["formatUpgrade"]["fromVersion"]), 1);
        expectEquals (opened["formatUpgrade"]["suggestedPath"].toString(), dir.getChildFile ("Old song-v2.mcpdaw").getFullPathName());
        expectEquals (original.loadFileAsString(), originalBytes);

        beginTest ("ordinary Save is redirected: the original older-format file is protected");
        s.run ("set_event_gain", args ({ { "eventId", "event-3" }, { "db", -6.0 } }));   // editing is allowed
        expectThrows (s.run ("save_project"));
        expectThrows (s.run ("save_project", args ({ { "path", original.getFullPathName() } })));   // no explicit overwrite
        expectEquals (original.loadFileAsString(), originalBytes, "still untouched");
        try { s.run ("save_project"); }
        catch (const std::exception& ex) { expect (juce::String (ex.what()).contains ("Old song-v2.mcpdaw"), "error names the suggested copy"); }

        beginTest ("saving the upgraded copy writes the current format and resumes normal saving");
        const auto copy = s.controller.suggestedUpgradedFile();
        s.run ("save_project", args ({ { "path", copy.getFullPathName() } }));
        expectEquals (s.controller.upgradedFromVersion(), 0);
        expect (s.controller.currentFile() == copy);
        auto json = juce::JSON::parse (copy.loadFileAsString());
        expectEquals (static_cast<int> (json["version"]), Project::kFormatVersion);
        expect (json["tracks"][0]["events"][0].hasProperty ("fadeInTicks"));
        s.run ("set_event_gain", args ({ { "eventId", "event-3" }, { "db", -3.0 } }));
        s.run ("save_project");   // ordinary Save now goes to the copy
        expectEquals (original.loadFileAsString(), originalBytes, "original never overwritten");

        beginTest ("the original can be replaced only with an explicit, confirmed overwrite");
        Session t;
        t.run ("open_project", args ({ { "path", original.getFullPathName() } }));
        t.run ("save_project", args ({ { "path", original.getFullPathName() }, { "overwrite", true } }));
        expectEquals (static_cast<int> (juce::JSON::parse (original.loadFileAsString())["version"]), Project::kFormatVersion);
        expectEquals (t.controller.upgradedFromVersion(), 0);

        beginTest ("current-format and new projects are not flagged; future formats fail safely");
        Session u;
        u.run ("open_project", args ({ { "path", copy.getFullPathName() } }));
        expectEquals (u.controller.upgradedFromVersion(), 0);
        expect (! u.run ("open_project", args ({ { "path", copy.getFullPathName() }, { "discardChanges", true } })).hasProperty ("formatUpgrade"));
        auto future = juce::JSON::parse (copy.loadFileAsString());
        future.getDynamicObject()->setProperty ("version", Project::kFormatVersion + 1);
        const auto futureFile = dir.getChildFile ("future.mcpdaw");
        futureFile.replaceWithText (juce::JSON::toString (future));
        try
        {
            u.run ("open_project", args ({ { "path", futureFile.getFullPathName() }, { "discardChanges", true } }));
            expect (false, "a newer format must be refused");
        }
        catch (const std::exception& ex) { expect (juce::String (ex.what()).contains ("newer")); }
        expect (u.controller.currentFile() == copy, "the open project is unchanged after the refusal");
        int version = 0;
        loadProjectFile (original, {}, &version);
        expectEquals (version, Project::kFormatVersion);
    }
};

// ================================================================================================
class EditingMcpTests final : public juce::UnitTest
{
public:
    EditingMcpTests() : juce::UnitTest ("Editing over MCP", "Editing") {}

    void runTest() override
    {
        const auto dc = dcWav ("mcp_edit.wav", 0.5f);
        Session s;
        McpSession mcp (s.registry, s.controller);
        mcp.handleText (R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})");
        mcp.handleText (R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        auto call = [&] (const juce::String& name, const juce::var& a)
        {
            const auto request = makeObject ({ { "jsonrpc", "2.0" }, { "id", 7 }, { "method", "tools/call" },
                                               { "params", makeObject ({ { "name", name }, { "arguments", a } }) } });
            return juce::JSON::parse (mcp.handleText (juce::JSON::toString (request)))["result"];
        };

        beginTest ("new tools are listed from the registry");
        auto list = juce::JSON::parse (mcp.handleText (R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"));
        juce::StringArray names;
        for (const auto& t : *list["result"]["tools"].getArray()) names.add (t["name"].toString());
        for (auto* n : { "set_event_fades", "set_event_reverse", "slip_event", "get_wav_loop_metadata", "write_wav_loop_metadata", "set_default_declick" })
            expect (names.contains (n), n);

        beginTest ("an agent can fade, reverse, slip and level events and read it all back");
        call ("import_audio", args ({ { "path", dc.getFullPathName() }, { "lengthBeats", 8 } }));
        call ("duplicate_event", args ({ { "eventId", "event-3" } }));
        auto r = call ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeInBeats", 0.25 }, { "fadeOutBeats", 0.5 },
                                                 { "fadeInCurve", "smooth" }, { "fadeOutCurve", "fast" } }));
        expect (! static_cast<bool> (r["isError"]));
        call ("set_event_reverse", args ({ { "eventIds", ids ({ "event-3" }) }, { "enabled", true } }));
        call ("slip_event", args ({ { "eventId", "event-3" }, { "sourceOffsetSeconds", 0.124 } }));
        call ("set_event_gain", args ({ { "eventIds", ids ({ "event-3", "event-4" }) }, { "db", -3.0 } }));
        expectEquals (s.controller.undoLabel(), juce::String ("Adjust event gain"));
        expectEquals (s.project().tracks[0].events[1].gainDb, -3.0, "multi-event gain in one step");
        auto state = call ("get_project_state", args ({}))["structuredContent"];
        const auto ev = state["tracks"][0]["events"][0];
        expectEquals (ev["id"].toString(), juce::String ("event-3"));
        expect (static_cast<bool> (ev["reversed"]));
        expectEquals (static_cast<double> (ev["gainDb"]), -3.0);
        expectEquals (static_cast<double> (ev["fadeInBeats"]), 0.25);
        expectEquals (static_cast<double> (ev["fadeOutBeats"]), 0.5);
        expectEquals (ev["fadeInCurve"].toString(), juce::String ("smooth"));
        expectEquals (ev["fadeOutCurve"].toString(), juce::String ("fast"));
        expectWithinAbsoluteError (static_cast<double> (ev["sourceOffsetSeconds"]), 0.124, 1e-12);

        beginTest ("errors are tool results an agent can read");
        r = call ("set_event_fades", args ({ { "eventIds", ids ({ "event-3" }) }, { "fadeOutCurve", "logarithmic" } }));
        expect (static_cast<bool> (r["isError"]));
        expect (r["content"][0]["text"].toString().contains ("linear, smooth, fast, slow"));

        beginTest ("one batch = one undo step");
        call ("execute_batch", args ({ { "actions", juce::Array<juce::var> {
                  makeObject ({ { "action", "set_event_reverse" }, { "arguments", args ({ { "eventIds", ids ({ "event-4" }) }, { "enabled", true } }) } }),
                  makeObject ({ { "action", "set_event_fades" }, { "arguments", args ({ { "eventIds", ids ({ "event-4" }) }, { "fadeOutBeats", 1 } }) } }) } },
                                       { "label", "Reverse swell" } }));
        expectEquals (s.controller.undoLabel(), juce::String ("Reverse swell"));
        s.run ("undo");
        expect (! s.project().tracks[0].events[1].reversed);
        expectEquals (s.project().tracks[0].events[1].fadeOutTicks, static_cast<Tick> (0));
    }
};

// ================================================================================================
class RenderEquivalenceTests final : public juce::UnitTest
{
public:
    RenderEquivalenceTests() : juce::UnitTest ("Live/offline equivalence", "Editing") {}

    void runTest() override
    {
        beginTest ("live callback (odd block sizes) and offline bounce produce the same audio with fades, reverse, pitch, loops");
        const auto tone = writeWav ("equiv_tone.wav", 44100, 2, 44100, [] (int ch, int i)
                                    { return 0.4f * static_cast<float> (std::sin (0.031 * i + ch)) + 0.1f * static_cast<float> ((i % 300) / 300.0); });
        Session s;
        s.run ("import_audio", args ({ { "path", tone.getFullPathName() }, { "lengthBeats", 6 } }));
        s.run ("set_event_looping", args ({ { "eventId", "event-3" }, { "enabled", true } }));
        s.run ("import_audio", args ({ { "path", tone.getFullPathName() }, { "startBeat", 1.37 }, { "lengthBeats", 3 } }));
        s.run ("set_event_reverse", args ({ { "eventIds", ids ({ "event-5" }) }, { "enabled", true } }));
        s.run ("set_event_pitch", args ({ { "eventIds", ids ({ "event-5" }) }, { "semitones", -5 } }));
        s.run ("set_event_fades", args ({ { "eventIds", ids ({ "event-3", "event-5" }) }, { "fadeInBeats", 0.7 }, { "fadeOutBeats", 1.1 },
                                         { "fadeInCurve", "smooth" }, { "fadeOutCurve", "slow" } }));
        s.run ("set_event_gain", args ({ { "eventId", "event-3" }, { "db", -4.5 } }));
        s.run ("slip_event", args ({ { "eventId", "event-3" }, { "deltaBeats", 0.33 } }));
        s.controller.setDefaultDeclickMs (2.5);

        RenderOptions o;
        o.sampleRate = 48000;
        o.tailSeconds = 0;
        o.declickMs = s.controller.defaultDeclickMs();
        const auto offline = renderToBuffer (s.project(), o);

        AudioEngine engine (s.controller);
        s.run ("play");
        juce::AudioBuffer<float> live (2, offline.getNumSamples());
        juce::AudioIODeviceCallbackContext ctx;
        const int blocks[] = { 441, 17, 1024, 2048, 333 };
        for (int pos = 0, b = 0; pos < live.getNumSamples(); ++b)
        {
            const int n = std::min (blocks[b % 5], live.getNumSamples() - pos);
            float* chans[2] = { live.getWritePointer (0) + pos, live.getWritePointer (1) + pos };
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, chans, 2, n, ctx);
            pos += n;
        }
        float worst = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < live.getNumSamples(); ++i) worst = std::max (worst, std::abs (live.getSample (ch, i) - offline.getSample (ch, i)));
        expectLessThan (worst, 1e-5f);
        expectGreaterThan (offline.getMagnitude (0, offline.getNumSamples()), 0.1f);

        beginTest ("the live engine follows a declick preference change");
        s.run ("stop");
        s.controller.setDefaultDeclickMs (0.0);
        o.declickMs = 0.0;
        const auto hardOffline = renderToBuffer (s.project(), o);
        expect (std::abs (hardOffline.getSample (0, 0)) >= 0.0f);   // rendered without throwing
        s.run ("play");
        juce::AudioBuffer<float> hardLive (2, 4800);
        float* chans[2] = { hardLive.getWritePointer (0), hardLive.getWritePointer (1) };
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, chans, 2, 4800, ctx);
        float d = 0;
        for (int i = 0; i < 4800; ++i) d = std::max (d, std::abs (hardLive.getSample (0, i) - hardOffline.getSample (0, i)));
        expectLessThan (d, 1e-5f);
    }
};

static FadeTests fadeTests;
static ReverseTests reverseTests;
static SlipTests slipTests;
static MigrationTests migrationTests;
static EditingMcpTests editingMcpTests;
static RenderEquivalenceTests renderEquivalenceTests;
} // namespace mcpdaw::test
