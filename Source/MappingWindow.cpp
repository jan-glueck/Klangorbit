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
    setVisible (true);
}
