// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "Panel.h"

namespace mcpdaw::ui
{
// ================================================================================================
// DockablePanel
// ================================================================================================
DockablePanel::DockablePanel (juce::String i, juce::String t, juce::Component& c, bool canDetach)
    : id (std::move (i)), title (std::move (t)), content (c), detachable (canDetach)
{
    addAndMakeVisible (content);
    if (detachable)
    {
        addAndMakeVisible (detachButton);
        detachButton.onClick = [this] { if (onDetachToggle) onDetachToggle (*this); };
    }
}

void DockablePanel::setHeaderComponent (juce::Component* c)
{
    if (headerComponent != nullptr) removeChildComponent (headerComponent);
    headerComponent = c;
    if (c != nullptr) addAndMakeVisible (c);
    resized();
}

void DockablePanel::setDetached (bool d)
{
    detached = d;
    detachButton.setIcon (d ? Icon::Dock : Icon::Detach);
    detachButton.setTooltip (d ? "Dock back into the main window" : "Detach into its own window (for a second monitor)");
}

void DockablePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    auto header = getLocalBounds().removeFromTop (kHeaderHeight);
    g.setColour (colours::raised);
    g.fillRect (header);
    g.setColour (colours::accent);
    g.fillRect (header.getX() + 10, header.getCentreY() - 5, 3, 10);
    g.setColour (colours::textDim);
    g.setFont (fonts::display (12.5f));
    g.drawText (title.toUpperCase(), header.withTrimmedLeft (20), juce::Justification::centredLeft);
    g.setColour (colours::lineSoft);
    g.drawHorizontalLine (kHeaderHeight - 1, 0.0f, static_cast<float> (getWidth()));
}

void DockablePanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (kHeaderHeight);
    if (detachable) detachButton.setBounds (header.removeFromRight (kHeaderHeight).reduced (4));
    if (headerComponent != nullptr)
    {
        const int w = std::min (header.getWidth() - 140, std::max (headerComponent->getWidth(), 160));
        headerComponent->setBounds (header.removeFromRight (w).reduced (2, 3));
    }
    content.setBounds (r);
}

