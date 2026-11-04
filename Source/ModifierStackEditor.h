#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "Modifier.h"
#include "ParameterRowComponents.h"

/**
    UI for one modifier stack (feature/modifier-stack) -- a variable-
    length, reorderable list of Modifier slots, each showing only the
    controls relevant to its own `type`. Unlike the rest of ParameterPanel
    (a FIXED set of rows built once, shown/hidden by simple conditions),
    this genuinely needs a dynamic child-component list, since the
    number of slots changes at runtime (Add/Remove) -- see
    ModifierStackEditor::rebuildRows().

    Reports its own total preferred height via onHeightChanged whenever
    that changes (a slot added/removed/reordered, or a slot's own type
    switching to one with a different row count) -- ParameterPanel wires
    this to its own updateLayoutHeight()/layoutContent(), the same two
    calls any other visibility change there already triggers.
*/

/** One slot: Enabled/Type/Up/Down/Remove header, plus whichever type-specific rows are currently relevant. */
class ModifierRowComponent : public juce::Component
{
public:
    // ownerStack: resolved AFRESH on every access, never cached across a
    // call -- the vector can reallocate on a sibling row's Add/Remove/
    // Move, same "always re-resolve the live pointer" discipline
    // ParameterPanel's own editedGrainCloud/editedDelayInstance already
    // use. allowPitch: false for DelayInstance's stack (see DelayTap.h's
    // own note on why Pitch isn't offered there). onMoveUp/onMoveDown/
    // onRemove: structural changes -- the owning ModifierStackEditor
    // responds by rebuilding its whole row list (indices shift).
    // onHeightChanged: this row's OWN height changed (type switched to
    // one with a different row count) -- no structural rebuild needed,
    // just a re-layout.
    ModifierRowComponent (std::function<std::vector<Modifier>*()> ownerStack, int indexIn, bool allowPitchIn,
                           std::function<void()> onMoveUp, std::function<void()> onMoveDown,
                           std::function<void()> onRemove, std::function<void()> onHeightChanged);

    void resized() override;

    // Re-reads this slot's current values into every control -- call
    // after any change that didn't originate from this row itself (a
    // preset load, switching the edited object/cloud).
    void refreshFromModel();

    int getPreferredHeight() const;
    static int preferredHeightFor (ModifierType type);

    static constexpr int headerHeight = 28;

private:
    Modifier* current() const; // ownerStack()->at(index), or nullptr if the index is somehow out of range (defensive)
    void refreshTypeVisibility();

    std::function<std::vector<Modifier>*()> ownerStack;
    int index;
    bool allowPitch;
    std::function<void()> onHeightChangedCallback;

    juce::Label indexLabel; // "1.", "2.", ... -- so the stack reads top-to-bottom unambiguously
    juce::ToggleButton enabledToggle;
    juce::ComboBox typeCombo;
    juce::TextButton upButton, downButton, removeButton;

    std::unique_ptr<FloatRowComponent> gainRow, gainSpreadRow;
    std::unique_ptr<FloatRowComponent> timeOffsetRow, timeOffsetSpreadRow;
    std::unique_ptr<ComboRowComponent> filterTypeRow;
    std::unique_ptr<FloatRowComponent> filterCutoffRow, filterResonanceRow, filterCutoffJitterRow;
    std::unique_ptr<FloatRowComponent> pitchRow, pitchSpreadRow;

    std::unique_ptr<ComboRowComponent> movementShapeRow;
    std::unique_ptr<FloatRowComponent> movementSpeedRow, movementSpeedSpreadRow;
    std::unique_ptr<FloatRowComponent> movementAccelerationRow, movementAccelerationSpreadRow;

    std::unique_ptr<ComboRowComponent> spatialShapeRow;
    std::unique_ptr<FloatRowComponent> spatialRadiusRow, spatialRadiusSpreadRow;
};

class ModifierStackEditor : public juce::Component
{
public:
    explicit ModifierStackEditor (bool allowPitchIn);

    // nullptr = no selection (mirrors ParameterPanel::setEditedGrainCloud()).
    void setModifiers (std::vector<Modifier>* stackIn);
    void refreshFromModel();

    int getPreferredHeight() const;
    void resized() override;

    // Called whenever getPreferredHeight() would now return something
    // different -- the owning ParameterPanel reacts with
    // updateLayoutHeight()/layoutContent().
    std::function<void()> onHeightChanged;

    static constexpr int addButtonHeight = 28;
    static constexpr int rowGap = 6;

private:
    void rebuildRows();

    std::vector<Modifier>* modifiers = nullptr;
    bool allowPitch;
    std::vector<std::unique_ptr<ModifierRowComponent>> rows;
    juce::TextButton addButton { "Add Modifier" };
};
