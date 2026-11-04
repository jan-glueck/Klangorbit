#include "RoutingPanel.h"
#include "InputRouting.h"
#include "UiTheme.h"
#include <algorithm>

RoutingPanel::RoutingPanel (TrajectoryEngine& engineToControl)
    : engine (engineToControl)
{
    headerLabel.setText ("Input Routing Matrix -- tick which input channel(s) feed which object",
                           juce::dontSendNotification);
    headerLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    headerLabel.setFont (juce::Font (13.0f));
    addAndMakeVisible (headerLabel);

    refreshFromModel();
}

void RoutingPanel::refreshFromModel()
{
    const int n = engine.getNumObjects(); // live-input-channel count == object-slot count, see this class's own header comment

    objectColumnLabels.clear();
    resetButtons.clear();
    channelRowLabels.clear();
    cells.clear();

    for (int objIdx = 0; objIdx < n; ++objIdx)
    {
        const bool objectActive = engine.getObject (objIdx).inputChannel >= 0;

        auto columnLabel = std::make_unique<juce::Label>();
        columnLabel->setText ("Obj " + juce::String (objIdx + 1), juce::dontSendNotification);
        columnLabel->setJustificationType (juce::Justification::centred);
        columnLabel->setFont (juce::Font (12.0f));
        columnLabel->setColour (juce::Label::textColourId, objectActive ? UiColours::textPrimary() : UiColours::textDisabled());
        addAndMakeVisible (*columnLabel);
        objectColumnLabels.push_back (std::move (columnLabel));

        auto resetButton = std::make_unique<juce::TextButton> ("Reset");
        resetButton->setEnabled (objectActive);
        resetButton->onClick = [this, objIdx] { resetObjectClicked (objIdx); };
        addAndMakeVisible (*resetButton);
        resetButtons.push_back (std::move (resetButton));
    }

    cells.resize ((size_t) n);
    for (int channel = 0; channel < n; ++channel)
    {
        auto rowLabel = std::make_unique<juce::Label>();
        rowLabel->setText ("Input " + juce::String (channel + 1), juce::dontSendNotification);
        rowLabel->setColour (juce::Label::textColourId, UiColours::textSecondary());
        rowLabel->setFont (juce::Font (12.0f));
        addAndMakeVisible (*rowLabel);
        channelRowLabels.push_back (std::move (rowLabel));

        for (int objIdx = 0; objIdx < n; ++objIdx)
        {
            auto& obj = engine.getObject (objIdx);
            const bool objectActive = obj.inputChannel >= 0;
            const auto resolved = InputRouting::resolveChannels (obj);
            const bool checked = std::find (resolved.begin(), resolved.end(), channel) != resolved.end();

            auto toggle = std::make_unique<juce::ToggleButton>();
            toggle->setToggleState (checked, juce::dontSendNotification);
            toggle->setEnabled (objectActive);
            toggle->onClick = [this, objIdx, channel, toggleRaw = toggle.get()]
            {
                cellToggled (objIdx, channel, toggleRaw->getToggleState());
            };
            addAndMakeVisible (*toggle);
            cells[(size_t) channel].push_back (std::move (toggle));
        }
    }

    resized();
}

void RoutingPanel::cellToggled (int objIdx, int channel, bool nowOn)
{
    auto& obj = engine.getObject (objIdx);

    // Copy-on-write from the resolved default -- see this class's own
    // header comment on why.
    if (obj.routedInputChannels.empty())
        obj.routedInputChannels = InputRouting::resolveChannels (obj);

    auto& list = obj.routedInputChannels;
    if (nowOn)
    {
        if (std::find (list.begin(), list.end(), channel) == list.end())
            list.push_back (channel);
    }
    else
    {
        list.erase (std::remove (list.begin(), list.end(), channel), list.end());
    }
}

void RoutingPanel::resetObjectClicked (int objIdx)
{
    engine.getObject (objIdx).routedInputChannels.clear(); // back to the plain 1:1 identity channel
    refreshFromModel();
}

void RoutingPanel::resized()
{
    auto b = getLocalBounds().reduced (UiSpacing::m);

    headerLabel.setBounds (b.removeFromTop (20));
    b.removeFromTop (UiSpacing::s);

    const int n = (int) objectColumnLabels.size();
    if (n == 0)
        return;

    auto gridArea = b;
    auto headerRow = gridArea.removeFromTop (headerRowHeight);
    headerRow.removeFromLeft (rowLabelWidth);
    for (int objIdx = 0; objIdx < n; ++objIdx)
        objectColumnLabels[(size_t) objIdx]->setBounds (headerRow.removeFromLeft (cellSize));

    for (int channel = 0; channel < n; ++channel)
    {
        auto row = gridArea.removeFromTop (cellSize);
        channelRowLabels[(size_t) channel]->setBounds (row.removeFromLeft (rowLabelWidth));
        for (int objIdx = 0; objIdx < n; ++objIdx)
        {
            auto cellArea = row.removeFromLeft (cellSize);
            // Centre the (fixed-size) checkbox within its own cell rather
            // than stretching it -- juce::ToggleButton's own tick glyph
            // doesn't scale meaningfully past its default size.
            cells[(size_t) channel][(size_t) objIdx]->setBounds (cellArea.withSizeKeepingCentre (24, 24));
        }
    }

    auto footerRow = gridArea.removeFromTop (footerRowHeight);
    footerRow.removeFromLeft (rowLabelWidth);
    for (int objIdx = 0; objIdx < n; ++objIdx)
        resetButtons[(size_t) objIdx]->setBounds (footerRow.removeFromLeft (cellSize).reduced (1));
}

void RoutingPanel::paint (juce::Graphics& g)
{
    g.fillAll (UiColours::bgPanel());
}
