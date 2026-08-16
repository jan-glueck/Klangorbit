#include "PluginEditor.h"
#include "PresetManager.h"

SpatialAudioPOCEditor::SpatialAudioPOCEditor (SpatialAudioPOCProcessor& p)
    : juce::AudioProcessorEditor (&p), audioProcessor (p)
{
    addAndMakeVisible (loadPresetButton);
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (presetStatusLabel);
    addAndMakeVisible (addObjectButton);
    addAndMakeVisible (removeObjectButton);
    addAndMakeVisible (objectCountLabel);
    addAndMakeVisible (parameterPanel);

    loadPresetButton.onClick = [this] { loadPresetClicked(); };
    savePresetButton.onClick = [this] { savePresetClicked(); };
    addObjectButton.onClick = [this] { addObjectClicked(); };
    removeObjectButton.onClick = [this] { removeObjectClicked(); };

    presetStatusLabel.setText (currentPresetName, juce::dontSendNotification);
    presetStatusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    presetStatusLabel.setJustificationType (juce::Justification::centredLeft);

    objectCountLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    objectCountLabel.setJustificationType (juce::Justification::centredLeft);

    parameterPanel.setSceneSettings (&audioProcessor.getTrajectoryEngine().getSceneSettings());
    parameterPanel.refreshFromModel(); // show scene defaults (roomSize etc.) right away
    selectObject (-1);                 // initializes panel enablement + object count label consistently

    setSize (700 + parameterPanelWidth, 700 + toolbarHeight);
    setWantsKeyboardFocus (true);
    lastTimerMs = juce::Time::getMillisecondCounter();
    startTimerHz (90); // control rate for the TrajectoryEngine
}

SpatialAudioPOCEditor::~SpatialAudioPOCEditor()
{
    stopTimer();
}

void SpatialAudioPOCEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const double dt = juce::jlimit (0.0, 0.1, (double) (now - lastTimerMs) / 1000.0); // clamp against outliers
    lastTimerMs = now;

    // Polled here (not from mouseDrag) so a Ctrl/Alt press registers
    // immediately even while the mouse itself isn't moving.
    updateSlingModifiers();

    audioProcessor.getTrajectoryEngine().update (dt);
    repaint();
}

juce::Point<float> SpatialAudioPOCEditor::objectToScreen (Vec3 pos) const
{
    const auto c = viewArea.toFloat().getCentre();
    // Screen y points downward, world y (left) should optically point up-left,
    // so x -> screen y (front = up), y -> screen x (left = left).
    return { c.x - pos.y * pixelsPerMeter, c.y - pos.x * pixelsPerMeter };
}

Vec3 SpatialAudioPOCEditor::screenToObject (juce::Point<float> screenPos) const
{
    const auto c = viewArea.toFloat().getCentre();
    const float worldX = (c.y - screenPos.y) / pixelsPerMeter;
    const float worldY = (c.x - screenPos.x) / pixelsPerMeter;
    return { worldX, worldY, 0.0f };
}

int SpatialAudioPOCEditor::findObjectNear (juce::Point<float> screenPos) const
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        auto& obj = engine.getObject (i);
        if (obj.inputChannel < 0) continue; // inactive objects are not clickable/visible

        auto p = objectToScreen (obj.position);
        if (p.getDistanceFrom (screenPos) <= hitRadiusPixels)
            return i;
    }
    return -1;
}

void SpatialAudioPOCEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    // The 2D scene must never draw outside viewArea -- objects can wander
    // up to the room boundary (SceneSettings::roomSize) in any direction,
    // which in screen space can reach past the toolbar/parameter-panel
    // edge without this: they'd render underneath those child components
    // instead of staying confined to the scene area, looking like the two
    // areas overlap.
    g.saveState();
    g.reduceClipRegion (viewArea);

    const auto centre = viewArea.toFloat().getCentre();

    // Reference circles (1m/2m/3m) as orientation aids
    g.setColour (juce::Colours::darkgrey);
    for (int m = 1; m <= 3; ++m)
    {
        const float r = (float) m * pixelsPerMeter;
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.0f);
    }

    // Room boundary (SceneSettings::roomSize), if enabled
    const auto& sceneSettings = audioProcessor.getTrajectoryEngine().getSceneSettings();
    if (sceneSettings.roomSize > 0.0f)
    {
        g.setColour (juce::Colours::darkred);
        const float r = sceneSettings.roomSize * pixelsPerMeter;
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.5f);
    }

    // Listening position/origin
    g.setColour (juce::Colours::white);
    g.drawLine (centre.x - 10, centre.y, centre.x + 10, centre.y);
    g.drawLine (centre.x, centre.y - 10, centre.x, centre.y + 10);
    g.drawText ("Front", (int) (centre.x - 30), (int) (centre.y - pixelsPerMeter * 3 - 20), 60, 20, juce::Justification::centred);

    auto& engine = audioProcessor.getTrajectoryEngine();
    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        auto& obj = engine.getObject (i);
        if (obj.inputChannel < 0) continue; // no active object

        auto p = objectToScreen (obj.position);

        if (slingActive && i == slingObjectIndex)
        {
            // While aiming, the object's real position stays frozen at the
            // anchor (see startSling()) -- draw that as a hollow marker;
            // the solid "object following the cursor" marker is drawn
            // separately below.
            g.setColour (juce::Colours::grey);
            g.drawEllipse (p.x - 8, p.y - 8, 16, 16, 1.5f);
        }
        else
        {
            g.setColour (i == draggedObjectIndex ? juce::Colours::yellow : juce::Colours::cyan);
            g.fillEllipse (p.x - 8, p.y - 8, 16, 16);
        }

        if (i == selectedObjectIndex)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (p.x - 11, p.y - 11, 22, 22, 2.0f);
        }

        g.setColour (juce::Colours::white);
        g.drawText (juce::String (i), (int) p.x - 20, (int) p.y + 10, 40, 16, juce::Justification::centred);
    }

    if (slingActive)
    {
        const auto anchorScreen = objectToScreen (slingAnchorWorldPos);
        const auto isOrbit = slingWantsOrbit;
        const auto slingColour = isOrbit ? juce::Colours::violet : juce::Colours::orange;

        // The "bow": anchor -> current cursor position.
        g.setColour (slingColour);
        g.drawLine (anchorScreen.x, anchorScreen.y, slingCursorScreenPos.x, slingCursorScreenPos.y, 2.0f);

        // The object visually following the cursor while pulled back.
        g.fillEllipse (slingCursorScreenPos.x - 8, slingCursorScreenPos.y - 8, 16, 16);

        const auto label = isOrbit
            ? juce::String ("Orbit: ") + SlingGesture::orbitEccentricitySteps[(size_t) slingEccentricityStepIndex].label
            : juce::String ("Free Throw");
        g.setColour (juce::Colours::white);
        g.drawText (label, (int) slingCursorScreenPos.x - 60, (int) slingCursorScreenPos.y + 12, 120, 16, juce::Justification::centred);
    }

    g.restoreState();
}

void SpatialAudioPOCEditor::resized()
{
    auto bounds = getLocalBounds();
    auto toolbar = bounds.removeFromTop (toolbarHeight);

    auto row1 = toolbar.removeFromTop (32);
    loadPresetButton.setBounds (row1.removeFromLeft (140).reduced (4));
    savePresetButton.setBounds (row1.removeFromLeft (140).reduced (4));
    presetStatusLabel.setBounds (row1.reduced (4));

    auto row2 = toolbar;
    addObjectButton.setBounds (row2.removeFromLeft (100).reduced (4));
    removeObjectButton.setBounds (row2.removeFromLeft (160).reduced (4));
    objectCountLabel.setBounds (row2.removeFromLeft (120).reduced (4));

    parameterPanel.setBounds (bounds.removeFromRight (parameterPanelWidth));
    viewArea = bounds;
}

void SpatialAudioPOCEditor::showPresetError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
}

void SpatialAudioPOCEditor::loadPresetClicked()
{
    juce::File startDir (juce::File::getCurrentWorkingDirectory());
#if defined (SAPOC_PRESETS_USER_DIR)
    if (juce::File (SAPOC_PRESETS_USER_DIR).isDirectory())
        startDir = juce::File (SAPOC_PRESETS_USER_DIR);
#endif

    fileChooser = std::make_unique<juce::FileChooser> ("Load Preset", startDir, "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;

            juce::String loadedName;
            const auto result = PresetManager::loadFile (file, audioProcessor.getTrajectoryEngine(), &loadedName);

            if (result.failed())
            {
                showPresetError ("Could not load preset", result.getErrorMessage());
                return;
            }

            currentPresetName = loadedName;
            presetStatusLabel.setText ("Loaded: " + currentPresetName, juce::dontSendNotification);

            // The scene was replaced entirely -- the old selection/panel
            // values no longer make sense.
            selectObject (-1);
            parameterPanel.refreshFromModel();
        });
}

