#include "MappingWindow.h"
#include "UiTheme.h"

MappingWindow::MappingWindow (const ParameterRegistry& registryToShow, MappingEngine& engineToControl)
    : juce::DocumentWindow ("Klangorbit -- Controller Mapping",
                             UiColours::bgPanel(),
                             juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      panel (registryToShow, engineToControl)
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setContentNonOwned (&panel, false);
    centreWithSize (520, 560);

    // Always-on-top: without this, hosted as a VST3 in some DAWs (Reaper,
    // confirmed by a user report), this window's peer can end up behind
    // the host's own main window even after setVisible()/toFront() below --
    // toFront() only reorders within this app's own window stack, but a
    // plugin process's auxiliary window and the host's own window aren't
    // necessarily at the same OS window level to begin with, so reordering
    // alone doesn't guarantee it actually surfaces. A small reference/
    // settings window like this one floating above the host is an accepted
    // tradeoff for a plugin utility window, not just a workaround.
    setAlwaysOnTop (true);
    setVisible (true);
}
