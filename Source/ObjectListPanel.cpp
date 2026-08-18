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

void ObjectListPanel::refresh (TrajectoryEngine& engine)
{
    rows.clear();
    rowContainer.removeAllChildren();

    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        if (engine.getObject (i).inputChannel < 0)
            continue; // inactive slot -- not listed, matching what's visible/clickable in the scene view

        Row row;
        row.objectIndex = i;
        row.button = std::make_unique<juce::TextButton> ("Object " + juce::String (i));
        row.button->onClick = [this, i]
        {
            if (onObjectSelected != nullptr)
                onObjectSelected (i);
        };
        rowContainer.addAndMakeVisible (*row.button);
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
        row.button->setColour (juce::TextButton::buttonColourId,
                                isSelected ? juce::Colours::darkslateblue : juce::Colours::darkgrey);
        row.button->setColour (juce::TextButton::textColourOffId,
                                isSelected ? juce::Colours::white : juce::Colours::lightgrey);
    }
}

void ObjectListPanel::resized()
{
    auto bounds = getLocalBounds();
    titleLabel.setBounds (bounds.removeFromTop (24));
    viewport.setBounds (bounds);

    rowContainer.setSize (viewport.getWidth(), juce::jmax (1, (int) rows.size() * rowHeight));

    int y = 0;
    for (auto& row : rows)
    {
        row.button->setBounds (0, y, rowContainer.getWidth(), rowHeight - 2);
        y += rowHeight;
    }
}

void ObjectListPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.3f));
}