void SpatialAudioPOCEditor::savePresetClicked()
{
    juce::File startDir (juce::File::getCurrentWorkingDirectory());
#if defined (SAPOC_PRESETS_USER_DIR)
    startDir = juce::File (SAPOC_PRESETS_USER_DIR);
    if (! startDir.isDirectory())
        startDir.createDirectory();
#endif

    fileChooser = std::make_unique<juce::FileChooser> ("Save Preset",
                                                        startDir.getChildFile (currentPresetName + ".json"),
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
            const auto result = PresetManager::saveFile (file, audioProcessor.getTrajectoryEngine(), name);

            if (result.failed())
            {
                showPresetError ("Could not save preset", result.getErrorMessage());
                return;
            }

            currentPresetName = name;
            presetStatusLabel.setText ("Saved: " + currentPresetName, juce::dontSendNotification);
        });
}

void SpatialAudioPOCEditor::addObjectClicked()
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    const int idx = engine.findNextInactiveObject();
    if (idx < 0)
        return; // all objects already active

    engine.activateObject (idx);
    selectObject (idx); // select it right away, convenient for immediate tweaking
}

void SpatialAudioPOCEditor::removeObjectClicked()
{
    if (selectedObjectIndex < 0)
        return;

    audioProcessor.getTrajectoryEngine().deactivateObject (selectedObjectIndex);
    selectObject (-1);
}

void SpatialAudioPOCEditor::selectObject (int index)
{
    selectedObjectIndex = index;
    auto& engine = audioProcessor.getTrajectoryEngine();

    if (index < 0)
        parameterPanel.setEditedObject (nullptr, -1, engine.getNumObjects());
    else
        parameterPanel.setEditedObject (&engine.getObject (index), index, engine.getNumObjects());

    updateObjectUiState();
}

void SpatialAudioPOCEditor::updateObjectUiState()
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    const int active = engine.getNumActiveObjects();
    const int total = engine.getNumObjects();

    objectCountLabel.setText ("Objects: " + juce::String (active) + " / " + juce::String (total), juce::dontSendNotification);
    addObjectButton.setEnabled (active < total);
    removeObjectButton.setEnabled (selectedObjectIndex >= 0);
}

void SpatialAudioPOCEditor::startSling (int objectIndex)
{
    auto& engine = audioProcessor.getTrajectoryEngine();

    slingActive = true;
    slingObjectIndex = objectIndex;
    slingAnchorWorldPos = engine.getObject (objectIndex).position;
    slingCursorScreenPos = objectToScreen (slingAnchorWorldPos);

    // If Ctrl/Alt already happen to be held when the gesture starts, take
    // that as the starting state directly (rather than requiring a
    // press-transition first) -- but don't auto-cycle the eccentricity
    // step just because Alt happens to already be down; that only
    // advances on an actual press (see updateSlingModifiers()).
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    slingWantsOrbit = mods.isCtrlDown();
    slingEccentricityStepIndex = 0;
    slingPrevCtrlDown = mods.isCtrlDown();
    slingPrevAltDown = mods.isAltDown();
}

void SpatialAudioPOCEditor::updateSlingModifiers()
{
    if (! slingActive) return;

    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    const bool ctrlDown = mods.isCtrlDown();
    const bool altDown = mods.isAltDown();

    // Edge-detected (only on the down-transition), so holding a key
    // doesn't repeatedly toggle/cycle every timer tick.
    if (ctrlDown && ! slingPrevCtrlDown)
        slingWantsOrbit = ! slingWantsOrbit;

    if (altDown && ! slingPrevAltDown)
        slingEccentricityStepIndex = (slingEccentricityStepIndex + 1)
                                      % (int) SlingGesture::orbitEccentricitySteps.size();

    slingPrevCtrlDown = ctrlDown;
    slingPrevAltDown = altDown;
}

