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

    Each row also has its own Mute/Solo buttons (SoundObject::muted/soloed) --
    this is the only place in the GUI those two fields are exposed; there is
    deliberately no separate checkbox for them in ParameterPanel's Object
    category, one control surface per feature. The buttons write directly
    into the SoundObject held by the TrajectoryEngine reference passed to
    refresh() (stored for the lifetime of the current row set), same
    message-thread-only access rule as everywhere else in the GUI --
    PluginProcessor's audio thread reads muted/soloed itself every block,
    no callback/round trip needed here.
*/
class ObjectListPanel : public juce::Component
{
public:
    ObjectListPanel();

    // Rebuilds the row list from the engine's current active objects --
    // call after anything that can change which objects are active
    // (activate/deactivate, preset load) or their id ordering, or their
    // muted/soloed state (e.g. after loading a preset).
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
    static constexpr int headerHeight = 34;
    static constexpr int rowHeight = 34;
    static constexpr int rowGap = 4;
    static constexpr int toggleButtonWidth = 26;
    static constexpr int sidePadding = 8;

    struct Row
    {
        int objectIndex = -1;
        std::unique_ptr<juce::TextButton> selectButton;
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
    };
    std::vector<Row> rows;
    int selectedIndex = -1;

    // Not owned -- valid only because refresh() is called again by
    // PluginEditor any time the object set could change; used solely to
    // read current muted/soloed state for the button colours and to
    // toggle it on click, both message-thread-only.
    TrajectoryEngine* engine = nullptr;

    juce::Viewport viewport;
    juce::Component rowContainer; // hosted inside viewport, holds the actual row buttons
    juce::Label titleLabel { {}, "Objects" };

    void updateRowColours();
};
