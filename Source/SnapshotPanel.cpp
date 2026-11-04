#include "SnapshotPanel.h"
#include "UiTheme.h"
#include <algorithm>

SnapshotPanel::SnapshotPanel (const ParameterRegistry& registryToShow, std::function<void()> onRecalled)
    : registry (registryToShow), onRecalledCallback (std::move (onRecalled))
{
    categoriesHeaderLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (categoriesHeaderLabel);

    // One toggle per category actually present in the registry, in
    // registration order (same order ParameterPanel's own tabs/
    // AgitationEditor's own groups already use) -- ticked by default,
    // so "every box checked" (== SnapshotManager::capture()'s own
    // "empty list" convention, see its class comment) is the ordinary
    // starting point, not an empty, useless scope.
    for (auto& descriptor : registry.all())
    {
        if (descriptor.scope != ParameterRegistry::Scope::Global
            && descriptor.scope != ParameterRegistry::Scope::SelectedObject)
            continue;

        const bool alreadyHave = std::any_of (categoryToggles.begin(), categoryToggles.end(),
            [&] (auto& t) { return t.category == descriptor.category; });
        if (alreadyHave)
            continue;

        CategoryToggle entry;
        entry.category = descriptor.category;
        entry.toggle = std::make_unique<juce::ToggleButton> (descriptor.category);
        entry.toggle->setToggleState (true, juce::dontSendNotification);
        entry.toggle->setColour (juce::ToggleButton::textColourId, UiColours::textSecondary());
        addAndMakeVisible (*entry.toggle);
        categoryToggles.push_back (std::move (entry));
    }

    loadButton.onClick = [this] { loadSnapshotClicked(); };
    addAndMakeVisible (loadButton);
    saveButton.onClick = [this] { saveSnapshotClicked(); };
    addAndMakeVisible (saveButton);

    statusLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    statusLabel.setText (currentSnapshotName, juce::dontSendNotification);
    addAndMakeVisible (statusLabel);
}

std::vector<juce::String> SnapshotPanel::selectedCategories() const
{
    const bool allChecked = std::all_of (categoryToggles.begin(), categoryToggles.end(),
        [] (auto& t) { return t.toggle->getToggleState(); });
    if (allChecked)
        return {}; // see SnapshotManager::capture()'s own "empty == no restriction" convention

    std::vector<juce::String> result;
    for (auto& t : categoryToggles)
        if (t.toggle->getToggleState())
            result.push_back (t.category);
    return result;
}

void SnapshotPanel::loadSnapshotClicked()
{
    juce::File startDir (juce::File::getCurrentWorkingDirectory());
   #if defined (SAPOC_SNAPSHOTS_USER_DIR)
    if (juce::File (SAPOC_SNAPSHOTS_USER_DIR).isDirectory())
        startDir = juce::File (SAPOC_SNAPSHOTS_USER_DIR);
   #endif

    fileChooser = std::make_unique<juce::FileChooser> ("Load Snapshot", startDir, "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;

            SnapshotManager::Snapshot snapshot;
            juce::String loadedName;
            const auto result = SnapshotManager::loadFile (file, snapshot, &loadedName);

            if (result.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load snapshot", result.getErrorMessage());
                return;
            }

            SnapshotManager::recall (snapshot, registry);

            currentSnapshotName = loadedName;
            statusLabel.setText ("Loaded: " + currentSnapshotName, juce::dontSendNotification);

            if (onRecalledCallback)
                onRecalledCallback();
        });
}

void SnapshotPanel::saveSnapshotClicked()
{
    juce::File startDir (juce::File::getCurrentWorkingDirectory());
   #if defined (SAPOC_SNAPSHOTS_USER_DIR)
    startDir = juce::File (SAPOC_SNAPSHOTS_USER_DIR);
    if (! startDir.isDirectory())
        startDir.createDirectory();
   #endif

    fileChooser = std::make_unique<juce::FileChooser> ("Save Snapshot",
                                                        startDir.getChildFile (currentSnapshotName + ".json"),
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
            const auto snapshot = SnapshotManager::capture (registry, name, selectedCategories());
            const auto result = SnapshotManager::saveFile (file, snapshot);

            if (result.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save snapshot", result.getErrorMessage());
                return;
            }

            currentSnapshotName = name;
            statusLabel.setText ("Saved: " + currentSnapshotName, juce::dontSendNotification);
        });
}

void SnapshotPanel::resized()
{
    auto b = getLocalBounds().reduced (UiSpacing::m);

    categoriesHeaderLabel.setBounds (b.removeFromTop (22));
    b.removeFromTop (UiSpacing::xs);

    constexpr int toggleRowHeight = 24;
    for (auto& t : categoryToggles)
        t.toggle->setBounds (b.removeFromTop (toggleRowHeight));

    b.removeFromTop (UiSpacing::m);

    auto buttonRow = b.removeFromTop (28);
    loadButton.setBounds (buttonRow.removeFromLeft (140));
    buttonRow.removeFromLeft (UiSpacing::s);
    saveButton.setBounds (buttonRow.removeFromLeft (140));

    b.removeFromTop (UiSpacing::s);
    statusLabel.setBounds (b.removeFromTop (22));
}

void SnapshotPanel::paint (juce::Graphics& g)
{
    g.fillAll (UiColours::bgPanel());
}
