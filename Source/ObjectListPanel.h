#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "TrajectoryEngine.h"

/**
    Sidebar list of active SoundObjects, clickable to select -- an
    alternative to clicking an object directly in the 3D scene view, for
    objects that are small, far away, or moving too fast to reliably hit
    with the mouse (PluginEditor::findObjectNear()'s fixed screen-space hit
    radius doesn't help if the object simply isn't under the cursor at
    all).

    This panel does NOT own selection state -- PluginEditor's
    selectedObjectIndex remains the single source of truth (same as
    before this panel existed). A row click here calls onObjectSelected,
    which PluginEditor wires straight to its existing selectObject() --
    the same method a scene-view click already used, so the resulting
    highlight (in paint()) and ParameterPanel wiring are identical either
    way. setSelectedIndex() reflects a selection made *elsewhere* (e.g. a
    scene-view click) back into this list's own highlight, without
    re-firing onObjectSelected (which would be a redundant round trip).

    Only lists active objects (inputChannel >= 0), matching what's
    visible/clickable in the scene view -- not grains, which have no
    individual selection concept (see ParameterPanel's Grain Cloud
    category: one parameter set per cloud, not per grain).
*/
class ObjectListPanel : public juce::Component
{
public:
    ObjectListPanel();

    // Rebuilds the row list from the engine's current active objects --
    // call after anything that can change which objects are active
    // (activate/deactivate, preset load) or their id ordering.
    void refresh (TrajectoryEngine& engine);

    // Reflects an externally-driven selection (e.g. a scene-view click)
    // into this list's highlight. -1 clears the highlight. Does NOT call
    // onObjectSelected.
    void setSelectedIndex (int index);

    void resized() override;
    void paint (juce::Graphics&) override;

    // Fired when the user clicks a row; index is always a currently-active
    // object's id (this panel never offers to select an inactive slot).
    std::function<void (int)> onObjectSelected;

private:
    static constexpr int rowHeight = 28;

    struct Row
    {
        int objectIndex = -1;
        std::unique_ptr<juce::TextButton> button;
    };
    std::vector<Row> rows;
    int selectedIndex = -1;

    juce::Viewport viewport;
    juce::Component rowContainer; // hosted inside viewport, holds the actual row buttons
    juce::Label titleLabel { {}, "Objects" };

    void updateRowColours();
};
