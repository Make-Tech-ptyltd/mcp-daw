#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "BrowserPanel.h"
#include "CommandTicker.h"
#include "FxRackPanel.h"
#include "MixerPanel.h"
#include "Panel.h"
#include "TimelineView.h"
#include "TransportBar.h"
#include "mcp/McpPipeServer.h"

namespace mcpdaw::ui
{
/** Top-level content: menu, transport, dockable workspace, status line; owns keyboard commands. */
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel,
                            private ProjectController::Listener,
                            private juce::Timer
{
public:
    TimelineView& getTimeline() noexcept { return timeline; }
    MainComponent (AppContext&, juce::AudioDeviceManager&, McpPipeServer*);
    ~MainComponent() override;

    juce::ApplicationCommandManager& getCommandManager() { return commands; }
    /** Ask to save if needed, then run `proceed`. */
    void confirmDiscard (std::function<void()> proceed);
    void openProject (const juce::File&);
    juce::Image snapshot();
    void restoreWorkspace() { workspace.restoreState(); }
    std::function<void (const juce::String& title)> onTitleChanged;

    // Component
    void paint (juce::Graphics&) override;
    void resized() override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int itemId, int topLevelMenuIndex) override;

private:
    AppContext& app;
    juce::AudioDeviceManager& devices;
    McpPipeServer* mcp;
    juce::ApplicationCommandManager commands;
    juce::MenuBarComponent menuBar { this };

    TransportBar transport { app };
    TimelineView timeline { app };
    BrowserPanel browser { app };
    FxRackPanel fxRack { app };
    MixerPanel mixer { app };
    DockablePanel timelinePanel { "timeline", "Arrangement", timeline, false };
    DockablePanel browserPanel { "browser", "Browser", browser };
    DockablePanel fxPanel { "fx", "FX Rack", fxRack };
    DockablePanel mixerPanel { "mixer", "Mixer", mixer };
    Workspace workspace;
    CommandTicker ticker;

    juce::String statusText;
    bool statusIsError = false;
    juce::uint32 statusTime = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> audioSettingsWindow, helpWindow;

    void projectChanged (const Project&, bool) override;
    void timerCallback() override;
    void updateTitle();
    void updateMcpStatus();
    void layoutStatusBar();
    juce::String fileStatusText() const;
    void chooseFile (const juce::String& title, const juce::String& patterns, bool save, std::function<void (const juce::File&)> then);
    void saveAs (std::function<void()> after = {});
    void save (std::function<void()> after = {});
    void showAudioSettings();
    void showMcpHelp();
    void showAbout();
    void renderMix();
    void importAudio();
    void addFileAtCursor (const juce::File&);
};
} // namespace mcpdaw::ui
