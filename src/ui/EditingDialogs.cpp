// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "EditingDialogs.h"
#include "core/WavLoopMetadata.h"

namespace mcpdaw::ui
{
namespace
{
void styleLabel (juce::Label& l, const juce::String& text, float size = 13.5f, bool bold = false, juce::Colour colour = colours::text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (fonts::ui (size, bold));
    l.setColour (juce::Label::textColourId, colour);
}

void styleNumberField (juce::TextEditor& t, const juce::String& allowed)
{
    t.setFont (fonts::mono (13.5f));
    t.setInputRestrictions (10, allowed);
    t.setJustification (juce::Justification::centredRight);
    t.setSelectAllWhenFocused (true);
}
} // namespace

// ================================================================================================
// Editing preferences
// ================================================================================================
EditingPreferencesPanel::EditingPreferencesPanel (AppContext& a) : app (a)
{
    styleLabel (title, "EDITING", 15.0f, true);
    title.setFont (fonts::display (15.0f));
    styleLabel (label, "Default declick time");
    styleLabel (help,
                "Event fades never render shorter than this. With the default 1.5 ms, an event whose fade handle is at zero "
                "still gets a tiny anti-click fade at both edges; a longer fade you draw simply replaces it. Set 0 ms if you "
                "need completely hard event edges (e.g. sample-accurate impulses).\n\n"
                "This is an application preference: it is not saved in projects and applies to playback and Render alike.",
                12.5f, false, colours::textDim);
    help.setJustificationType (juce::Justification::topLeft);
    for (auto* c : { &title, &label, &help }) addAndMakeVisible (*c);

    declick.setSliderStyle (juce::Slider::LinearHorizontal);
    declick.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24);
    declick.setRange (0.0, kMaxDeclickMs, 0.1);
    declick.setSkewFactorFromMidPoint (3.0);
    declick.setTextValueSuffix (" ms");
    declick.setValue (app.controller.defaultDeclickMs(), juce::dontSendNotification);
    declick.onValueChange = [this] { app.controller.setDefaultDeclickMs (declick.getValue()); };
    addAndMakeVisible (declick);
    resetButton.onClick = [this] { app.controller.setDefaultDeclickMs (kDefaultDeclickMs); };
    addAndMakeVisible (resetButton);
    app.controller.addListener (this);
    setSize (520, 250);
}

EditingPreferencesPanel::~EditingPreferencesPanel() { app.controller.removeListener (this); }

void EditingPreferencesPanel::editingPreferencesChanged()
{
    declick.setValue (app.controller.defaultDeclickMs(), juce::dontSendNotification);   // also follows MCP changes
}

void EditingPreferencesPanel::paint (juce::Graphics& g) { g.fillAll (colours::panel); }

void EditingPreferencesPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    title.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);
    auto row = r.removeFromTop (28);
    label.setBounds (row.removeFromLeft (160));
    resetButton.setBounds (row.removeFromRight (130));
    row.removeFromRight (8);
    declick.setBounds (row);
    r.removeFromTop (12);
    help.setBounds (r);
}

