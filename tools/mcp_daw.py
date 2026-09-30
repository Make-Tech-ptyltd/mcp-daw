#!/usr/bin/env python3
"""mcp_daw - drive a running MCP DAW from Python (standard library only, Python 3.9+).

MCP DAW is controlled over MCP (Model Context Protocol). This module talks to it the same way an
AI app does: it starts `mcp-daw-bridge` and sends it JSON-RPC over stdin/stdout. If MCP DAW isn't
open, the first command opens it.

    from mcp_daw import McpDaw

    with McpDaw(client_name="my-beat") as daw:        # the name shows in MCP DAW's top bar
        daw.call("new_project", discardChanges=True)
        daw.call("set_project_tempo", bpm=120)
        track = daw.call("create_track", name="Kick")["trackId"]
        kick = daw.call("import_audio", path="C:/Samples/kick.wav", place=False)["assetId"]
        daw.batch("Four on the floor", [
            {"action": "create_event", "arguments": {"trackId": track, "assetId": kick,
                                                     "startBeat": b, "looping": False}}
            for b in range(16)])
        daw.call("play", fromBeat=0)

Every tool, its arguments and units are listed in SCRIPTING.md (or ask: `daw.tools()`).

One AI or script uses MCP DAW at a time. In MCP DAW's default "take turns" mode the connection is
handed back after 20 s without commands and taken again automatically on the next call - you don't
need to do anything. If someone else is using it, a call waits up to ~25 s for its turn, then raises
ToolError("MCP DAW is in use by ...").
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

__all__ = ["McpDaw", "ToolError", "find_bridge"]

BRIDGE_NAMES = ("mcp-daw-bridge.exe", "mcp-daw-bridge")
MAX_BATCH_STEPS = 512          # MCP DAW's limit for one execute_batch


class ToolError(RuntimeError):
    """MCP DAW rejected a call (bad argument, unknown id, in use by another client...). The message says why."""


def find_bridge(explicit: str | Path | None = None) -> Path | None:
    """Locate mcp-daw-bridge without assuming a machine layout. Order:
    1. `explicit` (e.g. from a --bridge argument)
    2. the MCP_DAW_BRIDGE environment variable
    3. PATH
    4. next to the running script / this module, or in up to 3 parent folders (the release zip layout)
    5. a build folder: */MCPDAW_artefacts/<config>/ within two levels of those folders (newest wins)
    """
    if explicit:
        p = Path(explicit)
        return p if p.exists() else None
    env = os.environ.get("MCP_DAW_BRIDGE")
    if env and Path(env).exists():
        return Path(env)
    for name in ("mcp-daw-bridge",):
        if found := shutil.which(name):
            return Path(found)

    starts = []
    for f in (getattr(sys.modules.get("__main__"), "__file__", None), __file__):
        if f:
            d = Path(f).resolve().parent
            if d not in starts:
                starts.append(d)
    bases = []
    for d in starts:
        for b in [d, *d.parents][:4]:
            if b not in bases:
                bases.append(b)
    for b in bases:
        for name in BRIDGE_NAMES:
            if (b / name).is_file():
                return b / name
    hits = []
    for b in bases:
        for pattern in ("*/MCPDAW_artefacts/*/mcp-daw-bridge*", "*/*/MCPDAW_artefacts/*/mcp-daw-bridge*"):
            hits += [h for h in b.glob(pattern) if h.suffix in ("", ".exe") and ".old-" not in h.name]
    return max(hits, key=lambda h: h.stat().st_mtime) if hits else None


class McpDaw:
    """A connection to MCP DAW (through mcp-daw-bridge).

    call(tool, **arguments)   run one tool; returns its result (dict, or list for list-type tools)
    batch(label, actions)     several edits as ONE undo step, all-or-nothing
    tools()                   the tool list with descriptions and argument schemas
    close()                   finish (also done by `with McpDaw(...) as daw:`)
    """

    def __init__(self, bridge: str | Path | None = None, client_name: str = "python-script", show_bridge_log: bool = True):
        path = find_bridge(bridge)
        if path is None:
            raise FileNotFoundError(
                "Can't find mcp-daw-bridge. Put this script next to MCP-DAW.exe / mcp-daw-bridge.exe, "
                "set the MCP_DAW_BRIDGE environment variable, or pass bridge=<path>.")
        self.bridge = path
        # The bridge's own status lines (connected / waiting for a turn...) go to stderr.
        self._proc = subprocess.Popen([str(path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=None if show_bridge_log else subprocess.DEVNULL)
        self._next_id = 1
        init = self._request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                            "clientInfo": {"name": client_name, "version": "1.0"}})
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        info = init.get("serverInfo", {})
        self.server = f"{info.get('title', info.get('name', 'MCP DAW'))} {info.get('version', '')}".strip()

    # -- public ------------------------------------------------------------------------------
    def call(self, tool: str, **arguments):
        """Run one tool. Returns its result; raises ToolError with MCP DAW's explanation on failure."""
        result = self._request("tools/call", {"name": tool, "arguments": arguments})
        if result.get("isError"):
            raise ToolError(f"{tool}: {result['content'][0]['text']}")
        sc = result.get("structuredContent")
        if isinstance(sc, dict) and set(sc) == {"items"}:     # list results arrive wrapped as {"items": [...]}
            return sc["items"]
        return sc

    def batch(self, label: str, actions: list):
        """Run several actions as ONE undo step ("Undo <label>"), all-or-nothing.

        actions: [{"action": "<tool>", "arguments": {...}}, ...]. A later step may use an earlier
        step's result as "$<index>.<field>", e.g. "$0.trackId". More than 512 steps: pass a list of
        lists (chunks); each chunk is its own step and references stay within a chunk.
        """
        chunks = actions if actions and isinstance(actions[0], list) else [actions]
        results = []
        for chunk in chunks:
            if not chunk:
                continue
            if len(chunk) > MAX_BATCH_STEPS:
                raise ValueError(f"a batch holds at most {MAX_BATCH_STEPS} steps (got {len(chunk)}); split it into chunks")
            results.append(self.call("execute_batch", label=label, actions=chunk))
        return results[0] if len(results) == 1 else results

    def tools(self) -> list[dict]:
        return self._request("tools/list")["tools"]

    def close(self) -> None:
        try:
            if self._proc.stdin and not self._proc.stdin.closed:
                self._proc.stdin.close()
            self._proc.wait(timeout=60)
        except (OSError, subprocess.TimeoutExpired):
            self._proc.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- JSON-RPC ------------------------------------------------------------------------------
    def _send(self, msg: dict) -> None:
        self._proc.stdin.write((json.dumps(msg) + "\n").encode("utf-8"))
        self._proc.stdin.flush()

    def _request(self, method: str, params: dict | None = None) -> dict:
        msg_id, self._next_id = self._next_id, self._next_id + 1
        msg = {"jsonrpc": "2.0", "id": msg_id, "method": method}
        if params is not None:
            msg["params"] = params
        self._send(msg)
        while True:                                   # replies come back in order; skip anything else
            line = self._proc.stdout.readline()
            if not line:
                raise ConnectionError("mcp-daw-bridge stopped unexpectedly")
            reply = json.loads(line)
            if reply.get("id") != msg_id:
                continue
            if "error" in reply:
                raise ToolError(reply["error"].get("message", "JSON-RPC error"))
            return reply["result"]


if __name__ == "__main__":                            # quick check: python mcp_daw.py
    with McpDaw(client_name="mcp_daw-check") as daw:
        state = daw.call("get_transport_state")
        print(f"Connected to {daw.server} via {daw.bridge}")
        print(f"{len(daw.tools())} tools; transport: {state['position']} at {state['bpm']} BPM, playing={state['playing']}")
