#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Vec3.h"

/**
    Generic row widgets (label + one control, with a plain callback) --
    extracted out of ParameterPanel.h so that other, independently-
    layouting components (e.g. ModifierStackEditor.h, which needs these
    same widgets but must NOT #include ParameterPanel.h, to avoid a
    circular include -- ParameterPanel.h holds a ModifierStackEditor
    member) can reuse them too. Binding any of these to a concrete
    model field still happens exclusively in whichever component
    constructs it (ParameterPanel's add*Row() helpers, ModifierRowComponent),
    never here.
*/

class FloatRowComponent : public juce::Component
{
public:
    FloatRowComponent (const juce::String& name, double min, double max, double step);

    void resized() override;
    void setValueQuiet (float v);

    // Double-clicking the slider resets it to v (juce::Slider's own
    // built-in double-click-return mechanism -- no custom mouse handling
    // needed). Not called from this class's own constructor since the
    // "default" value is schema-specific and only the caller knows which
    // field this row is even bound to.
    void setDefaultValue (float v) { slider.setDoubleClickReturnValue (true, (double) v); }

    static constexpr int preferredHeight = 42;

    juce::Label label;
    juce::Slider slider;
    std::function<void (float)> onValueChanged;
};

/** Like FloatRowComponent, but for Vec3 (three axis sliders stacked vertically). */
class Vec3RowComponent : public juce::Component
{
public:
    Vec3RowComponent (const juce::String& name, double min, double max, double step);

    void resized() override;
    void setValueQuiet (Vec3 v);

    // See FloatRowComponent::setDefaultValue() -- same idea, one default
    // per axis.
    void setDefaultValue (Vec3 v)
    {
        xSlider.setDoubleClickReturnValue (true, (double) v.x);
        ySlider.setDoubleClickReturnValue (true, (double) v.y);
        zSlider.setDoubleClickReturnValue (true, (double) v.z);
    }

    static constexpr int preferredHeight = 18 + 3 * 22;

    juce::Label label, xLabel, yLabel, zLabel;
    juce::Slider xSlider, ySlider, zSlider;
    std::function<void (Vec3)> onValueChanged;

private:
    void setupAxis (juce::Label& l, juce::Slider& s, const juce::String& axisName,
                    double min, double max, double step);
};

/** Label + ComboBox, with a generic onSelected(itemIndex) callback. */
class ComboRowComponent : public juce::Component
{
public:
    explicit ComboRowComponent (const juce::String& name);

    void resized() override;

    static constexpr int preferredHeight = 44;

    juce::Label label;
    juce::ComboBox combo;
    std::function<void (int)> onSelected;
};

/** A single checkbox-style row, with a generic onToggled(newState) callback. */
class ToggleRowComponent : public juce::Component
{
public:
    explicit ToggleRowComponent (const juce::String& name);

    void resized() override;
    void setValueQuiet (bool v);

    static constexpr int preferredHeight = 28;

    juce::ToggleButton toggle;
    std::function<void (bool)> onToggled;
};

/**
    Shows a current randomization seed (read-only) plus a "New Seed"
    button that assigns it a fresh, genuinely random value via
    onNewSeed -- the UI counterpart to feature/randomization-engine's
    seed-based reproducibility: once a seed is set (by this button, or
    by loading a preset that saved one), the exact pattern reloads
    identically every time, until this button is pressed again.
*/
class SeedRowComponent : public juce::Component
{
public:
    explicit SeedRowComponent (const juce::String& name);

    void resized() override;
    void setSeedValue (juce::int64 seed);

    static constexpr int preferredHeight = 42;

    juce::Label label;
    juce::Label valueLabel;
    juce::TextButton newSeedButton { "New Seed" };
    std::function<void()> onNewSeed;
};