// ================================================================================================
// Embed Loop Metadata
// ================================================================================================
LoopMetadataPanel::LoopMetadataPanel (AppContext& a, const juce::File& wav) : app (a), file (wav)
{
    styleLabel (heading, "EMBED LOOP METADATA");
    heading.setFont (fonts::display (15.0f));
    styleLabel (fileLabel, file.getFullPathName(), 12.0f, false, colours::textDim);
    fileLabel.setMinimumHorizontalScale (0.6f);
    styleLabel (note, "Writes ACID-compatible loop metadata into a WAV file. The audio data is copied byte for byte and verified.",
                12.0f, false, colours::textDim);
    note.setJustificationType (juce::Justification::topLeft);
    styleLabel (typeLabel, "Type");
    styleLabel (lengthLabel, "Length");
    styleLabel (beatsUnit, "beats", 13.0f, false, colours::textDim);
    styleLabel (tempoLabel, "Tempo");
    styleLabel (bpmUnit, "BPM", 13.0f, false, colours::textDim);
    styleLabel (meterLabel, "Time signature");
    styleLabel (slash, "/");
    slash.setJustificationType (juce::Justification::centred);
    styleLabel (rootLabel, "Root note");
    for (auto* l : { &calculated, &actual }) styleLabel (*l, {}, 13.0f, false, colours::textDim);
    styleLabel (discrepancy, {}, 13.0f, true, colours::amber);
    discrepancy.setJustificationType (juce::Justification::topLeft);

    typeBox.addItem ("Loop", 1);
    typeBox.addItem ("One-shot", 2);
    for (int d : { 1, 2, 4, 8, 16, 32 }) denominatorBox.addItem (juce::String (d), d);
    rootBox.addItem ("None", 1);
    for (int note = 36; note <= 83; ++note) rootBox.addItem (midiNoteName (note) + "  (MIDI " + juce::String (note) + ")", note + 100);
    styleNumberField (beatsField, "0123456789");
    styleNumberField (tempoField, "0123456789.");
    styleNumberField (numeratorField, "0123456789");

    for (auto* c : std::initializer_list<juce::Component*> { &heading, &fileLabel, &note, &typeLabel, &lengthLabel, &beatsUnit, &tempoLabel, &bpmUnit,
                                                             &meterLabel, &slash, &rootLabel, &typeBox, &denominatorBox, &rootBox, &beatsField,
                                                             &tempoField, &numeratorField, &stretchToggle, &tempoFromLength, &calculated, &actual,
                                                             &discrepancy, &saveCopyButton, &updateButton })
        addAndMakeVisible (*c);

    auto changed = [this] { updateChecks(); };
    typeBox.onChange = changed;
    denominatorBox.onChange = changed;
    beatsField.onTextChange = changed;
    tempoField.onTextChange = changed;
    numeratorField.onTextChange = changed;
    tempoFromLength.setTooltip ("Set the tempo so that the length in beats matches the file's actual duration exactly");
    tempoFromLength.onClick = [this]
    {
        const int beats = beatsField.getText().getIntValue();
        if (beats > 0 && actualSeconds > 0) tempoField.setText (juce::String (beats * 60.0 / actualSeconds, 3));
    };

    saveCopyButton.setTooltip ("Write a new WAV with the metadata; the original file is not touched (safest)");
    updateButton.setTooltip ("Rewrite this WAV's metadata in place (the audio data is not changed)");
    saveCopyButton.onClick = [this]
    {
        const auto suggested = file.getSiblingFile (file.getFileNameWithoutExtension() + "-loop.wav");
        chooser = std::make_unique<juce::FileChooser> ("Save WAV copy with loop metadata", suggested, "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<LoopMetadataPanel> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File()) return;
            safe->write (fc.getResult().withFileExtension ("wav"));
        });
    };
    updateButton.onClick = [this]
    {
        auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Update WAV")
                           .withMessage ("Write the loop metadata into \"" + file.getFileName() + "\" in place?\n\n"
                                         "Only the metadata changes; the audio data is copied byte for byte and verified "
                                         "before the file is replaced.")
                           .withButton ("Update WAV").withButton ("Cancel").withAssociatedComponent (this);
        juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<LoopMetadataPanel> (this)] (int result)
        {
            if (safe != nullptr && result == 1) safe->write (safe->file);
        });
    };

    loadFromFile();
    setSize (560, 470);
}

