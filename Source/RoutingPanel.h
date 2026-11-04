#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "TrajectoryEngine.h"

/**
    The input routing matrix UI (feature/input-routing-matrix) -- a grid
    of checkboxes, rows = live input channels, columns = object slots
    (live-input-channel count and object-slot count are the same number
    by construction throughout this project, see `PluginProcessor.h`'s
    `numLiveInputs`, so this is always a square grid). Checking cell
    (channel, object) means that channel's samples are summed into that
    object's own live signal -- see `Source/InputRouting.h` for the
    actual resolve/mix logic this UI only ever edits the input to.

    First widget of its kind in this project (see the investigation that
    preceded this feature: no existing grid/matrix component to build
    on) -- composed from plain `juce::ToggleButton`s, no label text per
    cell (the row/column headers already identify it).

    Columns for inactive object slots are entirely disabled (greyed
    out) -- routing an inactive object means nothing until it's
    activated via "+ Object" elsewhere. This panel does NOT live-poll
    for activation changes while open (same "doesn't live-refresh"
    convention `OutputWindow`/`MappingPanel`'s own binding list each
    already accept in their own way) -- `refreshFromModel()` rebuilds
    the whole grid from the engine's current state, called once each
    time this panel's owning window is (re)shown.

    Lazy, copy-on-write editing: a cell toggle for an object whose
    `SoundObject::routedInputChannels` is still empty first initializes
    it from `InputRouting::resolveChannels()`'s own resolved view (the
    1:1 default), THEN applies that one toggle -- so unchecking an
    object's own original channel on the very first interaction behaves
    intuitively, and an object nobody has ever touched the matrix for
    never gets a persisted entry at all (same "touching it is enough to
    persist it" convention `AgitationEngine::settingsFor()` already
    established).
*/
class RoutingPanel : public juce::Component
{
public:
    // engineToControl: not owned, must outlive this panel
    // (KlangorbitProcessor owns it for its whole lifetime, this panel is
    // owned by a window that closes/hides before the processor does).
    explicit RoutingPanel (TrajectoryEngine& engineToControl);

    void resized() override;
    void paint (juce::Graphics&) override;

    // Rebuilds the whole grid from the engine's current object
    // activation state -- see this class's own comment on why this
    // isn't done automatically/continuously.
    void refreshFromModel();

private:
    void cellToggled (int objIdx, int channel, bool nowOn);
    void resetObjectClicked (int objIdx);

    TrajectoryEngine& engine;

    juce::Label headerLabel;

    std::vector<std::unique_ptr<juce::Label>> objectColumnLabels;   // one per object slot
    std::vector<std::unique_ptr<juce::TextButton>> resetButtons;    // one per object slot
    std::vector<std::unique_ptr<juce::Label>> channelRowLabels;     // one per live input channel
    // cells[channel][objIdx] -- row-major, matching the grid's own visual layout.
    std::vector<std::vector<std::unique_ptr<juce::ToggleButton>>> cells;

    static constexpr int rowLabelWidth = 72;
    static constexpr int cellSize = 30;
    static constexpr int headerRowHeight = 24;
    static constexpr int footerRowHeight = 28;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingPanel)
};
