#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
    Standalone top-level window showing HelpContent::text (see
    HelpContent.h) in a scrollable, read-only, monospaced text view --
    opened via KlangorbitEditor's "?" toolbar button.

    A real OS-level DocumentWindow rather than a child component: it needs
    to work identically whether Klangorbit is running as the Standalone
    app or hosted as a VST3 inside a DAW, where the editor itself is
    embedded in the host's own window and has no spare screen space of its
    own to show a large reference document in.

    Lifetime: owned by KlangorbitEditor as a single reusable instance
    (see its helpWindow member) -- closing it just hides it rather than
    destroying it, so reopening is instant and doesn't re-parse/rebuild
    anything.
*/
class HelpWindow : public juce::DocumentWindow
{
public:
    HelpWindow();

    // Fires after this window hides itself (see closeButtonPressed()) --
    // KlangorbitEditor wires this to bring its OWN top-level window back to
    // front (see PluginEditor.cpp's bringEditorToFront()). Needed because
    // hiding an always-on-top window doesn't itself hand focus/z-order back
    // to whatever was behind it in every host -- reported in Logic Pro
    // (AU): the plugin's own editor window sometimes stayed behind other
    // windows after this one closed, only recovering once the editor
    // itself was closed and reopened.
    std::function<void()> onClosed;

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClosed)
            onClosed();
    }

private:
    juce::TextEditor textEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpWindow)
};
