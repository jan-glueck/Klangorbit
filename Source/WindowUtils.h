#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
    Shared "actually bring this auxiliary window to front" helper for
    HelpWindow/MappingWindow/OutputWindow -- the toolbar's Help/Mappings/
    Output buttons all lazily create-or-reshow one of these, and all three
    hit the identical bug this exists to fix: once such a window has
    fallen behind the plugin's own host-provided editor window (e.g. the
    user clicks back on the editor), a plain toFront() call -- even
    repeated synchronously and again one message-loop iteration later via
    MessageManager::callAsync() -- sometimes doesn't win the ordering race
    back, reported in Logic Pro (AU) specifically but not treated as
    host-specific here, since the underlying cause isn't: each window's
    own setAlwaysOnTop(true) (set once, at construction) elevates it
    above NORMAL windows, but once the plugin's own editor window is
    ALSO effectively elevated (common for hosted plugin windows) and gets
    reactivated by the user's click, a same-level "most recently
    activated wins" tiebreak can leave our window behind it -- and a
    same-level toFront() alone doesn't reliably resolve that tie on every
    platform/host.

    Fix: toggle setAlwaysOnTop() off then back on before calling
    toFront(). Many window systems only take real action on the
    ON/OFF TRANSITION of an elevated window level, not on reasserting the
    same value again -- forcing that transition reliably re-wins the
    ordering race where a bare toFront() sometimes doesn't. Applied
    uniformly across every format (AU/VST3/Standalone) and every
    auxiliary window, not just where first reported, since the mechanism
    isn't host-specific.
*/
namespace WindowUtils
{
    inline void forceToFront (juce::DocumentWindow& window)
    {
        window.setVisible (true);
        window.setAlwaysOnTop (false);
        window.setAlwaysOnTop (true);
        window.toFront (true);
    }
}
