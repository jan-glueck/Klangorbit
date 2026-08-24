#include "ParameterPanel.h"
#include "UiTheme.h"
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
    return category == Category::Object || category == Category::Attraction
        || category == Category::Orbit || category == Category::Doppler
        || category == Category::GrainCloud;
}

ParameterPanel::ParameterPanel()
{
    styleRowLabel (objectHeaderLabel, "No object selected", 15.0f, UiColours::textPrimary());
    objectHeaderLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)).withExtraKerningFactor (0.03f));
    addAndMakeVisible (objectHeaderLabel);

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    addCategoryButton ("Scene", Category::Scene);
    addCategoryButton ("Acoustics", Category::Acoustics);
    addCategoryButton ("Object", Category::Object);
    addCategoryButton ("Attraction", Category::Attraction);
    addCategoryButton ("Orbit", Category::Orbit);
    addCategoryButton ("Doppler", Category::Doppler);
    addCategoryButton ("Grains", Category::GrainCloud);

    // --- Scene ------------------------------------------------------------
    addSceneFloatRow ("Room Size (0 = no boundary)", &SceneSettings::roomSize, 0.0, 50.0, 0.1, Category::Scene);

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

    // Purely visual -- the boundary keeps applying physically even while
    // hidden, see SceneSettings::showRoomBoundary.
    showRoomBoundaryRow = std::make_unique<ToggleRowComponent> ("Show Boundary");
    showRoomBoundaryRow->onToggled = [this] (bool v) { if (sceneSettings != nullptr) sceneSettings->showRoomBoundary = v; };
    content.addAndMakeVisible (*showRoomBoundaryRow);
    addToLayout (*showRoomBoundaryRow, ToggleRowComponent::preferredHeight, Category::Scene);

    globalFieldRow = std::make_unique<Vec3RowComponent> ("Global Field (Wind/Gravity)", -20.0, 20.0, 0.01);
    globalFieldRow->onValueChanged = [this] (Vec3 v) { if (sceneSettings != nullptr) sceneSettings->globalField = v; };
    content.addAndMakeVisible (*globalFieldRow);
    addToLayout (*globalFieldRow, Vec3RowComponent::preferredHeight, Category::Scene);

    addSceneFloatRow ("Time Scale (timeScale)", &SceneSettings::timeScale, 0.05, 5.0, 0.01, Category::Scene);

    // --- Acoustics ----------------------------------------------------------
    addSceneFloatRow ("Speed of Sound (m/s)", &SceneSettings::speedOfSound, 1.0, 400.0, 1.0, Category::Acoustics);
    addSceneFloatRow ("Temperature (C)", &SceneSettings::temperature, -20.0, 45.0, 0.5, Category::Acoustics);
    addSceneFloatRow ("Relative Humidity (%)", &SceneSettings::relativeHumidity, 0.0, 100.0, 1.0, Category::Acoustics);
    addSceneFloatRow ("Atmospheric Pressure (kPa)", &SceneSettings::atmosphericPressure, 80.0, 110.0, 0.1, Category::Acoustics);
    addSceneVec3Row ("Wind (m/s)", &SceneSettings::windVector, -50.0, 50.0, 0.1, Category::Acoustics);

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
    modeRow->combo.addItem ("Attracted", 5);
    modeRow->onSelected = [this] (int index)
    {
        if (editedObject != nullptr)
            editedObject->mode = (SoundObject::Mode) index;
    };
    content.addAndMakeVisible (*modeRow);
    addToLayout (*modeRow, ComboRowComponent::preferredHeight, Category::Object);

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
    addObjectVec3Row ("Orbit Center (fixed point)", &SoundObject::orbitCenter, -20.0, 20.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Orbit Radius", &SoundObject::orbitRadius, 0.05, 10.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Orbit Angular Speed (rad/s)", &SoundObject::orbitAngularSpeed, -10.0, 10.0, 0.01, Category::Orbit);
    addObjectVec3Row ("Orbit Plane Normal", &SoundObject::orbitPlaneNormal, -1.0, 1.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Orbit Eccentricity", &SoundObject::orbitEccentricity, 0.0, 0.95, 0.01, Category::Orbit);
    addObjectFloatRow ("Orbit Decay (m/s)", &SoundObject::orbitDecay, -2.0, 2.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Radius Baseline (mean-reverting)", &SoundObject::orbitRadiusBaseline, 0.05, 10.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Radius Reversion Rate", &SoundObject::orbitRadiusReversionRate, 0.0, 5.0, 0.01, Category::Orbit);
    addObjectFloatRow ("Radius Noise Amplitude", &SoundObject::orbitRadiusNoiseAmplitude, 0.0, 5.0, 0.01, Category::Orbit);

    orbitRefRow = std::make_unique<ComboRowComponent> ("Orbit Reference Object");
    orbitRefRow->onSelected = [this] (int index)
    {
        if (editedObject != nullptr)
            editedObject->orbitReferenceObjectId = index - 1; // index 0 = "Fixed", see rebuildOrbitReferenceItems()
    };
    content.addAndMakeVisible (*orbitRefRow);
    addToLayout (*orbitRefRow, ComboRowComponent::preferredHeight, Category::Orbit);

    // --- Doppler ----------------------------------------------------------
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
    grainSourceMutedRow = std::make_unique<ToggleRowComponent> ("Grains Only (Mute Original Audio)");
    grainSourceMutedRow->onToggled = [this] (bool v) { if (editedGrainCloud != nullptr) editedGrainCloud->sourceMuted = v; };
    content.addAndMakeVisible (*grainSourceMutedRow);
    addToLayout (*grainSourceMutedRow, ToggleRowComponent::preferredHeight, Category::GrainCloud);

    // Default off -- grains skip PropagationProcessor entirely for
    // performance, so this is a much cheaper, coarser approximation (see
    // GrainDoppler.h), opt-in rather than silently changing existing
    // grain-cloud sound. Uses the object's own "Doppler Factor" (Doppler
    // category) to scale strength, same as main-object Doppler.
    grainDopplerEnabledRow = std::make_unique<ToggleRowComponent> ("Doppler (uses object's Doppler Factor)");
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

    addGrainIntRow ("Max Concurrent Grains (this cloud)", &GrainCloudSettings::maxConcurrentGrains, 1.0, 128.0, Category::GrainCloud);

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
    };
    content.addAndMakeVisible (*grainMovementModeRow);
    addToLayout (*grainMovementModeRow, ComboRowComponent::preferredHeight, Category::GrainCloud);

    addGrainFloatRow ("Random Walk Speed (m/s)", &GrainCloudSettings::randomWalkSpeed, 0.0, 10.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Boundary Radius (m, Bounce)", &GrainCloudSettings::boundaryRadius, 0.05, 5.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Boundary Radius Jitter (Bounce)", &GrainCloudSettings::boundaryRadiusJitter, 0.0, 1.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Restitution (Bounce)", &GrainCloudSettings::restitution, 0.0, 1.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Initial Speed (m/s, Explosion)", &GrainCloudSettings::initialSpeed, 0.0, 20.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Initial Speed Jitter (Explosion)", &GrainCloudSettings::initialSpeedJitter, 0.0, 1.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Acceleration (m/s^2, Explosion)", &GrainCloudSettings::acceleration, -20.0, 20.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Orbit Radius (m, OrbitAroundParent)", &GrainCloudSettings::orbitRadius, 0.05, 5.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Orbit Radius Jitter (OrbitAroundParent)", &GrainCloudSettings::orbitRadiusJitter, 0.0, 1.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Orbit Angular Speed (rad/s)", &GrainCloudSettings::orbitAngularSpeed, -10.0, 10.0, 0.01, Category::GrainCloud);
    addGrainFloatRow ("Attraction Strength (Siblings)", &GrainCloudSettings::attractionStrength, -10.0, 10.0, 0.01, Category::GrainCloud);

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
    content.addAndMakeVisible (*row);
    addToLayout (*row, Vec3RowComponent::preferredHeight, category);

    objectVec3Rows.push_back ({ std::move (row), member });
    return *objectVec3Rows.back().row;
}

