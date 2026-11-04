#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RoutingPanel.h"
#include "TrajectoryEngine.h"

/**
    Standalone top-level window hosting RoutingPanel (the input routing
    matrix) -- opened via the toolbar's Settings menu. Same "real
    OS-level DocumentWindow, works identically Standalone or hosted" and
    "lazily-created, hide-not-destroy" reasoning as MappingWindow/
    OutputWindow/SnapshotWindow (see their own class comments).
*/
class RoutingWindow : public juce::DocumentWindow
{
public:
    explicit RoutingWindow (TrajectoryEngine& engineToControl);

    // See HelpWindow.h's identical member for why this exists.
    std::function<void()> onClosed;

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClosed)
            onClosed();
    }

    // Forwards to RoutingPanel::refreshFromModel() -- called every time
    // this window is (re)shown, see RoutingPanel's own class comment on
    // why it doesn't live-refresh continuously.
    void refreshFromModel() { panel.refreshFromModel(); }

private:
    RoutingPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingWindow)
};
