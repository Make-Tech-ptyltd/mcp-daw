// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "BrowserPanel.h"
#include "core/AudioAsset.h"

namespace mcpdaw::ui
{
namespace
{
/** Samples-Starter-Kit next to the app (release zip and build folder alike). */
juce::File starterKitFolder()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("Samples-Starter-Kit");
}
} // namespace

BrowserPanel::BrowserPanel (AppContext& a) : app (a)
{
    addAndMakeVisible (pathField);
    addAndMakeVisible (upButton);
    addAndMakeVisible (placesButton);
    addAndMakeVisible (autoPreview);
    addAndMakeVisible (list);
    pathField.setFont (fonts::ui (12.5f));
    pathField.onReturnKey = [this] { navigate (juce::File (pathField.getText().trim())); };
    upButton.onClick = [this] { navigate (folder.getParentDirectory()); };
    placesButton.onClick = [this] { showPlacesMenu(); };
    placesButton.setTooltip ("Favourite and recent folders");
    autoPreview.setToggleState (true, juce::dontSendNotification);
    autoPreview.setTooltip ("Audition files when clicked");
    autoPreview.onClick = [this] { if (! autoPreview.getToggleState()) app.engine.stopPreview(); };
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, colours::panel);
    list.setMultipleSelectionEnabled (false);

    loadPlaces();
    auto* props = app.properties.getUserSettings();
    juce::File start (props != nullptr ? props->getValue ("browser.folder") : juce::String());
    if (! start.isDirectory()) start = starterKitFolder();      // first run: the samples that ship with the app
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    navigate (start);
}

BrowserPanel::~BrowserPanel()
{
    app.engine.stopPreview();
    savePlaces();
}

void BrowserPanel::loadPlaces()
{
    if (auto* props = app.properties.getUserSettings())
    {
        favourites.addTokens (props->getValue ("browser.favourites"), "|", "");
        recents.addTokens (props->getValue ("browser.recent"), "|", "");
        favourites.removeEmptyStrings();
        recents.removeEmptyStrings();
    }
}

void BrowserPanel::savePlaces()
{
    if (auto* props = app.properties.getUserSettings())
    {
        props->setValue ("browser.favourites", favourites.joinIntoString ("|"));
        props->setValue ("browser.recent", recents.joinIntoString ("|"));
        props->setValue ("browser.folder", folder.getFullPathName());
    }
}

void BrowserPanel::navigate (const juce::File& f)
{
    if (! f.isDirectory()) { app.status ("Not a folder: " + f.getFullPathName(), true); return; }
    folder = f;
    app.browserFolder = f;
    entries.clear();
    for (const auto& entry : juce::RangedDirectoryIterator (f, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
    {
        const auto& file = entry.getFile();
        if (entry.isDirectory() || isSupportedAudioFile (file)) entries.add (file);
    }
    std::sort (entries.begin(), entries.end(), [] (const juce::File& a, const juce::File& b)
    {
        if (a.isDirectory() != b.isDirectory()) return a.isDirectory();
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });
    pathField.setText (f.getFullPathName(), false);
    recents.removeString (f.getFullPathName());
    recents.insert (0, f.getFullPathName());
    while (recents.size() > 10) recents.remove (recents.size() - 1);
    list.updateContent();
    list.deselectAllRows();
    list.scrollToEnsureRowIsOnscreen (0);
    list.repaint();
    savePlaces();
}

void BrowserPanel::showPlacesMenu()
{
    juce::PopupMenu m, recentMenu, favMenu;
    struct Place { const char* name; juce::File::SpecialLocationType type; };
    const Place places[] = { { "Music", juce::File::userMusicDirectory }, { "Desktop", juce::File::userDesktopDirectory },
                             { "Documents", juce::File::userDocumentsDirectory }, { "Home", juce::File::userHomeDirectory } };
    juce::Array<juce::File> targets;
    auto add = [&] (juce::PopupMenu& menu, const juce::String& name, const juce::File& f)
    {
        targets.add (f);
        menu.addItem (targets.size(), name, f.isDirectory());
    };
    for (const auto& p : places) add (m, p.name, juce::File::getSpecialLocation (p.type));
    add (m, "Downloads", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"));
    if (const auto kit = starterKitFolder(); kit.isDirectory()) add (m, "Samples Starter Kit", kit);
    if (auto f = app.controller.currentFile(); f != juce::File()) add (m, "Project folder", f.getParentDirectory());
    for (const auto& fav : favourites) add (favMenu, juce::File (fav).getFileName(), juce::File (fav));
    for (const auto& r : recents) add (recentMenu, r, juce::File (r));
    m.addSeparator();
    m.addSubMenu ("Favourites", favMenu, ! favourites.isEmpty());
    m.addSubMenu ("Recent", recentMenu, ! recents.isEmpty());
    m.addSeparator();
    const bool isFav = favourites.contains (folder.getFullPathName());
    m.addItem (1000, isFav ? "Remove this folder from favourites" : "Add this folder to favourites");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&placesButton),
                     [safe = juce::Component::SafePointer<BrowserPanel> (this), targets, isFav] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1000)
        {
            if (isFav) safe->favourites.removeString (safe->folder.getFullPathName());
            else safe->favourites.addIfNotAlreadyThere (safe->folder.getFullPathName());
            safe->savePlaces();
        }
        else if (r - 1 < targets.size()) safe->navigate (targets[r - 1]);
    });
}

