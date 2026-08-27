#include "HelpWindow.h"
#include "HelpContent.h"
#include "UiTheme.h"

HelpWindow::HelpWindow()
    : juce::DocumentWindow ("Klangorbit -- Help",
                             UiColours::bgPanel(),
                             juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    textEditor.setMultiLine (true, true);
    textEditor.setReadOnly (true);
    textEditor.setScrollbarsShown (true);
    textEditor.setCaretVisible (false);
    // Monospaced: HelpContent.h's text relies on fixed-width alignment
    // (section dividers, two-column parameter/description layout) to stay
    // readable, same reasoning a plain-text man page uses a fixed-width font.
    textEditor.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    textEditor.setColour (juce::TextEditor::backgroundColourId, UiColours::bgDeep());
    textEditor.setColour (juce::TextEditor::textColourId, UiColours::textPrimary());
    textEditor.setColour (juce::TextEditor::outlineColourId, UiColours::border());
    textEditor.setColour (juce::TextEditor::focusedOutlineColourId, UiColours::border());
    textEditor.setText (HelpContent::text, juce::dontSendNotification);
    textEditor.setCaretPosition (0); // scroll to the top, not wherever setText() left it

    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setContentNonOwned (&textEditor, false);
    centreWithSize (640, 760);

    // Always-on-top -- see MappingWindow.cpp's own comment for why (a
    // hosted VST3's auxiliary window can otherwise open behind the DAW's
    // own window in some hosts, e.g. Reaper).
    setAlwaysOnTop (true);
    setVisible (true);
}
