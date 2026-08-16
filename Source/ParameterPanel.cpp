#include "ParameterPanel.h"

namespace
{
    void styleRowLabel (juce::Label& l, const juce::String& text, float fontSize, juce::Colour colour)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (fontSize)));
        l.setColour (juce::Label::textColourId, colour);
    }
}

// ============================================================== FloatRowComponent

FloatRowComponent::FloatRowComponent (const juce::String& name, double min, double max, double step)
{
    styleRowLabel (label, name, 13.0f, juce::Colours::lightgrey);
    addAndMakeVisible (label);

    slider.setRange (min, max, step);
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    addAndMakeVisible (slider);

    slider.onValueChange = [this] { if (onValueChanged) onValueChanged ((float) slider.getValue()); };
}

void FloatRowComponent::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (16));
    slider.setBounds (b);
}

void FloatRowComponent::setValueQuiet (float v)
{
    slider.setValue ((double) v, juce::dontSendNotification);
}

// ============================================================== Vec3RowComponent

Vec3RowComponent::Vec3RowComponent (const juce::String& name, double min, double max, double step)
{
    styleRowLabel (label, name, 13.0f, juce::Colours::lightgrey);
    addAndMakeVisible (label);

    setupAxis (xLabel, xSlider, "X", min, max, step);
    setupAxis (yLabel, ySlider, "Y", min, max, step);
    setupAxis (zLabel, zSlider, "Z", min, max, step);

    auto notify = [this]
    {
        if (onValueChanged)
            onValueChanged ({ (float) xSlider.getValue(), (float) ySlider.getValue(), (float) zSlider.getValue() });
    };
    xSlider.onValueChange = notify;
    ySlider.onValueChange = notify;
    zSlider.onValueChange = notify;
}

void Vec3RowComponent::setupAxis (juce::Label& l, juce::Slider& s, const juce::String& axisName,
                                   double min, double max, double step)
{
    styleRowLabel (l, axisName, 11.0f, juce::Colours::grey);
    addAndMakeVisible (l);

    s.setRange (min, max, step);
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    addAndMakeVisible (s);
}

void Vec3RowComponent::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (16));

    auto layoutAxis = [&] (juce::Label& l, juce::Slider& s)
    {
        auto row = b.removeFromTop (20);
        l.setBounds (row.removeFromLeft (16));
        s.setBounds (row);
    };
    layoutAxis (xLabel, xSlider);
    layoutAxis (yLabel, ySlider);
    layoutAxis (zLabel, zSlider);
}

void Vec3RowComponent::setValueQuiet (Vec3 v)
{
    xSlider.setValue ((double) v.x, juce::dontSendNotification);
    ySlider.setValue ((double) v.y, juce::dontSendNotification);
    zSlider.setValue ((double) v.z, juce::dontSendNotification);
}

// ============================================================== ComboRowComponent

ComboRowComponent::ComboRowComponent (const juce::String& name)
{
    styleRowLabel (label, name, 13.0f, juce::Colours::lightgrey);
    addAndMakeVisible (label);
    addAndMakeVisible (combo);

    combo.onChange = [this] { if (onSelected) onSelected (combo.getSelectedItemIndex()); };
}

void ComboRowComponent::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (16));
    combo.setBounds (b.removeFromTop (22));
}

// ============================================================== ParameterPanel

