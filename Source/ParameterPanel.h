#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "SoundObject.h"
#include "SceneSettings.h"
#include "Grain.h"
#include "AmbisonicsDecoder.h"

class KlangorbitProcessor;

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
    two visually distinct sections (see resized()) -- "Scene / Output"
    (Scene, which now also holds the former separate Acoustics fields --
    speed of sound, temperature, humidity, pressure, wind -- room/global-
    field/time-scale settings and per-object acoustic-propagation
    settings both being "not a single object's own property" is exactly
    the distinction ParameterRegistry's own category strings already draw
    (both register under "Global", see PluginProcessor.cpp's
    buildParameterRegistry()) -- and Output) above "Selected Object"
    (Object, Attraction, Orbit, Doppler, Grains) below -- and a scrollable
    area beneath showing only the currently selected category's
    parameters, not one long list.

    Categories Object/Attraction/Orbit/Doppler/GrainCloud need a selected
    object; their buttons are disabled without one, and the panel force-
    switches away from them back to Scene if the selection is cleared
    while one of them is showing (so the panel never gets stuck showing a
    page with nothing behind it). categoryRequiresObject() is also what
    resized() uses to sort a button into the "Scene / Output" group vs.
    the "Selected Object" group -- one boolean partition serves both
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
    enum class Category { Scene, Output, Object, Attraction, Orbit, Doppler, GrainCloud };

    ParameterPanel();

    // Set once by the editor (never null again afterwards).
    void setSceneSettings (SceneSettings* settings);

    // Set once by the editor (never null again afterwards) -- for the
    // Output category's decoder-mode/bass-management controls, which live
    // on the processor itself rather than a settings struct (see
    // KlangorbitProcessor::setDecoderMode()).
    void setProcessor (KlangorbitProcessor* proc);

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
    FloatRowComponent& addGrainFloatRow (const juce::String& name, float GrainCloudSettings::* member,
                                          double min, double max, double step, Category category);
    // maxConcurrentGrains is the only int field -- reuses FloatRowComponent
    // (a whole-number slider) rather than a dedicated int row type for one field.
    FloatRowComponent& addGrainIntRow (const juce::String& name, int GrainCloudSettings::* member,
                                        double min, double max, Category category);
    void addToLayout (juce::Component& c, int height, Category category);
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

    juce::Viewport viewport;
    juce::Component content;

    SceneSettings* sceneSettings = nullptr;
    SoundObject* editedObject = nullptr;
    GrainCloudSettings* editedGrainCloud = nullptr;
    KlangorbitProcessor* decoderProcessor = nullptr;

    Category currentCategory = Category::Scene;
    struct CategoryButton { std::unique_ptr<juce::TextButton> button; Category category; };
    std::vector<CategoryButton> categoryButtons;

    std::vector<juce::Component*> layoutOrder;
    std::vector<int> layoutHeights;
    std::vector<Category> layoutCategory;

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
    // Sits WITHIN the scrollable Scene page's content, between the
    // room/time-scale rows and the former-Acoustics-category rows now
    // folded into the same page -- see the class comment.
    juce::Label acousticsSectionLabel;
    juce::Label orbitModeHintLabel; // see updateOrbitModeHintVisibility()
    juce::Label decoderModeHintLabel;
    std::unique_ptr<ComboRowComponent> decoderModeRow;
    std::unique_ptr<FloatRowComponent> circularSpeakerCountRow;
    juce::Label circularArrayHintLabel;
    std::unique_ptr<ToggleRowComponent> bassManagementRow;
    std::unique_ptr<ComboRowComponent> modeRow;
    std::unique_ptr<ComboRowComponent> boundaryRow;
    std::unique_ptr<ToggleRowComponent> showRoomBoundaryRow;
    std::unique_ptr<Vec3RowComponent> globalFieldRow; // SceneSettings::globalField, not a SoundObject field -> its own binding
    std::unique_ptr<ComboRowComponent> orbitRefRow;
    std::unique_ptr<ComboRowComponent> directivityRow;

    std::unique_ptr<ToggleRowComponent> grainEnabledRow;
    std::unique_ptr<ToggleRowComponent> grainSourceMutedRow;
    std::unique_ptr<ToggleRowComponent> grainDopplerEnabledRow;
    std::unique_ptr<ComboRowComponent> grainWindowShapeRow;
    std::unique_ptr<ComboRowComponent> grainMovementModeRow;
    std::unique_ptr<ComboRowComponent> grainReadDepthDistributionRow;
};
