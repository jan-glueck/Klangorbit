#include "AgitationEditor.h"
#include "UiTheme.h"

// ============================================================== AgitationRowComponent

AgitationRowComponent::AgitationRowComponent (juce::String idIn, const juce::String& displayName, float amountRange,
                                                AgitationEngine& engineIn, std::function<void()> onHeightChanged)
    : id (std::move (idIn)), engine (engineIn), onHeightChangedCallback (std::move (onHeightChanged))
{
    enabledRow = std::make_unique<ToggleRowComponent> (displayName);
    enabledRow->onToggled = [this] (bool v)
    {
        engine.settingsFor (id).enabled = v;
        resized();
        if (onHeightChangedCallback)
            onHeightChangedCallback();
    };
    addAndMakeVisible (*enabledRow);

    // 0..the bound parameter's own full span (maxValue - minValue) --
    // a modulation deeper than the parameter's entire range would be
    // meaningless, so this is a sane per-parameter upper bound without
    // needing a hand-picked one per row (there are far too many rows
    // for that, see this class's own comment).
    const double amountMax = (double) juce::jmax (0.001f, amountRange);
    amountRow = std::make_unique<FloatRowComponent> ("Amount", 0.0, amountMax, amountMax / 1000.0);
    amountRow->onValueChanged = [this] (float v) { engine.settingsFor (id).amount = v; };
    addAndMakeVisible (*amountRow);

    rateRow = std::make_unique<FloatRowComponent> ("Rate (1/s)", 0.01, 20.0, 0.01);
    rateRow->onValueChanged = [this] (float v) { engine.settingsFor (id).reversionRate = v; };
    addAndMakeVisible (*rateRow);

    smoothingRow = std::make_unique<FloatRowComponent> ("Smoothing (s)", 0.0, 5.0, 0.01);
    smoothingRow->onValueChanged = [this] (float v) { engine.settingsFor (id).smoothing = v; };
    addAndMakeVisible (*smoothingRow);

    tempoSyncRow = std::make_unique<ToggleRowComponent> ("Tempo Synced");
    tempoSyncRow->onToggled = [this] (bool v)
    {
        engine.settingsFor (id).tempoSynced = v;
        resized();
        if (onHeightChangedCallback)
            onHeightChangedCallback();
    };
    addAndMakeVisible (*tempoSyncRow);

    tempoDivisionRow = std::make_unique<ComboRowComponent> ("Tempo Division");
    tempoDivisionRow->combo.addItem ("Whole", (int) Agitation::TempoDivision::Whole + 1);
    tempoDivisionRow->combo.addItem ("Half", (int) Agitation::TempoDivision::Half + 1);
    tempoDivisionRow->combo.addItem ("Quarter", (int) Agitation::TempoDivision::Quarter + 1);
    tempoDivisionRow->combo.addItem ("Eighth", (int) Agitation::TempoDivision::Eighth + 1);
    tempoDivisionRow->combo.addItem ("Sixteenth", (int) Agitation::TempoDivision::Sixteenth + 1);
    tempoDivisionRow->combo.addItem ("Quarter Triplet", (int) Agitation::TempoDivision::QuarterTriplet + 1);
    tempoDivisionRow->combo.addItem ("Eighth Triplet", (int) Agitation::TempoDivision::EighthTriplet + 1);
    tempoDivisionRow->combo.addItem ("Sixteenth Triplet", (int) Agitation::TempoDivision::SixteenthTriplet + 1);
    tempoDivisionRow->onSelected = [this] (int)
    {
        engine.settingsFor (id).tempoDivision = (Agitation::TempoDivision) (tempoDivisionRow->combo.getSelectedId() - 1);
    };
    addAndMakeVisible (*tempoDivisionRow);

    refreshFromModel();
}

Agitation::Settings AgitationRowComponent::shownSettings() const
{
    const auto* s = engine.findSettings (id);
    return s != nullptr ? *s : Agitation::Settings {};
}

void AgitationRowComponent::refreshFromModel()
{
    const auto shown = shownSettings();
    enabledRow->setValueQuiet (shown.enabled);
    amountRow->setValueQuiet (shown.amount);
    rateRow->setValueQuiet (shown.reversionRate);
    smoothingRow->setValueQuiet (shown.smoothing);
    tempoSyncRow->setValueQuiet (shown.tempoSynced);
    tempoDivisionRow->combo.setSelectedId ((int) shown.tempoDivision + 1, juce::dontSendNotification);
    resized();
}

