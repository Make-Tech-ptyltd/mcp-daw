// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "NamedLock.h"
#include <mutex>

namespace mcpdaw::mcp
{
namespace
{
std::mutex& heldMutex() { static std::mutex m; return m; }
juce::StringArray& heldNames() { static juce::StringArray names; return names; }
} // namespace

NamedLock::NamedLock (juce::String n) : name (std::move (n)) {}
NamedLock::~NamedLock() { release(); }

bool NamedLock::tryAcquire()
{
    if (held) return true;
    const std::lock_guard<std::mutex> guard (heldMutex());
    if (heldNames().contains (name)) return false;
    auto l = std::make_unique<juce::InterProcessLock> (name);
    if (! l->enter (0)) return false;
    lock = std::move (l);
    heldNames().add (name);
    held = true;
    return true;
}

void NamedLock::release()
{
    if (! held) return;
    const std::lock_guard<std::mutex> guard (heldMutex());
    lock->exit();
    lock.reset();
    heldNames().removeString (name);
    held = false;
}

bool NamedLock::isHeldAnywhere (const juce::String& lockName)
{
    const std::lock_guard<std::mutex> guard (heldMutex());
    if (heldNames().contains (lockName)) return true;
    juce::InterProcessLock probe (lockName);
    if (! probe.enter (0)) return true;
    probe.exit();
    return false;
}
} // namespace mcpdaw::mcp