// ================================================================================================
// Splitter and detached window
// ================================================================================================
class Workspace::Splitter final : public juce::Component
{
public:
    Splitter (bool verticalBar, std::function<void (int delta)> onDragIn, std::function<void()> onEndIn)
        : vertical (verticalBar), onDrag (std::move (onDragIn)), onEnd (std::move (onEndIn))
    {
        setMouseCursor (vertical ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::window);
        g.setColour (isMouseOverOrDragging() ? colours::accentDim : colours::lineSoft);
        if (vertical) g.fillRect (getWidth() / 2, 0, 1, getHeight());
        else g.fillRect (0, getHeight() / 2, getWidth(), 1);
    }
    void mouseDown (const juce::MouseEvent&) override { last = 0; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int d = vertical ? e.getDistanceFromDragStartX() : e.getDistanceFromDragStartY();
        onDrag (d - last);
        last = d;
    }
    void mouseUp (const juce::MouseEvent&) override { onEnd(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    bool vertical;
    int last = 0;
    std::function<void (int)> onDrag;
    std::function<void()> onEnd;
};

class Workspace::PanelWindow final : public juce::DocumentWindow
{
public:
    PanelWindow (DockablePanel& p, std::function<void()> onCloseIn)
        : juce::DocumentWindow ("MCP DAW - " + p.title, colours::panel, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          onClose (std::move (onCloseIn))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setContentNonOwned (&p, false);
    }
    void closeButtonPressed() override { onClose(); }

private:
    std::function<void()> onClose;
};

// ================================================================================================
// Workspace
// ================================================================================================
Workspace::Workspace (juce::PropertiesFile* s) : settings (s)
{
    auto clampSizes = [this]
    {
        leftWidth = juce::jlimit (160, std::max (160, getWidth() / 2), leftWidth);
        rightWidth = juce::jlimit (240, std::max (240, getWidth() / 2), rightWidth);
        bottomHeight = juce::jlimit (120, std::max (120, getHeight() - 200), bottomHeight);
        resized();
    };
    leftSplitter = std::make_unique<Splitter> (true, [this, clampSizes] (int d) { leftWidth += d; clampSizes(); }, [this] { saveState(); });
    rightSplitter = std::make_unique<Splitter> (true, [this, clampSizes] (int d) { rightWidth -= d; clampSizes(); }, [this] { saveState(); });
    bottomSplitter = std::make_unique<Splitter> (false, [this, clampSizes] (int d) { bottomHeight -= d; clampSizes(); }, [this] { saveState(); });
    addChildComponent (*leftSplitter);
    addChildComponent (*rightSplitter);
    addChildComponent (*bottomSplitter);
}

Workspace::~Workspace()
{
    saveState();
    windows.clear();   // detached windows only reference panels, never own them
}

void Workspace::addPanel (DockablePanel& p, Area area)
{
    slots.push_back ({ &p, area, true });
    addAndMakeVisible (p);
    p.onDetachToggle = [this] (DockablePanel& panel) { toggleDetached (panel); };
    resized();
}

Workspace::Slot* Workspace::slotFor (const DockablePanel& p)
{
    for (auto& s : slots) if (s.panel == &p) return &s;
    return nullptr;
}

const Workspace::Slot* Workspace::slotFor (const DockablePanel& p) const
{
    for (const auto& s : slots) if (s.panel == &p) return &s;
    return nullptr;
}

bool Workspace::areaOccupied (Area a) const
{
    for (const auto& s : slots)
        if (s.area == a && s.visible && ! s.panel->isDetached()) return true;
    return false;
}

void Workspace::detach (DockablePanel& p)
{
    if (p.isDetached()) return;
    removeChildComponent (&p);
    p.setDetached (true);
    auto window = std::make_unique<PanelWindow> (p, [this, &p] { dock (p); });
    if (keyListener != nullptr) window->addKeyListener (keyListener);

    // Restore the last position if it is still on a connected display, else cascade from here.
    juce::Rectangle<int> bounds;
    if (settings != nullptr) bounds = juce::Rectangle<int>::fromString (settings->getValue ("window." + p.id));
    const bool onScreen = ! bounds.isEmpty() && juce::Desktop::getInstance().getDisplays().getDisplayForRect (bounds) != nullptr
                          && juce::Desktop::getInstance().getDisplays().getTotalBounds (true).intersects (bounds);
    if (! onScreen)
    {
        const auto origin = getScreenBounds().getCentre();
        bounds = juce::Rectangle<int> (origin.x - 220, origin.y - 300, 440, 640);
    }
    window->setBounds (bounds);
    window->setVisible (true);
    windows[p.id] = std::move (window);
    if (auto* s = slotFor (p)) s->visible = true;
    resized();
    saveState();
}

void Workspace::dock (DockablePanel& p)
{
    if (! p.isDetached()) return;
    if (auto it = windows.find (p.id); it != windows.end())
    {
        if (settings != nullptr) settings->setValue ("window." + p.id, it->second->getBounds().toString());
        it->second->setVisible (false);
        it->second->clearContentComponent();
        juce::MessageManager::callAsync ([this, id = p.id] { windows.erase (id); });   // not from inside its own callback
    }
    p.setDetached (false);
    addAndMakeVisible (p);
    resized();
    saveState();
}

void Workspace::setPanelVisible (DockablePanel& p, bool visible)
{
    if (auto* s = slotFor (p))
    {
        s->visible = visible;
        if (! p.isDetached()) p.setVisible (visible);
        else if (auto it = windows.find (p.id); it != windows.end()) it->second->setVisible (visible);
        resized();
        saveState();
    }
}

bool Workspace::isPanelVisible (const DockablePanel& p) const
{
    const auto* s = slotFor (p);
    return s != nullptr && s->visible;
}

void Workspace::paint (juce::Graphics& g)
{
    g.fillAll (colours::window);
}

void Workspace::resized()
{
    constexpr int kSplit = 5;
    auto r = getLocalBounds();
    const bool hasLeft = areaOccupied (Area::Left), hasRight = areaOccupied (Area::Right), hasBottom = areaOccupied (Area::Bottom);

    auto place = [this] (Area a, juce::Rectangle<int> bounds)
    {
        for (auto& s : slots)
            if (s.area == a && ! s.panel->isDetached())
            {
                s.panel->setVisible (s.visible);
                if (s.visible) s.panel->setBounds (bounds);
            }
    };

    bottomSplitter->setVisible (hasBottom);
    if (hasBottom)
    {
        auto bottom = r.removeFromBottom (std::min (bottomHeight, r.getHeight() - 150));
        bottomSplitter->setBounds (r.removeFromBottom (kSplit));
        place (Area::Bottom, bottom);
    }
    leftSplitter->setVisible (hasLeft);
    if (hasLeft)
    {
        place (Area::Left, r.removeFromLeft (leftWidth));
        leftSplitter->setBounds (r.removeFromLeft (kSplit));
    }
    rightSplitter->setVisible (hasRight);
    if (hasRight)
    {
        place (Area::Right, r.removeFromRight (rightWidth));
        rightSplitter->setBounds (r.removeFromRight (kSplit));
    }
    place (Area::Centre, r);
}

void Workspace::saveState()
{
    if (settings == nullptr) return;
    settings->setValue ("layout.left", leftWidth);
    settings->setValue ("layout.right", rightWidth);
    settings->setValue ("layout.bottom", bottomHeight);
    for (const auto& s : slots)
    {
        settings->setValue ("panel." + s.panel->id + ".detached", s.panel->isDetached());
        settings->setValue ("panel." + s.panel->id + ".visible", s.visible);
        if (auto it = windows.find (s.panel->id); it != windows.end())
            settings->setValue ("window." + s.panel->id, it->second->getBounds().toString());
    }
    settings->saveIfNeeded();
}

void Workspace::restoreState()
{
    if (settings == nullptr) return;
    leftWidth = settings->getIntValue ("layout.left", leftWidth);
    rightWidth = settings->getIntValue ("layout.right", rightWidth);
    bottomHeight = settings->getIntValue ("layout.bottom", bottomHeight);
    for (auto& s : slots)
    {
        s.visible = settings->getBoolValue ("panel." + s.panel->id + ".visible", true);
        if (settings->getBoolValue ("panel." + s.panel->id + ".detached", false)) detach (*s.panel);
    }
    resized();
}
} // namespace mcpdaw::ui
