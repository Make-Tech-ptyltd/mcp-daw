// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "TestSupport.h"

namespace mcpdaw::test
{
class MusicalTimeTests final : public juce::UnitTest
{
public:
    MusicalTimeTests() : juce::UnitTest ("Musical time", "Core") {}

    void runTest() override
    {
        beginTest ("tick / beat / second / sample conversions");
        expectEquals (beatsToTicks (1.25), static_cast<Tick> (1200));
        expectEquals (ticksToBeats (kTicksPerBar), 4.0);
        expectWithinAbsoluteError (beatsToSeconds (4, 120), 2.0, 1e-12);
        expectEquals (ticksToSamples (kTicksPerBeat, 120, 48000), static_cast<std::int64_t> (24000));
        expectEquals (ticksToSamples (kTicksPerBeat, 90, 44100), static_cast<std::int64_t> (29400));
        expectWithinAbsoluteError (samplesToBeats (24000, 120, 48000), 1.0, 1e-12);

        beginTest ("snapping");
        const Tick sixteenth = gridTicks (SnapGrid::Sixteenth);
        expectEquals (sixteenth, static_cast<Tick> (240));
        expectEquals (snapTicks (359, sixteenth), static_cast<Tick> (240));
        expectEquals (snapTicks (360, sixteenth), static_cast<Tick> (480));
        expectEquals (snapTicks (1234, 0), static_cast<Tick> (1234));
        expectEquals (snapTicksDown (kTicksPerBar * 2 - 1, kTicksPerBar), kTicksPerBar);

        beginTest ("bar.beat.tick display");
        expectEquals (formatBarBeat (0), juce::String ("1.1.000"));
        expectEquals (formatBarBeat (kTicksPerBar * 2 + kTicksPerBeat * 3 + 480), juce::String ("3.4.480"));

        beginTest ("decibels");
        expectWithinAbsoluteError (dbToGain (-6.0206), 0.5, 1e-4);
        expectEquals (dbToGain (kMinDb), 0.0);
        expectWithinAbsoluteError (gainToDb (1.0), 0.0, 1e-12);
    }
};

class AudioAssetTests final : public juce::UnitTest
{
public:
    AudioAssetTests() : juce::UnitTest ("Audio assets", "Core") {}

    void runTest() override
    {
        beginTest ("BPM from file names");
        expectEquals (parseBpmFromName ("drums_90bpm"), 90.0);
        expectEquals (parseBpmFromName ("Break 128 BPM Dusty"), 128.0);
        expectEquals (parseBpmFromName ("bpm-100_hats"), 100.0);
        expectEquals (parseBpmFromName ("kick_one_shot"), 0.0);
        expectEquals (parseBpmFromName ("bass 12bpm"), 0.0);   // implausible tempo rejected

        beginTest ("loop tempo estimate");
        const auto est = estimateLoopTempo (60.0 / 90.0 * 8.0);   // 2 bars at 90
        expectWithinAbsoluteError (est.bpm, 90.0, 1e-9);
        expectEquals (est.beats, 8.0);

        beginTest ("peak pyramid");
        juce::AudioBuffer<float> pcm (1, 100000);
        pcm.clear();
        pcm.setSample (0, 5000, 0.9f);
        pcm.setSample (0, 70000, -0.7f);
        PeakPyramid peaks;
        peaks.build (pcm);
        auto all = peaks.query (0, 100000);
        expectWithinAbsoluteError (all.hi, 0.9f, 1e-6f);
        expectWithinAbsoluteError (all.lo, -0.7f, 1e-6f);
        auto early = peaks.query (0, 10000);
        expectWithinAbsoluteError (early.lo, 0.0f, 1e-6f);
        // Resolution contract: bins are never wider than the queried span, so edges may over-report by < 1 span.
        auto none = peaks.query (20000, 60000);
        expectWithinAbsoluteError (none.hi, 0.0f, 1e-6f);

        beginTest ("WAV decode with ACID chunk tempo");
        auto f = writeWav ("acid_loop.wav", 44100, 2, 44100, [] (int c, int i) { return c == 0 ? 0.25f : (i % 2 ? 0.5f : -0.5f); }, 97.5, 4);
        auto data = loadAudioFile (f);
        expectEquals (data->numChannels(), 2);
        expectEquals (data->numFrames(), 44100);
        expectEquals (data->sampleRate, 44100.0);
        expectWithinAbsoluteError (data->pcm.getSample (0, 10), 0.25f, 1e-6f);
        expectEquals (data->detectedTempo.source, juce::String ("acid-chunk"));
        expectWithinAbsoluteError (data->detectedTempo.bpm, 97.5, 1e-3);

        beginTest ("tempo from file name when no chunk");
        auto named = writeWav ("hats_120bpm.wav", 48000, 1, 1000, [] (int, int) { return 0.1f; });
        expectEquals (loadAudioFile (named)->detectedTempo.bpm, 120.0);

        beginTest ("load errors are descriptive");
        expectThrows (loadAudioFile (scratchDir().getChildFile ("missing.wav")));
        auto junk = scratchDir().getChildFile ("junk.wav");
        junk.replaceWithText ("not audio");
        expectThrows (loadAudioFile (junk));
        auto txt = scratchDir().getChildFile ("notes.txt");
        txt.replaceWithText ("x");
        expectThrows (loadAudioFile (txt));
    }
};

class ProjectJsonTests final : public juce::UnitTest
{
public:
    ProjectJsonTests() : juce::UnitTest ("Project serialisation", "Core") {}

