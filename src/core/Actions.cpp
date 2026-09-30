// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Actions.h"
#include "ProjectJson.h"
#include <stdexcept>

namespace mcpdaw
{
// ================================================================================================
// Schema builder
// ================================================================================================
Schema& Schema::add (const char* name, juce::var definition, bool required)
{
    properties.getDynamicObject()->setProperty (name, definition);
    if (required) requiredNames.add (name);
    return *this;
}

Schema& Schema::string (const char* name, const char* description, bool required)
{
    return add (name, makeObject ({ { "type", "string" }, { "description", description } }), required);
}

Schema& Schema::number (const char* name, const char* description, bool required, std::optional<double> min, std::optional<double> max)
{
    auto d = makeObject ({ { "type", "number" }, { "description", description } });
    if (min) d.getDynamicObject()->setProperty ("minimum", *min);
    if (max) d.getDynamicObject()->setProperty ("maximum", *max);
    return add (name, d, required);
}

Schema& Schema::integer (const char* name, const char* description, bool required, std::optional<double> min, std::optional<double> max)
{
    auto d = makeObject ({ { "type", "integer" }, { "description", description } });
    if (min) d.getDynamicObject()->setProperty ("minimum", *min);
    if (max) d.getDynamicObject()->setProperty ("maximum", *max);
    return add (name, d, required);
}

Schema& Schema::boolean (const char* name, const char* description, bool required)
{
    return add (name, makeObject ({ { "type", "boolean" }, { "description", description } }), required);
}

Schema& Schema::enumeration (const char* name, const char* description, const juce::StringArray& values, bool required)
{
    juce::Array<juce::var> items;
    for (const auto& v : values) items.add (v);
    return add (name, makeObject ({ { "type", "string" }, { "enum", items }, { "description", description } }), required);
}

Schema& Schema::object (const char* name, const char* description, bool required)
{
    return add (name, makeObject ({ { "type", "object" }, { "description", description } }), required);
}

Schema& Schema::array (const char* name, const char* description, juce::var itemSchema, bool required)
{
    return add (name, makeObject ({ { "type", "array" }, { "items", itemSchema }, { "description", description } }), required);
}

Schema& Schema::numberOrString (const char* name, const char* description, bool required)
{
    juce::Array<juce::var> types { "number", "string", "boolean" };
    return add (name, makeObject ({ { "type", types }, { "description", description } }), required);
}

juce::var Schema::build() const
{
    return makeObject ({ { "type", "object" }, { "properties", properties }, { "required", requiredNames },
                         { "additionalProperties", false } });
}

// ================================================================================================
// Registry
// ================================================================================================
void ActionRegistry::add (ActionDef def)
{
    jassert (find (def.name) == nullptr);
    actions.push_back (std::move (def));
}

const ActionDef* ActionRegistry::find (const juce::String& name) const noexcept
{
    for (const auto& a : actions)
        if (a.name == name) return &a;
    return nullptr;
}

juce::var ActionRegistry::execute (ProjectController& controller, const juce::String& name, const juce::var& argsIn)
{
    const auto* def = find (name);
    if (def == nullptr) throw std::runtime_error (("Unknown action '" + name + "'").toStdString());

    const juce::var args = argsIn.isObject() ? argsIn : juce::var (new juce::DynamicObject());
    const auto& props = def->inputSchema["properties"];
    for (const auto& required : *def->inputSchema["required"].getArray())
        if (! args.hasProperty (required.toString()))
            throw std::runtime_error ((name + ": missing required argument '" + required.toString() + "'").toStdString());
    for (const auto& kv : args.getDynamicObject()->getProperties())
        if (! props.hasProperty (kv.name))
            throw std::runtime_error ((name + ": unknown argument '" + kv.name.toString() + "'").toStdString());

    ActionContext ctx { controller, *this, JsonArgs (args, name) };
    return def->run (ctx);
}

// ================================================================================================
// Editing helpers
// ================================================================================================
namespace
{
[[noreturn]] void fail (const juce::String& message) { throw std::runtime_error (message.toStdString()); }

/** Advance an event's source offset by `delta` ticks of timeline (unstretched: real time). */
double advancedOffset (const Project& p, const Event& e, Tick delta)
{
    double offset = e.sourceOffsetSeconds + ticksToSeconds (delta, p.bpm) * e.rate();
    if (e.looping)
        if (const auto* a = p.findAsset (e.assetId); a != nullptr && a->durationSeconds() > 0)
        {
            offset = std::fmod (offset, a->durationSeconds());
            if (offset < 0) offset += a->durationSeconds();
        }
    return offset;
}

/** Default for new events: tempo-based assets loop; one-shots (ACID flag, or short with no tempo) don't. */
bool loopsByDefault (const Asset& a)
{
    if (a.sourceBpm > 0.0) return true;
    if (a.data != nullptr && a.data->detectedTempo.oneShot) return false;
    return a.durationSeconds() >= kOneShotMaxSeconds;
}

Track& requireTrack (Project& p, const juce::String& id)
{
    if (auto* t = p.findTrack (id)) return *t;
    juce::StringArray known;
    for (const auto& t : p.tracks) known.add (t.id + " (" + t.name + ")");
    fail ("Track '" + id + "' not found. Tracks: " + (known.isEmpty() ? juce::String ("none") : known.joinIntoString (", ")));
}

Project::EventRef requireEvent (Project& p, const juce::String& id)
{
    auto ref = p.findEvent (id);
    if (ref.event == nullptr) fail ("Event '" + id + "' not found");
    return ref;
}

const Asset& requireAsset (const Project& p, const juce::String& id)
{
    if (const auto* a = p.findAsset (id)) return *a;
    fail ("Asset '" + id + "' not found");
}

EffectInstance& requireEffect (Project& p, const juce::String& id)
{
    if (auto* fx = p.findEffect (id)) return *fx;
    fail ("Effect '" + id + "' not found");
}

Tick beatArg (const JsonArgs& a, const char* key) { return beatsToTicks (a.number (key, 0.0, ticksToBeats (kMaxProjectTicks))); }

std::optional<Tick> optBeatArg (const JsonArgs& a, const char* key)
{
    if (auto v = a.optNumber (key, 0.0, ticksToBeats (kMaxProjectTicks))) return beatsToTicks (*v);
    return std::nullopt;
}

Tick lengthArg (const JsonArgs& a, const char* key)
{
    const auto t = beatsToTicks (a.number (key, 1.0 / kTicksPerBeat, ticksToBeats (kMaxProjectTicks)));
    if (t <= 0) a.fail ("'" + juce::String (key) + "' must be positive");
    return t;
}

/** Resolve a list of track ids (or all tracks when absent). */
std::vector<juce::String> trackIdsArg (const Project& p, const JsonArgs& a, const char* key)
{
    std::vector<juce::String> ids;
    if (a.has (key))
    {
        for (const auto& v : a.array (key))
        {
            if (! v.isString()) a.fail ("'" + juce::String (key) + "' must contain track id strings");
            if (p.findTrack (v.toString()) == nullptr) fail ("Track '" + v.toString() + "' not found");
            ids.push_back (v.toString());
        }
    }
    else
        for (const auto& t : p.tracks) ids.push_back (t.id);
    return ids;
}

std::vector<EffectInstance>& chainForTarget (Project& p, const juce::String& target)
{
    if (target == "master") return p.master.effects;
    return requireTrack (p, target).effects;
}

void sortEvents (Track& t)
{
    std::stable_sort (t.events.begin(), t.events.end(), [] (const Event& a, const Event& b) { return a.start < b.start; });
}

Track makeTrack (Project& p, const juce::String& name)
{
    Track t;
    t.id = p.allocateId ("track");
    t.name = name.isNotEmpty() ? name : "Track " + juce::String (p.tracks.size() + 1);
    t.colour = static_cast<int> (p.tracks.size() % 8);
    return t;
}

juce::String requireTransportMessage() { return "No audio engine is attached to this session"; }
} // namespace

void carveOut (Project& p, Track& track, Tick start, Tick end, const juce::String& keepId)
{
    if (end <= start) return;
    std::vector<Event> result;
    result.reserve (track.events.size() + 1);
    for (const auto& e : track.events)
    {
        if (e.id == keepId || e.end() <= start || e.start >= end)
        {
            result.push_back (e);
            continue;
        }
        if (e.start < start)                       // keep the part before the carved region
        {
            Event left = e;
            left.length = start - e.start;
            result.push_back (left);
        }
        if (e.end() > end)                         // keep the part after the carved region
        {
            Event right = e;
            if (e.start < start) right.id = p.allocateId ("event");   // original id stays with the left part
            right.sourceOffsetSeconds = advancedOffset (p, e, end - e.start);
            right.start = end;
            right.length = e.end() - end;
            result.push_back (right);
        }
    }
    track.events = std::move (result);
    sortEvents (track);
}

juce::String splitEventAt (Project& p, Track& track, const juce::String& eventId, Tick at)
{
    for (auto& e : track.events)
    {
        if (e.id != eventId) continue;
        if (at <= e.start || at >= e.end())
            fail ("Split position " + formatBarBeat (at) + " is not inside event " + eventId + " (" + formatBarBeat (e.start) + " - " + formatBarBeat (e.end()) + ")");
        Event right = e;
        right.id = p.allocateId ("event");
        right.start = at;
        right.length = e.end() - at;
        right.sourceOffsetSeconds = advancedOffset (p, e, at - e.start);
        e.length = at - e.start;
        const auto id = right.id;
        track.events.push_back (std::move (right));
        sortEvents (track);
        return id;
    }
    fail ("Event '" + eventId + "' is not on track " + track.id);
}

Tick naturalLengthTicks (const Project& p, const Asset& a, double pitchSemitones)
{
    const double rate = std::pow (2.0, pitchSemitones / 12.0);
    return std::max<Tick> (1, beatsToTicks (secondsToBeats (a.durationSeconds() / rate, p.bpm)));
}

float parseParamValue (const ParamSpec& spec, const juce::var& v)
{
    if (v.isBool()) return spec.clamp (static_cast<bool> (v) ? 1.0f : 0.0f);
    if (v.isString())
    {
        const auto s = v.toString();
        if (spec.kind == ParamKind::Choice)
        {
            for (int i = 0; i < spec.choices.size(); ++i)
                if (spec.choices[i].equalsIgnoreCase (s)) return static_cast<float> (i);
            fail ("Parameter '" + spec.id + "': '" + s + "' is not one of " + spec.choices.joinIntoString (", "));
        }
        if (spec.kind == ParamKind::Toggle && (s.equalsIgnoreCase ("on") || s.equalsIgnoreCase ("off")))
            return s.equalsIgnoreCase ("on") ? 1.0f : 0.0f;
        fail ("Parameter '" + spec.id + "' expects a number");
    }
    if (! (v.isInt() || v.isInt64() || v.isDouble())) fail ("Parameter '" + spec.id + "' expects a number");
    const double d = static_cast<double> (v);
    if (! std::isfinite (d) || d < spec.min - 1e-6 || d > spec.max + 1e-6)
        fail ("Parameter '" + spec.id + "' = " + juce::String (d) + " out of range [" + juce::String (spec.min) + ", " + juce::String (spec.max) + "]");
    return spec.clamp (static_cast<float> (d));
}

// ================================================================================================
// Core actions
// ================================================================================================
void registerCoreActions (ActionRegistry& r)
{
    using K = ActionKind;

    // ---------------------------------------------------------------- queries
    r.add ({ "get_project_state",
             "Complete semantic project state: tempo, loop region, assets (with tempo metadata), tracks with events "
             "(positions in zero-based beats plus bar.beat.tick strings, loop repetitions, occupied bars) and effect chains.",
             Schema().build(), K::Query,
             [] (ActionContext& c) { return projectStateJson (c.project()); } });

    r.add ({ "list_tracks", "Summary of all tracks (id, name, mix settings, occupied bars, event count).", Schema().build(), K::Query,
             [] (ActionContext& c)
             {
                 juce::Array<juce::var> list;
                 const auto& p = c.project();
                 for (const auto& t : p.tracks)
                     list.add (makeObject ({ { "id", t.id }, { "name", t.name }, { "gainDb", t.gainDb }, { "pan", t.pan },
                                             { "mute", t.mute }, { "solo", t.solo }, { "eventCount", static_cast<int> (t.events.size()) },
                                             { "effectCount", static_cast<int> (t.effects.size()) }, { "occupiedBars", occupiedBars (t) } }));
                 return juce::var (list);
             } });

    r.add ({ "get_track", "Full detail for one track.", Schema().string ("trackId", "Track id").build(), K::Query,
             [] (ActionContext& c)
             {
                 const auto* t = c.project().findTrack (c.args.string ("trackId"));
                 if (t == nullptr) fail ("Track '" + c.args.string ("trackId") + "' not found");
                 return trackJson (c.project(), *t);
             } });

    r.add ({ "list_assets", "Imported audio sources with duration, sample rate and tempo metadata.", Schema().build(), K::Query,
             [] (ActionContext& c)
             {
                 juce::Array<juce::var> list;
                 for (const auto& a : c.project().assets) list.add (assetJson (a));
                 return juce::var (list);
             } });

    r.add ({ "list_effect_types", "Available native effect types with every parameter (id, range, unit, choices).", Schema().build(), K::Query,
             [] (ActionContext&)
             {
                 juce::Array<juce::var> list;
                 for (const auto& d : effectCatalogue()) list.add (effectTypeJson (d));
                 return juce::var (list);
             } });

    r.add ({ "get_effect_parameters", "Current parameter values of an effect, with specs and display strings.",
             Schema().string ("effectId", "Effect id").build(), K::Query,
             [] (ActionContext& c)
             {
                 const auto* fx = c.project().findEffect (c.args.string ("effectId"));
                 if (fx == nullptr) fail ("Effect '" + c.args.string ("effectId") + "' not found");
                 return effectJson (*fx, true);
             } });

    r.add ({ "get_history", "Undo/redo availability and labels (oldest first).", Schema().build(), K::Query,
             [] (ActionContext& c)
             {
                 juce::Array<juce::var> labels;
                 for (const auto& l : c.controller.historyLabels()) labels.add (l);
                 return makeObject ({ { "undo", labels }, { "canUndo", c.controller.canUndo() }, { "canRedo", c.controller.canRedo() },
                                      { "redoLabel", c.controller.redoLabel() }, { "dirty", c.controller.isDirty() },
                                      { "file", c.controller.currentFile().getFullPathName() } });
             } });

    // ---------------------------------------------------------------- project
    r.add ({ "set_project_tempo", "Set project tempo in BPM. Audio plays unstretched (no time-stretch yet): events keep their beat positions, so one-shots follow the new tempo while loops keep their own speed.",
             Schema().number ("bpm", "Tempo", true, kMinBpm, kMaxBpm).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const double bpm = c.args.number ("bpm", kMinBpm, kMaxBpm);
                 return c.edit ("Set tempo", [&] (Project& p) { p.bpm = bpm; return makeObject ({ { "bpm", bpm } }); });
             } });

