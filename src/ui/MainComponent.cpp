// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "MainComponent.h"
#include "mcp/PipeName.h"
#include "app/AppLinks.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace mcpdaw::ui
{
namespace
{
enum Command : juce::CommandID
{
    cmdNew = 1, cmdOpen, cmdSave, cmdSaveAs, cmdImport, cmdRender, cmdAudioSettings,
    cmdUndo = 100, cmdRedo, cmdCut, cmdCopy, cmdPaste, cmdDelete, cmdSplit, cmdDuplicate, cmdSelectAll, cmdToggleLooping,
    cmdLoopToSelection, cmdNudgeLeft, cmdNudgeRight, cmdAddTrack,
    cmdPlayStop = 200, cmdStop, cmdRewind, cmdToggleLoop, cmdGoToEnd,
    cmdZoomIn = 300, cmdZoomOut, cmdZoomFit, cmdFollow, cmdShowBrowser, cmdShowFx, cmdShowMixer, cmdDetachBrowser, cmdDetachFx, cmdDetachMixer,
    cmdMcpHelp = 400
};
// Plain menu items (not commands) for Help and the heart menu.
enum { kAboutItem = 900, kFeedbackItem, kDonateItem, kCopyPaypalItem, kSourceItem };
} // namespace

MainComponent::MainComponent (AppContext& a, juce::AudioDeviceManager& d, McpPipeServer* server)
    : app (a), devices (d), mcp (server), workspace (a.properties.getUserSettings())
{
    setOpaque (true);
    addAndMakeVisible (menuBar);
    addAndMakeVisible (transport);
    addAndMakeVisible (workspace);

    workspace.addPanel (timelinePanel, Workspace::Area::Centre);
    workspace.addPanel (browserPanel, Workspace::Area::Left);
    workspace.addPanel (fxPanel, Workspace::Area::Right);
    workspace.addPanel (mixerPanel, Workspace::Area::Bottom);
    fxPanel.setHeaderComponent (&fxRack.headerComponent());

    transport.onAudioSettings = [this] { showAudioSettings(); };
    browser.onAddFile = [this] (const juce::File& f) { addFileAtCursor (f); };
    app.onStatus = [this] (const juce::String& text, bool isError)
    {
        statusText = text;
        statusIsError = isError;
        statusTime = juce::Time::getMillisecondCounter();
        repaint (getLocalBounds().removeFromBottom (24));
    };

    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    addKeyListener (commands.getKeyMappings());
    workspace.setKeyListenerForWindows (commands.getKeyMappings());

    addAndMakeVisible (ticker);
    ticker.onActiveChanged = [this] { repaint (getLocalBounds().removeFromBottom (24)); };
    if (mcp != nullptr)
    {
        mcp->onStatusChanged = [this] { updateMcpStatus(); };
        mcp->onToolCall = [this] (const juce::String& client, const juce::String& tool, const juce::var& args)
        {
            ticker.push (client, tool, args);
        };
    }
    updateMcpStatus();

    app.controller.addListener (this);
    setSize (1600, 960);
    startTimerHz (30);
    updateTitle();
}

MainComponent::~MainComponent()
{
    app.controller.removeListener (this);
    removeKeyListener (commands.getKeyMappings());
    if (mcp != nullptr)
    {
        mcp->onStatusChanged = nullptr;
        mcp->onToolCall = nullptr;
    }
}

// ================================================================================================
void MainComponent::projectChanged (const Project& p, bool)
{
    // Keep a track selected so the FX rack always has something sensible to show.
    if (p.findTrack (app.selectedTrack) == nullptr && ! p.tracks.empty()) app.selectTrack (p.tracks.front().id);
    updateTitle();
    commands.commandStatusChanged();
}

void MainComponent::updateMcpStatus()
{
    using S = TransportBar::McpState;
    if (mcp == nullptr) transport.setMcpStatus (S::Off, {});
    else if (mcp->isClientConnected()) transport.setMcpStatus (S::Connected, mcp->clientLabel());
    else transport.setMcpStatus (S::Ready, mcp->lastClientLabel());
}

juce::String MainComponent::fileStatusText() const
{
    const auto file = app.controller.currentFile();
    return (file == juce::File() ? juce::String ("Unsaved project") : file.getFullPathName()) + (app.controller.isDirty() ? "  (modified)" : "");
}

void MainComponent::layoutStatusBar()
{
    // The ticker runs from the left edge of the status bar up to the project/file text on the right.
    auto text = getLocalBounds().removeFromBottom (24).reduced (10, 0);
    const int fileWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (fonts::ui (12.0f), fileStatusText()));
    text.removeFromRight (juce::jmin (text.getWidth() / 2, fileWidth + 24));
    ticker.setBounds (text.reduced (0, 1));
}

