// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "ProjectJson.h"
#include "JsonArgs.h"
#include <stdexcept>

namespace mcpdaw
{
juce::var makeObject (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [key, v] : properties) o->setProperty (key, v);
    return juce::var (o);
}

juce::String formatBarBeat (Tick t)
{
    const auto bbt = toBarBeatTick (t);
    return juce::String (bbt.bar) + "." + juce::String (bbt.beat) + "." + juce::String (bbt.tick).paddedLeft ('0', 3);
}

juce::String occupiedBars (const Track& track)
{
    // Merge event spans (in whole bars) into sorted ranges.
    std::vector<std::pair<Tick, Tick>> spans;
    for (const auto& e : track.events)
        spans.emplace_back (snapTicksDown (e.start, kTicksPerBar), e.end());
    std::sort (spans.begin(), spans.end());
    std::vector<std::pair<Tick, Tick>> merged;
    for (const auto& s : spans)
    {
        if (! merged.empty() && s.first <= merged.back().second) merged.back().second = std::max (merged.back().second, s.second);
        else merged.push_back (s);
    }
    juce::StringArray parts;
    for (const auto& [a, b] : merged)
    {
        const int firstBar = static_cast<int> (a / kTicksPerBar) + 1;
        const int lastBar = static_cast<int> ((b - 1) / kTicksPerBar) + 1;
        parts.add (firstBar == lastBar ? juce::String (firstBar) : juce::String (firstBar) + "-" + juce::String (lastBar));
    }
    return parts.joinIntoString (", ");
}

// ================================================================================================
// Effects
// ================================================================================================
namespace
{
/** Floats widened to double print as 0.7070000171661377; keep JSON readable and stable. */
double tidy (float v) { return std::round (static_cast<double> (v) * 1.0e5) / 1.0e5; }

juce::var effectValuesJson (const EffectInstance& fx)
{
    auto* o = new juce::DynamicObject();
    if (const auto* d = fx.descriptor())
        for (size_t i = 0; i < d->params.size() && i < fx.values.size(); ++i)
            o->setProperty (d->params[i].id, tidy (fx.values[i]));
    return juce::var (o);
}

juce::var paramSpecJson (const ParamSpec& p, float value, bool includeValue)
{
    auto o = makeObject ({ { "id", p.id }, { "name", p.name }, { "group", p.group },
                           { "kind", p.kind == ParamKind::Toggle ? "toggle" : p.kind == ParamKind::Choice ? "choice" : "continuous" },
                           { "min", p.min }, { "max", p.max }, { "default", p.def } });
    if (p.unit.isNotEmpty()) o.getDynamicObject()->setProperty ("unit", p.unit);
    if (p.help.isNotEmpty()) o.getDynamicObject()->setProperty ("help", p.help);
    if (p.kind == ParamKind::Choice)
    {
        juce::Array<juce::var> items;
        for (const auto& c : p.choices) items.add (c);
        o.getDynamicObject()->setProperty ("choices", items);
    }
    if (includeValue)
    {
        o.getDynamicObject()->setProperty ("value", tidy (value));
        o.getDynamicObject()->setProperty ("display", p.format (value));
    }
    return o;
}

juce::var chainJson (const std::vector<EffectInstance>& chain, bool compact)
{
    juce::Array<juce::var> list;
    for (const auto& fx : chain) list.add (compact ? effectCompactJson (fx) : effectJson (fx, false));
    return list;
}
} // namespace

juce::var paramValueJson (const ParamSpec& p, float value)
{
    switch (p.kind)
    {
        case ParamKind::Toggle: return value >= 0.5f;
        case ParamKind::Choice: return p.format (value);
        case ParamKind::Continuous: break;
    }
    return tidy (value);
}

juce::var effectCompactJson (const EffectInstance& fx)
{
    auto* values = new juce::DynamicObject();
    if (const auto* d = fx.descriptor())
        for (size_t i = 0; i < d->params.size() && i < fx.values.size(); ++i)
            if (tidy (fx.values[i]) != tidy (d->params[i].def))
                values->setProperty (d->params[i].id, paramValueJson (d->params[i], fx.values[i]));
    return makeObject ({ { "id", fx.id }, { "type", fx.type }, { "bypassed", fx.bypassed }, { "values", juce::var (values) } });
}

