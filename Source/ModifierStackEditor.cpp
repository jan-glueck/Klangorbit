#include "ModifierStackEditor.h"
#include "UiTheme.h"

// ============================================================== ModifierRowComponent

ModifierRowComponent::ModifierRowComponent (std::function<std::vector<Modifier>*()> ownerStackIn, int indexIn, bool allowPitchIn,
                                              std::function<void()> onMoveUp, std::function<void()> onMoveDown,
                                              std::function<void()> onRemove, std::function<void()> onHeightChanged)
    : ownerStack (std::move (ownerStackIn)), index (indexIn), allowPitch (allowPitchIn),
      onHeightChangedCallback (std::move (onHeightChanged))
{
    styleRowLabel (indexLabel, juce::String (index + 1) + ".", 13.0f, UiColours::textSecondary());
    addAndMakeVisible (indexLabel);

    enabledToggle.setColour (juce::ToggleButton::textColourId, UiColours::textSecondary());
    enabledToggle.onClick = [this] { if (auto* m = current()) m->enabled = enabledToggle.getToggleState(); };
    addAndMakeVisible (enabledToggle);

    typeCombo.addItem ("Gain", 1);
    typeCombo.addItem ("Time Offset", 2);
    typeCombo.addItem ("Filter", 3);
    if (allowPitch)
        typeCombo.addItem ("Pitch", 4);
    typeCombo.onChange = [this]
    {
        if (auto* m = current())
            m->type = (ModifierType) typeCombo.getSelectedItemIndex();
        refreshTypeVisibility();
        if (onHeightChangedCallback)
            onHeightChangedCallback();
    };
    addAndMakeVisible (typeCombo);

    upButton.setButtonText ("Up");
    upButton.onClick = onMoveUp;
    addAndMakeVisible (upButton);

    downButton.setButtonText ("Down");
    downButton.onClick = onMoveDown;
    addAndMakeVisible (downButton);

    removeButton.setButtonText ("Remove");
    removeButton.onClick = onRemove;
    addAndMakeVisible (removeButton);

    gainRow = std::make_unique<FloatRowComponent> ("Gain", 0.0, 4.0, 0.01);
    gainRow->onValueChanged = [this] (float v) { if (auto* m = current()) m->gain.base = v; };
    addAndMakeVisible (*gainRow);

    gainSpreadRow = std::make_unique<FloatRowComponent> ("Gain Spread", 0.0, 2.0, 0.01);
    gainSpreadRow->onValueChanged = [this] (float v) { if (auto* m = current()) m->gain.spread = v; };
    addAndMakeVisible (*gainSpreadRow);

    timeOffsetRow = std::make_unique<FloatRowComponent> ("Time Offset (s)", -2.0, 2.0, 0.01);
    timeOffsetRow->onValueChanged = [this] (float v) { if (auto* m = current()) m->timeOffset.base = v; };
    addAndMakeVisible (*timeOffsetRow);

    timeOffsetSpreadRow = std::make_unique<FloatRowComponent> ("Time Offset Spread (s)", 0.0, 2.0, 0.01);
    timeOffsetSpreadRow->onValueChanged = [this] (float v) { if (auto* m = current()) m->timeOffset.spread = v; };
    addAndMakeVisible (*timeOffsetSpreadRow);

    filterTypeRow = std::make_unique<ComboRowComponent> ("Filter Type");
    filterTypeRow->combo.addItem ("Low-Pass", 1);
    filterTypeRow->combo.addItem ("High-Pass", 2);
    filterTypeRow->combo.addItem ("Band-Pass", 3);
    filterTypeRow->combo.addItem ("Notch", 4);
    filterTypeRow->onSelected = [this] (int i) { if (auto* m = current()) m->filter.type = (VoiceFilterType) i; };
    addAndMakeVisible (*filterTypeRow);

    filterCutoffRow = std::make_unique<FloatRowComponent> ("Filter Cutoff (Hz)", (double) VoiceFilterLimits::minCutoffHz, 20000.0, 1.0);
    filterCutoffRow->onValueChanged = [this] (float v) { if (auto* m = current()) m->filter.cutoffHz.base = v; };
    addAndMakeVisible (*filterCutoffRow);

    filterResonanceRow = std::make_unique<FloatRowComponent> ("Filter Resonance (Q)", (double) VoiceFilterLimits::minResonance, (double) VoiceFilterLimits::maxResonance, 0.01);
    filterResonanceRow->onValueChanged = [this] (float v) { if (auto* m = current()) m->filter.resonance.base = v; };
    addAndMakeVisible (*filterResonanceRow);

    filterCutoffJitterRow = std::make_unique<FloatRowComponent> ("Filter Cutoff Jitter (Hz)", 0.0, 10000.0, 1.0);
    filterCutoffJitterRow->onValueChanged = [this] (float v) { if (auto* m = current()) m->filter.cutoffHz.spread = v; };
    addAndMakeVisible (*filterCutoffJitterRow);

    pitchRow = std::make_unique<FloatRowComponent> ("Pitch (semitones)", -24.0, 24.0, 0.1);
    pitchRow->onValueChanged = [this] (float v) { if (auto* m = current()) m->pitchSemitones.base = v; };
    addAndMakeVisible (*pitchRow);

    pitchSpreadRow = std::make_unique<FloatRowComponent> ("Pitch Spread (semitones)", 0.0, 24.0, 0.1);
    pitchSpreadRow->onValueChanged = [this] (float v) { if (auto* m = current()) m->pitchSemitones.spread = v; };
    addAndMakeVisible (*pitchSpreadRow);

    refreshFromModel();
}