void LoopMetadataPanel::loadFromFile()
{
    const auto info = app.run ("get_wav_loop_metadata", makeObject ({ { "path", file.getFullPathName() } }));
    writable = info.isObject() && static_cast<bool> (info["supported"]);
    actualSeconds = info.isObject() ? static_cast<double> (info["durationSeconds"]) : 0.0;

    // Prefill: the file's own metadata when it has some; otherwise clearly-labelled suggestions.
    int beats = 0, numerator = 4, denominator = 4, root = -1;
    double bpm = 0.0;
    bool oneShot = false, stretch = true;
    juce::String source;
    if (info.isObject() && static_cast<bool> (info["hasLoopMetadata"]))
    {
        const auto m = info["metadata"];
        oneShot = m["type"].toString() == "one-shot";
        bpm = m["bpm"];
        beats = m["beats"];
        numerator = m["numerator"];
        denominator = m["denominator"];
        stretch = m["stretch"];
        root = m["rootNote"].isVoid() ? -1 : static_cast<int> (m["rootNote"]);
        source = "Loaded from the file's existing loop metadata.";
    }
    else
    {
        const Asset* asset = nullptr;
        for (const auto& a : app.project().assets)
            if (juce::File (a.path) == file) asset = &a;
        if (asset != nullptr && asset->sourceBpm > 0)
        {
            bpm = asset->sourceBpm;
            source = "No loop metadata in this file yet. Tempo suggested from the asset (" + asset->tempoSource + ") - check before writing.";
        }
        else
        {
            bpm = app.project().bpm;
            source = "No loop metadata in this file yet. Values suggested from the project tempo - check before writing.";
        }
        beats = std::max (1, juce::roundToInt (actualSeconds * bpm / 60.0));
    }
    if (! writable) source = "This file cannot be edited: " + (info.isObject() ? info["problem"].toString() : juce::String ("unreadable"));
    note.setText (source, juce::dontSendNotification);

    typeBox.setSelectedId (oneShot ? 2 : 1, juce::dontSendNotification);
    beatsField.setText (juce::String (beats), false);
    tempoField.setText (bpm > 0 ? juce::String (bpm, 3) : juce::String ("0"), false);
    numeratorField.setText (juce::String (numerator), false);
    denominatorBox.setSelectedId (denominator > 0 ? denominator : 4, juce::dontSendNotification);
    if (root >= 0 && rootBox.indexOfItemId (root + 100) < 0) rootBox.addItem (midiNoteName (root) + "  (MIDI " + juce::String (root) + ")", root + 100);
    rootBox.setSelectedId (root >= 0 ? root + 100 : 1, juce::dontSendNotification);
    stretchToggle.setToggleState (stretch, juce::dontSendNotification);
    updateChecks();
}

void LoopMetadataPanel::updateChecks()
{
    const bool loop = isLoop();
    stretchToggle.setEnabled (loop);
    tempoFromLength.setEnabled (loop && writable);
    const int beats = beatsField.getText().getIntValue();
    const double bpm = tempoField.getText().getDoubleValue();
    actual.setText ("Actual duration:      " + juce::String (actualSeconds, 3) + " s", juce::dontSendNotification);

    // Never "fix" anything silently: show the arithmetic and any mismatch, and let the user decide.
    juce::String problem;
    if (loop && bpm > 0 && beats > 0)
    {
        const double expected = beats * 60.0 / bpm;
        calculated.setText ("Calculated duration:  " + juce::String (expected, 3) + " s   (" + juce::String (beats) + " beats at "
                            + juce::String (bpm, 3) + " BPM)", juce::dontSendNotification);
        const double diff = actualSeconds - expected;
        if (std::abs (diff) > std::max (0.005, expected * 0.001))
            problem = "Mismatch: the file is " + juce::String (std::abs (diff) * 1000.0, 1) + " ms " + (diff > 0 ? "longer" : "shorter")
                    + " than " + juce::String (beats) + " beats at this tempo (" + juce::String (100.0 * diff / expected, 2)
                    + "%). Check the beat count or use \"Tempo from length\".";
    }
    else
        calculated.setText (loop ? "Calculated duration:  - (enter tempo and length)" : "Calculated duration:  - (one-shots play at their own speed)",
                            juce::dontSendNotification);

    LoopMetadata m;
    m.oneShot = ! loop;
    m.bpm = bpm;
    m.beats = beats;
    m.numerator = numeratorField.getText().getIntValue();
    m.denominator = denominatorBox.getSelectedId();
    const auto invalid = validateLoopMetadata (m);
    if (invalid.isNotEmpty()) problem = invalid;
    discrepancy.setText (problem, juce::dontSendNotification);
    discrepancy.setColour (juce::Label::textColourId, invalid.isNotEmpty() ? colours::red : colours::amber);
    saveCopyButton.setEnabled (writable && invalid.isEmpty());
    updateButton.setEnabled (writable && invalid.isEmpty());
}

