#include "ObjectListPanel.h"

ObjectListPanel::ObjectListPanel()
{
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (titleLabel);

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

        row.selectButton = std::make_unique<juce::TextButton> ("Object " + juce::String (i));
        row.selectButton->onClick = [this, i]
        {
            if (onObjectSelected != nullptr)
                onObjectSelected (i);
        };
        rowContainer.addAndMakeVisible (*row.selectButton);

        row.muteButton = std::make_unique<juce::TextButton> ("M");
        row.muteButton->onClick = [this, i]
        {
            if (engine == nullptr) return;
            auto& obj = engine->getObject (i);
            obj.muted = ! obj.muted;
            updateRowColours();
        };
        rowContainer.addAndMakeVisible (*row.muteButton);

        row.soloButton = std::make_unique<juce::TextButton> ("S");
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

    resized();
    updateRowColours();
}

void ObjectListPanel::setSelectedIndex (int index)
{
    selectedIndex = index;
    updateRowColours();
}

void ObjectListPanel::updateRowColours()
{
    for (auto& row : rows)
    {
        const bool isSelected = (row.objectIndex == selectedIndex);
        row.selectButton->setColour (juce::TextButton::buttonColourId,
                                      isSelected ? juce::Colours::darkslateblue : juce::Colours::darkgrey);
        row.selectButton->setColour (juce::TextButton::textColourOffId,
                                      isSelected ? juce::Colours::white : juce::Colours::lightgrey);

        if (engine != nullptr)
        {
            const auto& obj = engine->getObject (row.objectIndex);
            row.muteButton->setColour (juce::TextButton::buttonColourId,
                                        obj.muted ? juce::Colours::orangered : juce::Colours::darkgrey);
            row.soloButton->setColour (juce::TextButton::buttonColourId,
                                        obj.soloed ? juce::Colours::gold : juce::Colours::darkgrey);
            row.soloButton->setColour (juce::TextButton::textColourOffId,
                                        obj.soloed ? juce::Colours::black : juce::Colours::lightgrey);
        }
    }
}

void ObjectListPanel::resized()
{
    auto bounds = getLocalBounds();
    titleLabel.setBounds (bounds.removeFromTop (24));
    viewport.setBounds (bounds);

    rowContainer.setSize (viewport.getWidth(), juce::jmax (1, (int) rows.size() * rowHeight));

    constexpr int gap = 2;
    int y = 0;
    for (auto& row : rows)
    {
        auto rowBounds = juce::Rectangle<int> (0, y, rowContainer.getWidth(), rowHeight - 2);
        auto soloBounds = rowBounds.removeFromRight (toggleButtonWidth);
        rowBounds.removeFromRight (gap);
        auto muteBounds = rowBounds.removeFromRight (toggleButtonWidth);
        rowBounds.removeFromRight (gap);

        row.selectButton->setBounds (rowBounds);
        row.muteButton->setBounds (muteBounds);
        row.soloButton->setBounds (soloBounds);
        y += rowHeight;
    }
}

void ObjectListPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.3f));
}
