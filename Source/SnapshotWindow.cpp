#include "SnapshotWindow.h"
#include "UiTheme.h"

SnapshotWindow::SnapshotWindow (const ParameterRegistry& registryToShow, std::function<void()> onRecalled)
    : juce::DocumentWindow ("Klangorbit -- Snapshots",
                             UiColours::bgPanel(),
                             juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      panel (registryToShow, std::move (onRecalled))
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setContentNonOwned (&panel, false);
    centreWithSize (420, 420);

    // Always-on-top -- see MappingWindow.cpp's identical comment for why
    // (a hosted DAW's own window can otherwise end up on top).
    setAlwaysOnTop (true);
    setVisible (true);
}
