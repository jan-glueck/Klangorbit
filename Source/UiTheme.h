#pragma once
#include <juce_graphics/juce_graphics.h>

/**
    Shared design tokens (colours, spacing) for the whole editor GUI --
    PluginEditor, ParameterPanel, ObjectListPanel, and SciFiLookAndFeel all
    read from here instead of each picking its own ad hoc shade of grey, so
    the look stays one cohesive dark/cyan "reduced sci-fi HUD" theme rather
    than drifting apart file by file.

    Functions, not `constexpr juce::Colour` constants: juce::Colour's
    constructor isn't guaranteed usable in a constant-initializer in every
    JUCE version this project might build against, and a plain function
    call costs nothing measurable here (called from paint()/resized(), not
    per-sample audio code).
*/
namespace UiColours
{
    // --- Backgrounds, darkest to lightest -------------------------------
    inline juce::Colour bgDeep()        { return juce::Colour (0xff0a0e13); } // 3D viewport background
    inline juce::Colour bgPanel()       { return juce::Colour (0xff11161d); } // side panels (object list, parameter panel), toolbar
    inline juce::Colour bgRaised()      { return juce::Colour (0xff171d26); } // rows/controls within a panel, one step lighter than the panel itself

    // --- Structure --------------------------------------------------------
    inline juce::Colour border()        { return juce::Colour (0xff232b35); } // hairline dividers between sections/rows
    inline juce::Colour borderBright()  { return juce::Colour (0xff34424f); } // hover/focus state of the above
    inline juce::Colour gridLine()      { return juce::Colour (0xff17232a); } // 3D viewport's ground-reference grid

    // --- Accent -------------------------------------------------------------
    inline juce::Colour accent()        { return juce::Colour (0xff34e5c9); } // primary cyan-teal -- active tab, slider fill, selection ring
    inline juce::Colour accentDim()     { return juce::Colour (0xff1d4a45); } // unfilled slider track, inactive accent-family fill

    // --- Text ---------------------------------------------------------------
    inline juce::Colour textPrimary()   { return juce::Colour (0xffe7eef2); }
    inline juce::Colour textSecondary() { return juce::Colour (0xff81909c); }
    inline juce::Colour textDisabled()  { return juce::Colour (0xff4a5560); }

    // --- Semantic (kept distinct from the accent/object-colour families so
    //     neither can ever visually collide with a Mute/Solo state) --------
    inline juce::Colour mute()          { return juce::Colour (0xffff5c6c); } // red-pink
    inline juce::Colour solo()          { return juce::Colour (0xffffc857); } // amber
}

/** Consistent spacing scale (pixels), used throughout layout code instead
    of ad hoc numbers, so padding reads as deliberate rather than random. */
namespace UiSpacing
{
    constexpr int xs = 4;
    constexpr int s  = 8;
    constexpr int m  = 12;
    constexpr int l  = 16;
    constexpr int xl = 24;
}
