#!/usr/bin/env python3
"""End-to-end MCP test: drives a running MCP DAW through the mcp-daw-bridge stdio bridge, exactly as
an AI agent would, builds a small arrangement from the demo loops, renders it and takes a screenshot.

Usage:
    python mcp_smoke_test.py <path to mcp-daw-bridge(.exe)> <demo loops folder> <output folder>

Exit code 0 = every step succeeded. Each call and result is printed so failures are easy to read.
"""
import json
import subprocess
import sys
from pathlib import Path


class McpClient:
    def __init__(self, bridge):
        self.proc = subprocess.Popen([bridge], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=sys.stderr, text=True, bufsize=1)
        self.next_id = 1

    def request(self, method, params=None):
        msg = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            msg["params"] = params
        self.next_id += 1
        self.proc.stdin.write(json.dumps(msg) + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError("bridge closed the connection")
        reply = json.loads(line)
        if "error" in reply:
            raise RuntimeError(f"{method}: {reply['error']}")
        return reply["result"]

    def notify(self, method):
        self.proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
        self.proc.stdin.flush()

    def call(self, name, **arguments):
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        text = result["content"][0]["text"]
        status = "ERROR" if result.get("isError") else "ok"
        print(f"  {name:<22} {status:<5} {text[:150]}")
        if result.get("isError"):
            raise RuntimeError(f"{name} failed: {text}")
        return result.get("structuredContent") or json.loads(text)

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)


def main():
    bridge, loops, out = sys.argv[1], Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    c = McpClient(bridge)
    init = c.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "smoke-test", "version": "1"}})
    print("server:", init["serverInfo"], "protocol", init["protocolVersion"])
    c.notify("notifications/initialized")
    tools = c.request("tools/list")["tools"]
    print(f"{len(tools)} tools:", ", ".join(t["name"] for t in tools))

    c.call("new_project", discardChanges=True)
    ids = {}
    for name in ["kick", "snare", "hats", "bass", "keys", "vox_chop"]:
        r = c.call("import_audio", path=str(loops / f"{name}_90bpm.wav"), startBeat=0)
        ids[name] = r
    # 8-bar arrangement: drums everywhere, bass from bar 3, keys from bar 5, vox in bars 7-8.
    for name in ["kick", "snare", "hats"]:
        c.call("resize_event", eventId=ids[name]["eventId"], endBeat=32)
    c.call("move_event", eventId=ids["bass"]["eventId"], startBeat=8)
    c.call("resize_event", eventId=ids["bass"]["eventId"], endBeat=32)
    c.call("move_event", eventId=ids["keys"]["eventId"], startBeat=16)
    c.call("resize_event", eventId=ids["keys"]["eventId"], endBeat=32)
    c.call("move_event", eventId=ids["vox_chop"]["eventId"], startBeat=24)
    c.call("resize_event", eventId=ids["vox_chop"]["eventId"], endBeat=32)
    # "Create a four-bar intro using the drums but remove the kick for the first two bars."
    c.call("clear_range", startBeat=0, endBeat=8, trackIds=[ids["kick"]["trackId"]])

    # Effects in one undoable batch: EQ + compressor on drums, delay on vox, reverb on master.
    c.call("execute_batch", label="Mix setup", actions=[
        {"action": "add_effect", "arguments": {"target": ids["hats"]["trackId"], "type": "eq",
                                                 "parameters": {"band1.enabled": True, "band1.freq": 300}}},
        {"action": "add_effect", "arguments": {"target": ids["kick"]["trackId"], "type": "compressor",
                                                 "parameters": {"threshold": -20, "ratio": 4, "attack": 15}}},
        {"action": "add_effect", "arguments": {"target": ids["vox_chop"]["trackId"], "type": "multitap_delay",
                                                 "parameters": {"tap1.division": "1/8D", "tap2.division": "1/4", "mix": 0.4}}},
        {"action": "add_effect", "arguments": {"target": ids["keys"]["trackId"], "type": "reverb", "parameters": {"mix": 0.3, "decay": 2.5}}},
        {"action": "set_track_gain", "arguments": {"trackId": ids["hats"]["trackId"], "db": -4}},
        {"action": "set_track_pan", "arguments": {"trackId": ids["keys"]["trackId"], "pan": -0.2}},
        {"action": "set_master_gain", "arguments": {"db": -8}},
    ])
    c.call("set_loop_region", startBeat=0, endBeat=16, enabled=True)
    state = c.call("get_project_state")
    for t in state["tracks"]:
        print(f"    {t['name']:<10} bars {t['occupiedBars']:<8} fx {[fx['type'] for fx in t['effects']]}")
    assert state["bpm"] == 90.0, "project should adopt the loops' 90 BPM"
    assert state["tracks"][0]["occupiedBars"] == "3-8", "kick removed from bars 1-2"

    c.call("save_project", path=str(out / "mcp-demo.mcpdaw"), overwrite=True)
    r = c.call("render_mix", path=str(out / "mcp-demo.wav"), overwrite=True)
    assert r["seconds"] > 20 and not r["clipped"], r
    c.call("seek", beat=16)
    try:
        c.call("capture_screenshot", path=str(out / "mcp-demo.png"))
    except RuntimeError as e:
        print("  (screenshot unavailable:", e, ")")
    c.call("undo")          # undo the loop region
    c.call("redo")
    c.close()
    print("MCP smoke test PASSED")


if __name__ == "__main__":
    main()