ParameterPanel::ParameterPanel()
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    // --- Scene (always visible) -----------------------------------------
    addHeader ("Scene");

    addSceneFloatRow ("Room Size (0 = no boundary)", &SceneSettings::roomSize, 0.0, 50.0, 0.1);

    boundaryRow = std::make_unique<ComboRowComponent> ("Boundary Behavior");
    boundaryRow->combo.addItem ("Reflect", 1);
    boundaryRow->combo.addItem ("Wrap", 2);
    boundaryRow->combo.addItem ("Absorb", 3);
    boundaryRow->onSelected = [this] (int index)
    {
        if (sceneSettings != nullptr)
            sceneSettings->boundaryBehavior = (SceneSettings::BoundaryBehavior) index;
    };
    content.addAndMakeVisible (*boundaryRow);
    addToLayout (*boundaryRow, ComboRowComponent::preferredHeight);

    globalFieldRow = std::make_unique<Vec3RowComponent> ("Global Field (Wind/Gravity)", -20.0, 20.0, 0.01);
    globalFieldRow->onValueChanged = [this] (Vec3 v) { if (sceneSettings != nullptr) sceneSettings->globalField = v; };
    content.addAndMakeVisible (*globalFieldRow);
    addToLayout (*globalFieldRow, Vec3RowComponent::preferredHeight);

    addSceneFloatRow ("Time Scale (timeScale)", &SceneSettings::timeScale, 0.05, 5.0, 0.01);

    addHeader ("Acoustic Propagation");
    addSceneFloatRow ("Speed of Sound (m/s)", &SceneSettings::speedOfSound, 1.0, 400.0, 1.0);
    addSceneFloatRow ("Temperature (C)", &SceneSettings::temperature, -20.0, 45.0, 0.5);
    addSceneFloatRow ("Relative Humidity (%)", &SceneSettings::relativeHumidity, 0.0, 100.0, 1.0);
    addSceneFloatRow ("Atmospheric Pressure (kPa)", &SceneSettings::atmosphericPressure, 80.0, 110.0, 0.1);
    addSceneVec3Row ("Wind (m/s)", &SceneSettings::windVector, -50.0, 50.0, 0.1);

    // --- Object ------------------------------------------------------------
    styleRowLabel (objectHeaderLabel, "No object selected", 15.0f, juce::Colours::white);
    content.addAndMakeVisible (objectHeaderLabel);
    addToLayout (objectHeaderLabel, 24);

    modeRow = std::make_unique<ComboRowComponent> ("Mode");
    modeRow->combo.addItem ("Static", 1);
    modeRow->combo.addItem ("Manual", 2);
    modeRow->combo.addItem ("Orbit", 3);
    modeRow->combo.addItem ("Impulse", 4);
    modeRow->combo.addItem ("Attracted", 5);
    modeRow->onSelected = [this] (int index)
    {
        if (editedObject != nullptr)
            editedObject->mode = (SoundObject::Mode) index;
    };
    content.addAndMakeVisible (*modeRow);
    addToLayout (*modeRow, ComboRowComponent::preferredHeight);
    objectOnlyComponents.push_back (modeRow.get());

    addHeader ("Motion / Inertia");
    objectOnlyComponents.push_back (&addObjectFloatRow ("Mass", &SoundObject::mass, 0.01, 20.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Gain", &SoundObject::gain, 0.0, 2.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Damping (simple decay)", &SoundObject::damping, 0.0, 1.0, 0.001));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Max Velocity (0 = unlimited)", &SoundObject::maxVelocity, 0.0, 30.0, 0.1));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Drag Coefficient", &SoundObject::dragCoefficient, 0.0, 10.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Restitution (wall bounce)", &SoundObject::restitution, 0.0, 1.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Stop Threshold (velocitySnapThreshold)", &SoundObject::velocitySnapThreshold, 0.0, 1.0, 0.001));

    addHeader ("Attraction / Repulsion (as source)");
    objectOnlyComponents.push_back (&addObjectFloatRow ("Strength (negative = repulsive)", &SoundObject::attractionStrength, -10.0, 10.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Force Exponent", &SoundObject::forceExponent, 1.0, 3.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Min. Distance (softening)", &SoundObject::minDistance, 0.01, 2.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Max. Range (0 = unlimited)", &SoundObject::maxRange, 0.0, 20.0, 0.1));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Pulse Rate (Hz, 0 = off)", &SoundObject::attractionPulseRate, 0.0, 5.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Pulse Depth", &SoundObject::attractionPulseDepth, 0.0, 1.0, 0.01));

    addHeader ("Orbit");
    objectOnlyComponents.push_back (&addObjectVec3Row ("Orbit Center (fixed point)", &SoundObject::orbitCenter, -20.0, 20.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Orbit Radius", &SoundObject::orbitRadius, 0.05, 10.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Orbit Angular Speed (rad/s)", &SoundObject::orbitAngularSpeed, -10.0, 10.0, 0.01));
    objectOnlyComponents.push_back (&addObjectVec3Row ("Orbit Plane Normal", &SoundObject::orbitPlaneNormal, -1.0, 1.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Orbit Eccentricity", &SoundObject::orbitEccentricity, 0.0, 0.95, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Orbit Decay (m/s)", &SoundObject::orbitDecay, -2.0, 2.0, 0.01));

    orbitRefRow = std::make_unique<ComboRowComponent> ("Orbit Reference Object");
    orbitRefRow->onSelected = [this] (int index)
    {
        if (editedObject != nullptr)
            editedObject->orbitReferenceObjectId = index - 1; // index 0 = "Fixed", see rebuildOrbitReferenceItems()
    };
    content.addAndMakeVisible (*orbitRefRow);
    addToLayout (*orbitRefRow, ComboRowComponent::preferredHeight);
    objectOnlyComponents.push_back (orbitRefRow.get());

    addHeader ("Doppler / Directivity");
    objectOnlyComponents.push_back (&addObjectFloatRow ("Doppler Factor (0=off, 1=physical)", &SoundObject::dopplerFactor, 0.0, 5.0, 0.01));
    objectOnlyComponents.push_back (&addObjectFloatRow ("Doppler Smoothing (s)", &SoundObject::dopplerSmoothing, 0.0, 2.0, 0.01));

    directivityRow = std::make_unique<ComboRowComponent> ("Directivity Pattern");
    directivityRow->combo.addItem ("Omni", 1);
    directivityRow->combo.addItem ("Cardioid", 2);
    directivityRow->combo.addItem ("Figure-8", 3);
    directivityRow->onSelected = [this] (int index)
    {
        if (editedObject != nullptr)
            editedObject->directivityPattern = (SoundObject::DirectivityPattern) index;
    };
    content.addAndMakeVisible (*directivityRow);
    addToLayout (*directivityRow, ComboRowComponent::preferredHeight);
    objectOnlyComponents.push_back (directivityRow.get());

    objectOnlyComponents.push_back (&addObjectVec3Row ("Source Orientation", &SoundObject::sourceOrientation, -1.0, 1.0, 0.01));

    for (auto* c : objectOnlyComponents)
        c->setEnabled (false);
}

FloatRowComponent& ParameterPanel::addSceneFloatRow (const juce::String& name, float SceneSettings::* member,
                                                       double min, double max, double step)
{
    auto row = std::make_unique<FloatRowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (float v) { if (sceneSettings != nullptr) sceneSettings->*member = v; };
    content.addAndMakeVisible (*row);
    addToLayout (*row, FloatRowComponent::preferredHeight);

    sceneFloatRows.push_back ({ std::move (row), member });
    return *sceneFloatRows.back().row;
}

Vec3RowComponent& ParameterPanel::addSceneVec3Row (const juce::String& name, Vec3 SceneSettings::* member,
                                                     double min, double max, double step)
{
    auto row = std::make_unique<Vec3RowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (Vec3 v) { if (sceneSettings != nullptr) sceneSettings->*member = v; };
    content.addAndMakeVisible (*row);
    addToLayout (*row, Vec3RowComponent::preferredHeight);

    sceneVec3Rows.push_back ({ std::move (row), member });
    return *sceneVec3Rows.back().row;
}

FloatRowComponent& ParameterPanel::addObjectFloatRow (const juce::String& name, float SoundObject::* member,
                                                        double min, double max, double step)
{
    auto row = std::make_unique<FloatRowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (float v) { if (editedObject != nullptr) editedObject->*member = v; };
    content.addAndMakeVisible (*row);
    addToLayout (*row, FloatRowComponent::preferredHeight);

    objectFloatRows.push_back ({ std::move (row), member });
    return *objectFloatRows.back().row;
}

Vec3RowComponent& ParameterPanel::addObjectVec3Row (const juce::String& name, Vec3 SoundObject::* member,
                                                      double min, double max, double step)
{
    auto row = std::make_unique<Vec3RowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (Vec3 v) { if (editedObject != nullptr) editedObject->*member = v; };
    content.addAndMakeVisible (*row);
    addToLayout (*row, Vec3RowComponent::preferredHeight);

    objectVec3Rows.push_back ({ std::move (row), member });
    return *objectVec3Rows.back().row;
}

juce::Label& ParameterPanel::addHeader (const juce::String& text)
{
    auto header = std::make_unique<juce::Label>();
    styleRowLabel (*header, text, 15.0f, juce::Colours::white);
    content.addAndMakeVisible (*header);
    addToLayout (*header, 24);

    headers.push_back (std::move (header));
    return *headers.back();
}

void ParameterPanel::addToLayout (juce::Component& c, int height)
{
    layoutOrder.push_back (&c);
    layoutHeights.push_back (height);
}

void ParameterPanel::setSceneSettings (SceneSettings* settings)
{
    sceneSettings = settings;
}

void ParameterPanel::rebuildOrbitReferenceItems (int numObjects, int selfId)
{
    auto& combo = orbitRefRow->combo;
    combo.clear (juce::dontSendNotification);
    combo.addItem ("Fixed (orbit center)", 1);
    for (int i = 0; i < numObjects; ++i)
        combo.addItem ("Object " + juce::String (i) + (i == selfId ? " (itself -- ignored)" : ""), i + 2);
}

void ParameterPanel::setEditedObject (SoundObject* obj, int objectIndexForHeader, int numObjects)
{
    editedObject = obj;

    if (obj == nullptr)
    {
        objectHeaderLabel.setText ("No object selected", juce::dontSendNotification);
        for (auto* c : objectOnlyComponents)
            c->setEnabled (false);
        return;
    }

    objectHeaderLabel.setText ("Object " + juce::String (objectIndexForHeader), juce::dontSendNotification);
    for (auto* c : objectOnlyComponents)
        c->setEnabled (true);

    rebuildOrbitReferenceItems (numObjects, obj->id);
    refreshFromModel();
}

void ParameterPanel::refreshFromModel()
{
    if (sceneSettings != nullptr)
    {
        for (auto& b : sceneFloatRows)
            b.row->setValueQuiet (sceneSettings->*b.member);
        for (auto& b : sceneVec3Rows)
            b.row->setValueQuiet (sceneSettings->*b.member);

        boundaryRow->combo.setSelectedItemIndex ((int) sceneSettings->boundaryBehavior, juce::dontSendNotification);
        globalFieldRow->setValueQuiet (sceneSettings->globalField);
    }

    if (editedObject == nullptr)
        return;

    for (auto& b : objectFloatRows)
        b.row->setValueQuiet (editedObject->*b.member);
    for (auto& b : objectVec3Rows)
        b.row->setValueQuiet (editedObject->*b.member);

    modeRow->combo.setSelectedItemIndex ((int) editedObject->mode, juce::dontSendNotification);
    orbitRefRow->combo.setSelectedItemIndex (editedObject->orbitReferenceObjectId + 1, juce::dontSendNotification);
    directivityRow->combo.setSelectedItemIndex ((int) editedObject->directivityPattern, juce::dontSendNotification);
}

void ParameterPanel::resized()
{
    viewport.setBounds (getLocalBounds());

    const int width = juce::jmax (140, getWidth() - 24);
    int y = 6;
    for (size_t i = 0; i < layoutOrder.size(); ++i)
    {
        layoutOrder[i]->setBounds (6, y, width, layoutHeights[i]);
        y += layoutHeights[i] + 6;
    }
    content.setSize (width + 18, y);
}