Modifier* ModifierRowComponent::current() const
{
    auto* stack = ownerStack ? ownerStack() : nullptr;
    if (stack == nullptr || index < 0 || index >= (int) stack->size())
        return nullptr;
    return &(*stack)[(size_t) index];
}

void ModifierRowComponent::refreshFromModel()
{
    auto* m = current();
    if (m == nullptr)
        return;

    enabledToggle.setToggleState (m->enabled, juce::dontSendNotification);
    typeCombo.setSelectedItemIndex ((int) m->type, juce::dontSendNotification);

    gainRow->setValueQuiet (m->gain.base);
    gainSpreadRow->setValueQuiet (m->gain.spread);
    timeOffsetRow->setValueQuiet (m->timeOffset.base);
    timeOffsetSpreadRow->setValueQuiet (m->timeOffset.spread);
    filterTypeRow->combo.setSelectedItemIndex ((int) m->filter.type, juce::dontSendNotification);
    filterCutoffRow->setValueQuiet (m->filter.cutoffHz.base);
    filterResonanceRow->setValueQuiet (m->filter.resonance.base);
    filterCutoffJitterRow->setValueQuiet (m->filter.cutoffHz.spread);
    pitchRow->setValueQuiet (m->pitchSemitones.base);
    pitchSpreadRow->setValueQuiet (m->pitchSemitones.spread);

    refreshTypeVisibility();
}

void ModifierRowComponent::refreshTypeVisibility()
{
    auto* m = current();
    const ModifierType type = m != nullptr ? m->type : ModifierType::Gain;

    const bool isGain = type == ModifierType::Gain;
    const bool isTimeOffset = type == ModifierType::TimeOffset;
    const bool isFilter = type == ModifierType::Filter;
    const bool isPitch = type == ModifierType::Pitch;

    gainRow->setVisible (isGain);
    gainSpreadRow->setVisible (isGain);
    timeOffsetRow->setVisible (isTimeOffset);
    timeOffsetSpreadRow->setVisible (isTimeOffset);
    filterTypeRow->setVisible (isFilter);
    filterCutoffRow->setVisible (isFilter);
    filterResonanceRow->setVisible (isFilter);
    filterCutoffJitterRow->setVisible (isFilter);
    pitchRow->setVisible (isPitch);
    pitchSpreadRow->setVisible (isPitch);

    resized();
}

int ModifierRowComponent::preferredHeightFor (ModifierType type)
{
    switch (type)
    {
        case ModifierType::Gain:
        case ModifierType::TimeOffset:
        case ModifierType::Pitch:
            return 2 * FloatRowComponent::preferredHeight + ModifierStackEditor::rowGap;

        case ModifierType::Filter:
            return ComboRowComponent::preferredHeight + 3 * FloatRowComponent::preferredHeight + 3 * ModifierStackEditor::rowGap;
    }
    return 0;
}

int ModifierRowComponent::getPreferredHeight() const
{
    auto* m = current();
    const ModifierType type = m != nullptr ? m->type : ModifierType::Gain;
    return headerHeight + ModifierStackEditor::rowGap + preferredHeightFor (type);
}

