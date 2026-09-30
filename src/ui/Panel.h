#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Dockable / detachable workspace panels.
//
// Deliberately simple: four dock areas (left, centre, right, bottom) with draggable splitters, and
// any panel except the centre one can be torn off into its own top-level window (e.g. browser +
// FX rack on a second monitor). Layout, detach state and window bounds persist between sessions.
// This is not a general docking framework; the Panel/Workspace split keeps one possible later.

#include "Theme.h"

namespace mcpdaw::ui
{
/** Also a DragAndDropContainer so drags keep working when the panel is torn off into its own window. */
class DockablePanel final : public juce::Component, public juce::DragAndDropContainer
{
public:
    DockablePanel (juce::String id, juce::String title, juce::Component& content, bool canDetach = true);

    const juce::String id, title;
    juce::Component& content;

    /** Optional component placed in the header's right-hand side (e.g. a target selector). */
    void setHeaderComponent (juce::Component* c);
    void setDetached (bool d);
    bool isDetached() const noexcept { return detached; }
    std::function<void (DockablePanel&)> onDetachToggle;

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int kHeaderHeight = 28;

private:
    IconButton detachButton { "Detach", Icon::Detach, "Detach into its own window (for a second monitor)" };
    juce::Component* headerComponent = nullptr;
    bool detached = false;
    const bool detachable;
};

class Workspace final : public juce::Component
{
public:
    enum class Area { Left, Centre, Right, Bottom };

    explicit Workspace (juce::PropertiesFile* settings);
    ~Workspace() override;

    void addPanel (DockablePanel&, Area);
    void detach (DockablePanel&);
    void dock (DockablePanel&);
    void toggleDetached (DockablePanel& p) { p.isDetached() ? dock (p) : detach (p); }
    void setPanelVisible (DockablePanel&, bool);
    bool isPanelVisible (const DockablePanel&) const;

    /** Keyboard shortcuts should keep working in torn-off windows. */
    void setKeyListenerForWindows (juce::KeyListener* k) { keyListener = k; }

    void saveState();
    void restoreState();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Slot { DockablePanel* panel; Area area; bool visible = true; };
    class Splitter;
    class PanelWindow;

    juce::PropertiesFile* settings;
    std::vector<Slot> slots;
    std::map<juce::String, std::unique_ptr<PanelWindow>> windows;
    std::unique_ptr<Splitter> leftSplitter, rightSplitter, bottomSplitter;
    int leftWidth = 260, rightWidth = 420, bottomHeight = 250;
    juce::KeyListener* keyListener = nullptr;

    Slot* slotFor (const DockablePanel&);
    const Slot* slotFor (const DockablePanel&) const;
    bool areaOccupied (Area) const;
};
} // namespace mcpdaw::ui