void BrowserPanel::preview (const juce::File& f)
{
    if (! autoPreview.getToggleState()) return;
    app.previewFile (f);
}

void BrowserPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= entries.size()) return;
    const auto& f = entries.getReference (row);
    if (selected)
    {
        g.setColour (colours::accentDim.withAlpha (0.45f));
        g.fillRect (0, 0, w, h);
    }
    const bool dir = f.isDirectory();
    drawIcon (g, dir ? Icon::Folder : Icon::Wave, juce::Rectangle<float> (6, 3, static_cast<float> (h - 6), static_cast<float> (h - 6)),
              dir ? colours::textFaint : colours::accent);
    g.setColour (dir ? colours::textDim : colours::text);
    g.setFont (fonts::ui (13.0f, dir));
    auto text = juce::Rectangle<int> (h + 6, 0, w - h - 12, h);
    if (! dir)
    {
        const double bpm = parseBpmFromName (f.getFileNameWithoutExtension());
        g.setColour (colours::textFaint);
        g.setFont (fonts::ui (11.0f));
        const auto info = (bpm > 0 ? juce::String (bpm, 0) + " bpm   " : juce::String()) + juce::File::descriptionOfSizeInBytes (f.getSize());
        g.drawText (info, text.removeFromRight (110), juce::Justification::centredRight);
        g.setColour (colours::text);
        g.setFont (fonts::ui (13.0f));
    }
    g.drawText (f.getFileName(), text, juce::Justification::centredLeft, true);
}

void BrowserPanel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < entries.size() && ! entries[row].isDirectory()) preview (entries[row]);
}

void BrowserPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { returnKeyPressed (row); }

void BrowserPanel::returnKeyPressed (int row)
{
    if (row < 0 || row >= entries.size()) return;
    const auto f = entries[row];
    if (f.isDirectory()) navigate (f);
    else
    {
        app.engine.stopPreview();
        if (onAddFile) onAddFile (f);
    }
}

juce::var BrowserPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty()) return {};
    const int row = rows[0];
    if (row < 0 || row >= entries.size() || entries[row].isDirectory()) return {};
    app.engine.stopPreview();
    return "file:" + entries[row].getFullPathName();
}

void BrowserPanel::paint (juce::Graphics& g) { g.fillAll (colours::panel); }

void BrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto top = r.removeFromTop (26);
    upButton.setBounds (top.removeFromLeft (26));
    top.removeFromLeft (4);
    placesButton.setBounds (top.removeFromRight (64));
    top.removeFromRight (4);
    pathField.setBounds (top);
    r.removeFromTop (4);
    autoPreview.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);
    list.setBounds (r);
}
} // namespace mcpdaw::ui
