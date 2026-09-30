#!/bin/sh
# MCP DAW demo: builds three songs live through MCP while they play (about 4 minutes).
# Uses ../mcp-daw-bridge (as laid out by a build), or $MCP_DAW_BRIDGE.
cd "$(dirname "$0")" || exit 1
BRIDGE="${MCP_DAW_BRIDGE:-../mcp-daw-bridge}"
if [ ! -x "$BRIDGE" ]; then
  echo "Could not find mcp-daw-bridge: set MCP_DAW_BRIDGE to its path." >&2
  exit 1
fi
exec "$BRIDGE" --script demo-script.txt "$@"
