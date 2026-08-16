#include "PluginEditor.h"

SpatialAudioPOCEditor::SpatialAudioPOCEditor (SpatialAudioPOCProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setSize (700, 700);
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

    processor.getTrajectoryEngine().update (dt);
    repaint();
}

juce::Point<float> SpatialAudioPOCEditor::objectToScreen (juce::Vector3D<float> pos) const
{
    const auto c = getLocalBounds().toFloat().getCentre();
    // Bildschirm-y zeigt nach unten, Raum-y (links) soll optisch nach oben-links,
    // daher x -> Bildschirm-y (vorne = oben), y -> Bildschirm-x (links = links).
    return { c.x - pos.y * pixelsPerMeter, c.y - pos.x * pixelsPerMeter };
}

juce::Vector3D<float> SpatialAudioPOCEditor::screenToObject (juce::Point<float> screenPos) const
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float raumX = (c.y - screenPos.y) / pixelsPerMeter;
    const float raumY = (c.x - screenPos.x) / pixelsPerMeter;
    return { raumX, raumY, 0.0f };
}

int SpatialAudioPOCEditor::findObjectNear (juce::Point<float> screenPos) const
{
    auto& engine = processor.getTrajectoryEngine();
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

    const auto centre = getLocalBounds().toFloat().getCentre();

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
    g.drawText ("Vorne", centre.x - 30, centre.y - pixelsPerMeter * 3 - 20, 60, 20, juce::Justification::centred);

    auto& engine = processor.getTrajectoryEngine();
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

void SpatialAudioPOCEditor::resized() {}

void SpatialAudioPOCEditor::mouseDown (const juce::MouseEvent& e)
{
    draggedObjectIndex = findObjectNear (e.position);
    if (draggedObjectIndex >= 0)
    {
        processor.getTrajectoryEngine().beginDrag (draggedObjectIndex);
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
    auto& engine = processor.getTrajectoryEngine();
    auto oldPos = engine.getObject (draggedObjectIndex).position;

    estimatedDragVelocity = (newPos - oldPos) / (float) dt;
    engine.dragTo (draggedObjectIndex, newPos);

    lastDragScreenPos = e.position;
    lastDragTimeMs = now;
}

void SpatialAudioPOCEditor::mouseUp (const juce::MouseEvent&)
{
    if (draggedObjectIndex < 0) return;

    auto& engine = processor.getTrajectoryEngine();

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

    auto& engine = processor.getTrajectoryEngine();
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
