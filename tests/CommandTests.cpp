// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "TestSupport.h"

namespace mcpdaw::test
{
class CommandTests final : public juce::UnitTest
{
public:
    CommandTests() : juce::UnitTest ("Commands and undo", "Commands") {}

    const Event& ev (const Session& s, const juce::String& id)
    {
        auto ref = s.project().findEvent (id);
        jassert (ref.event != nullptr);
        return *ref.event;
    }

    void runTest() override
    {
        // 1 second of audio at 120 BPM = 2 beats.
        const auto wav = dcWav ("one_second.wav", 0.5f);

        beginTest ("import creates asset, track and event at natural length; IDs are deterministic");
        Session s;
        auto r = s.run ("import_audio", args ({ { "path", wav.getFullPathName() }, { "startBeat", 4 } }));
        expectEquals (r["assetId"].toString(), juce::String ("asset-1"));
        expectEquals (r["trackId"].toString(), juce::String ("track-2"));
        expectEquals (r["eventId"].toString(), juce::String ("event-3"));
        expectEquals (ev (s, "event-3").start, 4 * kTicksPerBeat);
        expectEquals (ev (s, "event-3").length, 2 * kTicksPerBeat);
        expectEquals (s.project().tracks[0].name, juce::String ("one_second"));
        expect (! r["tempoAdopted"], "no tempo metadata -> tempo unchanged");

        beginTest ("importing the same file reuses the decoded asset");
        s.run ("import_audio", args ({ { "path", wav.getFullPathName() }, { "trackId", "track-2" }, { "startBeat", 0 }, { "lengthBeats", 1 } }));
        expectEquals (static_cast<int> (s.project().assets.size()), 1);

        beginTest ("move, undo, redo");
        s.run ("move_event", args ({ { "eventId", "event-3" }, { "startBeat", 8 } }));
        expectEquals (ev (s, "event-3").start, 8 * kTicksPerBeat);
        s.run ("undo");
        expectEquals (ev (s, "event-3").start, 4 * kTicksPerBeat);
        s.run ("redo");
        expectEquals (ev (s, "event-3").start, 8 * kTicksPerBeat);

        beginTest ("short files without tempo default to one-shots; looping can be switched on");
        expect (! ev (s, "event-3").looping, "1 s untagged file -> one-shot by default");
        s.run ("set_event_looping", args ({ { "eventId", "event-3" }, { "enabled", true } }));

        beginTest ("extending a looping event repeats the loop");
        s.run ("resize_event", args ({ { "eventId", "event-3" }, { "endBeat", 16 } }));
        expectEquals (ev (s, "event-3").length, 8 * kTicksPerBeat);
        auto state = s.run ("get_track", args ({ { "trackId", "track-2" } }));
        expectWithinAbsoluteError (static_cast<double> (state["events"][1]["repetitions"]), 4.0, 1e-9);

        beginTest ("left trim keeps audio in place (source offset follows)");
        s.run ("resize_event", args ({ { "eventId", "event-3" }, { "startBeat", 9 } }));
        expectEquals (ev (s, "event-3").start, 9 * kTicksPerBeat);
        expectWithinAbsoluteError (ev (s, "event-3").sourceOffsetSeconds, 0.5, 1e-9);   // 1 beat at 120 BPM

        beginTest ("split preserves source phase and returns the new id");
        auto split = s.run ("split_event", args ({ { "eventId", "event-3" }, { "beat", 12 } }));
        const auto right = split["rightEventId"].toString();
        expectEquals (ev (s, right).start, 12 * kTicksPerBeat);
        // offset 0.5 s + 3 beats (1.5 s) = 2.0 s, wrapped by the 1 s loop -> 0.0
        expectWithinAbsoluteError (ev (s, right).sourceOffsetSeconds, 0.0, 1e-9);
        expectThrows (s.run ("split_event", args ({ { "eventId", right }, { "beat", 100 } })));

        beginTest ("placing over existing audio overwrites it (arranger overwrite)");
        Session o;
        o.run ("import_audio", args ({ { "path", wav.getFullPathName() }, { "lengthBeats", 16 } }));   // event-3 [0,16)
        o.run ("create_event", args ({ { "trackId", "track-2" }, { "assetId", "asset-1" }, { "startBeat", 4 }, { "lengthBeats", 4 } }));
        const auto& events = o.project().tracks[0].events;
        expectEquals (static_cast<int> (events.size()), 3);
        expectEquals (events[0].length, 4 * kTicksPerBeat);                         // left remainder
        expectEquals (events[1].start, 4 * kTicksPerBeat);                          // the new event
        expectEquals (events[2].start, 8 * kTicksPerBeat);                          // right remainder
        expectEquals (events[2].length, 8 * kTicksPerBeat);

        beginTest ("clear_range removes the kick for bars 1-2");
        Session k;
        k.run ("import_audio", args ({ { "path", wav.getFullPathName() }, { "lengthBeats", 16 } }));
        k.run ("clear_range", args ({ { "startBeat", 0 }, { "endBeat", 8 }, { "trackIds", juce::Array<juce::var> { "track-2" } } }));
        expectEquals (static_cast<int> (k.project().tracks[0].events.size()), 1);
        expectEquals (k.project().tracks[0].events[0].start, 8 * kTicksPerBeat);
        expectEquals (k.project().tracks[0].events[0].length, 8 * kTicksPerBeat);
        expectEquals (occupiedBars (k.project().tracks[0]), juce::String ("3-4"));

        beginTest ("duplicate_range copies bars 1-2 to bar 5, replacing what was there");
        k.run ("duplicate_range", args ({ { "startBeat", 8 }, { "endBeat", 12 }, { "destBeat", 32 } }));
        expectEquals (occupiedBars (k.project().tracks[0]), juce::String ("3-4, 9"));

        beginTest ("duplicate_event with count builds back-to-back copies");
        Session d;
        auto imp = d.run ("import_audio", args ({ { "path", wav.getFullPathName() }, { "lengthBeats", 4 } }));
        auto dup = d.run ("duplicate_event", args ({ { "eventId", imp["eventId"] }, { "count", 3 } }));
        expectEquals (dup["eventIds"].size(), 3);
        expectEquals (d.project().contentEnd(), 16 * kTicksPerBeat);

        beginTest ("move_events moves a group without the members overwriting each other");
        Session g;
        auto first = g.run ("import_audio", args ({ { "path", wav.getFullPathName() }, { "lengthBeats", 4 } }));      // [0,4)
        auto second = g.run ("create_event", args ({ { "trackId", first["trackId"] }, { "assetId", first["assetId"] }, { "startBeat", 4 }, { "lengthBeats", 4 } }));   // [4,8)
        g.run ("move_events", args ({ { "moves", juce::Array<juce::var> {
            args ({ { "eventId", first["eventId"] }, { "startBeat", 2 } }),
            args ({ { "eventId", second["id"] }, { "startBeat", 6 } }) } } }));
        expectEquals (ev (g, first["eventId"].toString()).length, 4 * kTicksPerBeat);
        expectEquals (ev (g, second["id"].toString()).start, 6 * kTicksPerBeat);
        expectEquals (ev (g, second["id"].toString()).length, 4 * kTicksPerBeat);
        auto copies = g.run ("move_events", args ({ { "copy", true }, { "moves", juce::Array<juce::var> {
            args ({ { "eventId", first["eventId"] }, { "startBeat", 10 } }),
            args ({ { "eventId", second["id"] }, { "startBeat", 14 } }) } } }));
        expectEquals (copies["eventIds"].size(), 2);
        expectEquals (static_cast<int> (g.project().tracks[0].events.size()), 4);
        expectEquals (g.project().contentEnd(), 18 * kTicksPerBeat);

        beginTest ("validation: bad arguments change nothing and explain themselves");
        const auto before = juce::JSON::toString (projectStateJson (d.project()));
        auto expectError = [&] (const juce::String& action, const juce::var& a, const juce::String& fragment)
        {
            try { d.run (action, a); expect (false, action + " should have failed"); }
            catch (const std::exception& e) { expect (juce::String (e.what()).contains (fragment), juce::String (e.what())); }
        };
        expectError ("set_project_tempo", args ({ { "bpm", 500 } }), "out of range");
        expectError ("move_event", args ({ { "eventId", "event-999" }, { "startBeat", 1 } }), "not found");
        expectError ("set_track_gain", args ({ { "trackId", "track-2" }, { "db", 3 }, { "colour", 1 } }), "unknown argument 'colour'");
        expectError ("set_track_gain", args ({ { "trackId", "track-2" } }), "missing required argument 'db'");
        expectError ("rename_track", args ({ { "trackId", "nope" }, { "name", "x" } }), "Tracks: track-2");
        expectError ("no_such_action", args ({}), "Unknown action");
        expectEquals (juce::JSON::toString (projectStateJson (d.project())), before);

        beginTest ("effects: add with choice names, set, bypass, reorder, remove");
        auto fx = d.run ("add_effect", args ({ { "target", "track-2" }, { "type", "eq" } }));
        auto delayFx = d.run ("add_effect", args ({ { "target", "track-2" }, { "type", "multitap_delay" }, { "index", 0 } }));
        expectEquals (d.project().tracks[0].effects[0].type, juce::String ("multitap_delay"));
        d.run ("set_effect_parameters", args ({ { "effectId", delayFx["id"] }, { "values", args ({ { "tap3.enabled", true }, { "tap3.division", "1/16" }, { "mix", 0.5 } }) } }));
        auto params = d.run ("get_effect_parameters", args ({ { "effectId", delayFx["id"] } }));
        expectEquals (static_cast<double> (params["parameters"]["tap3.division"]), 5.0);
        expectEquals (static_cast<double> (params["parameters"]["mix"]), 0.5);
        expectError ("set_effect_parameter", args ({ { "effectId", fx["id"] }, { "parameter", "band1.freq" }, { "value", 5 } }), "out of range");
        expectError ("set_effect_parameter", args ({ { "effectId", fx["id"] }, { "parameter", "band1.type" }, { "value", "Wobble" } }), "not one of");
        d.run ("move_effect", args ({ { "effectId", fx["id"] }, { "index", 0 } }));
        expectEquals (d.project().tracks[0].effects[0].type, juce::String ("eq"));
        d.run ("set_effect_bypass", args ({ { "effectId", fx["id"] }, { "bypassed", true } }));
        expect (d.project().tracks[0].effects[0].bypassed);
        d.run ("remove_effect", args ({ { "effectId", fx["id"] } }));
        expectEquals (static_cast<int> (d.project().tracks[0].effects.size()), 1);

        beginTest ("transactions: several edits, one undo step");
        const auto undoDepth = d.controller.historyLabels().size();
        d.controller.beginTransaction ("Fader drag");
        for (int i = 0; i < 20; ++i) d.run ("set_track_gain", args ({ { "trackId", "track-2" }, { "db", -i * 0.5 } }));
        d.controller.endTransaction();
        expectEquals (d.controller.historyLabels().size(), undoDepth + 1);
        d.run ("undo");
        expectEquals (d.project().tracks[0].gainDb, 0.0);

        beginTest ("execute_batch: result references and atomic rollback");
        auto batch = d.run ("execute_batch", args ({ { "label", "Build intro" }, { "actions", juce::Array<juce::var> {
            args ({ { "action", "create_track" }, { "arguments", args ({ { "name", "Perc" } }) } }),
            args ({ { "action", "create_event" }, { "arguments", args ({ { "trackId", "$0.trackId" }, { "assetId", "asset-1" }, { "startBeat", 0 }, { "lengthBeats", 8 } }) } }),
            args ({ { "action", "set_track_pan" }, { "arguments", args ({ { "trackId", "$0.trackId" }, { "pan", 0.5 } }) } }) } } }));
        expectEquals (batch["results"].size(), 3);
        expectEquals (static_cast<int> (d.project().tracks.size()), 2);
        expectEquals (d.controller.undoLabel(), juce::String ("Build intro"));

        const auto beforeBatch = juce::JSON::toString (projectStateJson (d.project()));
        expectError ("execute_batch", args ({ { "actions", juce::Array<juce::var> {
            args ({ { "action", "create_track" }, { "arguments", args ({ { "name", "Doomed" } }) } }),
            args ({ { "action", "set_project_tempo" }, { "arguments", args ({ { "bpm", 9999 } }) } }) } } }), "step 1 failed");
        expectEquals (juce::JSON::toString (projectStateJson (d.project())), beforeBatch);
        expectEquals (d.controller.undoLabel(), juce::String ("Build intro"));

        beginTest ("execute_batch reaches listeners once (the engine never plays half a batch); drags notify each move");
        {
            struct Counter : ProjectController::Listener
            {
                int calls = 0;
                void projectChanged (const Project&, bool) override { ++calls; }
            } counter;
            d.controller.addListener (&counter);
            juce::Array<juce::var> steps;
            for (int i = 0; i < 10; ++i)
                steps.add (args ({ { "action", "set_track_gain" }, { "arguments", args ({ { "trackId", "track-2" }, { "db", -i * 1.0 } }) } }));
            d.run ("execute_batch", args ({ { "actions", steps } }));
            expectEquals (counter.calls, 1);
            counter.calls = 0;
            d.controller.beginTransaction ("Fader drag");
            for (int i = 0; i < 5; ++i) d.run ("set_track_gain", args ({ { "trackId", "track-2" }, { "db", -i * 1.0 } }));
            d.controller.endTransaction();
            expectEquals (counter.calls, 5);
            d.controller.removeListener (&counter);
        }

        beginTest ("tempo adoption from the first tempo-tagged loop");
        Session t;
        auto tagged = dcWav ("groove_90bpm.wav", 0.1f, 48000, 48000 * 8 / 3);   // 2.666 s = 4 beats at 90
        auto res = t.run ("import_audio", args ({ { "path", tagged.getFullPathName() } }));
        expect (static_cast<bool> (res["tempoAdopted"]));
        expectEquals (t.project().bpm, 90.0);
        expectEquals (t.project().tracks[0].events[0].length, 4 * kTicksPerBeat);

        beginTest ("replace_track_audio swaps the file on every event, keeps timing, resets offsets, undoable");
        Session u;
        u.run ("import_audio", args ({ { "path", wav.getFullPathName() }, { "lengthBeats", 1 } }));          // asset-1 track-2 event-3
        u.run ("create_event", args ({ { "trackId", "track-2" }, { "assetId", "asset-1" }, { "startBeat", 3 },
                                       { "lengthBeats", 0.5 }, { "looping", false }, { "sourceOffsetSeconds", 0.4 } }));
        const auto other = dcWav ("other_sound.wav", 0.2f);
        auto rep = u.run ("replace_track_audio", args ({ { "trackId", "track-2" }, { "path", other.getFullPathName() } }));
        expectEquals (static_cast<int> (rep["eventsChanged"]), 2);
        expectEquals (u.project().tracks[0].name, juce::String ("other_sound"));   // was named after the old file
        const auto newAsset = rep["assetId"].toString();
        for (const auto& e : u.project().tracks[0].events)
        {
            expectEquals (e.assetId, newAsset);
            expectEquals (e.sourceOffsetSeconds, 0.0);
        }
        expectEquals (u.project().tracks[0].events[1].start, 3 * kTicksPerBeat);
        expect (! u.project().tracks[0].events[1].looping, "looping flag untouched");
        u.run ("undo");
        expectEquals (u.project().tracks[0].events[1].assetId, juce::String ("asset-1"));
        expectEquals (u.project().tracks[0].events[1].sourceOffsetSeconds, 0.4);
        u.run ("replace_track_audio", args ({ { "trackId", "track-2" }, { "path", other.getFullPathName() }, { "keepOffsets", true } }));
        expectEquals (u.project().tracks[0].events[1].sourceOffsetSeconds, 0.4);

        beginTest ("set_event_pitch: one-shots refit to the new duration, loops keep length, bounds and undo");
        {
            Session q;
            q.run ("import_audio", args ({ { "path", wav.getFullPathName() } }));                       // event-3: 0..2 beats, one-shot
            q.run ("create_event", args ({ { "trackId", "track-2" }, { "assetId", "asset-1" }, { "startBeat", 3 } }));   // event-4 at beat 3
            q.run ("set_event_pitch", args ({ { "eventIds", juce::Array<juce::var> { "event-3" } }, { "semitones", 12 } }));
            expectEquals (ev (q, "event-3").length, 1 * kTicksPerBeat, "+12: 1 s one-shot now lasts 0.5 s = 1 beat");
            q.run ("set_event_pitch", args ({ { "eventIds", juce::Array<juce::var> { "event-3" } }, { "semitones", -12 } }));
            expectEquals (ev (q, "event-3").length, 3 * kTicksPerBeat, "-12 would be 4 beats; clamped at the next event");
            expectEquals (ev (q, "event-4").start, 3 * kTicksPerBeat, "neighbour untouched");
            q.run ("undo");
            expectEquals (ev (q, "event-3").pitchSemitones, 12.0);
            expectEquals (ev (q, "event-3").length, 1 * kTicksPerBeat);

            q.run ("set_event_looping", args ({ { "eventId", "event-4" }, { "enabled", true } }));
            q.run ("resize_event", args ({ { "eventId", "event-4" }, { "endBeat", 11 } }));
            q.run ("set_event_pitch", args ({ { "eventIds", juce::Array<juce::var> { "event-4" } }, { "semitones", 7 } }));
            expectEquals (ev (q, "event-4").length, 8 * kTicksPerBeat, "looping events keep their length");
            expectThrows (q.run ("set_event_pitch", args ({ { "eventIds", juce::Array<juce::var> { "event-4" } }, { "semitones", 13 } })));
            expectThrows (q.run ("set_event_pitch", args ({ { "eventIds", juce::Array<juce::var> { "nope" } }, { "semitones", 1 } })));

            beginTest ("pitched events: split and left-trim advance the source offset at the playback rate");
            q.run ("set_event_pitch", args ({ { "eventIds", juce::Array<juce::var> { "event-4" } }, { "semitones", 12 } }));
            q.run ("resize_event", args ({ { "eventId", "event-4" }, { "startBeat", 4 } }));   // trim 1 beat = 0.5 s timeline = 1 s source
            expectWithinAbsoluteError (ev (q, "event-4").sourceOffsetSeconds, 0.0, 1e-9, "wrapped: 1 s into a 1 s loop");
            auto sp = q.run ("split_event", args ({ { "eventId", "event-4" }, { "beat", 4.5 } }));   // 0.25 s timeline = 0.5 s source
            expectWithinAbsoluteError (ev (q, sp["rightEventId"].toString()).sourceOffsetSeconds, 0.5, 1e-9);
            expectEquals (ev (q, sp["rightEventId"].toString()).pitchSemitones, 12.0, "split keeps pitch");

            beginTest ("create_event / import_audio accept pitchSemitones and size one-shots for it");
            auto c = q.run ("create_event", args ({ { "trackId", "track-2" }, { "assetId", "asset-1" }, { "startBeat", 20 }, { "pitchSemitones", -12 } }));
            expectEquals (ev (q, c["id"].toString()).length, 4 * kTicksPerBeat);
            const auto evs = q.run ("get_track", args ({ { "trackId", "track-2" } }))["events"];
            expectEquals (static_cast<double> (evs[evs.size() - 1]["pitchSemitones"]), -12.0, "pitch is reported to MCP clients");
        }

        beginTest ("unsaved changes guard for new/open");
        expectError ("new_project", args ({}), "unsaved changes");
        // (errors from lambdas above were captured on Session d; this one runs on d as well)
        d.run ("new_project", args ({ { "discardChanges", true } }));
        expectEquals (static_cast<int> (d.project().tracks.size()), 0);
        expect (! d.controller.canUndo());
    }
};

static CommandTests commandTests;
} // namespace mcpdaw::test
