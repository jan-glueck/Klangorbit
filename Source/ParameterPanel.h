#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "SoundObject.h"
#include "SceneSettings.h"
#include "Grain.h"

/**
    A row widget: label + a slider, with a generic onValueChanged callback.
    Knows nothing about SoundObject or SceneSettings -- binding to a
    concrete field happens exclusively in ParameterPanel.
*/
class FloatRowComponent : public juce::Component
{
public:
    FloatRowComponent (const juce::String& name, double min, double max, double step);

    void resized() override;
    void setValueQuiet (float v);

    // Double-clicking the slider resets it to v (juce::Slider's own
    // built-in double-click-return mechanism -- no custom mouse handling
    // needed). Not called from this class's own constructor since the
    // "default" value is schema-specific (SoundObject{}.*member etc.) and
    // only ParameterPanel's add*Row() helpers know which member this row
    // is even bound to.
    void setDefaultValue (float v) { slider.setDoubleClickReturnValue (true, (double) v); }

    static constexpr int preferredHeight = 42;

    juce::Label label;
    juce::Slider slider;
    std::function<void (float)> onValueChanged;
};

/** Like FloatRowComponent, but for Vec3 (three axis sliders stacked vertically). */
class Vec3RowComponent : public juce::Component
{
public:
    Vec3RowComponent (const juce::String& name, double min, double max, double step);

    void resized() override;
    void setValueQuiet (Vec3 v);

    // See FloatRowComponent::setDefaultValue() -- same idea, one default
    // per axis.
    void setDefaultValue (Vec3 v)
    {
        xSlider.setDoubleClickReturnValue (true, (double) v.x);
        ySlider.setDoubleClickReturnValue (true, (double) v.y);
        zSlider.setDoubleClickReturnValue (true, (double) v.z);
    }

    static constexpr int preferredHeight = 18 + 3 * 22;

    juce::Label label, xLabel, yLabel, zLabel;
    juce::Slider xSlider, ySlider, zSlider;
    std::function<void (Vec3)> onValueChanged;

private:
    void setupAxis (juce::Label& l, juce::Slider& s, const juce::String& axisName,
                    double min, double max, double step);
};

/** Label + ComboBox, with a generic onSelected(itemIndex) callback. */
class ComboRowComponent : public juce::Component
{
public:
    explicit ComboRowComponent (const juce::String& name);

    void resized() override;

    static constexpr int preferredHeight = 44;

    juce::Label label;
    juce::ComboBox combo;
    std::function<void (int)> onSelected;
};

/** A single checkbox-style row, with a generic onToggled(newState) callback. */
class ToggleRowComponent : public juce::Component
{
public:
    explicit ToggleRowComponent (const juce::String& name);

    void resized() override;
    void setValueQuiet (bool v);

    static constexpr int preferredHeight = 28;

    juce::ToggleButton toggle;
    std::function<void (bool)> onToggled;
};

/**
    Side panel: a fixed row of category buttons at the top, grouped into
    two visually distinct sections (see resized()) -- "SCENE SETTINGS"
    (Scene, Acoustics) above "OBJECT SETTINGS" (Object, Attraction, Orbit,
    Doppler, Grains) below -- and a scrollable area beneath showing only
    the currently selected category's parameters, not one long list.
    objectHeaderLabel (which object, if any, is selected) shares the
    "OBJECT SETTINGS" label's own row, right-aligned, rather than costing
    a row of its own -- selection only matters to that group's pages, not
    to Scene/Acoustics, so it stays visually tied to that one label.

    Acoustics (speed of sound, temperature, humidity, pressure,
    propagation wind) is its own category, not folded into Scene -- both
    went through an earlier revision that merged them (same
    "not a single object's own property" reasoning
    `ParameterRegistry`'s own "Global" category string already draws, see
    `PluginProcessor.cpp`'s `buildParameterRegistry()`, still true of
    both), but a single flat Scene page mixing room/force-field settings
    with acoustic-medium settings read as more cluttered than two
    separate, still-both-under-"SCENE SETTINGS" tabs. Propagation Wind
    (`SceneSettings::windVector`) in particular lives here now rather
    than in Scene, alongside the rest of the acoustic-medium settings it
    conceptually belongs with -- separating it from Force Field
    (`SceneSettings::globalField`, stays in Scene), which is a real
    physical force on object movement, not an acoustic property at all.

    Output Format/Bass Management/Circular Array speaker count are
    deliberately NOT a category here at all -- they moved to their own
    dedicated OutputWindow (see OutputPanel.h), opened via its own toolbar
    button next to Mappings/Help, since they're plugin-wide settings tied
    to neither the scene nor any object, and get their own place rather
    than sharing space with either.

    Categories Object/Attraction/Orbit/Doppler/GrainCloud need a selected
    object; their buttons are disabled without one, and the panel force-
    switches away from them back to Scene if the selection is cleared
    while one of them is showing (so the panel never gets stuck showing a
    page with nothing behind it). categoryRequiresObject() is also what
    resized() uses to sort a button into the "SCENE SETTINGS" group vs.
    the "OBJECT SETTINGS" group -- one boolean partition serves both
    purposes, rather than tracking group membership as separate state
    that could drift out of sync with it.

    Writes directly to the SoundObject and SceneSettings pointers passed
    in -- same access rules as everywhere else in the GUI (message thread).

    refreshFromModel() synchronizes the controls with the current model
    state. Deliberately NOT called on a timer, only on selection changes
    and after loading a preset -- otherwise a slider would "snap back"
    while being dragged, due to the running physics/snapshot updates.
*/
class ParameterPanel : public juce::Component
{
public:
    enum class Category { Scene, Acoustics, Object, Attraction, Orbit, Doppler, GrainCloud };

