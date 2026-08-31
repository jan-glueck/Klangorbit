#include "OutputPanel.h"
#include "UiTheme.h"
#include "PluginProcessor.h"
#include <cmath>

namespace
{
    void styleHintLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (12.5f)));
        l.setColour (juce::Label::textColourId, UiColours::textDisabled());
        l.setJustificationType (juce::Justification::topLeft);
    }
}

OutputPanel::OutputPanel()
{
    styleHintLabel (decoderModeHintLabel,
                     "Switching output format changes the plugin's output "
                     "channel count. Most hosts pick this up live; some need "
                     "the plugin removed and reinserted (or the project "
                     "reloaded) to fully apply it.");
    addAndMakeVisible (decoderModeHintLabel);

    decoderModeRow = std::make_unique<ComboRowComponent> ("Output Format");
    // Order matches AmbisonicsDecoder::Mode exactly -- combo item POSITION
    // (0-based, from getSelectedItemIndex() below) is cast directly to the
    // enum. addItem()'s IDs (1, 2, 3, ...) are just JUCE's required
    // 1-based ComboBox item IDs, unrelated to the enum's own values.
    decoderModeRow->combo.addItem ("Ambisonics (Order 1, 4ch)", 1);
    decoderModeRow->combo.addItem ("Ambisonics (Order 2, 9ch)", 2);
    decoderModeRow->combo.addItem ("Ambisonics (Order 3, 16ch)", 3);
    decoderModeRow->combo.addItem ("Stereo", 4);
    decoderModeRow->combo.addItem ("Binaural (HRTF, 2ch)", 5);
    decoderModeRow->combo.addItem ("Quad", 6);
    decoderModeRow->combo.addItem ("5.1", 7);
    decoderModeRow->combo.addItem ("7.1", 8);
    decoderModeRow->combo.addItem ("5.1.2 (Atmos)", 9);
    decoderModeRow->combo.addItem ("5.1.4 (Atmos)", 10);
    decoderModeRow->combo.addItem ("7.1.2 (Atmos)", 11);
    decoderModeRow->combo.addItem ("7.1.4 (Atmos)", 12);
    decoderModeRow->combo.addItem ("Octophonic (8ch circular)", 13);
    decoderModeRow->combo.addItem ("Circular Array (adjustable)", 14);
    decoderModeRow->onSelected = [this] (int index)
    {
        if (decoderProcessor != nullptr)
            decoderProcessor->setDecoderMode ((AmbisonicsDecoder::Mode) index);
        updateBinauralRowsVisibility();
        resized();
    };
    addAndMakeVisible (*decoderModeRow);

    // Only meaningful while Output Format == Circular Array (silently
    // ignored otherwise, including by the fixed Octophonic preset -- see
    // AmbisonicsDecoder::setCircularArraySpeakerCount()). Kept always
    // visible rather than hidden/shown per mode -- a single row, not worth
    // the extra state.
    circularSpeakerCountRow = std::make_unique<FloatRowComponent> (
        "Circular Array: Speaker Count", (double) AmbisonicsDecoder::minCircularSpeakers,
        (double) AmbisonicsDecoder::maxCircularSpeakers, 1.0);
    circularSpeakerCountRow->onValueChanged = [this] (float v)
    {
        if (decoderProcessor != nullptr)
            decoderProcessor->setCircularArraySpeakerCount ((int) std::round (v));
    };
    addAndMakeVisible (*circularSpeakerCountRow);

    styleHintLabel (circularArrayHintLabel,
                     "Octophonic/Circular Array are horizontal-only -- a "
                     "circular array of speakers cannot reproduce elevation, "
                     "regardless of decoder quality. Circular Array with "
                     "fewer than 7 speakers will show extra spatial blur, an "
                     "unavoidable property of a small ring at this internal "
                     "Ambisonics order.");
    addAndMakeVisible (circularArrayHintLabel);

    // Default off -- Ambisonics has no dedicated LFE signal, so this is a
    // real, audible addition (a low-passed W channel) to what was mixed,
    // not something to silently turn on. Only affects modes with an LFE
    // channel (5.1/7.1/Atmos variants); harmless no-op otherwise.
    bassManagementRow = std::make_unique<ToggleRowComponent> ("Bass Management (LFE from W)");
    bassManagementRow->onToggled = [this] (bool v)
    {
        if (decoderProcessor != nullptr)
            decoderProcessor->setBassManagementEnabled (v);
    };
    addAndMakeVisible (*bassManagementRow);

    // Only shown while Output Format == Binaural -- see
    // updateBinauralRowsVisibility(). Order matches
    // KlangorbitProcessor::BinauralDatasetSource exactly, same
    // position-cast-to-enum idiom as decoderModeRow above.
    binauralDatasetRow = std::make_unique<ComboRowComponent> ("HRTF Dataset");
    binauralDatasetRow->combo.addItem ("KEMAR (MIT Media Lab)", 1);
    binauralDatasetRow->combo.addItem ("SADIE II -- D1, KU100 (University of York)", 2);
    binauralDatasetRow->combo.addItem ("Custom SOFA file...", 3);
    binauralDatasetRow->onSelected = [this] (int index)
    {
        if (decoderProcessor == nullptr)
            return;

        using Source = KlangorbitProcessor::BinauralDatasetSource;
        if (index == (int) Source::CustomFile)
        {
            // Selecting "Custom..." always opens the picker (even if a
            // custom file is already loaded) -- lets the user swap to a
            // different custom file without a separate control. Reverts
            // the combo to whatever dataset is actually active if the
            // user cancels, see browseForCustomSofaFile().
            browseForCustomSofaFile();
            return;
        }

        decoderProcessor->setBinauralDataset ((Source) index);
    };
    addChildComponent (*binauralDatasetRow); // hidden until Binaural is selected

    binauralBrowseButton.onClick = [this] { browseForCustomSofaFile(); };
    addChildComponent (binauralBrowseButton);

    styleHintLabel (binauralCustomFileLabel, {});
    addChildComponent (binauralCustomFileLabel);

    styleHintLabel (binauralHintLabel,
                     "KEMAR: Gardner & Martin, MIT Media Lab. SADIE II D1: "
                     "University of York, CC BY-SA / Apache 2.0. See "
                     "THIRD_PARTY_LICENSES.md for the full required "
                     "attribution. Switching datasets rebuilds the decoder "
                     "-- a brief pause is expected, longer for SADIE II.");
    addChildComponent (binauralHintLabel);
}

