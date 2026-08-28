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

    Also owns the "+"/"-" (add/remove object) buttons and the "Objects:
    N / M" count display (moved here from the toolbar -- this is where
    they act, so this is where they live). Deliberately just "+"/"-", not
    "+ Object"/"- Remove Object" -- the two labels used to be different
    lengths/phrasing for what's really one symmetric pair of actions;
    bare symbols read as a matched pair and fit comfortably side by side
    in this panel's narrow width, where the old longer labels didn't.
    Same "fires a callback, PluginEditor does the actual work" pattern as
    onObjectSelected: onAddClicked/onRemoveClicked wire straight to the
    editor's existing addObjectClicked()/removeObjectClicked().
    addButton/removeButton's enabled state and the count text are both
    kept in sync by refresh() (active/total counts) and
    setSelectedIndex() (whether Remove has anything to act on) -- see
    updateButtonStates().

    Only lists active objects (inputChannel >= 0), matching what's
    visible/clickable in the scene view -- not grains, which have no
    individual selection concept (see ParameterPanel's Grains
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

    // See the class comment. Public, like onObjectSelected above --
    // PluginEditor wires these directly to its existing
    // addObjectClicked()/removeObjectClicked().
    std::function<void()> onAddClicked;
    std::function<void()> onRemoveClicked;

private:
    void updateButtonStates(); // addButton/removeButton enabled state -- see the class comment

    static constexpr int headerHeight = 34;
    static constexpr int addRemoveButtonHeight = 28;
    // Explicit spacer/padding between the +/- row and the object row
    // list below it, per an explicit request -- visually separates
    // "controls" from "content" rather than the list starting right
    // under the buttons.
    static constexpr int addRemoveToListGap = 14;
    // headerHeight + the single add/remove button row + the gap above
    // (see resized()) -- where the header/divider ends and the
    // scrollable row list begins. A plain literal for the 4px gap
    // between header and buttons (matching UiSpacing::xs's own value)
    // rather than pulling in UiTheme.h just for a constexpr used in
    // exactly one place.
    static constexpr int contentTopHeight = headerHeight + 4 + addRemoveButtonHeight + addRemoveToListGap;
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
    // Cached from the most recent refresh() -- setSelectedIndex() needs
    // numTotal (via updateButtonStates()) to re-derive addButton's
    // enabled state too, but only refresh() actually sees the engine.
    int numActive = 0;
    int numTotal = 0;

    // Not owned -- valid only because refresh() is called again by
    // PluginEditor any time the object set could change; used solely to
    // read current muted/soloed state for the button colours and to
    // toggle it on click, both message-thread-only.
    TrajectoryEngine* engine = nullptr;

    juce::Viewport viewport;
    juce::Component rowContainer; // hosted inside viewport, holds the actual row buttons
    // Text set dynamically by refresh() to "Objects: N / M" -- see the
    // class comment on why the count display lives here now.
    juce::Label titleLabel { {}, "Objects" };
    juce::TextButton addButton { "+" };
    juce::TextButton removeButton { "-" };

    void updateRowColours();
};
