#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// The MCP server's self-description (initialize result, protocol versions, tool list), generated
// at build time from the app's own action registry by tools/mcp-bridge/ManifestGen.cpp. The bridge
// answers initialize and tools/list from it, so an MCP client can start up without MCP DAW running.
#include <juce_core/juce_core.h>

namespace mcpdaw::mcp
{
/** JSON: { "initialize": {...}, "protocolVersions": [...], "tools": { "tools": [...] } } */
juce::String manifestJson();
}
