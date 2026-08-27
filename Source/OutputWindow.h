#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "OutputPanel.h"

class KlangorbitProcessor;

/**
    Standalone top-level window hosting OutputPanel (Output Format / Bass
    Management / Circular Array speaker count) -- opened via
    KlangorbitEditor's toolbar, alongside Help and Mappings.

    A real OS-level DocumentWindow, same reasoning as HelpWindow/
    MappingWindow (see their own class comments): needs to work
    identically whether Klangorbit is the Standalone app or hosted as a
    VST3 embedded in a DAW's own window, which has no spare space for a
    control panel like this one.

    Lifetime: owned by KlangorbitEditor as a single reusable instance --
    closing it just hides it (see closeButtonPressed()) rather than
    destroying it, so reopening is instant.
*/
class OutputWindow : public juce::DocumentWindow
{
public:
    explicit OutputWindow (KlangorbitProcessor& processorToControl);

    void closeButtonPressed() override { setVisible (false); }

    // Re-syncs the controls with the current model state -- call every
    // time the window is (re)shown, see OutputPanel::refreshFromModel().
    void refreshFromModel() { panel.refreshFromModel(); }

private:
    OutputPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputWindow)
};
