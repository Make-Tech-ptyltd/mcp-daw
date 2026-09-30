#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// FX rack: one clean vertical rack of native effect modules for the selected track (or master),
// instead of a pile of floating plugin windows. Editors are generated from the effect
// descriptors, with purpose-built displays for the EQ curve, compressor gain reduction and the
// multi-tap delay pattern.

#include "AppContext.h"
#include "Theme.h"
#include <set>

namespace mcpdaw::ui
{
class EffectModule;

class FxRackPanel final : public juce::Component, private ProjectController::Listener, private juce::ChangeListener
{
public:
    explicit FxRackPanel (AppContext&);
    ~FxRackPanel() override;

    /** Header widget (target selector) for the DockablePanel title bar. */
    juce::Component& headerComponent() { return targetBox; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    AppContext& app;
    juce::ComboBox targetBox;
    juce::OwnedArray<juce::TextButton> addButtons;
    juce::Viewport viewport;
    juce::Component moduleHolder;
    std::vector<std::unique_ptr<EffectModule>> modules;
    juce::String shownSignature;
    juce::StringArray targetIds;

    juce::String currentTarget() const;
    const std::vector<EffectInstance>* currentChain() const;
    void rebuild (bool force = false);
    void layoutModules();
    // When an effect appears that this panel did not add (MCP, undo/redo), show its chain, so a
    // watcher sees what the agent is doing. Whole-project loads (new/open) never switch.
    std::set<juce::String> knownEffects;
    int seenLoadCount = -1;
    void followNewEffect (const Project&);

    void projectChanged (const Project& p, bool) override { followNewEffect (p); rebuild(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
};
} // namespace mcpdaw::ui
