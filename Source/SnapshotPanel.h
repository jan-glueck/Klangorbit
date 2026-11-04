#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "ParameterRegistry.h"
#include "SnapshotManager.h"
#include "SnapshotMorphEngine.h"
#include "RandomEngine.h"

/**
    Capture/recall UI for Snapshots (feature/agitation-and-snapshots,
    Branch 5, Stages 3-4) -- a category checklist (the "Geltungsbereich
    ueber die Kategorien des Parameter-Registers" scope selector the
    spec asked for), plus three sections built on top of it:

    - Load/Save Snapshot: round-trips exactly one snapshot through its
      own file, mirroring MappingPanel's own Load/Save Profile file-
      chooser pattern exactly. Loading immediately recalls it (writes
      it live) -- the quick, common "just load my saved state" path.
      Also keeps the loaded/saved data in `currentSnapshot`, the base
      the Randomize section below acts on.

    - Randomize: "Zufaellige Variante aus Snapshot" (Strength slider,
      0 = `currentSnapshot` exactly, 1 = can reach a parameter's full
      range either side of its own captured value) and "alles
      randomisieren" (ignores `currentSnapshot`'s own values entirely,
      draws a fresh value across each parameter's full range) -- both
      act on `currentSnapshot`'s own scope and recall the result
      immediately. Disabled until a snapshot has actually been loaded
      or saved at least once.

    - Morph: "Load A..."/"Load B..." populate two in-memory endpoints
      (NOT auto-recalled -- unlike the plain Load above, setting up a
      morph target shouldn't itself jump the live state anywhere) for
      `SnapshotMorphEngine`, then a Morph slider (0 = pure A, 1 = pure
      B) and a Smoothing slider drive it continuously once BOTH are
      loaded. Dragging Morph does NOT itself refresh ParameterPanel's
      visible values on every tick (same limitation AgitationEditor's
      own continuous wobble already has -- neither polls the live
      model back into the UI every control-rate tick); reopening/
      refreshing that panel picks up wherever the morph currently sits.

    randomizeRng is this panel's own RNG, deliberately NOT a persisted/
    seeded SeededRandom the way GrainCloud/DelayInstance/AgitationEngine
    each have one -- a Randomize button click is itself meant to be a
    fresh, unpredictable draw each time (same spirit as the "New Seed"
    buttons elsewhere), not something a saved preset needs to
    reproduce exactly.
*/
class SnapshotPanel : public juce::Component
{
public:
    // registryToShow/morphEngineToControl: not owned, must outlive this
    // panel (KlangorbitProcessor owns both for its whole lifetime, this
    // panel is owned by a window that closes/hides before the processor
    // does).
    //
    // onRecalled: called after anything that writes live values through
    // the registry (a plain Load, or either Randomize action) -- this
    // panel doesn't know about ParameterPanel itself (same separation
    // MappingPanel/AgitationEditor already keep). NOT called for Morph
    // changes -- see this class's own comment on why.
    SnapshotPanel (const ParameterRegistry& registryToShow, SnapshotMorphEngine& morphEngineToControl,
                    std::function<void()> onRecalled);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void loadSnapshotClicked();
    void saveSnapshotClicked();
    void loadMorphEndpointClicked (bool isA);
    void randomizeVariantClicked();
    void randomizeEverythingClicked();
    void updateMorphControlsEnabled();
    void updateRandomizeControlsEnabled();
    std::vector<juce::String> selectedCategories() const; // empty = every checkbox ticked, i.e. no restriction

    const ParameterRegistry& registry;
    SnapshotMorphEngine& morphEngine;
    std::function<void()> onRecalledCallback;
    SeededRandom randomizeRng; // see this class's own comment on why not persisted/seeded

    juce::Label categoriesHeaderLabel { {}, "Scope (which categories to include):" };
    struct CategoryToggle { std::unique_ptr<juce::ToggleButton> toggle; juce::String category; };
    std::vector<CategoryToggle> categoryToggles;

    juce::TextButton loadButton { "Load Snapshot..." };
    juce::TextButton saveButton { "Save Snapshot..." };
    juce::Label statusLabel;
    juce::String currentSnapshotName { "(no snapshot loaded)" };
    SnapshotManager::Snapshot currentSnapshot; // the Randomize section's own base
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Label randomizeHeaderLabel { {}, "Randomize (from the snapshot loaded/saved above):" };
    juce::Slider strengthSlider;
    juce::Label strengthLabel { {}, "Strength" };
    juce::TextButton randomizeVariantButton { "Randomize Variant" };
    juce::TextButton randomizeEverythingButton { "Randomize Everything" };

    juce::Label morphHeaderLabel { {}, "Morph (between two loaded snapshots):" };
    juce::TextButton loadAButton { "Load A..." };
    juce::TextButton loadBButton { "Load B..." };
    juce::Label morphStatusLabel;
    SnapshotManager::Snapshot morphSnapshotA, morphSnapshotB;
    juce::String morphNameA { "(none)" }, morphNameB { "(none)" };
    juce::Slider morphSlider;
    juce::Label morphLabel { {}, "Morph A <-> B" };
    juce::Slider morphSmoothingSlider;
    juce::Label morphSmoothingLabel { {}, "Smoothing (s)" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapshotPanel)
};
