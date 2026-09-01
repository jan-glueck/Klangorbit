#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "ParameterPanel.h"
#include "AmbisonicsDecoder.h"

class KlangorbitProcessor;

/**
    Content for OutputWindow -- the plugin-wide Output Format / Bass
    Management / Circular Array speaker count controls, moved here out of
    ParameterPanel's former "Output" category (see CHANGELOG) into their
    own dedicated window instead, alongside Help and Mappings, rather than
    a tab sharing space with Scene/Object settings -- these are plugin-
    wide settings, not tied to the scene or any object, so they get their
    own place rather than living inside either of those.

    Reuses FloatRowComponent/ComboRowComponent/ToggleRowComponent
    (declared in ParameterPanel.h, not nested inside that class) rather
    than duplicating those row widgets -- same reasoning MappingPanel
    already applies for its own rows.

    No scrollable viewport needed (unlike ParameterPanel) -- OutputWindow
    is sized tall enough to fit every row, including the Binaural
    HRTF-dataset controls that only appear while Output Format ==
    Binaural (see updateBinauralRowsVisibility()), without scrolling.
*/
class OutputPanel : public juce::Component
{
public:
    OutputPanel();

    // Set once by KlangorbitEditor (never null again afterwards) -- same
    // convention as ParameterPanel::setProcessor() had.
    void setProcessor (KlangorbitProcessor* proc);

    // Synchronizes the controls with the current model state -- call once
    // whenever the window is (re)shown, since (unlike ParameterPanel) this
    // panel isn't already kept in sync by selection-change callbacks.
    void refreshFromModel();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    // Opens the "Browse..." SOFA file picker, calls
    // KlangorbitProcessor::loadCustomSofaFile() on the result, and updates
    // binauralDatasetRow/binauralCustomFileLabel to match -- reverts the
    // combo back to whatever was active before on failure (see the
    // header's own comment on loadCustomSofaFile() never silently going
    // silent).
    void browseForCustomSofaFile();
    void updateBinauralRowsVisibility();
    // circularSpeakerCountRow: only while Output Format == Circular Array
    // (Octophonic's own speaker count is fixed at 8, not adjustable, so
    // the slider is irrelevant there too, same as every other format).
    // circularArrayHintLabel: while Output Format == Octophonic OR
    // Circular Array (its text -- horizontal-only, spatial blur below 7
    // speakers -- applies to both, not just the adjustable one).
    void updateCircularArrayRowsVisibility();
    // Only while the current Output Format actually has an LFE channel
    // (5.1/7.1/Atmos-bed variants) -- see
    // AmbisonicsDecoder::lfeChannelIndexFor().
    void updateBassManagementRowVisibility();
    // Greys out (ComboBox::setItemEnabled, not removed) any Output Format
    // combo item KlangorbitProcessor::isOutputModeAvailable() rejects --
    // a no-op on VST3/Standalone (every mode always available there); see
    // that method's own comment for the AU-specific reasoning.
    void updateDecoderModeAvailability();

    KlangorbitProcessor* decoderProcessor = nullptr;

    juce::Label decoderModeHintLabel;
    std::unique_ptr<ComboRowComponent> decoderModeRow;
    std::unique_ptr<FloatRowComponent> circularSpeakerCountRow;
    juce::Label circularArrayHintLabel;
    std::unique_ptr<ToggleRowComponent> bassManagementRow;

    // Only shown while Output Format == Binaural -- see
    // updateBinauralRowsVisibility(). "A (KEMAR/SADIE) as default, B
    // (custom SOFA import) as an additional option" -- see the CHANGELOG
    // entry for this feature and THIRD_PARTY_LICENSES.md for the required
    // attribution both bundled datasets carry.
    std::unique_ptr<ComboRowComponent> binauralDatasetRow;
    juce::TextButton binauralBrowseButton { "Browse for custom SOFA file..." };
    juce::Label binauralCustomFileLabel;
    juce::Label binauralHintLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputPanel)
};
