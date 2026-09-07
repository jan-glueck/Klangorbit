#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MappingPanel.h"
#include "MappingEngine.h"
#include "ParameterRegistry.h"

/**
    Standalone top-level window hosting MappingPanel (Learn-mode
    controller mapping UI) -- opened via KlangorbitEditor's toolbar.

    A real OS-level DocumentWindow, same reasoning as HelpWindow (see its
    own class comment): needs to work identically whether Klangorbit is
    the Standalone app or hosted as a VST3 embedded in a DAW's own
    window, which has no spare space for a control panel like this one.

    Lifetime: owned by KlangorbitEditor as a single reusable instance --
    closing it just hides it (see closeButtonPressed()) rather than
    destroying it, so reopening is instant.
*/
class MappingWindow : public juce::DocumentWindow
{
public:
    MappingWindow (const ParameterRegistry& registryToShow, MappingEngine& engineToControl);

    // See HelpWindow.h's identical member for why this exists.
    std::function<void()> onClosed;

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClosed)
            onClosed();
    }

private:
    MappingPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MappingWindow)
};