void MainComponent::updateTitle()
{
    const auto file = app.controller.currentFile();
    const auto name = file == juce::File() ? app.project().name : file.getFileNameWithoutExtension();
    if (onTitleChanged) onTitleChanged ("MCP DAW by Make Tech  -  " + name + (app.controller.isDirty() ? " *" : ""));
    layoutStatusBar();
    repaint (getLocalBounds().removeFromBottom (24));
}

void MainComponent::timerCallback()
{
    app.pollMeters();
    if (statusText.isNotEmpty() && juce::Time::getMillisecondCounter() - statusTime > (statusIsError ? 10000u : 5000u))
    {
        statusText.clear();
        repaint (getLocalBounds().removeFromBottom (24));
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::window);
    auto status = getLocalBounds().removeFromBottom (24);
    g.setColour (colours::raised);
    g.fillRect (status);
    g.setColour (colours::lineSoft);
    g.drawHorizontalLine (status.getY(), 0.0f, static_cast<float> (getWidth()));
    auto text = status.reduced (10, 0);
    g.setFont (fonts::ui (12.0f));
    g.setColour (colours::textFaint);
    g.drawText (fileStatusText(), text.removeFromRight (text.getWidth() / 2), juce::Justification::centredRight, true);
    if (ticker.isActive()) return;   // MCP commands are streaming: the ticker has this space
    g.setColour (statusIsError ? colours::red : colours::textDim);
    g.drawText (statusText.isNotEmpty() ? statusText
                                        : juce::String ("Space play/stop  |  drag right edge to loop  |  drag empty lane to paint, Ctrl+drag to select  |  S split  |  Ctrl+wheel zoom"),
                text, juce::Justification::centredLeft, true);
    g.setColour (colours::textFaint.withAlpha (0.7f));
    g.setFont (fonts::ui (11.0f));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    menuBar.setBounds (r.removeFromTop (24));
    transport.setBounds (r.removeFromTop (54));
    r.removeFromBottom (24);
    workspace.setBounds (r);
    layoutStatusBar();
}

juce::Image MainComponent::snapshot()
{
    return createComponentSnapshot (getLocalBounds(), true, 1.0f);
}

// ================================================================================================
// Files
// ================================================================================================
void MainComponent::chooseFile (const juce::String& title, const juce::String& patterns, bool saving, std::function<void (const juce::File&)> then)
{
    auto start = app.controller.currentFile();
    if (start == juce::File()) start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> (title, start, patterns);
    const int flags = saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                             : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [then] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != juce::File()) then (f);
    });
}

void MainComponent::save (std::function<void()> after)
{
    if (app.controller.currentFile() == juce::File()) { saveAs (std::move (after)); return; }
    if (app.run ("save_project").isObject())
    {
        app.status ("Saved " + app.controller.currentFile().getFileName());
        if (after) after();
    }
}

void MainComponent::saveAs (std::function<void()> after)
{
    chooseFile ("Save project", "*.mcpdaw", true, [this, after] (const juce::File& f)
    {
        if (app.run ("save_project", makeObject ({ { "path", f.withFileExtension ("mcpdaw").getFullPathName() }, { "overwrite", true } })).isObject())
        {
            app.status ("Saved " + app.controller.currentFile().getFileName());
            updateTitle();
            if (after) after();
        }
    });
}

void MainComponent::confirmDiscard (std::function<void()> proceed)
{
    if (! app.controller.isDirty()) { proceed(); return; }
    auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Unsaved changes").withMessage ("Save changes to \"" + app.project().name + "\" first?")
                       .withButton ("Save").withButton ("Don't save").withButton ("Cancel").withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [this, proceed] (int result)
    {
        if (result == 1) save (proceed);
        else if (result == 2) proceed();
    });
}