    r.add ({ "set_project_name", "Rename the project.", Schema().string ("name", "New name").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto name = c.args.string ("name").trim();
                 if (name.isEmpty()) c.args.fail ("name must not be empty");
                 return c.edit ("Rename project", [&] (Project& p) { p.name = name; return juce::var(); });
             } });

    r.add ({ "set_loop_region", "Set the loop region (zero-based beats) and/or enable looped playback.",
             Schema().number ("startBeat", "Loop start", false, 0.0).number ("endBeat", "Loop end", false, 0.0)
                     .boolean ("enabled", "Loop playback on/off", false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto start = optBeatArg (c.args, "startBeat");
                 const auto end = optBeatArg (c.args, "endBeat");
                 const auto enabled = c.args.optBoolean ("enabled");
                 return c.edit ("Set loop region", [&] (Project& p)
                 {
                     if (start) p.loopStart = *start;
                     if (end) p.loopEnd = *end;
                     if (enabled) p.loopEnabled = *enabled;
                     return makeObject ({ { "startBeat", ticksToBeats (p.loopStart) }, { "endBeat", ticksToBeats (p.loopEnd) }, { "enabled", p.loopEnabled } });
                 });
             } });

    // ---------------------------------------------------------------- tracks
    r.add ({ "create_track", "Create an empty audio track. Returns its id.",
             Schema().string ("name", "Track name", false).integer ("index", "Insert position (0 = top)", false, 0).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto name = c.args.optString ("name").value_or ("");
                 const auto index = c.args.optNumber ("index");
                 return c.edit ("Create track", [&] (Project& p)
                 {
                     auto t = makeTrack (p, name);
                     const auto id = t.id;
                     const auto at = index ? std::min<size_t> (static_cast<size_t> (*index), p.tracks.size()) : p.tracks.size();
                     p.tracks.insert (p.tracks.begin() + static_cast<std::ptrdiff_t> (at), std::move (t));
                     return makeObject ({ { "trackId", id } });
                 });
             } });

    r.add ({ "delete_track", "Delete a track with all of its events and effects.", Schema().string ("trackId", "Track id").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("trackId");
                 return c.edit ("Delete track", [&] (Project& p)
                 {
                     requireTrack (p, id);
                     std::erase_if (p.tracks, [&] (const Track& t) { return t.id == id; });
                     return juce::var();
                 });
             } });

    r.add ({ "rename_track", "Rename a track.", Schema().string ("trackId", "Track id").string ("name", "New name").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("trackId");
                 const auto name = c.args.string ("name").trim();
                 return c.edit ("Rename track", [&] (Project& p) { requireTrack (p, id).name = name; return juce::var(); });
             } });

    r.add ({ "move_track", "Reorder a track.", Schema().string ("trackId", "Track id").integer ("index", "New index (0 = top)", true, 0).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("trackId");
                 const auto index = c.args.integer ("index", 0, Project::kMaxTracks);
                 return c.edit ("Move track", [&] (Project& p)
                 {
                     const int from = p.trackIndex (id);
                     if (from < 0) requireTrack (p, id);
                     auto t = std::move (p.tracks[static_cast<size_t> (from)]);
                     p.tracks.erase (p.tracks.begin() + from);
                     const auto to = std::min<size_t> (static_cast<size_t> (index), p.tracks.size());
                     p.tracks.insert (p.tracks.begin() + static_cast<std::ptrdiff_t> (to), std::move (t));
                     return juce::var();
                 });
             } });

    r.add ({ "set_track_colour", "Set a track's colour (palette index 0..7).",
             Schema().string ("trackId", "Track id").integer ("colour", "Palette index", true, 0, 7).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("trackId");
                 const auto colour = static_cast<int> (c.args.integer ("colour", 0, 7));
                 return c.edit ("Track colour", [&] (Project& p) { requireTrack (p, id).colour = colour; return juce::var(); });
             } });

    r.add ({ "set_track_gain", "Set track fader level in dB (-70 = silent, max +12).",
             Schema().string ("trackId", "Track id").number ("db", "Gain in dB", true, kMinDb, kMaxDb).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("trackId");
                 const double db = c.args.number ("db", kMinDb, kMaxDb);
                 return c.edit ("Track gain", [&] (Project& p) { requireTrack (p, id).gainDb = db; return juce::var(); });
             } });

    r.add ({ "set_track_pan", "Set track pan (-1 = left, 0 = centre, +1 = right).",
             Schema().string ("trackId", "Track id").number ("pan", "Pan position", true, -1.0, 1.0).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("trackId");
                 const double pan = c.args.number ("pan", -1.0, 1.0);
                 return c.edit ("Track pan", [&] (Project& p) { requireTrack (p, id).pan = pan; return juce::var(); });
             } });

    for (const auto* which : { "mute_track", "solo_track" })
    {
        const bool isMute = juce::String (which) == "mute_track";
        r.add ({ which, isMute ? "Mute or unmute a track." : "Solo or unsolo a track (any solo silences non-soloed tracks).",
                 Schema().string ("trackId", "Track id").boolean ("enabled", isMute ? "Muted" : "Soloed").build(), K::Edit,
                 [isMute] (ActionContext& c)
                 {
                     const auto id = c.args.string ("trackId");
                     const bool on = c.args.boolean ("enabled");
                     return c.edit (isMute ? "Mute" : "Solo", [&] (Project& p)
                     {
                         auto& t = requireTrack (p, id);
                         (isMute ? t.mute : t.solo) = on;
                         return juce::var();
                     });
                 } });
    }

    r.add ({ "set_master_gain", "Set master bus level in dB.", Schema().number ("db", "Gain in dB", true, kMinDb, kMaxDb).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const double db = c.args.number ("db", kMinDb, kMaxDb);
                 return c.edit ("Master gain", [&] (Project& p) { p.master.gainDb = db; return juce::var(); });
             } });

    // ---------------------------------------------------------------- audio / events
    r.add ({ "import_audio",
             "Import an audio file (WAV; AIFF/FLAC also decode) and by default place it as an event. Without trackId a new track "
             "named after the file is created. If the project is empty and the file carries tempo (ACID chunk or '90bpm' in the "
             "name) the project adopts that tempo. Returns assetId, trackId, eventId.",
             Schema().string ("path", "Absolute file path")
                     .string ("trackId", "Existing track to place on", false)
                     .number ("startBeat", "Placement (zero-based beats, default 0)", false, 0.0)
                     .number ("lengthBeats", "Event length (default: the file's natural length)", false, 0.0)
                     .number ("sourceBpm", "Override the file's tempo (0 = not tempo-based)", false, 0.0, kMaxBpm)
                     .boolean ("place", "Create an event (default true). false = only add to the asset pool", false)
                     .number ("pitchSemitones", "Transpose the placed event, -12..+12 (varispeed: also changes its speed)", false,
                              -kMaxPitchSemitones, kMaxPitchSemitones)
                     .build(), K::Edit,
             [] (ActionContext& c)
             {
                 const juce::File file (c.args.string ("path"));
                 if (! juce::File::isAbsolutePath (c.args.string ("path"))) c.args.fail ("path must be absolute");
                 const auto trackId = c.args.optString ("trackId");
                 const auto start = optBeatArg (c.args, "startBeat").value_or (0);
                 const auto lengthBeats = c.args.optNumber ("lengthBeats");
                 const auto bpmOverride = c.args.optNumber ("sourceBpm", 0.0, kMaxBpm);
                 const bool place = c.args.optBoolean ("place").value_or (true);
                 const double pitch = c.args.optNumber ("pitchSemitones", -kMaxPitchSemitones, kMaxPitchSemitones).value_or (0.0);

                 // Decode outside the edit so a decoding failure is reported before touching the model.
                 const Asset* existing = nullptr;
                 for (const auto& a : c.project().assets)
                     if (juce::File (a.path) == file) existing = &a;
                 AudioDataPtr data = existing ? existing->data : loadAudioFile (file);

                 return c.edit ("Import " + file.getFileName(), [&] (Project& p)
                 {
                     juce::String assetId;
                     bool tempoAdopted = false;
                     if (existing != nullptr && bpmOverride == std::nullopt) assetId = existing->id;
                     else
                     {
                         Asset a;
                         a.id = p.allocateId ("asset");
                         a.path = file.getFullPathName();
                         a.data = data;
                         a.sourceBpm = bpmOverride ? *bpmOverride : data->detectedTempo.bpm;
                         a.tempoSource = bpmOverride ? juce::String ("user") : data->detectedTempo.source;
                         if (a.sourceBpm > 0 && a.sourceBpm < kMinBpm) fail ("sourceBpm must be 0 or >= " + juce::String (kMinBpm));
                         assetId = a.id;
                         p.assets.push_back (std::move (a));
                     }
                     const auto& asset = *p.findAsset (assetId);
                     if (p.contentEnd() == 0 && asset.sourceBpm >= kMinBpm)
                     {
                         p.bpm = asset.sourceBpm;
                         tempoAdopted = true;
                     }

                     auto result = makeObject ({ { "assetId", assetId }, { "asset", assetJson (asset) }, { "projectBpm", p.bpm },
                                                 { "tempoAdopted", tempoAdopted } });
                     if (! place) return result;

                     Track* track = nullptr;
                     if (trackId) track = &requireTrack (p, *trackId);
                     else
                     {
                         p.tracks.push_back (makeTrack (p, asset.displayName()));
                         track = &p.tracks.back();
                     }
                     Event e;
                     e.id = p.allocateId ("event");
                     e.assetId = assetId;
                     e.start = start;
                     e.pitchSemitones = pitch;
                     e.length = lengthBeats ? std::max<Tick> (1, beatsToTicks (*lengthBeats)) : naturalLengthTicks (p, asset, pitch);
                     e.looping = loopsByDefault (asset);
                     carveOut (p, *track, e.start, e.end());
                     track->events.push_back (e);
                     sortEvents (*track);
                     result.getDynamicObject()->setProperty ("trackId", track->id);
                     result.getDynamicObject()->setProperty ("eventId", e.id);
                     result.getDynamicObject()->setProperty ("event", eventJson (p, e));
                     return result;
                 });
             } });

    r.add ({ "replace_track_audio",
             "Swap the sound file used by every event on a track (positions, lengths, looping and gains stay; a track "
             "still named after its old file is renamed). "
             "By default each event restarts from the top of the new file; keepOffsets=true keeps each event's "
             "start point inside the source (useful when swapping for a similar take). Returns the new assetId.",
             Schema().string ("trackId", "Track id").string ("path", "Absolute path of the new audio file")
                     .boolean ("keepOffsets", "Keep each event's source offset (default false)", false)
                     .build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto trackId = c.args.string ("trackId");
                 const juce::File file (c.args.string ("path"));
                 if (! juce::File::isAbsolutePath (c.args.string ("path"))) c.args.fail ("path must be absolute");
                 const bool keepOffsets = c.args.optBoolean ("keepOffsets").value_or (false);
                 if (c.project().findTrack (trackId) == nullptr) c.args.fail ("No track with id " + trackId);

                 // Decode before editing, so a bad file is reported without touching the model.
                 const Asset* existing = nullptr;
                 for (const auto& a : c.project().assets)
                     if (juce::File (a.path) == file) existing = &a;
                 AudioDataPtr data = existing ? existing->data : loadAudioFile (file);

                 return c.edit ("Replace audio on track", [&] (Project& p)
                 {
                     juce::String assetId;
                     if (existing != nullptr) assetId = existing->id;
                     else
                     {
                         Asset a;
                         a.id = p.allocateId ("asset");
                         a.path = file.getFullPathName();
                         a.data = data;
                         a.sourceBpm = data->detectedTempo.bpm;
                         a.tempoSource = data->detectedTempo.source;
                         assetId = a.id;
                         p.assets.push_back (std::move (a));
                     }
                     const double duration = p.findAsset (assetId)->durationSeconds();
                     auto& track = requireTrack (p, trackId);
                     // A track still named after its old file follows the new file's name.
                     if (! track.events.empty())
                         if (const auto* old = p.findAsset (track.events.front().assetId); old != nullptr && old->displayName() == track.name)
                             track.name = p.findAsset (assetId)->displayName();
                     int changed = 0;
                     for (auto& e : track.events)
                     {
                         e.assetId = assetId;
                         if (! keepOffsets || e.sourceOffsetSeconds >= duration) e.sourceOffsetSeconds = 0.0;
                         ++changed;
                     }
                     return makeObject ({ { "assetId", assetId }, { "asset", assetJson (*p.findAsset (assetId)) },
                                          { "eventsChanged", changed } });
                 });
             } });

    r.add ({ "set_asset_bpm", "Declare the native tempo of an asset (0 = not tempo-based / one-shot).",
             Schema().string ("assetId", "Asset id").number ("bpm", "Tempo or 0", true, 0.0, kMaxBpm).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("assetId");
                 const double bpm = c.args.number ("bpm", 0.0, kMaxBpm);
                 return c.edit ("Set asset tempo", [&] (Project& p)
                 {
                     requireAsset (p, id);
                     for (auto& a : p.assets)
                         if (a.id == id) { a.sourceBpm = bpm; a.tempoSource = "user"; }
                     return juce::var();
                 });
             } });

    r.add ({ "create_event",
             "Place an asset on a track. Overlapped parts of existing events on that track are replaced (loop-arranger overwrite, like painting over tape).",
             Schema().string ("trackId", "Track id").string ("assetId", "Asset id")
                     .number ("startBeat", "Start (zero-based beats)", true, 0.0)
                     .number ("lengthBeats", "Length in beats (default: natural length). Longer than the source repeats it when looping", false, 0.0)
                     .boolean ("looping", "Repeat the source to fill the event (default: true for loops, false for one-shots / files under 2 s without tempo)", false)
                     .number ("sourceOffsetSeconds", "Start point inside the source", false, 0.0)
                     .number ("pitchSemitones", "Transpose, -12..+12 semitones (varispeed like a sampler: +12 = octave up, twice as fast)",
                              false, -kMaxPitchSemitones, kMaxPitchSemitones)
                     .build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto trackId = c.args.string ("trackId");
                 const auto assetId = c.args.string ("assetId");
                 const auto start = beatArg (c.args, "startBeat");
                 const auto length = c.args.has ("lengthBeats") ? std::optional<Tick> (lengthArg (c.args, "lengthBeats")) : std::nullopt;
                 const auto looping = c.args.optBoolean ("looping");
                 const auto offset = c.args.optNumber ("sourceOffsetSeconds", 0.0, 1.0e6).value_or (0.0);
                 const auto pitch = c.args.optNumber ("pitchSemitones", -kMaxPitchSemitones, kMaxPitchSemitones).value_or (0.0);
                 return c.edit ("Create event", [&] (Project& p)
                 {
                     auto& t = requireTrack (p, trackId);
                     const auto& a = requireAsset (p, assetId);
                     Event e;
                     e.id = p.allocateId ("event");
                     e.assetId = assetId;
                     e.start = start;
                     e.pitchSemitones = pitch;
                     e.length = length.value_or (naturalLengthTicks (p, a, pitch));
                     e.looping = looping.value_or (loopsByDefault (a));
                     e.sourceOffsetSeconds = offset;
                     carveOut (p, t, e.start, e.end());
                     t.events.push_back (e);
                     sortEvents (t);
                     return eventJson (p, e);
                 });
             } });

    r.add ({ "move_event", "Move an event to a new start (absolute startBeat or relative deltaBeats) and/or another track.",
             Schema().string ("eventId", "Event id")
                     .number ("startBeat", "New start (zero-based beats)", false, 0.0)
                     .number ("deltaBeats", "Relative move in beats (alternative to startBeat)", false)
                     .string ("trackId", "Destination track", false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("eventId");
                 const auto start = optBeatArg (c.args, "startBeat");
                 const auto delta = c.args.optNumber ("deltaBeats", -ticksToBeats (kMaxProjectTicks), ticksToBeats (kMaxProjectTicks));
                 const auto dest = c.args.optString ("trackId");
                 if (start && delta) c.args.fail ("give either startBeat or deltaBeats, not both");
                 return c.edit ("Move event", [&] (Project& p)
                 {
                     auto ref = requireEvent (p, id);
                     Event moved = *ref.event;
                     if (start) moved.start = *start;
                     if (delta) moved.start += beatsToTicks (*delta);
                     if (moved.start < 0) fail ("Event would start before the beginning of the project");
                     std::erase_if (ref.track->events, [&] (const Event& e) { return e.id == id; });
                     auto& target = dest ? requireTrack (p, *dest) : *ref.track;
                     carveOut (p, target, moved.start, moved.end());
                     target.events.push_back (moved);
                     sortEvents (target);
                     return eventJson (p, moved);
                 });
             } });

    r.add ({ "resize_event",
             "Move an event's edges. endBeat sets the right edge (extending a looping event repeats the loop). startBeat trims/extends "
             "the left edge while keeping audio in place (source offset adjusts).",
             Schema().string ("eventId", "Event id").number ("startBeat", "New left edge", false, 0.0)
                     .number ("endBeat", "New right edge", false, 0.0).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("eventId");
                 const auto newStart = optBeatArg (c.args, "startBeat");
                 const auto newEnd = optBeatArg (c.args, "endBeat");
                 if (! newStart && ! newEnd) c.args.fail ("give startBeat and/or endBeat");
                 return c.edit ("Resize event", [&] (Project& p)
                 {
                     auto ref = requireEvent (p, id);
                     auto& e = *ref.event;
                     const Tick end = newEnd.value_or (e.end());
                     const Tick start = newStart.value_or (e.start);
                     if (end <= start) fail ("Event would have zero or negative length");
                     if (start != e.start)
                     {
                         const double offset = e.sourceOffsetSeconds + ticksToSeconds (start - e.start, p.bpm) * e.rate();
                         if (offset < -1e-9 && ! e.looping) fail ("Cannot extend a non-looping event before the start of its source");
                         e.sourceOffsetSeconds = advancedOffset (p, e, start - e.start);
                         e.start = start;
                     }
                     e.length = end - e.start;
                     const Event copy = e;
                     carveOut (p, *ref.track, copy.start, copy.end(), copy.id);
                     return eventJson (p, copy);
                 });
             } });

    r.add ({ "split_event", "Split an event at an absolute beat. Returns the id of the new right-hand event.",
             Schema().string ("eventId", "Event id").number ("beat", "Split position (zero-based beats)", true, 0.0).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("eventId");
                 const auto at = beatArg (c.args, "beat");
                 return c.edit ("Split event", [&] (Project& p)
                 {
                     auto ref = requireEvent (p, id);
                     const auto rightId = splitEventAt (p, *ref.track, id, at);
                     return makeObject ({ { "leftEventId", id }, { "rightEventId", rightId } });
                 });
             } });

    r.add ({ "duplicate_event",
             "Duplicate an event. Default places the copy immediately after the original; count > 1 creates back-to-back copies.",
             Schema().string ("eventId", "Event id").number ("startBeat", "Start of the first copy", false, 0.0)
                     .string ("trackId", "Destination track (default: same)", false)
                     .integer ("count", "Number of copies (default 1)", false, 1, 256).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("eventId");
                 const auto start = optBeatArg (c.args, "startBeat");
                 const auto dest = c.args.optString ("trackId");
                 const auto count = c.args.has ("count") ? c.args.integer ("count", 1, 256) : 1;
                 return c.edit ("Duplicate event", [&] (Project& p)
                 {
                     const auto ref = requireEvent (p, id);
                     const Event source = *ref.event;
                     auto& target = dest ? requireTrack (p, *dest) : *ref.track;
                     juce::Array<juce::var> ids;
                     Tick at = start.value_or (source.end());
                     for (juce::int64 i = 0; i < count; ++i)
                     {
                         Event copy = source;
                         copy.id = p.allocateId ("event");
                         copy.start = at;
                         carveOut (p, target, copy.start, copy.end());
                         target.events.push_back (copy);
                         ids.add (copy.id);
                         at += source.length;
                     }
                     sortEvents (target);
                     return makeObject ({ { "eventIds", ids } });
                 });
             } });

    auto moveItem = makeObject ({ { "type", "object" },
                                  { "properties", makeObject ({ { "eventId", makeObject ({ { "type", "string" } }) },
                                                                { "startBeat", makeObject ({ { "type", "number" } }) },
                                                                { "trackId", makeObject ({ { "type", "string" } }) } }) },
                                  { "required", juce::Array<juce::var> { "eventId", "startBeat" } } });
    r.add ({ "move_events",
             "Move (or copy, with copy=true) several events at once as one edit. All sources are read before anything is placed, "
             "so events moved together never overwrite each other; they only overwrite other audio at their destinations. "
             "Returns the resulting event ids in order.",
             Schema().array ("moves", "[{eventId, startBeat, trackId?}]", moveItem)
                     .boolean ("copy", "Leave the originals in place (default false)", false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 struct Move { juce::String id; Tick start; std::optional<juce::String> track; };
                 std::vector<Move> moves;
                 for (const auto& item : c.args.array ("moves"))
                 {
                     JsonArgs m (item, "move");
                     moves.push_back ({ m.string ("eventId"), beatArg (m, "startBeat"), m.optString ("trackId") });
                 }
                 const bool copy = c.args.optBoolean ("copy").value_or (false);
                 return c.edit (copy ? "Copy events" : "Move events", [&] (Project& p)
                 {
                     // 1. Capture sources and their destination tracks.
                     struct Placement { Event event; juce::String trackId; };
                     std::vector<Placement> placements;
                     for (const auto& m : moves)
                     {
                         auto ref = requireEvent (p, m.id);
                         Event e = *ref.event;
                         const auto dest = m.track.value_or (ref.track->id);
                         requireTrack (p, dest);
                         e.start = m.start;
                         placements.push_back ({ e, dest });
                     }
                     // 2. Remove the originals (move only).
                     if (! copy)
                         for (const auto& m : moves)
                         {
                             auto ref = requireEvent (p, m.id);
                             std::erase_if (ref.track->events, [&] (const Event& e) { return e.id == m.id; });
                         }
                     // 3. Carve every destination range first, then insert, so the moved set is placed intact.
                     for (auto& pl : placements)
                     {
                         if (copy) pl.event.id = p.allocateId ("event");
                         carveOut (p, requireTrack (p, pl.trackId), pl.event.start, pl.event.end());
                     }
                     juce::Array<juce::var> ids;
                     for (auto& pl : placements)
                     {
                         auto& t = requireTrack (p, pl.trackId);
                         t.events.push_back (pl.event);
                         sortEvents (t);
                         ids.add (pl.event.id);
                     }
                     return makeObject ({ { "eventIds", ids } });
                 });
             } });

    r.add ({ "delete_event", "Delete one event.", Schema().string ("eventId", "Event id").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("eventId");
                 return c.edit ("Delete event", [&] (Project& p)
                 {
                     auto ref = requireEvent (p, id);
                     std::erase_if (ref.track->events, [&] (const Event& e) { return e.id == id; });
                     return juce::var();
                 });
             } });

    r.add ({ "set_event_looping", "Enable/disable looping (repetition of the source) for an event.",
             Schema().string ("eventId", "Event id").boolean ("enabled", "Looping on/off").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("eventId");
                 const bool on = c.args.boolean ("enabled");
                 return c.edit ("Event looping", [&] (Project& p) { requireEvent (p, id).event->looping = on; return juce::var(); });
             } });

    r.add ({ "set_event_gain", "Set per-event gain in dB.",
             Schema().string ("eventId", "Event id").number ("db", "Gain in dB", true, kMinDb, kMaxDb).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("eventId");
                 const double db = c.args.number ("db", kMinDb, kMaxDb);
                 return c.edit ("Event gain", [&] (Project& p) { requireEvent (p, id).event->gainDb = db; return juce::var(); });
             } });

    r.add ({ "set_event_pitch",
             "Transpose one or more events by -12..+12 semitones (fractions allowed for detune). Varispeed, like a hardware "
             "sampler: pitch and speed change together, so a +12 one-shot plays an octave higher in half the time. Great for "
             "basslines from a single note sample, e.g. a 55 Hz (A1) sine: +3 = C2, +7 = E2, -5 = E1. Positions and source "
             "offsets stay. Looping events keep their length; a one-shot that was playing its whole sample is refitted to the "
             "sample's new duration (never past the next event on its track).",
             Schema().array ("eventIds", "Events to transpose", makeObject ({ { "type", "string" } }))
                     .number ("semitones", "Pitch in semitones (0 = original)", true, -kMaxPitchSemitones, kMaxPitchSemitones).build(), K::Edit,
             [] (ActionContext& c)
             {
                 juce::StringArray ids;
                 for (const auto& v : c.args.array ("eventIds")) ids.addIfNotAlreadyThere (v.toString());
                 const double semis = c.args.number ("semitones", -kMaxPitchSemitones, kMaxPitchSemitones);
                 if (ids.isEmpty()) c.args.fail ("eventIds is empty");
                 return c.edit ("Pitch " + juce::String (semis > 0 ? "+" : "") + juce::String (semis, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " st", [&] (Project& p) -> juce::var
                 {
                     int refitted = 0;
                     for (const auto& id : ids)
                     {
                         auto ref = requireEvent (p, id);
                         auto& e = *ref.event;
                         const auto* a = p.findAsset (e.assetId);
                         if (! e.looping && a != nullptr && a->durationSeconds() > 0)
                         {
                             // Ticks the remaining source lasts at a given rate.
                             auto remaining = [&] (double rate)
                             {
                                 const double secs = std::max (0.0, a->durationSeconds() - e.sourceOffsetSeconds) / rate;
                                 return std::max<Tick> (1, beatsToTicks (secondsToBeats (secs, p.bpm)));
                             };
                             const Tick oldFull = remaining (e.rate());
                             if (std::abs (e.length - oldFull) <= kTicksPerBeat / 32)   // was showing the whole hit
                             {
                                 Tick limit = kMaxProjectTicks;
                                 for (const auto& other : ref.track->events)
                                     if (other.id != e.id && other.start > e.start) limit = std::min (limit, other.start);
                                 e.pitchSemitones = semis;
                                 e.length = std::min (remaining (e.rate()), limit - e.start);
                                 ++refitted;
                                 continue;
                             }
                         }
                         e.pitchSemitones = semis;
                     }
                     return makeObject ({ { "eventsChanged", ids.size() }, { "semitones", semis }, { "oneShotsRefitted", refitted } });
                 });
             } });

    r.add ({ "clear_range",
             "Remove audio between two beats on the given tracks (all tracks if omitted). Events crossing the edges are trimmed/split. "
             "Example: remove the kick for bars 1-2 -> {trackIds:[kick], startBeat:0, endBeat:8}.",
             Schema().number ("startBeat", "Range start", true, 0.0).number ("endBeat", "Range end", true, 0.0)
                     .array ("trackIds", "Tracks to affect (default all)", makeObject ({ { "type", "string" } }), false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto start = beatArg (c.args, "startBeat");
                 const auto end = beatArg (c.args, "endBeat");
                 if (end <= start) c.args.fail ("endBeat must be greater than startBeat");
                 const auto ids = trackIdsArg (c.project(), c.args, "trackIds");
                 return c.edit ("Clear range", [&] (Project& p)
                 {
                     for (const auto& id : ids) carveOut (p, requireTrack (p, id), start, end);
                     return juce::var();
                 });
             } });

    r.add ({ "duplicate_range",
             "Copy everything between startBeat and endBeat to destBeat on the given tracks (all if omitted), replacing what was there. "
             "Example: duplicate bars 17-24 after bar 24 -> {startBeat:64, endBeat:96, destBeat:96}.",
             Schema().number ("startBeat", "Source start", true, 0.0).number ("endBeat", "Source end", true, 0.0)
                     .number ("destBeat", "Destination start", true, 0.0)
                     .array ("trackIds", "Tracks to affect (default all)", makeObject ({ { "type", "string" } }), false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto start = beatArg (c.args, "startBeat");
                 const auto end = beatArg (c.args, "endBeat");
                 const auto dest = beatArg (c.args, "destBeat");
                 if (end <= start) c.args.fail ("endBeat must be greater than startBeat");
                 const auto ids = trackIdsArg (c.project(), c.args, "trackIds");
                 return c.edit ("Duplicate range", [&] (Project& p)
                 {
                     juce::Array<juce::var> created;
                     for (const auto& id : ids)
                     {
                         auto& t = requireTrack (p, id);
                         // Collect clipped copies first (reading the pre-edit events), then carve and insert.
                         std::vector<Event> copies;
                         for (const auto& e : t.events)
                         {
                             const Tick s = std::max (e.start, start), en = std::min (e.end(), end);
                             if (en <= s) continue;
                             Event copy = e;
                             copy.sourceOffsetSeconds = advancedOffset (p, e, s - e.start);
                             copy.start = dest + (s - start);
                             copy.length = en - s;
                             copies.push_back (copy);
                         }
                         carveOut (p, t, dest, dest + (end - start));
                         for (auto& copy : copies)
                         {
                             copy.id = p.allocateId ("event");
                             created.add (copy.id);
                             t.events.push_back (copy);
                         }
                         sortEvents (t);
                     }
                     return makeObject ({ { "eventIds", created } });
                 });
             } });

    // ---------------------------------------------------------------- effects
    juce::StringArray effectTypes;
    for (const auto& d : effectCatalogue()) effectTypes.add (d.type);

    r.add ({ "add_effect",
             "Insert a native effect into a track's chain or the master chain. Optionally set initial parameters "
             "(object of parameterId -> number, or choice name). Returns the effect with its id.",
             Schema().string ("target", "Track id, or \"master\"").enumeration ("type", "Effect type", effectTypes)
                     .integer ("index", "Chain position (default: end)", false, 0)
                     .object ("parameters", "Initial parameter values", false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto target = c.args.string ("target");
                 const auto type = c.args.string ("type");
                 const auto index = c.args.optNumber ("index");
                 const auto params = c.args.has ("parameters") ? c.args.raw()["parameters"] : juce::var();
                 return c.edit ("Add " + type, [&] (Project& p)
                 {
                     auto fx = makeEffect (p, type);
                     const auto* d = fx.descriptor();
                     if (params.isObject())
                         for (const auto& kv : params.getDynamicObject()->getProperties())
                         {
                             const int i = d->indexOf (kv.name.toString());
                             if (i < 0) fail ("Effect type '" + type + "' has no parameter '" + kv.name.toString() + "'");
                             fx.values[static_cast<size_t> (i)] = parseParamValue (d->params[static_cast<size_t> (i)], kv.value);
                         }
                     auto& chain = chainForTarget (p, target);
                     const auto at = index ? std::min<size_t> (static_cast<size_t> (*index), chain.size()) : chain.size();
                     chain.insert (chain.begin() + static_cast<std::ptrdiff_t> (at), fx);
                     return effectJson (fx, false);
                 });
             } });

    r.add ({ "remove_effect", "Remove an effect from its chain.", Schema().string ("effectId", "Effect id").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("effectId");
                 return c.edit ("Remove effect", [&] (Project& p)
                 {
                     auto* chain = p.findEffectChain (id);
                     if (chain == nullptr) fail ("Effect '" + id + "' not found");
                     std::erase_if (*chain, [&] (const EffectInstance& fx) { return fx.id == id; });
                     return juce::var();
                 });
             } });

    r.add ({ "move_effect", "Reorder an effect within its chain.",
             Schema().string ("effectId", "Effect id").integer ("index", "New position (0 = first)", true, 0).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("effectId");
                 const auto index = c.args.integer ("index", 0, Project::kMaxEffectsPerChain);
                 return c.edit ("Move effect", [&] (Project& p)
                 {
                     auto* chain = p.findEffectChain (id);
                     if (chain == nullptr) fail ("Effect '" + id + "' not found");
                     auto it = std::find_if (chain->begin(), chain->end(), [&] (const EffectInstance& fx) { return fx.id == id; });
                     auto fx = *it;
                     chain->erase (it);
                     const auto to = std::min<size_t> (static_cast<size_t> (index), chain->size());
                     chain->insert (chain->begin() + static_cast<std::ptrdiff_t> (to), fx);
                     return juce::var();
                 });
             } });

    r.add ({ "set_effect_bypass", "Bypass or re-enable an effect.",
             Schema().string ("effectId", "Effect id").boolean ("bypassed", "Bypassed").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("effectId");
                 const bool on = c.args.boolean ("bypassed");
                 return c.edit ("Bypass effect", [&] (Project& p) { requireEffect (p, id).bypassed = on; return juce::var(); });
             } });

    r.add ({ "set_effect_parameters",
             "Set one or more effect parameters: values is an object of parameterId -> number (choice parameters also accept "
             "their name, e.g. {\"tap1.division\": \"1/8D\"}). Use get_effect_parameters or list_effect_types for ids and ranges.",
             Schema().string ("effectId", "Effect id").object ("values", "parameterId -> value").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("effectId");
                 const auto values = c.args.object ("values");
                 return c.edit ("Effect parameters", [&] (Project& p)
                 {
                     auto& fx = requireEffect (p, id);
                     const auto* d = fx.descriptor();
                     for (const auto& kv : values.raw().getDynamicObject()->getProperties())
                     {
                         const int i = d->indexOf (kv.name.toString());
                         if (i < 0) fail ("Effect " + id + " (" + fx.type + ") has no parameter '" + kv.name.toString() + "'");
                         fx.values[static_cast<size_t> (i)] = parseParamValue (d->params[static_cast<size_t> (i)], kv.value);
                     }
                     return effectJson (fx, false);
                 });
             } });

    r.add ({ "set_effect_parameter", "Set a single effect parameter (see set_effect_parameters).",
             Schema().string ("effectId", "Effect id").string ("parameter", "Parameter id, e.g. band3.gain")
                     .numberOrString ("value", "Value (number, bool, or choice name)").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("effectId");
                 const auto param = c.args.string ("parameter");
                 const auto value = c.args.raw()["value"];
                 return c.edit ("Effect parameter", [&] (Project& p)
                 {
                     auto& fx = requireEffect (p, id);
                     const auto* d = fx.descriptor();
                     const int i = d->indexOf (param);
                     if (i < 0) fail ("Effect " + id + " (" + fx.type + ") has no parameter '" + param + "'");
                     fx.values[static_cast<size_t> (i)] = parseParamValue (d->params[static_cast<size_t> (i)], value);
                     return makeObject ({ { "parameter", param }, { "value", fx.values[static_cast<size_t> (i)] },
                                          { "display", d->params[static_cast<size_t> (i)].format (fx.values[static_cast<size_t> (i)]) } });
                 });
             } });

    // ---------------------------------------------------------------- undo / batch
    r.add ({ "undo", "Undo the last edit.", Schema().build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto label = c.controller.undoLabel();
                 if (! c.controller.undo()) fail ("Nothing to undo");
                 return makeObject ({ { "undone", label } });
             } });

    r.add ({ "redo", "Redo the last undone edit.", Schema().build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto label = c.controller.redoLabel();
                 if (! c.controller.redo()) fail ("Nothing to redo");
                 return makeObject ({ { "redone", label } });
             } });

    auto itemSchema = makeObject ({ { "type", "object" },
                                    { "properties", makeObject ({ { "action", makeObject ({ { "type", "string" } }) },
                                                                  { "arguments", makeObject ({ { "type", "object" } }) } }) },
                                    { "required", juce::Array<juce::var> { "action" } } });
    r.add ({ "execute_batch",
             "Run several actions atomically as ONE undo step. If any action fails, all changes are rolled back and the error names "
             "the failing step. Results are returned in order. Use \"$N.field\" in a string argument to reference field of the result "
             "of step N (0-based), e.g. {\"trackId\": \"$0.trackId\"}.",
             Schema().array ("actions", "Steps: [{action, arguments}]", itemSchema).string ("label", "Undo label", false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto& steps = c.args.array ("actions");
                 if (steps.size() > 512) c.args.fail ("at most 512 steps per batch");
                 c.controller.beginTransaction (c.args.optString ("label").value_or ("Batch"), true);   // one plan rebuild, applied at once
                 juce::Array<juce::var> results;

                 // "$2.eventId" -> results[2]["eventId"]
                 auto resolve = [&] (const juce::var& args) -> juce::var
                 {
                     if (! args.isObject()) return args;
                     auto* copy = new juce::DynamicObject();
                     for (const auto& kv : args.getDynamicObject()->getProperties())
                     {
                         juce::var v = kv.value;
                         const auto s = v.toString();
                         if (v.isString() && s.startsWithChar ('$') && s.containsChar ('.'))
                         {
                             const int step = s.substring (1).upToFirstOccurrenceOf (".", false, false).getIntValue();
                             const auto field = s.fromFirstOccurrenceOf (".", false, false);
                             if (step < 0 || step >= results.size()) fail ("Reference " + s + " points to a step that has not run");
                             v = results[step][juce::Identifier (field)];
                             if (v.isVoid()) fail ("Reference " + s + ": step " + juce::String (step) + " has no field '" + field + "'");
                         }
                         copy->setProperty (kv.name, v);
                     }
                     return juce::var (copy);
                 };

                 for (int i = 0; i < steps.size(); ++i)
                 {
                     try
                     {
                         const auto& step = steps.getReference (i);
                         const auto name = step["action"].toString();
                         if (name == "execute_batch") fail ("nested batches are not allowed");
                         const auto* def = c.registry.find (name);
                         if (def != nullptr && (def->kind == ActionKind::File || def->kind == ActionKind::App))
                             fail ("'" + name + "' cannot run inside a batch");
                         results.add (c.registry.execute (c.controller, name, resolve (step["arguments"])));
                     }
                     catch (const std::exception& e)
                     {
                         c.controller.rollbackTransaction();
                         fail ("Batch step " + juce::String (i) + " failed (all changes rolled back): " + juce::String (e.what()));
                     }
                 }
                 try { c.controller.validateTransaction(); }
                 catch (const std::exception& e)
                 {
                     c.controller.rollbackTransaction();
                     fail ("The batch would leave the project invalid (all changes rolled back): " + juce::String (e.what()));
                 }
                 c.controller.endTransaction();
                 return makeObject ({ { "results", results } });
             } });

    // ---------------------------------------------------------------- transport
    r.add ({ "get_transport_state", "Playback state, position (beats and bar.beat), device, meters.", Schema().build(), K::Query,
             [] (ActionContext& c)
             {
                 if (c.controller.transport == nullptr) return makeObject ({ { "available", false } });
                 auto state = c.controller.transport->describeState();
                 return state;
             } });

    r.add ({ "play", "Start playback (optionally from a beat).", Schema().number ("fromBeat", "Start position", false, 0.0).build(), K::Transport,
             [] (ActionContext& c)
             {
                 auto* t = c.controller.transport;
                 if (t == nullptr) fail (requireTransportMessage());
                 if (auto from = c.args.optNumber ("fromBeat", 0.0, ticksToBeats (kMaxProjectTicks))) t->seekBeats (*from);
                 t->play();
                 return t->describeState();
             } });

    r.add ({ "stop", "Stop playback and return to where playback started.", Schema().build(), K::Transport,
             [] (ActionContext& c)
             {
                 auto* t = c.controller.transport;
                 if (t == nullptr) fail (requireTransportMessage());
                 t->stop();
                 return t->describeState();
             } });

    r.add ({ "pause", "Stop playback and keep the current position.", Schema().build(), K::Transport,
             [] (ActionContext& c)
             {
                 auto* t = c.controller.transport;
                 if (t == nullptr) fail (requireTransportMessage());
                 t->pause();
                 return t->describeState();
             } });

    r.add ({ "seek", "Move the playhead (zero-based beats).", Schema().number ("beat", "Position", true, 0.0).build(), K::Transport,
             [] (ActionContext& c)
             {
                 auto* t = c.controller.transport;
                 if (t == nullptr) fail (requireTransportMessage());
                 t->seekBeats (c.args.number ("beat", 0.0, ticksToBeats (kMaxProjectTicks)));
                 return makeObject ({ { "beat", c.args.number ("beat") } });
             } });

    // ---------------------------------------------------------------- files
    r.add ({ "new_project", "Start a new empty project. Requires discardChanges=true if there are unsaved changes.",
             Schema().boolean ("discardChanges", "Confirm losing unsaved changes", false).build(), K::File,
             [] (ActionContext& c)
             {
                 if (c.controller.isDirty() && ! c.args.optBoolean ("discardChanges").value_or (false))
                     fail ("The project has unsaved changes; pass discardChanges=true or save first");
                 if (c.controller.transport) { c.controller.transport->pause(); c.controller.transport->seekBeats (0); }
                 c.controller.replaceProject (Project());
                 return juce::var();
             } });

    r.add ({ "open_project", "Open a .mcpdaw project. Requires discardChanges=true if there are unsaved changes.",
             Schema().string ("path", "Absolute path").boolean ("discardChanges", "Confirm losing unsaved changes", false).build(), K::File,
             [] (ActionContext& c)
             {
                 if (c.controller.isDirty() && ! c.args.optBoolean ("discardChanges").value_or (false))
                     fail ("The project has unsaved changes; pass discardChanges=true or save first");
                 const juce::File f (c.args.string ("path"));
                 auto p = loadProjectFile (f);
                 if (c.controller.transport) { c.controller.transport->pause(); c.controller.transport->seekBeats (0); }
                 c.controller.replaceProject (std::move (p), f);
                 return projectStateJson (c.project());
             } });

    r.add ({ "save_project",
             "Save the project as JSON (.mcpdaw). Without path, saves to the current file. Writing over a different existing file "
             "requires overwrite=true.",
             Schema().string ("path", "Absolute path", false).boolean ("overwrite", "Allow replacing an existing file", false).build(), K::File,
             [] (ActionContext& c)
             {
                 juce::File f = c.args.has ("path") ? juce::File (c.args.string ("path")) : c.controller.currentFile();
                 if (f == juce::File()) fail ("No current file; give a path");
                 if (! f.hasFileExtension ("mcpdaw")) f = f.withFileExtension ("mcpdaw");
                 if (f.existsAsFile() && f != c.controller.currentFile() && ! c.args.optBoolean ("overwrite").value_or (false))
                     fail ("File exists: " + f.getFullPathName() + " (pass overwrite=true)");
                 saveProjectFile (c.project(), f);
                 c.controller.markSaved (f);
                 return makeObject ({ { "path", f.getFullPathName() } });
             } });
}

