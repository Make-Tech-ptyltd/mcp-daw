# MCP DAW — Make Tech Pty Ltd
#
# Target layout (why it is split this way):
#   mcpdaw_juce      static lib: the JUCE modules compiled ONCE and shared by every executable.
#   mcpdaw_core      static lib: project model, commands/undo, DSP, realtime engine, MCP protocol.
#                  No GUI code, so it is fully exercisable by the headless test runner.
#   MCPDAW       the GUI workstation (links mcpdaw_core + UI sources).
#   mcpdaw_tests     headless unit/DSP/engine/MCP tests, registered with CTest.
#   mcp-daw-bridge   tiny stdio <-> named-pipe bridge an MCP client launches (lazy: see src/mcp/Bridge.h).
#   mcpdaw_manifest  build-time generator of the tool list the bridge serves while MCP DAW is closed.
cmake_minimum_required(VERSION 3.25)
project(MCPDAW VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# Windows: link the C++ runtime statically, so MCP-DAW.exe runs without the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------------------------
# JUCE (pinned). Prefer a local checkout (offline, reproducible); fall back to FetchContent.
# ---------------------------------------------------------------------------------------------
set(MCPDAW_JUCE_TAG 91ad83ae34a81e0833b1a2b0866f54846370ae53) # JUCE 8.0.15
set(JUCE_PATH "" CACHE PATH "Path to a JUCE 8.0.15 checkout (leave empty to download)")
option(MCPDAW_BUILD_TESTS "Build the headless test runner" ON)
option(MCPDAW_BUILD_APP "Build the GUI application" ON)

if(JUCE_PATH)
    add_subdirectory("${JUCE_PATH}" juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${MCPDAW_JUCE_TAG})
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------------------------
# mcpdaw_juce — JUCE modules compiled once. This is the pattern recommended by the JUCE team for
# sharing modules between several executables: link the module targets PRIVATELY and re-export
# their include directories/definitions through the INTERFACE.
# ---------------------------------------------------------------------------------------------
add_library(mcpdaw_juce STATIC)
target_link_libraries(mcpdaw_juce
    PRIVATE
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

target_compile_definitions(mcpdaw_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        # Audio: the operating system's own audio only - WASAPI (+DirectSound) on Windows, ALSA on
        # Linux. No ASIO or JACK: nothing extra to install, and the same behaviour everywhere.
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_ASIO=0
        JUCE_JACK=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
    INTERFACE
        $<TARGET_PROPERTY:mcpdaw_juce,COMPILE_DEFINITIONS>)
target_include_directories(mcpdaw_juce INTERFACE $<TARGET_PROPERTY:mcpdaw_juce,INCLUDE_DIRECTORIES>)
set_target_properties(mcpdaw_juce PROPERTIES POSITION_INDEPENDENT_CODE TRUE FOLDER "deps")

# Embedded assets (Audiowide display font, OFL licensed).
juce_add_binary_data(mcpdaw_assets SOURCES assets/Audiowide-Regular.ttf)
set_target_properties(mcpdaw_assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE FOLDER "deps")

# Project-wide warning policy for OUR code (JUCE sources keep JUCE's own flags).
add_library(mcpdaw_warnings INTERFACE)
if(MSVC)
    target_compile_options(mcpdaw_warnings INTERFACE /W4 /permissive- /utf-8 /wd4244 /wd4267)
else()
    target_compile_options(mcpdaw_warnings INTERFACE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------------------------
# mcpdaw_core — everything that is not GUI.
# ---------------------------------------------------------------------------------------------
add_library(mcpdaw_core STATIC
    src/core/MusicalTime.h
    src/core/Fades.h
    src/core/Project.h                src/core/Project.cpp
    src/core/WavLoopMetadata.h        src/core/WavLoopMetadata.cpp
    src/core/AudioAsset.h             src/core/AudioAsset.cpp
    src/core/EffectCatalogue.h        src/core/EffectCatalogue.cpp
    src/core/ProjectJson.h            src/core/ProjectJson.cpp
    src/core/JsonArgs.h               src/core/JsonArgs.cpp
    src/core/ProjectController.h      src/core/ProjectController.cpp
    src/core/Actions.h                src/core/Actions.cpp
    src/dsp/EffectProcessor.h         src/dsp/EffectProcessor.cpp
    src/dsp/Biquad.h
    src/dsp/ParametricEq.h            src/dsp/ParametricEq.cpp
    src/dsp/Compressor.h              src/dsp/Compressor.cpp
    src/dsp/Reverb.h                  src/dsp/Reverb.cpp
    src/dsp/MultiTapDelay.h           src/dsp/MultiTapDelay.cpp
    src/dsp/Distortion.h              src/dsp/Distortion.cpp
    src/engine/TimeStretch.h
    src/engine/PlaybackPlan.h         src/engine/PlaybackPlan.cpp
    src/engine/PlanRenderer.h         src/engine/PlanRenderer.cpp
    src/engine/AudioEngine.h          src/engine/AudioEngine.cpp
    src/engine/OfflineRenderer.h      src/engine/OfflineRenderer.cpp
    src/mcp/PipeName.h
    src/mcp/NamedLock.h               src/mcp/NamedLock.cpp
    src/mcp/McpManifest.h
    src/mcp/CommandFeed.h
    src/mcp/McpSession.h              src/mcp/McpSession.cpp
    src/mcp/McpPipeServer.h           src/mcp/McpPipeServer.cpp
    src/mcp/Bridge.h                  src/mcp/Bridge.cpp
    src/mcp/ScriptPlayer.h            src/mcp/ScriptPlayer.cpp)
target_include_directories(mcpdaw_core PUBLIC src)
target_link_libraries(mcpdaw_core PUBLIC mcpdaw_juce PRIVATE mcpdaw_warnings)
# One version string for the app title, About box, MCP serverInfo and feedback emails.
target_compile_definitions(mcpdaw_core PUBLIC MCPDAW_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(mcpdaw_core PROPERTIES FOLDER "mcpdaw")

# ---------------------------------------------------------------------------------------------
# MCPDAW — GUI application
# ---------------------------------------------------------------------------------------------
if(MCPDAW_BUILD_APP)
juce_add_gui_app(MCPDAW
    PRODUCT_NAME "MCP-DAW"
    COMPANY_NAME "Make Tech Pty Ltd"
    COMPANY_WEBSITE "https://maketech.com.au"
    BUNDLE_ID "au.com.maketech.mcpdaw"
    ICON_BIG "assets/icon_big.png"        # large sizes (256 px): bars + wordmark
    ICON_SMALL "assets/icon_small.png"    # 16-48 px: bars only (tools/make_icon.py redraws both)
    VERSION ${PROJECT_VERSION})
# JUCE turns the PNGs into the Windows .ico only when CMake configures, so re-configure when they
# change (otherwise a rebuild keeps the old icon).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_big.png" "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_small.png")
target_sources(MCPDAW PRIVATE
    src/app/Main.cpp
    src/ui/AppContext.h
    src/ui/Theme.h                    src/ui/Theme.cpp
    src/ui/Panel.h                    src/ui/Panel.cpp
    src/ui/TransportBar.h             src/ui/TransportBar.cpp
    src/ui/CommandTicker.h            src/ui/CommandTicker.cpp
    src/ui/TimelineView.h             src/ui/TimelineView.cpp
    src/ui/MixerPanel.h               src/ui/MixerPanel.cpp
    src/ui/FxRackPanel.h              src/ui/FxRackPanel.cpp
    src/ui/BrowserPanel.h             src/ui/BrowserPanel.cpp
    src/ui/EditingDialogs.h           src/ui/EditingDialogs.cpp
    src/ui/MainComponent.h            src/ui/MainComponent.cpp)
target_link_libraries(MCPDAW PRIVATE mcpdaw_core mcpdaw_assets mcpdaw_warnings)
set_target_properties(MCPDAW PROPERTIES FOLDER "mcpdaw")
# The one-click demo (Run Demo.cmd + script + samples) and the Samples Starter Kit go next to the app,
# as in the release zip (the browser opens the kit on first run).
add_custom_command(TARGET MCPDAW POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/demo" "$<TARGET_FILE_DIR:MCPDAW>/Demo"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Samples-Starter-Kit"
            "$<TARGET_FILE_DIR:MCPDAW>/Samples-Starter-Kit"
    VERBATIM)
endif()

# ---------------------------------------------------------------------------------------------
# MCP manifest — generated at build time from the app's own action registry, so the bridge can
# answer initialize / tools/list without MCP DAW running (and can never drift from the app).
# The generator only rewrites the .cpp when the content changes (Ninja restats the byproduct).
# ---------------------------------------------------------------------------------------------
juce_add_console_app(mcpdaw_manifest PRODUCT_NAME "mcpdaw_manifest")
target_sources(mcpdaw_manifest PRIVATE tools/mcp-bridge/ManifestGen.cpp)
target_link_libraries(mcpdaw_manifest PRIVATE mcpdaw_core mcpdaw_warnings)
set_target_properties(mcpdaw_manifest PROPERTIES FOLDER "mcpdaw")
set(MCPDAW_MANIFEST_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/McpManifest.cpp")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/manifest.stamp"
    BYPRODUCTS "${MCPDAW_MANIFEST_CPP}"
    COMMAND mcpdaw_manifest "${MCPDAW_MANIFEST_CPP}"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/generated/manifest.stamp"
    DEPENDS mcpdaw_manifest
    COMMENT "Generating the MCP manifest (tool list) for mcp-daw-bridge"
    VERBATIM)
add_custom_target(mcpdaw_manifest_gen DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/manifest.stamp")
set_target_properties(mcpdaw_manifest_gen PROPERTIES FOLDER "mcpdaw")

# ---------------------------------------------------------------------------------------------
# mcp-daw-bridge — stdio bridge (deliberately tiny: only juce_core + juce_events)
# ---------------------------------------------------------------------------------------------
# PRODUCT_NAME is what Task Manager shows ("MCP DAW Bridge"); the file stays mcp-daw-bridge.exe.
juce_add_console_app(mcpdaw_mcp_bridge PRODUCT_NAME "MCP DAW Bridge" COMPANY_NAME "Make Tech Pty Ltd"
    BUNDLE_ID "au.com.maketech.mcpdaw.bridge")
target_sources(mcpdaw_mcp_bridge PRIVATE tools/mcp-bridge/BridgeMain.cpp src/mcp/PipeName.h
    src/mcp/NamedLock.h src/mcp/NamedLock.cpp src/mcp/Bridge.h src/mcp/Bridge.cpp src/mcp/ScriptPlayer.h src/mcp/ScriptPlayer.cpp
    src/mcp/McpManifest.h "${MCPDAW_MANIFEST_CPP}")
set_source_files_properties("${MCPDAW_MANIFEST_CPP}" PROPERTIES GENERATED TRUE)
add_dependencies(mcpdaw_mcp_bridge mcpdaw_manifest_gen)
target_include_directories(mcpdaw_mcp_bridge PRIVATE src)
target_compile_definitions(mcpdaw_mcp_bridge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(mcpdaw_mcp_bridge PRIVATE juce::juce_events juce::juce_recommended_config_flags mcpdaw_warnings)
set_target_properties(mcpdaw_mcp_bridge PROPERTIES FOLDER "mcpdaw" OUTPUT_NAME "mcp-daw-bridge")
if(MCPDAW_BUILD_APP)
    # Ship the bridge next to MCP-DAW(.exe) so the app can show its path and the bridge can find the app.
    add_custom_command(TARGET mcpdaw_mcp_bridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DSRC=$<TARGET_FILE:mcpdaw_mcp_bridge> -DDST_DIR=$<TARGET_FILE_DIR:MCPDAW>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyBridge.cmake"
        VERBATIM)
    add_dependencies(MCPDAW mcpdaw_mcp_bridge)
endif()

# ---------------------------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------------------------
if(MCPDAW_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(mcpdaw_tests PRODUCT_NAME "mcpdaw_tests")
    target_sources(mcpdaw_tests PRIVATE
        tests/TestMain.cpp
        tests/TestSupport.h
        tests/CoreTests.cpp
        tests/CommandTests.cpp
        tests/DspTests.cpp
        tests/EngineTests.cpp
        tests/McpTests.cpp
        tests/BridgeTests.cpp
        tests/ScriptTests.cpp
        tests/EditingTests.cpp
        tests/WavMetadataTests.cpp
        "${MCPDAW_MANIFEST_CPP}")
    add_dependencies(mcpdaw_tests mcpdaw_manifest_gen)
    target_link_libraries(mcpdaw_tests PRIVATE mcpdaw_core mcpdaw_warnings)
    set_target_properties(mcpdaw_tests PROPERTIES FOLDER "mcpdaw")
    add_test(NAME mcpdaw_tests COMMAND mcpdaw_tests)
endif()