void MainComponent::openProject (const juce::File& f)
{
    const auto r = app.run ("open_project", makeObject ({ { "path", f.getFullPathName() }, { "discardChanges", true } }));
    if (r.isObject())
    {
        app.selectEvents ({});
        app.selectTrack (app.project().tracks.empty() ? juce::String() : app.project().tracks.front().id);
        timeline.zoomToFit();
        app.status ("Opened " + f.getFileName());
    }
}

void MainComponent::importAudio()
{
    chooseFile ("Import audio", "*.wav;*.aif;*.aiff;*.flac", false, [this] (const juce::File& f) { addFileAtCursor (f); });
}

void MainComponent::addFileAtCursor (const juce::File& f)
{
    // ACID: adding from the browser creates a new track; place at the cursor, snapped to the bar.
    const auto at = snapTicksDown (beatsToTicks (app.engine.positionBeats()), kTicksPerBar);
    const auto r = app.run ("import_audio", makeObject ({ { "path", f.getFullPathName() }, { "startBeat", ticksToBeats (at) } }));
    if (r.isObject())
    {
        app.selectTrack (r["trackId"].toString());
        app.selectEvents ({ r["eventId"].toString() });
        if (static_cast<bool> (r["tempoAdopted"]))
            app.status ("Project tempo set to " + juce::String (static_cast<double> (r["projectBpm"]), 2) + " BPM from " + f.getFileName());
    }
}

void MainComponent::renderMix()
{
    if (app.project().contentEnd() == 0) { app.status ("Nothing to render yet", true); return; }
    chooseFile ("Render mix to WAV", "*.wav", true, [this] (const juce::File& f)
    {
        const auto r = app.run ("render_mix", makeObject ({ { "path", f.withFileExtension ("wav").getFullPathName() }, { "overwrite", true } }));
        if (r.isObject())
            app.status ("Rendered " + juce::String (static_cast<double> (r["seconds"]), 1) + " s to " + f.getFileName()
                        + "  (peak " + juce::String (static_cast<double> (r["peakDb"]), 1) + " dBFS" + (static_cast<bool> (r["clipped"]) ? ", CLIPPED)" : ")"),
                        static_cast<bool> (r["clipped"]));
    });
}

// ================================================================================================
// Auxiliary windows
// ================================================================================================
namespace
{
class ToolWindow final : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& title, juce::Component* content, std::function<void()> onCloseIn)
        : juce::DocumentWindow (title, colours::panel, juce::DocumentWindow::closeButton), onClose (std::move (onCloseIn))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, false);
    }
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    std::function<void()> onClose;
};

} // namespace

void MainComponent::showAudioSettings()
{
    if (audioSettingsWindow != nullptr) { audioSettingsWindow->toFront (true); return; }
    auto* selector = new juce::AudioDeviceSelectorComponent (devices, 0, 0, 2, 2, false, false, true, false);
    selector->setSize (560, 440);
    audioSettingsWindow = std::make_unique<ToolWindow> ("Audio settings", selector, [this]
    {
        if (auto xml = devices.createStateXml())
            if (auto* props = app.properties.getUserSettings()) { props->setValue ("audio.device", xml.get()); props->saveIfNeeded(); }
        juce::MessageManager::callAsync ([this] { audioSettingsWindow.reset(); });
    });
    audioSettingsWindow->centreAroundComponent (this, 560, 440);
    audioSettingsWindow->setVisible (true);
}

namespace
{
juce::File defaultBridgeFile()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile (
       #if JUCE_WINDOWS
        "mcp-daw-bridge.exe"
       #else
        "mcp-daw-bridge"
       #endif
    );
}

/** MCP setup: where the stdio bridge lives (a per-user setting, defaulting to next to the app),
    ready-to-paste client configs built from that path, and live server status. Nothing about the
    machine is baked in: every path shown is computed at runtime or chosen by the user. */