juce::var LoopMetadataPanel::metadataArgs() const
{
    return makeObject ({ { "type", isLoop() ? "loop" : "one-shot" },
                         { "bpm", tempoField.getText().getDoubleValue() },
                         { "beats", beatsField.getText().getIntValue() },
                         { "numerator", numeratorField.getText().getIntValue() },
                         { "denominator", denominatorBox.getSelectedId() },
                         { "rootNote", rootBox.getSelectedId() > 1 ? rootBox.getSelectedId() - 100 : -1 },
                         { "stretch", stretchToggle.getToggleState() } });
}

void LoopMetadataPanel::write (const juce::File& destination)
{
    auto args = metadataArgs();
    args.getDynamicObject()->setProperty ("path", file.getFullPathName());
    args.getDynamicObject()->setProperty ("overwrite", true);   // the user confirmed (dialog or the save chooser)
    if (destination != file) args.getDynamicObject()->setProperty ("outputPath", destination.getFullPathName());
    const auto r = app.run ("write_wav_loop_metadata", args);
    if (! r.isObject()) return;   // the error is on the status line
    app.status ("Loop metadata written to " + destination.getFileName() + " - audio data unchanged (SHA-256 verified)"
                + (static_cast<bool> (r["durationMatches"]) || ! isLoop() ? juce::String() : juce::String ("; note: length/tempo don't match the duration")));
    if (destination == file) loadFromFile();
}

void LoopMetadataPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::lineSoft);
    g.drawHorizontalLine (108, 16.0f, static_cast<float> (getWidth() - 16));
    g.drawHorizontalLine (getHeight() - 58, 16.0f, static_cast<float> (getWidth() - 16));
}

void LoopMetadataPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    heading.setBounds (r.removeFromTop (24));
    fileLabel.setBounds (r.removeFromTop (20));
    note.setBounds (r.removeFromTop (44));
    r.removeFromTop (14);
    const int labelW = 130, rowH = 28, gap = 8;
    auto row = [&] { auto x = r.removeFromTop (rowH); r.removeFromTop (gap); return x; };

    auto t = row();
    typeLabel.setBounds (t.removeFromLeft (labelW));
    typeBox.setBounds (t.removeFromLeft (160));
    auto l = row();
    lengthLabel.setBounds (l.removeFromLeft (labelW));
    beatsField.setBounds (l.removeFromLeft (80));
    beatsUnit.setBounds (l.removeFromLeft (60));
    auto tp = row();
    tempoLabel.setBounds (tp.removeFromLeft (labelW));
    tempoField.setBounds (tp.removeFromLeft (80));
    bpmUnit.setBounds (tp.removeFromLeft (50));
    tempoFromLength.setBounds (tp.removeFromLeft (150));
    auto m = row();
    meterLabel.setBounds (m.removeFromLeft (labelW));
    numeratorField.setBounds (m.removeFromLeft (50));
    slash.setBounds (m.removeFromLeft (20));
    denominatorBox.setBounds (m.removeFromLeft (70));
    auto rt = row();
    rootLabel.setBounds (rt.removeFromLeft (labelW));
    rootBox.setBounds (rt.removeFromLeft (200));
    stretchToggle.setBounds (row().withTrimmedLeft (labelW));

    calculated.setBounds (r.removeFromTop (20));
    actual.setBounds (r.removeFromTop (20));
    auto buttons = r.removeFromBottom (30);
    updateButton.setBounds (buttons.removeFromRight (130));
    buttons.removeFromRight (8);
    saveCopyButton.setBounds (buttons.removeFromRight (130));
    r.removeFromBottom (12);
    discrepancy.setBounds (r.withTrimmedTop (4));
}
} // namespace mcpdaw::ui
