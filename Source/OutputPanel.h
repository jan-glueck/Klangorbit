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

    No scrollable viewport needed (unlike ParameterPanel) -- only five
    rows total, all fit in a fixed-size window without scrolling.
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
    KlangorbitProcessor* decoderProcessor = nullptr;

    juce::Label decoderModeHintLabel;
    std::unique_ptr<ComboRowComponent> decoderModeRow;
    std::unique_ptr<FloatRowComponent> circularSpeakerCountRow;
    juce::Label circularArrayHintLabel;
    std::unique_ptr<ToggleRowComponent> bassManagementRow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputPanel)
};