class McpSetupPanel final : public juce::Component
{
public:
    McpSetupPanel (AppContext& a, McpPipeServer* server, std::function<juce::String()> status)
        : app (a), mcp (server), statusText (std::move (status))
    {
        takeTurns.setButtonText ("Take turns: release the connection after 20 seconds idle, so other AIs and scripts can connect");
        takeTurns.setTooltip ("On: Claude, ChatGPT and scripts take turns - whoever is working holds the connection, and hands it back "
                              "after 20 s without commands.\nOff: lock - the first AI to connect keeps it until it disconnects.");
        takeTurns.setToggleState (mcp == nullptr || mcp->getTakeTurns(), juce::dontSendNotification);
        takeTurns.setEnabled (mcp != nullptr);
        takeTurns.onClick = [this]
        {
            const bool on = takeTurns.getToggleState();
            if (mcp != nullptr) mcp->setTakeTurns (on);
            if (auto* props = app.properties.getUserSettings())
            {
                props->setValue ("mcp.takeTurns", on);
                props->saveIfNeeded();
            }
            app.status (on ? "MCP: taking turns (released after 20 s idle)" : "MCP: locked to the first AI that connects");
            refresh();
        };
        addAndMakeVisible (takeTurns);
        title.setText ("MCP bridge executable (what an AI client launches):", juce::dontSendNotification);
        title.setFont (fonts::ui (13.5f, true));
        addAndMakeVisible (title);
        pathField.setFont (fonts::mono (13.0f));
        pathField.setText (bridge().getFullPathName(), false);
        pathField.onReturnKey = pathField.onFocusLost = [this] { setBridge (juce::File (pathField.getText().trim())); };
        addAndMakeVisible (pathField);
        for (auto* b : { &browseButton, &defaultButton, &copyClaude, &copyCodex, &copyVsCode }) addAndMakeVisible (*b);
        browseButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Locate mcp-daw-bridge", bridge().getParentDirectory(),
                                                           juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFileExtension().isNotEmpty() ? "*.exe" : "*");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe = juce::Component::SafePointer<McpSetupPanel> (this)] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult().existsAsFile()) safe->setBridge (fc.getResult());
            });
        };
        defaultButton.onClick = [this] { setBridge ({}); };
        copyClaude.onClick = [this] { copy (claudeConfig(), "Claude Desktop config"); };
        copyCodex.onClick = [this] { copy (codexConfig(), "Codex config"); };
        copyVsCode.onClick = [this] { copy (vsCodeConfig(), "VS Code / Cursor config"); };
        info.setMultiLine (true);
        info.setReadOnly (true);
        info.setFont (fonts::mono (13.0f));
        addAndMakeVisible (info);
        refresh();
        setSize (780, 600);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        title.setBounds (r.removeFromTop (22));
        auto row = r.removeFromTop (28);
        defaultButton.setBounds (row.removeFromRight (90));
        row.removeFromRight (6);
        browseButton.setBounds (row.removeFromRight (90));
        row.removeFromRight (6);
        pathField.setBounds (row);
        r.removeFromTop (8);
        auto buttons = r.removeFromTop (28);
        const int w = (buttons.getWidth() - 12) / 3;
        copyClaude.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (6);
        copyCodex.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (6);
        copyVsCode.setBounds (buttons);
        r.removeFromTop (10);
        takeTurns.setBounds (r.removeFromTop (24));
        r.removeFromTop (8);
        info.setBounds (r);
    }

