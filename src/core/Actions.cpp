// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Actions.h"
#include "EffectPresets.h"
#include "ProjectJson.h"
#include "WavLoopMetadata.h"
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

/** Clip `e` to the timeline range [s, en) (inside or beyond the event): the result plays exactly the
    audio `e` plays there (reverse-aware source offset), keeps a fade only on an edge it shares with
    `e`, and keeps its id (callers assign new ids where needed). */
Event clippedEvent (const Project& p, const Event& e, Tick s, Tick en)
{
    Event c = e;
    c.sourceOffsetSeconds = offsetForRange (p, e, s, en);
    c.start = s;
    c.length = en - s;
    if (s != e.start) c.fadeInTicks = 0;
    if (en != e.end()) c.fadeOutTicks = 0;
    c.clampFades();
    return c;
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

// Optional "detail" argument: "full" (the default, unchanged responses for existing clients) or
// "compact" (identities and only what matters, to save tokens on routine reads and edits).
const juce::StringArray kDetailLevels { "full", "compact" };
constexpr const char* kDetailHelp = "\"full\" (default) or \"compact\": effects as {id, type, bypassed, values} with only non-default values";

bool compactDetail (const ActionContext& c)
{
    const auto detail = c.args.optString ("detail").value_or ("full");
    if (! kDetailLevels.contains (detail)) c.args.fail ("detail must be \"full\" or \"compact\"");
    return detail == "compact";
}

/** Keep only identity fields of a step result: id, type, and anything named *Id / *Ids. */
juce::var compactResult (const juce::var& result)
{
    auto* obj = result.getDynamicObject();
    if (obj == nullptr) return {};
    auto* out = new juce::DynamicObject();
    for (const auto& kv : obj->getProperties())
    {
        const auto key = kv.name.toString();
        if (key == "id" || key == "type" || key.endsWith ("Id") || key.endsWith ("Ids")) out->setProperty (kv.name, kv.value);
    }
    return juce::var (out);
}

juce::StringArray fadeCurveNames() { return { "linear", "smooth", "fast", "slow" }; }

/** Event ids from "eventIds" (array) and/or "eventId" (string), de-duplicated, never empty. */
juce::StringArray eventIdsArg (const JsonArgs& a)
{
    juce::StringArray ids;
    if (auto one = a.optString ("eventId")) ids.add (*one);
    if (a.has ("eventIds"))
        for (const auto& v : a.array ("eventIds"))
        {
            if (! v.isString()) a.fail ("'eventIds' must contain event id strings");
            ids.addIfNotAlreadyThere (v.toString());
        }
    if (ids.isEmpty()) a.fail ("give eventIds (or eventId)");
    return ids;
}

/** Optional fade arguments shared by create_event and set_event_fades. */
struct FadeArgs
{
    std::optional<Tick> inTicks, outTicks;
    std::optional<FadeCurve> inCurve, outCurve;

    static FadeArgs parse (const JsonArgs& a)
    {
        FadeArgs f;
        const double maxBeats = ticksToBeats (kMaxProjectTicks);
        if (auto v = a.optNumber ("fadeInBeats", 0.0, maxBeats)) f.inTicks = beatsToTicks (*v);
        if (auto v = a.optNumber ("fadeOutBeats", 0.0, maxBeats)) f.outTicks = beatsToTicks (*v);
        auto curve = [&] (const char* key) -> std::optional<FadeCurve>
        {
            const auto name = a.optString (key);
            if (! name) return std::nullopt;
            const auto c = fadeCurveFromName (name->toRawUTF8());
            if (! c) a.fail ("'" + juce::String (key) + "' must be one of " + fadeCurveNames().joinIntoString (", "));
            return c;
        };
        f.inCurve = curve ("fadeInCurve");
        f.outCurve = curve ("fadeOutCurve");
        return f;
    }
    bool any() const noexcept { return inTicks || outTicks || inCurve || outCurve; }

    /** Apply what was given; returns true if a length had to be clipped to the event. */
    bool applyTo (Event& e) const
    {
        bool clipped = false;
        if (inTicks) { e.fadeInTicks = std::min (*inTicks, e.length); clipped |= *inTicks > e.length; }
        if (outTicks) { e.fadeOutTicks = std::min (*outTicks, e.length); clipped |= *outTicks > e.length; }
        if (inCurve) e.fadeInCurve = *inCurve;
        if (outCurve) e.fadeOutCurve = *outCurve;
        return clipped;
    }
};

LoopMetadata loopMetadataArgs (const JsonArgs& a, const WavLoopInfo& existing)
{
    // Start from what the file has (so unspecified fields keep their values), then apply arguments.
    LoopMetadata m = existing.hasAcidChunk ? existing.metadata : LoopMetadata();
    if (! existing.hasAcidChunk) { m.bpm = 0.0; m.beats = 0; }
    if (auto type = a.optString ("type"))
    {
        if (*type == "loop") m.oneShot = false;
        else if (*type == "one-shot") m.oneShot = true;
        else a.fail ("'type' must be \"loop\" or \"one-shot\"");
    }
    if (auto v = a.optNumber ("bpm", 0.0, kMaxBpm)) m.bpm = *v;
    if (a.has ("beats")) m.beats = static_cast<int> (a.integer ("beats", 0, 65535));
    if (a.has ("numerator")) m.numerator = static_cast<int> (a.integer ("numerator", 1, 32));
    if (a.has ("denominator")) m.denominator = static_cast<int> (a.integer ("denominator", 1, 32));
    if (a.has ("rootNote"))
    {
        const auto note = a.integer ("rootNote", -1, 127);
        m.rootNote = note < 0 ? std::nullopt : std::optional<int> (static_cast<int> (note));
    }
    if (auto v = a.optBoolean ("stretch")) m.stretch = *v;
    else if (! existing.hasAcidChunk) m.stretch = ! m.oneShot;
    return m;
}

juce::var loopMetadataJson (const LoopMetadata& m)
{
    // The chunk stores tempo as a 32-bit float: show it rounded (118.489, not 118.48899841308594).
    auto o = makeObject ({ { "type", m.oneShot ? "one-shot" : "loop" }, { "bpm", std::round (m.bpm * 1.0e4) / 1.0e4 }, { "beats", m.beats },
                           { "numerator", m.numerator }, { "denominator", m.denominator }, { "stretch", m.stretch && ! m.oneShot },
                           { "rootNote", m.rootNote ? juce::var (*m.rootNote) : juce::var() },
                           { "rootNoteName", m.rootNote ? juce::var (midiNoteName (*m.rootNote)) : juce::var() } });
    return o;
}

juce::var wavInfoJson (const juce::File& f, const WavLoopInfo& info)
{
    auto o = makeObject ({ { "path", f.getFullPathName() }, { "supported", info.supported }, { "hasLoopMetadata", info.hasAcidChunk },
                           { "durationSeconds", info.durationSeconds }, { "sampleRate", info.sampleRate }, { "channels", info.channels },
                           { "bitsPerSample", info.bitsPerSample } });
    if (info.problem.isNotEmpty()) o.getDynamicObject()->setProperty ("problem", info.problem);
    if (info.hasAcidChunk)
    {
        o.getDynamicObject()->setProperty ("metadata", loopMetadataJson (info.metadata));
        if (const double expected = info.metadata.expectedSeconds(); expected > 0)
        {
            const double diff = info.durationSeconds - expected;
            o.getDynamicObject()->setProperty ("expectedDurationSeconds", expected);
            o.getDynamicObject()->setProperty ("durationDifferenceSeconds", diff);
            o.getDynamicObject()->setProperty ("durationMatches", std::abs (diff) <= std::max (0.005, expected * 0.001));
        }
    }
    return o;
}
} // namespace

