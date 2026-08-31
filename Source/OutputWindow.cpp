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
    // Tall enough to fit every row including the Binaural HRTF-dataset
    // controls (only shown while Output Format == Binaural, see
    // OutputPanel::updateBinauralRowsVisibility()) without needing a
    // manual resize when switching into that mode -- just extra empty
    // space below Bass Management for every other mode. Still resizable,
    // so a user who wants it smaller for the common non-Binaural case can
    // shrink it themselves.
    centreWithSize (460, 490);

    // Always-on-top -- see MappingWindow.cpp's own comment for why (a
    // hosted VST3's auxiliary window can otherwise open behind the DAW's
    // own window in some hosts, e.g. Reaper).
    setAlwaysOnTop (true);
    setVisible (true);
}