juce::var effectTypeBriefJson (const EffectDescriptor& d)
{
    return makeObject ({ { "type", d.type }, { "name", d.name }, { "description", d.description } });
}

juce::var effectJson (const EffectInstance& fx, bool includeSpecs)
{
    const auto* d = fx.descriptor();
    auto o = makeObject ({ { "id", fx.id }, { "type", fx.type }, { "name", d ? d->name : fx.type }, { "bypassed", fx.bypassed },
                           { "parameters", effectValuesJson (fx) } });
    if (includeSpecs && d != nullptr)
    {
        juce::Array<juce::var> specs;
        for (size_t i = 0; i < d->params.size(); ++i) specs.add (paramSpecJson (d->params[i], fx.values[i], true));
        o.getDynamicObject()->setProperty ("parameterDetails", specs);
    }
    return o;
}

juce::var effectTypeJson (const EffectDescriptor& d)
{
    juce::Array<juce::var> specs;
    for (const auto& p : d.params) specs.add (paramSpecJson (p, p.def, false));
    return makeObject ({ { "type", d.type }, { "name", d.name }, { "description", d.description }, { "parameters", specs } });
}

// ================================================================================================
// State JSON (semantic, for MCP)
// ================================================================================================
juce::var assetJson (const Asset& a)
{
    auto o = makeObject ({ { "id", a.id }, { "name", a.displayName() }, { "path", a.path },
                           { "durationSeconds", a.durationSeconds() },
                           { "sampleRate", a.data ? a.data->sampleRate : 0.0 },
                           { "channels", a.data ? a.data->numChannels() : 0 },
                           { "sourceBpm", a.sourceBpm }, { "tempoSource", a.tempoSource } });
    if (a.sourceBpm > 0) o.getDynamicObject()->setProperty ("sourceBeats", a.sourceBeats());
    else if (a.data)
    {
        const auto guess = estimateLoopTempo (a.durationSeconds());
        if (guess.bpm > 0)
            o.getDynamicObject()->setProperty ("suggestedBpm", makeObject ({ { "bpm", guess.bpm }, { "beats", guess.beats },
                                                                              { "note", "Heuristic from duration; confirm with set_asset_bpm" } }));
    }
    return o;
}

juce::var eventJson (const Project& p, const Event& e, double declickMs)
{
    const auto* a = p.findAsset (e.assetId);
    const Tick cycle = sourceCycleTicks (p, e);
    auto o = makeObject ({ { "id", e.id }, { "assetId", e.assetId }, { "assetName", a ? a->displayName() : juce::String() },
                           { "startBeat", ticksToBeats (e.start) }, { "lengthBeats", ticksToBeats (e.length) },
                           { "endBeat", ticksToBeats (e.end()) },
                           { "start", formatBarBeat (e.start) }, { "end", formatBarBeat (e.end()) },
                           { "looping", e.looping }, { "reversed", e.reversed }, { "sourceOffsetSeconds", e.sourceOffsetSeconds },
                           { "gainDb", e.gainDb }, { "pitchSemitones", e.pitchSemitones },
                           { "fadeInBeats", ticksToBeats (e.fadeInTicks) }, { "fadeOutBeats", ticksToBeats (e.fadeOutTicks) },
                           { "fadeInCurve", fadeCurveName (e.fadeInCurve) }, { "fadeOutCurve", fadeCurveName (e.fadeOutCurve) } });
    if (declickMs >= 0.0)
    {
        // What the renderer actually applies: the musical fade, or the Default declick time if longer
        // (never more than the event itself). Derived, never stored.
        const double lengthMs = ticksToSeconds (e.length, p.bpm) * 1000.0;
        auto effective = [&] (Tick fade) { return std::min (lengthMs, std::max (ticksToSeconds (fade, p.bpm) * 1000.0, declickMs)); };
        o.getDynamicObject()->setProperty ("effectiveFadeInMs", effective (e.fadeInTicks));
        o.getDynamicObject()->setProperty ("effectiveFadeOutMs", effective (e.fadeOutTicks));
    }
    if (cycle > 0)
    {
        o.getDynamicObject()->setProperty ("sourceCycleBeats", ticksToBeats (cycle));
        o.getDynamicObject()->setProperty ("repetitions", static_cast<double> (e.length) / static_cast<double> (cycle));
    }
    return o;
}

