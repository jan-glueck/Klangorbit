#include "PluginEditor.h"
#include "PresetManager.h"

SpatialAudioPOCEditor::SpatialAudioPOCEditor (SpatialAudioPOCProcessor& p)
    : juce::AudioProcessorEditor (&p), audioProcessor (p)
{
    addAndMakeVisible (loadPresetButton);
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (presetStatusLabel);

    loadPresetButton.onClick = [this] { loadPresetClicked(); };
    savePresetButton.onClick = [this] { savePresetClicked(); };

    presetStatusLabel.setText (currentPresetName, juce::dontSendNotification);
    presetStatusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    presetStatusLabel.setJustificationType (juce::Justification::centredLeft);

    setSize (700, 700 + toolbarHeight);
    setWantsKeyboardFocus (true);
    lastTimerMs = juce::Time::getMillisecondCounter();
    startTimerHz (90); // Control-Rate fuer die TrajectoryEngine
}

SpatialAudioPOCEditor::~SpatialAudioPOCEditor()
{
    stopTimer();
}

void SpatialAudioPOCEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const double dt = juce::jlimit (0.0, 0.1, (double) (now - lastTimerMs) / 1000.0); // clamp gegen Ausreisser
    lastTimerMs = now;

    audioProcessor.getTrajectoryEngine().update (dt);
    repaint();
}

juce::Point<float> SpatialAudioPOCEditor::objectToScreen (Vec3 pos) const
{
    const auto c = viewArea.toFloat().getCentre();
    // Bildschirm-y zeigt nach unten, Raum-y (links) soll optisch nach oben-links,
    // daher x -> Bildschirm-y (vorne = oben), y -> Bildschirm-x (links = links).
    return { c.x - pos.y * pixelsPerMeter, c.y - pos.x * pixelsPerMeter };
}

Vec3 SpatialAudioPOCEditor::screenToObject (juce::Point<float> screenPos) const
{
    const auto c = viewArea.toFloat().getCentre();
    const float raumX = (c.y - screenPos.y) / pixelsPerMeter;
    const float raumY = (c.x - screenPos.x) / pixelsPerMeter;
    return { raumX, raumY, 0.0f };
}

int SpatialAudioPOCEditor::findObjectNear (juce::Point<float> screenPos) const
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        auto p = objectToScreen (engine.getObject (i).position);
        if (p.getDistanceFrom (screenPos) <= hitRadiusPixels)
            return i;
    }
    return -1;
}

void SpatialAudioPOCEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto centre = viewArea.toFloat().getCentre();

    // Referenzkreise (1m/2m/3m) als Orientierungshilfe
    g.setColour (juce::Colours::darkgrey);
    for (int m = 1; m <= 3; ++m)
    {
        const float r = (float) m * pixelsPerMeter;
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.0f);
    }
    // Hoerposition/Ursprung
    g.setColour (juce::Colours::white);
    g.drawLine (centre.x - 10, centre.y, centre.x + 10, centre.y);
    g.drawLine (centre.x, centre.y - 10, centre.x, centre.y + 10);
    g.drawText ("Vorne", (int) (centre.x - 30), (int) (centre.y - pixelsPerMeter * 3 - 20), 60, 20, juce::Justification::centred);

    auto& engine = audioProcessor.getTrajectoryEngine();
    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        auto& obj = engine.getObject (i);
        if (obj.inputChannel < 0) continue; // kein aktives Objekt

        auto p = objectToScreen (obj.position);

        g.setColour (i == draggedObjectIndex ? juce::Colours::yellow : juce::Colours::cyan);
        g.fillEllipse (p.x - 8, p.y - 8, 16, 16);
        g.setColour (juce::Colours::white);
        g.drawText (juce::String (i), (int) p.x - 20, (int) p.y + 10, 40, 16, juce::Justification::centred);
    }
}

void SpatialAudioPOCEditor::resized()
{
    auto bounds = getLocalBounds();
    auto toolbar = bounds.removeFromTop (toolbarHeight);

    loadPresetButton.setBounds (toolbar.removeFromLeft (140).reduced (4));
    savePresetButton.setBounds (toolbar.removeFromLeft (140).reduced (4));
    presetStatusLabel.setBounds (toolbar.reduced (4));

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

    fileChooser = std::make_unique<juce::FileChooser> ("Preset laden", startDir, "*.json");

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
                showPresetError ("Preset konnte nicht geladen werden", result.getErrorMessage());
                return;
            }

            currentPresetName = loadedName;
            presetStatusLabel.setText ("Geladen: " + currentPresetName, juce::dontSendNotification);
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

    fileChooser = std::make_unique<juce::FileChooser> ("Preset speichern",
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
                showPresetError ("Preset konnte nicht gespeichert werden", result.getErrorMessage());
                return;
            }

            currentPresetName = name;
            presetStatusLabel.setText ("Gespeichert: " + currentPresetName, juce::dontSendNotification);
        });
}

void SpatialAudioPOCEditor::mouseDown (const juce::MouseEvent& e)
{
    draggedObjectIndex = findObjectNear (e.position);
    if (draggedObjectIndex >= 0)
    {
        audioProcessor.getTrajectoryEngine().beginDrag (draggedObjectIndex);
        lastDragScreenPos = e.position;
        lastDragTimeMs = juce::Time::getMillisecondCounter();
        estimatedDragVelocity = { 0.0f, 0.0f, 0.0f };
    }
}

void SpatialAudioPOCEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedObjectIndex < 0) return;

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
    if (draggedObjectIndex < 0) return;

    auto& engine = audioProcessor.getTrajectoryEngine();

    // Wenn beim Loslassen noch spuerbar Schwung da ist: als Wurf interpretieren.
    if (estimatedDragVelocity.length() > 0.3f)
        engine.throwObject (draggedObjectIndex, estimatedDragVelocity);
    else
        engine.endDrag (draggedObjectIndex);

    draggedObjectIndex = -1;
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