    ParameterPanel();

    // Set once by the editor (never null again afterwards).
    void setSceneSettings (SceneSettings* settings);

    // nullptr = no selection. numObjects for the reference-object combo (orbit).
    void setEditedObject (SoundObject* obj, int objectIndexForHeader, int numObjects);

    // nullptr = no selection. Called alongside setEditedObject() by the
    // editor -- the GrainCloud belongs to the same selected object, but is
    // a separate settings struct owned by PluginProcessor, not a
    // SoundObject field.
    void setEditedGrainCloud (GrainCloudSettings* settings);

    // Synchronize all controls with the current model state.
    void refreshFromModel();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    FloatRowComponent& addSceneFloatRow (const juce::String& name, float SceneSettings::* member,
                                          double min, double max, double step, Category category);
    Vec3RowComponent& addSceneVec3Row (const juce::String& name, Vec3 SceneSettings::* member,
                                        double min, double max, double step, Category category);
    FloatRowComponent& addObjectFloatRow (const juce::String& name, float SoundObject::* member,
                                           double min, double max, double step, Category category);
    Vec3RowComponent& addObjectVec3Row (const juce::String& name, Vec3 SoundObject::* member,
                                         double min, double max, double step, Category category);
    // requiredMovementMode (default nullopt = always visible whenever the
    // GrainCloud category itself is showing): if set, this row is ALSO
    // hidden whenever editedGrainCloud->movementMode doesn't match, on top
    // of the ordinary category visibility check -- see
    // updateGrainMovementModeVisibility(). Lets e.g. "Boundary Radius
    // (Bounce)" hide itself while movementMode is RandomWalk, so the
    // Grains page only ever shows controls that actually do something for
    // the currently selected mode.
    FloatRowComponent& addGrainFloatRow (const juce::String& name, float GrainCloudSettings::* member,
                                          double min, double max, double step, Category category,
                                          std::optional<GrainMovementMode> requiredMovementMode = std::nullopt);
    // maxConcurrentGrains is the only int field -- reuses FloatRowComponent
    // (a whole-number slider) rather than a dedicated int row type for one field.
    FloatRowComponent& addGrainIntRow (const juce::String& name, int GrainCloudSettings::* member,
                                        double min, double max, Category category);
    void addToLayout (juce::Component& c, int height, Category category,
                       std::optional<GrainMovementMode> requiredMovementMode = std::nullopt);
    void addCategoryButton (const juce::String& label, Category category);

    void rebuildOrbitReferenceItems (int numObjects, int selfId);

    void selectCategory (Category category);
    void updateCategoryButtonsEnabled();
    void layoutContent(); // lays out only the currently visible rows within the viewport
    static bool categoryRequiresObject (Category category);

    // Shows/hides orbitModeHintLabel: visible only while the Orbit category
    // is showing AND the edited object's Mode isn't actually "Orbit" --
    // every other control in that category is inert until Mode is set, a
    // point of real confusion reported by a user who didn't realize the
    // Mode dropdown (Object category) is the actual switch, not anything
    // in the Orbit category itself. Called from selectCategory() (category
    // switched), setEditedObject()/refreshFromModel() (selection/preset
    // changed), and modeRow's onSelected (mode changed live).
    void updateOrbitModeHintVisibility();

