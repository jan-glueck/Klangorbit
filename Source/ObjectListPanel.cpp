#include "ObjectListPanel.h"
#include "UiTheme.h"

ObjectListPanel::ObjectListPanel()
{
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    titleLabel.setFont (juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.08f));
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (addButton);
    addButton.onClick = [this] { if (onAddClicked != nullptr) onAddClicked(); };

    addAndMakeVisible (removeButton);
    removeButton.onClick = [this] { if (onRemoveClicked != nullptr) onRemoveClicked(); };

    viewport.setViewedComponent (&rowContainer, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

void ObjectListPanel::refresh (TrajectoryEngine& engineIn)
{
    engine = &engineIn;
    rows.clear();
    rowContainer.removeAllChildren();

    for (int i = 0; i < engineIn.getNumObjects(); ++i)
    {
        if (engineIn.getObject (i).inputChannel < 0)
            continue; // inactive slot -- not listed, matching what's visible/clickable in the scene view

        Row row;
        row.objectIndex = i;

        // Displayed 1-based (i+1) -- purely cosmetic, i itself (the actual
        // 0-based object/array index used everywhere internally, including
        // onObjectSelected below) is completely unaffected.
        row.selectButton = std::make_unique<juce::TextButton> ("Object " + juce::String (i + 1));
        row.selectButton->setClickingTogglesState (false); // toggle state reflects selection, driven by updateRowColours(), not by clicking itself
        row.selectButton->setColour (juce::TextButton::buttonColourId, UiColours::bgRaised());
        row.selectButton->setColour (juce::TextButton::buttonOnColourId, UiColours::accentDim());
        row.selectButton->setColour (juce::TextButton::textColourOffId, UiColours::textPrimary());
        row.selectButton->setColour (juce::TextButton::textColourOnId, UiColours::textPrimary());
        row.selectButton->onClick = [this, i]
        {
            if (onObjectSelected != nullptr)
                onObjectSelected (i);
        };
        rowContainer.addAndMakeVisible (*row.selectButton);

        row.muteButton = std::make_unique<juce::TextButton> ("M");
        row.muteButton->setClickingTogglesState (false); // we flip the model field ourselves, see onClick below
        row.muteButton->setColour (juce::TextButton::buttonColourId, UiColours::bgRaised());
        row.muteButton->setColour (juce::TextButton::buttonOnColourId, UiColours::mute());
        row.muteButton->setColour (juce::TextButton::textColourOffId, UiColours::textSecondary());
        row.muteButton->setColour (juce::TextButton::textColourOnId, UiColours::textPrimary());
        row.muteButton->onClick = [this, i]
        {
            if (engine == nullptr) return;
            auto& obj = engine->getObject (i);
            obj.muted = ! obj.muted;
            updateRowColours();
        };
        rowContainer.addAndMakeVisible (*row.muteButton);

        row.soloButton = std::make_unique<juce::TextButton> ("S");
        row.soloButton->setClickingTogglesState (false);
        row.soloButton->setColour (juce::TextButton::buttonColourId, UiColours::bgRaised());
        row.soloButton->setColour (juce::TextButton::buttonOnColourId, UiColours::solo());
        row.soloButton->setColour (juce::TextButton::textColourOffId, UiColours::textSecondary());
        // Solo's "on" colour (amber) is light, so its own text needs to be
        // dark to stay readable -- unlike mute/select, whose "on" colours
        // are dark enough for the usual light text.
        row.soloButton->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        row.soloButton->onClick = [this, i]
        {
            if (engine == nullptr) return;
            auto& obj = engine->getObject (i);
            obj.soloed = ! obj.soloed;
            updateRowColours();
        };
        rowContainer.addAndMakeVisible (*row.soloButton);

        rows.push_back (std::move (row));
    }

    numActive = engineIn.getNumActiveObjects();
    numTotal = engineIn.getNumObjects();
    titleLabel.setText ("Objects: " + juce::String (numActive) + " / " + juce::String (numTotal), juce::dontSendNotification);
    updateButtonStates();

    resized();
    updateRowColours();
}

void ObjectListPanel::updateButtonStates()
{
    addButton.setEnabled (numActive < numTotal);
    removeButton.setEnabled (selectedIndex >= 0);
}

void ObjectListPanel::setSelectedIndex (int index)
{
    selectedIndex = index;
    updateButtonStates();
    updateRowColours();
}

void ObjectListPanel::updateRowColours()
{
    for (auto& row : rows)
    {
        row.selectButton->setToggleState (row.objectIndex == selectedIndex, juce::dontSendNotification);

        if (engine != nullptr)
        {
            const auto& obj = engine->getObject (row.objectIndex);
            row.muteButton->setToggleState (obj.muted, juce::dontSendNotification);
            row.soloButton->setToggleState (obj.soloed, juce::dontSendNotification);
        }
    }
}

void ObjectListPanel::resized()
{
    auto bounds = getLocalBounds();
    titleLabel.setBounds (bounds.removeFromTop (headerHeight));
    bounds.removeFromTop (UiSpacing::xs);

    // "+"/"-" side by side, one row -- short enough now (see the class
    // comment on why they're bare symbols, not "+ Object"/"- Remove
    // Object") to fit comfortably even in this panel's narrow width.
    auto buttonArea = bounds.removeFromTop (addRemoveButtonHeight).reduced (sidePadding, 0);
    const int buttonWidth = (buttonArea.getWidth() - UiSpacing::xs) / 2;
    addButton.setBounds (buttonArea.removeFromLeft (buttonWidth));
    buttonArea.removeFromLeft (UiSpacing::xs);
    removeButton.setBounds (buttonArea);

    // Explicit spacer/padding before the object row list -- see
    // addRemoveToListGap's own comment.
    bounds.removeFromTop (addRemoveToListGap);

    bounds.removeFromLeft (sidePadding);
    bounds.removeFromRight (sidePadding);
    viewport.setBounds (bounds);

    rowContainer.setSize (viewport.getWidth(), juce::jmax (1, (int) rows.size() * (rowHeight + rowGap)));

    int y = 0;
    for (auto& row : rows)
    {
        auto rowBounds = juce::Rectangle<int> (0, y, rowContainer.getWidth(), rowHeight);
        auto soloBounds = rowBounds.removeFromRight (toggleButtonWidth);
        rowBounds.removeFromRight (UiSpacing::xs);
        auto muteBounds = rowBounds.removeFromRight (toggleButtonWidth);
        rowBounds.removeFromRight (UiSpacing::xs);

        row.selectButton->setBounds (rowBounds);
        row.muteButton->setBounds (muteBounds);
        row.soloButton->setBounds (soloBounds);
        y += rowHeight + rowGap;
    }
}

void ObjectListPanel::paint (juce::Graphics& g)
{
    g.fillAll (UiColours::bgPanel());

    g.setColour (UiColours::border());
    g.fillRect (0, contentTopHeight - 1, getWidth(), 1); // divider under the header (title + add/remove buttons)
    g.fillRect (getWidth() - 1, 0, 1, getHeight());  // right edge, separates the panel from the 3D viewport
}