juce::var trackJson (const Project& p, const Track& t, double declickMs, bool compactEffects)
{
    std::vector<const Event*> sorted;
    for (const auto& e : t.events) sorted.push_back (&e);
    std::sort (sorted.begin(), sorted.end(), [] (auto* a, auto* b) { return a->start < b->start; });

    juce::Array<juce::var> events;
    for (const auto* e : sorted) events.add (eventJson (p, *e, declickMs));
    const bool audible = ! t.mute && (! p.anySolo() || t.solo);
    return makeObject ({ { "id", t.id }, { "name", t.name }, { "index", p.trackIndex (t.id) }, { "colour", t.colour },
                         { "gainDb", t.gainDb }, { "pan", t.pan }, { "mute", t.mute }, { "solo", t.solo }, { "audible", audible },
                         { "occupiedBars", occupiedBars (t) }, { "events", events }, { "effects", chainJson (t.effects, compactEffects) } });
}

juce::var projectStateJson (const Project& p, double declickMs, bool compactEffects)
{
    juce::Array<juce::var> assets, tracks;
    for (const auto& a : p.assets) assets.add (assetJson (a));
    for (const auto& t : p.tracks) tracks.add (trackJson (p, t, declickMs, compactEffects));
    const Tick end = p.contentEnd();
    auto state = makeObject ({
        { "name", p.name }, { "bpm", p.bpm }, { "timeSignature", "4/4" }, { "ticksPerBeat", static_cast<int> (kTicksPerBeat) },
        { "positionUnits", "zero-based beats (quarter notes); bar N starts at beat (N-1)*4" },
        { "lengthBeats", ticksToBeats (end) }, { "lengthBars", static_cast<double> (end) / kTicksPerBar },
        { "loop", makeObject ({ { "enabled", p.loopEnabled }, { "startBeat", ticksToBeats (p.loopStart) }, { "endBeat", ticksToBeats (p.loopEnd) } }) },
        { "master", makeObject ({ { "gainDb", p.master.gainDb }, { "effects", chainJson (p.master.effects, compactEffects) } }) },
        { "assets", assets }, { "tracks", tracks } });
    if (declickMs >= 0.0)
        state.getDynamicObject()->setProperty ("editing", makeObject ({ { "defaultDeclickMs", declickMs },
            { "note", "Event fades never render shorter than defaultDeclickMs (an application preference, not saved in the project)" } }));
    return state;
}

// ================================================================================================
// File JSON
// ================================================================================================
namespace
{
constexpr const char* kFormatName = "mcp-daw-project";

juce::var chainFileJson (const std::vector<EffectInstance>& chain)
{
    juce::Array<juce::var> list;
    for (const auto& fx : chain)
        list.add (makeObject ({ { "id", fx.id }, { "type", fx.type }, { "bypassed", fx.bypassed }, { "parameters", effectValuesJson (fx) } }));
    return list;
}

std::vector<EffectInstance> chainFromJson (const JsonArgs& owner, const char* key)
{
    std::vector<EffectInstance> chain;
    if (! owner.has (key)) return chain;
    for (const auto& item : owner.array (key))
    {
        JsonArgs j (item, "effect");
        EffectInstance fx;
        fx.id = j.string ("id");
        fx.type = j.string ("type");
        fx.bypassed = j.optBoolean ("bypassed").value_or (false);
        const auto* d = fx.descriptor();
        if (d == nullptr) j.fail ("unknown effect type '" + fx.type + "'");
        fx.values = d->defaults();
        // Unknown parameters are ignored and missing ones keep defaults: forward/backward compatible.
        if (j.has ("parameters"))
        {
            const auto params = j.object ("parameters");
            for (size_t i = 0; i < d->params.size(); ++i)
                if (auto v = params.optNumber (d->params[i].id.toRawUTF8()))
                    fx.values[i] = d->params[i].clamp (static_cast<float> (*v));
        }
        chain.push_back (std::move (fx));
    }
    return chain;
}
} // namespace

