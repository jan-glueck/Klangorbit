#include "MappingPanel.h"
#include "MappingProfileManager.h"
#include "UiTheme.h"

MappingPanel::MappingPanel (const ParameterRegistry& registryToShow, MappingEngine& engineToControl)
    : registry (registryToShow), engine (engineToControl)
{
    targetLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (targetLabel);

    // See the class comment: only Global/SelectedObject parameters are
    // offered here, not the per-object-slot SpecificObject variants.
    int itemId = 1;
    for (auto& d : registry.all())
    {
        if (d.scope != ParameterRegistry::Scope::Global && d.scope != ParameterRegistry::Scope::SelectedObject)
            continue;

        targetParameterCombo.addItem (d.category + " - " + d.displayName, itemId++);
        comboIndexToParameterId.push_back (d.id);
    }
    if (targetParameterCombo.getNumItems() > 0)
        targetParameterCombo.setSelectedItemIndex (0, juce::dontSendNotification);
    addAndMakeVisible (targetParameterCombo);

    learnButton.onClick = [this] { learnButtonClicked(); };
    addAndMakeVisible (learnButton);

    statusLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    modifierLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (modifierLabel);
    modifierValueLabel.setColour (juce::Label::textColourId, UiColours::textPrimary());
    addAndMakeVisible (modifierValueLabel);

    bindingsHeaderLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (bindingsHeaderLabel);

    addAndMakeVisible (bindingsViewport);
    bindingsViewport.setViewedComponent (&bindingsContent, false);
    bindingsViewport.setScrollBarsShown (true, false);

    loadProfileButton.onClick = [this] { loadProfileClicked(); };
    addAndMakeVisible (loadProfileButton);
    saveProfileButton.onClick = [this] { saveProfileClicked(); };
    addAndMakeVisible (saveProfileButton);

    profileStatusLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    profileStatusLabel.setText (currentProfileName, juce::dontSendNotification);
    addAndMakeVisible (profileStatusLabel);

    refreshStatus();
    refreshBindingsList();
    startTimerHz (10); // see the class comment on why polling, not a change callback
}

MappingPanel::~MappingPanel()
{
    stopTimer();
}

void MappingPanel::timerCallback()
{
    refreshStatus();
    if (engine.getBindings().size() != lastBindingsCount)
        refreshBindingsList();
}

void MappingPanel::refreshStatus()
{
    modifierValueLabel.setText (engine.getModifierSourceId()
                                     + (engine.getCurrentBank() == 1 ? "  (held -- bank 1 active)" : "  (bank 0 active)"),
                                 juce::dontSendNotification);

    if (engine.isLearning())
    {
        const auto* descriptor = registry.find (engine.getLearningParameterId());
        statusLabel.setText ("Learning -- move the stick/trigger/button to bind to \""
                                  + (descriptor != nullptr ? descriptor->displayName : engine.getLearningParameterId()) + "\"...",
                              juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, UiColours::solo());
        learnButton.setButtonText ("Cancel");
    }
    else
    {
        statusLabel.setText ("Select a target parameter, then press Learn.", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
        learnButton.setButtonText ("Learn");
    }
}

void MappingPanel::learnButtonClicked()
{
    if (engine.isLearning())
    {
        engine.cancelLearning();
    }
    else
    {
        const int index = targetParameterCombo.getSelectedItemIndex();
        if (index < 0 || (size_t) index >= comboIndexToParameterId.size())
            return;

        engine.startLearning (comboIndexToParameterId[(size_t) index]);
    }

    refreshStatus();
}

void MappingPanel::refreshBindingsList()
{
    lastBindingsCount = engine.getBindings().size();

    bindingRows.clear();
    bindingsContent.removeAllChildren();

    for (auto& binding : engine.getBindings())
    {
        BindingRow row;
        row.binding = binding;

        const auto* descriptor = registry.find (binding.parameterId);
        const auto targetLabelText = descriptor != nullptr ? descriptor->displayName : binding.parameterId;

        row.label = std::make_unique<juce::Label> (juce::String(),
            binding.sourceId + "  ->  " + targetLabelText + "  (bank " + juce::String (binding.bank) + ")");
        row.label->setColour (juce::Label::textColourId, UiColours::textPrimary());
        bindingsContent.addAndMakeVisible (*row.label);

        row.removeButton = std::make_unique<juce::TextButton> ("Remove");
        const auto sourceId = binding.sourceId;
        const auto bank = binding.bank;
        row.removeButton->onClick = [this, sourceId, bank]
        {
            engine.removeBinding (sourceId, bank);
            refreshBindingsList();
        };
        bindingsContent.addAndMakeVisible (*row.removeButton);

        bindingRows.push_back (std::move (row));
    }

    resized(); // re-lays out the bindings list (its own child rows) against the current size
}

void MappingPanel::loadProfileClicked()
{
    juce::File startDir (juce::File::getCurrentWorkingDirectory());
   #if defined (SAPOC_MAPPING_PROFILES_USER_DIR)
    if (juce::File (SAPOC_MAPPING_PROFILES_USER_DIR).isDirectory())
        startDir = juce::File (SAPOC_MAPPING_PROFILES_USER_DIR);
   #endif

    fileChooser = std::make_unique<juce::FileChooser> ("Load Mapping Profile", startDir, "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;

            juce::String loadedName;
            const auto result = MappingProfileManager::loadFile (file, engine, &loadedName);

            if (result.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load mapping profile", result.getErrorMessage());
                return;
            }

            currentProfileName = loadedName;
            profileStatusLabel.setText ("Loaded: " + currentProfileName, juce::dontSendNotification);
            refreshBindingsList();
        });
}