double rawOffsetForRange (const Project& p, const Event& e, Tick newStart, Tick newEnd)
{
    // Forwards, the audio heard at the event's start sits at sourceOffsetSeconds, so moving the left
    // edge moves the offset. Reversed, the offset is heard at the event's END, so the right edge does.
    const Tick delta = e.reversed ? e.end() - newEnd : newStart - e.start;
    return e.sourceOffsetSeconds + ticksToSeconds (delta, p.bpm) * e.rate();
}

double offsetForRange (const Project& p, const Event& e, Tick newStart, Tick newEnd)
{
    return advancedOffset (p, e, e.reversed ? e.end() - newEnd : newStart - e.start);
}

double slippedOffset (const Project& p, const Event& e, double deltaBeats, bool* clamped)
{
    if (clamped != nullptr) *clamped = false;
    // Audio moving right by d: forwards the source position under any fixed point decreases by d (in
    // source time); reversed it increases.
    const double d = beatsToSeconds (deltaBeats, p.bpm) * e.rate();
    double offset = e.sourceOffsetSeconds + (e.reversed ? d : -d);
    const auto* a = p.findAsset (e.assetId);
    const double duration = a != nullptr ? a->durationSeconds() : 0.0;
    if (duration <= 0.0) return std::max (0.0, offset);
    if (e.looping)
    {
        offset = std::fmod (offset, duration);
        if (offset < 0) offset += duration;
        return offset;
    }
    const double maxOffset = std::max (0.0, duration - 1.0 / std::max (1.0, a->data->sampleRate));   // keep at least one frame
    const double limited = std::clamp (offset, 0.0, maxOffset);
    if (clamped != nullptr && std::abs (limited - offset) > 1e-12) *clamped = true;
    return limited;
}

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
            result.push_back (clippedEvent (p, e, e.start, start));
        if (e.end() > end)                         // keep the part after the carved region
        {
            Event right = clippedEvent (p, e, end, e.end());
            if (e.start < start) right.id = p.allocateId ("event");   // original id stays with the left part
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
        // Left keeps the fade-in, right keeps the fade-out; the cut edges get only the declick.
        Event right = clippedEvent (p, e, at, e.end());
        right.id = p.allocateId ("event");
        e = clippedEvent (p, e, e.start, at);
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
             "(positions in zero-based beats plus bar.beat.tick strings, loop repetitions, occupied bars) and effect chains. "
             "detail=\"compact\" lists each effect's non-default values only (much smaller with EQs and delays).",
             Schema().enumeration ("detail", kDetailHelp, kDetailLevels, false).build(), K::Query,
             [] (ActionContext& c) { return projectStateJson (c.project(), c.controller.defaultDeclickMs(), compactDetail (c)); } });

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

    r.add ({ "get_track", "Full detail for one track.",
             Schema().string ("trackId", "Track id").enumeration ("detail", kDetailHelp, kDetailLevels, false).build(), K::Query,
             [] (ActionContext& c)
             {
                 const auto* t = c.project().findTrack (c.args.string ("trackId"));
                 if (t == nullptr) fail ("Track '" + c.args.string ("trackId") + "' not found");
                 return trackJson (c.project(), *t, c.controller.defaultDeclickMs(), compactDetail (c));
             } });

    r.add ({ "list_assets", "Imported audio sources with duration, sample rate and tempo metadata.", Schema().build(), K::Query,
             [] (ActionContext& c)
             {
                 juce::Array<juce::var> list;
                 for (const auto& a : c.project().assets) list.add (assetJson (a));
                 return juce::var (list);
             } });

    juce::StringArray effectTypeNames;
    for (const auto& d : effectCatalogue()) effectTypeNames.add (d.type);

    r.add ({ "list_effect_types",
             "Available native effect types with every parameter (id, range, unit, choices). Pass type for one effect's "
             "specification, or brief=true for just the types, names and descriptions.",
             Schema().enumeration ("type", "Only this effect type", effectTypeNames, false)
                     .boolean ("brief", "Types, names and descriptions only (no parameters)", false).build(), K::Query,
             [] (ActionContext& c)
             {
                 const auto only = c.args.optString ("type");
                 if (only && findEffectType (*only) == nullptr) c.args.fail ("unknown effect type '" + *only + "'");
                 const bool brief = c.args.optBoolean ("brief").value_or (false);
                 juce::Array<juce::var> list;
                 for (const auto& d : effectCatalogue())
                     if (! only || d.type == *only) list.add (brief ? effectTypeBriefJson (d) : effectTypeJson (d));
                 return juce::var (list);
             } });

    r.add ({ "get_effect_parameters",
             "Current parameter values of an effect, with specs and display strings. detail=\"values\" omits the specs; "
             "\"compact\" returns only the values that differ from the defaults.",
             Schema().string ("effectId", "Effect id")
                     .enumeration ("detail", "\"full\" (default), \"values\" or \"compact\"", { "full", "values", "compact" }, false).build(),
             K::Query,
             [] (ActionContext& c)
             {
                 const auto* fx = c.project().findEffect (c.args.string ("effectId"));
                 if (fx == nullptr) fail ("Effect '" + c.args.string ("effectId") + "' not found");
                 const auto detail = c.args.optString ("detail").value_or ("full");
                 if (detail == "values") return effectJson (*fx, false);
                 if (detail == "compact") return effectCompactJson (*fx);
                 if (detail != "full") c.args.fail ("detail must be \"full\", \"values\" or \"compact\"");
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
             "Place an asset on a track. Overlapped parts of existing events on that track are replaced (loop-arranger overwrite, like painting over tape). "
             "Optional fades are set in the same step, e.g. a vocal at bar 9 with a two-beat fade-in -> {startBeat: 32, fadeInBeats: 2}. "
             "Without fades the event still gets the tiny anti-click fade set by the Default declick time preference.",
             Schema().string ("trackId", "Track id").string ("assetId", "Asset id")
                     .number ("startBeat", "Start (zero-based beats)", true, 0.0)
                     .number ("lengthBeats", "Length in beats (default: natural length). Longer than the source repeats it when looping", false, 0.0)
                     .boolean ("looping", "Repeat the source to fill the event (default: true for loops, false for one-shots / files under 2 s without tempo)", false)
                     .number ("sourceOffsetSeconds", "Start point inside the source", false, 0.0)
                     .number ("pitchSemitones", "Transpose, -12..+12 semitones (varispeed like a sampler: +12 = octave up, twice as fast)",
                              false, -kMaxPitchSemitones, kMaxPitchSemitones)
                     .number ("gainDb", "Event gain in dB (default 0)", false, kMinDb, kMaxDb)
                     .boolean ("reversed", "Play the event backwards (non-destructive; default false)", false)
                     .number ("fadeInBeats", "Fade-in length in beats (default 0 = only the declick; clipped to the event length)", false, 0.0)
                     .number ("fadeOutBeats", "Fade-out length in beats (default 0 = only the declick; clipped to the event length)", false, 0.0)
                     .enumeration ("fadeInCurve", "Fade-in curve (default linear)", fadeCurveNames(), false)
                     .enumeration ("fadeOutCurve", "Fade-out curve (default linear)", fadeCurveNames(), false)
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
                 const auto gain = c.args.optNumber ("gainDb", kMinDb, kMaxDb).value_or (0.0);
                 const bool reversed = c.args.optBoolean ("reversed").value_or (false);
                 const auto fades = FadeArgs::parse (c.args);
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
                     e.gainDb = gain;
                     e.reversed = reversed;
                     fades.applyTo (e);
                     carveOut (p, t, e.start, e.end());
                     t.events.push_back (e);
                     sortEvents (t);
                     return eventJson (p, e, c.controller.defaultDeclickMs());
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
                     // Audio stays in place: the source offset follows the edge that carries it (the left
                     // edge forwards, the right edge when reversed).
                     if (! e.looping && rawOffsetForRange (p, e, start, end) < -1e-9)
                         fail (e.reversed ? "Cannot extend a reversed one-shot past the start of its source (drag the left edge instead)"
                                          : "Cannot extend a non-looping event before the start of its source");
                     e.sourceOffsetSeconds = offsetForRange (p, e, start, end);
                     e.start = start;
                     e.length = end - start;
                     e.clampFades();
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

    r.add ({ "set_event_gain", "Set event gain in dB (per-event volume; -70 = silent, max +12) on one event (eventId) or several (eventIds) as one undo step.",
             Schema().string ("eventId", "Event id", false)
                     .array ("eventIds", "Several events (alternative to eventId)", makeObject ({ { "type", "string" } }), false)
                     .number ("db", "Gain in dB", true, kMinDb, kMaxDb).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto ids = eventIdsArg (c.args);
                 const double db = c.args.number ("db", kMinDb, kMaxDb);
                 return c.edit ("Adjust event gain", [&] (Project& p)
                 {
                     for (const auto& id : ids) requireEvent (p, id).event->gainDb = db;
                     return makeObject ({ { "eventsChanged", ids.size() }, { "db", db } });
                 });
             } });

    r.add ({ "set_event_fades",
             "Set event fades on one or more events: fade-in/fade-out length in beats and curve (linear, smooth, fast, slow) for "
             "each edge. Every argument is optional, so you can change one property and leave the rest, e.g. {eventIds:[...], fadeOutBeats:2}. "
             "Fade-in and fade-out may overlap (they multiply). 0 means no deliberate fade: the edge still gets the tiny anti-click "
             "fade set by the Default declick time preference (see get_project_state editing.defaultDeclickMs). Lengths beyond an "
             "event are clipped to it.",
             Schema().array ("eventIds", "Events to change", makeObject ({ { "type", "string" } }))
                     .number ("fadeInBeats", "Fade-in length in beats", false, 0.0)
                     .number ("fadeOutBeats", "Fade-out length in beats", false, 0.0)
                     .enumeration ("fadeInCurve", "Fade-in curve", fadeCurveNames(), false)
                     .enumeration ("fadeOutCurve", "Fade-out curve", fadeCurveNames(), false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto ids = eventIdsArg (c.args);
                 const auto fades = FadeArgs::parse (c.args);
                 if (! fades.any()) c.args.fail ("give at least one of fadeInBeats, fadeOutBeats, fadeInCurve, fadeOutCurve");
                 const bool lengths = fades.inTicks || fades.outTicks;
                 return c.edit (lengths ? "Adjust event fade" : "Change fade curve", [&] (Project& p)
                 {
                     juce::Array<juce::var> events, clipped;
                     for (const auto& id : ids)
                     {
                         auto& e = *requireEvent (p, id).event;
                         if (fades.applyTo (e)) clipped.add (id);
                         events.add (eventJson (p, e, c.controller.defaultDeclickMs()));
                     }
                     auto result = makeObject ({ { "events", events } });
                     if (! clipped.isEmpty()) result.getDynamicObject()->setProperty ("clippedToEventLength", clipped);
                     return result;
                 });
             } });

    r.add ({ "set_event_reverse",
             "Reverse event playback on or off for one or more events (explicit state). Non-destructive: the audio file is not "
             "changed and other events using it are unaffected. A reversed event plays the same stretch of audio it shows, back "
             "to front; fades, gain, looping, trimming and pitch all still apply.",
             Schema().array ("eventIds", "Events to change", makeObject ({ { "type", "string" } }))
                     .boolean ("enabled", "true = reversed, false = forwards").build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto ids = eventIdsArg (c.args);
                 const bool on = c.args.boolean ("enabled");
                 return c.edit ("Reverse event", [&] (Project& p)
                 {
                     for (const auto& id : ids) requireEvent (p, id).event->reversed = on;
                     return makeObject ({ { "eventsChanged", ids.size() }, { "reversed", on } });
                 });
             } });

    r.add ({ "slip_event",
             "Slip edit: move the source audio underneath an event while the event stays where it is (start, length and track "
             "unchanged; only sourceOffsetSeconds changes). deltaBeats moves the audio later (positive = to the right) by that many "
             "beats of timeline; or give the absolute sourceOffsetSeconds. Pitch (varispeed) and reverse are accounted for. "
             "Looping events wrap around their source; one-shots are kept within their source (a relative move stops at the edge).",
             Schema().string ("eventId", "Event id")
                     .number ("deltaBeats", "Relative slip in beats (positive = audio moves right / later)", false)
                     .number ("sourceOffsetSeconds", "Absolute source offset (alternative to deltaBeats)", false, 0.0).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("eventId");
                 const auto delta = c.args.optNumber ("deltaBeats", -ticksToBeats (kMaxProjectTicks), ticksToBeats (kMaxProjectTicks));
                 const auto absolute = c.args.optNumber ("sourceOffsetSeconds", 0.0, 1.0e6);
                 if (delta.has_value() == absolute.has_value()) c.args.fail ("give either deltaBeats or sourceOffsetSeconds");
                 return c.edit ("Slip event", [&] (Project& p)
                 {
                     auto& e = *requireEvent (p, id).event;
                     bool clamped = false, wrapped = false;
                     if (delta) e.sourceOffsetSeconds = slippedOffset (p, e, *delta, &clamped);
                     else
                     {
                         const auto* a = p.findAsset (e.assetId);
                         const double duration = a != nullptr ? a->durationSeconds() : 0.0;
                         double offset = *absolute;
                         if (duration > 0 && offset >= duration)
                         {
                             if (! e.looping)
                                 fail ("sourceOffsetSeconds " + juce::String (offset, 4) + " is beyond the end of the source ("
                                       + juce::String (duration, 4) + " s) of one-shot event " + id);
                             offset = std::fmod (offset, duration);
                             wrapped = true;
                         }
                         e.sourceOffsetSeconds = offset;
                     }
                     auto result = eventJson (p, e);
                     result.getDynamicObject()->setProperty ("clampedToSource", clamped);
                     result.getDynamicObject()->setProperty ("wrapped", wrapped);
                     return result;
                 });
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
                                 e.clampFades();
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
                             Event copy = clippedEvent (p, e, s, en);
                             copy.start = dest + (s - start);
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
                     .object ("parameters", "Initial parameter values", false)
                     .enumeration ("detail", kDetailHelp, kDetailLevels, false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto target = c.args.string ("target");
                 const auto type = c.args.string ("type");
                 const auto index = c.args.optNumber ("index");
                 const auto params = c.args.has ("parameters") ? c.args.raw()["parameters"] : juce::var();
                 const bool compact = compactDetail (c);
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
                     return compact ? effectCompactJson (fx) : effectJson (fx, false);
                 });
             } });

    juce::StringArray presetIds, presetLines;
    for (const auto& pr : effectPresets())
    {
        presetIds.add (pr.id);
        juce::StringArray types;
        for (const auto& step : pr.chain) types.add (step.type);
        presetLines.add (pr.id + " (" + types.joinIntoString (" -> ") + ")");
    }
    r.add ({ "add_effect_preset",
             "Insert a factory preset chain - ordinary effects, each fully editable afterwards and saved with all of its values - "
             "into a track's chain or the master chain, as one undo step. Presets: " + presetLines.joinIntoString ("; ")
             + ". Returns the new effect ids in chain order.",
             Schema().string ("target", "Track id, or \"master\"").enumeration ("preset", "Preset id", presetIds)
                     .integer ("index", "Chain position of the first effect (default: end)", false, 0).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto target = c.args.string ("target");
                 const auto* preset = findEffectPreset (c.args.string ("preset"));
                 if (preset == nullptr) c.args.fail ("unknown preset '" + c.args.string ("preset") + "'");
                 const auto index = c.args.optNumber ("index");
                 return c.edit ("Add " + preset->name, [&] (Project& p)
                 {
                     auto& chain = chainForTarget (p, target);
                     auto added = instantiatePreset (p, *preset);
                     const auto at = index ? std::min<size_t> (static_cast<size_t> (*index), chain.size()) : chain.size();
                     juce::Array<juce::var> ids, effects;
                     for (const auto& fx : added) { ids.add (fx.id); effects.add (effectCompactJson (fx)); }
                     chain.insert (chain.begin() + static_cast<std::ptrdiff_t> (at), added.begin(), added.end());
                     return makeObject ({ { "preset", preset->id }, { "name", preset->name }, { "effectIds", ids }, { "effects", effects } });
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
             Schema().string ("effectId", "Effect id").object ("values", "parameterId -> value")
                     .enumeration ("detail", "\"full\" (default: every parameter value) or \"compact\" (only the values set by this call)",
                                   kDetailLevels, false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto id = c.args.string ("effectId");
                 const auto values = c.args.object ("values");
                 const bool compact = compactDetail (c);
                 return c.edit ("Effect parameters", [&] (Project& p)
                 {
                     auto& fx = requireEffect (p, id);
                     const auto* d = fx.descriptor();
                     auto* set = new juce::DynamicObject();
                     const juce::var setVar (set);
                     for (const auto& kv : values.raw().getDynamicObject()->getProperties())
                     {
                         const int i = d->indexOf (kv.name.toString());
                         if (i < 0) fail ("Effect " + id + " (" + fx.type + ") has no parameter '" + kv.name.toString() + "'");
                         const auto& spec = d->params[static_cast<size_t> (i)];
                         fx.values[static_cast<size_t> (i)] = parseParamValue (spec, kv.value);
                         set->setProperty (kv.name, paramValueJson (spec, fx.values[static_cast<size_t> (i)]));
                     }
                     if (compact) return makeObject ({ { "id", fx.id }, { "type", fx.type }, { "set", setVar } });
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
             Schema().array ("actions", "Steps: [{action, arguments}]", itemSchema).string ("label", "Undo label", false)
                     .enumeration ("detail", "\"full\" (default: every step's result) or \"compact\" (each result reduced to id, type "
                                   "and *Id/*Ids fields; $N references still see the full results)", kDetailLevels, false).build(), K::Edit,
             [] (ActionContext& c)
             {
                 const auto& steps = c.args.array ("actions");
                 if (steps.size() > 512) c.args.fail ("at most 512 steps per batch");
                 const bool compact = compactDetail (c);
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
                 if (compact)
                     for (auto& result : results) result = compactResult (result);
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
                 int version = Project::kFormatVersion;
                 auto p = loadProjectFile (f, {}, &version);
                 if (c.controller.transport) { c.controller.transport->pause(); c.controller.transport->seekBeats (0); }
                 c.controller.replaceProject (std::move (p), f, version);
                 auto state = projectStateJson (c.project(), c.controller.defaultDeclickMs());
                 if (c.controller.upgradedFromVersion() > 0)
                     state.getDynamicObject()->setProperty ("formatUpgrade", makeObject ({
                         { "fromVersion", c.controller.upgradedFromVersion() }, { "toVersion", Project::kFormatVersion },
                         { "originalFileProtected", true },
                         { "suggestedPath", c.controller.suggestedUpgradedFile().getFullPathName() },
                         { "note", "Opened and upgraded in memory; the original file is untouched. save_project without a path is refused "
                                   "until the project is saved to a new file (pass path), so the original stays usable by older versions." } }));
                 return state;
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
                 const bool overwrite = c.args.optBoolean ("overwrite").value_or (false);
                 // An upgraded older-format project must not silently replace its original file.
                 if (c.controller.saveNeedsNewFile() && f == c.controller.currentFile() && ! (c.args.has ("path") && overwrite))
                     fail ("This project was opened from an older format (v" + juce::String (c.controller.upgradedFromVersion())
                           + ") file and upgraded to v" + juce::String (Project::kFormatVersion) + ". Save the upgraded project to a new file "
                           "so the original stays usable by older versions: pass path (suggested: " + c.controller.suggestedUpgradedFile().getFullPathName()
                           + "). To replace the original anyway, pass its path with overwrite=true.");
                 if (f.existsAsFile() && f != c.controller.currentFile() && ! overwrite)
                     fail ("File exists: " + f.getFullPathName() + " (pass overwrite=true)");
                 saveProjectFile (c.project(), f);
                 c.controller.markSaved (f);
                 return makeObject ({ { "path", f.getFullPathName() } });
             } });

    // ---------------------------------------------------------------- loop metadata (WAV files)
    r.add ({ "get_wav_loop_metadata",
             "Inspect a WAV file's ACID-compatible loop metadata (type loop/one-shot, tempo, beats, time signature, root note, "
             "stretch) and check it against the file's real duration (expected = beats * 60 / bpm). Also says whether "
             "write_wav_loop_metadata can safely edit the file (conventional RIFF/WAVE only).",
             Schema().string ("path", "Absolute .wav path").build(), K::Query,
             [] (ActionContext& c)
             {
                 const juce::File f (c.args.string ("path"));
                 if (! juce::File::isAbsolutePath (c.args.string ("path"))) c.args.fail ("path must be absolute");
                 if (! f.existsAsFile()) c.args.fail ("file not found: " + f.getFullPathName());
                 return wavInfoJson (f, readWavLoopMetadata (f));
             } });

    r.add ({ "write_wav_loop_metadata",
             "Embed Loop Metadata: write ACID-compatible loop metadata into a WAV file without touching its audio (the PCM data "
             "chunk is copied byte for byte and verified by SHA-256). Safest: give outputPath to save a copy. Updating the file in "
             "place needs overwrite=true. Unspecified fields keep the file's existing values. Loops need bpm (20..300) and beats; "
             "check durationMatches in the result. Conventional RIFF/WAVE only (RF64/Wave64 are refused). Assets already in the "
             "project keep their current tempo (use set_asset_bpm).",
             Schema().string ("path", "Absolute path of the source .wav")
                     .string ("outputPath", "Write a copy here instead of updating the source", false)
                     .boolean ("overwrite", "Required to update the source in place, or to replace an existing outputPath", false)
                     .enumeration ("type", "loop (tempo-based, may be stretched) or one-shot", { "loop", "one-shot" }, false)
                     .number ("bpm", "Tempo the material was played at", false, 0.0, kMaxBpm)
                     .integer ("beats", "Musical length in beats", false, 0, 65535)
                     .integer ("numerator", "Time signature numerator (default 4)", false, 1, 32)
                     .integer ("denominator", "Time signature denominator (default 4)", false, 1, 32)
                     .integer ("rootNote", "Root key as a MIDI note (60 = C4); -1 = none", false, -1, 127)
                     .boolean ("stretch", "Loop may be tempo-matched (default true for loops)", false).build(), K::File,
             [] (ActionContext& c)
             {
                 const auto pathText = c.args.string ("path");
                 if (! juce::File::isAbsolutePath (pathText)) c.args.fail ("path must be absolute");
                 const juce::File source (pathText);
                 if (! source.existsAsFile()) c.args.fail ("file not found: " + source.getFullPathName());
                 const bool overwrite = c.args.optBoolean ("overwrite").value_or (false);
                 juce::File dest = source;
                 if (auto out = c.args.optString ("outputPath"))
                 {
                     if (! juce::File::isAbsolutePath (*out)) c.args.fail ("outputPath must be absolute");
                     dest = juce::File (*out);
                     if (! dest.hasFileExtension ("wav")) dest = dest.withFileExtension ("wav");
                 }
                 if (dest == source && ! overwrite)
                     c.args.fail ("updating " + source.getFileName() + " in place needs overwrite=true (or give outputPath to save a copy)");
                 if (dest != source && dest.existsAsFile() && ! overwrite)
                     c.args.fail ("file exists: " + dest.getFullPathName() + " (pass overwrite=true)");
                 const auto existing = readWavLoopMetadata (source);
                 if (! existing.supported) c.args.fail ("cannot edit " + source.getFileName() + ": " + existing.problem);
                 const auto meta = loopMetadataArgs (c.args, existing);
                 const auto result = writeWavLoopMetadata (source, dest, meta);
                 auto o = wavInfoJson (dest, result.written);
                 o.getDynamicObject()->setProperty ("updatedInPlace", dest == source);
                 o.getDynamicObject()->setProperty ("createdChunk", result.createdChunk);
                 o.getDynamicObject()->setProperty ("audioDataSha256", result.dataSha256);
                 o.getDynamicObject()->setProperty ("audioDataUnchanged", true);
                 return o;
             } });

    // ---------------------------------------------------------------- editing preference
    r.add ({ "set_default_declick",
             "Application preference (not part of the project, not undoable): the Default declick time in milliseconds, 0..20 "
             "(default 1.5). Every event fade renders at least this long, so edges never click; 0 gives genuinely hard event "
             "edges. Current value: get_project_state editing.defaultDeclickMs.",
             Schema().number ("ms", "Declick time in milliseconds", true, 0.0, kMaxDeclickMs).build(), K::App,
             [] (ActionContext& c)
             {
                 c.controller.setDefaultDeclickMs (c.args.number ("ms", 0.0, kMaxDeclickMs));
                 return makeObject ({ { "defaultDeclickMs", c.controller.defaultDeclickMs() } });
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
