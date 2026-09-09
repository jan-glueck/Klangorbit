#include "ParameterPanel.h"
#include "UiTheme.h"
#include "PluginProcessor.h"
#include <cmath>

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
    styleRowLabel (label, name, 13.0f, UiColours::textSecondary());
    addAndMakeVisible (label);

    slider.setRange (min, max, step);
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    // Let mouse-wheel events pass through to the enclosing Viewport instead
    // of nudging the value -- with dozens of sliders in a scrollable panel,
    // "scroll the page" should win over "scroll whichever slider happens
    // to be under the cursor".
    slider.setScrollWheelEnabled (false);
    addAndMakeVisible (slider);

    slider.onValueChange = [this] { if (onValueChanged) onValueChanged ((float) slider.getValue()); };
}

void FloatRowComponent::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (16));
    b.removeFromTop (UiSpacing::xs);
    slider.setBounds (b);
}

void FloatRowComponent::setValueQuiet (float v)
{
    slider.setValue ((double) v, juce::dontSendNotification);
}

// ============================================================== Vec3RowComponent

Vec3RowComponent::Vec3RowComponent (const juce::String& name, double min, double max, double step)
{
    styleRowLabel (label, name, 13.0f, UiColours::textSecondary());
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
    styleRowLabel (l, axisName, 11.0f, UiColours::textDisabled());
    addAndMakeVisible (l);

    s.setRange (min, max, step);
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    s.setScrollWheelEnabled (false); // let the page scroll instead of nudging the value, see FloatRowComponent
    addAndMakeVisible (s);
}

void Vec3RowComponent::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (18));

    auto layoutAxis = [&] (juce::Label& l, juce::Slider& s)
    {
        auto row = b.removeFromTop (22);
        l.setBounds (row.removeFromLeft (20));
        row.removeFromLeft (UiSpacing::xs);
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
    styleRowLabel (label, name, 13.0f, UiColours::textSecondary());
    addAndMakeVisible (label);
    addAndMakeVisible (combo);

    combo.onChange = [this] { if (onSelected) onSelected (combo.getSelectedItemIndex()); };
}

void ComboRowComponent::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (16));
    b.removeFromTop (UiSpacing::xs);
    combo.setBounds (b.removeFromTop (24));
}

// ============================================================== ToggleRowComponent

ToggleRowComponent::ToggleRowComponent (const juce::String& name)
{
    toggle.setButtonText (name);
    toggle.setColour (juce::ToggleButton::textColourId, UiColours::textSecondary());
    addAndMakeVisible (toggle);

    toggle.onClick = [this] { if (onToggled) onToggled (toggle.getToggleState()); };
}

void ToggleRowComponent::resized()
{
    toggle.setBounds (getLocalBounds());
}

void ToggleRowComponent::setValueQuiet (bool v)
{
    toggle.setToggleState (v, juce::dontSendNotification);
}

// ============================================================== ParameterPanel

bool ParameterPanel::categoryRequiresObject (Category category)
{
    // Also doubles as resized()'s own "SCENE SETTINGS" vs. "OBJECT
    // SETTINGS" button-group partition, see the class comment.
    return category == Category::Object || category == Category::Attraction
        || category == Category::Orbit || category == Category::Doppler
        || category == Category::GrainCloud;
}

