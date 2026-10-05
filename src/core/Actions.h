#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// The application command/action layer.
//
// Every operation a human or an AI can perform on the project is an Action: a name, a JSON-Schema
// for its arguments, a description, and a handler. The GUI invokes actions by name exactly like an
// MCP client does, so both paths share validation, undo behaviour and semantics. The MCP tool list
// is generated from this registry, so it can never drift from what the application actually does.

#include "JsonArgs.h"
#include "ProjectController.h"
#include <functional>
#include <memory>

namespace mcpdaw
{
enum class ActionKind { Query, Edit, Transport, File, App };

class ActionRegistry;

struct ActionContext
{
    ProjectController& controller;
    ActionRegistry& registry;
    JsonArgs args;

    const Project& project() const noexcept { return controller.project(); }
    /** Run an undoable edit. */
    juce::var edit (const juce::String& label, const std::function<juce::var (Project&)>& fn) { return controller.apply (label, fn); }
};

struct ActionDef
{
    juce::String name;
    juce::String description;
    juce::var inputSchema;
    ActionKind kind = ActionKind::Query;
    std::function<juce::var (ActionContext&)> run;
};

/** Small builder for JSON Schema "object" definitions. */
class Schema
{
public:
    Schema& string (const char* name, const char* description, bool required = true);
    Schema& number (const char* name, const char* description, bool required = true,
                    std::optional<double> min = std::nullopt, std::optional<double> max = std::nullopt);
    Schema& integer (const char* name, const char* description, bool required = true,
                     std::optional<double> min = std::nullopt, std::optional<double> max = std::nullopt);
    Schema& boolean (const char* name, const char* description, bool required = true);
    Schema& enumeration (const char* name, const char* description, const juce::StringArray& values, bool required = true);
    Schema& object (const char* name, const char* description, bool required = true);
    Schema& array (const char* name, const char* description, juce::var itemSchema, bool required = true);
    /** A value that may be a number or a string (e.g. effect choice parameters). */
    Schema& numberOrString (const char* name, const char* description, bool required = true);
    juce::var build() const;

private:
    juce::var properties { new juce::DynamicObject() };
    juce::Array<juce::var> requiredNames;
    Schema& add (const char* name, juce::var definition, bool required);
};

class ActionRegistry
{
public:
    void add (ActionDef def);
    const ActionDef* find (const juce::String& name) const noexcept;
    const std::vector<ActionDef>& all() const noexcept { return actions; }

    /** Validate arguments against the schema (required + no unknown keys) and run. Throws on error. */
    juce::var execute (ProjectController&, const juce::String& name, const juce::var& args);

private:
    std::vector<ActionDef> actions;
};

/** Registers the project/transport/file actions that need nothing beyond core. */
void registerCoreActions (ActionRegistry&);

/** What the GUI plugs into the window-level actions. Empty (null) in headless builds: the tools
    still exist, so the tool list is identical everywhere (the MCP bridge ships a copy of it), and
    they report that no window is available. */
struct AppHooks
{
    std::function<juce::var (const juce::File& png)> captureScreenshot;   // returns { path, width, height }
    /** Fit [startBeat, endBeat) across the timeline; endBeat <= startBeat means the whole arrangement. */
    std::function<void (double startBeat, double endBeat)> zoomToFit;
};
/** Registers capture_screenshot and zoom_to_fit. `hooks` is filled in once the window exists. */
void registerAppActions (ActionRegistry&, std::shared_ptr<AppHooks> hooks);

// --- editing helpers (exposed for the GUI and tests) --------------------------------------------
/** Remove/trim every event on `track` overlapping [start, end), except `keepId`. ACID-style overwrite. */
void carveOut (Project&, Track& track, Tick start, Tick end, const juce::String& keepId = {});

/** Source offset an event needs so that, resized to [newStart, newEnd), its audio stays where it
    is on the timeline. Reverse-aware (a reversed event's offset is heard at its end, so it follows
    the right edge). Looping events wrap into the source. */
double offsetForRange (const Project&, const Event&, Tick newStart, Tick newEnd);
/** The same without wrapping: negative = the edge would reach before the start of the source. */
double rawOffsetForRange (const Project&, const Event&, Tick newStart, Tick newEnd);

/** Slip edit: the source offset after moving the audio `deltaBeats` later (positive = right) under
    the stationary event, accounting for pitch (varispeed) and direction. Looping events wrap;
    one-shots are kept within the source (`clamped` reports whether that limited the move). */
double slippedOffset (const Project&, const Event&, double deltaBeats, bool* clamped = nullptr);

/** Split event at tick `at` (strictly inside). Returns the id of the right-hand part. */
juce::String splitEventAt (Project&, Track& track, const juce::String& eventId, Tick at);

/** Default event length for an asset: its natural length at project tempo, rounded to ticks. */
/** Timeline length of the whole source at the project tempo (and the given pitch, which is varispeed). */
Tick naturalLengthTicks (const Project&, const Asset&, double pitchSemitones = 0.0);

/** Parse an effect parameter value given as number, bool or choice name. Throws if invalid. */
float parseParamValue (const ParamSpec&, const juce::var& value);
} // namespace mcpdaw
