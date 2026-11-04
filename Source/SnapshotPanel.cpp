#include "SnapshotPanel.h"
#include "UiTheme.h"
#include <algorithm>

SnapshotPanel::SnapshotPanel (const ParameterRegistry& registryToShow, SnapshotMorphEngine& morphEngineToControl,
                                std::function<void()> onRecalled)
    : registry (registryToShow), morphEngine (morphEngineToControl), onRecalledCallback (std::move (onRecalled))
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

    // --- Randomize ----------------------------------------------------------
    randomizeHeaderLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (randomizeHeaderLabel);

    strengthLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (strengthLabel);
    strengthSlider.setRange (0.0, 1.0, 0.01);
    strengthSlider.setValue (0.3, juce::dontSendNotification);
    strengthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    strengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    addAndMakeVisible (strengthSlider);

    randomizeVariantButton.onClick = [this] { randomizeVariantClicked(); };
    addAndMakeVisible (randomizeVariantButton);
    randomizeEverythingButton.onClick = [this] { randomizeEverythingClicked(); };
    addAndMakeVisible (randomizeEverythingButton);

    // --- Morph ----------------------------------------------------------------
    morphHeaderLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (morphHeaderLabel);

    loadAButton.onClick = [this] { loadMorphEndpointClicked (true); };
    addAndMakeVisible (loadAButton);
    loadBButton.onClick = [this] { loadMorphEndpointClicked (false); };
    addAndMakeVisible (loadBButton);

    morphStatusLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (morphStatusLabel);

    morphLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (morphLabel);
    morphSlider.setRange (0.0, 1.0, 0.001);
    morphSlider.setValue ((double) morphEngine.morph, juce::dontSendNotification);
    morphSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    morphSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    morphSlider.onValueChange = [this] { morphEngine.morph = (float) morphSlider.getValue(); };
    addAndMakeVisible (morphSlider);

    morphSmoothingLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    addAndMakeVisible (morphSmoothingLabel);
    morphSmoothingSlider.setRange (0.0, 5.0, 0.01);
    morphSmoothingSlider.setValue ((double) morphEngine.smoothing, juce::dontSendNotification);
    morphSmoothingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    morphSmoothingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    morphSmoothingSlider.onValueChange = [this] { morphEngine.smoothing = (float) morphSmoothingSlider.getValue(); };
    addAndMakeVisible (morphSmoothingSlider);

    updateMorphControlsEnabled();
    updateRandomizeControlsEnabled();
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

void SnapshotPanel::updateMorphControlsEnabled()
{
    const bool ready = morphEngine.hasEndpoints();
    morphSlider.setEnabled (ready);
    morphSmoothingSlider.setEnabled (ready);
}

void SnapshotPanel::updateRandomizeControlsEnabled()
{
    const bool ready = ! currentSnapshot.entries.empty();
    strengthSlider.setEnabled (ready);
    randomizeVariantButton.setEnabled (ready);
    randomizeEverythingButton.setEnabled (ready);
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
            currentSnapshot = std::move (snapshot);
            statusLabel.setText ("Loaded: " + currentSnapshotName, juce::dontSendNotification);
            updateRandomizeControlsEnabled();

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
            auto snapshot = SnapshotManager::capture (registry, name, selectedCategories());
            const auto result = SnapshotManager::saveFile (file, snapshot);

            if (result.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save snapshot", result.getErrorMessage());
                return;
            }

            currentSnapshotName = name;
            currentSnapshot = std::move (snapshot);
            statusLabel.setText ("Saved: " + currentSnapshotName, juce::dontSendNotification);
            updateRandomizeControlsEnabled();
        });
}

void SnapshotPanel::loadMorphEndpointClicked (bool isA)
{
    juce::File startDir (juce::File::getCurrentWorkingDirectory());
   #if defined (SAPOC_SNAPSHOTS_USER_DIR)
    if (juce::File (SAPOC_SNAPSHOTS_USER_DIR).isDirectory())
        startDir = juce::File (SAPOC_SNAPSHOTS_USER_DIR);
   #endif

    fileChooser = std::make_unique<juce::FileChooser> (isA ? "Load Snapshot A" : "Load Snapshot B", startDir, "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, isA] (const juce::FileChooser& fc)
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

            // Deliberately NOT recalled -- setting up a morph endpoint
            // shouldn't itself jump the live state anywhere, see this
            // class's own header comment.
            if (isA) { morphSnapshotA = std::move (snapshot); morphNameA = loadedName; }
            else     { morphSnapshotB = std::move (snapshot); morphNameB = loadedName; }

            morphStatusLabel.setText ("A: " + morphNameA + "   B: " + morphNameB, juce::dontSendNotification);

            if (! morphSnapshotA.entries.empty() && ! morphSnapshotB.entries.empty())
                morphEngine.setEndpoints (morphSnapshotA, morphSnapshotB);

            updateMorphControlsEnabled();
        });
}

void SnapshotPanel::randomizeVariantClicked()
{
    if (currentSnapshot.entries.empty())
        return;

    const auto variant = SnapshotManager::randomVariant (currentSnapshot, registry, (float) strengthSlider.getValue(), randomizeRng);
    SnapshotManager::recall (variant, registry);

    if (onRecalledCallback)
        onRecalledCallback();
}

void SnapshotPanel::randomizeEverythingClicked()
{
    if (currentSnapshot.entries.empty())
        return;

    const auto variant = SnapshotManager::randomizeEverything (currentSnapshot, registry, randomizeRng);
    SnapshotManager::recall (variant, registry);

    if (onRecalledCallback)
        onRecalledCallback();
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

    b.removeFromTop (UiSpacing::m);
    randomizeHeaderLabel.setBounds (b.removeFromTop (20));
    b.removeFromTop (UiSpacing::xs);

    auto strengthRow = b.removeFromTop (24);
    strengthLabel.setBounds (strengthRow.removeFromLeft (80));
    strengthSlider.setBounds (strengthRow);

    b.removeFromTop (UiSpacing::xs);
    auto randomizeButtonRow = b.removeFromTop (28);
    randomizeVariantButton.setBounds (randomizeButtonRow.removeFromLeft (140));
    randomizeButtonRow.removeFromLeft (UiSpacing::s);
    randomizeEverythingButton.setBounds (randomizeButtonRow.removeFromLeft (150));

    b.removeFromTop (UiSpacing::m);
    morphHeaderLabel.setBounds (b.removeFromTop (20));
    b.removeFromTop (UiSpacing::xs);

    auto morphButtonRow = b.removeFromTop (28);
    loadAButton.setBounds (morphButtonRow.removeFromLeft (110));
    morphButtonRow.removeFromLeft (UiSpacing::s);
    loadBButton.setBounds (morphButtonRow.removeFromLeft (110));

    b.removeFromTop (UiSpacing::xs);
    morphStatusLabel.setBounds (b.removeFromTop (20));

    b.removeFromTop (UiSpacing::xs);
    auto morphRow = b.removeFromTop (24);
    morphLabel.setBounds (morphRow.removeFromLeft (110));
    morphSlider.setBounds (morphRow);

    b.removeFromTop (UiSpacing::xs);
    auto morphSmoothingRow = b.removeFromTop (24);
    morphSmoothingLabel.setBounds (morphSmoothingRow.removeFromLeft (110));
    morphSmoothingSlider.setBounds (morphSmoothingRow);
}

void SnapshotPanel::paint (juce::Graphics& g)
{
    g.fillAll (UiColours::bgPanel());
}