ParameterPanel::ParameterPanel()
{
    // Smaller, regular weight (not the section-title treatment it used to
    // have) -- now shares the "OBJECT SETTINGS" label's own row,
    // right-aligned (see resized()), rather than costing a row of its
    // own above or below the button group, since which object is
    // selected is only actually relevant to that group's own pages, not
    // to Scene Settings.
    styleRowLabel (objectHeaderLabel, "No object selected", 13.0f, UiColours::textPrimary());
    objectHeaderLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (objectHeaderLabel);

    auto styleGroupLabel = [] (juce::Label& l, const juce::String& text)
    {
        styleRowLabel (l, text, 11.0f, UiColours::textDisabled());
        l.setFont (l.getFont().withExtraKerningFactor (0.08f));
    };
    styleGroupLabel (sceneGroupLabel, "SCENE SETTINGS");
    addAndMakeVisible (sceneGroupLabel);
    styleGroupLabel (objectGroupLabel, "OBJECT SETTINGS");
    addAndMakeVisible (objectGroupLabel);

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    // Call order doesn't affect layout (resized() partitions by
    // categoryRequiresObject(), see the class comment) -- kept in the same
    // visual order the two groups render in purely for readability here.
    addCategoryButton ("Scene", Category::Scene);
    addCategoryButton ("Acoustics", Category::Acoustics);
    addCategoryButton ("Object", Category::Object);
    addCategoryButton ("Attraction", Category::Attraction);
    addCategoryButton ("Orbit", Category::Orbit);
    addCategoryButton ("Doppler", Category::Doppler);
    addCategoryButton ("Grains", Category::GrainCloud);

    // --- Scene ------------------------------------------------------------
    // Purely visual -- the boundary keeps applying physically even while
    // hidden, see SceneSettings::showRoomBoundary. Placed above Boundary
    // Size/Behavior (not below, as originally) since it's the natural
    // first question ("do I even see this?") before tuning the boundary
    // itself.
    showRoomBoundaryRow = std::make_unique<ToggleRowComponent> ("Show Boundary");
    showRoomBoundaryRow->onToggled = [this] (bool v) { if (sceneSettings != nullptr) sceneSettings->showRoomBoundary = v; };
    content.addAndMakeVisible (*showRoomBoundaryRow);
    addToLayout (*showRoomBoundaryRow, ToggleRowComponent::preferredHeight, Category::Scene);

    // Renamed from "Room Size" -- this sphere is a physics boundary
    // (reflect/wrap/absorb, see SceneSettings::boundaryBehavior), not a
    // "room" in any acoustic-modeling sense (no reverb/reflection audio
    // processing is tied to it), so "Boundary Size" names what it
    // actually does. Underlying field name (roomSize) left unchanged --
    // purely a display-string rename, no preset/schema impact.
    addSceneFloatRow ("Boundary Size (0 = no boundary)", &SceneSettings::roomSize, 0.0, 50.0, 0.1, Category::Scene);

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
    addToLayout (*boundaryRow, ComboRowComponent::preferredHeight, Category::Scene);

    // "Force Field" (not "Global Field") specifically to disambiguate from
    // "Propagation Wind" below (SceneSettings::windVector) -- this one is
    // a real physical force that pushes moving objects around (Impulse
    // mode); that one only shifts the effective speed of sound
    // for propagation delay/Doppler and never touches object motion at
    // all. Both are scene-wide (SceneSettings, Scope::Global -- see
    // buildParameterRegistry()), not per-object, so both stay here in
    // Scene Settings rather than moving into the (per-object) Doppler
    // category.
    globalFieldRow = std::make_unique<Vec3RowComponent> ("Force Field (Wind/Gravity)", -20.0, 20.0, 0.01);
    globalFieldRow->onValueChanged = [this] (Vec3 v) { if (sceneSettings != nullptr) sceneSettings->globalField = v; };
    globalFieldRow->setDefaultValue (SceneSettings{}.globalField);
    content.addAndMakeVisible (*globalFieldRow);
    addToLayout (*globalFieldRow, Vec3RowComponent::preferredHeight, Category::Scene);

    addSceneFloatRow ("Time Scale (timeScale)", &SceneSettings::timeScale, 0.05, 5.0, 0.01, Category::Scene);

    // --- Acoustics ----------------------------------------------------------
    // Its own category, not a section within Scene -- see the class
    // comment. Still SceneSettings fields (Scope::Global, not per-object),
    // so still grouped under "SCENE SETTINGS" in resized() -- just its
    // own tab within that group now.
    addSceneFloatRow ("Speed of Sound (m/s)", &SceneSettings::speedOfSound, 1.0, 400.0, 1.0, Category::Acoustics);
    addSceneFloatRow ("Temperature (C)", &SceneSettings::temperature, -20.0, 45.0, 0.5, Category::Acoustics);
    addSceneFloatRow ("Relative Humidity (%)", &SceneSettings::relativeHumidity, 0.0, 100.0, 1.0, Category::Acoustics);
    addSceneFloatRow ("Atmospheric Pressure (kPa)", &SceneSettings::atmosphericPressure, 80.0, 110.0, 0.1, Category::Acoustics);
    // "Propagation Wind" (not just "Wind") -- see Force Field's own
    // comment above for why: this only shifts the effective speed of
    // sound for propagation delay/Doppler, it never affects how objects
    // actually move (that's Force Field, in Scene). Lives in Acoustics,
    // not Scene, alongside the rest of the acoustic-medium settings it
    // conceptually belongs with -- separating it from Force Field is the
    // whole point of the rename/relocation (see the class comment).
    addSceneVec3Row ("Propagation Wind (m/s)", &SceneSettings::windVector, -50.0, 50.0, 0.1, Category::Acoustics);

    // --- Object -------------------------------------------------------------
    // Mute/Solo (SoundObject::muted/soloed) are deliberately NOT exposed
    // here -- they live exclusively as buttons on each row of the object
    // list sidebar (Source/ObjectListPanel.h), one control surface per
    // feature rather than duplicating it in two places.
    modeRow = std::make_unique<ComboRowComponent> ("Mode");
    modeRow->combo.addItem ("Static", 1);
    modeRow->combo.addItem ("Manual", 2);
    modeRow->combo.addItem ("Orbit", 3);
    modeRow->combo.addItem ("Impulse", 4);
    modeRow->onSelected = [this] (int index)
    {
        if (editedObject != nullptr)
            editedObject->mode = (SoundObject::Mode) index;
        updateOrbitModeHintVisibility();
    };
    content.addAndMakeVisible (*modeRow);
    addToLayout (*modeRow, ComboRowComponent::preferredHeight, Category::Object);

    // Not built via addObjectVec3Row() (which does a plain member-pointer
    // write) -- writing position must also force Mode::Manual and clear
    // manualVelocityActive, exactly what TrajectoryEngine::beginDrag()/
    // dragTo() already do for a mouse drag, so the physics integrator
    // doesn't fight this write on the very next tick (see
    // TrajectoryEngine::update()'s Manual-mode case). Same reasoning as
    // registerObjectPositionParam() in PluginProcessor.cpp, which this
    // mirrors for the GUI side. Like every other row here, this is NOT
    // refreshed on a timer (see class comment), so its shown value goes
    // stale while the object moves on its own (Orbit/Impulse) --
    // it reflects the position as of the last selection/preset-load, and
    // setting it always switches the object into Manual first.
    positionRow = std::make_unique<Vec3RowComponent> ("Position", -20.0, 20.0, 0.01);
    positionRow->onValueChanged = [this] (Vec3 v)
    {
        if (editedObject == nullptr) return;
        editedObject->mode = SoundObject::Mode::Manual;
        editedObject->manualVelocityActive = false;
        editedObject->position = v;
    };
    positionRow->setDefaultValue (SoundObject{}.position);
    content.addAndMakeVisible (*positionRow);
    addToLayout (*positionRow, Vec3RowComponent::preferredHeight, Category::Object);

    // See SoundObject::momentumEnabled's own comment -- only affects a
    // plain mouse drag's release behavior (KlangorbitEditor::mouseUp()),
    // not the Shift+drag sling gesture or gamepad throw buttons.
    momentumEnabledRow = std::make_unique<ToggleRowComponent> ("Momentum");
    momentumEnabledRow->onToggled = [this] (bool v) { if (editedObject != nullptr) editedObject->momentumEnabled = v; };
    content.addAndMakeVisible (*momentumEnabledRow);
    addToLayout (*momentumEnabledRow, ToggleRowComponent::preferredHeight, Category::Object);

    addObjectFloatRow ("Mass", &SoundObject::mass, 0.01, 20.0, 0.01, Category::Object);
    addObjectFloatRow ("Gain", &SoundObject::gain, 0.0, 2.0, 0.01, Category::Object);
    addObjectFloatRow ("Damping (simple decay)", &SoundObject::damping, 0.0, 1.0, 0.001, Category::Object);
    addObjectFloatRow ("Max Velocity (0 = unlimited)", &SoundObject::maxVelocity, 0.0, 30.0, 0.1, Category::Object);
    addObjectFloatRow ("Drag Coefficient", &SoundObject::dragCoefficient, 0.0, 10.0, 0.01, Category::Object);
    addObjectFloatRow ("Restitution (wall bounce)", &SoundObject::restitution, 0.0, 1.0, 0.01, Category::Object);
    addObjectFloatRow ("Stop Threshold (velocitySnapThreshold)", &SoundObject::velocitySnapThreshold, 0.0, 1.0, 0.001, Category::Object);

    // --- Attraction -----------------------------------------------------
    addObjectFloatRow ("Strength (negative = repulsive)", &SoundObject::attractionStrength, -10.0, 10.0, 0.01, Category::Attraction);
    addObjectFloatRow ("Force Exponent", &SoundObject::forceExponent, 1.0, 3.0, 0.01, Category::Attraction);
    addObjectFloatRow ("Min. Distance (softening)", &SoundObject::minDistance, 0.01, 2.0, 0.01, Category::Attraction);
    addObjectFloatRow ("Max. Range (0 = unlimited)", &SoundObject::maxRange, 0.0, 20.0, 0.1, Category::Attraction);
    addObjectFloatRow ("Pulse Rate (Hz, 0 = off)", &SoundObject::attractionPulseRate, 0.0, 5.0, 0.01, Category::Attraction);
    addObjectFloatRow ("Pulse Depth", &SoundObject::attractionPulseDepth, 0.0, 1.0, 0.01, Category::Attraction);

    // --- Orbit ----------------------------------------------------------
    // Every control below is inert until the object's own Mode (Object
    // category) is actually set to "Orbit" -- TrajectoryEngine::integrate()
    // only ever reads these fields in its Orbit case. orbitModeHintLabel
    // makes that visible instead of silently doing nothing; see
    // updateOrbitModeHintVisibility().
    styleRowLabel (orbitModeHintLabel, "Set the Object's Mode to \"Orbit\" (Object category) "
                                        "to start and edit an orbit -- these settings have no "
                                        "effect otherwise.", 12.5f, UiColours::solo());
    orbitModeHintLabel.setJustificationType (juce::Justification::topLeft);
    content.addAndMakeVisible (orbitModeHintLabel);
    addToLayout (orbitModeHintLabel, 48, Category::Orbit);

    addObjectVec3Row ("Orbit Center (fixed point)", &SoundObject::orbitCenter, -20.0, 20.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Orbit Radius", &SoundObject::orbitRadius, 0.05, 10.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Orbit Angular Speed (rad/s)", &SoundObject::orbitAngularSpeed, -10.0, 10.0, 0.01, Category::Orbit);
    addObjectVec3Row ("Orbit Plane Normal", &SoundObject::orbitPlaneNormal, -1.0, 1.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Orbit Eccentricity", &SoundObject::orbitEccentricity, 0.0, 0.95, 0.01, Category::Orbit);
    addObjectFloatRow ("Orbit Decay (m/s)", &SoundObject::orbitDecay, -2.0, 2.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Radius Baseline (mean-reverting)", &SoundObject::orbitRadiusBaseline, 0.05, 10.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Radius Reversion Rate", &SoundObject::orbitRadiusReversionRate, 0.0, 5.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Radius Noise Amplitude", &SoundObject::orbitRadiusNoiseAmplitude, 0.0, 5.0, 0.01, Category::Orbit);
    // 0 = raw per-tick Gaussian samples (unchanged default behavior); higher
    // low-pass-filters the noise source itself before it's scaled into the
    // radius update, so the wander looks like a smooth, organic "breathing"
    // instead of a jagged random walk. See TrajectoryEngine::integrate()'s
    // Orbit case and SoundObject::orbitRadiusNoiseSmoothed.
    addObjectFloatRow ("Radius Noise Smoothing (s)", &SoundObject::orbitRadiusNoiseSmoothing, 0.0, 5.0, 0.01, Category::Orbit);

    orbitRefRow = std::make_unique<ComboRowComponent> ("Orbit Reference Object");
    orbitRefRow->onSelected = [this] (int index)
    {
        if (editedObject != nullptr)
            editedObject->orbitReferenceObjectId = index - 1; // index 0 = "Fixed", see rebuildOrbitReferenceItems()
    };
    content.addAndMakeVisible (*orbitRefRow);
    addToLayout (*orbitRefRow, ComboRowComponent::preferredHeight, Category::Orbit);

    // --- Doppler ----------------------------------------------------------
    // Top of the category, on by default -- a quick on/off switch that
    // doesn't touch the Doppler Factor dial below it (see
    // SoundObject::dopplerEnabled's own comment: re-enabling restores
    // whatever factor was actually set, rather than needing to remember
    // and re-type a value that was overwritten to 0).
    dopplerEnabledRow = std::make_unique<ToggleRowComponent> ("Doppler Enabled");
    dopplerEnabledRow->onToggled = [this] (bool v) { if (editedObject != nullptr) editedObject->dopplerEnabled = v; };
    content.addAndMakeVisible (*dopplerEnabledRow);
    addToLayout (*dopplerEnabledRow, ToggleRowComponent::preferredHeight, Category::Doppler);

    addObjectFloatRow ("Doppler Factor (0=off, 1=physical)", &SoundObject::dopplerFactor, 0.0, 5.0, 0.01, Category::Doppler);
    addObjectFloatRow ("Doppler Smoothing (s)", &SoundObject::dopplerSmoothing, 0.0, 2.0, 0.01, Category::Doppler);

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
    addToLayout (*directivityRow, ComboRowComponent::preferredHeight, Category::Doppler);

    addObjectVec3Row ("Source Orientation", &SoundObject::sourceOrientation, -1.0, 1.0, 0.01, Category::Doppler);

    // --- Grains -------------------------------------------------------------
    grainEnabledRow = std::make_unique<ToggleRowComponent> ("Enabled");
    grainEnabledRow->onToggled = [this] (bool v) { if (editedGrainCloud != nullptr) editedGrainCloud->enabled = v; };
    content.addAndMakeVisible (*grainEnabledRow);
    addToLayout (*grainEnabledRow, ToggleRowComponent::preferredHeight, Category::GrainCloud);

    // Mutes just this object's own dry audio (independent of the
    // Mute/Solo buttons in the object list, which silence both source
    // and grains together) -- lets the grains be heard on their own,
    // isolated from the underlying signal they're granulated from. See
    // GrainCloudSettings::sourceMuted's own comment.
    grainSourceMutedRow = std::make_unique<ToggleRowComponent> ("Isolate Grains");
    grainSourceMutedRow->onToggled = [this] (bool v) { if (editedGrainCloud != nullptr) editedGrainCloud->sourceMuted = v; };
    content.addAndMakeVisible (*grainSourceMutedRow);
    addToLayout (*grainSourceMutedRow, ToggleRowComponent::preferredHeight, Category::GrainCloud);

    // Default off -- grains skip PropagationProcessor entirely for
    // performance, so this is a much cheaper, coarser approximation (see
    // GrainDoppler.h), opt-in rather than silently changing existing
    // grain-cloud sound. Uses the object's own "Doppler Factor" (Doppler
    // category) to scale strength, same as main-object Doppler.
    grainDopplerEnabledRow = std::make_unique<ToggleRowComponent> ("Doppler");
    grainDopplerEnabledRow->onToggled = [this] (bool v) { if (editedGrainCloud != nullptr) editedGrainCloud->dopplerEnabled = v; };
    content.addAndMakeVisible (*grainDopplerEnabledRow);
    addToLayout (*grainDopplerEnabledRow, ToggleRowComponent::preferredHeight, Category::GrainCloud);

    // Ranges for grainRate/grainDuration/positionJitterInBuffer are tied to
    // GrainLimits (Grain.h), the same constants the ring buffer is sized
    // from -- see there for why these three can't be extended
    // independently of the buffer without risking silent misbehavior.
    addGrainFloatRow ("Grain Rate (Spawn Rate) (grains/sec)", &GrainCloudSettings::grainRate, 0.1, GrainLimits::maxGrainRate, 0.1, Category::GrainCloud);
    addGrainFloatRow ("Grain Rate Jitter", &GrainCloudSettings::grainRateJitter, 0.0, 1.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Grain Duration (s)", &GrainCloudSettings::grainDuration, 0.01, GrainLimits::maxGrainDuration, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Grain Duration Jitter", &GrainCloudSettings::grainDurationJitter, 0.0, 1.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Pitch Jitter", &GrainCloudSettings::pitchJitter, 0.0, 1.0, 0.01, Category::GrainCloud);

    pitchJitterModeRow = std::make_unique<ComboRowComponent> ("Pitch Jitter Mode");
    pitchJitterModeRow->combo.addItem ("Random", 1);
    pitchJitterModeRow->combo.addItem ("Scale", 2);
    pitchJitterModeRow->onSelected = [this] (int index)
    {
        if (editedGrainCloud != nullptr)
            editedGrainCloud->pitchJitterMode = (PitchJitterMode) index;
        updatePitchJitterModeVisibility(); // show the Scale row only when it now applies
        layoutContent(); // heights above/below the now-hidden/shown row changed -- re-flow immediately
    };
    content.addAndMakeVisible (*pitchJitterModeRow);
    addToLayout (*pitchJitterModeRow, ComboRowComponent::preferredHeight, Category::GrainCloud);

    // Standard music-theory interval sets -- see Grain.h's own comment on
    // PitchQuantizeScale for what each one means (Octaves/Fifths are bare
    // intervals, not full scales; Acoustic is the "overtone scale"). Only
    // shown while Pitch Jitter Mode == Scale, see
    // updatePitchJitterModeVisibility().
    pitchQuantizeScaleRow = std::make_unique<ComboRowComponent> ("Pitch Quantize Scale");
    pitchQuantizeScaleRow->combo.addItem ("Octaves", 1);
    pitchQuantizeScaleRow->combo.addItem ("Fifths", 2);
    pitchQuantizeScaleRow->combo.addItem ("Major Triad", 3);
    pitchQuantizeScaleRow->combo.addItem ("Minor Triad", 4);
    pitchQuantizeScaleRow->combo.addItem ("Major Scale", 5);
    pitchQuantizeScaleRow->combo.addItem ("Dorian Scale", 6);
    pitchQuantizeScaleRow->combo.addItem ("Lydian Scale", 7);
    pitchQuantizeScaleRow->combo.addItem ("Mixolydian Scale", 8);
    pitchQuantizeScaleRow->combo.addItem ("Aeolian Scale", 9);
    pitchQuantizeScaleRow->combo.addItem ("Whole-Tone Scale", 10);
    pitchQuantizeScaleRow->combo.addItem ("Octatonic Scale", 11);
    pitchQuantizeScaleRow->combo.addItem ("Hexatonic Scale", 12);
    pitchQuantizeScaleRow->combo.addItem ("Acoustic Scale (Overtone Series)", 13);
    pitchQuantizeScaleRow->onSelected = [this] (int index)
    {
        if (editedGrainCloud != nullptr)
            editedGrainCloud->pitchQuantizeScale = (PitchQuantizeScale) index;
    };
    content.addChildComponent (*pitchQuantizeScaleRow); // hidden until updatePitchJitterModeVisibility() shows it
    addToLayout (*pitchQuantizeScaleRow, ComboRowComponent::preferredHeight, Category::GrainCloud);

    addGrainFloatRow ("Position Jitter In Buffer (s)", &GrainCloudSettings::positionJitterInBuffer, 0.0, GrainLimits::maxPositionJitterInBuffer, 0.01, Category::GrainCloud);

    // Bounded to GrainLimits::maxGrainReadDepthRange -- the same constant
    // the ring buffer is sized from (see Grain.h) -- so the user simply
    // cannot configure a depth beyond what's actually allocated; there is
    // no separate runtime clamp/warning needed because the slider itself
    // can't produce an out-of-range value.
    addGrainFloatRow ("Read Depth Min (s)", &GrainCloudSettings::grainReadDepthRangeMin, 0.0, GrainLimits::maxGrainReadDepthRange, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Read Depth Max (s)", &GrainCloudSettings::grainReadDepthRangeMax, 0.0, GrainLimits::maxGrainReadDepthRange, 0.01, Category::GrainCloud);

    grainReadDepthDistributionRow = std::make_unique<ComboRowComponent> ("Read Depth Distribution");
    grainReadDepthDistributionRow->combo.addItem ("Uniform", 1);
    grainReadDepthDistributionRow->combo.addItem ("Weighted Toward Recent", 2);
    grainReadDepthDistributionRow->combo.addItem ("Weighted Toward Old", 3);
    grainReadDepthDistributionRow->onSelected = [this] (int index)
    {
        if (editedGrainCloud != nullptr)
            editedGrainCloud->grainReadDepthDistribution = (GrainReadDepthDistribution) index;
    };
    content.addAndMakeVisible (*grainReadDepthDistributionRow);
    addToLayout (*grainReadDepthDistributionRow, ComboRowComponent::preferredHeight, Category::GrainCloud);

    addGrainIntRow ("Max Concurrent Grains (this cloud)", &GrainCloudSettings::maxConcurrentGrains,
                     1.0, (double) GrainLimits::maxConcurrentGrainsGlobal, Category::GrainCloud);

    // See updateGrainBudgetHintVisibility(): warns that the scene-wide
    // budget above is currently being split between more than one
    // object's cloud, so this cloud's own setting may not be fully
    // honored every tick (see PluginProcessor::timerCallback()'s
    // fair-share split) -- without this, a lower-than-configured grain
    // count could look like a bug rather than expected, shared-budget
    // behavior.
    styleRowLabel (grainBudgetHintLabel, "This scene's shared grain budget is currently split between "
                                          "more than one object -- this cloud may use fewer than its own "
                                          "Max Concurrent Grains setting while that's the case.", 12.5f, UiColours::solo());
    grainBudgetHintLabel.setJustificationType (juce::Justification::topLeft);
    content.addAndMakeVisible (grainBudgetHintLabel);
    addToLayout (grainBudgetHintLabel, 48, Category::GrainCloud);

    grainWindowShapeRow = std::make_unique<ComboRowComponent> ("Window Shape");
    grainWindowShapeRow->combo.addItem ("Hann", 1);
    grainWindowShapeRow->onSelected = [this] (int index)
    {
        if (editedGrainCloud != nullptr)
            editedGrainCloud->windowShape = (GrainWindowShape) index;
    };
    content.addAndMakeVisible (*grainWindowShapeRow);
    addToLayout (*grainWindowShapeRow, ComboRowComponent::preferredHeight, Category::GrainCloud);

    grainMovementModeRow = std::make_unique<ComboRowComponent> ("Movement Mode");
    grainMovementModeRow->combo.addItem ("Random Walk", 1);
    grainMovementModeRow->combo.addItem ("Bounce", 2);
    grainMovementModeRow->combo.addItem ("Radial Explosion", 3);
    grainMovementModeRow->combo.addItem ("Orbit Around Parent", 4);
    grainMovementModeRow->combo.addItem ("Attract/Repel Siblings", 5);
    grainMovementModeRow->onSelected = [this] (int index)
    {
        if (editedGrainCloud != nullptr)
            editedGrainCloud->movementMode = (GrainMovementMode) index;
        updateGrainMovementModeVisibility(); // show only the rows that apply to the newly selected mode
        layoutContent(); // heights above/below the now-hidden/shown rows changed -- re-flow immediately, don't wait for the next resize
    };
    content.addAndMakeVisible (*grainMovementModeRow);
    addToLayout (*grainMovementModeRow, ComboRowComponent::preferredHeight, Category::GrainCloud);

    // Each row below is tagged with the one movementMode it actually does
    // anything for (see addGrainFloatRow()'s own comment) -- Movement
    // Mode above then shows only the rows relevant to whichever mode is
    // currently selected, instead of all fifteen-ish at once regardless
    // of mode.
    addGrainFloatRow ("Random Walk Speed (m/s)", &GrainCloudSettings::randomWalkSpeed, 0.0, 10.0, 0.01, Category::GrainCloud, GrainMovementMode::RandomWalk);
    addGrainFloatRow ("Boundary Radius (m, Bounce)", &GrainCloudSettings::boundaryRadius, 0.05, 5.0, 0.01, Category::GrainCloud, GrainMovementMode::Bounce);
    addGrainFloatRow ("Boundary Radius Jitter (Bounce)", &GrainCloudSettings::boundaryRadiusJitter, 0.0, 1.0, 0.01, Category::GrainCloud, GrainMovementMode::Bounce);
    addGrainFloatRow ("Restitution (Bounce)", &GrainCloudSettings::restitution, 0.0, 1.0, 0.01, Category::GrainCloud, GrainMovementMode::Bounce);
    addGrainFloatRow ("Initial Speed (m/s, Explosion)", &GrainCloudSettings::initialSpeed, 0.0, 20.0, 0.01, Category::GrainCloud, GrainMovementMode::RadialExplosion);
    addGrainFloatRow ("Initial Speed Jitter (Explosion)", &GrainCloudSettings::initialSpeedJitter, 0.0, 1.0, 0.01, Category::GrainCloud, GrainMovementMode::RadialExplosion);
    addGrainFloatRow ("Acceleration (m/s^2, Explosion)", &GrainCloudSettings::acceleration, -20.0, 20.0, 0.01, Category::GrainCloud, GrainMovementMode::RadialExplosion);
    addGrainFloatRow ("Orbit Radius (m, OrbitAroundParent)", &GrainCloudSettings::orbitRadius, 0.05, 5.0, 0.01, Category::GrainCloud, GrainMovementMode::OrbitAroundParent);
    addGrainFloatRow ("Orbit Radius Jitter (OrbitAroundParent)", &GrainCloudSettings::orbitRadiusJitter, 0.0, 1.0, 0.01, Category::GrainCloud, GrainMovementMode::OrbitAroundParent);
    addGrainFloatRow ("Orbit Angular Speed (rad/s)", &GrainCloudSettings::orbitAngularSpeed, -10.0, 10.0, 0.01, Category::GrainCloud, GrainMovementMode::OrbitAroundParent);
    // 0 = flat, all grains circle in the same horizontal plane (original
    // behavior); 1 = each grain's own orbit plane is essentially random,
    // so over many grains/rotations the swept shape approaches a sphere.
    addGrainFloatRow ("Orbit Sphere Spread (2D -> 3D)", &GrainCloudSettings::orbitSphereSpread, 0.0, 1.0, 0.01, Category::GrainCloud, GrainMovementMode::OrbitAroundParent);
    addGrainFloatRow ("Attraction Strength (Siblings)", &GrainCloudSettings::attractionStrength, -10.0, 10.0, 0.01, Category::GrainCloud, GrainMovementMode::AttractRepelSiblings);

    updateCategoryButtonsEnabled();
    selectCategory (Category::Scene);
}

void ParameterPanel::addCategoryButton (const juce::String& label, Category category)
{
    auto button = std::make_unique<juce::TextButton> (label);
    button->setClickingTogglesState (false); // state is driven entirely by selectCategory(), see there
    button->setColour (juce::TextButton::buttonColourId, UiColours::bgRaised());
    button->setColour (juce::TextButton::buttonOnColourId, UiColours::accentDim());
    button->setColour (juce::TextButton::textColourOffId, UiColours::textSecondary());
    button->setColour (juce::TextButton::textColourOnId, UiColours::textPrimary());
    button->onClick = [this, category] { selectCategory (category); };
    addAndMakeVisible (*button);

    categoryButtons.push_back ({ std::move (button), category });
}

FloatRowComponent& ParameterPanel::addSceneFloatRow (const juce::String& name, float SceneSettings::* member,
                                                       double min, double max, double step, Category category)
{
    auto row = std::make_unique<FloatRowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (float v) { if (sceneSettings != nullptr) sceneSettings->*member = v; };
    // Double-click resets to SceneSettings's own default member
    // initializer -- a fresh, default-constructed instance, not the
    // currently edited one, so this is the schema default, not "whatever
    // it happened to start at."
    row->setDefaultValue (SceneSettings{}.*member);
    content.addAndMakeVisible (*row);
    addToLayout (*row, FloatRowComponent::preferredHeight, category);

    sceneFloatRows.push_back ({ std::move (row), member });
    return *sceneFloatRows.back().row;
}

Vec3RowComponent& ParameterPanel::addSceneVec3Row (const juce::String& name, Vec3 SceneSettings::* member,
                                                     double min, double max, double step, Category category)
{
    auto row = std::make_unique<Vec3RowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (Vec3 v) { if (sceneSettings != nullptr) sceneSettings->*member = v; };
    row->setDefaultValue (SceneSettings{}.*member);
    content.addAndMakeVisible (*row);
    addToLayout (*row, Vec3RowComponent::preferredHeight, category);

    sceneVec3Rows.push_back ({ std::move (row), member });
    return *sceneVec3Rows.back().row;
}

FloatRowComponent& ParameterPanel::addObjectFloatRow (const juce::String& name, float SoundObject::* member,
                                                        double min, double max, double step, Category category)
{
    auto row = std::make_unique<FloatRowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (float v) { if (editedObject != nullptr) editedObject->*member = v; };
    row->setDefaultValue (SoundObject{}.*member);
    content.addAndMakeVisible (*row);
    addToLayout (*row, FloatRowComponent::preferredHeight, category);

    objectFloatRows.push_back ({ std::move (row), member });
    return *objectFloatRows.back().row;
}

Vec3RowComponent& ParameterPanel::addObjectVec3Row (const juce::String& name, Vec3 SoundObject::* member,
                                                      double min, double max, double step, Category category)
{
    auto row = std::make_unique<Vec3RowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (Vec3 v) { if (editedObject != nullptr) editedObject->*member = v; };
    row->setDefaultValue (SoundObject{}.*member);
    content.addAndMakeVisible (*row);
    addToLayout (*row, Vec3RowComponent::preferredHeight, category);

    objectVec3Rows.push_back ({ std::move (row), member });
    return *objectVec3Rows.back().row;
}

FloatRowComponent& ParameterPanel::addGrainFloatRow (const juce::String& name, float GrainCloudSettings::* member,
                                                       double min, double max, double step, Category category,
                                                       std::optional<GrainMovementMode> requiredMovementMode)
{
    auto row = std::make_unique<FloatRowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (float v) { if (editedGrainCloud != nullptr) editedGrainCloud->*member = v; };
    row->setDefaultValue (GrainCloudSettings{}.*member);
    content.addAndMakeVisible (*row);
    addToLayout (*row, FloatRowComponent::preferredHeight, category, requiredMovementMode);

    grainFloatRows.push_back ({ std::move (row), member });
    return *grainFloatRows.back().row;
}

FloatRowComponent& ParameterPanel::addGrainIntRow (const juce::String& name, int GrainCloudSettings::* member,
                                                     double min, double max, Category category)
{
    auto row = std::make_unique<FloatRowComponent> (name, min, max, 1.0);
    row->onValueChanged = [this, member] (float v)
    {
        if (editedGrainCloud != nullptr)
            editedGrainCloud->*member = (int) std::round (v);
    };
    row->setDefaultValue ((float) (GrainCloudSettings{}.*member));
    content.addAndMakeVisible (*row);
    addToLayout (*row, FloatRowComponent::preferredHeight, category);

    grainIntRows.push_back ({ std::move (row), member });
    return *grainIntRows.back().row;
}

void ParameterPanel::addToLayout (juce::Component& c, int height, Category category,
                                   std::optional<GrainMovementMode> requiredMovementMode)
{
    layoutOrder.push_back (&c);
    layoutHeights.push_back (height);
    layoutCategory.push_back (category);
    layoutRequiredMovementMode.push_back (requiredMovementMode);
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
        // Displayed 1-based (i+1) -- purely cosmetic; the item ID (i+2,
        // decoded back to the real 0-based orbitReferenceObjectId in
        // onSelected above) and selfId comparison both stay on the actual
        // 0-based index i.
        combo.addItem ("Object " + juce::String (i + 1) + (i == selfId ? " (itself -- ignored)" : ""), i + 2);
}

void ParameterPanel::updateCategoryButtonsEnabled()
{
    const bool hasObject = editedObject != nullptr;
    for (auto& cb : categoryButtons)
        cb.button->setEnabled (! categoryRequiresObject (cb.category) || hasObject);
}

void ParameterPanel::selectCategory (Category category)
{
    currentCategory = category;

    for (size_t i = 0; i < layoutOrder.size(); ++i)
        layoutOrder[i]->setVisible (layoutCategory[i] == category);

    for (auto& cb : categoryButtons)
        cb.button->setToggleState (cb.category == category, juce::dontSendNotification);

    // Overrides the generic category-visibility pass above for this one
    // row specifically -- visible only while ALSO not in Orbit mode, not
    // just whenever the Orbit category happens to be showing.
    updateOrbitModeHintVisibility();

    // Same idea, for GrainCloud rows tagged with a specific
    // requiredMovementMode (see addToLayout()) -- further narrows what
    // the generic pass above just made visible.
    updateGrainMovementModeVisibility();
    updatePitchJitterModeVisibility();
    updateGrainBudgetHintVisibility();

    viewport.setViewPosition (0, 0);
    layoutContent();
}

void ParameterPanel::updateOrbitModeHintVisibility()
{
    orbitModeHintLabel.setVisible (currentCategory == Category::Orbit
                                    && editedObject != nullptr
                                    && editedObject->mode != SoundObject::Mode::Orbit);
}

void ParameterPanel::updateGrainMovementModeVisibility()
{
    if (currentCategory != Category::GrainCloud || editedGrainCloud == nullptr)
        return;

    for (size_t i = 0; i < layoutOrder.size(); ++i)
        if (layoutRequiredMovementMode[i].has_value())
            layoutOrder[i]->setVisible (*layoutRequiredMovementMode[i] == editedGrainCloud->movementMode);
}

void ParameterPanel::updatePitchJitterModeVisibility()
{
    if (currentCategory != Category::GrainCloud || editedGrainCloud == nullptr)
        return;

    pitchQuantizeScaleRow->setVisible (editedGrainCloud->pitchJitterMode == PitchJitterMode::Scale);
}

void ParameterPanel::setGrainBudgetOversubscribed (bool oversubscribed)
{
    if (grainBudgetOversubscribed == oversubscribed)
        return;

    grainBudgetOversubscribed = oversubscribed;
    updateGrainBudgetHintVisibility();
}

void ParameterPanel::updateGrainBudgetHintVisibility()
{
    grainBudgetHintLabel.setVisible (currentCategory == Category::GrainCloud && grainBudgetOversubscribed);
}

void ParameterPanel::setEditedObject (SoundObject* obj, int objectIndexForHeader, int numObjects)
{
    editedObject = obj;

    if (obj == nullptr)
    {
        objectHeaderLabel.setText ("No object selected", juce::dontSendNotification);
        updateCategoryButtonsEnabled();

        // Don't leave the panel stuck showing a page for an object that no
        // longer exists.
        if (categoryRequiresObject (currentCategory))
            selectCategory (Category::Scene);
        updateOrbitModeHintVisibility();
        return;
    }

    objectHeaderLabel.setText ("Object " + juce::String (objectIndexForHeader + 1), juce::dontSendNotification);
    updateCategoryButtonsEnabled();

    rebuildOrbitReferenceItems (numObjects, obj->id);
    refreshFromModel(); // also updates orbitModeHintLabel's visibility
}

void ParameterPanel::setEditedGrainCloud (GrainCloudSettings* settings)
{
    editedGrainCloud = settings;
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
        showRoomBoundaryRow->setValueQuiet (sceneSettings->showRoomBoundary);
        globalFieldRow->setValueQuiet (sceneSettings->globalField);
    }

    if (editedObject == nullptr)
        return;

    for (auto& b : objectFloatRows)
        b.row->setValueQuiet (editedObject->*b.member);
    for (auto& b : objectVec3Rows)
        b.row->setValueQuiet (editedObject->*b.member);

    positionRow->setValueQuiet (editedObject->position);
    momentumEnabledRow->setValueQuiet (editedObject->momentumEnabled);
    modeRow->combo.setSelectedItemIndex ((int) editedObject->mode, juce::dontSendNotification);
    orbitRefRow->combo.setSelectedItemIndex (editedObject->orbitReferenceObjectId + 1, juce::dontSendNotification);
    dopplerEnabledRow->setValueQuiet (editedObject->dopplerEnabled);
    directivityRow->combo.setSelectedItemIndex ((int) editedObject->directivityPattern, juce::dontSendNotification);
    updateOrbitModeHintVisibility(); // Mode may have changed (e.g. a preset load) without going through setEditedObject()

    if (editedGrainCloud == nullptr)
        return;

    grainEnabledRow->setValueQuiet (editedGrainCloud->enabled);
    grainSourceMutedRow->setValueQuiet (editedGrainCloud->sourceMuted);
    grainDopplerEnabledRow->setValueQuiet (editedGrainCloud->dopplerEnabled);
    for (auto& b : grainFloatRows)
        b.row->setValueQuiet (editedGrainCloud->*b.member);
    for (auto& b : grainIntRows)
        b.row->setValueQuiet ((float) (editedGrainCloud->*b.member));

    grainWindowShapeRow->combo.setSelectedItemIndex ((int) editedGrainCloud->windowShape, juce::dontSendNotification);
    grainMovementModeRow->combo.setSelectedItemIndex ((int) editedGrainCloud->movementMode, juce::dontSendNotification);
    grainReadDepthDistributionRow->combo.setSelectedItemIndex ((int) editedGrainCloud->grainReadDepthDistribution, juce::dontSendNotification);
    pitchJitterModeRow->combo.setSelectedItemIndex ((int) editedGrainCloud->pitchJitterMode, juce::dontSendNotification);
    pitchQuantizeScaleRow->combo.setSelectedItemIndex ((int) editedGrainCloud->pitchQuantizeScale, juce::dontSendNotification);
    updateGrainMovementModeVisibility(); // movementMode may have changed (e.g. a preset load, or switching selected object) without going through grainMovementModeRow's own onSelected
    updatePitchJitterModeVisibility(); // same idea, for pitchJitterMode
}

void ParameterPanel::layoutContent()
{
    constexpr int margin = UiSpacing::m;
    const int width = juce::jmax (140, viewport.getWidth() - margin * 2);
    int y = UiSpacing::s;
    for (size_t i = 0; i < layoutOrder.size(); ++i)
    {
        if (! layoutOrder[i]->isVisible())
            continue;
        layoutOrder[i]->setBounds (margin, y, width, layoutHeights[i]);
        y += layoutHeights[i] + UiSpacing::m;
    }
    content.setSize (width + margin * 2, y);
}

void ParameterPanel::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (UiSpacing::xs);

    constexpr int buttonHeight = 32;
    constexpr int buttonsPerRow = 2;
    constexpr int groupLabelHeight = 18;

    // Two visually distinct groups (see the class comment): "SCENE
    // SETTINGS" (categoryRequiresObject() == false) above "OBJECT
    // SETTINGS" (== true) below, each under its own small section label.
    // Rebuilt from categoryButtons every call rather than cached -- cheap
    // (at most a handful of buttons), and the button set itself never
    // changes after construction, so this is really just reusing the one
    // partition categoryRequiresObject() already defines instead of
    // tracking group membership as separate, driftable state.
    std::vector<CategoryButton*> sceneGroup, objectGroup;
    for (auto& cb : categoryButtons)
        (categoryRequiresObject (cb.category) ? objectGroup : sceneGroup).push_back (&cb);

    // trailingLabel (only the OBJECT SETTINGS group uses it, for
    // objectHeaderLabel -- see the class comment): shares the group
    // label's own row, right-aligned, instead of costing an extra row of
    // vertical space of its own.
    auto layoutButtonGroup = [&] (juce::Label& groupLabel, std::vector<CategoryButton*>& buttons, juce::Label* trailingLabel)
    {
        auto labelRow = bounds.removeFromTop (groupLabelHeight);
        if (trailingLabel != nullptr)
            trailingLabel->setBounds (labelRow.removeFromRight (labelRow.getWidth() / 2).reduced (UiSpacing::s, 0));
        groupLabel.setBounds (labelRow.reduced (UiSpacing::s, 0));

        const int numRows = (int) std::ceil ((double) buttons.size() / (double) buttonsPerRow);
        auto buttonGrid = bounds.removeFromTop (numRows * buttonHeight).reduced (UiSpacing::s, 0);
        for (int r = 0; r < numRows; ++r)
        {
            auto row = buttonGrid.removeFromTop (buttonHeight);
            const int buttonWidth = row.getWidth() / buttonsPerRow;
            for (int c = 0; c < buttonsPerRow; ++c)
            {
                const size_t idx = (size_t) (r * buttonsPerRow + c);
                if (idx >= buttons.size())
                    break;
                buttons[idx]->button->setBounds (row.removeFromLeft (buttonWidth).reduced (UiSpacing::xs));
            }
        }
        bounds.removeFromTop (UiSpacing::xs);
    };

    layoutButtonGroup (sceneGroupLabel, sceneGroup, nullptr);
    layoutButtonGroup (objectGroupLabel, objectGroup, &objectHeaderLabel);

    bounds.removeFromTop (UiSpacing::s);

    viewport.setBounds (bounds);
    layoutContent();
}

void ParameterPanel::paint (juce::Graphics& g)
{
    g.fillAll (UiColours::bgPanel());
    g.setColour (UiColours::border());
    g.fillRect (0, 0, 1, getHeight()); // left edge, separates the panel from the 3D viewport
}
