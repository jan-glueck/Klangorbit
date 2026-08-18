#include "SciFiLookAndFeel.h"
#include "UiTheme.h"

SciFiLookAndFeel::SciFiLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, UiColours::bgDeep());

    setColour (juce::TextButton::buttonColourId, UiColours::bgRaised());
    setColour (juce::TextButton::buttonOnColourId, UiColours::accentDim());
    setColour (juce::TextButton::textColourOffId, UiColours::textPrimary());
    setColour (juce::TextButton::textColourOnId, UiColours::textPrimary());

    setColour (juce::Slider::backgroundColourId, UiColours::bgRaised());
    setColour (juce::Slider::trackColourId, UiColours::accent());
    setColour (juce::Slider::thumbColourId, UiColours::textPrimary());
    setColour (juce::Slider::textBoxTextColourId, UiColours::textPrimary());
    setColour (juce::Slider::textBoxBackgroundColourId, UiColours::bgRaised());
    setColour (juce::Slider::textBoxOutlineColourId, UiColours::border());
    setColour (juce::Slider::textBoxHighlightColourId, UiColours::accentDim());

    setColour (juce::ComboBox::backgroundColourId, UiColours::bgRaised());
    setColour (juce::ComboBox::textColourId, UiColours::textPrimary());
    setColour (juce::ComboBox::outlineColourId, UiColours::border());
    setColour (juce::ComboBox::arrowColourId, UiColours::accent());
    setColour (juce::ComboBox::buttonColourId, UiColours::bgRaised());

    setColour (juce::PopupMenu::backgroundColourId, UiColours::bgPanel());
    setColour (juce::PopupMenu::textColourId, UiColours::textPrimary());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, UiColours::accentDim());
    setColour (juce::PopupMenu::highlightedTextColourId, UiColours::textPrimary());

    setColour (juce::Label::textColourId, UiColours::textPrimary());

    setColour (juce::ToggleButton::textColourId, UiColours::textPrimary());
    setColour (juce::ToggleButton::tickColourId, UiColours::accent());
    setColour (juce::ToggleButton::tickDisabledColourId, UiColours::textDisabled());

    setColour (juce::ScrollBar::thumbColourId, UiColours::borderBright());
    setColour (juce::ScrollBar::trackColourId, UiColours::bgPanel());

    setColour (juce::AlertWindow::backgroundColourId, UiColours::bgPanel());
    setColour (juce::AlertWindow::textColourId, UiColours::textPrimary());
    setColour (juce::AlertWindow::outlineColourId, UiColours::border());

    setColour (juce::TextEditor::backgroundColourId, UiColours::bgRaised());
    setColour (juce::TextEditor::textColourId, UiColours::textPrimary());
    setColour (juce::TextEditor::outlineColourId, UiColours::border());
    setColour (juce::TextEditor::focusedOutlineColourId, UiColours::accent());

    setColour (juce::FileChooserDialogBox::backgroundColourId, UiColours::bgPanel());
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, UiColours::accentDim());
    setColour (juce::ListBox::backgroundColourId, UiColours::bgPanel());
    setColour (juce::ListBox::textColourId, UiColours::textPrimary());
}

void SciFiLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    constexpr float cornerSize = 3.0f;

    juce::Colour fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.15f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, cornerSize);

    const bool active = button.getToggleState();
    g.setColour (active ? UiColours::accent()
                         : (shouldDrawButtonAsHighlighted ? UiColours::borderBright() : UiColours::border()));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerSize, 1.0f);

    // Active-tab affordance for toggle-driven buttons (e.g. ParameterPanel's
    // category tabs) -- a thin accent underline, distinct from a plain
    // recoloured background so "currently selected" reads unambiguously
    // rather than just looking like a slightly different button colour.
    if (active)
    {
        constexpr float stripeHeight = 2.0f;
        g.setColour (UiColours::accent());
        g.fillRect (bounds.removeFromBottom (stripeHeight));
    }
}

void SciFiLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    // Only the horizontal style is used anywhere in this app (see
    // ParameterPanel's row components) -- fall back to the stock look for
    // anything else rather than guessing at a vertical/rotary layout that
    // was never exercised here.
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const bool enabled = slider.isEnabled();

    constexpr float trackHeight = 4.0f;
    const float trackY = (float) y + (float) height * 0.5f - trackHeight * 0.5f;
    const float left = juce::jmin (minSliderPos, maxSliderPos);
    const float right = juce::jmax (minSliderPos, maxSliderPos);

    g.setColour (enabled ? UiColours::accentDim() : UiColours::bgRaised());
    g.fillRoundedRectangle (left, trackY, right - left, trackHeight, trackHeight * 0.5f);

    const float filledWidth = juce::jlimit (0.0f, right - left, sliderPos - left);
    g.setColour (enabled ? UiColours::accent() : UiColours::textDisabled());
    g.fillRoundedRectangle (left, trackY, filledWidth, trackHeight, trackHeight * 0.5f);

    // Thin vertical tick as the thumb, rather than a filled circular knob --
    // reads as a technical readout rather than a physical dial.
    constexpr float thumbWidth = 2.5f;
    constexpr float thumbOverhang = 4.0f; // extends a little above/below the track
    g.setColour (enabled ? UiColours::textPrimary() : UiColours::textDisabled());
    g.fillRoundedRectangle (sliderPos - thumbWidth * 0.5f, trackY - thumbOverhang,
                             thumbWidth, trackHeight + thumbOverhang * 2.0f, thumbWidth * 0.5f);
}

void SciFiLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();

    constexpr float switchWidth = 36.0f;
    constexpr float switchHeight = 16.0f;
    const juce::Rectangle<float> switchArea (bounds.getX(), bounds.getCentreY() - switchHeight * 0.5f,
                                              switchWidth, switchHeight);

    g.setColour (! enabled ? UiColours::bgRaised() : (on ? UiColours::accent() : UiColours::bgRaised()));
    g.fillRoundedRectangle (switchArea, switchHeight * 0.5f);

    g.setColour (enabled && shouldDrawButtonAsHighlighted ? UiColours::borderBright() : UiColours::border());
    g.drawRoundedRectangle (switchArea.reduced (0.5f), switchHeight * 0.5f, 1.0f);

    constexpr float knobDiameter = 12.0f;
    constexpr float knobInset = 2.0f;
    const float knobX = on ? (switchArea.getRight() - knobInset - knobDiameter)
                            : (switchArea.getX() + knobInset);
    g.setColour (enabled ? UiColours::textPrimary() : UiColours::textDisabled());
    g.fillEllipse (knobX, switchArea.getCentreY() - knobDiameter * 0.5f, knobDiameter, knobDiameter);

    const float textX = switchArea.getRight() + (float) UiSpacing::s;
    g.setColour (enabled ? UiColours::textPrimary() : UiColours::textDisabled());
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawFittedText (button.getButtonText(), (int) textX, 0,
                       juce::jmax (0, (int) (bounds.getRight() - textX)), (int) bounds.getHeight(),
                       juce::Justification::centredLeft, 1);
}