juce::var projectToFileJson (const Project& p, const juce::File& dir)
{
    juce::Array<juce::var> assets, tracks;
    for (const auto& a : p.assets)
    {
        const juce::File f (a.path);
        const bool relative = dir != juce::File() && f.isAChildOf (dir);
        assets.add (makeObject ({ { "id", a.id }, { "path", relative ? f.getRelativePathFrom (dir).replaceCharacter ('\\', '/') : a.path },
                                  { "sourceBpm", a.sourceBpm }, { "tempoSource", a.tempoSource } }));
    }
    for (const auto& t : p.tracks)
    {
        juce::Array<juce::var> events;
        for (const auto& e : t.events)
            events.add (makeObject ({ { "id", e.id }, { "assetId", e.assetId }, { "startTicks", e.start }, { "lengthTicks", e.length },
                                      { "sourceOffsetSeconds", e.sourceOffsetSeconds }, { "looping", e.looping },
                                      { "gainDb", e.gainDb }, { "pitchSemitones", e.pitchSemitones },
                                      { "fadeInTicks", e.fadeInTicks }, { "fadeOutTicks", e.fadeOutTicks },
                                      { "fadeInCurve", fadeCurveName (e.fadeInCurve) }, { "fadeOutCurve", fadeCurveName (e.fadeOutCurve) },
                                      { "reversed", e.reversed } }));
        tracks.add (makeObject ({ { "id", t.id }, { "name", t.name }, { "colour", t.colour }, { "gainDb", t.gainDb }, { "pan", t.pan },
                                  { "mute", t.mute }, { "solo", t.solo }, { "events", events }, { "effects", chainFileJson (t.effects) } }));
    }
    return makeObject ({
        { "format", kFormatName }, { "version", Project::kFormatVersion }, { "generator", "MCP DAW " MCPDAW_VERSION_STRING },
        { "ticksPerBeat", static_cast<int> (kTicksPerBeat) }, { "timeSignature", "4/4" },
        { "name", p.name }, { "bpm", p.bpm }, { "nextSerial", p.nextSerial },
        { "loop", makeObject ({ { "enabled", p.loopEnabled }, { "startTicks", p.loopStart }, { "endTicks", p.loopEnd } }) },
        { "master", makeObject ({ { "gainDb", p.master.gainDb }, { "effects", chainFileJson (p.master.effects) } }) },
        { "assets", assets }, { "tracks", tracks } });
}

