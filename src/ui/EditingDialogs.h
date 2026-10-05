#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Small editing dialogs:
//   * EditingPreferencesPanel - the "Default declick time" preference (the minimum length of every
//     event fade; see core/Fades.h).
//   * LoopMetadataPanel - "Embed Loop Metadata...": writes ACID-compatible loop metadata into a WAV
//     file through the write_wav_loop_metadata action (the same one MCP clients call), never touching
//     the audio data.

#include "AppContext.h"
#include "Theme.h"

namespace mcpdaw::ui
{
class EditingPreferencesPanel final : public juce::Component, private ProjectController::Listener
{
public:
    explicit EditingPreferencesPanel (AppContext&);
    ~EditingPreferencesPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& app;
    juce::Label title, label, help;
    juce::Slider declick;
    juce::TextButton resetButton { "Default (1.5 ms)" };
    void projectChanged (const Project&, bool) override {}
    void editingPreferencesChanged() override;
};

class LoopMetadataPanel final : public juce::Component
{
public:
    LoopMetadataPanel (AppContext&, const juce::File& wav);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& app;
    juce::File file;
    double actualSeconds = 0.0;
    bool writable = false;

    juce::Label heading, fileLabel, note;
    juce::Label typeLabel, lengthLabel, beatsUnit, tempoLabel, bpmUnit, meterLabel, slash, rootLabel;
    juce::ComboBox typeBox, denominatorBox, rootBox;
    juce::TextEditor beatsField, tempoField, numeratorField;
    juce::ToggleButton stretchToggle { "Stretch (the host may tempo-match this loop)" };
    juce::TextButton tempoFromLength { "Tempo from length" };
    juce::Label calculated, actual, discrepancy;
    juce::TextButton saveCopyButton { "Save Copy..." }, updateButton { "Update WAV" };
    std::unique_ptr<juce::FileChooser> chooser;

    void loadFromFile();
    void updateChecks();
    juce::var metadataArgs() const;
    void write (const juce::File& destination);
    bool isLoop() const { return typeBox.getSelectedId() == 1; }
};
} // namespace mcpdaw::ui
