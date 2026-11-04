#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "Agitation.h"
#include "AgitationEngine.h"
#include "ParameterRegistry.h"
#include "ParameterRowComponents.h"

/**
    UI for Agitation (feature/agitation-and-snapshots, Branch 5, Stage 2's
    final piece) -- unlike ModifierStackEditor's own per-source, dynamic-
    length stack, this is a FIXED list built once from whichever
    parameters are currently in `ParameterRegistry`: one row per
    Scope::Global or Scope::SelectedObject descriptor (Scope::
    SpecificObject entries -- "object.3.mass" etc. -- are deliberately
    excluded; this panel already only ever shows ONE object's own
    settings at a time, the currently selected one, same convention as
    every other category here), grouped under a small header per
    ParameterRegistry category (mirroring ParameterPanel's own category
    tab order).

    Because `ParameterRegistry`/`AgitationEngine` are both single,
    processor-lifetime-stable instances (NOT rebound per selected object,
    unlike editedGrainCloud/editedDelayInstance), setup() is meant to be
    called exactly ONCE, not on every selection change -- see
    ParameterPanel::setAgitationModel()'s own call site. A row bound to
    a Scope::SelectedObject id (e.g. "selectedObject.mass") naturally
    keeps acting on whichever object is currently selected underneath,
    exactly as every other consumer of that scope already does (see
    ParameterRegistry.h's own class comment) -- this UI doesn't need to
    know or care which object that is.

    Same "reports its own total preferred height via onHeightChanged"
    pattern as ModifierStackEditor, for the identical reason: a genuinely
    variable-height component within ParameterPanel's otherwise fixed-row
    layout system (see ParameterPanel::updateLayoutHeight()).
*/

/**
    One parameter's Agitation row -- Enabled toggle, plus (only while
    enabled) Amount/Rate/Smoothing/TempoSynced, plus (only while BOTH
    enabled and TempoSynced) TempoDivision. Rate is hidden whenever
    TempoSynced is on, since it's then ignored entirely (see Agitation
    ::advance()) in favour of the tempo-derived rate -- showing a control
    that currently has no effect would be confusing.

    Reads/writes AgitationEngine's entry for `id` carefully asymmetrically:
    refreshFromModel()/getPreferredHeight()/resized() only ever READ via
    AgitationEngine::findSettings() (never creating an entry just by
    being displayed or refreshed -- the whole point of this panel
    existing is that the overwhelming majority of its ~80 rows will
    never be configured at all), while the row's own control callbacks
    (onToggled/onValueChanged) are the ONLY things that ever call
    AgitationEngine::settingsFor() (which lazily creates), and only when
    the user actually changes something.
*/
class AgitationRowComponent : public juce::Component
{
public:
    AgitationRowComponent (juce::String idIn, const juce::String& displayName, float amountRange,
                            AgitationEngine& engineIn, std::function<void()> onHeightChanged);

    void resized() override;
    void refreshFromModel();
    int getPreferredHeight() const;

    static constexpr int rowGap = 4;

private:
    // Snapshot of what to currently show -- read once per call via
    // findSettings() (never creating an entry), defaulted to
    // Agitation::Settings{} (all disabled) when this parameter has no
    // entry at all yet.
    Agitation::Settings shownSettings() const;

    juce::String id;
    AgitationEngine& engine;
    std::function<void()> onHeightChangedCallback;

    std::unique_ptr<ToggleRowComponent> enabledRow;
    std::unique_ptr<FloatRowComponent> amountRow;
    std::unique_ptr<FloatRowComponent> rateRow;
    std::unique_ptr<FloatRowComponent> smoothingRow;
    std::unique_ptr<ToggleRowComponent> tempoSyncRow;
    std::unique_ptr<ComboRowComponent> tempoDivisionRow;
};

class AgitationEditor : public juce::Component
{
public:
    // Builds the full, fixed row list from every Scope::Global/
    // SelectedObject descriptor currently in `registry`, grouped by
    // category -- see this class's own comment on why this is meant to
    // run exactly once, not per selection change.
    void setup (const ParameterRegistry& registry, AgitationEngine& engineIn);

    // Re-reads every row's current values (e.g. after a preset load) --
    // does NOT rebuild the row list itself (registry/engine never change
    // shape after setup(), unlike ModifierStackEditor's own variable
    // slot count).
    void refreshFromModel();

    int getPreferredHeight() const;
    void resized() override;

    std::function<void()> onHeightChanged;

    static constexpr int rowGap = 6;
    static constexpr int categoryHeaderHeight = 22;

private:
    struct CategoryGroup
    {
        std::unique_ptr<juce::Label> headerLabel;
        std::vector<std::unique_ptr<AgitationRowComponent>> rows;
    };

    std::vector<CategoryGroup> groups;
};
