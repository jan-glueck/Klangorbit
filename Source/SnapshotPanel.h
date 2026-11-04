#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "ParameterRegistry.h"
#include "SnapshotManager.h"

/**
    Capture/recall UI for Snapshots (feature/agitation-and-snapshots,
    Branch 5, Stage 3) -- a category checklist (the "Geltungsbereich
    ueber die Kategorien des Parameter-Registers" scope selector the
    spec asked for) plus Save/Load buttons, mirroring MappingPanel's
    own Load/Save Profile file-chooser pattern exactly (same "own
    schema, own file, own file-chooser dialog" treatment).

    Deliberately simple for this stage: no in-session list of multiple
    named snapshots, no morphing, no randomized variants yet (see
    SnapshotManager.h's own comment on why -- those are Stage 4). Each
    Save/Load round-trips exactly one snapshot through its own file,
    same "pick a file each time" workflow Save/Load Preset and
    Load/Save Mapping Profile already use.
*/
class SnapshotPanel : public juce::Component
{
public:
    // registryToShow: not owned, must outlive this panel (KlangorbitProcessor
    // owns it for its whole lifetime, this panel is owned by a window that
    // closes/hides before the processor does).
    //
    // onRecalled: called after a successful Load -- recall() writes
    // values straight into the live model through the registry's own
    // setValue(), so whichever UI is currently showing those parameters
    // (ParameterPanel) needs to be told to re-read them. This panel
    // doesn't know about ParameterPanel itself (same separation
    // MappingPanel/AgitationEditor already keep).
    SnapshotPanel (const ParameterRegistry& registryToShow, std::function<void()> onRecalled);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void loadSnapshotClicked();
    void saveSnapshotClicked();
    std::vector<juce::String> selectedCategories() const; // empty = every checkbox ticked, i.e. no restriction

    const ParameterRegistry& registry;
    std::function<void()> onRecalledCallback;

    juce::Label categoriesHeaderLabel { {}, "Scope (which categories to include):" };
    struct CategoryToggle { std::unique_ptr<juce::ToggleButton> toggle; juce::String category; };
    std::vector<CategoryToggle> categoryToggles;

    juce::TextButton loadButton { "Load Snapshot..." };
    juce::TextButton saveButton { "Save Snapshot..." };
    juce::Label statusLabel;
    juce::String currentSnapshotName { "(no snapshot loaded)" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapshotPanel)
};
