#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MappingEngine.h"
#include "ParameterRegistry.h"

/**
    Learn-mode mapping UI: pick a target parameter, press Learn, then move
    the stick/trigger/button you want to drive it -- MappingEngine (see
    its own class comment) does the actual capture; this panel just drives
    that workflow and shows the current binding list. Also Load/Save for
    mapping profiles (MappingProfileManager.h), mirroring
    KlangorbitEditor's own preset load/save file-chooser pattern.

    The target-parameter picker deliberately only lists Scope::Global and
    Scope::SelectedObject parameters (see ParameterRegistry.h), not the
    per-object Scope::SpecificObject variants -- those exist (8 slots x
    every field) specifically to keep the combo box to a manageable size
    for this Learn-mode workflow, which is built around "whichever object
    is currently selected" as its central concept anyway. A
    SpecificObject binding can still be authored by hand-editing a
    mapping profile JSON (MappingProfiles/schema/README.md); nothing
    about MappingEngine itself is restricted to Global/SelectedObject.

    Refreshes its own binding list / Learn-mode status on a light
    juce::Timer (~10Hz) rather than a proper change-notification callback
    from MappingEngine -- simple polling, cheap at this rate, avoids
    adding listener plumbing to MappingEngine for a single UI consumer.
*/
class MappingPanel : public juce::Component,
                      private juce::Timer
{
public:
    // registryToShow/engineToControl: not owned, must outlive this panel
    // (KlangorbitProcessor owns both for its whole lifetime, this panel
    // is owned by a window that closes/hides before the processor does).
    MappingPanel (const ParameterRegistry& registryToShow, MappingEngine& engineToControl);
    ~MappingPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void refreshBindingsList();
    void refreshStatus();
    void learnButtonClicked();
    void loadProfileClicked();
    void saveProfileClicked();

    const ParameterRegistry& registry;
    MappingEngine& engine;

    // Parallel to targetParameterCombo's items: index i (0-based) holds
    // the ParameterRegistry::Descriptor::id for combo item id (i+1) --
    // see the class comment on why this is Global/SelectedObject only.
    std::vector<juce::String> comboIndexToParameterId;

    juce::Label targetLabel { {}, "Target parameter:" };
    juce::ComboBox targetParameterCombo;
    juce::TextButton learnButton { "Learn" };
    juce::Label statusLabel;

    juce::Label modifierLabel { {}, "Paging modifier (held = bank 1):" };
    juce::Label modifierValueLabel; // shows the current modifier source id -- read-only display, no edit UI yet

    juce::Label bindingsHeaderLabel { {}, "Current bindings:" };
    juce::Viewport bindingsViewport;
    juce::Component bindingsContent;
    struct BindingRow
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::TextButton> removeButton;
        MappingBinding binding;
    };
    std::vector<BindingRow> bindingRows;
    size_t lastBindingsCount = (size_t) -1; // forces the first refreshBindingsList() to actually rebuild

    juce::TextButton loadProfileButton { "Load Profile..." };
    juce::TextButton saveProfileButton { "Save Profile..." };
    juce::Label profileStatusLabel;
    juce::String currentProfileName { "(no profile loaded)" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MappingPanel)
};