    // Re-applies the requiredMovementMode gate (see addToLayout()) to
    // every already-visible GrainCloud-category row, hiding whichever
    // ones don't apply to editedGrainCloud->movementMode -- same set of
    // triggers as updateOrbitModeHintVisibility() above: selectCategory()
    // (category switched, e.g. into GrainCloud), setEditedGrainCloud()/
    // refreshFromModel() (selection/preset changed), and
    // grainMovementModeRow's onSelected (mode changed live).
    void updateGrainMovementModeVisibility();

    // Shows/hides pitchQuantizeScaleRow: visible only while the GrainCloud
    // category is showing AND editedGrainCloud->pitchJitterMode == Scale --
    // same "narrows what the generic category pass just made visible"
    // pattern as updateGrainMovementModeVisibility() above, on an
    // independent axis (pitch jitter mode, not movement mode). Called from
    // the same trigger points: selectCategory(), setEditedGrainCloud()/
    // refreshFromModel(), and pitchJitterModeRow's own onSelected.
    void updatePitchJitterModeVisibility();

    juce::Viewport viewport;
    juce::Component content;

    SceneSettings* sceneSettings = nullptr;
    SoundObject* editedObject = nullptr;
    GrainCloudSettings* editedGrainCloud = nullptr;

    Category currentCategory = Category::Scene;
    struct CategoryButton { std::unique_ptr<juce::TextButton> button; Category category; };
    std::vector<CategoryButton> categoryButtons;

    std::vector<juce::Component*> layoutOrder;
    std::vector<int> layoutHeights;
    std::vector<Category> layoutCategory;
    // Parallel to the three above -- see addToLayout()'s own comment.
    std::vector<std::optional<GrainMovementMode>> layoutRequiredMovementMode;

    struct SceneFloatBinding { std::unique_ptr<FloatRowComponent> row; float SceneSettings::* member; };
    std::vector<SceneFloatBinding> sceneFloatRows;

    struct SceneVec3Binding { std::unique_ptr<Vec3RowComponent> row; Vec3 SceneSettings::* member; };
    std::vector<SceneVec3Binding> sceneVec3Rows;

    struct ObjectFloatBinding { std::unique_ptr<FloatRowComponent> row; float SoundObject::* member; };
    std::vector<ObjectFloatBinding> objectFloatRows;

    struct ObjectVec3Binding { std::unique_ptr<Vec3RowComponent> row; Vec3 SoundObject::* member; };
    std::vector<ObjectVec3Binding> objectVec3Rows;

    struct GrainFloatBinding { std::unique_ptr<FloatRowComponent> row; float GrainCloudSettings::* member; };
    std::vector<GrainFloatBinding> grainFloatRows;

    struct GrainIntBinding { std::unique_ptr<FloatRowComponent> row; int GrainCloudSettings::* member; };
    std::vector<GrainIntBinding> grainIntRows;

    juce::Label objectHeaderLabel;
    // Sit above the (scrollable) category-button groups, see resized() --
    // small, dim section labels, not part of the scrollable content.
    juce::Label sceneGroupLabel;
    juce::Label objectGroupLabel;
    juce::Label orbitModeHintLabel; // see updateOrbitModeHintVisibility()
    std::unique_ptr<ComboRowComponent> modeRow;
    std::unique_ptr<Vec3RowComponent> positionRow; // SoundObject::position -- custom setter, see constructor for why
    std::unique_ptr<ComboRowComponent> boundaryRow;
    std::unique_ptr<ToggleRowComponent> showRoomBoundaryRow;
    std::unique_ptr<Vec3RowComponent> globalFieldRow; // SceneSettings::globalField, not a SoundObject field -> its own binding
    std::unique_ptr<ComboRowComponent> orbitRefRow;
    std::unique_ptr<ToggleRowComponent> dopplerEnabledRow; // SoundObject::dopplerEnabled -- top of the Doppler category
    std::unique_ptr<ComboRowComponent> directivityRow;

    std::unique_ptr<ToggleRowComponent> grainEnabledRow;
    std::unique_ptr<ToggleRowComponent> grainSourceMutedRow;
    std::unique_ptr<ToggleRowComponent> grainDopplerEnabledRow;
    std::unique_ptr<ComboRowComponent> grainWindowShapeRow;
    std::unique_ptr<ComboRowComponent> grainMovementModeRow;
    std::unique_ptr<ComboRowComponent> grainReadDepthDistributionRow;
    std::unique_ptr<ComboRowComponent> pitchJitterModeRow;
    // Visible only while pitchJitterModeRow == Scale, see
    // updatePitchJitterModeVisibility().
    std::unique_ptr<ComboRowComponent> pitchQuantizeScaleRow;
};