void OutputPanel::setProcessor (KlangorbitProcessor* proc)
{
    decoderProcessor = proc;
}

void OutputPanel::refreshFromModel()
{
    if (decoderProcessor == nullptr)
        return;

    decoderModeRow->combo.setSelectedItemIndex ((int) decoderProcessor->getDecoderMode(), juce::dontSendNotification);
    circularSpeakerCountRow->setValueQuiet ((float) decoderProcessor->getCircularArraySpeakerCount());
    bassManagementRow->setValueQuiet (decoderProcessor->isBassManagementEnabled());

    binauralDatasetRow->combo.setSelectedItemIndex ((int) decoderProcessor->getBinauralDatasetSource(), juce::dontSendNotification);
    const auto customFile = decoderProcessor->getCustomSofaFilePath();
    binauralCustomFileLabel.setText (customFile.existsAsFile()
                                          ? "Custom file: " + customFile.getFileName()
                                          : "No custom SOFA file loaded yet.",
                                      juce::dontSendNotification);
    updateBinauralRowsVisibility();
    resized();
}

void OutputPanel::updateBinauralRowsVisibility()
{
    const bool isBinaural = decoderProcessor != nullptr
        && decoderProcessor->getDecoderMode() == AmbisonicsDecoder::Mode::Binaural;

    binauralDatasetRow->setVisible (isBinaural);
    binauralBrowseButton.setVisible (isBinaural);
    binauralCustomFileLabel.setVisible (isBinaural);
    binauralHintLabel.setVisible (isBinaural);
}

void OutputPanel::browseForCustomSofaFile()
{
    if (decoderProcessor == nullptr)
        return;

    const auto existingCustom = decoderProcessor->getCustomSofaFilePath();
    const juce::File startDir = existingCustom.existsAsFile()
        ? existingCustom.getParentDirectory()
        : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    fileChooser = std::make_unique<juce::FileChooser> ("Load Custom SOFA File", startDir, "*.sofa");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            if (decoderProcessor == nullptr)
                return;

            const auto file = fc.getResult();
            if (! file.existsAsFile())
            {
                refreshFromModel(); // user cancelled -- revert the combo to whatever's actually active
                return;
            }

            if (! decoderProcessor->loadCustomSofaFile (file))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "Could not load SOFA file",
                    "\"" + file.getFileName() + "\" could not be opened as a SOFA (AES69) HRTF file. "
                    "The previously active dataset is still in use.");

            refreshFromModel();
        });
}

void OutputPanel::resized()
{
    auto b = getLocalBounds().reduced (UiSpacing::m);

    decoderModeHintLabel.setBounds (b.removeFromTop (56));
    b.removeFromTop (UiSpacing::m);

    decoderModeRow->setBounds (b.removeFromTop (ComboRowComponent::preferredHeight));
    b.removeFromTop (UiSpacing::m);

    circularSpeakerCountRow->setBounds (b.removeFromTop (FloatRowComponent::preferredHeight));
    b.removeFromTop (UiSpacing::m);

    circularArrayHintLabel.setBounds (b.removeFromTop (64));
    b.removeFromTop (UiSpacing::m);

    bassManagementRow->setBounds (b.removeFromTop (ToggleRowComponent::preferredHeight));

    if (binauralDatasetRow->isVisible())
    {
        b.removeFromTop (UiSpacing::m);
        binauralDatasetRow->setBounds (b.removeFromTop (ComboRowComponent::preferredHeight));
        b.removeFromTop (UiSpacing::s);
        binauralBrowseButton.setBounds (b.removeFromTop (24));
        b.removeFromTop (UiSpacing::s);
        binauralCustomFileLabel.setBounds (b.removeFromTop (18));
        b.removeFromTop (UiSpacing::m);
        binauralHintLabel.setBounds (b.removeFromTop (48));
    }
}

void OutputPanel::paint (juce::Graphics& g)
{
    g.fillAll (UiColours::bgPanel());
}