private:
    AppContext& app;
    McpPipeServer* mcp;
    std::function<juce::String()> statusText;
    juce::ToggleButton takeTurns;
    juce::Label title;
    juce::TextEditor pathField, info;
    juce::TextButton browseButton { "Browse..." }, defaultButton { "Default" },
                     copyClaude { "Copy Claude Desktop config" }, copyCodex { "Copy Codex config" }, copyVsCode { "Copy VS Code / Cursor config" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::File bridge() const
    {
        if (auto* props = app.properties.getUserSettings())
            if (const auto custom = props->getValue ("mcp.bridgePath"); custom.isNotEmpty()) return juce::File (custom);
        return defaultBridgeFile();
    }
    void setBridge (const juce::File& f)
    {
        if (auto* props = app.properties.getUserSettings())
        {
            props->setValue ("mcp.bridgePath", f == juce::File() || f == defaultBridgeFile() ? juce::String() : f.getFullPathName());
            props->saveIfNeeded();
        }
        pathField.setText (bridge().getFullPathName(), false);
        refresh();
    }
    juce::String jsonPath() const { return bridge().getFullPathName().replace ("\\", "\\\\"); }
    juce::String claudeConfig() const { return "{ \"mcpServers\": { \"mcp-daw\": { \"command\": \"" + jsonPath() + "\" } } }"; }
    juce::String codexConfig() const { return "[mcp_servers.mcp-daw]\ncommand = '" + bridge().getFullPathName() + "'"; }
    juce::String vsCodeConfig() const { return "{ \"servers\": { \"mcp-daw\": { \"type\": \"stdio\", \"command\": \"" + jsonPath() + "\" } } }"; }
    void copy (const juce::String& text, const juce::String& what)
    {
        juce::SystemClipboard::copyTextToClipboard (text);
        app.status ("Copied " + what + " to the clipboard");
    }

    void refresh()
    {
        juce::String t;
        if (! bridge().existsAsFile())
            t << "WARNING: no file at that path. Point this at mcp-daw-bridge (shipped next to MCP DAW).\n\n";
        t << statusText() << "\n\n"
          << "Agents connect through the stdio bridge above, which forwards to THIS running session.\n"
          << "One AI or script at a time. " << (mcp == nullptr || mcp->getTakeTurns()
                 ? "Taking turns: the connection is handed back after 20 s without commands,\nso Claude, ChatGPT and scripts can each take a turn.\n"
                 : "Locked: the first AI to connect keeps it until it disconnects.\n")
          << "An AI app starting up never opens MCP DAW; the first real command does (if it isn't open).\n"
          << "The bridge path is stored in your user settings.\n\n"
          << "Claude Desktop / Cowork  (claude_desktop_config.json):\n  " << claudeConfig() << "\n\n"
          << "Codex CLI  (~/.codex/config.toml):\n  " << codexConfig().replace ("\n", "\n  ") << "\n\n"
          << "Cursor / VS Code  (mcp.json):\n  " << vsCodeConfig() << "\n\n"
          << "Tools (" << app.registry.all().size() << "): ";
        juce::StringArray names;
        for (const auto& a : app.registry.all()) names.add (a.name);
        t << names.joinIntoString (", ")
          << "\n\nPositions are zero-based beats; bar N starts at beat (N-1)*4. Every edit is undoable (Ctrl+Z).\n";
        info.setText (t, false);
    }
};
} // namespace

void MainComponent::showMcpHelp()
{
    if (helpWindow != nullptr) { helpWindow->toFront (true); return; }
    auto status = [this]
    {
        juce::String s = "Status: ";
        if (mcp == nullptr) s << "OFF (the MCP server could not start)";
        else if (mcp->isClientConnected()) s << "connected - " << mcp->clientLabel();
        else s << "ready" << (mcp->lastClientLabel().isNotEmpty() ? " (last: " + mcp->lastClientLabel() + ")" : juce::String());
        return s + "\nPipe:   " + mcp::pipeName() + " (per user)";
    };
    helpWindow = std::make_unique<ToolWindow> ("MCP setup", new McpSetupPanel (app, mcp, status), [this]
    {
        juce::MessageManager::callAsync ([this] { helpWindow.reset(); });
    });
    helpWindow->centreAroundComponent (this, 780, 600);
    helpWindow->setVisible (true);
}

void MainComponent::showAbout()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
        .withIconType (juce::MessageBoxIconType::InfoIcon)
        .withTitle ("About MCP DAW")
        .withMessage (juce::String ("MCP DAW by Make Tech  -  version ") + MCPDAW_VERSION_STRING + "\n"
                      "A loop and one-shot arranger with first-class AI (MCP) control.\n\n"
                      "Built by Make Tech Pty Ltd\n"
                      + juce::String (links::website).fromFirstOccurrenceOf ("//", false, false) + "\n\n"
                      "Free and open source. Feedback: " + links::feedbackEmail + "\n"
                      "If it's useful to you, the heart menu has a donate link.\n\n"
                      "Source code (GNU AGPL v3): " + juce::String (links::sourceUrl).fromFirstOccurrenceOf ("//", false, false) + "\n"
                      "Uses JUCE (AGPLv3). Font: Audiowide (SIL Open Font License).")
        .withButton ("Donate...")
        .withButton ("maketech.com.au")
        .withButton ("Close"),
        [] (int result)   // JUCE: first button = 1, second = 2, last = 0
        {
            if (result == 1) juce::URL (links::donateUrl).launchInDefaultBrowser();
            if (result == 2) juce::URL (links::website).launchInDefaultBrowser();
        });
}

