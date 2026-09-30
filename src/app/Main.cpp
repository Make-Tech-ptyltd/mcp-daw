// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// MCP DAW application entry point: wires the model, engine, MCP server and UI together.
//
// Object graph (construction order = dependency order, destruction is the reverse):
//   ApplicationProperties -> AudioDeviceManager -> ProjectController -> ActionRegistry
//   -> AudioEngine (listens to the controller, owns the realtime plan) -> McpPipeServer
//   -> MainWindow / MainComponent (UI)
//
// Command line (useful for automation and CI screenshots):
//   MCP DAW [project.mcpdaw] [--open <project>] [--snapshot <png>] [--no-audio]

#include "engine/OfflineRenderer.h"
#include "mcp/McpPipeServer.h"
#include "ui/MainComponent.h"

namespace mcpdaw
{
inline constexpr const char* kWindowTitle = "MCP DAW by Make Tech";

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (ui::MainComponent* content, juce::PropertiesFile* settings)
        : juce::DocumentWindow (kWindowTitle, ui::colours::window, juce::DocumentWindow::allButtons), props (settings)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, false);
        setResizeLimits (960, 600, 10000, 10000);
        addKeyListener (content->getCommandManager().getKeyMappings());
        content->onTitleChanged = [this] (const juce::String& t) { setName (t); };

        const auto saved = props != nullptr ? props->getValue ("window.main") : juce::String();
        if (saved.isNotEmpty()) restoreWindowStateFromString (saved);
        else centreWithSize (1600, 960);
        setVisible (true);
    }

    ~MainWindow() override
    {
        if (props != nullptr) props->setValue ("window.main", getWindowStateAsString());
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    juce::PropertiesFile* props;
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "MCP DAW"; }
    const juce::String getApplicationVersion() override { return MCPDAW_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        juce::PropertiesFile::Options options;
        options.applicationName = "MCP DAW";
        options.filenameSuffix = ".settings";
        options.folderName = "Make Tech/MCP DAW";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        const auto args = juce::StringArray::fromTokens (commandLine, true);
        for (auto& a : const_cast<juce::StringArray&> (args)) a = a.unquoted();

        // Audio: restore the previous device (Windows Audio/WASAPI on Windows, ALSA on Linux).
        if (! args.contains ("--no-audio"))
        {
            std::unique_ptr<juce::XmlElement> saved;
            if (auto* p = properties.getUserSettings()) saved = p->getXmlValue ("audio.device");
            const auto error = devices.initialise (0, 2, saved.get(), true);
            if (error.isNotEmpty()) juce::Logger::writeToLog ("Audio device: " + error);
        }

        registerCoreActions (registry);
        registerEngineActions (registry);
        registerAppActions (registry, appHooks);
        engine = std::make_unique<AudioEngine> (controller);
        if (! args.contains ("--no-audio")) engine->attach (devices);

        context = std::make_unique<ui::AppContext> (controller, registry, *engine, properties);

        mcp = std::make_unique<McpPipeServer> (registry, controller);
        if (! mcp->start())
        {
            juce::Logger::writeToLog ("MCP pipe unavailable (another instance running?)");
            mcp.reset();
        }
        else if (auto* settings = properties.getUserSettings())
        {
            mcp->setTakeTurns (settings->getBoolValue ("mcp.takeTurns", true));   // default: take turns
        }

        auto* content = new ui::MainComponent (*context, devices, mcp.get());
        appHooks->captureScreenshot = [this] (const juce::File& f) -> juce::var
        {
            if (! saveSnapshot (f)) return {};
            return makeObject ({ { "path", f.getFullPathName() },
                                 { "width", mainComponent->getWidth() }, { "height", mainComponent->getHeight() } });
        };
        appHooks->zoomToFit = [this] (double start, double end) { mainComponent->getTimeline().fitRange (start, end); };
        window = std::make_unique<MainWindow> (content, properties.getUserSettings());
        mainComponent = content;
        content->restoreWorkspace();

        // Project to open: first bare argument, or --open <file>.
        juce::File toOpen;
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--open" && i + 1 < args.size()) toOpen = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]);
            else if (args[i] == "--snapshot" && i + 1 < args.size()) snapshotFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]);
            else if (! args[i].startsWith ("--") && args[i].endsWithIgnoreCase (".mcpdaw")) toOpen = juce::File::getCurrentWorkingDirectory().getChildFile (args[i]);
        }
        if (toOpen.existsAsFile()) content->openProject (toOpen);

        if (snapshotFile != juce::File())
            juce::Timer::callAfterDelay (1500, [this]
            {
                saveSnapshot (snapshotFile);
                quit();
            });
    }

    void shutdown() override
    {
        window.reset();          // UI first: it observes everything below
        mcp.reset();
        context.reset();
        if (engine) engine->detach();
        engine.reset();
        devices.closeAudioDevice();
        properties.saveIfNeeded();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainComponent == nullptr || snapshotFile != juce::File()) { quit(); return; }
        mainComponent->confirmDiscard ([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // Double-clicking a project while MCP DAW runs opens it here.
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (commandLine.unquoted());
        if (mainComponent != nullptr && file.hasFileExtension ("mcpdaw") && file.existsAsFile())
            mainComponent->confirmDiscard ([this, file] { mainComponent->openProject (file); });
        if (window) window->toFront (true);
    }

private:
    ui::MakeTechLookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;

    juce::AudioDeviceManager devices;
    ProjectController controller;
    ActionRegistry registry;
    std::shared_ptr<AppHooks> appHooks = std::make_shared<AppHooks>();
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<ui::AppContext> context;
    std::unique_ptr<McpPipeServer> mcp;
    std::unique_ptr<MainWindow> window;
    ui::MainComponent* mainComponent = nullptr;
    juce::File snapshotFile;

    bool saveSnapshot (const juce::File& file)
    {
        if (mainComponent == nullptr) return false;
        const auto image = mainComponent->snapshot();
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (image, out);
    }
};
} // namespace mcpdaw

START_JUCE_APPLICATION (mcpdaw::Application)