FloatRowComponent& ParameterPanel::addGrainFloatRow (const juce::String& name, float GrainCloudSettings::* member,
                                                       double min, double max, double step, Category category)
{
    auto row = std::make_unique<FloatRowComponent> (name, min, max, step);
    row->onValueChanged = [this, member] (float v) { if (editedGrainCloud != nullptr) editedGrainCloud->*member = v; };
    content.addAndMakeVisible (*row);
    addToLayout (*row, FloatRowComponent::preferredHeight, category);

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
    content.addAndMakeVisible (*row);
    addToLayout (*row, FloatRowComponent::preferredHeight, category);

    grainIntRows.push_back ({ std::move (row), member });
    return *grainIntRows.back().row;
}

void ParameterPanel::addToLayout (juce::Component& c, int height, Category category)
{
    layoutOrder.push_back (&c);
    layoutHeights.push_back (height);
    layoutCategory.push_back (category);
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

    viewport.setViewPosition (0, 0);
    layoutContent();
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
        return;
    }

    objectHeaderLabel.setText ("Object " + juce::String (objectIndexForHeader), juce::dontSendNotification);
    updateCategoryButtonsEnabled();

    rebuildOrbitReferenceItems (numObjects, obj->id);
    refreshFromModel();
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

    modeRow->combo.setSelectedItemIndex ((int) editedObject->mode, juce::dontSendNotification);
    orbitRefRow->combo.setSelectedItemIndex (editedObject->orbitReferenceObjectId + 1, juce::dontSendNotification);
    directivityRow->combo.setSelectedItemIndex ((int) editedObject->directivityPattern, juce::dontSendNotification);

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

    objectHeaderLabel.setBounds (bounds.removeFromTop (36).reduced (UiSpacing::m, 0));
    bounds.removeFromTop (UiSpacing::xs);

    constexpr int buttonHeight = 32;
    constexpr int buttonsPerRow = 2;
    const int numRows = (int) std::ceil ((double) categoryButtons.size() / (double) buttonsPerRow);

    auto buttonGrid = bounds.removeFromTop (numRows * buttonHeight).reduced (UiSpacing::s, 0);
    for (int r = 0; r < numRows; ++r)
    {
        auto row = buttonGrid.removeFromTop (buttonHeight);
        const int buttonWidth = row.getWidth() / buttonsPerRow;
        for (int c = 0; c < buttonsPerRow; ++c)
        {
            const size_t idx = (size_t) (r * buttonsPerRow + c);
            if (idx >= categoryButtons.size())
                break;
            categoryButtons[idx].button->setBounds (row.removeFromLeft (buttonWidth).reduced (UiSpacing::xs));
        }
    }

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