Project projectFromFileJson (const juce::var& json, const juce::File& dir, const AssetLoader& loaderIn, int* formatVersionRead)
{
    const AssetLoader loader = loaderIn ? loaderIn : AssetLoader ([] (const juce::File& f) { return loadAudioFile (f); });
    JsonArgs root (json, "project");
    if (root.string ("format") != kFormatName) root.fail ("not an MCP DAW project file");
    const auto version = root.integer ("version", 1, 1000);
    if (version > Project::kFormatVersion)
        root.fail ("file format version " + juce::String (version) + " is newer than this build supports (" + juce::String (Project::kFormatVersion) + ")");
    if (root.integer ("ticksPerBeat", 1, 100000) != kTicksPerBeat) root.fail ("unsupported ticksPerBeat");

    Project p;
    p.name = root.string ("name");
    p.bpm = root.number ("bpm", kMinBpm, kMaxBpm);
    p.nextSerial = static_cast<int> (root.integer ("nextSerial", 1, 1'000'000'000));
    const auto loop = root.object ("loop");
    p.loopEnabled = loop.boolean ("enabled");
    p.loopStart = loop.integer ("startTicks", 0, kMaxProjectTicks);
    p.loopEnd = loop.integer ("endTicks", 0, kMaxProjectTicks);
    const auto master = root.object ("master");
    p.master.gainDb = master.number ("gainDb", kMinDb, kMaxDb);
    p.master.effects = chainFromJson (master, "effects");

    for (const auto& item : root.array ("assets"))
    {
        JsonArgs j (item, "asset");
        Asset a;
        a.id = j.string ("id");
        const auto stored = j.string ("path");
        const juce::File file = juce::File::isAbsolutePath (stored) ? juce::File (stored) : dir.getChildFile (stored);
        a.path = file.getFullPathName();
        a.sourceBpm = j.number ("sourceBpm", 0.0, kMaxBpm);
        a.tempoSource = j.optString ("tempoSource").value_or ("");
        a.data = loader (file);
        p.assets.push_back (std::move (a));
    }

    for (const auto& item : root.array ("tracks"))
    {
        JsonArgs j (item, "track");
        Track t;
        t.id = j.string ("id");
        t.name = j.string ("name");
        t.colour = static_cast<int> (j.optNumber ("colour", 0, 64).value_or (0));
        t.gainDb = j.number ("gainDb", kMinDb, kMaxDb);
        t.pan = j.number ("pan", -1.0, 1.0);
        t.mute = j.boolean ("mute");
        t.solo = j.boolean ("solo");
        for (const auto& ev : j.array ("events"))
        {
            JsonArgs je (ev, "event");
            Event e;
            e.id = je.string ("id");
            e.assetId = je.string ("assetId");
            e.start = je.integer ("startTicks", 0, kMaxProjectTicks);
            e.length = je.integer ("lengthTicks", 1, kMaxProjectTicks);
            e.sourceOffsetSeconds = je.number ("sourceOffsetSeconds", 0.0, 1.0e6);
            e.looping = je.boolean ("looping");
            e.gainDb = je.optNumber ("gainDb", kMinDb, kMaxDb).value_or (0.0);
            e.pitchSemitones = je.optNumber ("pitchSemitones", -kMaxPitchSemitones, kMaxPitchSemitones).value_or (0.0);
            // Format v2 fields. Optional on read, so v1 files (and hand-written ones) get the defaults:
            // no musical fades, linear curves, forwards.
            e.fadeInTicks = je.has ("fadeInTicks") ? je.integer ("fadeInTicks", 0, kMaxProjectTicks) : 0;
            e.fadeOutTicks = je.has ("fadeOutTicks") ? je.integer ("fadeOutTicks", 0, kMaxProjectTicks) : 0;
            auto curve = [&] (const char* key)
            {
                const auto name = je.optString (key).value_or ("linear");
                const auto c = fadeCurveFromName (name.toRawUTF8());
                if (! c) je.fail ("'" + juce::String (key) + "' must be linear, smooth, fast or slow");
                return *c;
            };
            e.fadeInCurve = curve ("fadeInCurve");
            e.fadeOutCurve = curve ("fadeOutCurve");
            e.reversed = je.optBoolean ("reversed").value_or (false);
            e.clampFades();
            t.events.push_back (std::move (e));
        }
        t.effects = chainFromJson (j, "effects");
        p.tracks.push_back (std::move (t));
    }

    validateProject (p);
    if (formatVersionRead != nullptr) *formatVersionRead = static_cast<int> (version);
    return p;
}

void saveProjectFile (const Project& p, const juce::File& file)
{
    if (! file.getParentDirectory().isDirectory())
        throw std::runtime_error (("Folder does not exist: " + file.getParentDirectory().getFullPathName()).toStdString());
    const auto text = juce::JSON::toString (projectToFileJson (p, file.getParentDirectory()), false);
    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (text, false, false, "\n"))
        throw std::runtime_error (("Could not write " + temp.getFile().getFullPathName()).toStdString());
    if (! temp.overwriteTargetFileWithTemporary())
        throw std::runtime_error (("Could not replace " + file.getFullPathName()).toStdString());
}

Project loadProjectFile (const juce::File& file, const AssetLoader& loader, int* formatVersionRead)
{
    if (! file.existsAsFile()) throw std::runtime_error (("Project file not found: " + file.getFullPathName()).toStdString());
    if (file.getSize() > 64 * 1024 * 1024) throw std::runtime_error ("Project file is implausibly large (>64 MiB)");
    juce::var json;
    const auto result = juce::JSON::parse (file.loadFileAsString(), json);
    if (result.failed()) throw std::runtime_error (("Invalid JSON in project file: " + result.getErrorMessage()).toStdString());
    return projectFromFileJson (json, file.getParentDirectory(), loader, formatVersionRead);
}
} // namespace mcpdaw
