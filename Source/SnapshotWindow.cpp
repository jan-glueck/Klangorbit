#include "SnapshotWindow.h"
#include "UiTheme.h"

SnapshotWindow::SnapshotWindow (const ParameterRegistry& registryToShow, SnapshotMorphEngine& morphEngineToControl,
                                  std::function<void()> onRecalled)
    : juce::DocumentWindow ("Klangorbit -- Snapshots",
                             UiColours::bgPanel(),
                             juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      panel (registryToShow, morphEngineToControl, std::move (onRecalled))
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setContentNonOwned (&panel, false);
    centreWithSize (460, 620);

    // Always-on-top -- see MappingWindow.cpp's identical comment for why
    // (a hosted DAW's own window can otherwise end up on top).
    setAlwaysOnTop (true);
    setVisible (true);
}
