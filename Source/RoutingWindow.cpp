#include "RoutingWindow.h"
#include "UiTheme.h"

RoutingWindow::RoutingWindow (TrajectoryEngine& engineToControl)
    : juce::DocumentWindow ("Klangorbit -- Input Routing",
                             UiColours::bgPanel(),
                             juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      panel (engineToControl)
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setContentNonOwned (&panel, false);
    centreWithSize (440, 420);

    // Always-on-top -- see MappingWindow.cpp's identical comment for why
    // (a hosted DAW's own window can otherwise end up on top).
    setAlwaysOnTop (true);
    setVisible (true);
}