void registerAppActions (ActionRegistry& r, std::shared_ptr<AppHooks> hooks)
{
    r.add ({ "capture_screenshot",
             "Save a PNG screenshot of the MCP DAW main window (for visual verification). Returns the path and size.",
             Schema().string ("path", "Absolute .png path").build(), ActionKind::App,
             [hooks] (ActionContext& c)
             {
                 const auto path = c.args.string ("path");
                 if (! juce::File::isAbsolutePath (path)) c.args.fail ("path must be absolute");
                 juce::File f (path);
                 if (! f.hasFileExtension ("png")) f = f.withFileExtension ("png");
                 if (hooks == nullptr || ! hooks->captureScreenshot) c.args.fail ("no window to capture (headless)");
                 auto result = hooks->captureScreenshot (f);
                 if (! result.isObject()) c.args.fail ("could not write " + f.getFullPathName());
                 return result;
             } });
    r.add ({ "zoom_to_fit",
             "View only (not an edit, not undoable): fit the whole arrangement into the timeline horizontally "
             "and size tracks so they all show. With startBeat/endBeat, fit that range instead (a 'camera' for "
             "demos: e.g. 0-32 shows 8 bars; the view still follows the playhead page by page).",
             Schema().number ("startBeat", "First beat to show (default: 0)", false, 0.0)
                     .number ("endBeat", "Last beat to show (default: end of the arrangement)", false, 0.0).build(),
             ActionKind::App,
             [hooks] (ActionContext& c)
             {
                 if (hooks == nullptr || ! hooks->zoomToFit) c.args.fail ("no window (headless)");
                 const double start = c.args.optNumber ("startBeat").value_or (0.0);
                 const double end = c.args.optNumber ("endBeat").value_or (0.0);
                 if (c.args.optNumber ("endBeat") && end <= start) c.args.fail ("endBeat must be after startBeat");
                 hooks->zoomToFit (start, end);
                 return makeObject ({ { "ok", true } });
             } });
}
} // namespace mcpdaw
