#include "OutputWindow.h"
#include "UiTheme.h"
#include "PluginProcessor.h"

OutputWindow::OutputWindow (KlangorbitProcessor& processorToControl)
    : juce::DocumentWindow ("Klangorbit -- Output",
                             UiColours::bgPanel(),
                             juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    panel.setProcessor (&processorToControl);
    panel.refreshFromModel();

    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setContentNonOwned (&panel, false);
    centreWithSize (460, 320);

    // Always-on-top -- see MappingWindow.cpp's own comment for why (a
    // hosted VST3's auxiliary window can otherwise open behind the DAW's
    // own window in some hosts, e.g. Reaper).
    setAlwaysOnTop (true);
    setVisible (true);
}
