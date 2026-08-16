#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "SoundObject.h"
#include "SceneSettings.h"

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

    static constexpr int preferredHeight = 36;

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

    static constexpr int preferredHeight = 16 + 3 * 20;

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

    static constexpr int preferredHeight = 38;

    juce::Label label;
    juce::ComboBox combo;
    std::function<void (int)> onSelected;
};

/**
    Scrollable side panel: scene parameters (always visible/editable) and
    the parameters of the object currently selected in the 2D view.

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
    ParameterPanel();

    // Set once by the editor (never null again afterwards).
    void setSceneSettings (SceneSettings* settings);

    // nullptr = no selection. numObjects for the reference-object combo (orbit).
    void setEditedObject (SoundObject* obj, int objectIndexForHeader, int numObjects);

    // Synchronize all controls with the current model state.
    void refreshFromModel();

    void resized() override;

private:
    FloatRowComponent& addSceneFloatRow (const juce::String& name, float SceneSettings::* member,
                                          double min, double max, double step);
    Vec3RowComponent& addSceneVec3Row (const juce::String& name, Vec3 SceneSettings::* member,
                                        double min, double max, double step);
    FloatRowComponent& addObjectFloatRow (const juce::String& name, float SoundObject::* member,
                                           double min, double max, double step);
    Vec3RowComponent& addObjectVec3Row (const juce::String& name, Vec3 SoundObject::* member,
                                         double min, double max, double step);
    juce::Label& addHeader (const juce::String& text);
    void addToLayout (juce::Component& c, int height);

    void rebuildOrbitReferenceItems (int numObjects, int selfId);

    juce::Viewport viewport;
    juce::Component content;

    SceneSettings* sceneSettings = nullptr;
    SoundObject* editedObject = nullptr;

    std::vector<juce::Component*> layoutOrder;
    std::vector<int> layoutHeights;

    struct SceneFloatBinding { std::unique_ptr<FloatRowComponent> row; float SceneSettings::* member; };
    std::vector<SceneFloatBinding> sceneFloatRows;

    struct SceneVec3Binding { std::unique_ptr<Vec3RowComponent> row; Vec3 SceneSettings::* member; };
    std::vector<SceneVec3Binding> sceneVec3Rows;

    struct ObjectFloatBinding { std::unique_ptr<FloatRowComponent> row; float SoundObject::* member; };
    std::vector<ObjectFloatBinding> objectFloatRows;

    struct ObjectVec3Binding { std::unique_ptr<Vec3RowComponent> row; Vec3 SoundObject::* member; };
    std::vector<ObjectVec3Binding> objectVec3Rows;

    std::vector<std::unique_ptr<juce::Label>> headers;

    juce::Label objectHeaderLabel;
    std::unique_ptr<ComboRowComponent> modeRow;
    std::unique_ptr<ComboRowComponent> boundaryRow;
    std::unique_ptr<Vec3RowComponent> globalFieldRow; // SceneSettings::globalField, not a SoundObject field -> its own binding
    std::unique_ptr<ComboRowComponent> orbitRefRow;
    std::unique_ptr<ComboRowComponent> directivityRow;

    // Everything that only makes sense while an object is selected -- gets
    // enabled/disabled in setEditedObject().
    std::vector<juce::Component*> objectOnlyComponents;
};
