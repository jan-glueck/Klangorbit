#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
    App-wide dark, cyan-accented, reduced "sci-fi HUD" look, applied once to
    the top-level editor (see PluginEditor's constructor) so every child
    Component inherits it by default instead of each widget type being
    styled ad hoc.

    Built mostly on LookAndFeel_V4's own colour-ID mechanism (safe,
    well-tested colour cascading -- see the constructor) plus three
    deliberately simple, hand-drawn overrides where the stock V4 shapes
    didn't fit the brief (flat buttons with an active-tab underline, a
    thin-track slider instead of a filled pill + circular knob, and a
    pill-style toggle switch instead of a checkbox tick). Kept deliberately
    minimal: geometry bugs in custom LookAndFeel drawing code are hard to
    catch without an interactive GUI session (not available in this
    environment, see CHANGELOG's Known limitations), so anything not
    clearly worth that risk was left as V4's default, just recoloured via
    setColour() below.
*/
class SciFiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SciFiLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPos, float minSliderPos, float maxSliderPos,
                            juce::Slider::SliderStyle style, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};