int AgitationRowComponent::getPreferredHeight() const
{
    const auto shown = shownSettings();

    int h = ToggleRowComponent::preferredHeight + rowGap; // enabledRow
    if (shown.enabled)
    {
        h += FloatRowComponent::preferredHeight + rowGap; // amount
        if (! shown.tempoSynced)
            h += FloatRowComponent::preferredHeight + rowGap; // rate (hidden while tempoSynced)
        h += FloatRowComponent::preferredHeight + rowGap; // smoothing
        h += ToggleRowComponent::preferredHeight + rowGap; // tempoSynced
        if (shown.tempoSynced)
            h += ComboRowComponent::preferredHeight + rowGap; // tempoDivision
    }
    return h;
}

void AgitationRowComponent::resized()
{
    const auto shown = shownSettings();
    auto b = getLocalBounds();

    auto layout = [&] (juce::Component& c, int h)
    {
        c.setBounds (b.removeFromTop (h));
        b.removeFromTop (rowGap);
    };

    layout (*enabledRow, ToggleRowComponent::preferredHeight);

    amountRow->setVisible (shown.enabled);
    rateRow->setVisible (shown.enabled && ! shown.tempoSynced);
    smoothingRow->setVisible (shown.enabled);
    tempoSyncRow->setVisible (shown.enabled);
    tempoDivisionRow->setVisible (shown.enabled && shown.tempoSynced);

    if (shown.enabled)
    {
        layout (*amountRow, FloatRowComponent::preferredHeight);
        if (! shown.tempoSynced)
            layout (*rateRow, FloatRowComponent::preferredHeight);
        layout (*smoothingRow, FloatRowComponent::preferredHeight);
        layout (*tempoSyncRow, ToggleRowComponent::preferredHeight);
        if (shown.tempoSynced)
            layout (*tempoDivisionRow, ComboRowComponent::preferredHeight);
    }
}

// ============================================================== AgitationEditor

void AgitationEditor::setup (const ParameterRegistry& registry, AgitationEngine& engineIn)
{
    groups.clear();

    // Fixed category order, mirroring ParameterPanel's own category tab
    // order (see ParameterPanel.cpp's addCategoryButton() calls) -- so
    // this reads in the same order a user already expects from every
    // other category in this panel. "Global" is ParameterRegistry's own
    // category string for Scene-wide parameters (see buildParameterRegistry()'s
    // registerSceneFloatParam()/-BoolParam()/-Vec3Param() calls), not a
    // typo for ParameterPanel::Category's own "Scene".
    static const char* const categoryOrder[] = {
        "Global", "Object Physics", "Attraction", "Orbit", "Doppler", "Grain Cloud", "Delay"
    };

    for (auto* categoryName : categoryOrder)
    {
        CategoryGroup group;

        for (auto& descriptor : registry.all())
        {
            if (descriptor.category != juce::String (categoryName))
                continue;
            // Scope::SpecificObject entries ("object.3.mass") excluded --
            // see this class's own header comment on why.
            if (descriptor.scope != ParameterRegistry::Scope::Global
                && descriptor.scope != ParameterRegistry::Scope::SelectedObject)
                continue;

            auto row = std::make_unique<AgitationRowComponent> (
                descriptor.id, descriptor.displayName, descriptor.maxValue - descriptor.minValue,
                engineIn, [this] { if (onHeightChanged) onHeightChanged(); });
            addAndMakeVisible (*row);
            group.rows.push_back (std::move (row));
        }

        if (! group.rows.empty())
        {
            group.headerLabel = std::make_unique<juce::Label>();
            styleRowLabel (*group.headerLabel, juce::String (categoryName).toUpperCase(), 11.0f, UiColours::textDisabled());
            addAndMakeVisible (*group.headerLabel);
            groups.push_back (std::move (group));
        }
    }

    resized();
    if (onHeightChanged)
        onHeightChanged();
}

void AgitationEditor::refreshFromModel()
{
    for (auto& group : groups)
        for (auto& row : group.rows)
            row->refreshFromModel();
    resized();
}

int AgitationEditor::getPreferredHeight() const
{
    int total = 0;
    for (auto& group : groups)
    {
        total += categoryHeaderHeight + rowGap;
        for (auto& row : group.rows)
            total += row->getPreferredHeight() + rowGap;
    }
    return total;
}

void AgitationEditor::resized()
{
    auto b = getLocalBounds();
    for (auto& group : groups)
    {
        group.headerLabel->setBounds (b.removeFromTop (categoryHeaderHeight));
        b.removeFromTop (rowGap);
        for (auto& row : group.rows)
        {
            row->setBounds (b.removeFromTop (row->getPreferredHeight()));
            b.removeFromTop (rowGap);
        }
    }
}
