#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SnapshotPanel.h"
#include "ParameterRegistry.h"
#include "SnapshotMorphEngine.h"

/**
    Standalone top-level window hosting SnapshotPanel -- opened via
    KlangorbitEditor's toolbar. Same "real OS-level DocumentWindow,
    works identically Standalone or hosted" reasoning as MappingWindow/
    HelpWindow (see their own class comments).

    Lifetime: owned by KlangorbitEditor as a single reusable instance --
    closing it just hides it (see closeButtonPressed()) rather than
    destroying it, same as MappingWindow/HelpWindow/OutputWindow.
*/
class SnapshotWindow : public juce::DocumentWindow
{
public:
    // registryToShow/morphEngineToControl/onRecalled: see SnapshotPanel's
    // own constructor comment -- forwarded straight through.
    SnapshotWindow (const ParameterRegistry& registryToShow, SnapshotMorphEngine& morphEngineToControl,
                     std::function<void()> onRecalled);

    // See HelpWindow.h's identical member for why this exists.
    std::function<void()> onClosed;

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClosed)
            onClosed();
    }

private:
    SnapshotPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapshotWindow)
};