// ================================================================================================
// Commands
// ================================================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c)
{
    c.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdImport, cmdRender, cmdAudioSettings,
                  cmdUndo, cmdRedo, cmdCut, cmdCopy, cmdPaste, cmdDelete, cmdSplit, cmdDuplicate, cmdSelectAll, cmdToggleLooping,
                  cmdLoopToSelection, cmdNudgeLeft, cmdNudgeRight, cmdAddTrack,
                  cmdPlayStop, cmdStop, cmdRewind, cmdToggleLoop, cmdGoToEnd,
                  cmdZoomIn, cmdZoomOut, cmdZoomFit, cmdFollow, cmdShowBrowser, cmdShowFx, cmdShowMixer, cmdDetachBrowser, cmdDetachFx, cmdDetachMixer,
                  cmdMcpHelp });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto ctrl = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier;
    const bool sel = timeline.hasSelection();
    switch (id)
    {
        case cmdNew:      info.setInfo ("New project", "", "File", 0); info.addDefaultKeypress ('n', ctrl); break;
        case cmdOpen:     info.setInfo ("Open project...", "", "File", 0); info.addDefaultKeypress ('o', ctrl); break;
        case cmdSave:     info.setInfo ("Save", "", "File", 0); info.addDefaultKeypress ('s', ctrl); break;
        case cmdSaveAs:   info.setInfo ("Save as...", "", "File", 0); info.addDefaultKeypress ('s', ctrl | shift); break;
        case cmdImport:   info.setInfo ("Import audio...", "", "File", 0); info.addDefaultKeypress ('i', ctrl); break;
        case cmdRender:   info.setInfo ("Render mix to WAV...", "", "File", 0); info.addDefaultKeypress ('r', ctrl); break;
        case cmdAudioSettings: info.setInfo ("Audio settings...", "", "File", 0); break;

        case cmdUndo:
            info.setInfo (app.controller.canUndo() ? "Undo " + app.controller.undoLabel() : juce::String ("Undo"), "", "Edit", 0);
            info.setActive (app.controller.canUndo());
            info.addDefaultKeypress ('z', ctrl);
            break;
        case cmdRedo:
            info.setInfo (app.controller.canRedo() ? "Redo " + app.controller.redoLabel() : juce::String ("Redo"), "", "Edit", 0);
            info.setActive (app.controller.canRedo());
            info.addDefaultKeypress ('y', ctrl);
            info.addDefaultKeypress ('z', ctrl | shift);
            break;
        case cmdCut:      info.setInfo ("Cut", "", "Edit", 0); info.setActive (sel); info.addDefaultKeypress ('x', ctrl); break;
        case cmdCopy:     info.setInfo ("Copy", "", "Edit", 0); info.setActive (sel); info.addDefaultKeypress ('c', ctrl); break;
        case cmdPaste:    info.setInfo ("Paste at cursor", "", "Edit", 0); info.setActive (timeline.canPaste()); info.addDefaultKeypress ('v', ctrl); break;
        case cmdDelete:   info.setInfo ("Delete", "", "Edit", 0); info.setActive (sel);
                          info.addDefaultKeypress (KP::deleteKey, 0); info.addDefaultKeypress (KP::backspaceKey, 0); break;
        case cmdSplit:    info.setInfo ("Split at cursor", "", "Edit", 0); info.addDefaultKeypress ('s', 0); break;
        case cmdDuplicate: info.setInfo ("Duplicate", "", "Edit", 0); info.setActive (sel); info.addDefaultKeypress ('d', ctrl); break;
        case cmdSelectAll: info.setInfo ("Select all events", "", "Edit", 0); info.addDefaultKeypress ('a', ctrl); break;
        case cmdToggleLooping: info.setInfo ("Toggle event looping", "", "Edit", 0); info.setActive (sel); info.addDefaultKeypress ('j', ctrl); break;
        case cmdLoopToSelection: info.setInfo ("Loop region to selection", "", "Edit", 0); info.setActive (sel); info.addDefaultKeypress ('l', ctrl); break;
        case cmdNudgeLeft:  info.setInfo ("Nudge left", "", "Edit", 0); info.setActive (sel); info.addDefaultKeypress (KP::leftKey, 0); break;
        case cmdNudgeRight: info.setInfo ("Nudge right", "", "Edit", 0); info.setActive (sel); info.addDefaultKeypress (KP::rightKey, 0); break;
        case cmdAddTrack: info.setInfo ("Add track", "", "Edit", 0); info.addDefaultKeypress ('t', ctrl); break;

        case cmdPlayStop: info.setInfo (app.engine.isPlaying() ? "Stop" : "Play", "", "Transport", 0); info.addDefaultKeypress (KP::spaceKey, 0); break;
        case cmdStop:     info.setInfo ("Pause (stay here)", "", "Transport", 0); info.addDefaultKeypress (KP::returnKey, 0); break;
        case cmdRewind:   info.setInfo ("Go to start", "", "Transport", 0); info.addDefaultKeypress (KP::homeKey, 0); break;
        case cmdGoToEnd:  info.setInfo ("Go to end", "", "Transport", 0); info.addDefaultKeypress (KP::endKey, 0); break;
        case cmdToggleLoop: info.setInfo ("Loop playback", "", "Transport", 0); info.setTicked (app.project().loopEnabled); info.addDefaultKeypress ('l', 0); break;

        case cmdZoomIn:   info.setInfo ("Zoom in", "", "View", 0); info.addDefaultKeypress (KP::upKey, 0); info.addDefaultKeypress ('=', 0); break;
        case cmdZoomOut:  info.setInfo ("Zoom out", "", "View", 0); info.addDefaultKeypress (KP::downKey, 0); info.addDefaultKeypress ('-', 0); break;
        case cmdZoomFit:  info.setInfo ("Zoom to fit", "", "View", 0); info.addDefaultKeypress ('f', 0); break;
        case cmdFollow:   info.setInfo ("Follow playhead", "", "View", 0); info.setTicked (timeline.followPlayhead); break;
        case cmdShowBrowser: info.setInfo ("Show browser", "", "View", 0); info.setTicked (workspace.isPanelVisible (browserPanel)); info.addDefaultKeypress ('1', ctrl); break;
        case cmdShowFx:      info.setInfo ("Show FX rack", "", "View", 0); info.setTicked (workspace.isPanelVisible (fxPanel)); info.addDefaultKeypress ('2', ctrl); break;
        case cmdShowMixer:   info.setInfo ("Show mixer", "", "View", 0); info.setTicked (workspace.isPanelVisible (mixerPanel)); info.addDefaultKeypress ('3', ctrl); break;
        case cmdDetachBrowser: info.setInfo ("Browser in own window", "", "View", 0); info.setTicked (browserPanel.isDetached()); break;
        case cmdDetachFx:      info.setInfo ("FX rack in own window", "", "View", 0); info.setTicked (fxPanel.isDetached()); break;
        case cmdDetachMixer:   info.setInfo ("Mixer in own window", "", "View", 0); info.setTicked (mixerPanel.isDetached()); break;
        case cmdMcpHelp:  info.setInfo ("MCP / AI control setup...", "", "Help", 0); break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdNew:
            confirmDiscard ([this]
            {
                app.run ("new_project", makeObject ({ { "discardChanges", true } }));
                app.selectEvents ({});
                app.selectTrack ({});
            });
            break;
        case cmdOpen:
            confirmDiscard ([this] { chooseFile ("Open project", "*.mcpdaw", false, [this] (const juce::File& f) { openProject (f); }); });
            break;
        case cmdSave:     save(); break;
        case cmdSaveAs:   saveAs(); break;
        case cmdImport:   importAudio(); break;
        case cmdRender:   renderMix(); break;
        case cmdAudioSettings: showAudioSettings(); break;
        case cmdUndo:     app.run ("undo"); break;
        case cmdRedo:     app.run ("redo"); break;
        case cmdCut:      timeline.cutSelection(); break;
        case cmdCopy:     timeline.copySelection(); break;
        case cmdPaste:    timeline.paste(); break;
        case cmdDelete:   timeline.deleteSelection(); break;
        case cmdSplit:    timeline.splitAtCursor(); break;
        case cmdDuplicate: timeline.duplicateSelection(); break;
        case cmdSelectAll: timeline.selectAll(); break;
        case cmdToggleLooping: timeline.toggleSelectionLooping(); break;
        case cmdLoopToSelection: timeline.loopToSelection(); break;
        case cmdNudgeLeft:  timeline.nudgeSelection (-1); break;
        case cmdNudgeRight: timeline.nudgeSelection (1); break;
        case cmdAddTrack:
        {
            const auto r = app.run ("create_track");
            if (r.isObject()) app.selectTrack (r["trackId"].toString());
            break;
        }
        case cmdPlayStop: app.run (app.engine.isPlaying() ? "stop" : "play"); break;
        case cmdStop:     app.run ("pause"); break;
        case cmdRewind:   app.run ("seek", makeObject ({ { "beat", 0 } })); break;
        case cmdGoToEnd:  app.run ("seek", makeObject ({ { "beat", ticksToBeats (app.project().contentEnd()) } })); break;
        case cmdToggleLoop: app.run ("set_loop_region", makeObject ({ { "enabled", ! app.project().loopEnabled } })); break;
        case cmdZoomIn:   timeline.zoomBy (1.25); break;
        case cmdZoomOut:  timeline.zoomBy (0.8); break;
        case cmdZoomFit:  timeline.zoomToFit(); break;
        case cmdFollow:   timeline.followPlayhead = ! timeline.followPlayhead; break;
        case cmdShowBrowser: workspace.setPanelVisible (browserPanel, ! workspace.isPanelVisible (browserPanel)); break;
        case cmdShowFx:      workspace.setPanelVisible (fxPanel, ! workspace.isPanelVisible (fxPanel)); break;
        case cmdShowMixer:   workspace.setPanelVisible (mixerPanel, ! workspace.isPanelVisible (mixerPanel)); break;
        case cmdDetachBrowser: workspace.toggleDetached (browserPanel); break;
        case cmdDetachFx:      workspace.toggleDetached (fxPanel); break;
        case cmdDetachMixer:   workspace.toggleDetached (mixerPanel); break;
        case cmdMcpHelp:  showMcpHelp(); break;
        default: return false;
    }
    commands.commandStatusChanged();
    return true;
}

juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "Transport", "View", "Help", kHeartMenuName }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto add = [&] (std::initializer_list<int> ids) { for (int id : ids) { if (id == 0) m.addSeparator(); else m.addCommandItem (&commands, id); } };
    switch (index)
    {
        case 0: add ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, 0, cmdImport, cmdRender, 0, cmdAudioSettings }); break;
        case 1: add ({ cmdUndo, cmdRedo, 0, cmdCut, cmdCopy, cmdPaste, cmdDelete, 0, cmdSplit, cmdDuplicate, cmdToggleLooping, cmdSelectAll,
                       0, cmdLoopToSelection, cmdNudgeLeft, cmdNudgeRight, 0, cmdAddTrack }); break;
        case 2: add ({ cmdPlayStop, cmdStop, cmdRewind, cmdGoToEnd, 0, cmdToggleLoop }); break;
        case 3: add ({ cmdZoomIn, cmdZoomOut, cmdZoomFit, cmdFollow, 0, cmdShowBrowser, cmdShowFx, cmdShowMixer, 0, cmdDetachBrowser, cmdDetachFx, cmdDetachMixer }); break;
        case 4:
            add ({ cmdMcpHelp });
            m.addSeparator();
            m.addItem (kSourceItem, "Source code on GitHub...");
            m.addItem (kAboutItem, "About MCP DAW...");
            break;
        case 5:
            m.addItem (kFeedbackItem, "Send feedback (email)...");
            m.addItem (kDonateItem, "Donate via PayPal...");
            m.addSeparator();
            m.addItem (kCopyPaypalItem, juce::String ("Copy PayPal address (") + links::paypalAddress + ")");
            break;
        default: break;
    }
    return m;
}

void MainComponent::menuItemSelected (int itemId, int)
{
    switch (itemId)
    {
        case kAboutItem: showAbout(); break;
        case kSourceItem: juce::URL (links::sourceUrl).launchInDefaultBrowser(); break;
        case kFeedbackItem:
            juce::URL (juce::String ("mailto:") + links::feedbackEmail + "?subject="
                       + juce::URL::addEscapeChars ("MCP DAW " MCPDAW_VERSION_STRING " feedback", true)).launchInDefaultBrowser();
            break;
        case kDonateItem: juce::URL (links::donateUrl).launchInDefaultBrowser(); break;
        case kCopyPaypalItem:
            juce::SystemClipboard::copyTextToClipboard (links::paypalAddress);
            app.status (juce::String ("Copied ") + links::paypalAddress + " - thank you for supporting MCP DAW");
            break;
        default: break;
    }
}
} // namespace mcpdaw::ui