void ModifierRowComponent::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (headerHeight);

    indexLabel.setBounds (header.removeFromLeft (24));
    enabledToggle.setBounds (header.removeFromLeft (28));
    removeButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (UiSpacing::xs);
    downButton.setBounds (header.removeFromRight (48));
    header.removeFromRight (UiSpacing::xs);
    upButton.setBounds (header.removeFromRight (48));
    header.removeFromRight (UiSpacing::xs);
    typeCombo.setBounds (header);

    b.removeFromTop (ModifierStackEditor::rowGap);

    auto* m = current();
    const ModifierType type = m != nullptr ? m->type : ModifierType::Gain;

    auto layoutRow = [&] (juce::Component& c, int h)
    {
        c.setBounds (b.removeFromTop (h));
        b.removeFromTop (ModifierStackEditor::rowGap);
    };

    switch (type)
    {
        case ModifierType::Gain:
            layoutRow (*gainRow, FloatRowComponent::preferredHeight);
            layoutRow (*gainSpreadRow, FloatRowComponent::preferredHeight);
            break;

        case ModifierType::TimeOffset:
            layoutRow (*timeOffsetRow, FloatRowComponent::preferredHeight);
            layoutRow (*timeOffsetSpreadRow, FloatRowComponent::preferredHeight);
            break;

        case ModifierType::Filter:
            layoutRow (*filterTypeRow, ComboRowComponent::preferredHeight);
            layoutRow (*filterCutoffRow, FloatRowComponent::preferredHeight);
            layoutRow (*filterResonanceRow, FloatRowComponent::preferredHeight);
            layoutRow (*filterCutoffJitterRow, FloatRowComponent::preferredHeight);
            break;

        case ModifierType::Pitch:
            layoutRow (*pitchRow, FloatRowComponent::preferredHeight);
            layoutRow (*pitchSpreadRow, FloatRowComponent::preferredHeight);
            break;
    }
}

// ============================================================== ModifierStackEditor

ModifierStackEditor::ModifierStackEditor (bool allowPitchIn) : allowPitch (allowPitchIn)
{
    addButton.onClick = [this]
    {
        if (modifiers != nullptr && (int) modifiers->size() < ModifierLimits::maxModifiersPerStack)
        {
            modifiers->push_back (Modifier {});
            rebuildRows();
        }
    };
    addAndMakeVisible (addButton);
}

void ModifierStackEditor::setModifiers (std::vector<Modifier>* stackIn)
{
    modifiers = stackIn;
    rebuildRows();
}

void ModifierStackEditor::refreshFromModel()
{
    // The slot COUNT might have changed underneath us (e.g. a preset
    // load) -- rebuild rather than just refreshing existing rows.
    rebuildRows();
}

void ModifierStackEditor::rebuildRows()
{
    rows.clear();

    if (modifiers != nullptr)
    {
        for (int i = 0; i < (int) modifiers->size(); ++i)
        {
            auto row = std::make_unique<ModifierRowComponent> (
                [this] { return modifiers; }, i, allowPitch,
                [this, i] { if (i > 0) { std::swap ((*modifiers)[(size_t) i], (*modifiers)[(size_t) (i - 1)]); rebuildRows(); } },
                [this, i] { if (i + 1 < (int) modifiers->size()) { std::swap ((*modifiers)[(size_t) i], (*modifiers)[(size_t) (i + 1)]); rebuildRows(); } },
                [this, i] { modifiers->erase (modifiers->begin() + i); rebuildRows(); },
                [this] { if (onHeightChanged) onHeightChanged(); });
            addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
    }

    addButton.setVisible (modifiers != nullptr);
    addButton.setEnabled (modifiers != nullptr && (int) modifiers->size() < ModifierLimits::maxModifiersPerStack);

    resized();
    if (onHeightChanged)
        onHeightChanged();
}

int ModifierStackEditor::getPreferredHeight() const
{
    int total = 0;
    for (auto& row : rows)
        total += row->getPreferredHeight() + rowGap;
    total += addButtonHeight;
    return total;
}

void ModifierStackEditor::resized()
{
    auto b = getLocalBounds();
    for (auto& row : rows)
    {
        row->setBounds (b.removeFromTop (row->getPreferredHeight()));
        b.removeFromTop (rowGap);
    }
    addButton.setBounds (b.removeFromTop (addButtonHeight));
}
