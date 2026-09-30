#!/usr/bin/env python3
"""Writes docs/TOOLS.md - every MCP DAW tool with its arguments - straight from the bridge.

The bridge answers `tools/list` from its built-in manifest (generated from the app's own actions),
so MCP DAW doesn't need to be running and this reference can never drift from the build.
--effects also appends every effect parameter (this calls list_effect_types, which opens MCP DAW):

    python tools/gen_tool_reference.py [--effects] [--bridge <path to mcp-daw-bridge>]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mcp_daw import McpDaw  # noqa: E402

GROUPS = [   # (heading, tool names in order); anything not listed lands in "Other"
    ("Inspect", ["get_project_state", "list_tracks", "get_track", "list_assets", "list_effect_types",
                 "get_effect_parameters", "get_history", "get_transport_state"]),
    ("Project and files", ["new_project", "open_project", "save_project", "set_project_name", "set_project_tempo",
                           "render_mix"]),
    ("Tracks and mixing", ["create_track", "delete_track", "rename_track", "move_track", "set_track_colour",
                           "set_track_gain", "set_track_pan", "mute_track", "solo_track", "set_master_gain"]),
    ("Audio and events", ["import_audio", "replace_track_audio", "set_asset_bpm", "create_event", "move_event",
                          "move_events", "resize_event", "split_event", "duplicate_event", "delete_event",
                          "set_event_looping", "set_event_gain", "set_event_pitch"]),
    ("Arrangement", ["clear_range", "duplicate_range"]),
    ("Effects", ["add_effect", "remove_effect", "move_effect", "set_effect_bypass", "set_effect_parameters",
                 "set_effect_parameter"]),
    ("Transport", ["play", "stop", "pause", "seek", "set_loop_region"]),
    ("Edits as one step, undo", ["execute_batch", "undo", "redo"]),
    ("Window", ["zoom_to_fit", "capture_screenshot"]),
]


def describe_arg(name: str, spec: dict, required: bool) -> str:
    kind = spec.get("type", "")
    if "enum" in spec:
        kind = " | ".join(f"`{e}`" for e in spec["enum"])
    rng = ""
    if "minimum" in spec or "maximum" in spec:
        rng = f" ({spec.get('minimum', '')}..{spec.get('maximum', '')})"
    text = spec.get("description", "")
    return f"`{name}`{'' if required else ' *(optional)*'} — {kind}{rng}. {text}".rstrip(". ") + "."


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--bridge", type=Path, default=None)
    ap.add_argument("--effects", action="store_true", help="also list effect parameters (opens MCP DAW)")
    ap.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "docs" / "TOOLS.md")
    args = ap.parse_args()

    with McpDaw(args.bridge, client_name="tool-reference", show_bridge_log=False) as daw:
        tools = {t["name"]: t for t in daw.tools()}
        server = daw.server
        effects = daw.call("list_effect_types") if args.effects else []

    listed = [n for _, names in GROUPS for n in names]
    groups = GROUPS + [("Other", [n for n in tools if n not in listed])]
    out = [f"# MCP DAW tools", "",
           f"Generated from {server} by `tools/gen_tool_reference.py` — {len(tools)} tools. "
           "Positions and lengths are **beats** (zero-based quarter notes; bar N starts at beat (N−1)×4), "
           "levels are **dB**, pan is −1…+1. From Python: `daw.call(\"<tool>\", arg=value, ...)`.", ""]
    for heading, names in groups:
        names = [n for n in names if n in tools]
        if not names:
            continue
        out += [f"## {heading}", ""]
        for n in names:
            t = tools[n]
            schema = t.get("inputSchema", {})
            props, req = schema.get("properties", {}), set(schema.get("required", []))
            out += [f"### `{n}`", "", t.get("description", ""), ""]
            out += [f"- {describe_arg(k, v, k in req)}" for k, v in props.items()] or ["- *(no arguments)*"]
            out.append("")
    if effects:
        out += ["## Effect parameters", "",
                "For `add_effect` (`parameters`) and `set_effect_parameters` (`values`). Choice parameters "
                "also accept their name, e.g. `\"type\": \"Warm\"`.", ""]
        for fx in effects:
            out += [f"### `{fx['type']}` — {fx['name']}", "", fx.get("description", ""), "",
                    "| Parameter | Range | Default | Notes |", "| --- | --- | --- | --- |"]
            num = lambda v: f"{round(v, 3):g}"                          # noqa: E731
            for p in fx.get("parameters", []):
                if p.get("choices"):
                    rng, default = " / ".join(p["choices"]), p["choices"][int(p["default"])]
                elif p.get("kind") == "toggle":
                    rng, default = "on / off", "on" if p["default"] >= 0.5 else "off"
                else:
                    unit = p.get("unit", "")
                    rng, default = f"{num(p['min'])} .. {num(p['max'])} {unit}".strip(), f"{num(p['default'])} {unit}".strip()
                out.append(f"| `{p['id']}` | {rng} | {default} | {p.get('help', '') or p.get('name', '')} |")
            out.append("")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(out), encoding="utf-8")
    print(f"wrote {args.out} ({len(tools)} tools)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
