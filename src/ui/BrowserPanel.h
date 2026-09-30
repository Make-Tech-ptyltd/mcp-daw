#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Audio file browser: folder navigation, favourites/recent folders, click-to-audition and drag to
// the timeline. Directory listing is synchronous (fast for normal sample folders); decoding for
// preview happens only for the file you click.

#include "AppContext.h"
#include "Theme.h"

namespace mcpdaw::ui
{
class BrowserPanel final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit BrowserPanel (AppContext&);
    ~BrowserPanel() override;

    void navigate (const juce::File& folder);
    /** Called on double-click of an audio file (the main component places it at the cursor). */
    std::function<void (const juce::File&)> onAddFile;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    AppContext& app;
    juce::File folder;
    juce::Array<juce::File> entries;
    juce::TextEditor pathField;
    IconButton upButton { "Up", Icon::Up, "Parent folder" };
    juce::TextButton placesButton { "Places" };
    juce::ToggleButton autoPreview { "Preview" };
    juce::ListBox list { "Files", this };
    juce::StringArray favourites, recents;

    void loadPlaces();
    void savePlaces();
    void showPlacesMenu();
    void preview (const juce::File&);

    int getNumRows() override { return entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
};
} // namespace mcpdaw::ui