void SpatialAudioPOCEditor::releaseSling()
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    const Vec3 releasePoint = screenToObject (slingCursorScreenPos);
    const Vec3 pullVector = SlingGesture::computePullVector (slingAnchorWorldPos, releasePoint);

    // Below the threshold: treat like a plain click that didn't turn into
    // a real drag -- no shot, object keeps whatever mode/position it had
    // (it was never touched while aiming).
    if (pullVector.length() >= SlingGesture::minPullDistanceMeters)
    {
        const Vec3 launchDirection = pullVector; // already points opposite the drag, i.e. the launch direction

        if (slingWantsOrbit)
        {
            const Vec3 center { 0.0f, 0.0f, 0.0f }; // always the origin, see design decision in the feature discussion
            const float semiMajor = juce::jmax (SlingGesture::minOrbitRadiusMeters,
                                                 pullVector.length() * SlingGesture::orbitRadiusScale);
            const float orientation = SlingGesture::computeOrbitOrientation (pullVector);
            const float directionSign = SlingGesture::computeOrbitDirectionSign (slingAnchorWorldPos - center, launchDirection);
            const float eccentricity = SlingGesture::orbitEccentricitySteps[(size_t) slingEccentricityStepIndex].eccentricity;

            engine.startOrbit (slingObjectIndex, center, semiMajor,
                                directionSign * SlingGesture::orbitAngularSpeedMagnitude,
                                eccentricity, orientation);
        }
        else
        {
            engine.throwObject (slingObjectIndex, launchDirection * SlingGesture::throwVelocityScale);
        }
    }

    slingActive = false;
    slingObjectIndex = -1;
}

void SpatialAudioPOCEditor::mouseDown (const juce::MouseEvent& e)
{
    const int hit = findObjectNear (e.position);

    // Shift+click on an object starts the sling gesture instead of the
    // ordinary free drag -- deliberately a modifier key, not just "drag
    // starts a sling", so the two remain clearly distinguishable (see
    // class comment).
    if (hit >= 0 && e.mods.isShiftDown())
    {
        selectObject (hit);
        startSling (hit);
        return;
    }

    draggedObjectIndex = hit;
    selectObject (draggedObjectIndex); // -1 on click on empty space -> clear selection

    if (draggedObjectIndex >= 0)
    {
        lastDragScreenPos = e.position;
        lastDragTimeMs = juce::Time::getMillisecondCounter();
        estimatedDragVelocity = { 0.0f, 0.0f, 0.0f };
    }
}

void SpatialAudioPOCEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (slingActive)
    {
        // The real object position is never touched while aiming (see
        // startSling()) -- only this cursor position, which paint() uses
        // to draw the object "following" the cursor and the bow line.
        slingCursorScreenPos = e.position;
        repaint();
        return;
    }

    if (draggedObjectIndex < 0) return;

    if (! physicsDragActive)
    {
        // Only switch into the manual motion mode once there's actually
        // some movement -- a plain click (for selecting) should NOT
        // interrupt a running orbit/impulse.
        if (e.position.getDistanceFrom (lastDragScreenPos) < 3.0f)
            return;

        physicsDragActive = true;
        audioProcessor.getTrajectoryEngine().beginDrag (draggedObjectIndex);
    }

    const auto now = juce::Time::getMillisecondCounter();
    const double dt = juce::jmax (0.001, (double) (now - lastDragTimeMs) / 1000.0);

    auto newPos = screenToObject (e.position);
    auto& engine = audioProcessor.getTrajectoryEngine();
    auto oldPos = engine.getObject (draggedObjectIndex).position;

    estimatedDragVelocity = (newPos - oldPos) / (float) dt;
    engine.dragTo (draggedObjectIndex, newPos);

    lastDragScreenPos = e.position;
    lastDragTimeMs = now;
}

void SpatialAudioPOCEditor::mouseUp (const juce::MouseEvent&)
{
    if (slingActive)
    {
        releaseSling();
        return;
    }

    if (draggedObjectIndex < 0) return;

    if (physicsDragActive)
    {
        auto& engine = audioProcessor.getTrajectoryEngine();

        // If there's still noticeable momentum on release: interpret it as a throw.
        if (estimatedDragVelocity.length() > 0.3f)
            engine.throwObject (draggedObjectIndex, estimatedDragVelocity);
        else
            engine.endDrag (draggedObjectIndex);
    }
    // Plain click without movement (physicsDragActive == false): leave the
    // object's mode unchanged, see mouseDrag() -- only the selection
    // (already set in mouseDown) counts.

    draggedObjectIndex = -1;
    physicsDragActive = false;
}

void SpatialAudioPOCEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int idx = findObjectNear (e.position);
    if (idx < 0) return;

    auto& engine = audioProcessor.getTrajectoryEngine();
    auto& obj = engine.getObject (idx);

    if (obj.mode == SoundObject::Mode::Orbit)
    {
        engine.endDrag (idx); // -> Static
    }
    else
    {
        const float radius = obj.position.length();
        engine.startOrbit (idx, { 0.0f, 0.0f, 0.0f }, juce::jmax (radius, 0.5f), 1.0f);
    }
}