    void runTest() override
    {
        beginTest ("round trip through a file with relative asset paths");
        Session s;
        auto wav = dcWav ("loop_100bpm.wav", 0.5f);
        s.run ("import_audio", args ({ { "path", wav.getFullPathName() }, { "startBeat", 4 } }));
        const auto trackId = s.project().tracks[0].id;
        s.run ("add_effect", args ({ { "target", trackId }, { "type", "multitap_delay" }, { "parameters", args ({ { "tap1.division", "1/4D" } }) } }));
        s.run ("add_effect", args ({ { "target", "master" }, { "type", "compressor" } }));
        s.run ("set_track_pan", args ({ { "trackId", trackId }, { "pan", -0.25 } }));
        s.run ("set_loop_region", args ({ { "startBeat", 4 }, { "endBeat", 12 }, { "enabled", true } }));

        auto file = scratchDir().getChildFile ("song.mcpdaw");
        s.run ("save_project", args ({ { "path", file.getFullPathName() } }));
        expect (! s.controller.isDirty());
        const auto text = file.loadFileAsString();
        expect (text.contains ("\"path\": \"loop_100bpm.wav\"") || text.contains ("\"path\":\"loop_100bpm.wav\""), "asset path stored relative");

        auto loaded = loadProjectFile (file);
        const auto a = juce::JSON::toString (projectToFileJson (s.project(), file.getParentDirectory()));
        const auto b = juce::JSON::toString (projectToFileJson (loaded, file.getParentDirectory()));
        expectEquals (b, a);
        expectEquals (loaded.bpm, 100.0);
        expectEquals (loaded.tracks[0].effects[0].values[static_cast<size_t> (findEffectType ("multitap_delay")->indexOf ("tap1.division"))],
                      static_cast<float> (13));   // "1/4D" index in the division list

        beginTest ("forward compatibility: unknown effect params ignored, missing ones default");
        auto json = projectToFileJson (s.project(), file.getParentDirectory());
        auto fx = json["master"]["effects"][0];
        fx["parameters"].getDynamicObject()->setProperty ("futureKnob", 3.0);
        fx["parameters"].getDynamicObject()->removeProperty ("ratio");
        auto reloaded = projectFromFileJson (json, file.getParentDirectory());
        expectEquals (reloaded.master.effects[0].values[1], 4.0f);   // compressor ratio default

        beginTest ("invalid files are rejected with an error");
        auto bad = projectToFileJson (s.project(), file.getParentDirectory());
        bad.getDynamicObject()->setProperty ("bpm", 1000);
        expectThrows (projectFromFileJson (bad, file.getParentDirectory()));
        auto newer = projectToFileJson (s.project(), file.getParentDirectory());
        newer.getDynamicObject()->setProperty ("version", 99);
        expectThrows (projectFromFileJson (newer, file.getParentDirectory()));
        auto dangling = projectToFileJson (s.project(), file.getParentDirectory());
        dangling["tracks"][0]["events"][0].getDynamicObject()->setProperty ("assetId", "asset-999");
        expectThrows (projectFromFileJson (dangling, file.getParentDirectory()));
    }
};

static MusicalTimeTests musicalTimeTests;
static AudioAssetTests audioAssetTests;
static ProjectJsonTests projectJsonTests;
} // namespace mcpdaw::test