void MappingPanel::saveProfileClicked()
{
    juce::File startDir (juce::File::getCurrentWorkingDirectory());
   #if defined (SAPOC_MAPPING_PROFILES_USER_DIR)
    startDir = juce::File (SAPOC_MAPPING_PROFILES_USER_DIR);
    if (! startDir.isDirectory())
        startDir.createDirectory();
   #endif

    fileChooser = std::make_unique<juce::FileChooser> ("Save Mapping Profile",
                                                        startDir.getChildFile (currentProfileName + ".json"),
                                                        "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            if (! file.hasFileExtension ("json"))
                file = file.withFileExtension ("json");

            const auto name = file.getFileNameWithoutExtension();
            const auto result = MappingProfileManager::saveFile (file, engine, name);

            if (result.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save mapping profile", result.getErrorMessage());
                return;
            }

            currentProfileName = name;
            profileStatusLabel.setText ("Saved: " + currentProfileName, juce::dontSendNotification);
        });
}

void MappingPanel::resized()
{
    auto b = getLocalBounds().reduced (UiSpacing::m);

    auto targetRow = b.removeFromTop (28);
    targetLabel.setBounds (targetRow.removeFromLeft (160));
    targetRow.removeFromLeft (UiSpacing::s);
    learnButton.setBounds (targetRow.removeFromRight (90));
    targetRow.removeFromRight (UiSpacing::s);
    targetParameterCombo.setBounds (targetRow);

    b.removeFromTop (UiSpacing::xs);
    statusLabel.setBounds (b.removeFromTop (22));

    b.removeFromTop (UiSpacing::s);
    auto modifierRow = b.removeFromTop (22);
    modifierLabel.setBounds (modifierRow.removeFromLeft (220));
    modifierValueLabel.setBounds (modifierRow);

    b.removeFromTop (UiSpacing::m);
    bindingsHeaderLabel.setBounds (b.removeFromTop (20));
    b.removeFromTop (UiSpacing::xs);

    auto profileRow = b.removeFromBottom (28);
    b.removeFromBottom (UiSpacing::s);
    auto profileStatusRow = b.removeFromBottom (20);
    profileStatusLabel.setBounds (profileStatusRow);
    loadProfileButton.setBounds (profileRow.removeFromLeft (140));
    profileRow.removeFromLeft (UiSpacing::s);
    saveProfileButton.setBounds (profileRow.removeFromLeft (140));

    b.removeFromBottom (UiSpacing::s);
    bindingsViewport.setBounds (b);

    constexpr int rowHeight = 26;
    const int width = juce::jmax (200, bindingsViewport.getWidth() - 12);
    int y = 0;
    for (auto& row : bindingRows)
    {
        auto rowBounds = juce::Rectangle<int> (0, y, width, rowHeight);
        row.removeButton->setBounds (rowBounds.removeFromRight (80));
        rowBounds.removeFromRight (UiSpacing::xs);
        row.label->setBounds (rowBounds);
        y += rowHeight;
    }
    bindingsContent.setSize (width, juce::jmax (y, bindingsViewport.getHeight()));
}

void MappingPanel::paint (juce::Graphics& g)
{
    g.fillAll (UiColours::bgPanel());
}
