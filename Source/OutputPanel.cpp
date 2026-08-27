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
    decoderModeRow->combo.addItem ("Quad", 5);
    decoderModeRow->combo.addItem ("5.1", 6);
    decoderModeRow->combo.addItem ("7.1", 7);
    decoderModeRow->combo.addItem ("5.1.2 (Atmos)", 8);
    decoderModeRow->combo.addItem ("5.1.4 (Atmos)", 9);
    decoderModeRow->combo.addItem ("7.1.2 (Atmos)", 10);
    decoderModeRow->combo.addItem ("7.1.4 (Atmos)", 11);
    decoderModeRow->combo.addItem ("Octophonic (8ch circular)", 12);
    decoderModeRow->combo.addItem ("Circular Array (adjustable)", 13);
    decoderModeRow->onSelected = [this] (int index)
    {
        if (decoderProcessor != nullptr)
            decoderProcessor->setDecoderMode ((AmbisonicsDecoder::Mode) index);
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
}

void OutputPanel::paint (juce::Graphics& g)
{
    g.fillAll (UiColours::bgPanel());
}
